<#
.SYNOPSIS
    THE PRE-DISPATCH EXPORTER for a review-lane dispatch (plan 0.35, S126). Runs BEFORE a `sol`
    review lane starts, from the venue where `gh` actually works, and writes the hosted evidence
    that sol-review-PR-TEMPLATE.md tells its lane to read instead of calling `gh` itself.

.DESCRIPTION
    THE HUB WAS THE EXPORTER for the three PRs that land before this card; from this card on the
    DISPATCHER is (hub-procedure.md, S108/S126). Same reason as the read-only lane's own hosted
    evidence export in Invoke-Workstream.ps1: a read-only sandbox denies `gh` its Windows credential
    keyring read, so a lane that tries `gh` itself burns a whole slot proving a venue fact the
    dispatcher already knew.

    Reads the PR and the master branch-protection required-context set TWICE - once before
    `gh pr checks`, once after - because a check run or a force-push can land mid-export, and a
    reviewer bound to a stale head or a since-changed required-context set is reviewing the wrong
    thing. Refuses (non-zero exit, no partial evidence trusted) on:
      - head drift:            headRefOidBefore != headRefOidAfter
      - required-context drift: requiredContextsBefore (as a set) != requiredContextsAfter
    A required context absent from the exported checks (never run, or run and not 'SUCCESS') is
    reported in the review export as a FAILURE, not silently folded into either verdict - S121.

    Exports (into -RunDir):
      pr-<n>-checks.json  = { checks, retrievedUtc }
      pr-<n>-review.json  = { number, stateBefore, stateAfter, headRefOidBefore, headRefOidAfter, requiredContextsBefore,
                               requiredContextsAfter, body, checks, missingRequiredContexts,
                               retrievedUtc }

.NOTES
    ASCII-only by project convention. -GhExe exists for falsification: a test points it at a fake
    shim so the exporter's OWN logic (pinned repo, drift refusal, missing-context reporting) can be
    proven without a network call or a real PR.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory)][int]$PrNumber,
    [Parameter(Mandatory)][string]$RunDir,
    [string]$RepoRoot = 'C:\!Layi Wkspc\MLV-App',
    [string]$GhExe = 'gh'
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

# HARDCODED, NOT A PARAMETER (sol round-1 review, MAJOR 3). A -Repo parameter is a caller-
# overridable pin, which is no pin at all - any caller (including a future dispatcher call
# site) could point this exporter at a different repository and every downstream "-R
# layibabalola/MLV-App on every call" guarantee would still hold locally while being wrong
# globally. Removing the parameter, rather than merely validating it, means there is no argv
# path that can ever carry a different value in.
$Repo = 'layibabalola/MLV-App'

function Write-Utf8NoBom([string]$Path, [string]$Content) {
    [System.IO.File]::WriteAllText($Path, $Content, [System.Text.UTF8Encoding]::new($false))
}

function Get-PrView {
    param([string]$GhExe, [string]$Repo, [int]$PrNumber)
    $out = & $GhExe pr view -R $Repo $PrNumber --json number,headRefOid,body,state 2>$null
    if ($LASTEXITCODE -ne 0 -or -not $out) { throw "gh pr view failed (exit $LASTEXITCODE) for PR #$PrNumber on $Repo" }
    return ($out | ConvertFrom-Json)
}

function Get-RequiredContexts {
    param([string]$GhExe, [string]$Repo)
    $out = & $GhExe api "repos/$Repo/branches/master/protection" --jq '.required_status_checks.contexts' 2>$null
    if ($LASTEXITCODE -ne 0) { throw "gh api branch protection failed (exit $LASTEXITCODE) for $Repo" }
    if (-not $out -or -not $out.Trim()) { return @() }
    return @($out | ConvertFrom-Json)
}

if (-not (Test-Path -LiteralPath $RunDir)) { New-Item -ItemType Directory -Path $RunDir -Force | Out-Null }

# FAIL CLOSED ON FETCH FAILURE (sol round-1 review, MAJOR 2). A nonzero exit here used to be
# silently discarded (piped to Out-Null with no check), so a lane could be dispatched to review
# objects this exporter never actually fetched - the head/base sha checks below would then run
# against whatever was ALREADY local, which is exactly the stale-review risk this exporter
# exists to close.
$fetchOutput = & git -C $RepoRoot fetch fork --quiet 2>&1
if ($LASTEXITCODE -ne 0) {
    Write-Output "REFUSED: git-fetch-failed pr=$PrNumber exit=$LASTEXITCODE detail=$($fetchOutput -join ' ')"
    exit 3
}

