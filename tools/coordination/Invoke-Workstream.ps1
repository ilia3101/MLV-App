<#
.SYNOPSIS
    Pick the next actionable card on a workstream, build a COMPACT self-contained
    lane brief, and dispatch a bounded lane. No seat, no lease, no queue mutation.

.DESCRIPTION
    THIS SCRIPT EXISTS BECAUSE OF A MEASURED COST, NOT A STYLE PREFERENCE.

    On 2026-08-31 the same card was dispatched twice to the same lane:

      run 1  prompt 5.7 KB, no capability line, no reading budget
             -> 16 turns, 1,956,254 cache-creation tokens, DIED prompt_too_long, $51.61
      run 2  prompt 7.0 KB, facts inlined, byte-sized reading budget, capability stated
             -> 3 turns, 40,759 cache-creation tokens, $1.18

    A 48x reduction in tokens ingested and 44x in cost, from prompt shape alone.
    The lane in run 1 spent its context reading this board's own coordination
    surface, which is ~1.6 MB:
        .claude-state\coordination\gpu-lane-impl-review-sync.md   ~1.34 MB
        .claude-state\coordination\dual-lane\queue.json           ~270 KB

    So the token economy is NOT about the conversation. It is about never letting
    bulk state reach a model. This script reads the bulk itself, in PowerShell,
    and hands the lane a few KB with the facts already in it.

    THREE THINGS THE BRIEF ALWAYS CARRIES, because each was a measured failure:
      1. THE LANE'S REAL TOOLSET. Invoke-Lane grants tools BY ENGINE. A claude
         lane without -AllowEdits gets Read,Grep,Glob and NO SHELL; a codex lane
         gets ALL tools under a read-only sandbox and CAN execute. A prompt that
         says "prove by execution" is unsatisfiable for the former, and nothing
         told it so.
      2. A READING BUDGET WITH BYTE SIZES, so a lane knows which files will eat
         its context before it opens one.
      3. THE FACTS INLINED. The dispatcher already holds them; making the lane
         re-derive them is what costs the 1.9M tokens.

    QUEUE IS NEVER MUTATED. RESUME.md STEP 4/5 bars it under the recovery
    authority. Dispatch records go to their own append-only log.

.NOTES
    ASCII-only by project convention. The lane/model table lives in
    Invoke-Lane.ps1 and is deliberately NOT duplicated here.
