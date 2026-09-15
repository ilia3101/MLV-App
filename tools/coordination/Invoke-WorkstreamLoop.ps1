<#
.SYNOPSIS
    One unattended cycle of the workstream driver: rotate across tracks, dispatch a
    bounded number of lanes, write a cycle receipt. Designed to be driven by a
    Windows Scheduled Task so the board advances without a chat session awake.

.DESCRIPTION
    WHY THIS IS A SCRIPT AND NOT A CHAT LOOP.

    A lane is already a bounded CLI process (Invoke-Lane.ps1), so "the orchestrator
    drives lanes over the CLI" has been true since 2026-08-29. What was NOT true is
    that the ORCHESTRATOR survived anything. When an interactive session drives the
    lanes, the board advances only while that session has context and quota - and on
    2026-08-31 both ran out inside one hour: one lane died `prompt_too_long` after
    burning $51.62, the next died on a 429 session cap.

    A scheduled task holds no session, no lease, no seat and no MCP handle. It
    belongs to the OS user, which is why MLV-BoardStateHeartbeat survives an account
    rotation and every seat-era producer did not. This script is the same shape.

    SPENDING IS BOUNDED THREE WAYS, because an unattended loop that dispatches
    models is an unattended loop that spends money:
      1. -MaxDispatchesPerCycle  (default 2)
      2. -DailyBudget            (default 12) counted from the dispatch log's own
                                 records for the current UTC day - derived, not tracked
                                 in a counter that can drift from reality
      3. A KILL SWITCH FILE. If it exists, this script exits 0 having dispatched
         nothing. Stopping the fleet must not require finding a process.

    IT NEVER MUTATES queue.json. RESUME.md STEP 4/5 bars that under the recovery
    authority, and an unattended actor is exactly the wrong thing to relax it for.

.NOTES
    ASCII-only by project convention. Install with -Install; verify BY ITS RECEIPTS
    under .claude-state\fleet-runs\loop-cycles\, never by the task's config existing.
