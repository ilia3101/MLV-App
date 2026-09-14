<#
.SYNOPSIS
    Invoke one fleet lane as a bounded CLI process. No seat, no lease, no registry.

.DESCRIPTION
    Claude-driven fleet topology (adopted 2026-08-29, Layi's ruling). Replaces the
    Codex-Desktop scheduled-automation lane model, under which the board spent
    seven heartbeat rotations in three days and landed zero product changes.

    THE CENTRAL DESIGN CHANGE: a lane is a PROCESS, not a SEAT.

    The prior topology simulated liveness in documents - seat-registry.json,
    health/leases/*.json, durableTaskId, automation.toml target_thread_id - and
    every one of those could disagree with reality. A lane was "alive" because a
    file said so, and the board deadlocked when the file was stale (LIVENESS-KEY-1,
    BOARD-DARK-1, and the v13-r15 transition-receipt block that left the hub dark
    for 53 hours).

    A process needs none of it. It is alive because it is running and dead when it
    exits, and the exit code is the truth. So this script:
      - takes NO lease and renews nothing
      - writes NO shared coordination state (no registry, no queue, no pen)
      - therefore requires NO seat and has no succession gate to pass

    The audit record is a per-invocation RECEIPT. Receipts accumulate; nothing is
    ever renewed or reconciled.

    WHAT THE RECEIPT ACTUALLY GUARANTEES - stated narrowly on purpose, because an
    earlier version of this header implied a durability the code did not provide:
      - its slot is reserved ATOMICALLY before any work starts, so concurrent
        lanes cannot overwrite each other's evidence
      - it is written from a finally block, so it exists on every exit path FROM
        THE RESERVATION ONWARD, including a throw; `complete` and `failure` say
        which path was taken. Argument-validation failures BEFORE the reservation
        (bad -PromptFile, unresolvable -WorkDir) produce NO receipt - correctly,
        because no slot was taken and no work was attempted. Verified by
        injecting a post-reservation throw: receipt written, complete=false,
        failure carried, exitCode -999.
      - prompt and output are hashed, so the record cannot drift from what ran
    It does NOT guarantee anything about a host that dies mid-write, and it makes
    no claim of immutability - a later run with the same explicit -RunDir and a
    freed slot could reuse the name.

.NOTES
    ASCII-only by project convention (non-ASCII in .ps1 has broken parsing here).
    Receipts are UTF-8 without BOM for the same reason.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidateSet('opus', 'sonnet', 'fable', 'sol', 'luna')]
    [string]$Lane,

    [string]$Prompt,
    [string]$PromptFile,

    # Working root the lane reasons about. Defaults to the repo this script lives in.
    [string]$WorkDir,

    # Where receipts and outputs land. Defaults to a timestamped run dir.
    [string]$RunDir,

    [int]$TimeoutSec = 900,

    # Grant the lane write access. OFF by default: an analysis or review lane that
    # cannot mutate the tree cannot corrupt the thing it is judging.
    [switch]$AllowEdits,

    # Free-text tag recorded in the receipt (e.g. the card id).
    [string]$Card = '',

    # Backstop against a runaway lane. Claude only (codex exec has no equivalent).
    # 0 disables the cap. Measured 2026-09-03: real lanes used 13-21 turns, so 40 is a
    # runaway guard, NOT the spend control - that is -DenyBulkReads below.
    [int]$MaxTurns = 40,

    # Optional per-process override; never changes the user's provider settings.
    [ValidateSet('', 'low', 'medium', 'high')]
    [string]$ReasoningEffort = '',

    # Let the lane read the bulk coordination files. OFF by default.
    # MEASURED 2026-09-03: three unattended fable lanes cost USD 22-25 EACH, every one
    # burning ~970,000 cache-creation tokens - almost exactly the 1.6 MB coordination
    # surface. Invoke-Workstream.ps1 already reads that bulk in PowerShell and inlines
    # the facts into a 4-7 KB brief, so a lane re-reading it pays full price for context
    # it was already given. A compact brief bounds the PROMPT; it does not bound the
    # READING, and nothing here did until now.
    [switch]$AllowBulkReads,

    # 0.1: the explicit tool allowlist an editing lane is granted. REQUIRED with
    # -AllowEdits; 'ALL' is never accepted (allowlist-required). Comma-separated,
    # passed straight through to claude's --allowedTools.
    [string]$AllowedTools = '',

    # 0.1: an extra directory the lane may read beyond -WorkDir (e.g. board
    # coordination paths an editing lane needs without a full -AllowBulkReads
    # grant). Optional; claude engine only (--add-dir).
    [string]$ExtraReadDir = '',

    # Disk hygiene (2026-09-14): when the lane exits, retire -WorkDir if it is a linked
    # worktree that passes the SAFE gate in Retire-LaneWorktree.ps1; otherwise keep it.
    # Either way the receipt carries `worktreeDisposition` saying which and why.
    # Never applies to the main checkout, and never deletes a branch ref.
    [switch]$RetireWorktree,

    # Lane scratch. The child's TEMP/TMP point at <ScratchRoot>\<lane>-NNN, never bare
    # %TEMP% (which every project on this box shares). C:\mlvtmp is a DiskGuard-registered
    # MLV root. The run's own scratch dir is removed when the lane exits unless -KeepScratch;
    # its size is recorded in the receipt either way.
    [string]$ScratchRoot = 'C:\mlvtmp\lane-scratch',
    [switch]$KeepScratch
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

# ---------------------------------------------------------------- lane table
# Engine, model and effort per lane. This table IS the topology; there is no
# other place a lane's model is decided, so a lane cannot silently drift onto
# the wrong tier the way a registry field could.
$LANES = @{
    opus   = @{ engine = 'claude'; model = 'opus';           effort = 'high';   role = 'orchestrator' }
    sonnet = @{ engine = 'claude'; model = 'sonnet';         effort = '';       role = 'implementer' }
    fable  = @{ engine = 'claude'; model = 'claude-fable-5'; effort = '';       role = 'review-guidance-planning' }
    sol    = @{ engine = 'codex';  model = 'gpt-5.6-sol';    effort = 'high';   role = 'adversarial-verifier' }
    luna   = @{ engine = 'codex';  model = 'gpt-5.6-luna';   effort = 'high';   role = 'breadth-recon' }
}

# Absolute launcher paths. NEITHER is on the Git Bash PATH on this host, and a
# bare name resolves differently per shell - measured 2026-08-29.
$CLAUDE_EXE = Join-Path $env:APPDATA 'npm\claude.cmd'
$CODEX_EXE  = Join-Path $env:APPDATA 'npm\codex.cmd'

function Get-Sha256([string]$Text) {
    $sha = [System.Security.Cryptography.SHA256]::Create()
    try {
        $bytes = [System.Text.Encoding]::UTF8.GetBytes($Text)
        return ([BitConverter]::ToString($sha.ComputeHash($bytes)) -replace '-', '')
    } finally { $sha.Dispose() }
}

function Write-Utf8NoBom([string]$Path, [string]$Content) {
    # UTF-8 WITHOUT BOM: a BOM has broken JSON consumers on this box before.
    [System.IO.File]::WriteAllText($Path, $Content, [System.Text.UTF8Encoding]::new($false))
}

function Write-Utf8NoBomAtomic([string]$Path, [string]$Content) {
    $tmp = "$Path.$([guid]::NewGuid().ToString('N')).tmp"
    try {
        [IO.File]::WriteAllText($tmp, $Content, [Text.UTF8Encoding]::new($false))
        [IO.File]::Move($tmp, $Path, $true)
    } finally {
        if (Test-Path -LiteralPath $tmp) { Remove-Item -LiteralPath $tmp -Force }
    }
}

# Windows job ownership is established around an inert PowerShell host before that
# host receives any provider configuration. Descendants then inherit kill-on-close.
if (-not ('MlvLaneJob' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.ComponentModel;
using System.Runtime.InteropServices;
public static class MlvLaneJob {
  [StructLayout(LayoutKind.Sequential)] struct IO_COUNTERS { public UInt64 a,b,c,d,e,f; }
  [StructLayout(LayoutKind.Sequential)] struct BASIC_LIMIT {
    public Int64 PerProcessUserTimeLimit, PerJobUserTimeLimit;
    public UInt32 LimitFlags;
    public UIntPtr MinimumWorkingSetSize, MaximumWorkingSetSize;
    public UInt32 ActiveProcessLimit;
    public UIntPtr Affinity;
    public UInt32 PriorityClass, SchedulingClass;
  }
  [StructLayout(LayoutKind.Sequential)] struct EXTENDED_LIMIT {
    public BASIC_LIMIT BasicLimitInformation;
    public IO_COUNTERS IoInfo;
    public UIntPtr ProcessMemoryLimit, JobMemoryLimit, PeakProcessMemoryUsed, PeakJobMemoryUsed;
  }
  [DllImport("kernel32.dll", CharSet=CharSet.Unicode, SetLastError=true)]
  static extern IntPtr CreateJobObject(IntPtr attributes, string name);
  [DllImport("kernel32.dll", SetLastError=true)]
  static extern bool SetInformationJobObject(IntPtr job, int infoClass, IntPtr info, uint length);
  [DllImport("kernel32.dll", SetLastError=true)]
  public static extern bool AssignProcessToJobObject(IntPtr job, IntPtr process);
  [DllImport("kernel32.dll", SetLastError=true)]
  public static extern bool CloseHandle(IntPtr handle);
  public static IntPtr CreateKillOnClose() {
    IntPtr job=CreateJobObject(IntPtr.Zero, null);
    if(job==IntPtr.Zero) throw new Win32Exception(Marshal.GetLastWin32Error(), "CreateJobObject");
    EXTENDED_LIMIT value=new EXTENDED_LIMIT();
    value.BasicLimitInformation.LimitFlags=0x2000;
    int size=Marshal.SizeOf(value); IntPtr mem=Marshal.AllocHGlobal(size);
    try {
      Marshal.StructureToPtr(value,mem,false);
      if(!SetInformationJobObject(job,9,mem,(uint)size)) {
        int error=Marshal.GetLastWin32Error(); CloseHandle(job);
        throw new Win32Exception(error,"SetInformationJobObject");
      }
    } finally { Marshal.FreeHGlobal(mem); }
    return job;
  }
  public static void AssignOrThrow(IntPtr job, IntPtr process) {
    if(!AssignProcessToJobObject(job,process))
      throw new Win32Exception(Marshal.GetLastWin32Error(),"AssignProcessToJobObject");
  }
}
'@
}

# ---------------------------------------------------------------- resolve inputs
if (-not $Prompt -and -not $PromptFile) { throw 'Supply -Prompt or -PromptFile.' }
if ($PromptFile) {
    if (-not (Test-Path -LiteralPath $PromptFile)) { throw "PromptFile not found: $PromptFile" }
    $Prompt = Get-Content -LiteralPath $PromptFile -Raw
}

if (-not $WorkDir) { $WorkDir = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..\..')).Path }
$WorkDir = (Resolve-Path -LiteralPath $WorkDir).Path

# ---------------------------------------------------------------- 0.1 pre-flight (before any process, before any run dir)
# (a) A codex lane (sol, luna) can never be granted write access: no Claude hook is
# visible to codex exec, so nothing here could enforce NA-1..NA-10 against it.
if ($AllowEdits -and ($Lane -eq 'sol' -or $Lane -eq 'luna')) {
    throw "codex-lane-never-edits: -Lane $Lane with -AllowEdits (no Claude hook is visible to codex exec)"
}
# (b) An editing lane's tool grant must be an explicit, auditable list. 'ALL' is
# never accepted - that is the exact grant this whole patch exists to narrow.
if ($AllowEdits -and ([string]::IsNullOrWhiteSpace($AllowedTools) -or $AllowedTools -eq 'ALL')) {
    throw "allowlist-required: -AllowEdits requires -AllowedTools <comma-separated list>; 'ALL' is never granted"
}
# Nested agent dispatch is forbidden even when embedded in a caller-supplied editing
# allowlist. Normalize comma tokens for the decision; preserve the original argv text.
if ($AllowEdits) {
    $forbiddenTools = @($AllowedTools -split ',' | ForEach-Object { $_.Trim().ToLowerInvariant() } |
        Where-Object { $_ -eq 'agent' -or $_ -eq 'task' })
    if ($forbiddenTools.Count -gt 0) {
        throw "nested-agent-tool-forbidden: -AllowedTools cannot contain Agent or Task"
    }
}
# MLV_BOARD_ROOT: only a test sets it (a tmp-dir board fixture); the default is the
# real board (mirrors Start-EditingLane.ps1's own resolution, O107).
$RepoRoot = if ($env:MLV_BOARD_ROOT) { $env:MLV_BOARD_ROOT } else { 'C:\!Layi Wkspc\MLV-App' }
$HookEnforcedReceipt = Join-Path $RepoRoot '.claude-state\coordination\dual-lane\receipts\0.05-hook-enforced.json'
$WorkDirHookPath     = Join-Path $WorkDir 'tools\hooks\mlv-never-authorized.py'
# hookSha256 is computed unconditionally (when the file exists) so the receipt can
# always carry it, per 0.1's receipt-fields requirement - not only on editing lanes.
$WorkDirHookSha256 = if (Test-Path -LiteralPath $WorkDirHookPath) {
    (Get-FileHash -LiteralPath $WorkDirHookPath -Algorithm SHA256).Hash.ToLowerInvariant()
} else { $null }
# (e) The worktree's OWN hook copy is what actually governs an editing lane's
# session (Claude Code loads it from -WorkDir), so this checks THAT copy against
# the board's receipt of what the ratified hook hashes to - never the board root's
# own copy, which the lane never runs against.
if ($AllowEdits) {
    if (-not (Test-Path -LiteralPath $HookEnforcedReceipt)) {
        throw "hook-not-enforced: receipt missing at $HookEnforcedReceipt"
    }
    if ($null -eq $WorkDirHookSha256) {
        throw "hook-not-enforced: hook script missing in worktree: $WorkDirHookPath"
    }
    $hookEnforcedRec = Get-Content -LiteralPath $HookEnforcedReceipt -Raw | ConvertFrom-Json
    $receiptHookSha256 = ([string]$hookEnforcedRec.hookSha256).ToLowerInvariant()
    if ($WorkDirHookSha256 -ne $receiptHookSha256) {
        throw ("hook-not-enforced: worktree hook sha256={0} != receipt hookSha256={1}" -f $WorkDirHookSha256, $receiptHookSha256)
    }
}
$BaseSha = try {
    (& git -C $WorkDir rev-parse HEAD 2>$null | Select-Object -First 1)
} catch { $null }
if ([string]::IsNullOrWhiteSpace($BaseSha)) { $BaseSha = $null }

if (-not $RunDir) {
    $stamp = (Get-Date).ToUniversalTime().ToString('yyyyMMddTHHmmssZ')
    $RunDir = Join-Path $WorkDir ".claude-state\fleet-runs\$stamp"
}
if (-not (Test-Path -LiteralPath $RunDir)) { New-Item -ItemType Directory -Path $RunDir -Force | Out-Null }
$RunDir = (Resolve-Path -LiteralPath $RunDir).Path

$cfg = $LANES[$Lane].Clone()
if ($ReasoningEffort) { $cfg.effort = $ReasoningEffort }

# ATOMIC SLOT RESERVATION. The previous form was
#     while (Test-Path <candidate>) { $n++ }
# which is CHECK-THEN-ACT: two lanes launched concurrently into the same run dir
# both see the slot free, both pick it, and the second silently overwrites the
# first one's evidence. This session ran concurrent lanes, so the race was live.
# FileMode::CreateNew is a SINGLE filesystem operation that FAILS if the path
# already exists, so the winner is decided by the OS, not by our timing.
$base = $null
for ($n = 1; $n -le 999; $n++) {
    $candidate = Join-Path $RunDir ("{0}-{1:d3}" -f $Lane, $n)
    try {
        $fs = [System.IO.File]::Open(
            "$candidate.receipt.json",
            [System.IO.FileMode]::CreateNew,
            [System.IO.FileAccess]::Write,
            [System.IO.FileShare]::None)
        $fs.Dispose()
        $base = $candidate
        break
    } catch [System.IO.IOException] {
        continue   # someone else owns this slot; try the next
    }
}
if ($null -eq $base) { throw "Could not reserve a receipt slot in $RunDir after 999 attempts." }
$promptPath = "$base.prompt.txt"
$outPath    = "$base.out.txt"
$errPath    = "$base.err.txt"
$lastPath   = "$base.last.txt"
$rcptPath   = "$base.receipt.json"
$settingsPath = "$base.settings.json"

# RESERVED MARKER, written the instant the slot is taken and before ANY work.
# The atomic reservation above creates an EMPTY file, which is a valid claim but
# INVALID JSON - anything reading receipts while a lane is in flight crashes on
# it, and a crash between reservation and completion would leave that empty file
# as the only record. This is PEN-GAP-1's concern in receipt form: durable
# mutation must not precede durable explanation. The slot now always parses and
# always says what it is.
Write-Utf8NoBom $rcptPath (([ordered]@{
    schema   = 'mlv-app/fleet-lane-receipt/v1'
    state    = 'reserved'
    complete = $false
    lane     = $Lane
    card     = $Card
    reservedUtc = (Get-Date).ToUniversalTime().ToString('o')
} | ConvertTo-Json -Depth 4))

# CRASH-TOTAL RECEIPT. Every field the receipt needs is initialised BEFORE the
# try, so a failure anywhere still produces a well-formed receipt instead of
# nothing. The old code wrote the receipt only on the normal tail, outside any
# try/finally, with $ErrorActionPreference='Stop' - so any throw yielded ZERO
# receipt and a failed run was indistinguishable from one that never ran. That
# is the exact defect this runner's own card list called SWEEP-RECEIPT-1.
$startedUtc = (Get-Date).ToUniversalTime()
$sw         = [System.Diagnostics.Stopwatch]::StartNew()
$exitCode   = -999
$timedOut   = $false
$final      = ''
$failure    = $null
# Initialized here, before the try, so the finally never resolves a parent-scope $scratchDir
# (dynamic scoping) and removes a path this run did not create.
$scratchDir = $null
# A PROVIDER REFUSAL is a third outcome beside ran/threw: the child exited cleanly and
# the provider did no work. Detected from raw output after harvest; see lane-provider-refusal.ps1.
$providerRefusal = $null
$authority  = [ordered]@{ permissionMode = 'unset'; allowedTools = 'unset'; sandbox = 'unset'; writableRoot = $null }
$denyRules  = @()
$jobHandle = [IntPtr]::Zero
$jobAssigned = $false
$promptDelivered = $false
$containedHost = $null
# Set to $true the INSTANT Process::Start returns for the claude engine (a plain
# boolean assignment cannot throw), BEFORE the pid/dictionary construction that
# builds $containedHost -- which CAN throw (PR #105 round 4). $containedHost alone
# cannot tell "Start was never called/never returned" apart from "Start returned
# but the record of it never got built"; $hostStarted can, because it is set
# unconditionally the moment a host process exists.
$hostStarted = $false
# Captured on its OWN line the instant Start returns (PR #105 round 5, this
# packet): a bare property read on a live Process, which the .NET contract
# binds before Start returns and which therefore cannot throw. Kept separate
# from $containedHost (whose own dictionary/build CAN still throw) so the
# post-start-unrecorded fallback always has a pid to report -- the pid is the
# only channel by which anyone later learns an orphan existed.
$hostPid = $null
# Fixed tokens for containment.ownerAbsentReason, chosen by WHERE the failure
# happened rather than by what its exception message said -- free text is not
# admissible evidence about a safety property (PR #105 round 4). Kept in one
# place; tests/coordination/test_lane_containment.py pins these as literals
# since it cannot import a .ps1 file, with a comment pointing back here.
$OWNER_ABSENT_NO_HOST_BUDGET = 'launch-budget-exhausted'  # unchanged text: line ~471's throw message, asserted verbatim since PR #105 round 3
$OWNER_ABSENT_NO_HOST_START_THREW = 'start-threw'         # Process::Start itself threw; no host was ever created
$OWNER_ABSENT_POST_START_UNRECORDED = 'post-start-unrecorded'  # Start returned (a host EXISTS) but $containedHost's own construction threw -- the genuinely ambiguous state
# Outcome of the PRE-ASSIGNMENT kill at line ~677 (host started but never
# joined the job, so a swallowed kill failure there is a genuine orphan, unlike
# the post-timeout kill at line ~602 where the job is kill-on-close and the
# tree is already terminated). A kill cannot be made infallible, but its
# failure can be made visible instead of vanishing into a bare `catch { }`
# (PR #105 round 5, sol PR #105 blocker).
$ownerKillAttempted = $false
$ownerKillOutcome = $null
$ownerKillDetail = $null
$OWNER_KILL_OUTCOME_ALREADY_EXITED = 'already-exited'  # host had already exited before the pre-assignment kill was attempted
$OWNER_KILL_OUTCOME_KILLED = 'killed'                  # Kill() did not throw AND WaitForExit's own return value was $true -- the host was OBSERVED to exit within the bounded wait
$OWNER_KILL_OUTCOME_KILL_WAIT_TIMEOUT = 'kill-wait-timeout'  # Kill() did not throw, but WaitForExit's return value was $false -- the bounded wait expired before the host was observed to exit. It MAY STILL BE ALIVE. (PR #105 final: WaitForExit(Int32) returns a bool and round 5 discarded it with [void], so a host that outlived the wait was misreported as 'killed' -- worse than the bare catch{} it replaced, since it manufactured false evidence instead of merely omitting true evidence.)
$OWNER_KILL_OUTCOME_KILL_THREW = 'kill-threw'          # Kill() or WaitForExit itself threw -- see ownerKillDetail for the raw message
$childIdentity = $null
$deadlineUtc = $startedUtc.AddSeconds($TimeoutSec)
$containment = $null
$proc = $null
# $null, never 0. An engine that does not REPORT cost and a run that cost nothing are
# different facts and this receipt will not merge them - the same rule the dispatcher's
# malformed-row counter follows.
$costUsd = $null; $numTurns = $null
$cacheCreateTokens = $null; $cacheReadTokens = $null; $outputTokens = $null
. (Join-Path $PSScriptRoot 'lane-provider-refusal.ps1')

try {

Write-Utf8NoBom $promptPath $Prompt

# ---------------------------------------------------------------- build argv
# Argument ARRAYS, never a concatenated string: this repo has paths with spaces
# and a '!' in them, and string-built command lines have mis-split here before.
if ($cfg.engine -eq 'claude') {
    $exe  = $CLAUDE_EXE
    $argv = @('-p', '--model', $cfg.model, '--output-format', 'json', '--add-dir', $WorkDir)
    if ($ExtraReadDir) { $argv += @('--add-dir', $ExtraReadDir) }
    if ($MaxTurns -gt 0) { $argv += @('--max-turns', [string]$MaxTurns) }
    # Deny-list written to the RUN DIR so the grant is auditable beside the receipt that
    # it produced, rather than being an invisible property of the invocation.
    if (-not $AllowBulkReads) {
        $denyRules = @(
            'Read(**/gpu-lane-impl-review-sync.md)',
            'Read(**/queue.json)',
            'Read(**/claude-resume-CURRENT.md)',
            'Read(**/fable-resume-CURRENT.md)',
            'Read(**/orchestrator-resume-CURRENT.md)'
        )
        $settingsObj = @{ permissions = @{ deny = $denyRules } }
        Write-Utf8NoBom $settingsPath ($settingsObj | ConvertTo-Json -Depth 5)
        $argv += @('--settings', $settingsPath)
    }
    if ($AllowEdits) {
        # 0.1: acceptEdits still takes an explicit --allowedTools list - the mode
        # decides HOW an allowed tool behaves (auto-accept vs prompt), the list
        # decides WHICH tools are allowed at all. 'ALL' was refused above.
        $argv += @('--permission-mode', 'acceptEdits', '--allowedTools', $AllowedTools)
    } else {
        # No read-only permission mode exists, so restrict the TOOLS instead.
        # COMMA-SEPARATED, ONE TOKEN: --allowedTools is VARIADIC, so passing the
        # tools as separate arguments makes it swallow the positional prompt that
        # follows and the CLI dies with "Input must be provided...".
        $argv += @('--permission-mode', 'dontAsk',
                   '--allowedTools', 'Read,Grep,Glob')
        # A permission allowlist does not hide other tools from the model. A
        # readonly review previously burned its turn cap retrying denied shells.
        $capabilityNotice = 'This read-only lane has permission to use only Read, Grep, and Glob. Bash, PowerShell, editing tools, Agent, and Task are unavailable: do not call or retry them. Inspect hub-exported diffs and evidence with the available read tools. If a required export is missing, name that missing evidence and return an unmeasured finding; do not claim you ran shell commands or tests.'
        $argv += @('--append-system-prompt', $capabilityNotice)
    }
    # Prevent nested provider fan-out through the CLI's supported deny surface.
    # One comma-separated token avoids the same variadic swallowing hazard as allowedTools.
    $argv += @('--disallowedTools', 'Agent,Task')
    # PROMPT GOES VIA STDIN, NOT AS A POSITIONAL ARGUMENT. Several claude flags
    # (--allowedTools, --add-dir) are VARIADIC and keep consuming every following
    # token that does not start with '-', so a trailing positional prompt is
    # silently absorbed into the flag's value list and the CLI then dies with
    # "Input must be provided either through stdin or as a prompt argument".
    # stdin has no such ambiguity and no command-line length limit.
    $stdinContent = $Prompt
    # CAUSAL-REACH-1: the receipt used to record only allowEdits, which says what a
    # lane may WRITE and nothing about what it may CAUSE. Record the actual granted
    # authority so a receipt can be audited against the policy that produced it.
    $authority = [ordered]@{
        permissionMode = if ($AllowEdits) { 'acceptEdits' } else { 'dontAsk' }
        allowedTools   = if ($AllowEdits) { $AllowedTools } else { 'Read,Grep,Glob' }
        sandbox        = 'n/a (claude)'
        writableRoot   = if ($AllowEdits) { $WorkDir } else { $null }
        maxTurns       = if ($MaxTurns -gt 0) { $MaxTurns } else { 'unset' }
        bulkReads      = if ($AllowBulkReads) { 'ALLOWED' } else { 'DENIED' }
        denyRules      = if ($AllowBulkReads) { @() } else { $denyRules }
        disallowedTools = @('Agent', 'Task')
        capabilityNotice = if ($AllowEdits) { $null } else { $capabilityNotice }
    }
} else {
    $exe  = $CODEX_EXE
    $sandbox = if ($AllowEdits) { 'workspace-write' } else { 'read-only' }
    # -s and -c are set EXPLICITLY per call. ~/.codex/config.toml carries
    # approval_policy=never + sandbox_mode=danger-full-access globally, which is
    # fine for a watched interactive session and NOT fine for automated fan-out.
    $argv = @('exec',
              '-m', $cfg.model,
              '-c', ("model_reasoning_effort=`"{0}`"" -f $cfg.effort),
              '-s', $sandbox,
              '-C', $WorkDir,
              '-o', $lastPath,
              '--skip-git-repo-check',
              '-')
    # '-' MEANS "READ THE PROMPT FROM STDIN", and it is not optional here.
    # Passing a multi-line prompt POSITIONALLY is silently TRUNCATED AT THE FIRST
    # NEWLINE, because the launcher is a .cmd batch wrapper and cmd.exe breaks the
    # argument there. Measured 2026-08-29: a 3,243-byte review prompt arrived as
    # its first line only, and the lane answered "what would you like me to do?"
    # in 10.9s with exit 0 - a SUCCESSFUL-LOOKING run that reviewed nothing.
    $stdinContent = $Prompt
    $authority = [ordered]@{
        permissionMode = 'n/a (codex)'
        allowedTools   = 'ALL'
        sandbox        = $sandbox
        writableRoot   = if ($AllowEdits) { $WorkDir } else { $null }
        maxTurns       = 'n/a (codex exec has no turn cap)'
        bulkReads      = 'ALLOWED (codex takes no settings deny-list)'
        denyRules      = @()
    }
}

# ---------------------------------------------------------------- run, bounded
# Keep the stopwatch started at reservation: setup and child startup consume the
# same wall budget as provider execution.

# LAUNCH VIA ProcessStartInfo.ArgumentList, NOT Start-Process -ArgumentList.
# Start-Process joins an array into ONE command-line string without quoting the
# elements, so this repo's own path - "C:\!Layi Wkspc\MLV-App", which contains a
# space - arrives SPLIT and codex rejects the fragment as an unexpected argument.
# ArgumentList is a real collection and .NET applies correct per-argument
# escaping (including the special .cmd rules), so a path with spaces survives.
$psi = [System.Diagnostics.ProcessStartInfo]::new()
$psi.WorkingDirectory       = $WorkDir
$psi.UseShellExecute        = $false
$psi.CreateNoWindow         = $true
if ($cfg.engine -eq 'claude' -and $ReasoningEffort) {
    $psi.Environment['CLAUDE_CODE_EFFORT_LEVEL'] = $ReasoningEffort
}
# Per-run lane scratch under an MLV-owned root instead of the shared %TEMP%.
$scratchDir = $null
if ($ScratchRoot) {
    $scratchDir = Join-Path $ScratchRoot ('{0}-{1}' -f (Split-Path $RunDir -Leaf), (Split-Path $base -Leaf))
    New-Item -ItemType Directory -Force -Path $scratchDir | Out-Null
    foreach ($v in 'TEMP', 'TMP', 'TMPDIR') { $psi.Environment[$v] = $scratchDir }
}
$psi.RedirectStandardInput  = $true
$psi.RedirectStandardOutput = $true
$psi.RedirectStandardError  = $true
$psi.StandardOutputEncoding = [System.Text.UTF8Encoding]::new($false)
$psi.StandardErrorEncoding  = [System.Text.UTF8Encoding]::new($false)

if ($sw.Elapsed.TotalMilliseconds -ge ($TimeoutSec * 1000.0)) {
    throw [TimeoutException]::new('launch-budget-exhausted')
}

if ($cfg.engine -eq 'claude') {
    # This trusted host is inert until it reads frame one. It is assigned to the job
    # before frame one is sent, so the provider and every descendant inherit the job.
    $hostSource = @'
$ErrorActionPreference='Stop'; Set-StrictMode -Version Latest
$line=[Console]::In.ReadLine(); if([string]::IsNullOrWhiteSpace($line)){throw 'launch-frame-missing'}
$launch=$line|ConvertFrom-Json; if([string]$launch.schema -ne 'mlv-lane-launch/v1'){throw 'launch-frame-schema'}
$p=[Diagnostics.ProcessStartInfo]::new(); $p.FileName=[string]$launch.exe
foreach($a in @($launch.argv)){[void]$p.ArgumentList.Add([string]$a)}
$p.WorkingDirectory=[string]$launch.cwd; $p.UseShellExecute=$false
$p.RedirectStandardInput=$true; $p.RedirectStandardOutput=$true; $p.RedirectStandardError=$true
$p.StandardOutputEncoding=[Text.UTF8Encoding]::new($false); $p.StandardErrorEncoding=[Text.UTF8Encoding]::new($false)
$child=[Diagnostics.Process]::Start($p); $ot=$child.StandardOutput.ReadToEndAsync(); $et=$child.StandardError.ReadToEndAsync()
$control=[ordered]@{schema='mlv-lane-child/v1';pid=$child.Id;createdUtc=$child.StartTime.ToUniversalTime().ToString('o')}|ConvertTo-Json -Compress
$tmp=[string]$launch.controlPath+'.'+[guid]::NewGuid().ToString('N')+'.tmp'
[IO.File]::WriteAllText($tmp,$control,[Text.UTF8Encoding]::new($false)); [IO.File]::Move($tmp,[string]$launch.controlPath)
$line=[Console]::In.ReadLine(); if([string]::IsNullOrWhiteSpace($line)){throw 'prompt-frame-missing'}
$frame=$line|ConvertFrom-Json; if([string]$frame.schema -ne 'mlv-lane-prompt/v1'){throw 'prompt-frame-schema'}
$prompt=[Text.Encoding]::UTF8.GetString([Convert]::FromBase64String([string]$frame.promptBase64))
$child.StandardInput.Write($prompt); $child.StandardInput.Close(); $child.WaitForExit()
[Console]::Out.Write($ot.GetAwaiter().GetResult()); [Console]::Error.Write($et.GetAwaiter().GetResult()); exit $child.ExitCode
'@
    $encoded = [Convert]::ToBase64String([Text.Encoding]::Unicode.GetBytes($hostSource))
    $psi.FileName = (Get-Command pwsh.exe -ErrorAction Stop).Source
    foreach ($a in @('-NoLogo','-NoProfile','-NonInteractive','-ExecutionPolicy','Bypass','-EncodedCommand',$encoded)) {
        [void]$psi.ArgumentList.Add($a)
    }
    $jobHandle = [MlvLaneJob]::CreateKillOnClose()
    $proc = [Diagnostics.Process]::Start($psi)
    $hostStarted = $true
    # Record the pid the instant Start returns, before anything that can throw:
    # once Start succeeds a host EXISTS, and a receipt that omits its pid is
    # indistinguishable from "no host was started" (PR #105 round 2 blocker).
    # $hostPid is a bare property read on a live Process -- it cannot throw,
    # unlike $containedHost's own ordered-map build below (PR #105 round 4
    # blocker), so it survives even when that build itself throws (PR #105
    # round 5): the post-start-unrecorded fallback reports $hostPid instead of
    # null, so a host that exists is never reported as if it did not.
    # createdUtc is read defensively in its own try -- a StartTime failure must
    # not erase the pid we already have.
    $hostPid = $proc.Id
    $containedHost = [ordered]@{ pid=$hostPid; createdUtc=$null }
    try { $containedHost.createdUtc = $proc.StartTime.ToUniversalTime().ToString('o') } catch { }
    [MlvLaneJob]::AssignOrThrow($jobHandle, $proc.Handle)
    $jobAssigned = $true
} else {
    $psi.FileName = $exe
    foreach ($a in $argv) { [void]$psi.ArgumentList.Add($a) }
    $proc = [Diagnostics.Process]::Start($psi)
}

# Start the async reads BEFORE waiting: a child that fills a redirected pipe
# buffer blocks forever if nobody is draining it, and the timeout below would
# then measure a deadlock we caused rather than a slow lane.
$outTask = $proc.StandardOutput.ReadToEndAsync()
$errTask = $proc.StandardError.ReadToEndAsync()

# The prompt reaches claude this way; codex gets an empty stdin that is CLOSED,
# which is what stops it waiting on "Reading additional input from stdin...".
if ($cfg.engine -eq 'claude') {
    $controlPath = "$base.child.json"
    if (Test-Path -LiteralPath $controlPath) { throw "control-path-exists: $controlPath" }
    $launchFrame = [ordered]@{ schema='mlv-lane-launch/v1'; exe=$exe; argv=$argv; cwd=$WorkDir; controlPath=$controlPath } | ConvertTo-Json -Compress -Depth 5
    $proc.StandardInput.WriteLine($launchFrame); $proc.StandardInput.Flush()
    $controlDeadlineMs = [math]::Min($sw.Elapsed.TotalMilliseconds + 10000.0, $TimeoutSec * 1000.0)
    while (-not (Test-Path -LiteralPath $controlPath)) {
        if ($proc.HasExited) { throw "contained-host-exited-before-child: $($proc.ExitCode)" }
        if ($sw.Elapsed.TotalMilliseconds -ge $controlDeadlineMs) { throw [TimeoutException]::new('contained-child-start-timeout') }
        Start-Sleep -Milliseconds 25
    }
    # ConvertFrom-Json can turn ISO strings into DateTime values on newer pwsh;
    # casting back to string then loses precision and uses the current culture.
    # Keep the exact UTC creation identity emitted by the contained host.
    $controlJson = [System.Text.Json.JsonDocument]::Parse([IO.File]::ReadAllText($controlPath))
    try {
        $childIdentity = [pscustomobject]@{
            schema = $controlJson.RootElement.GetProperty('schema').GetString()
            pid = $controlJson.RootElement.GetProperty('pid').GetInt32()
            createdUtc = $controlJson.RootElement.GetProperty('createdUtc').GetString()
        }
    } finally { $controlJson.Dispose() }
    if ([string]$childIdentity.schema -ne 'mlv-lane-child/v1') { throw 'contained-child-schema' }
    $containment = [ordered]@{
        kind='windows-job-kill-on-close'; jobAssigned=$jobAssigned
        runnerPid=$PID; runnerCreatedUtc=(Get-Process -Id $PID).StartTime.ToUniversalTime().ToString('o')
        ownerPid=$containedHost.pid; ownerCreatedUtc=$containedHost.createdUtc
        childPid=[int]$childIdentity.pid; childCreatedUtc=[string]$childIdentity.createdUtc
        deadlineUtc=$deadlineUtc.ToString('o'); promptDelivered=$false; assignmentErrorCode=$null
    }
    $runningReceipt = [ordered]@{
        schema='mlv-app/fleet-lane-receipt/v1'; state='running'; complete=$false
        lane=$Lane; card=$Card; startedUtc=$startedUtc.ToString('o'); timeoutSec=$TimeoutSec
        promptSha256=(Get-Sha256 $Prompt); promptBytes=[Text.Encoding]::UTF8.GetByteCount($Prompt)
        containment=$containment
    }
    Write-Utf8NoBomAtomic $rcptPath ($runningReceipt | ConvertTo-Json -Depth 6)
    if ($sw.Elapsed.TotalMilliseconds -ge ($TimeoutSec * 1000.0)) { throw [TimeoutException]::new('contained-prompt-deadline-exhausted') }
    $promptFrame = [ordered]@{schema='mlv-lane-prompt/v1';promptBase64=[Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes($stdinContent))}|ConvertTo-Json -Compress
    $proc.StandardInput.WriteLine($promptFrame); $proc.StandardInput.Flush()
    $promptDelivered = $true; $containment.promptDelivered = $true
    Write-Utf8NoBomAtomic $rcptPath ($runningReceipt | ConvertTo-Json -Depth 6)
    $proc.StandardInput.Close()
} else {
    $proc.StandardInput.Write($stdinContent)
    $proc.StandardInput.Close()
}

$timedOut = $false
$remainingMs = [math]::Max(0, [math]::Floor(($TimeoutSec * 1000.0) - $sw.Elapsed.TotalMilliseconds))
if ($remainingMs -eq 0 -or -not $proc.WaitForExit([int]$remainingMs)) {
    $timedOut = $true
    # Closing the job is the authoritative descendant cleanup. Do it before
    # harvesting pipes, which descendants could otherwise keep open indefinitely.
    if ($jobHandle -ne [IntPtr]::Zero) {
        [void][MlvLaneJob]::CloseHandle($jobHandle)
        $jobHandle = [IntPtr]::Zero
    }
    try { $proc.Kill($true) } catch { }
    try { [void]$proc.WaitForExit(15000) } catch { }
}
$sw.Stop()
$exitCode = if ($timedOut) { -1 } else { $proc.ExitCode }

# ---------------------------------------------------------------- harvest
$stdout = try { $outTask.Result } catch { '' }
if ($null -eq $stdout) { $stdout = '' }
$stderrText = try { $errTask.Result } catch { '' }
if ($null -eq $stderrText) { $stderrText = '' }
Write-Utf8NoBom $outPath $stdout
Write-Utf8NoBom $errPath $stderrText
# Classify BEFORE parsing the answer: a refused run has no answer, and the 2026-09-07 sol
# receipt proved that exitCode alone cannot tell "refused in 7 s" from "reviewed and objected".
# Structurally, never by scanning the transcript (sol PR #80 R1+R2, both BLOCKER): the
# transcript legitimately CONTAINS the refusal vocabulary even when the run answered.
$providerRefusal = Get-ProviderRefusal -Text $stderrText -Answer $stdout -Engine $cfg.engine -Prompt $Prompt

$final = ''
if ($cfg.engine -eq 'claude') {
    # --output-format json wraps the answer; fall back to raw stdout if the
    # process died before emitting well-formed JSON.
    try {
        $j = $stdout | ConvertFrom-Json
        if ($j.PSObject.Properties.Name -contains 'result') { $final = [string]$j.result }
        # SPEND IS A FIRST-CLASS RECEIPT FACT. Until 2026-09-03 the receipt recorded
        # exitCode (the truth about completion) and byte counts, but NOTHING about cost -
        # so "what did this board spend" was answerable only by grepping raw stdout of
        # every run dir by hand. That is how USD 74.62 accumulated unnoticed in 4 h.
        if ($j.PSObject.Properties.Name -contains 'total_cost_usd') {
            $costUsd = [double]$j.total_cost_usd
        }
        if ($j.PSObject.Properties.Name -contains 'num_turns') { $numTurns = [int]$j.num_turns }
        if ($j.PSObject.Properties.Name -contains 'usage' -and $null -ne $j.usage) {
            if ($j.usage.PSObject.Properties.Name -contains 'cache_creation_input_tokens') {
                $cacheCreateTokens = [int64]$j.usage.cache_creation_input_tokens
            }
            if ($j.usage.PSObject.Properties.Name -contains 'cache_read_input_tokens') {
                $cacheReadTokens = [int64]$j.usage.cache_read_input_tokens
            }
            if ($j.usage.PSObject.Properties.Name -contains 'output_tokens') {
                $outputTokens = [int64]$j.usage.output_tokens
            }
        }
    } catch { $final = '' }
    if (-not $final) { $final = $stdout }
    Write-Utf8NoBom $lastPath $final
} else {
    $final = if (Test-Path -LiteralPath $lastPath) { Get-Content -LiteralPath $lastPath -Raw } else { '' }
    if ([string]::IsNullOrWhiteSpace($final)) { $final = $stdout }
    # LAST-RESORT FALLBACK TO STDERR. codex writes its -o last-message file only on
    # a CLEAN exit and streams its working narration to STDERR, not stdout - so a
    # lane killed at the timeout leaves the last-message file absent and stdout
    # EMPTY, i.e. a 15-minute run yielding literally zero bytes. Measured
    # 2026-08-29 on a timed-out luna recon. Partial narration is worth far more
    # than nothing when deciding whether to retry, widen the timeout, or change
    # approach entirely.
    if ([string]::IsNullOrWhiteSpace($final)) { $final = $stderrText }
    if ($null -eq $final) { $final = '' }
    if ($timedOut) { Write-Utf8NoBom $lastPath $final }
}

}
catch {
    if ($_.Exception -is [TimeoutException]) {
        $timedOut = $true
        $exitCode = -1
        $failure = $null
    } else {
        $failure = $_.Exception.Message
    }
    # Before assignment the inert host is outside the job. Terminate only the exact
    # Process object created by this invocation; it has received no launch frame.
    # UNLIKE the post-timeout kill at line ~602 -- where the job handle has just
    # been closed and the job is kill-on-close, so the tree is already terminated
    # and that catch is belt-and-braces -- this host was NEVER inside the job, so
    # a swallowed failure here is a GENUINE orphan (PR #105 round 5, sol PR #105
    # blocker). The kill call itself still must not throw out of this catch (this
    # is already a failure path; a second exception would only mask the first),
    # but its OUTCOME is recorded into the containment record below instead of
    # vanishing silently -- the honest fix for a swallow is not to make the kill
    # infallible (it cannot be), but to make its failure visible.
    if ($cfg.engine -eq 'claude' -and -not $jobAssigned -and $null -ne $proc) {
        $ownerKillAttempted = $true
        try {
            if ($proc.HasExited) {
                $ownerKillOutcome = $OWNER_KILL_OUTCOME_ALREADY_EXITED
            } else {
                # WaitForExit(Int32) RETURNS a bool -- $true iff the process exited
                # within the timeout, $false if the wait merely expired. Round 5
                # discarded that return with [void] and recorded 'killed'
                # unconditionally, so a host that survived the bounded wait (exactly
                # the orphan this whole change exists to make visible) was reported
                # as killed -- manufactured evidence, not just an omission. Capture
                # the observed boolean and branch on IT, never on what was attempted.
                $proc.Kill($true)
                $exitedWithinWait = $proc.WaitForExit(5000)
                $ownerKillOutcome = if ($exitedWithinWait) { $OWNER_KILL_OUTCOME_KILLED } else { $OWNER_KILL_OUTCOME_KILL_WAIT_TIMEOUT }
            }
        } catch {
            $ownerKillOutcome = $OWNER_KILL_OUTCOME_KILL_THREW
            $ownerKillDetail = $_.Exception.Message
        }
        # Considered also stamping $proc.HasExited at the moment the containment
        # record is BUILT (further below, well after this try/catch) as a second,
        # cheaper corroborating observation. Declined: that build site sits inside
        # the outer exception handler with no catch of its own, so a HasExited
        # read there that throws (it CAN -- Win32Exception/InvalidOperationException
        # per the .NET contract) would escape uncaught and blow past receipt
        # construction entirely, the same "second exception masks the first"
        # failure mode the comment on this catch already warns against. Not worth
        # it for a value that WaitForExit's own return already answers.
    }
    if ($cfg.engine -eq 'claude' -and $null -eq $containment) {
        $native = if ($_.Exception.PSObject.Properties.Name -contains 'NativeErrorCode') { [int]$_.Exception.NativeErrorCode } else { $null }
        # CLASSIFYING THE THIRD STATE (PR #105 round 4): round 3 recorded the raw exception
        # MESSAGE as ownerAbsentReason, and the test accepted any truthy string as proof no
        # host existed. But $containedHost's own construction (the pid/dictionary build
        # above) can itself throw AFTER Start already returned and a host is alive -- so
        # that exception's message would pose as a legitimate no-host reason. Free text is
        # not admissible evidence about a safety property, so classify by WHERE the failure
        # happened instead of by WHAT it said:
        #   - $containedHost non-null  -> a pid was recorded; no absence to explain.
        #   - $containedHost null, $hostStarted false -> Start never returned: either the
        #     launch budget was already gone (line ~470, throws before Start is reached) or
        #     Start itself threw. Distinguish cheaply by exception type/message where we can;
        #     otherwise fall back to the generic "start threw" token. Both are legitimate
        #     no-host reasons, and $hostPid is still null in both (no host ever existed).
        #   - $containedHost null, $hostStarted true -> Start returned (a host EXISTS) but
        #     the record of it was never built. This is the genuinely ambiguous state and it
        #     is named as such, never hidden behind a message that merely looks legitimate.
        #     $hostPid (captured on its own non-throwing line before this build) is used for
        #     ownerPid below, so this state is never reported as if no host existed (PR #105
        #     round 5): the pid is the only channel by which anyone learns the orphan existed.
        # The raw message is kept for humans in ownerAbsentDetail, a field no decision reads.
        $ownerAbsentReason = $null
        $ownerAbsentDetail = $null
        if ($null -eq $containedHost) {
            $ownerAbsentDetail = $_.Exception.Message
            if (-not $hostStarted) {
                $ownerAbsentReason = if ($_.Exception -is [TimeoutException] -and $_.Exception.Message -eq $OWNER_ABSENT_NO_HOST_BUDGET) {
                    $OWNER_ABSENT_NO_HOST_BUDGET
                } else {
                    $OWNER_ABSENT_NO_HOST_START_THREW
                }
            } else {
                $ownerAbsentReason = $OWNER_ABSENT_POST_START_UNRECORDED
            }
        }
        $containment = [ordered]@{
            kind='windows-job-kill-on-close'; jobAssigned=$jobAssigned
            runnerPid=$PID; runnerCreatedUtc=(Get-Process -Id $PID).StartTime.ToUniversalTime().ToString('o')
            ownerPid=if($null-ne $containedHost){$containedHost.pid}else{$hostPid}
            ownerCreatedUtc=if($null-ne $containedHost){$containedHost.createdUtc}else{$null}
            ownerAbsentReason=$ownerAbsentReason
            ownerAbsentDetail=$ownerAbsentDetail
            ownerKillAttempted=$ownerKillAttempted
            ownerKillOutcome=$ownerKillOutcome
            ownerKillDetail=$ownerKillDetail
            childPid=$null; childCreatedUtc=$null; deadlineUtc=$deadlineUtc.ToString('o')
            promptDelivered=$promptDelivered; assignmentErrorCode=$native
        }
    }
    # Convert managed failures into the receipt/exit taxonomy below. Rethrowing here
    # bypasses the final `exit $propagated` and turns the documented 127 into shell 1.
}
finally {

# Cleanup must precede all receipt construction and I/O, including exceptional
# startup paths. Failure to write evidence cannot keep a provider running.
if ($jobHandle -ne [IntPtr]::Zero) {
    [void][MlvLaneJob]::CloseHandle($jobHandle)
    $jobHandle = [IntPtr]::Zero
}
$sw.Stop()
# Disk hygiene. Neither step may throw: a failure here is recorded, never propagated,
# because the receipt below must still be written on every exit path.
$scratchDisposition = $null
if ($scratchDir) {
    try {
        $sb = [int64]0
        if (Test-Path -LiteralPath $scratchDir) { Get-ChildItem -LiteralPath $scratchDir -Recurse -File -Force -ErrorAction SilentlyContinue | ForEach-Object { $sb += $_.Length } }
        $removed = $false
        if (-not $KeepScratch -and (Test-Path -LiteralPath $scratchDir)) { Remove-Item -LiteralPath $scratchDir -Recurse -Force -ErrorAction Stop; $removed = $true }
        $scratchDisposition = [ordered]@{ path = $scratchDir; bytes = $sb; removed = $removed; error = $null }
    } catch {
        $scratchDisposition = [ordered]@{ path = $scratchDir; bytes = $null; removed = $false; error = $_.Exception.Message }
    }
}
$worktreeDisposition = $null
if ($RetireWorktree) {
    try {
        . (Join-Path $PSScriptRoot 'Retire-LaneWorktree.ps1')
        # The CANONICAL board (parent of the common .git), never the checkout this script runs
        # from: a copy of this script inside a linked worktree must not quarantine into that worktree.
        # No `| Select-Object -First 1` on the native call: it can stop the pipeline before
        # $LASTEXITCODE is set, and reading it then throws under StrictMode Latest.
        $commonOut = @(& git -C $PSScriptRoot rev-parse --path-format=absolute --git-common-dir 2>$null)
        $commonDir = if ($commonOut.Count) { [string]$commonOut[0] } else { $null }
        if (-not $commonDir -or -not (Test-Path -LiteralPath $commonDir)) { throw 'cannot resolve board root from git common dir' }
        $boardRoot = Split-Path -Parent ($commonDir -replace '/', '\')
        $worktreeDisposition = Invoke-RetireLaneWorktree -WorkDir $WorkDir -ProtectPath @($RunDir) `
            -QuarantineRoot (Join-Path $boardRoot ('.claude-state\disk-hygiene\quarantine\lane-exit\' + (Get-Date).ToUniversalTime().ToString('yyyyMMdd')))
    } catch {
        $worktreeDisposition = [ordered]@{ action = 'kept'; reason = "cannot-determine: $($_.Exception.Message)" }
    }
}
$receipt = [ordered]@{
    schema       = 'mlv-app/fleet-lane-receipt/v1'
    # SAME KEY AT EVERY STAGE. A reader checks `state` once - reserved, complete or
    # failed - instead of inferring liveness from which fields happen to be present.
    state        = if ($null -ne $failure) { 'failed' }
                   elseif ($null -ne $providerRefusal) { 'refused' }
                   elseif ($exitCode -ne -999) { 'complete' }
                   else { 'incomplete' }
    lane         = $Lane
    role         = $cfg.role
    engine       = $cfg.engine
    model        = $cfg.model
    effort       = $cfg.effort
    card         = $Card
    workDir      = $WorkDir
    allowEdits   = [bool]$AllowEdits
    allowedTools = if ($AllowEdits) { $AllowedTools } else { $authority.allowedTools }
    baseSha      = $BaseSha
    hookSha256   = $WorkDirHookSha256
    authority    = $authority
    startedUtc   = $startedUtc.ToString('o')
    endedUtc     = (Get-Date).ToUniversalTime().ToString('o')
    durationSec  = [math]::Round($sw.Elapsed.TotalSeconds, 1)
    timedOut     = $timedOut
    timeoutSec   = $TimeoutSec
    exitCode     = $exitCode
    promptSha256 = Get-Sha256 $Prompt
    promptBytes  = [System.Text.Encoding]::UTF8.GetByteCount($Prompt)
    outputSha256 = Get-Sha256 $final
    outputBytes  = [System.Text.Encoding]::UTF8.GetByteCount($final)
    promptPath   = $promptPath
    outputPath   = $lastPath
    stdoutPath   = $outPath
    stderrPath   = $errPath
    failure      = $failure
    # null when the provider did the work. Otherwise {kind, engine, match, retryAfter, remedy};
    # `complete` is false in that case even though `failure` is null -- the lane script did not
    # fail, the provider declined, and a reader must never mistake that for a verdict.
    providerRefusal = $providerRefusal
    containment  = $containment
    scratch      = $scratchDisposition
    worktreeDisposition = $worktreeDisposition
    complete     =($null -eq $failure -and $null -eq $providerRefusal -and $exitCode -ne -999)
    spend        = [ordered]@{
        costUsd            = $costUsd
        costReported       = ($null -ne $costUsd)
        # Absence is NOT zero. codex exec emits no cost telemetry, so a codex receipt
        # says so explicitly instead of implying a free run.
        costUnavailableWhy = if ($null -ne $costUsd) { $null }
                             elseif ($cfg.engine -eq 'codex') { 'codex exec reports no cost telemetry' }
                             else { 'claude json carried no total_cost_usd (died before emitting it?)' }
        numTurns           = $numTurns
        cacheCreateTokens  = $cacheCreateTokens
        cacheReadTokens    = $cacheReadTokens
        outputTokens       = $outputTokens
    }
}
try {
    Write-Utf8NoBomAtomic $rcptPath (($receipt | ConvertTo-Json -Depth 6))
} finally {
    # Receipt I/O failure must not retain the job handle and its provider tree.
    if ($jobHandle -ne [IntPtr]::Zero) {
        [void][MlvLaneJob]::CloseHandle($jobHandle)
        $jobHandle = [IntPtr]::Zero
    }
}

}   # end finally - the receipt is now written on EVERY exit path

Write-Host ("[{0}] {1}/{2} effort={3} exit={4} {5}s cost={6} -> {7}" -f `
    $Lane, $cfg.engine, $cfg.model, $cfg.effort, $exitCode, $receipt.durationSec,
    $(if ($null -ne $costUsd) { 'USD ' + ([math]::Round($costUsd,2)) } else { 'unreported' }),
    $rcptPath)

if ($timedOut) { Write-Host "  TIMED OUT after ${TimeoutSec}s - output is partial." }
if ($null -ne $providerRefusal) {
    Write-Host ("  PROVIDER REFUSED ({0}): {1}" -f $providerRefusal.kind, $providerRefusal.match)
    Write-Host ("  remedy: {0}" -f $providerRefusal.remedy)
}

# Emit the receipt so a caller can pipeline on it.
[pscustomobject]$receipt

# PROPAGATE THE OUTCOME. This script's own header says "it is alive because it is running and
# dead when it exits, and THE EXIT CODE IS THE TRUTH" -- and until now it did not honour that at
# its own boundary. It computed $exitCode, wrote it into the receipt, printed it, and then fell
# off the end, which in PowerShell exits 0. Invoke-Workstream.ps1 faithfully captured that
# $LASTEXITCODE and logged laneExitCode=0, so EVERY failed lane was recorded as a success in the
# only durable log the loop consults.
#
# Measured 2026-09-05, two dispatches of twelve:
#   CITE-TXN-1          181.5s  USD 2.24  api_error_status 429 "You've hit your session limit"
#   GATE-FAMILY-BOOKED    2.8s  USD 0     api_error_status 429
# Both receipts carried exitCode 1. Both dispatch-log rows carried laneExitCode 0. Two of the
# day's twelve dispatches produced nothing and were indistinguishable from work that succeeded.
#
# Sentinels are mapped rather than passed through: a negative value does not survive a process
# exit code intact, and -1 would surface as 255. 124 for a timeout matches the taxonomy the repo
# already uses in boundedRunnerExitCodes; 127 marks "reserved but never completed", which the
# receipt also records as complete=false with the failure carried.
# 125 = the provider refused (usage limit, 429, auth). Chosen beside 124/127 so a dispatcher
# reading laneExitCode can tell "nobody did the work" from "the work was done and objected".
$propagated = switch ($exitCode) {
    # First and with `break`: PowerShell evaluates EVERY matching clause, and a refusal can
    # arrive with any child exit code. "Nobody did the work" outranks how the child coded it.
    { $null -ne $providerRefusal } { 125; break }   # provider refused; see receipt.providerRefusal
    -1      { 124 }   # timed out
    -999    { 127 }   # slot reserved, no completion recorded
    default { if ($exitCode -ge 0 -and $exitCode -le 255) { $exitCode } else { 1 } }
}
exit $propagated