$prBefore = Get-PrView -GhExe $GhExe -Repo $Repo -PrNumber $PrNumber
$requiredBefore = @(Get-RequiredContexts -GhExe $GhExe -Repo $Repo)

$headSha = [string]$prBefore.headRefOid
$baseSha = (& git -C $RepoRoot rev-parse fork/master 2>$null | Select-Object -First 1)
# EACH VALUE VALIDATED AS A FULL 40-HEX SHA BEFORE THE COMMIT-EXISTENCE CHECK (sol round-1
# review, MAJOR 2). The previous form skipped cat-file entirely for a falsy value ("if ($sha)"),
# so an empty or short sha silently passed with no objects ever verified - the exact case a
# reviewer bound to "whatever headRefOid happened to be" is reviewing nothing.
foreach ($pair in @(@{ Field = 'head'; Sha = $headSha }, @{ Field = 'base'; Sha = $baseSha })) {
    if (-not $pair.Sha -or $pair.Sha -notmatch '^[0-9a-f]{40}$') {
        Write-Output "REFUSED: pr-sha-invalid field=$($pair.Field) value=$($pair.Sha) pr=$PrNumber"
        exit 3
    }
    & git -C $RepoRoot cat-file -e "$($pair.Sha)^{commit}" 2>$null
    if ($LASTEXITCODE -ne 0) {
        Write-Output "REFUSED: pr-object-missing sha=$($pair.Sha) pr=$PrNumber"
        exit 3
    }
}

$checksOut = & $GhExe pr checks -R $Repo $PrNumber --json name,state,link 2>$null
if ($LASTEXITCODE -ne 0 -or -not $checksOut) {
    Write-Output "REFUSED: gh-pr-checks-failed pr=$PrNumber exit=$LASTEXITCODE"
    exit 3
}
$checks = @($checksOut | ConvertFrom-Json)

$prAfter = Get-PrView -GhExe $GhExe -Repo $Repo -PrNumber $PrNumber
$requiredAfter = @(Get-RequiredContexts -GhExe $GhExe -Repo $Repo)

$headBefore = [string]$prBefore.headRefOid
$headAfter = [string]$prAfter.headRefOid
if ($headBefore -ne $headAfter) {
    Write-Output "REFUSED: pr-head-drift before=$headBefore after=$headAfter pr=$PrNumber"
    exit 3
}
$reqBeforeSorted = @($requiredBefore | Sort-Object)
$reqAfterSorted = @($requiredAfter | Sort-Object)
if (($reqBeforeSorted -join '|') -ne ($reqAfterSorted -join '|')) {
    Write-Output "REFUSED: required-context-drift before=$($reqBeforeSorted -join ',') after=$($reqAfterSorted -join ',') pr=$PrNumber"
    exit 3
}

$successNames = @($checks | Where-Object { [string]$_.state -match '^(?i)success$' } | ForEach-Object { [string]$_.name })
$missingRequiredContexts = @($requiredBefore | Where-Object { $successNames -notcontains $_ })
foreach ($m in $missingRequiredContexts) {
    Write-Output "EXPORT: missing-required-context $m pr=$PrNumber"
}

$retrievedUtc = (Get-Date).ToUniversalTime().ToString('o')

$checksRecord = [ordered]@{
    checks       = $checks
    retrievedUtc = $retrievedUtc
}
$checksPath = Join-Path $RunDir "pr-$PrNumber-checks.json"
Write-Utf8NoBom $checksPath ($checksRecord | ConvertTo-Json -Depth 8)

$reviewRecord = [ordered]@{
    # sol-review-PR-TEMPLATE.md binds a verdict to the PR itself (S114); without number and state
    # the export cannot prove WHICH PR it describes or whether it is open or merged (sol PR #111
    # post-merge and PR #115 reviews, both CHANGES_REQUESTED on this alone).
    number                  = [int]$prBefore.number
    stateBefore             = [string]$prBefore.state
    stateAfter              = [string]$prAfter.state
    headRefOidBefore        = $headBefore
    headRefOidAfter         = $headAfter
    requiredContextsBefore  = $requiredBefore
    requiredContextsAfter   = $requiredAfter
    body                    = [string]$prBefore.body
    checks                  = $checks
    missingRequiredContexts = $missingRequiredContexts
    retrievedUtc            = $retrievedUtc
}
$reviewPath = Join-Path $RunDir "pr-$PrNumber-review.json"
Write-Utf8NoBom $reviewPath ($reviewRecord | ConvertTo-Json -Depth 8)

Write-Output "EXPORT: pr=$PrNumber checks=$checksPath review=$reviewPath missingRequiredContexts=$($missingRequiredContexts.Count)"
exit 0