#>
[CmdletBinding()]
param(
    [int]$MaxDispatchesPerCycle = 2,
    [int]$DailyBudget = 12,
    [int]$TimeoutSec = 1500,
    [int]$StaleHours = 12,

    # Tracks to rotate through, in preference order.
    [string[]]$Tracks = @('playback','factory','product','UNSET'),

    # Lane and edit-authority to forward to Invoke-Workstream.ps1 on every dispatch this cycle.
    # Empty $Lane preserves the dispatcher's own kind/owner-based resolution.
    [ValidateSet('', 'opus', 'sonnet', 'fable', 'sol', 'luna')]
    [string]$Lane = '',
    [switch]$AllowEdits,

    [switch]$DryRun,
    [switch]$Install,
    [switch]$Status
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

# Pure -Install/-Tracks helpers, dot-sourced so a test can exercise them without running the loop
# (which syncs a real git worktree and would mutate live board state).
. (Join-Path $PSScriptRoot 'loop-install-args.ps1')

# A pwsh -File scheduled-task action hands back a persisted -Tracks value as ONE literal string;
# turn a comma-joined string back into an array before it is used for anything, including -Status
# output and the cycle receipt.
$Tracks = Resolve-Tracks -Tracks $Tracks

$RepoRoot   = 'C:\!Layi Wkspc\MLV-App'
$DualLane   = Join-Path $RepoRoot '.claude-state\coordination\dual-lane'
$LogPath    = Join-Path $DualLane 'workstream-dispatch-log.jsonl'
$KillSwitch = Join-Path $DualLane 'WORKSTREAM-LOOP-DISABLED'
$CycleDir   = Join-Path $RepoRoot '.claude-state\fleet-runs\loop-cycles'
$TaskName   = 'MLV-WorkstreamLoop'

# THE DRIVER WORKTREE, AND WHY IT EXISTS.
#
# The dispatcher is a TRACKED file under tools\. The canonical checkout is
# routinely parked on a peer branch - right now `diag/async-h2d-preupload-exits`,
# another actor's do-not-merge diagnostic branch - and a tracked script simply does
# not exist on a ref that predates it. This repo has already been bitten by exactly
# that: on 2026-08-30 all three hooks in .claude\settings.json pointed at scripts
# living only on peer branches, and every one began erroring the moment the checkout
# moved. The rule that came out of it is "a hook's script must live on the SAME REF
# as the tree it guards."
#
# An unattended loop cannot rely on where a human left the checkout. So it drives
# from its OWN worktree, pinned to the target branch and refreshed each cycle. This
# is a LOAD-BEARING worktree, not sweep debris: it is recreated on demand and its
# absence is a CANNOT-DETERMINE, never a silent no-op.
$DriverRoot   = 'C:\mlvtmp\ws-driver'
$TargetRef    = 'fork/master'
$Dispatcher   = Join-Path $DriverRoot 'tools\coordination\Invoke-Workstream.ps1'

function Get-ReservationBudget {
    param([string]$LedgerPath, [datetime]$TodayUtc, [string]$BoardRoot)
    $ErrorActionPreference = 'Stop'
    function Field($Object, [string]$Name) {
        if ($null -eq $Object) { return $null }
        $property = $Object.PSObject.Properties[$Name]
        if ($property) { return $property.Value }
        return $null
    }
    function Utc($Value) {
        if ($null -eq $Value) { throw 'reservation timestamp missing' }
        # Preserve DateTimeKind: reparsing an already-UTC DateTime as a string
        # applies the local offset twice (the measured September 4 regression).
        $v = $Value
        $rowStamp = if ($v -is [datetime]) {
            if ($v.Kind -eq [System.DateTimeKind]::Unspecified) { [datetime]::SpecifyKind($v, [System.DateTimeKind]::Utc) } else { $v }
        } else {
            [datetime]::Parse([string]$v, [cultureinfo]::InvariantCulture, [System.Globalization.DateTimeStyles]::RoundtripKind)
        }
        return $rowStamp.ToUniversalTime()
    }
    function FullPath([string]$Path) {
        if ([string]::IsNullOrWhiteSpace($Path)) { throw 'reservation path missing' }
        if (-not [IO.Path]::IsPathRooted($Path)) { $Path = Join-Path $BoardRoot $Path }
        return [IO.Path]::GetFullPath($Path).TrimEnd([IO.Path]::DirectorySeparatorChar)
    }
    function RefundMatches($Reservation, $Terminal) {
        try {
            if ((Field $Terminal.row 'state') -cne 'refunded' -or $Terminal.utc -lt $Reservation.utc) { return $false }
            foreach ($key in @('lane','card','runDir')) {
                $value = Field $Reservation.row $key
                if ($value -isnot [string] -or [string]::IsNullOrWhiteSpace($value) -or $value -cne (Field $Terminal.row $key)) { return $false }
            }
            $run = FullPath (Field $Reservation.row 'runDir')
            $fleet = (FullPath '.claude-state/fleet-runs') + [IO.Path]::DirectorySeparatorChar
            if (-not $run.StartsWith($fleet, [StringComparison]::OrdinalIgnoreCase)) { return $false }
            $path = FullPath (Field $Terminal.row 'receiptPath')
            if ([IO.Path]::GetDirectoryName($path) -ine $run -or [IO.Path]::GetFileName($path) -notlike '*.receipt.json') { return $false }
            $info = Get-Item -LiteralPath $path -ErrorAction Stop
            if ($info.Length -gt 1MB -or $info.Length -eq 0 -or ($info.Attributes -band [IO.FileAttributes]::ReparsePoint)) { return $false }
            if ((Get-Item -LiteralPath $run).Attributes -band [IO.FileAttributes]::ReparsePoint) { return $false }
            $bytes = [IO.File]::ReadAllBytes($path)
            $hash = [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($bytes)).ToLowerInvariant()
            $expected = Field $Terminal.row 'receiptSha256'
            if ($expected -isnot [string] -or $expected -cnotmatch '^[0-9a-f]{64}$' -or $hash -cne $expected) { return $false }
            $receipt = [Text.Encoding]::UTF8.GetString($bytes).TrimStart([char]0xfeff) | ConvertFrom-Json -ErrorAction Stop
            if ((Field $receipt 'schema') -cne 'mlv-app/fleet-lane-receipt/v1') { return $false }
            foreach ($key in @('lane','card')) {
                if ((Field $receipt $key) -cne (Field $Reservation.row $key)) { return $false }
            }
            foreach ($key in @('promptPath','outputPath')) {
                if ([IO.Path]::GetDirectoryName((FullPath (Field $receipt $key))) -ine $run) { return $false }
            }
            $start = Utc (Field $receipt 'startedUtc'); $end = Utc (Field $receipt 'endedUtc')
            if ($start -lt $Reservation.utc -or $end -lt $start -or $end -gt $Terminal.utc) { return $false }
            $exit = Field $receipt 'exitCode'; $spend = Field $receipt 'spend'
            $reported = Field $spend 'costReported'; $cost = Field $spend 'costUsd'
            $integerExit = $exit -is [int] -or $exit -is [long]
            $numericCost = $cost -is [int] -or $cost -is [long] -or $cost -is [double] -or $cost -is [decimal]
            if (-not $integerExit -or $exit -eq 0 -or $reported -isnot [bool] -or -not $reported -or -not $numericCost -or $cost -ne 0) { return $false }
            $copiedExit = Field $Terminal.row 'laneExitCode'; $copiedCost = Field $Terminal.row 'laneCostUsd'
            $copiedReported = Field $Terminal.row 'laneCostReported'
            return (($copiedExit -is [int] -or $copiedExit -is [long]) -and $copiedExit -eq $exit -and
                $copiedReported -is [bool] -and $copiedReported -and
                ($copiedCost -is [int] -or $copiedCost -is [long] -or $copiedCost -is [double] -or $copiedCost -is [decimal]) -and $copiedCost -eq $cost)
        } catch { return $false }
    }
    $today = (Utc $TodayUtc).ToString('yyyy-MM-dd')
    $reservations = [Collections.Generic.Dictionary[string,object]]::new([StringComparer]::Ordinal)
    $terminals = [Collections.Generic.Dictionary[string,object]]::new([StringComparer]::Ordinal)
    $spent = 0
    if (-not (Test-Path -LiteralPath $LedgerPath)) { return 0 }
    foreach ($line in [IO.File]::ReadLines($LedgerPath)) {
        if ([string]::IsNullOrWhiteSpace($line)) { continue }
        $row = $line | ConvertFrom-Json -ErrorAction Stop
        if ($row -isnot [pscustomobject]) { throw 'reservation ledger row must be an object' }
        $state = Field $row 'state'
        if ($state -cnotin @('reserved','charged','refunded')) { throw 'unknown reservation state' }
        $utc = Utc (Field $row 'recordedUtc')
        $entry = [pscustomobject]@{ row=$row; utc=$utc }
        $id = Field $row 'reservationId'
        if ($id -isnot [string] -or [string]::IsNullOrWhiteSpace($id)) {
            if ($state -ceq 'reserved' -and $utc.ToString('yyyy-MM-dd') -eq $today) { $spent++ }
            continue
        }
        $map = if ($state -ceq 'reserved') { $reservations } else { $terminals }
        if (-not $map.ContainsKey($id)) { $map[$id] = [Collections.Generic.List[object]]::new() }
        $map[$id].Add($entry)
    }
    foreach ($id in $reservations.Keys) {
        $reserved = $reservations[$id]
        if (-not @($reserved | Where-Object { $_.utc.ToString('yyyy-MM-dd') -eq $today }).Count) { continue }
        $spent++
        if ($reserved.Count -ne 1 -or -not $terminals.ContainsKey($id)) { continue }
        $terminal = $terminals[$id]
        if ($terminal.Count -ne 1 -or $terminal[0].utc.ToString('yyyy-MM-dd') -ne $today) { continue }
        if (RefundMatches $reserved[0] $terminal[0]) {
            $spent--
            Write-Information -InformationAction Continue "LOOP: refunded dispatch reservation=$id (verified zero spend)"
        }
    }
    return $spent
}

function Invoke-GitFetchFork {
    # A single TRANSIENT failure must not cost a whole cycle.
    # Observed 2026-09-04T18:01:22Z: the loop halted on "git fetch fork failed (exit 128)"
    # caused by lock contention with a concurrent git operation on the same repository. The
    # identical fetch succeeded moments later by hand, so the cycle was lost to a blip, not
    # to a stale worktree. Bounded retries; the attempt count is reported for the receipt.
    param([int]$Attempts = 3, [int]$DelaySeconds = 5)
    $last = 0
    for ($i = 1; $i -le $Attempts; $i++) {
        & git -C $RepoRoot fetch fork --quiet 2>&1 | Out-Null
        if ($LASTEXITCODE -eq 0) { return [pscustomobject]@{ Ok = $true; Attempts = $i; ExitCode = 0 } }
        $last = $LASTEXITCODE
        if ($i -lt $Attempts) { Start-Sleep -Seconds $DelaySeconds }
    }
    return [pscustomobject]@{ Ok = $false; Attempts = $Attempts; ExitCode = $last }
}

function Sync-DriverWorktree {
    # Returns $null on success, or a CANNOT-DETERMINE reason string.
    try {
        if (-not (Test-Path -LiteralPath (Join-Path $DriverRoot '.git'))) {
            if (Test-Path -LiteralPath $DriverRoot) { Remove-Item -LiteralPath $DriverRoot -Recurse -Force }
            Invoke-GitFetchFork | Out-Null
            & git -C $RepoRoot -c core.longpaths=true worktree add --detach $DriverRoot $TargetRef 2>&1 | Out-Null
            if ($LASTEXITCODE -ne 0) { return "could not create driver worktree at $DriverRoot" }
            return $null
        }
        $fetch = Invoke-GitFetchFork
        if (-not $fetch.Ok) { return "git fetch fork failed (exit $($fetch.ExitCode)) after $($fetch.Attempts) attempts; driver worktree may be stale" }
        # Hard reset is safe here and ONLY here: this worktree is tool-owned, is
        # never edited by hand, and holds nothing anyone can lose.
        & git -C $DriverRoot reset --hard $TargetRef --quiet 2>&1 | Out-Null
        if ($LASTEXITCODE -ne 0) { return "could not reset driver worktree to $TargetRef" }
        return $null
    } catch {
        return "driver worktree sync threw: $($_.Exception.Message)"
    }
}

if (-not (Test-Path -LiteralPath $CycleDir)) { New-Item -ItemType Directory -Path $CycleDir -Force | Out-Null }

# ------------------------------------------------------------------ -Status
if ($Status) {
    $task = Get-ScheduledTask -TaskName $TaskName -ErrorAction SilentlyContinue
    if (-not $task) {
        Write-Output "LOOP: task '$TaskName' NOT REGISTERED (use -Install)"
    } else {
        $info = Get-ScheduledTaskInfo -TaskName $TaskName
        Write-Output "LOOP: task state=$($task.State) lastRun=$($info.LastRunTime) lastResult=$($info.LastTaskResult) nextRun=$($info.NextRunTime)"
    }
    # RECEIPTS ARE THE PROOF, NOT THE CONFIG. A registered task that has never
    # produced a cycle receipt has never run, and looks identical to a healthy one.
    $cycles = @(Get-ChildItem -LiteralPath $CycleDir -Filter '*.json' -ErrorAction SilentlyContinue |
                Sort-Object LastWriteTimeUtc -Descending)
    Write-Output "LOOP: cycle receipts = $($cycles.Count)"
    if ($cycles.Count -gt 0) {
        $newest = $cycles[0]
        $ageMin = [math]::Round(((Get-Date).ToUniversalTime() - $newest.LastWriteTimeUtc).TotalMinutes, 1)
        Write-Output "LOOP: newest receipt $($newest.Name) age ${ageMin} m"
    } else {
        Write-Output 'LOOP: NO CYCLE RECEIPTS - this loop has never run. A registered task that never fired looks exactly like a healthy one.'
    }
    if (Test-Path -LiteralPath $KillSwitch) { Write-Output "LOOP: KILL SWITCH PRESENT at $KillSwitch - cycles will dispatch NOTHING" }
    exit 0
}

# ------------------------------------------------------------------ -Install
if ($Install) {
    # BUDGET FLAGS MUST SURVIVE A REINSTALL. Until 2026-09-03 this line was hardcoded with
    # no budget arguments, so the task ALWAYS ran the defaults no matter what -Install was
    # given. When the measured spend (USD 22-25 per fable lane) forced an emergency cap, it
    # had to be applied by editing the live task action - a change that any later -Install
    # would silently have thrown away. Pass EVERY schedulable parameter through, including
    # -Tracks/-Lane/-AllowEdits (dropped silently until now - see Get-InstallArgLine's own
    # header), so the registered task states its own bound and the bound is auditable from
    # the task itself.
    $argLine = Get-InstallArgLine -ScriptPath $PSCommandPath -DailyBudget $DailyBudget `
        -MaxDispatchesPerCycle $MaxDispatchesPerCycle -TimeoutSec $TimeoutSec -StaleHours $StaleHours `
        -Tracks $Tracks -Lane $Lane -AllowEdits:$AllowEdits

    if ($DryRun) {
        # Show what would be installed without registering anything - the escape hatch that lets
        # a test (or a human) verify the arg line by construction rather than by reading source.
        Write-Output "LOOP: install argLine = $argLine"
        exit 0
    }

    $action  = New-ScheduledTaskAction -Execute 'pwsh.exe' `
        -Argument $argLine `
        -WorkingDirectory $RepoRoot
    # No -RepetitionDuration: [TimeSpan]::MaxValue serialises to a value the task XML
    # validator REJECTS, and omitting the duration is how you say "indefinitely".
    # (Fleet TRAPS.md, appended by MLV-App 2026-08-31.)
    $trigger = New-ScheduledTaskTrigger -Once -At (Get-Date).AddMinutes(2) `
        -RepetitionInterval (New-TimeSpan -Minutes 45)
    $settings = New-ScheduledTaskSettingsSet -AllowStartIfOnBatteries `
        -DontStopIfGoingOnBatteries -StartWhenAvailable `
        -MultipleInstances IgnoreNew -ExecutionTimeLimit (New-TimeSpan -Hours 2)
    Register-ScheduledTask -TaskName $TaskName -Action $action -Trigger $trigger `
        -Settings $settings -Description 'MLV-App: advance workstreams by dispatching bounded lanes. Budget-capped; kill switch at .claude-state\coordination\dual-lane\WORKSTREAM-LOOP-DISABLED' `
        -Force | Out-Null
    Write-Output "LOOP: registered '$TaskName' every 45 min with dailyBudget=$DailyBudget perCycle=$MaxDispatchesPerCycle."
    Write-Output "LOOP: PROVE IT BY ITS RECEIPTS, not by this message - run with -Status after the first fire."
    exit 0
}

# ------------------------------------------------------------------ cycle
$cycleStart = (Get-Date).ToUniversalTime()
$stamp      = $cycleStart.ToString('yyyyMMddTHHmmssZ')
$dispatched = @()
$skipped    = @()
$receiptWriteFailures = @()
$halted     = $null
Write-Output "LOOP: tracks=$($Tracks -join ', ')"

# RACE SAFETY: only one cycle of this loop may run at a time. A named, machine-wide mutex
# (not a lock FILE - a crashed holder leaks a file forever but the OS reclaims a mutex on
# process exit) held for the whole cycle, so a scheduled task firing on top of a still-running
# previous cycle waits rather than double-dispatching against the same budget.
$cycleMutex = New-Object System.Threading.Mutex($false, 'Global\MLV-WorkstreamLoop')
$gotMutex = $false
try {
    $gotMutex = $cycleMutex.WaitOne(0)
} catch [System.Threading.AbandonedMutexException] {
    # A previous holder crashed without releasing it. The mutex is still valid; take it.
    $gotMutex = $true
}
if (-not $gotMutex) {
    Write-Output 'LOOP: HALTED - another cycle already holds Global\MLV-WorkstreamLoop'
    exit 0
}
try {

$syncError = $null
if (Test-Path -LiteralPath $KillSwitch) {
    $halted = "kill-switch present at $KillSwitch"
    Write-Output "LOOP: HALTED - $halted"
} else {
    $syncError = Sync-DriverWorktree
    if ($syncError) { Write-Output "LOOP: driver worktree - $syncError" }
    else { Write-Output "LOOP: driver worktree at $DriverRoot pinned to $TargetRef ($(& git -C $DriverRoot rev-parse --short HEAD 2>$null))" }
}

if ($halted) {
    # already halted by the kill switch
} elseif ($syncError) {
    $halted = $syncError
    Write-Output "LOOP: CANNOT-DETERMINE - $halted"
} elseif (-not (Test-Path -LiteralPath $Dispatcher)) {
    $halted = "dispatcher missing at $Dispatcher even after syncing to $TargetRef - it is not on the target branch yet"
    Write-Output "LOOP: CANNOT-DETERMINE - $halted"
} else {
    # Count reservations, then refund only verified zero-spend terminal evidence.
    # A malformed ledger halts this cycle before any dispatcher can run.
    $ReservationsPath = Join-Path $DualLane 'receipts\dispatch-reservations.jsonl'
    $todayUtc = $cycleStart.ToString('yyyy-MM-dd')
    $spentToday = $null
    try { $spentToday = Get-ReservationBudget -LedgerPath $ReservationsPath -TodayUtc $cycleStart -BoardRoot $RepoRoot }
    catch {
        $halted = 'reservation budget cannot be determined'
        Write-Output "LOOP: CANNOT-DETERMINE - $halted"
    }
    Write-Output "LOOP: budget $spentToday/$DailyBudget used today (UTC $todayUtc); cycle cap $MaxDispatchesPerCycle"

    if ($halted) {
        # The budget reducer failed closed; retain the diagnostic above.
    } elseif ($spentToday -ge $DailyBudget) {
        $halted = "daily budget exhausted ($spentToday/$DailyBudget)"
        Write-Output "LOOP: HALTED - $halted"
    } else {
        foreach ($track in $Tracks) {
            if ($dispatched.Count -ge $MaxDispatchesPerCycle) { break }
            if (($spentToday + $dispatched.Count) -ge $DailyBudget) {
                $halted = 'daily budget reached mid-cycle'
                break
            }

            $argv = @('-NoProfile','-NonInteractive','-ExecutionPolicy','Bypass','-File',$Dispatcher,
                      '-Track',$track,'-TimeoutSec',$TimeoutSec,'-StaleHours',$StaleHours)
            if ($Lane) { $argv += @('-Lane', $Lane) }
            if ($AllowEdits) { $argv += '-AllowEdits' }
            if ($DryRun) { $argv += '-DryRun' }

            $out = & pwsh @argv 2>&1
            $rc  = $LASTEXITCODE
            # THE DISPATCH LINE, not merely the first WORKSTREAM: line. PR #26 added
            # "WORKSTREAM: landing-probe ..." and "WORKSTREAM: SKIP-LANDED ..." which now
            # print BEFORE the dispatch line, so First-1 recorded a probe summary as the
            # dispatch detail. Match the line that actually names the card; fall back to
            # the last WORKSTREAM: line, never the first.
            $line = ($out | Where-Object { $_ -match 'WORKSTREAM: track=' } | Select-Object -First 1)
            if (-not $line) { $line = ($out | Where-Object { $_ -match 'WORKSTREAM:' } | Select-Object -Last 1) }
            # The single detail line above drops every other output line, including the dispatcher's
            # "dispatch-attempt receipt NOT written" report - the one fact its run dir could not hold
            # (sol PR #111 post-merge). Carry each such line into the cycle receipt.
            foreach ($w in @($out | Where-Object { "$_" -match 'dispatch-attempt receipt NOT written' })) {
                $receiptWriteFailures += [ordered]@{ track = $track; exitCode = $rc; line = "$w" }
            }

            switch ($rc) {
                0 { $dispatched += [ordered]@{ track = $track; detail = "$line" } ; Write-Output "LOOP: [$track] dispatched - $line" }
                5 { $skipped    += [ordered]@{ track = $track; reason = 'ALL-RECENTLY-DISPATCHED'; detail = "$line" } ; Write-Output "LOOP: [$track] ALL-RECENTLY-DISPATCHED - no slot consumed" }
                4 { $skipped    += [ordered]@{ track = $track; reason = 'NO-LIVE-CARDS'; detail = "$line" } ; Write-Output "LOOP: [$track] NO-LIVE-CARDS - backlog gap, nothing to dispatch" }
                3 { $skipped    += [ordered]@{ track = $track; reason = 'CANNOT-DETERMINE'; detail = "$line" } ; Write-Output "LOOP: [$track] CANNOT-DETERMINE - $line" }
                default { $skipped += [ordered]@{ track = $track; reason = "exit-$rc"; detail = "$line" } ; Write-Output "LOOP: [$track] exit=$rc - $line" }
            }
        }
    }
}

$cycleEnd = (Get-Date).ToUniversalTime()
$receipt = [ordered]@{
    schema        = 'mlv-app/workstream-loop-cycle/v1'
    startedUtc    = $cycleStart.ToString('o')
    endedUtc      = $cycleEnd.ToString('o')
    durationSec   = [math]::Round(($cycleEnd - $cycleStart).TotalSeconds, 1)
    dryRun        = [bool]$DryRun
    tracks        = $Tracks
    maxPerCycle   = $MaxDispatchesPerCycle
    dailyBudget   = $DailyBudget
    dispatched    = $dispatched
    skipped       = $skipped
    receiptWriteFailures = $receiptWriteFailures
    haltedReason  = $halted
}
$receiptPath = Join-Path $CycleDir "cycle-$stamp.json"
[System.IO.File]::WriteAllText($receiptPath, ($receipt | ConvertTo-Json -Depth 6), [System.Text.UTF8Encoding]::new($false))
Write-Output "LOOP: cycle receipt $receiptPath (dispatched=$($dispatched.Count) skipped=$($skipped.Count))"

} finally {
    # Release before exit, not merely on the fall-through path: an unhandled throw anywhere in the
    # cycle body above must not leave the mutex held for the next scheduled firing.
    if ($gotMutex) { $cycleMutex.ReleaseMutex() | Out-Null }
    $cycleMutex.Dispose()
}
exit 0