#>
[CmdletBinding(DefaultParameterSetName='Dispatch')]
param(
    [Parameter(ParameterSetName='Dispatch')]
    [ValidateSet('factory','playback','product','continuity','fleet','gate','UNSET','auto')]
    [string]$Track = 'auto',

    [Parameter(ParameterSetName='Dispatch')]
    [Parameter(Mandatory=$true,ParameterSetName='Completion')]
    [string]$CardId,

    [Parameter(ParameterSetName='Dispatch')]
    [switch]$DryRun,

    [Parameter(ParameterSetName='Dispatch')]
    [ValidateSet('opus','sonnet','fable','sol','luna')]
    [string]$Lane,

    # Grant the dispatched lane write access via $D\Start-EditingLane.ps1 (never
    # Invoke-Lane.ps1 -AllowEdits directly - the wrapper is the only sanctioned
    # entry point for write access, and this script adds its own refusals on top
    # of the wrapper's). OFF by default: a read-only analysis lane is the norm.
    [Parameter(ParameterSetName='Dispatch')]
    [switch]$AllowEdits,

    [Parameter(ParameterSetName='Dispatch')]
    [int]$TimeoutSec = 1800,

    [Parameter(ParameterSetName='Dispatch')]
    [switch]$Force,
    [Parameter(ParameterSetName='Dispatch')]
    [int]$StaleHours = 12,

    # Skip the merged-PR landing probe entirely (offline, or gh deliberately not consulted).
    [Parameter(ParameterSetName='Dispatch')]
    [switch]$NoLandingProbe,

    # Read the queue from somewhere other than the canonical path. EXISTS FOR FALSIFICATION:
    # the landed-card guard below can only be proven by a queue in which a landed card is the
    # TOP pick, and the real queue must never be mutated to manufacture that. Never used in
    # production; the default is the canonical queue.
    [Parameter(ParameterSetName='Dispatch')]
    [string]$QueuePath = '',

    # Path to the pre-dispatch PR-review evidence exporter (deliverable 9, S126). EXISTS FOR
    # FALSIFICATION: a test points this at a fake exporter shim so the dispatcher's OWN wiring -
    # it calls the exporter before a review-lane starts, and refuses the dispatch when the
    # exporter refuses - can be proven without a real PR or a network call, matching the
    # existing fake-gh shim pattern. Never overridden in production; the default is the real
    # exporter beside this script.
    [Parameter(ParameterSetName='Dispatch')]
    [string]$ExporterPath = '',

    [Parameter(Mandatory=$true,ParameterSetName='Completion')]
    [switch]$RecordCompletion,
    [Parameter(Mandatory=$true,ParameterSetName='Completion')]
    [string]$CompletionLaneReceipt,
    [Parameter(Mandatory=$true,ParameterSetName='Completion')]
    [string]$CompletionReviewVerdictPath,
    [Parameter(Mandatory=$true,ParameterSetName='Completion')]
    [string]$CompletionWorktree,
    [Parameter(Mandatory=$true,ParameterSetName='Completion')]
    [string[]]$CompletionAllowedPath,
    [Parameter(Mandatory=$true,ParameterSetName='Completion')]
    [string[]]$CompletionTestReceiptPath,
    [Parameter(ParameterSetName='Completion')]
    [string[]]$CompletionArtifactPath = @(),
    [Parameter(Mandatory=$true,ParameterSetName='Completion')]
    [string]$CompletionOutputReceipt
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

# Completion is an explicit hub operation after independent review. Parameter
# sets prohibit dispatch flags; no queue, reservation, provider or board setup
# is reachable through this mode. The sibling adapter owns evidence validation.
if ($PSCmdlet.ParameterSetName -eq 'Completion') {
    if (-not $RecordCompletion) { throw 'completion-mode-requires-record-completion' }
    & (Join-Path $PSScriptRoot 'Record-WorkstreamCompletion.ps1') `
        -CardId $CardId -LaneReceipt $CompletionLaneReceipt `
        -ReviewVerdictPath $CompletionReviewVerdictPath -Worktree $CompletionWorktree `
        -AllowedPath $CompletionAllowedPath -TestReceiptPath $CompletionTestReceiptPath `
        -ArtifactPath $CompletionArtifactPath -OutputReceipt $CompletionOutputReceipt
    exit $LASTEXITCODE
}

# MLV_BOARD_ROOT: only a test sets it (a tmp-dir board fixture, mirroring Invoke-Lane.ps1's own
# resolution); the default is the real board. Needed so a test can point -AllowEdits worktree
# creation and Start-EditingLane.ps1 resolution at a throwaway git repo instead of the live board.
$RepoRoot   = if ($env:MLV_BOARD_ROOT) { $env:MLV_BOARD_ROOT } else { 'C:\!Layi Wkspc\MLV-App' }
$DualLane   = Join-Path $RepoRoot '.claude-state\coordination\dual-lane'
if (-not $QueuePath) { $QueuePath = Join-Path $DualLane 'queue.json' }
else { Write-Output "WORKSTREAM: NON-CANONICAL QUEUE in use: $QueuePath" }
$LogPath    = Join-Path $DualLane 'workstream-dispatch-log.jsonl'
$PromptDir  = Join-Path $RepoRoot '.claude-state\fleet-runs\prompts'
# S76: every lane start (editing or read-only) appends a 'reserved' row here BEFORE the lane
# runs, and a 'charged'/'refunded' row after - Invoke-WorkstreamLoop.ps1 reads this exact file
# for spentToday, never workstream-dispatch-log.jsonl.
$ReservationsPath = Join-Path $DualLane 'receipts\dispatch-reservations.jsonl'
$KillSwitch        = Join-Path $DualLane 'WORKSTREAM-LOOP-DISABLED'
# The one sanctioned entry point for write access (hub-owned, never edited from a lane).
$StartEditingLane  = Join-Path $DualLane 'Start-EditingLane.ps1'
# THE LANE RUNNER MUST COME FROM THE TREE THIS SCRIPT LIVES IN, NOT FROM $RepoRoot.
# Invoke-WorkstreamLoop pins a driver worktree to fork/master precisely so the unattended loop
# runs reviewed code - and then this line reached OUTSIDE that pin, back to the canonical
# checkout, which is parked on a peer branch (diag/async-h2d-preupload-exits). Measured
# 2026-09-03: every dispatched lane ran the DIAG BRANCH's Invoke-Lane.ps1, so PR #27's read
# deny-list, turn cap and spend telemetry had NO EFFECT on any production dispatch. A fable lane
# at 20:16Z - three hours after #27 merged - cost USD 22.01 at 948,598 cache-creation tokens,
# the exact unprotected signature #27 was written to stop, and its receipt carried no spend field.
# $PSScriptRoot is the pinned sibling when driven by the loop, and the local sibling when run by
# hand: correct in both cases, and it can never silently cross into another branch's checkout.
$LaneRunner = Join-Path $PSScriptRoot 'Invoke-Lane.ps1'
$ProductRatioGuard = Join-Path $PSScriptRoot 'Test-ProductRatioGuard.ps1'
if (-not $ExporterPath) { $ExporterPath = Join-Path $PSScriptRoot 'Export-PrReviewEvidence.ps1' }

foreach ($p in @($QueuePath, $LaneRunner, $ExporterPath)) {
    if (-not (Test-Path -LiteralPath $p)) {
        Write-Output "WORKSTREAM: CANNOT-DETERMINE - missing $p"
        exit 3
    }
}
if (-not (Test-Path -LiteralPath $PromptDir)) {
    New-Item -ItemType Directory -Path $PromptDir -Force | Out-Null
}

# A card in one of these states is done. Anything else is non-terminal (live) work.
$Terminal = @(
    'closed-fixed','closed-not-this-board','closed-root-caused','closed-superseded',
    'closed-transformed','landed','landed-evidence','landed-local-proof','CLEARED',
    'RETIRED','withdrawn','superseded','retracted-and-fixed','fixed','answered-folded'
)

# ALLOWLIST, not blocklist. Measured 2026-09-05: card B2-TOOLING-BASELINE carried
# state=booked, track=UNSET, priority=999 and was still dispatched, burning a lane slot on
# work no lane can act on - queue.json's OWN top-level "note" field defines 'booked' as "a
# named trigger, not schedulable". A blocklist here would require enumerating every state
# that ALSO names a party or trigger other than a lane (booked-rule, consult-open,
# deferred-nonblocking, open-risk, optional-validation, surfaced-awaiting-ordering,
# handed-off-awaiting-review, changes-requested-awaiting-acceptance-evidence,
# blocked-operator, ...) and the queue keeps growing that vocabulary. An allowlist of the
# states the note itself says a LANE owns needs no such enumeration: anything non-terminal
# and not in this list is reported, never dispatched.
#
# 'dispatched-untracked-target' IS schedulable, despite the name - CHECKED AGAINST THE LIVE
# QUEUE, not assumed: SIDECAR-COVERAGE-1 (track=factory, priority=7, owner=codex) carries
# it right now. Its own stateReason explains the word "untracked": the deliverable lives
# under gitignored .claude-state/, so it has no git-trackable range and no gate - "untracked"
# there means git tracking, unrelated to this script's own `track` field. The work is real
# and still owned by a lane; excluding this state would silently strand it, which is the
# same class of defect this fix exists to remove, just aimed at a different card.
$Schedulable = @('queued', 'dispatched', 'in-review', 'waiting-evidence', 'dispatched-untracked-target')

function Get-Prop($obj, [string]$name) {
    if ($null -eq $obj) { return $null }
    $p = $obj.PSObject.Properties[$name]
    if ($null -eq $p) { return $null }
    return $p.Value
}

function Get-Track($item) {
    $t = Get-Prop $item 'track'
    if (-not $t) { return 'UNSET' }
    return $t
}

$queue = Get-Content -LiteralPath $QueuePath -Raw | ConvertFrom-Json
$items = @($queue.items)
$live  = @($items | Where-Object { $Terminal -notcontains (Get-Prop $_ 'state') })

# ------------------------------------------------------------------ dispatch history
$history = @{}
$malformed = 0
if (Test-Path -LiteralPath $LogPath) {
    foreach ($line in (Get-Content -LiteralPath $LogPath)) {
        if (-not $line.Trim()) { continue }
        try {
            $row = $line | ConvertFrom-Json
            $id = Get-Prop $row 'cardId'
            if ($id) { $history[$id] = $row }
        } catch { $malformed++ }
    }
}
# A malformed row is COUNTED, never silently treated as "no dispatch". Absent and
# unreadable are different facts and this script will not merge them.
if ($malformed -gt 0) { Write-Output "WORKSTREAM: WARNING - $malformed malformed dispatch-log row(s) skipped" }

function Get-LastDispatchAgeHours([string]$id) {
    if (-not $history.ContainsKey($id)) { return $null }
    $t = Get-Prop $history[$id] 'dispatchedUtc'
    if (-not $t) { return $null }
    # TIMEZONE, MEASURED 2026-09-03. ConvertFrom-Json already materialises an ISO-8601 "...Z"
    # string as a [datetime] with Kind=Utc. The previous body called [datetime]::Parse($t) on
    # that object, which stringifies it to a zone-less local-looking form, re-parses it as
    # Kind=Unspecified, and then ToUniversalTime() adds the local offset a SECOND time. On this
    # host (UTC-5) a card dispatched 0.21 h ago computed as -4.79 h, so `$age -ge $StaleHours`
    # was false and the card was filtered for ~5 h longer than configured.
    # It is invisible on a UTC machine, which is why it survived: the bug's size IS the offset.
    if ($t -is [datetime]) { return ((Get-Date).ToUniversalTime() - $t.ToUniversalTime()).TotalHours }
    try {
        $parsed = [datetime]::Parse(
            [string]$t, [cultureinfo]::InvariantCulture,
            [System.Globalization.DateTimeStyles]::AdjustToUniversal -bor
            [System.Globalization.DateTimeStyles]::AssumeUniversal)
        return ((Get-Date).ToUniversalTime() - $parsed).TotalHours
    } catch { return $null }
}

# ------------------------------------------------------------------ landed-elsewhere probe
# WHY THIS EXISTS. Card liveness above is derived from queue.json's `state` field alone.
# queue.json is the DISPATCH authority - it is NOT the authority on whether the WORK landed,
# and this script never mutates it (RESUME.md STEP 4/5). So a card whose fix merged stays
# `dispatched` forever and this selector re-picks it every cycle, spending real model budget
# on work that is already on master. Measured 2026-09-03: the loop's first unattended fire
# dispatched a lane for SIDECAR-FIX-1, which PR #23 had merged nine hours earlier.
#
# RAW OUTRANKS THE QUEUE, so ask GitHub, not the card. THE MATCH RULES, THE LANDING VOCABULARY
# AND THE NEAR-MISS ASYMMETRY NOW LIVE IN ONE PLACE: landing-probe.ps1, in this directory.
# queue-derive.ps1 asks GitHub the same question for a different purpose, and two copies of this
# vocabulary would be widened once and left stale - the single most frequently paid failure on
# this board. Read that file for the rules and for the measured incident behind each one.
. (Join-Path $PSScriptRoot 'landing-probe.ps1')

# The permanent composer (plan 0.35). Needs the resolved branch name before an editing
# dispatch can create its worktree, not just the prompt text, so this script dot-sources
# the pure logic directly rather than shelling out to Compose-LanePrompt.ps1.
. (Join-Path $PSScriptRoot 'compose-lane-prompt-core.ps1')

function Get-DispatchSpendEvidence {
    param([string]$RunDir, [string]$Lane, [string]$Card, $ObservedExit)
    $laneCostReported = $false
    $evidence = [ordered]@{ receiptPath=$null; receiptSha256=$null; laneExitCode=$ObservedExit; laneCostUsd=$null; laneCostReported=$laneCostReported }
    try {
        $files = @(Get-ChildItem -LiteralPath $RunDir -Filter "$Lane-*.receipt.json" -File -ErrorAction Stop)
        if ($files.Count -ne 1 -or $files[0].Length -gt 1MB -or $files[0].Length -eq 0) { return $evidence }
        $bytes = [IO.File]::ReadAllBytes($files[0].FullName)
        $receipt = [Text.Encoding]::UTF8.GetString($bytes).TrimStart([char]0xfeff) | ConvertFrom-Json -ErrorAction Stop
        if ($receipt.schema -cne 'mlv-app/fleet-lane-receipt/v1' -or $receipt.lane -cne $Lane -or $receipt.card -cne $Card) { return $evidence }
        if (($receipt.exitCode -isnot [int] -and $receipt.exitCode -isnot [long]) -or $receipt.exitCode -ne $ObservedExit) { return $evidence }
        $cost = $receipt.spend.costUsd
        if ($receipt.spend.costReported -isnot [bool] -or -not $receipt.spend.costReported -or
            ($cost -isnot [int] -and $cost -isnot [long] -and $cost -isnot [double] -and $cost -isnot [decimal])) { return $evidence }
        $evidence.receiptPath = [IO.Path]::GetRelativePath($RepoRoot, $files[0].FullName)
        $evidence.receiptSha256 = [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($bytes)).ToLowerInvariant()
        $evidence.laneCostUsd = $cost
        $evidence.laneCostReported = $true
    } catch { }
    return $evidence
}

function Write-DispatchReservation {
    # APPENDED, never updated in place - a reservation is a fact about a point in time, not
    # a mutable record. Two rows per lane start: 'reserved' immediately before the process
    # launches, then 'charged' or 'refunded' once the outcome of actually launching it is
    # known. The loop refunds only terminal events with verified zero-spend evidence.
    param(
        [Parameter(Mandatory)][string]$ReservationId,
        [Parameter(Mandatory)][ValidateSet('reserved','charged','refunded')][string]$State,
        [string]$Card = '',
        [string]$Kind = '',
        [string]$Lane = '',
        [string]$RunDir = '',
        $ObservedExit = $null
    )
    $dir = Split-Path -Parent $ReservationsPath
    if (-not (Test-Path -LiteralPath $dir)) { New-Item -ItemType Directory -Path $dir -Force | Out-Null }
    $row = [ordered]@{
        reservationId = $ReservationId
        state         = $State
        card          = $Card
        kind          = $Kind
        lane          = $Lane
        runDir        = $RunDir
        recordedUtc   = (Get-Date).ToUniversalTime().ToString('o')
    }
    if ($State -ne 'reserved') {
        $evidence = Get-DispatchSpendEvidence -RunDir $RunDir -Lane $Lane -Card $Card -ObservedExit $ObservedExit
        foreach ($key in $evidence.Keys) { $row[$key] = $evidence[$key] }
        # A catch or missing receipt cannot establish that no provider started.
        # Unknown spend remains charged; success at zero cost remains charged.
        $row.state = if ($evidence.laneCostReported -and $evidence.laneCostUsd -eq 0 -and $null -ne $ObservedExit -and $ObservedExit -ne 0) { 'refunded' } else { 'charged' }
    }
    Add-Content -LiteralPath $ReservationsPath -Value ($row | ConvertTo-Json -Compress -Depth 6) -Encoding UTF8
    return [pscustomobject]$row
}

function Test-KillSwitchArmed { return (Test-Path -LiteralPath $KillSwitch) }

function Test-RatioDispatchPermission {
    param([AllowEmptyString()][string]$Kind = '')

    if (-not (Test-Path -LiteralPath $ProductRatioGuard -PathType Leaf)) {
        Write-Information -InformationAction Continue "WORKSTREAM: CANNOT-DETERMINE ratio-guard-missing path=$ProductRatioGuard"
        return 3
    }

    $guardText = @(& pwsh -NoProfile -NonInteractive -ExecutionPolicy Bypass -File $ProductRatioGuard -RepoRoot $RepoRoot)
    $guardExit = $LASTEXITCODE
    $guard = $null
    try {
        if ($guardText.Count -ne 1) { throw 'guard emitted other than one line' }
        $raw = [string]$guardText[0]
        $document = [System.Text.Json.JsonDocument]::Parse($raw)
        try {
            $root = $document.RootElement
            if ($root.ValueKind -ne [System.Text.Json.JsonValueKind]::Object) { throw 'guard root is not an object' }
            $required = @('schema','asOfUtc','windowStartUtc','windowEndUtc','sourceRef','sourceSha','commitPopulation','productCommitCount','productShare7d','productShareThreshold','recognizedProductPrCount','recognizedProductPrIds','unrecognizedProductLandings','landingProvenanceComplete','hasProductLandings','dispatchEvidenceSource','dispatchCoverage','dispatchEvidenceAvailable','dispatchesObserved','malformedDispatchRows','dispatchesPerLandedProductPr7dLowerBound','dispatchRateThreshold','verdict','reasons','errorCode')
            $actual = @($root.EnumerateObject() | ForEach-Object { $_.Name })
            if (@($actual | Select-Object -Unique).Count -ne $actual.Count) { throw 'duplicate guard fields' }
            if (@($actual | Where-Object { $required -notcontains $_ }).Count -ne 0 -or @($required | Where-Object { $actual -notcontains $_ }).Count -ne 0) { throw 'guard schema fields differ' }
            foreach ($name in @('schema','asOfUtc','windowStartUtc','windowEndUtc','sourceRef','sourceSha','dispatchEvidenceSource','dispatchCoverage','verdict')) {
                if ($root.GetProperty($name).ValueKind -ne [System.Text.Json.JsonValueKind]::String) { throw "invalid string field $name" }
            }
            foreach ($name in @('commitPopulation','productCommitCount','recognizedProductPrCount','dispatchesObserved','malformedDispatchRows')) {
                $value = 0L
                if (-not $root.GetProperty($name).TryGetInt64([ref]$value) -or $value -lt 0) { throw "invalid count $name" }
            }
            foreach ($name in @('landingProvenanceComplete','hasProductLandings','dispatchEvidenceAvailable')) {
                if (@([System.Text.Json.JsonValueKind]::True,[System.Text.Json.JsonValueKind]::False) -notcontains $root.GetProperty($name).ValueKind) { throw "invalid bool $name" }
            }
            foreach ($name in @('productShare7d','dispatchesPerLandedProductPr7dLowerBound','productShareThreshold','dispatchRateThreshold')) {
                $number = $root.GetProperty($name)
                if ($number.ValueKind -eq [System.Text.Json.JsonValueKind]::Null -and $name -in @('productShare7d','dispatchesPerLandedProductPr7dLowerBound')) { continue }
                if ($number.ValueKind -ne [System.Text.Json.JsonValueKind]::Number) { throw "invalid numeric field $name" }
                $value = $number.GetDouble()
                if ([double]::IsNaN($value) -or [double]::IsInfinity($value)) { throw "nonfinite field $name" }
            }
            foreach ($name in @('recognizedProductPrIds','unrecognizedProductLandings','reasons')) {
                if ($root.GetProperty($name).ValueKind -ne [System.Text.Json.JsonValueKind]::Array) { throw "invalid array $name" }
            }
        } finally { $document.Dispose() }

        $guard = $raw | ConvertFrom-Json -ErrorAction Stop
        if ($guard.schema -ne 'mlv-app/product-ratio-guard/v1') { throw 'invalid schema' }
        if (@('GREEN','RED','ERROR') -notcontains [string]$guard.verdict) { throw 'invalid verdict' }
        if ($guard.sourceSha -notmatch '^[0-9a-f]{40}$' -and -not ($guard.verdict -eq 'ERROR' -and $guard.sourceSha -eq '')) { throw 'invalid source sha' }
        if (@('PARTIAL','COMPLETE') -notcontains [string]$guard.dispatchCoverage) { throw 'invalid coverage' }
        if (@('none','dispatch-reservations','legacy-dispatch-log','unavailable') -notcontains [string]$guard.dispatchEvidenceSource) { throw 'invalid evidence source' }
        if ([double]$guard.productShareThreshold -ne 0.50 -or [double]$guard.dispatchRateThreshold -ne 4.0) { throw 'invalid thresholds' }

        $population = [long]$guard.commitPopulation
        $productCount = [long]$guard.productCommitCount
        $recognizedCount = [long]$guard.recognizedProductPrCount
        $observed = [long]$guard.dispatchesObserved
        $malformed = [long]$guard.malformedDispatchRows
        if ($productCount -gt $population) { throw 'product count exceeds population' }
        if (@($guard.recognizedProductPrIds).Count -ne $recognizedCount) { throw 'recognized count mismatch' }
        if (@($guard.recognizedProductPrIds | Select-Object -Unique).Count -ne $recognizedCount) { throw 'duplicate recognized PR id' }
        if (@($guard.recognizedProductPrIds | Where-Object { $_ -isnot [int] -and $_ -isnot [long] -or [long]$_ -le 0 }).Count -ne 0) { throw 'invalid recognized PR id' }
        if (@($guard.unrecognizedProductLandings | Where-Object { $_ -isnot [string] -or $_ -notmatch '^[0-9a-f]{40}$' }).Count -ne 0) { throw 'invalid unrecognized landing' }
        if (@($guard.unrecognizedProductLandings | Select-Object -Unique).Count -ne @($guard.unrecognizedProductLandings).Count) { throw 'duplicate unrecognized landing' }
        if (@($guard.reasons | Where-Object { $_ -isnot [string] -or [string]::IsNullOrWhiteSpace($_) }).Count -ne 0) { throw 'invalid reasons' }

        $expectedProvenance = @($guard.unrecognizedProductLandings).Count -eq 0
        $expectedLandings = ($recognizedCount + @($guard.unrecognizedProductLandings).Count) -gt 0
        if ($guard.verdict -ne 'ERROR' -and [bool]$guard.landingProvenanceComplete -ne $expectedProvenance) { throw 'provenance contradiction' }
        if ([bool]$guard.hasProductLandings -ne $expectedLandings) { throw 'landing contradiction' }
        if ([bool]$guard.dispatchEvidenceAvailable -ne ([string]$guard.dispatchEvidenceSource -notin @('none','unavailable'))) { throw 'evidence availability contradiction' }

        if ($population -eq 0) {
            if ($null -ne $guard.productShare7d -or $productCount -ne 0) { throw 'empty population contradiction' }
        } else {
            if ($null -eq $guard.productShare7d) { throw 'missing product share for populated history' }
            $share = [double]$guard.productShare7d
            if ([double]::IsNaN($share) -or [double]::IsInfinity($share) -or $share -lt 0 -or $share -gt 1) { throw 'invalid product share' }
            if ([math]::Abs($share - ([double]$productCount / [double]$population)) -gt 1e-12) { throw 'product share mismatch' }
        }

        if ($null -ne $guard.dispatchesPerLandedProductPr7dLowerBound) {
            $rateValue = [double]$guard.dispatchesPerLandedProductPr7dLowerBound
            if ([double]::IsNaN($rateValue) -or [double]::IsInfinity($rateValue) -or $rateValue -lt 0 -or -not [bool]$guard.dispatchEvidenceAvailable -or -not $expectedProvenance -or $recognizedCount -eq 0) { throw 'invalid dispatch rate' }
            if ([math]::Abs($rateValue - ([double]$observed / [double]$recognizedCount)) -gt 1e-12) { throw 'dispatch rate mismatch' }
        } elseif ([bool]$guard.dispatchEvidenceAvailable -and $expectedProvenance -and $recognizedCount -gt 0) { throw 'missing dispatch rate' }

        if ([string]$guard.verdict -eq 'GREEN') {
            if ($guardExit -ne 0 -or $guard.dispatchCoverage -ne 'COMPLETE' -or -not [bool]$guard.dispatchEvidenceAvailable -or -not $expectedProvenance -or -not $expectedLandings -or $malformed -ne 0 -or $null -eq $guard.productShare7d -or [double]$guard.productShare7d -lt [double]$guard.productShareThreshold -or $null -eq $guard.dispatchesPerLandedProductPr7dLowerBound -or [double]$guard.dispatchesPerLandedProductPr7dLowerBound -gt [double]$guard.dispatchRateThreshold) { throw 'contradictory GREEN' }
        } elseif ([string]$guard.verdict -eq 'ERROR') {
            if ($guardExit -ne 3 -or [string]::IsNullOrWhiteSpace([string]$guard.errorCode)) { throw 'invalid ERROR contract' }
        } elseif ($guardExit -ne 0 -or $null -ne $guard.errorCode) { throw 'invalid RED contract' }
    } catch {
        Write-Information -InformationAction Continue 'WORKSTREAM: CANNOT-DETERMINE ratio-guard-output-missing-or-malformed'
        return 3
    }

    $share = if ($null -eq $guard.productShare7d) { 'unavailable' } else { ([double]$guard.productShare7d).ToString('0.####', [Globalization.CultureInfo]::InvariantCulture) }
    $rate = if ($null -eq $guard.dispatchesPerLandedProductPr7dLowerBound) { 'unavailable' } else { ([double]$guard.dispatchesPerLandedProductPr7dLowerBound).ToString('0.####', [Globalization.CultureInfo]::InvariantCulture) }
    $reasons = @($guard.reasons) -join ','
    Write-Information -InformationAction Continue "WORKSTREAM: product_share_7d=$share"
    Write-Information -InformationAction Continue "WORKSTREAM: dispatches_per_landed_product_pr_7d_lower_bound=$rate coverage=$($guard.dispatchCoverage)"
    Write-Information -InformationAction Continue "WORKSTREAM: ratio-guard verdict=$($guard.verdict) reasons=$reasons"

    if ($guardExit -ne 0 -or [string]$guard.verdict -eq 'ERROR') {
        Write-Information -InformationAction Continue 'WORKSTREAM: CANNOT-DETERMINE ratio-guard-error'
        return 3
    }
    if ([string]$guard.verdict -eq 'RED') {
        if (@('product','playback') -contains $Kind) {
            Write-Information -InformationAction Continue "WORKSTREAM: ratio-guard allowed-under-red kind=$Kind"
            return 0
        }
        Write-Information -InformationAction Continue "WORKSTREAM: REFUSED ratio-guard-red kind=$Kind"
        return 6
    }
    return 0
}

# Lane resolution: the card's own `kind`/`owner` fields win (0.18 seeds them for every
# product/playback card), then a RECON:/REVIEW: scope prefix routes to the breadth-recon or
# review-guidance lane, then the legacy needsShell heuristic is the last resort for a card
# that carries none of the above (e.g. factory/UNSET-track cards, and every existing test
# fixture). Never consulted when the caller passed an explicit -Lane - that always wins.
function Get-ResolvedLane {
    param($Kind, $Owner, [string]$Scope, [bool]$NeedsShell)
    if (@('product', 'playback') -contains $Kind -and $Owner -eq 'sonnet') { return 'sonnet' }
    if ($Scope -match '^RECON:') { return 'luna' }
    if ($Scope -match '^REVIEW:') { return 'fable' }
    return $(if ($NeedsShell) { 'luna' } else { 'fable' })
}

$landedById = @{}
$landedHow = @{}
$nearMisses = @()
$landingProbe = 'skipped'
if (-not $NoLandingProbe) {
    $probe = Get-CardLandingEvidence -CardId @($live | ForEach-Object { Get-Prop $_ 'id' } | Where-Object { $_ })
    $landingProbe = $probe.status
    foreach ($id in $probe.landed.Keys) {
        $landedById[$id] = $probe.landed[$id]
        $landedHow[$id]  = $probe.landed[$id].how
    }
    $nearMisses = @($probe.nearMiss)
    if ($nearMisses.Count -gt 0) {
        Write-Output ("WORKSTREAM: landing-probe NEAR-MISS ($($nearMisses.Count)): " +
            ($nearMisses -join ', ') + " - named in a merged PR without a landing verb, NOT skipped")
    }
}
Write-Output "WORKSTREAM: landing-probe $landingProbe"
foreach ($id in $landedById.Keys) {
    $pr = $landedById[$id]
    Write-Output ("WORKSTREAM: SKIP-LANDED card={0} merged in PR #{1} ({2}) [matched by {4}] but queue.json still says '{3}'. The queue is STALE on this card; this script does not mutate it - reconcile it." -f `
        $id, $pr.number, $pr.title, (Get-Prop (@($live | Where-Object { (Get-Prop $_ 'id') -eq $id })[0]) 'state'), $landedHow[$id])
}

# ------------------------------------------------------------------ schedulability
# TWO independent rules, both structural (like $Terminal above): neither is bypassable by
# -Force, which only ever waived the landed-probe and staleness filters below.
#   1. state must be in $Schedulable.
#   2. AUTO track selection must not fall through to an untracked card. 'UNSET' remains a
#      real, INTENTIONAL dispatch target (Invoke-WorkstreamLoop.ps1 rotates through it
#      explicitly as its own -Track value) - this only closes the silent fallthrough where
#      the default $Track='auto' pool never filtered by track at all, so an untracked card
#      was always a candidate whenever the tracked pools ran dry.
function Get-NotSchedulableReason($item) {
    $state = Get-Prop $item 'state'
    if ($Schedulable -notcontains $state) {
        return "state '$state' is not in the schedulable allowlist (queued, dispatched, in-review, waiting-evidence, dispatched-untracked-target) - queue.json's own note names this class a named trigger, not schedulable"
    }
    if ($Track -eq 'auto' -and (Get-Track $item) -eq 'UNSET') {
        # NAMED DELIBERATELY UNLIKE THE STATE 'dispatched-untracked-target' above, which
        # means something unrelated (its DELIVERABLE is untracked BY GIT). This is about the
        # QUEUE'S OWN `track` FIELD (factory/playback/product/...) being absent.
        return 'no-board-track-set - auto-select requires an explicit track field on the card; pass -Track UNSET to pick this card on purpose'
    }
    return $null
}

foreach ($item in $live) {
    $reason = Get-NotSchedulableReason $item
    if ($reason) {
        Write-Output ("WORKSTREAM: NOT-SCHEDULABLE card={0} state={1} reason={2}" -f (Get-Prop $item 'id'), (Get-Prop $item 'state'), $reason)
    }
}

# ------------------------------------------------------------------ selection
$script:WarnedNonNumericPriority = @{}
function Get-Rank($item) {
    # Unset priority sorts LAST, so an unprioritised card never outranks a p1.
    $pr = Get-Prop $item 'priority'
    if ($null -eq $pr) { return 999 }
    $n = 0
    if ([int]::TryParse([string]$pr, [ref]$n)) { return $n }

    # A bare [int] cast here THROWS on a non-numeric string, and the throw lands inside
    # Sort-Object -Property { Get-Rank $_ }, so the whole selection dies -- while the script
    # still exits 0. The loop then records a normal cycle that dispatched nothing. Silent
    # starvation is worse than a halt, because nothing reports it.
    #
    # This is not hypothetical: queue.json carries prose in this field on DISPATCH-CDX-14
    # ("HIGHEST - outranks the control-plane three") and DISPATCH-CDX-15 ("CRITICAL PATH -
    # the cap is now two blocks away, not one"). Both happen to be terminal today, so they
    # never reach this function -- the dispatcher is one live prose-priority card away from
    # dispatching nothing at all, and the authoring habit that produces them is demonstrated.
    #
    # Rank it as unset and SAY SO on stderr. Not stdout: the [WORKSTREAM] lines are parsed,
    # and emitting into a Sort-Object property block would make the sort key an array.
    $id = [string](Get-Prop $item 'id')
    if (-not $script:WarnedNonNumericPriority.ContainsKey($id)) {
        $script:WarnedNonNumericPriority[$id] = $true
        [Console]::Error.WriteLine(("WORKSTREAM: NON-NUMERIC PRIORITY on card '{0}': {1}. Ranked as unset (999) so selection continues; give the card a numeric priority to restore its rank." -f $id, ([string]$pr).Trim()))
    }
    return 999
}

if ($CardId) {
    $candidates = @($items | Where-Object { (Get-Prop $_ 'id') -eq $CardId })
    if ($candidates.Count -eq 0) {
        Write-Output "WORKSTREAM: CANNOT-DETERMINE - no card with id '$CardId'"
        exit 3
    }
} else {
    $pool = @($live | Where-Object { -not (Get-NotSchedulableReason $_) })
    if ($Track -ne 'auto') {
        $pool = @($pool | Where-Object { (Get-Track $_) -eq $Track })
    }
    # Landed-elsewhere cards are excluded here, never earlier: $live must keep its meaning
    # ("non-terminal per the queue") so the NO-LIVE-CARDS vs ALL-FILTERED distinction below
    # still reports the truth about the track.
    if ($landedById.Count -gt 0 -and -not $Force) {
        $pool = @($pool | Where-Object { -not $landedById.ContainsKey((Get-Prop $_ 'id')) })
    }
    if (-not $Force) {
        $pool = @($pool | Where-Object {
            $age = Get-LastDispatchAgeHours (Get-Prop $_ 'id')
            ($null -eq $age) -or ($age -ge $StaleHours)
        })
    }
    $candidates = @($pool | Sort-Object -Property `
        @{ Expression = { Get-Rank $_ } }, `
        @{ Expression = { Get-Prop $_ 'id' } })
}

if ($candidates.Count -eq 0) {
    # THREE OUTCOMES, NEVER TWO. "no card exists" and "every card was filtered out"
    # are different facts about the board and this script will not merge them - an
    # empty track is a GAP to be filled, a fully-dispatched track is HEALTHY.
    $onTrackLive = if ($Track -eq 'auto') { $live.Count }
                   else { @($live | Where-Object { (Get-Track $_) -eq $Track }).Count }
    if ($onTrackLive -eq 0) {
        Write-Output "WORKSTREAM: NO-LIVE-CARDS on track '$Track'. This track has ZERO non-terminal work - it is not idle, it is EMPTY. That is a backlog gap, not a healthy queue. (live overall = $($live.Count))"
        exit 4
    }
    # EXIT 5, NOT 0. This script preaches "three outcomes, never two" and then returned the
    # SAME code for "I dispatched a lane" and "I dispatched nothing". Invoke-WorkstreamLoop
    # counts exit 0 as a dispatch, so every ALL-RECENTLY-DISPATCHED cycle consumed a slot of
    # -MaxDispatchesPerCycle and inflated the cycle receipt with work that never happened.
    # Measured 2026-09-03: cycle-20260903T180119Z recorded dispatched=1 with no dispatch-log
    # row and no run directory to match it.
    Write-Output "WORKSTREAM: ALL-RECENTLY-DISPATCHED on track '$Track' - $onTrackLive live card(s), every one carrying a dispatch newer than $StaleHours h. Use -Force or lower -StaleHours to re-dispatch."
    exit 5
}

$card      = $candidates[0]
$cardId    = Get-Prop $card 'id'
$cardTrack = Get-Track $card

# ------------------------------------------------------------------ engine choice
# Cards asking for derivation or measurement need a SHELL. Only codex lanes have
# one when invoked read-only, so route those to codex and pure analysis to claude.
$cardText   = ($card | ConvertTo-Json -Depth 8)
$needsShell = $cardText -match '(?i)re-derive|derive|measure|reproduce|proving command|prove by|verify by execution|run the'
$cardKind   = Get-Prop $card 'kind'
$cardOwner  = Get-Prop $card 'owner'
$cardScope  = [string](Get-Prop $card 'scope')
if (-not $Lane) { $Lane = Get-ResolvedLane -Kind $cardKind -Owner $cardOwner -Scope $cardScope -NeedsShell $needsShell }
$engine = if ($Lane -eq 'sol' -or $Lane -eq 'luna') { 'codex' } else { 'claude' }

# The run directory is named here, not at dispatch, because the brief has to be able to NAME
# files that this script is about to write into it. -DryRun still exports, and still writes into
# this path, so what you inspect under -DryRun is byte-identical to what a lane would receive.
$stamp  = (Get-Date).ToUniversalTime().ToString('yyyyMMddTHHmmssZ')
$runDir = Join-Path $RepoRoot ".claude-state\fleet-runs\ws-$cardId-$stamp"

# DISPATCH-ATTEMPT RECEIPT. Every attempt that names a run directory leaves a typed
# dispatch-attempt.json in it: 'launching' (about to start the lane), then 'launched' (the lane process
# ran and returned; its own receipt sits beside this one), or 'refused-before-launch' with the cause,
# or 'launch-unconfirmed' (a throw between 'launching' and the child returning). MEASURED 2026-09-14: PLAY-COUNTERS-CPU left
# ~90 run dirs holding ONLY lane-prompt.md - `git worktree add` failed after the prompt was written;
# the exit-3 reason reached only a detail line in a separate loop-cycles receipt, so the run dirs
# themselves read as launches that silently produced nothing. Never throws: a receipt write failure is reported on stdout AND
# stderr (the disk that refused the receipt is the one fact no receipt can carry), never fatal.
# -DryRun writes one too (outcome 'dry-run-not-launched'): it names a run dir and writes into it.
function Write-DispatchAttempt {
    param([string]$Outcome, [string]$Cause, [int]$ExitCode, [string]$Detail = '', $LaneExitCode = $null)
    try {
        if (-not (Test-Path -LiteralPath $runDir)) { New-Item -ItemType Directory -Path $runDir -Force | Out-Null }
        $laneReceipts = @(Get-ChildItem -LiteralPath $runDir -Filter '*.receipt.json' -File -ErrorAction SilentlyContinue | ForEach-Object { $_.FullName })
        $attempt = [ordered]@{
            schema       = 'mlv-app/workstream-dispatch-attempt/v1'
            outcome      = $Outcome
            cause        = $Cause
            detail       = $Detail
            card         = $cardId
            track        = $cardTrack
            lane         = $Lane
            engine       = $engine
            allowEdits   = [bool]$AllowEdits
            exitCode     = $ExitCode
            laneExitCode = $LaneExitCode
            laneReceipts = $laneReceipts
            runDir       = $runDir
            recordedUtc  = (Get-Date).ToUniversalTime().ToString('o')
        }
        # dispatch-attempt.json is the LATEST state; dispatch-attempts.jsonl is the append-only history of
        # every transition (sol PR #111 post-merge: a single overwritten file lost 'launching' and any
        # refusal cause a later trap replaced).
        [System.IO.File]::AppendAllText((Join-Path $runDir 'dispatch-attempts.jsonl'), (($attempt | ConvertTo-Json -Depth 4 -Compress) + "`n"), [System.Text.UTF8Encoding]::new($false))
        [System.IO.File]::WriteAllText((Join-Path $runDir 'dispatch-attempt.json'), ($attempt | ConvertTo-Json -Depth 4), [System.Text.UTF8Encoding]::new($false))
    } catch {
        $why = "WORKSTREAM: dispatch-attempt receipt NOT written ($($_.Exception.Message)) runDir=$runDir"
        # FALLBACK SPOOL outside the run dir, so an unwritable run dir still leaves a typed record a
        # reader can find. Only when the spool also fails is stdout/stderr the last channel; the loop
        # copies these lines into its cycle receipt (receiptWriteFailures).
        $runDirError = $_.Exception.Message
        try {
            $spool = Join-Path $RepoRoot '.claude-state\fleet-runs\dispatch-attempt-spool'
            if (-not (Test-Path -LiteralPath $spool)) { New-Item -ItemType Directory -Path $spool -Force | Out-Null }
            $spooled = [ordered]@{
                schema = 'mlv-app/workstream-dispatch-attempt/v1'; outcome = $Outcome; cause = $Cause; detail = $Detail
                card = $cardId; lane = $Lane; exitCode = $ExitCode; laneExitCode = $LaneExitCode; runDir = $runDir
                runDirWriteError = $runDirError; recordedUtc = (Get-Date).ToUniversalTime().ToString('o')
            }
            $spoolFile = Join-Path $spool ('{0}-{1}-{2}.json' -f $cardId, (Get-Date).ToUniversalTime().ToString('yyyyMMddTHHmmssfffZ'), $Outcome)
            [System.IO.File]::WriteAllText($spoolFile, ($spooled | ConvertTo-Json -Depth 4), [System.Text.UTF8Encoding]::new($false))
            $why += " spooled=$spoolFile"
        } catch {
            $why += " spool ALSO failed ($($_.Exception.Message))"
        }
        Write-Output $why
        [Console]::Error.WriteLine($why)
    }
}

# A TERMINATING ERROR is an exit path too (sol PR #111 R1): New-Item, the prompt write, Get-FileHash,
# reservation writes and worktree cleanup can all throw after the run dir is named. A trap applies to
# the whole script scope, so it is guarded on $runDir existing; `break` re-throws, so the process
# still fails exactly as before - it just no longer fails silently.
# LaneStarting is set just before the child pwsh call; LaneLaunched only AFTER it returns (sol PR #111
# post-merge: setting 'launched' before the call receipted a start failure as a launch).
$script:LaneStarting = $false
$script:LaneLaunched = $false
trap {
    if (Get-Variable -Name runDir -Scope Script -ErrorAction SilentlyContinue) {
        $trapOutcome = if ($script:LaneLaunched) { 'launched' } elseif ($script:LaneStarting) { 'launch-unconfirmed' } else { 'refused-before-launch' }
        $trapLaneExit = if (Get-Variable -Name laneExit -Scope Script -ErrorAction SilentlyContinue) { $script:laneExit } else { $null }
        Write-DispatchAttempt -Outcome $trapOutcome -Cause 'unhandled-error' -ExitCode 1 -Detail $_.Exception.Message -LaneExitCode $trapLaneExit
    }
    break
}

# ------------------------------------------------------------------ pre-dispatch PR review evidence
# DELIVERABLE 9 (S126): before every review-lane dispatch, run the SAME exporter the hub ran by
# hand for the three PRs that landed before this card - from this card on, the DISPATCHER is the
# exporter. A review lane (fable, routed here by the REVIEW: scope rule; sol, the adversarial
# verifier, when explicitly requested) reads pr-<n>-checks.json / pr-<n>-review.json instead of
# calling `gh` itself, exactly like the generic HOSTED GITHUB EVIDENCE section below - this is the
# NARROW, deliverable-9-specific contract sol-review-PR-TEMPLATE.md actually consumes, and unlike
# that generic export (which fails OPEN), a review with unverified evidence is worse than no
# review at all, so a failed export here REFUSES the dispatch rather than proceeding anyway.
#
# Scoped to a card that names a PR to review (`prNumber`): a review-lane card with no PR to bind
# to has no subject for this exporter, and dispatching it without hosted evidence is already
# covered by the generic export below.
$isReviewLane = ($Lane -eq 'fable' -or $Lane -eq 'sol')
$cardPrNumber = Get-Prop $card 'prNumber'
if ($isReviewLane -and $cardPrNumber) {
    Write-Output "WORKSTREAM: pre-dispatch review-evidence export pr=$cardPrNumber card=$cardId runDir=$runDir"
    & pwsh -NoProfile -NonInteractive -ExecutionPolicy Bypass -File $ExporterPath `
        -PrNumber ([int]$cardPrNumber) -RunDir $runDir -RepoRoot $RepoRoot
    $exporterExit = $LASTEXITCODE
    if ($exporterExit -ne 0) {
        Write-Output "WORKSTREAM: REFUSED review-evidence-export-failed card=$cardId pr=$cardPrNumber exit=$exporterExit"
        Write-DispatchAttempt -Outcome 'refused-before-launch' -Cause 'review-evidence-export-failed' -ExitCode 6 -Detail "pr=$cardPrNumber exporterExit=$exporterExit"
        exit 6
    }
}

# ------------------------------------------------------------------ hosted GitHub evidence
# WHY THIS EXISTS, AND WHY A WARNING IN THE BRIEF WAS NOT ENOUGH.
#
# MEASURED 2026-09-05. Two priority-1 cards, FACTORY-MATURITY-1-CLAUDE and FACTORY-MATURITY-1-OPUS,
# consumed two lane slots each and both returned the SAME wall: `gh` answers "Access is denied"
# inside the read-only lane sandbox, so every hosted fact stayed UNVERIFIED. The same `gh`, the same
# token and the same machine work from an interactive session - `gh` resolves its token through the
# Windows credential keyring, and the sandbox denies that read. The failure is the VENUE, not the
# installation and not the credential.
#
# PR #55 had ALREADY told lanes that `gh` may be unusable and that saying so is a FINDING. Both
# lanes did exactly that, correctly, and were dispatched into the wall anyway - because the brief
# only taught them how to REPORT the limitation, never removed it. A WARNING IN A PROMPT IS NOT A
# FIX; IT IS A NICER WAY TO FAIL. This script already runs `gh` for the landing probe from the venue
# where it works, so the evidence was always one command away from the process doing the dispatching.
#
# THE ASYMMETRY IS THE OPPOSITE OF THE LANDING PROBE'S, and that is deliberate. There, a false
# positive SKIPS live work, so the match is kept narrow. Here, a false positive costs a handful of
# read-only API calls and ~30 KB in a run directory nobody reads, while a false NEGATIVE costs a
# whole lane slot. So the trigger below is broad ON PURPOSE. Over-exporting is not a defect.
#
# FAIL-OPEN, exactly like the landing probe: if `gh` is missing, denied, offline or slow, the brief
# SAYS SO IN THOSE WORDS and the lane is dispatched anyway. Silence would put the lane straight back
# into the wall while looking like a clean brief.
# Gated off for an -AllowEdits dispatch: an editing lane gets a real shell (unlike the
# read-only claude lane this export exists for) and composes its prompt from a card
# procedure, not from the $ghSection this export feeds - so exporting here would be pure
# waste (API calls, run-dir bytes) with nothing downstream ever reading the result.
$needsHostedEvidence = (-not $AllowEdits) -and ($cardText -match '(?i)\bgh\b|github|branch protection|ruleset|status check|actions run|workflow run|ci history|consecutive failure|hosted evidence|pull request|\bPR #')
$ghRows    = @()   # one row per attempted export: name, file, bytes-or-reason
$ghSection = ''

if ($needsHostedEvidence) {
    $ghDir = Join-Path $runDir 'github-evidence'
    New-Item -ItemType Directory -Path $ghDir -Force | Out-Null

    # A FIXED, PREDICTABLE SET. Not derived from the card text: a lane must be able to rely on the
    # same filenames every time, and a card-shaped guess would silently omit whatever the card
    # forgot to name. Anything missing is a FINDING the lane reports, not a gap it works around.
    $ghJobs = [ordered]@{
        'repo-metadata.json' = @(
            'api','repos/layibabalola/MLV-App','--jq',
            '{visibility,private,fork,archived,default_branch,pushed_at}')
        'branch-protection-master.json' = @(
            'api','repos/layibabalola/MLV-App/branches/master/protection')
        'rulesets.json' = @('api','repos/layibabalola/MLV-App/rulesets')
        'runs-tests-master-push-completed.json' = @(
            'run','list','--repo','layibabalola/MLV-App','--workflow','tests.yml',
            '--branch','master','--event','push','--status','completed','--limit','50',
            '--json','databaseId,conclusion,createdAt,headSha,displayTitle')
    }

    $gh = Get-Command gh -ErrorAction SilentlyContinue
    if (-not $gh) {
        $ghRows += [pscustomobject]@{ Name = '(all)'; Result = 'CANNOT-DETERMINE: gh not on PATH' }
    } else {
        foreach ($name in $ghJobs.Keys) {
            $outFile = Join-Path $ghDir $name
            $errFile = "$outFile.err.txt"
            try {
                $ghArgs = $ghJobs[$name]
                & gh @ghArgs 1>$outFile 2>$errFile
                $code = $LASTEXITCODE
            } catch {
                $code = -1
                Set-Content -LiteralPath $errFile -Value $_.Exception.Message -Encoding UTF8
            }
            $len = if (Test-Path -LiteralPath $outFile) { (Get-Item -LiteralPath $outFile).Length } else { 0 }
            if ($code -eq 0 -and $len -gt 0) {
                Remove-Item -LiteralPath $errFile -ErrorAction SilentlyContinue
                $ghRows += [pscustomobject]@{ Name = $name; Result = "$len bytes" }
            } else {
                # THE REASON IS KEPT, NOT JUST THE EXIT CODE. "Access is denied" and "404" send a
                # reader to completely different places, and collapsing both to "gh failed" is how
                # a venue problem gets misfiled as an authorization problem for a second time.
                $why = ''
                if (Test-Path -LiteralPath $errFile) {
                    $why = ((Get-Content -LiteralPath $errFile -Raw) -replace '\s+', ' ').Trim()
                }
                if (-not $why) { $why = "gh exit $code, no stderr" }
                if ($why.Length -gt 160) { $why = $why.Substring(0, 160) + '...' }
                Remove-Item -LiteralPath $outFile -ErrorAction SilentlyContinue
                $ghRows += [pscustomobject]@{ Name = $name; Result = "FAILED - $why" }
            }
        }

        # The job graph is a SECOND call keyed on the first one's newest row, so it can only be made
        # after the run list lands. Skipped without complaint when that export failed.
        $runsFile = Join-Path $ghDir 'runs-tests-master-push-completed.json'
        if (Test-Path -LiteralPath $runsFile) {
            try {
                $runs = @(Get-Content -LiteralPath $runsFile -Raw | ConvertFrom-Json |
                          Sort-Object -Property createdAt -Descending)
                if ($runs.Count -gt 0) {
                    $newest  = $runs[0].databaseId
                    $jobsOut = Join-Path $ghDir "jobs-latest-$newest.json"
                    $jobsErr = "$jobsOut.err.txt"
                    & gh api "repos/layibabalola/MLV-App/actions/runs/$newest/jobs" 1>$jobsOut 2>$jobsErr
                    $jcode = $LASTEXITCODE
                    $jlen  = if (Test-Path -LiteralPath $jobsOut) { (Get-Item -LiteralPath $jobsOut).Length } else { 0 }
                    if ($jcode -eq 0 -and $jlen -gt 0) {
                        Remove-Item -LiteralPath $jobsErr -ErrorAction SilentlyContinue
                        $ghRows += [pscustomobject]@{ Name = "jobs-latest-$newest.json"; Result = "$jlen bytes (run $newest, newest by createdAt)" }
                    } else {
                        Remove-Item -LiteralPath $jobsOut -ErrorAction SilentlyContinue
                        $ghRows += [pscustomobject]@{ Name = "jobs-latest-$newest.json"; Result = "FAILED - gh exit $jcode" }
                    }
                }
            } catch {
                $ghRows += [pscustomobject]@{ Name = '(job graph)'; Result = "FAILED - $($_.Exception.Message)" }
            }
        }
    }

    $ghLines  = ($ghRows | ForEach-Object { "    {0,-42} {1}" -f $_.Name, $_.Result }) -join "`n"
    $ghOk     = @($ghRows | Where-Object { $_.Result -notlike 'FAILED*' -and $_.Result -notlike 'CANNOT-DETERMINE*' }).Count
    $ghFailed = $ghRows.Count - $ghOk

    # THE HEADING AND THE CLOSING INSTRUCTION BOTH TRACK THE OUTCOME. A section headed "already
    # exported for you" that closes with "read these files" above a list of five FAILED rows tells
    # the lane to read files that do not exist - which is the same class of defect as the brief line
    # that told two lanes their own correct observation must be false.
    if ($ghOk -eq 0) {
        $ghHead    = 'HOSTED GITHUB EVIDENCE - THE EXPORT FAILED. THERE IS NOTHING TO READ.'
        $ghVerdict = @"
EVERY EXPORT FAILED ($ghFailed of $($ghRows.Count)) - the rows above carry the exact reason.
So the hosted facts are UNVERIFIED and you must SAY SO rather than substituting a remembered value.
This is a VENUE finding about the dispatcher, not a failing of yours and not ``operator-only``.
Do the parts of the card that need no hosted evidence, and report the rest as UNVERIFIED.
"@
    } else {
        # Single backticks here, NOT doubled: this string is INTERPOLATED into the here-string
        # below as a value, so the here-string's backtick escaping never runs over it.
        $ghHead    = 'HOSTED GITHUB EVIDENCE - ALREADY EXPORTED FOR YOU. DO NOT RUN `gh`.'
        $ghVerdict = if ($ghFailed -eq 0) {
            "ALL $ghOk EXPORTS SUCCEEDED. Read them instead of calling ``gh``: they are plain JSON`nfrom the GitHub API, so cite the file name and the field you relied on."
        } else {
            "$ghOk EXPORT(S) SUCCEEDED, $ghFailed FAILED - the rows above say why. Read the ones that landed`nand cite file and field. Treat every fact the failed ones would have carried as UNVERIFIED and`nSAY SO; do not substitute a remembered value."
        }
    }

    $ghSection = @"

## $ghHead
This card needs facts that live on GitHub. ``gh`` DOES NOT WORK FROM YOUR SANDBOX - it reads its
token from the Windows credential keyring and your sandbox denies that read, so it answers
``Access is denied``, which looks like an authorization failure and is not one. MEASURED 2026-09-05:
two lanes burned two slots each proving exactly that. So this dispatcher, which runs in a venue
where ``gh`` usually works, ran the calls for you. Attempted at $stamp into:
    $ghDir
$ghLines
$ghVerdict
DO NOT RETRY ``gh`` YOURSELF - it will fail the same way and cost you the card. If you need a fact
that is not in the exports, do not work around it: report it as a FINDING naming the exact ``gh``
command that would produce it, so the export set can be widened.
"@
}

if (-not $AllowEdits) {
    # ==================================================================== read-only dispatch
    # ------------------------------------------------------------------ the brief
    if ($engine -eq 'codex') {
        $capability = @'
You are invoked READ-ONLY on the codex engine: ALL tools under a `read-only` sandbox.
YOU CAN EXECUTE (git, python, pwsh) but CANNOT write files. Prove by EXECUTION and PRINT the
ref you bound to. Run a FALSIFIER beside every subject check - a control and a subject that
return the same reason prove nothing.
'@
    } else {
        $capability = @'
You are invoked READ-ONLY on a claude engine. YOUR ONLY TOOLS ARE `Read`, `Grep`, `Glob`.
YOU HAVE NO SHELL - `Bash` and `PowerShell` calls are DENIED by the runner, so no git, no
python, no pwsh. The board's standing "prove by EXECUTION" rule is UNSATISFIABLE for you on
this invocation; that is a known runner asymmetry, not your failing. Substitute: cite a FILE
PATH AND LINE via Grep/Read, and label anything you could not check
CANNOT-VERIFY-WITHOUT-SHELL. Never fold CANNOT-VERIFY into a pass.
'@
    }

    $cardJson = $card | ConvertTo-Json -Depth 8
    $fence    = '```'

    $brief = @"
# LANE BRIEF - card $cardId (track: $cardTrack)

## YOUR CAPABILITIES ON THIS INVOCATION - read before planning
$capability
$ghSection
## HARD READING BUDGET - a previous lane died of prompt_too_long after 16 turns and 51 dollars
It blew its context reading this board's coordination surface whole. NEVER read these without a
narrow offset/limit, or a grep-then-slice:
    .claude-state\coordination\gpu-lane-impl-review-sync.md   ~1.34 MB
    .claude-state\coordination\dual-lane\queue.json           ~270 KB
    .claude-state\RESUME.md                                   ~27 KB (fine to read once)
THE CARD IS INLINED BELOW IN FULL. You do not need to open the queue. Budget ~12 tool calls.

## THOSE PATHS ARE AT THE BOARD ROOT, NOT IN YOUR WORKDIR. ABSENT THERE MEANS NOTHING.
``.claude-state/`` is GITIGNORED, so it is never checked out into a worktree - and your workDir IS a
worktree. Resolve those paths against the BOARD ROOT by absolute path, never against your cwd.
MEASURED 2026-09-05: a lane on C2-PROV-1 reported the content-review gate file had been ``rotated
away`` because it was not in the driver worktree. It was intact at the board root: 1,342,684 bytes,
modified 2026-08-26, exactly where closeout.config.json points. The worktree even HAS a nearly-empty
``.claude-state/``, so the absence reads as deletion rather than as never-having-been-there.
DO NOT report a ``.claude-state`` file missing, deleted or rotated away unless you checked the board
root by absolute path. brokered_closeout.py already resolves it that way (GATE-ID-4); only
hand-reading gets this wrong.

## STANDING OPERATOR RULING (2026-08-31), binding on your output
Layi, verbatim: "use the wisdom of the hub lanes to adjudicate decisions rather than ask me my
opinion. I am not qualified." DO NOT return "ask Layi". Return a DECISION. If some part is
genuinely operator-only you must be able to write this line in full, or it is not operator-only:
    BLOCKED ON Layi (<action>) -- delegation check: <lane> <why not> ... Operator-only because
    <1 physical access | 2 external account/UI-only surface | 3 policy-reserved>.
NOTE: ``gh`` IS installed and authenticated on this box, so GitHub PR create/merge is NOT
operator-only. A GitHub blocker is USUALLY wrong - but VERIFY before asserting either way.
MEASURED 2026-09-05: two lanes (FACTORY-MATURITY-1-CLAUDE and -OPUS) both got ``gh auth status`` ->
``Access is denied`` under a read-only sandbox, and both correctly returned UNVERIFIED. This brief
previously stated flatly that gh IS authenticated and that any GitHub blocker is wrong - which told
both lanes their own correct observation must be false. If ``gh`` fails for you, say so plainly and
name it a VENUE limitation: that is a real finding, not a lane error, and it is NOT the same as
``operator-only``. Do not spend the card working around it. AND SINCE 2026-09-05 YOU SHOULD NOT
NEED TO: when a card wants hosted facts, this dispatcher runs the ``gh`` calls itself, from the
venue where they work, and a HOSTED GITHUB EVIDENCE section above names the exported JSON. If that
section is absent, this card was not classified as needing hosted evidence - report that as the
finding rather than reaching for ``gh``.

## STANDING ROUTING FACT
This host (VIRTUAL-TEN) is a VMware VM with ZERO NVIDIA hardware. CUDA build and GPU playback
legs are ROUTED to the GPU hosts (\\bachelor\mlv-agent). "CUDA blocked locally" is a ROUTING
decision, never a lane blocker.
A PATH UNDER \\bachelor\... OR C:\mlvtmp\mlv-agent\... IS ON ANOTHER MACHINE. That whole
root does not exist here, so Test-Path/Get-FileHash on it from this host reports MISSING for
EVERY file it contains. A LOCAL PROBE OF A REMOTE PATH IS A STATEMENT ABOUT THIS HOST, NOT
ABOUT THE FILE. Report it as UNVERIFIED and name the venue - never as absent, and never as
contradicting a deployment claim. A control proving your HASH CHECK discriminates does NOT
prove you tested the right MACHINE: a falsifier on the mechanism is not a falsifier on the
venue. (Measured 2026-09-04: a lane read PresentMon MISSING at C:\mlvtmp\mlv-agent\cache\
from this VM and reported it as contradicting the card, while the GPU host was unreachable.)

## MEASUREMENT DISCIPLINE THAT ALREADY COST THIS BOARD REAL TIME
- EVERY PLAYBACK NUMBER CARRIES ITS CONFIGURATION OR IT CARRIES NOTHING. Legs here have run with
  MLVAPP_PLAYBACK_SCALE_FACTOR=1 while the shipping default is scale 4 (HighQuality), and the
  overrides appear in NO downstream artifact - only in smoke-stdout.txt prose.
- The stage-timing clock was 1 ms until 2026-09-02. Any median-based attribution computed before
  that is biased toward "unattributed"; the mean is the unbiased estimator.
- An empty grep is a statement about the SEARCH, not about the tree.
- A 0% or 100% rate is a STRUCTURAL claim, not an extreme measurement.
- With n=1, report the observation and STOP.

## THE CARD, verbatim from queue.json
$fence json
$cardJson
$fence

## WHAT I NEED
1. State the card's CURRENT truth: is its blocker still real? Run or cite the proving command.
   A blocker repeated without a fresh proving run is not a blocker, it is a memory.
2. If it is actionable, DO THE ANALYSIS and return the decision, with evidence.
3. If it is NOT actionable, say precisely why, and name the party in the format above.
4. Name anything you find that contradicts the card's own text. A card that describes a retired
   mechanism is worse than an empty card.
5. End with a block headed DECISION containing: the card's live status, the single next action,
   and who owns it.
"@

    $promptPath = Join-Path $PromptDir ("ws-$cardId-$stamp.md")
    [System.IO.File]::WriteAllText($promptPath, $brief, [System.Text.UTF8Encoding]::new($false))

    $briefKb   = [math]::Round(($brief.Length / 1KB), 1)
    $onTrack   = @($live | Where-Object { (Get-Track $_) -eq $cardTrack }).Count

    Write-Output "WORKSTREAM: track=$cardTrack card=$cardId priority=$(Get-Rank $card) state=$(Get-Prop $card 'state')"
    Write-Output "WORKSTREAM: lane=$Lane engine=$engine needsShell=$needsShell briefKB=$briefKb"
    Write-Output "WORKSTREAM: live cards on this track = $onTrack (live overall = $($live.Count))"
    Write-Output "WORKSTREAM: prompt=$promptPath"
    if ($needsHostedEvidence) {
        $okCount = @($ghRows | Where-Object { $_.Result -notlike 'FAILED*' -and $_.Result -notlike 'CANNOT-DETERMINE*' }).Count
        Write-Output "WORKSTREAM: gh-evidence $okCount/$($ghRows.Count) export(s) ok -> $(Join-Path $runDir 'github-evidence')"
    } else {
        Write-Output 'WORKSTREAM: gh-evidence not-needed (card text names no hosted-evidence subject)'
    }

    if ($DryRun) {
        # The exports above ALREADY RAN and are on disk. Saying "nothing dispatched" without saying
        # that would be a lie by omission about a directory this command created.
        Write-Output 'WORKSTREAM: DRY RUN - no lane dispatched. Any gh-evidence export above is real and on disk.'
        Write-DispatchAttempt -Outcome 'dry-run-not-launched' -Cause 'dry-run' -ExitCode 0
        exit 0
    }

    # Kill switch re-checked IMMEDIATELY before this lane starts (deliverable 7) - not only once
    # at the top of the loop's cycle, which can dispatch several lanes across a single cycle.
    if (Test-KillSwitchArmed) {
        Write-Output "WORKSTREAM: REFUSED kill-switch-armed card=$cardId"
        Write-DispatchAttempt -Outcome 'refused-before-launch' -Cause 'kill-switch-armed' -ExitCode 6
        exit 6
    }

    $ratioExit = Test-RatioDispatchPermission -Kind $cardKind
    if ($ratioExit -ne 0) {
        Write-DispatchAttempt -Outcome 'refused-before-launch' -Cause 'product-ratio-guard' -ExitCode $ratioExit
        exit $ratioExit
    }

    # Reservation events (deliverable 7, S76): APPENDED, never updated in place. 'reserved' is
    # written before launch. The budget counts reservations and refunds only verified
    # zero-spend terminal events; absent/ambiguous outcomes stay spent.
    $reservationId = [guid]::NewGuid().ToString()
    $null = Write-DispatchReservation -ReservationId $reservationId -State 'reserved' -Card $cardId -Kind $cardKind -Lane $Lane -RunDir $runDir

    Write-DispatchAttempt -Outcome 'launching' -Cause 'lane-starting' -ExitCode 0
    $script:LaneStarting = $true
    $laneExit = $null
    $reservationOutcome = 'charged'
    try {
        & pwsh -NoProfile -NonInteractive -ExecutionPolicy Bypass -File $LaneRunner `
            -Lane $Lane -PromptFile $promptPath -Card $cardId -RunDir $runDir -TimeoutSec $TimeoutSec
        $laneExit = $LASTEXITCODE
        $script:LaneLaunched = $true
        $reservationOutcome = 'charged'
    } finally {
        $reservationRecord = Write-DispatchReservation -ReservationId $reservationId -State $reservationOutcome -Card $cardId -Kind $cardKind -Lane $Lane -RunDir $runDir -ObservedExit $laneExit
    }

    $record = [ordered]@{
        schema          = 'mlv-app/workstream-dispatch/v1'
        cardId          = $cardId
        track           = $cardTrack
        priority        = (Get-Rank $card)
        stateAtDispatch = (Get-Prop $card 'state')
        lane            = $Lane
        engine          = $engine
        needsShell      = [bool]$needsShell
        promptPath      = $promptPath
        promptBytes     = $brief.Length
        runDir          = $runDir
        ghEvidence      = if ($needsHostedEvidence) { @($ghRows | ForEach-Object { "$($_.Name)=$($_.Result)" }) } else { @() }
        dispatchedUtc   = (Get-Date).ToUniversalTime().ToString('o')
        laneExitCode    = $laneExit
        laneCostUsd     = $reservationRecord.laneCostUsd
        laneCostReported = $reservationRecord.laneCostReported
    }
    Add-Content -LiteralPath $LogPath -Value ($record | ConvertTo-Json -Compress) -Encoding UTF8

    Write-DispatchAttempt -Outcome 'launched' -Cause 'lane-returned' -ExitCode 0 -LaneExitCode $laneExit
    Write-Output "WORKSTREAM: dispatched, laneExit=$laneExit runDir=$runDir"
    exit 0
}

# ==================================================================== editing dispatch
# Reached only when $AllowEdits: the read-only branch above always exits before falling through.
    # Refusal 1: a codex lane can never be granted write access (no Claude hook is visible to
    # codex exec). Start-EditingLane.ps1 refuses this too, but failing here means the refusal
    # reason lands in stdout - and so in the loop's cycle receipt - before any process starts.
    if ($Lane -eq 'sol' -or $Lane -eq 'luna') {
        Write-Output "WORKSTREAM: REFUSED codex-lane-never-edits lane=$Lane card=$cardId"
        Write-DispatchAttempt -Outcome 'refused-before-launch' -Cause 'codex-lane-never-edits' -ExitCode 6
        exit 6
    }

    # Refusal 2: an editing lane composes its prompt from the card's OWN procedure, never from
    # the generic read-only analysis brief below - so a card with no procedure, or whose tracked
    # file has drifted from what the queue recorded, cannot be granted write access at all.
    # procedureSha256 is REQUIRED, never optional (mirrors check_roadmap_queue_parity.py's rule).
    $procedureRel = Get-Prop $card 'procedure'
    $procedureSha = Get-Prop $card 'procedureSha256'
    if (-not $procedureRel -or -not $procedureSha) {
        Write-Output "WORKSTREAM: REFUSED procedure-missing-or-drifted card=$cardId reason=no-procedure-or-sha"
        Write-DispatchAttempt -Outcome 'refused-before-launch' -Cause 'procedure-missing-or-drifted' -ExitCode 6 -Detail 'no-procedure-or-sha'
        exit 6
    }
    $procedurePath = Join-Path $RepoRoot $procedureRel
    if (-not (Test-Path -LiteralPath $procedurePath)) {
        Write-Output "WORKSTREAM: REFUSED procedure-missing-or-drifted card=$cardId reason=file-missing path=$procedurePath"
        Write-DispatchAttempt -Outcome 'refused-before-launch' -Cause 'procedure-missing-or-drifted' -ExitCode 6 -Detail "file-missing path=$procedurePath"
        exit 6
    }
    $actualProcedureSha = (Get-FileHash -LiteralPath $procedurePath -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($actualProcedureSha -ne ([string]$procedureSha).ToLowerInvariant()) {
        Write-Output ("WORKSTREAM: REFUSED procedure-missing-or-drifted card=$cardId reason=sha-mismatch " +
            "recorded=$procedureSha actual=$actualProcedureSha")
        Write-DispatchAttempt -Outcome 'refused-before-launch' -Cause 'procedure-missing-or-drifted' -ExitCode 6 -Detail "sha-mismatch recorded=$procedureSha actual=$actualProcedureSha"
        exit 6
    }

    $laneWorkDir = Join-Path 'C:\mlvtmp' "lane-$cardId-$stamp"

    $baseSha = (& git -C $RepoRoot rev-parse fork/master 2>$null | Select-Object -First 1)
    if (-not $baseSha -or $baseSha -notmatch '^[0-9a-f]{40}$') {
        Write-Output "WORKSTREAM: CANNOT-DETERMINE - could not resolve fork/master to a full sha at $RepoRoot"
        Write-DispatchAttempt -Outcome 'refused-before-launch' -Cause 'cannot-determine-base-sha' -ExitCode 3
        exit 3
    }

    # GH-CAPABILITY drives which of the two ratified PR_STEP literals the composer inserts.
    # Absent or unreadable is NOT fatal - it just means the safer ('otherwise') literal is
    # used, matching Get-PrStepLiteral's own default (anything other than the one ratified
    # 'lane-can-open-pr' string takes that branch).
    $ghCapability = 'unknown'
    $ghCapPath = Join-Path $DualLane 'lane-gh-capability.json'
    if (Test-Path -LiteralPath $ghCapPath) {
        try {
            $capRec = Get-Content -LiteralPath $ghCapPath -Raw | ConvertFrom-Json
            if (($capRec.PSObject.Properties.Name -contains 'ghCapability') -and $capRec.ghCapability) {
                $ghCapability = [string]$capRec.ghCapability
            }
        } catch { }
    }

    if (-not (Test-Path -LiteralPath $runDir)) { New-Item -ItemType Directory -Path $runDir -Force | Out-Null }
    $templatePath = Join-Path $DualLane 'prompts\v2\product-card-TEMPLATE.md'

    # Refusal 3 (unknown-field): thrown by the composer itself when a fields file carries a
    # top-level label outside the COMPOSER CONTRACT - nothing is silently dropped.
    try {
        $composed = Get-ComposedLanePrompt -ProcedurePath $procedurePath -TemplatePath $templatePath `
            -WorkDir $laneWorkDir -BaseSha $baseSha -RunDir $runDir -Ts $stamp -GhCapability $ghCapability
    } catch {
        $composerMsg = $_.Exception.Message
        if ($composerMsg -like 'unknown-field:*') {
            Write-Output "WORKSTREAM: REFUSED unknown-field card=$cardId detail=$composerMsg"
        } else {
            Write-Output "WORKSTREAM: REFUSED procedure-missing-or-drifted card=$cardId detail=$composerMsg"
        }
        $composerCause = if ($composerMsg -like 'unknown-field:*') { 'unknown-field' } else { 'procedure-missing-or-drifted' }
        Write-DispatchAttempt -Outcome 'refused-before-launch' -Cause $composerCause -ExitCode 6 -Detail $composerMsg
        exit 6
    }

    $branch = $composed.Branch
    $promptPath = Join-Path $runDir 'lane-prompt.md'
    [System.IO.File]::WriteAllText($promptPath, $composed.Text, [System.Text.UTF8Encoding]::new($false))

    # ------------------------------------------------------- worktree (deliverable 5)
    # baseSha is fork/master resolved HERE, at dispatch time - never the plan's fixed
    # diagnosis-base sha - and is recorded on the receipt below. A real branch checkout, never
    # --detach: the composed procedure itself tells the lane to `git switch -c` this branch, so
    # the worktree must already be on it.
    #
    # A branch left behind by an earlier attempt (the lane worktree is removed, the branch ref is
    # not) used to make `worktree add -b` fail on every later cycle - the loop logged
    # "worktree add failed" for PLAY-COUNTERS-CPU every 45 min from 2026-09-10. Reuse such a
    # branch ONLY when it carries nothing beyond baseSha, and move it to baseSha first so the lane
    # never starts from a stale tip. A branch with commits not in baseSha holds work this dispatch
    # must not overwrite: refuse the card instead (exit 6, the loop's skip-this-track code).
    & git -C $RepoRoot show-ref --verify --quiet "refs/heads/$branch" 2>$null
    $branchExists = switch ($LASTEXITCODE) { 0 { $true } 1 { $false } default { $null } }
    if ($null -eq $branchExists) {
        Write-Output "WORKSTREAM: CANNOT-DETERMINE - could not check whether branch $branch exists"
        Write-DispatchAttempt -Outcome 'refused-before-launch' -Cause 'cannot-determine-branch-exists' -ExitCode 3 -Detail "branch=$branch"
        exit 3
    }
    if ($branchExists) {
        $uniqueOut = @(& git -C $RepoRoot rev-list "$baseSha..refs/heads/$branch" 2>$null)
        if ($LASTEXITCODE -ne 0) {
            Write-Output "WORKSTREAM: CANNOT-DETERMINE - rev-list failed for existing branch $branch"
            Write-DispatchAttempt -Outcome 'refused-before-launch' -Cause 'cannot-determine-branch-commits' -ExitCode 3 -Detail "branch=$branch"
            exit 3
        }
        $unique = @($uniqueOut | Where-Object { $_ })
        if ($unique.Count -gt 0) {
            Write-Output "WORKSTREAM: REFUSED existing-branch-has-work card=$cardId branch=$branch commits=$($unique.Count)"
            Write-DispatchAttempt -Outcome 'refused-before-launch' -Cause 'existing-branch-has-work' -ExitCode 6 -Detail "branch=$branch commits=$($unique.Count)"
            exit 6
        }
        # Fails (and is reported) if the branch is checked out in another worktree.
        & git -C $RepoRoot branch -f $branch $baseSha 2>&1 | Out-Null
        if ($LASTEXITCODE -ne 0) {
            Write-Output "WORKSTREAM: CANNOT-DETERMINE - could not move existing branch $branch to $baseSha (checked out elsewhere?)"
            Write-DispatchAttempt -Outcome 'refused-before-launch' -Cause 'cannot-move-existing-branch' -ExitCode 3 -Detail "branch=$branch baseSha=$baseSha"
            exit 3
        }
        Write-Output "WORKSTREAM: reusing existing branch $branch (no commits beyond baseSha), moved to $baseSha"
        & git -C $RepoRoot -c core.longpaths=true worktree add $laneWorkDir $branch 2>&1 | Out-Null
    } else {
        & git -C $RepoRoot -c core.longpaths=true worktree add -b $branch $laneWorkDir $baseSha 2>&1 | Out-Null
    }
    if ($LASTEXITCODE -ne 0) {
        Write-Output "WORKSTREAM: CANNOT-DETERMINE - git worktree add failed for $laneWorkDir at $baseSha (branch $branch)"
        Write-DispatchAttempt -Outcome 'refused-before-launch' -Cause 'worktree-add-failed' -ExitCode 3 -Detail "workDir=$laneWorkDir baseSha=$baseSha branch=$branch"
        exit 3
    }

    $script:LastWorktreeDisposition = $null
    function Remove-LaneWorktreeIfClean([string]$WorkDirToCheck) {
        # Loaded lazily and fail-closed: a dispatcher copied without its helper (test fixtures copy
        # dependencies by name) must KEEP the worktree with a reason, never abort the dispatch.
        if (-not (Get-Command Invoke-RetireLaneWorktree -ErrorAction SilentlyContinue)) {
            try { . (Join-Path $PSScriptRoot 'Retire-LaneWorktree.ps1') } catch {
                $script:LastWorktreeDisposition = [ordered]@{ action = 'kept'; reason = "cannot-determine: Retire-LaneWorktree.ps1 not loadable: $($_.Exception.Message)" }
                Write-Output "WORKSTREAM: worktree left in place ($($script:LastWorktreeDisposition.reason)): $WorkDirToCheck"
                return $false
            }
        }
        # Never removes a worktree the lane left dirty, unpushed or unmerged - the SAFE gate in
        # Retire-LaneWorktree.ps1 decides, without --force, and the disposition (with its
        # reason) is recorded on the dispatch record, so nothing a lane produced is silently
        # discarded. The previous `worktree remove --force` after a porcelain-only check
        # also deleted git-ignored evidence and never looked for unpushed commits.
        # MergeTarget is the ref baseSha was resolved from: local master can lag fork/master,
        # which would wrongly keep a worktree whose HEAD is still exactly baseSha.
        $disp = Invoke-RetireLaneWorktree -WorkDir $WorkDirToCheck -MergeTarget 'fork/master' `
            -QuarantineRoot (Join-Path $RepoRoot ('.claude-state\disk-hygiene\quarantine\lane-exit\' + (Get-Date).ToUniversalTime().ToString('yyyyMMdd')))
        $script:LastWorktreeDisposition = $disp
        if ($disp.action -eq 'retired') { return $true }
        Write-Output "WORKSTREAM: worktree left in place ($($disp.reason)): $WorkDirToCheck"
        return $false
    }

    Write-Output "WORKSTREAM: track=$cardTrack card=$cardId priority=$(Get-Rank $card) state=$(Get-Prop $card 'state')"
    Write-Output "WORKSTREAM: lane=$Lane engine=$engine allowEdits=True branch=$branch"
    Write-Output "WORKSTREAM: workDir=$laneWorkDir"
    Write-Output "WORKSTREAM: baseSha=$baseSha"
    Write-Output "WORKSTREAM: prompt=$promptPath"

    if ($DryRun) {
        # The worktree above is REAL, exactly like the gh-evidence export in the read-only path
        # is real under -DryRun: what you inspect is byte-identical to what a lane would receive.
        # Nothing ran in it, so it is guaranteed clean - remove it rather than leaving debris.
        Write-Output 'WORKSTREAM: DRY RUN - no lane dispatched. Worktree and prompt above were real; the worktree is retired if it passes the SAFE gate.'
        # The function also emits status lines, so its pipeline output is an array (always truthy);
        # decide from the recorded disposition instead.
        # Receipt BEFORE cleanup, so the cause is on disk even if cleanup hangs or the process is
        # killed. A cleanup THROW still overwrites it with 'unhandled-error' (single-file receipt;
        # append-only semantics are deferred to TOOL-DISPATCH-ATTEMPT-WRITE-FAILURE-1).
        Write-DispatchAttempt -Outcome 'dry-run-not-launched' -Cause 'dry-run' -ExitCode 0
        Remove-LaneWorktreeIfClean $laneWorkDir | Out-Host
        if ($script:LastWorktreeDisposition -and $script:LastWorktreeDisposition.action -eq 'retired') { Write-Output "WORKSTREAM: DRY RUN worktree retired: $laneWorkDir" }
        exit 0
    }

    # Kill switch re-checked IMMEDIATELY before this lane starts, not only once at the top of
    # the loop's cycle: a long cycle can dispatch several lanes, and the switch may be armed
    # between the cycle's own check and this particular start.
    if (Test-KillSwitchArmed) {
        Write-Output "WORKSTREAM: REFUSED kill-switch-armed card=$cardId"
        Write-DispatchAttempt -Outcome 'refused-before-launch' -Cause 'kill-switch-armed' -ExitCode 6
        Remove-LaneWorktreeIfClean $laneWorkDir | Out-Null
        exit 6
    }

    $ratioExit = Test-RatioDispatchPermission -Kind $cardKind
    if ($ratioExit -ne 0) {
        Write-DispatchAttempt -Outcome 'refused-before-launch' -Cause 'product-ratio-guard' -ExitCode $ratioExit
        Remove-LaneWorktreeIfClean $laneWorkDir | Out-Null
        exit $ratioExit
    }

    # Reservation events (deliverable 7, S76): APPENDED, never updated in place. 'reserved' is
    # written before launch. Zero-spend refunds require a bound terminal receipt;
    # uncertain launch failures cannot create budget.
    $reservationId = [guid]::NewGuid().ToString()
    $null = Write-DispatchReservation -ReservationId $reservationId -State 'reserved' -Card $cardId -Kind $cardKind -Lane $Lane -RunDir $runDir

    Write-DispatchAttempt -Outcome 'launching' -Cause 'lane-starting' -ExitCode 0
    $script:LaneStarting = $true
    $laneExit = $null
    $reservationOutcome = 'charged'
    try {
        & pwsh -NoProfile -NonInteractive -ExecutionPolicy Bypass -File $StartEditingLane `
            -Lane $Lane -PromptFile $promptPath -WorkDir $laneWorkDir -Card $cardId -RunDir $runDir `
            -ExtraReadDir $runDir -TimeoutSec $TimeoutSec
        $laneExit = $LASTEXITCODE
        $script:LaneLaunched = $true
        # The terminal writer derives any refund from the actual receipt bytes.
        $reservationOutcome = 'charged'
    } finally {
        $reservationRecord = Write-DispatchReservation -ReservationId $reservationId -State $reservationOutcome -Card $cardId -Kind $cardKind -Lane $Lane -RunDir $runDir -ObservedExit $laneExit
    }

    $cleanRemoved = Remove-LaneWorktreeIfClean $laneWorkDir

    $record = [ordered]@{
        schema          = 'mlv-app/workstream-dispatch/v1'
        cardId          = $cardId
        track           = $cardTrack
        priority        = (Get-Rank $card)
        stateAtDispatch = (Get-Prop $card 'state')
        lane            = $Lane
        engine          = $engine
        allowEdits      = $true
        workDir         = $laneWorkDir
        baseSha         = $baseSha
        branch          = $branch
        promptPath      = $promptPath
        runDir          = $runDir
        worktreeRemoved = $cleanRemoved
        worktreeDisposition = $script:LastWorktreeDisposition
        dispatchedUtc   = (Get-Date).ToUniversalTime().ToString('o')
        laneExitCode    = $laneExit
        laneCostUsd     = $reservationRecord.laneCostUsd
        laneCostReported = $reservationRecord.laneCostReported
    }
    Add-Content -LiteralPath $LogPath -Value ($record | ConvertTo-Json -Compress) -Encoding UTF8

    Write-DispatchAttempt -Outcome 'launched' -Cause 'lane-returned' -ExitCode 0 -LaneExitCode $laneExit
    Write-Output "WORKSTREAM: dispatched, laneExit=$laneExit runDir=$runDir workDir=$laneWorkDir"
    exit 0
