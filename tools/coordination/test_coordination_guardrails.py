import glob
import hashlib
import os
import re
import json
import shlex
import shutil
import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
VALIDATOR = ROOT / "tools" / "coordination" / "validate_and_append_handoff.py"
WATCHDOG = ROOT / "tools" / "coordination" / "coordination_watchdog.py"
HEARTBEAT = ROOT / "tools" / "coordination" / "coordination-heartbeat.ps1"


def run(*args, cwd):
    return subprocess.run([sys.executable, *map(str, args)], cwd=cwd, text=True, capture_output=True)


def git(cwd, *args):
    return subprocess.run(["git", *args], cwd=cwd, text=True, capture_output=True, check=True).stdout.strip()


def init_repo(tmp_path):
    r"""`git init` a throwaway repo that survives a long TMPDIR.

    MEASURED 2026-09-05. With TMPDIR under a deep path, eleven tests in this file failed with
    `git add` reporting `Filename too long`. pytest's tmp_path adds
    `pytest-of-<user>/pytest-N/<testname>0/` on top of TMPDIR -- 220 chars here -- and Windows
    MAX_PATH bites at 260. The suite passed from `C:\mlvtmp\pt` and failed from the agent
    scratchpad, on the same commit and interpreter, so the result depended on WHERE it ran.
    A test that passes or fails by its temp directory is not testing what it claims to.

    THE ORDER MATTERS AND WAS FALSIFIED, because the obvious fix does not work:
      `git init` then `git config core.longpaths true`  -> init ITSELF dies, stat'ing
          .git/hooks/fsmonitor-watchman.sample, before any config can be set.
      `git -c core.longpaths=true init` alone           -> init succeeds, but -c is transient,
          so the very next bare `git add` fails exactly as before.
    Only -c ON THE INIT, followed by persisting it into the new repo, makes every later bare
    git call in these helpers safe.

    The repo already reaches for this mitigation elsewhere -- brokered_closeout.py's
    run_git_longpaths() and Invoke-WorkstreamLoop.ps1's worktree add both pass
    `-c core.longpaths=true`. These helpers were simply never given it.

    CI never sees this: hosted runners put RUNNER_TEMP at a short path like D:\_temp, which
    is why the trap survived to bite local runs only.
    """
    subprocess.run(
        ["git", "-c", "core.longpaths=true", "init", "-q"], cwd=tmp_path, check=True
    )
    subprocess.run(
        ["git", "config", "core.longpaths", "true"], cwd=tmp_path, check=True
    )


def make_repo(tmp_path):
    init_repo(tmp_path)
    subprocess.run(["git", "config", "user.email", "test@example.com"], cwd=tmp_path, check=True)
    subprocess.run(["git", "config", "user.name", "Test"], cwd=tmp_path, check=True)
    (tmp_path / "seed.txt").write_text("seed\n")
    subprocess.run(["git", "add", "seed.txt"], cwd=tmp_path, check=True)
    subprocess.run(["git", "commit", "-qm", "seed"], cwd=tmp_path, check=True)
    start = git(tmp_path, "rev-parse", "HEAD")
    (tmp_path / "seed.txt").write_text("feature\n")
    subprocess.run(["git", "commit", "-qam", "feature"], cwd=tmp_path, check=True)
    feature = git(tmp_path, "rev-parse", "HEAD")
    return start, feature


def args(repo, start, feature, *extra):
    coord = repo / ".claude-state" / "coordination"
    coord.mkdir(parents=True)
    ledgers = [coord / "gpu.md", coord / "claude.md", coord / "codex.md"]
    for ledger in ledgers:
        ledger.write_text("# ledger\n")
    return [
        "--repo-root", repo, "--start", start, "--feature", feature,
        "--work-block", "wb-test", "--summary", "test handoff",
        "--changes", "coordination only", "--validation", "focused test pass",
        "--proof-boundary", "no product claim", "--request", "review exact range",
        "--ledger", ledgers[0], "--ledger", ledgers[1], "--codex-ledger", ledgers[2], *extra,
    ], ledgers


def test_validator_rejects_phantom_feature(tmp_path):
    start, feature = make_repo(tmp_path)
    command, _ = args(tmp_path, start, "e7311126f0f638f01eb06808f99a5e62908fc7b8")
    result = run(VALIDATOR, *command, cwd=tmp_path)
    assert result.returncode == 2
    assert "HANDOFF_BLOCKED" in result.stderr


def test_validator_mirrors_exact_payload(tmp_path):
    start, feature = make_repo(tmp_path)
    command, ledgers = args(tmp_path, start, feature)
    result = run(VALIDATOR, *command, cwd=tmp_path)
    assert result.returncode == 0, result.stderr
    range_token = f"{start}..{feature}"
    for ledger in ledgers:
        text = ledger.read_text()
        assert text.endswith("Reviewer: Claude\n")
        assert range_token in text
        assert "WorkBlock: wb-test" in text


def test_watchdog_escalates_after_two_missed_reviews(tmp_path):
    start, feature = make_repo(tmp_path)
    command, ledgers = args(tmp_path, start, feature)
    assert run(VALIDATOR, *command, cwd=tmp_path).returncode == 0
    state_file = tmp_path / ".claude-state" / "coordination" / "state.json"
    watch = ["--repo-root", tmp_path, "--state-file", state_file, "--missed-heartbeats", "2", "--ledger", ledgers[0], "--ledger", ledgers[1], "--ledger", ledgers[2]]
    result = run(WATCHDOG, *watch, cwd=tmp_path)
    assert result.returncode == 3
    assert json.loads(result.stdout)["state"] == "STALL"


def test_watchdog_repairs_missing_mirror_but_requires_ack(tmp_path):
    start, feature = make_repo(tmp_path)
    command, ledgers = args(tmp_path, start, feature)
    assert run(VALIDATOR, *command, cwd=tmp_path).returncode == 0
    codex_ledger = ledgers[2]
    codex_ledger.write_text("# ledger\n")
    state_file = tmp_path / ".claude-state" / "coordination" / "state.json"
    watch = ["--repo-root", tmp_path, "--state-file", state_file, "--repair-mirrors", "--ledger", ledgers[0], "--ledger", ledgers[1], "--ledger", ledgers[2]]
    result = run(WATCHDOG, *watch, cwd=tmp_path)
    assert result.returncode == 2
    payload = json.loads(result.stdout)
    assert payload["state"] == "ACK_REQUIRED"
    assert payload["repairedMirrors"] == [str(codex_ledger)]


def test_heartbeat_resolves_watchdog_beside_wrapper_not_under_repo_root(tmp_path):
    coord = tmp_path / ".claude-state" / "coordination"
    dual = coord / "dual-lane"
    dual.mkdir(parents=True)
    (coord / "gpu-lane-impl-review-sync.md").write_text("# gate\n")
    (dual / "claude.md").write_text("# claude\n")
    (dual / "codex.md").write_text("# codex\n")
    result = subprocess.run(
        [
            "pwsh.exe", "-NoLogo", "-NoProfile", "-NonInteractive",
            "-ExecutionPolicy", "Bypass", "-File", str(HEARTBEAT),
            "-RepoRoot", str(tmp_path), "-Once",
        ],
        text=True,
        capture_output=True,
    )
    assert result.returncode == 0, result.stderr
    assert json.loads(result.stdout)["state"] == "IDLE"
    assert not (tmp_path / "tools" / "coordination" / "coordination_watchdog.py").exists()


def test_supplied_timestamp_must_be_a_real_instant(tmp_path):
    """STAMP-APPENDER-1: --timestamp was written into the entry verbatim, so NOT-A-CLOCK
    reached the ledger heading. The closeout gate BLOCKS on a heading it cannot parse
    (content_approval_unparsable_heading), so this appender could stop finalize by itself.
    A real instant must still pass through UNCHANGED: --timestamp exists for replay and
    fixtures, and silently rewriting it would surprise those callers."""
    start, feature = make_repo(tmp_path)

    # subject: a non-instant is refused before anything is appended
    command, ledgers = args(tmp_path, start, feature)
    result = run(VALIDATOR, *command, "--timestamp", "NOT-A-CLOCK", cwd=tmp_path)
    assert result.returncode != 0, result.stdout
    assert "ISO-8601" in (result.stderr + result.stdout)
    for ledger in ledgers:
        assert "NOT-A-CLOCK" not in ledger.read_text()

    # control: a real instant is accepted AND preserved byte-for-byte
    second = tmp_path / "second"
    second.mkdir()
    start2, feature2 = make_repo(second)
    command2, ledgers2 = args(second, start2, feature2)
    exact = "2000-01-02T03:04:05+00:00"
    ok = run(VALIDATOR, *command2, "--timestamp", exact, cwd=second)
    assert ok.returncode == 0, ok.stderr
    assert any(exact in ledger.read_text() for ledger in ledgers2)


LOOP = ROOT / "tools" / "coordination" / "Invoke-WorkstreamLoop.ps1"
WORKSTREAM = ROOT / "tools" / "coordination" / "Invoke-Workstream.ps1"
# The landing probe was extracted out of Invoke-Workstream.ps1 so queue-derive.ps1 could consult
# the SAME rules instead of growing a second copy that would be widened once and left stale. The
# three probe tests below follow it: their subject never changed, only which file owns it.
PROBE = ROOT / "tools" / "coordination" / "landing-probe.ps1"
DERIVE = ROOT / "tools" / "coordination" / "queue-derive.ps1"


def test_cycle_receipt_stamp_has_exactly_one_assignment():
    """The cycle-receipt filename is built from $stamp, which must stay a formatted STRING.

    On 2026-09-04 the daily-budget fix (PR #37) reused `$stamp` as a per-row [datetime]
    inside the counting loop -- same script scope, between the assignment and the use. The
    receipt path then interpolated with the current culture as
    'cycle-09\04\2026 09:05:02.json': "/" became directory separators and ":" is illegal
    in a Windows filename, so WriteAllText threw and the scheduled task exited 1 for ~4.5 h
    while writing no audit record.

    Asserted on the source rather than by running the loop: the loop syncs a git worktree,
    reads the live dispatch log and writes into .claude-state, so executing it under test
    would mutate real board state. The repo already asserts source shape this way in
    tools/repo_hygiene/test_repo_hygiene.py.
    """
    text = LOOP.read_text(encoding="utf-8")

    # the receipt is still built from $stamp
    assert 'cycle-$stamp.json' in text

    # ...and $stamp is assigned exactly once, at script scope
    assignments = re.findall(r"^\s*\$stamp\s*=", text, re.MULTILINE)
    assert len(assignments) == 1, (
        "$stamp must have exactly one assignment; a second one clobbers the receipt "
        "filename. Found %d." % len(assignments)
    )

    # ...and that one assignment produces a filename-safe format, not a culture default
    assert re.search(r"\$stamp\s*=\s*\$cycleStart\.ToString\('yyyyMMddTHHmmssZ'\)", text), (
        "$stamp must be formatted with an explicit invariant pattern; a culture-default "
        "ToString() yields '/' and ':' which are illegal in a Windows path."
    )


def test_a_transient_fetch_failure_does_not_halt_the_cycle():
    """One blip must not cost a whole 45-minute cycle.

    Observed 2026-09-04T18:01:22Z: the loop wrote
    haltedReason="git fetch fork failed (exit 128); driver worktree may be stale" and
    dispatched nothing. The cause was lock contention with a concurrent git operation on the
    same repository -- the identical fetch succeeded by hand moments later, and the driver
    worktree was neither stale nor broken. The old code halted on the FIRST non-zero exit, so
    a blip and a genuinely broken remote were indistinguishable.

    Asserted on the source for the reason documented in
    test_cycle_receipt_stamp_has_exactly_one_assignment: running the loop syncs a real git
    worktree and writes into .claude-state, so it would mutate live board state.
    """
    text = LOOP.read_text(encoding="utf-8")
    lines = text.splitlines()

    assert "function Invoke-GitFetchFork" in text, "no bounded retry helper"

    # the helper must attempt more than once
    attempts_line = [ln for ln in lines if "$Attempts" in ln and "param(" in ln]
    assert attempts_line, "Invoke-GitFetchFork must take a bounded $Attempts parameter"
    digits = "".join(ch for ch in attempts_line[0].split("$Attempts", 1)[1] if ch.isdigit())
    assert digits and int(digits[0]) > 1, "a single attempt is not a retry"

    # THE ANTI-PATTERN: a bare fetch whose very next line halts the cycle
    stale = "driver worktree may be stale"
    for i, ln in enumerate(lines):
        is_bare_fetch = ("git -C $RepoRoot fetch fork" in ln) and ("Invoke-GitFetchFork" not in ln)
        if not is_bare_fetch:
            continue
        following = lines[i + 1] if i + 1 < len(lines) else ""
        assert stale not in following, (
            "line %d halts the cycle on a single fetch attempt: %s" % (i + 1, ln.strip())
        )

    # the halt message reports the attempt count, so receipts stay diagnosable
    assert "attempts; " + stale in text


def test_landing_probe_knows_the_verbs_and_connectors_that_actually_get_used():
    """Two MEASURED misses on 2026-09-05, both from real merged PR bodies.

    PR #53: "Closes OWN-2 and delivers GATE-RESIDUALS-1(b)" -- OWN-2 was skipped,
    GATE-RESIDUALS-1 was not, and the loop spent a lane on it at 02:11Z.
    PR #52: "Closes queue item OWN-1-PRECEDENCE" -- only "card" was permitted between the verb
    and the id, so that missed too, and the near-miss diagnostic surfaced it on its first run.

    The vocabulary is a fixed list, and every writer who does not know it costs a dispatch.
    """
    text = PROBE.read_text(encoding="utf-8")
    assert "delivers" in text, "landing verb 'delivers' not accepted"
    assert "queue" in text, "'queue item' connector not accepted"


def test_a_bare_id_in_prose_still_does_NOT_count_as_landing():
    """The load-bearing asymmetry, from the probe's own comment: a false positive SKIPS
    genuinely open work, which is strictly worse than re-dispatching finished work. Widening
    the vocabulary must never reach a bare mention."""
    text = PROBE.read_text(encoding="utf-8")
    line = [l for l in text.splitlines() if "$script:LandingVerbs" in l and "=" in l]
    assert line, "landing verb list not found"
    assert "lands|closes|fixes|resolves|delivers" in line[0], (
        "the match is no longer gated behind an explicit landing verb"
    )


def test_near_misses_are_summarised_not_printed_per_card():
    """The first draft printed one line per near-miss and produced TEN on a single run, mostly
    genuine prose mentions. A diagnostic that fires every run is one nobody reads -- which is
    exactly the failure it exists to prevent."""
    text = WORKSTREAM.read_text(encoding="utf-8")
    assert "$nearMisses" in text, "near-miss diagnostic absent"
    assert "NEAR-MISS ($($nearMisses.Count))" in text, "near-misses are not summarised into one line"
    assert "NOT skipped" in text, "the diagnostic must say it did not act on the near-miss"


# --- pre-dispatch hosted GitHub evidence ----------------------------------------------------
# MEASURED 2026-09-05: FACTORY-MATURITY-1-CLAUDE and FACTORY-MATURITY-1-OPUS, both priority 1,
# consumed two lane slots each and both returned the same wall -- `gh` answers "Access is denied"
# inside the read-only lane sandbox. The same gh, token and machine work from an interactive
# session; gh resolves its token through the Windows credential keyring and the sandbox denies
# that read. PR #55 had already TOLD lanes this might happen and that saying so is a FINDING, and
# both lanes did say so, correctly -- and were dispatched into the wall anyway, because the brief
# only taught them to report the limitation and never removed it.
#
# Asserted on the source, for the reason given in
# test_cycle_receipt_stamp_has_exactly_one_assignment: running Invoke-Workstream reads the live
# queue and writes a prompt and a run directory into real .claude-state, so executing it under
# test would mutate board state. Behavioural verification is a subject/falsifier pair run by hand
# (real gh -> "gh-evidence 5/5 export(s) ok"; a stub gh on PATH that exits 1 with "Access is
# denied" -> "gh-evidence 0/4", the section headed "THE EXPORT FAILED", and dispatch continues).


def test_hosted_evidence_is_collected_by_the_dispatcher_not_left_to_the_lane():
    """A warning in a prompt is not a fix; it is a nicer way to fail. The dispatcher runs in the
    venue where gh works and was already calling it for the landing probe, so the evidence was
    always one command away from the process doing the dispatching."""
    text = WORKSTREAM.read_text(encoding="utf-8")
    assert "$needsHostedEvidence" in text, "no hosted-evidence classification"
    assert "github-evidence" in text, "no export directory beside the run"
    # the export must actually shell out to gh from here, not merely describe it
    assert "& gh @ghArgs" in text, "the dispatcher does not itself invoke gh for the export"
    # ...and the brief must forbid the lane from retrying it. Doubled backticks: inside the
    # here-string a backtick is PowerShell's escape character, so ``gh`` is what renders as `gh`.
    assert "DO NOT RETRY ``gh`` YOURSELF" in text, "the brief does not stop the lane re-hitting the wall"


def test_the_run_directory_is_named_before_the_brief_that_cites_it():
    """The brief has to name files this script is about to write. $runDir was originally computed
    at dispatch time, AFTER the brief and only on the non-dry-run path -- so a section naming the
    export directory could not exist, and -DryRun could not be used to inspect one.

    Exactly one assignment, for the same reason $stamp has exactly one: a second one further down
    would silently point the lane at a directory that never receives the export.
    """
    text = WORKSTREAM.read_text(encoding="utf-8")
    for var in ("$runDir", "$stamp"):
        assignments = re.findall(r"(?m)^\s*" + re.escape(var) + r"\s*=", text)
        assert len(assignments) == 1, (
            "%s must have exactly one assignment; found %d" % (var, len(assignments))
        )
    assert text.index("$runDir = Join-Path") < text.index("$brief = @\""), (
        "$runDir is assigned after the brief is built, so the brief cannot name the export"
    )


def test_the_export_fails_open_and_says_why_rather_than_only_that_it_failed():
    """Fail-open, exactly like the landing probe: an unreadable signal must never silently shrink
    the board. And the REASON is kept, not just the exit code -- 'Access is denied' and a 404 send
    a reader to completely different places, and collapsing both to 'gh failed' is how a venue
    problem gets misfiled as an authorization problem for a second time."""
    text = WORKSTREAM.read_text(encoding="utf-8")
    assert "CANNOT-DETERMINE: gh not on PATH" in text, "a missing gh must not be fatal"
    assert "$why" in text and "no stderr" in text, "the failure reason is not preserved"

    # nothing between the export block and the READ-ONLY dispatch's own -DryRun check may exit:
    # a failed export must still dispatch, with the lane told plainly that those facts are
    # UNVERIFIED. Scoped to AFTER "the brief" marker (rather than the first "if ($DryRun)" in the
    # file) because TOOL-LOOP-PLUMBING-1 added a second, earlier dispatch path (-AllowEdits) with
    # its own unrelated -DryRun check.
    start = text.index("$needsHostedEvidence =")
    brief = text.index("# ------------------------------------------------------------------ the brief")
    end = text.index("if ($DryRun)", brief)
    assert not re.search(r"(?m)^\s*exit\s+\d", text[start:end]), (
        "the hosted-evidence export can halt dispatch; it must fail open"
    )
    assert "UNVERIFIED" in text[start:end], "a failed export must tell the lane the facts are UNVERIFIED"


def test_dry_run_admits_the_export_already_happened():
    """-DryRun really does write the export -- that is the point, it is how you inspect what a
    lane would receive. Printing a bare 'nothing dispatched' would be a lie by omission about a
    directory this command just created. TOOL-LOOP-PLUMBING-1 added a second dry-run notice for
    the -AllowEdits path (about its own real-then-removed worktree) -- checking ANY line here,
    not just the first, keeps this test about the read-only path's own disclosure."""
    text = WORKSTREAM.read_text(encoding="utf-8")
    dry = [ln for ln in text.splitlines() if "DRY RUN" in ln and "Write-Output" in ln]
    assert dry, "no dry-run notice"
    assert any("on disk" in ln for ln in dry), "dry run does not disclose that the export is real"



def test_the_title_match_is_verb_gated_exactly_like_the_body_match():
    """MEASURED 2026-09-05. The title match used to be unconditional, on the theory that a card id
    in a merge subject implies a landing. PR #41 falsifies it: titled "(addresses
    STAMP-APPENDER-1)", body saying "this PR says addresses, not lands: a false landing signal
    would mark the card done and skip remaining work". The probe skipped the open card anyway.

    Gating costs nothing: across all 51 merged PRs and 117 queue ids there are exactly three title
    matches, and both real landings (#23, #38) read "lands".
    """
    text = PROBE.read_text(encoding="utf-8")
    body = text[text.index("function Get-CardLandingEvidence"):]
    # Only the LANDING decision is under test. The near-miss detector below it legitimately
    # matches a bare mention against the title -- that is its whole job, and it reports rather
    # than acting. The line that assigns $hit is the one that decides "landed".
    decisions = [l for l in body.splitlines() if "$hit = " in l and "-match" in l]
    assert len(decisions) == 2, "expected exactly a title and a body landing decision, got %d" % len(decisions)
    for line in decisions:
        assert "$rxLanding" in line and "$rxMention" not in line, (
            "a landing decision is made on something other than the verb-gated regex: %s" % line.strip()
        )
    assert any("$_.title" in l for l in decisions), "the title is no longer consulted at all"


def test_the_near_miss_matcher_has_a_word_boundary_on_BOTH_sides():
    """MEASURED 2026-09-05. The landing regex always had a leading `(?<![A-Za-z0-9-])`; the
    near-miss regex did not, and nobody noticed because it only ever over-reports. PR #42's body
    names the PROMPT FILE `ws-GATE-ID-3`, and the unanchored matcher found the card id inside that
    filename -- a near-miss for a card that PR never mentioned. Every generated prompt on this
    board is named `ws-<CARD-ID>-<stamp>.md`, so this misfires structurally, not by bad luck."""
    text = PROBE.read_text(encoding="utf-8")
    fn = text[text.index("function Get-MentionRegex"):text.index("function Get-CardLandingEvidence")]
    assert "(?<![A-Za-z0-9-])" in fn, "the mention regex can match inside a longer token (e.g. ws-<ID>)"
    assert "(?![A-Za-z0-9-])" in fn, "the mention regex can match a longer id (C2-SUBMIT-2 vs -22)"


def test_the_probe_is_shared_and_not_copied_into_its_callers():
    """Two tools ask 'did this card land'. A second copy of the vocabulary would be widened once
    and left stale -- this board's most frequently paid failure. Both must DOT-SOURCE the probe,
    and neither may carry its own matcher."""
    for caller in (WORKSTREAM, DERIVE):
        text = caller.read_text(encoding="utf-8")
        assert "landing-probe.ps1" in text, "%s does not consult the shared probe" % caller.name
        assert "Get-CardLandingEvidence" in text, "%s does not call the shared probe" % caller.name
        assert "gh pr list" not in text, (
            "%s re-implements the probe instead of consulting it" % caller.name
        )


def test_the_shared_probe_sets_no_strictmode_on_its_callers():
    """It is DOT-SOURCED, so anything it sets lands in the CALLER's scope. An earlier draft set
    `-Version Latest` and immediately broke queue-derive.ps1, which reads optional queue fields
    that are legitimately absent -- a shared helper silently changing an unrelated script's
    semantics just by being consulted."""
    text = PROBE.read_text(encoding="utf-8")
    active = [l for l in text.splitlines() if "Set-StrictMode" in l and not l.strip().startswith("#")]
    assert not active, "the dot-sourced probe imposes strictness on its callers: %s" % active


def test_queue_derive_fails_open_when_the_landing_probe_cannot_run():
    """An unreadable signal must never silently shrink the board. The probe's status is printed on
    EVERY run, including when it could not run: a check whose failure looks identical to a pass is
    the defect queue-derive.ps1 exists to refuse."""
    text = DERIVE.read_text(encoding="utf-8")
    assert "queue-derive: landing-probe {0}" in text, "probe status is not reported unconditionally"
    assert "NoLandingProbe" in text, "no way to run the ancestry axis without the network"
    probe = PROBE.read_text(encoding="utf-8")
    assert probe.count("cannot-determine:") >= 3, "the probe does not fail open on every failure path"


def test_an_open_card_naming_no_sha_is_reported_not_silently_passed():
    """THE BLIND SPOT THIS EXISTS TO CLOSE. On 2026-09-05 six landed cards named no sha and the
    tool printed NO MISMATCHES. An item the sha axis cannot judge must be VISIBLE, and if a merged
    PR claims it behind a landing verb it must be a MISMATCH -- not folded into either."""
    text = DERIVE.read_text(encoding="utf-8")
    assert "NO-SHA-EVIDENCE" in text, "items the sha axis cannot judge are not tracked"
    assert "STALE-LANDED-IN-PR" in text, "the PR axis produces no finding"
    assert "NOT a mismatch, nothing to check them against" in text, (
        "an unjudgeable item must not be reported as a disagreement"
    )

# --- assert-script-currency.ps1 -------------------------------------------------------------
# The board root is the only checkout carrying .claude-state/, and it routinely sits on a peer
# branch. Running a tool from there by absolute path silently executes a stale copy: the script
# exits 0 and prints a wrong answer. These tests pin the guard that refuses that, and pin the
# fail-OPEN direction -- a guard that blocked on "I could not check" would be worse than the bug.

GUARD = ROOT / "tools" / "coordination" / "assert-script-currency.ps1"
GUARDED = [
    ROOT / "tools" / "coordination" / "queue-derive.ps1",
    ROOT / "tools" / "coordination" / "board-health-sweep.ps1",
]


def guard_status(script_path, ref="fork/master", env=None):
    """Run the guard with -PassThru and return its status, or 'THREW' when it refuses."""
    cmd = (
        "try { (& '%s' -ScriptPath '%s' -Ref '%s' -PassThru).status } "
        "catch { 'THREW' }" % (GUARD.as_posix(), Path(script_path).as_posix(), ref)
    )
    import os
    e = dict(os.environ)
    e.pop("MLV_ALLOW_STALE_TOOLS", None)
    if env:
        e.update(env)
    out = subprocess.run(
        ["pwsh", "-NoProfile", "-Command", cmd], text=True, capture_output=True, env=e
    ).stdout.strip().splitlines()
    return out[-1].strip() if out else ""


def currency_repo(tmp_path):
    """A repo with a 'fork/master' ref and a tracked script, so the guard has something to compare."""
    init_repo(tmp_path)
    subprocess.run(["git", "config", "user.email", "t@e.com"], cwd=tmp_path, check=True)
    subprocess.run(["git", "config", "user.name", "T"], cwd=tmp_path, check=True)
    script = tmp_path / "tool.ps1"
    script.write_text("Write-Output 'original'\n")
    subprocess.run(["git", "add", "tool.ps1"], cwd=tmp_path, check=True)
    subprocess.run(["git", "commit", "-qm", "seed"], cwd=tmp_path, check=True)
    # A local ref literally named refs/remotes/fork/master, so no network or remote is needed.
    subprocess.run(
        ["git", "update-ref", "refs/remotes/fork/master", git(tmp_path, "rev-parse", "HEAD")],
        cwd=tmp_path, check=True,
    )
    return script


def test_currency_guard_passes_when_the_file_matches_the_reference(tmp_path):
    script = currency_repo(tmp_path)
    assert guard_status(script) == "current"


def test_currency_guard_refuses_when_the_file_differs_from_the_reference(tmp_path):
    script = currency_repo(tmp_path)
    script.write_text("Write-Output 'a stale peer-branch copy'\n")
    assert guard_status(script) == "THREW"


def test_currency_guard_refusal_names_the_branch_the_checkout_is_on(tmp_path):
    # The message has to say WHY, or the reader treats it as noise and sets the hatch reflexively.
    script = currency_repo(tmp_path)
    subprocess.run(["git", "checkout", "-qb", "diag/some-peer-branch"], cwd=tmp_path, check=True)
    script.write_text("Write-Output 'drifted'\n")
    cmd = "try { & '%s' -ScriptPath '%s' } catch { $_.Exception.Message }" % (
        GUARD.as_posix(), script.as_posix(),
    )
    import os
    e = dict(os.environ)
    e.pop("MLV_ALLOW_STALE_TOOLS", None)  # an inherited hatch would make this pass vacuously
    out = subprocess.run(
        ["pwsh", "-NoProfile", "-Command", cmd], text=True, capture_output=True, env=e
    )
    blob = out.stdout + out.stderr
    assert "diag/some-peer-branch" in blob
    assert "MLV_ALLOW_STALE_TOOLS" in blob


def test_currency_guard_honours_the_escape_hatch(tmp_path):
    script = currency_repo(tmp_path)
    script.write_text("Write-Output 'deliberately modified'\n")
    assert guard_status(script, env={"MLV_ALLOW_STALE_TOOLS": "1"}) == "skipped"


def test_currency_guard_allows_a_file_not_yet_present_on_the_reference(tmp_path):
    # A brand-new script is not stale. Blocking here would make it impossible to add one.
    script = currency_repo(tmp_path)
    fresh = script.parent / "brand-new.ps1"
    fresh.write_text("Write-Output 'new'\n")
    assert guard_status(fresh) == "untracked-on-ref"


def test_currency_guard_fails_open_when_the_reference_does_not_resolve(tmp_path):
    # A lane sandbox or a fresh clone has no 'fork' remote. That is missing infrastructure,
    # not proven drift, so the guard must stay silent rather than halt the caller.
    script = currency_repo(tmp_path)
    script.write_text("Write-Output 'differs'\n")
    assert guard_status(script, ref="nonexistent/ref") == "unknown"


def test_currency_guard_fails_open_outside_a_git_working_tree(tmp_path):
    loose = tmp_path / "loose.ps1"
    loose.write_text("Write-Output 'x'\n")
    assert guard_status(loose) == "unknown"


def test_the_read_only_board_diagnostics_actually_invoke_the_guard(tmp_path):
    # Without this, the guard could quietly stop being wired and nothing would notice.
    for script in GUARDED:
        body = script.read_text(encoding="utf-8")
        assert "assert-script-currency.ps1" in body, f"{script.name} no longer invokes the guard"
        assert "-ScriptPath $PSCommandPath" in body, f"{script.name} guards the wrong path"

# --- Invoke-Workstream: a malformed priority must not stop dispatch -------------------------
# Get-Rank used a bare [int] cast. On a non-numeric string that THROWS, and the throw lands
# inside Sort-Object -Property { Get-Rank $_ }, killing selection -- while the script still
# exits 0, so the loop records a normal cycle that dispatched nothing. Silent starvation is
# worse than a halt because nothing reports it. queue.json demonstrably carries prose in this
# field (DISPATCH-CDX-14, DISPATCH-CDX-15), so the authoring habit is real, not hypothetical.



def workstream_dry_run(tmp_path, items, track="factory"):
    queue = tmp_path / "queue.json"
    queue.write_text(json.dumps({"schema": "test", "items": items}), encoding="ascii")
    return subprocess.run(
        [
            "pwsh", "-NoProfile", "-File", str(WORKSTREAM),
            "-Track", track, "-DryRun", "-NoLandingProbe", "-QueuePath", str(queue),
        ],
        text=True, capture_output=True,
    )


# 'queued', not 'booked': PR #63 made the dispatcher's admission an ALLOWLIST derived from
# queue.json's own note, where booked means "named trigger, not schedulable". These fixtures
# exist to reach Get-Rank, so they must carry a state a lane actually owns. The rule is right;
# the fixture was relying on booked being dispatchable, which it never should have been.
NUMERIC_CARD = {"id": "NUMERIC-1", "state": "queued", "track": "factory", "priority": 1, "title": "normal"}
PROSE_CARD = {
    "id": "PROSE-1", "state": "queued", "track": "factory",
    "priority": "CRITICAL PATH - the cap is now two blocks away, not one",
    "title": "prose in the priority field",
}


def test_a_prose_priority_does_not_stop_the_dispatcher_selecting(tmp_path):
    out = workstream_dry_run(tmp_path, [NUMERIC_CARD, PROSE_CARD])
    assert "card=NUMERIC-1" in out.stdout, out.stdout + out.stderr
    assert "Sort-Object" not in out.stderr, "selection still dies on the cast"


def test_a_prose_priority_is_reported_loudly_rather_than_swallowed(tmp_path):
    # Ranking it 999 silently would hide a card that someone deliberately marked urgent.
    out = workstream_dry_run(tmp_path, [NUMERIC_CARD, PROSE_CARD])
    assert "NON-NUMERIC PRIORITY" in out.stderr
    assert "PROSE-1" in out.stderr


def test_the_priority_warning_stays_off_machine_readable_stdout(tmp_path):
    # The [WORKSTREAM] stdout lines are parsed, and emitting inside a Sort-Object property
    # block would make the sort key an array rather than an int.
    out = workstream_dry_run(tmp_path, [NUMERIC_CARD, PROSE_CARD])
    assert "NON-NUMERIC PRIORITY" not in out.stdout


def test_the_priority_warning_is_emitted_once_per_card(tmp_path):
    # Sort-Object may evaluate the property block more than once per item.
    out = workstream_dry_run(tmp_path, [NUMERIC_CARD, PROSE_CARD])
    assert out.stderr.count("NON-NUMERIC PRIORITY on card 'PROSE-1'") == 1


def test_a_card_with_a_prose_priority_is_still_dispatchable(tmp_path):
    # Ranked last, not excluded -- a malformed field must not make work unreachable.
    out = workstream_dry_run(tmp_path, [PROSE_CARD])
    assert "card=PROSE-1" in out.stdout, out.stdout + out.stderr


def test_a_wholly_numeric_queue_produces_no_priority_warning(tmp_path):
    out = workstream_dry_run(tmp_path, [NUMERIC_CARD])
    assert "NON-NUMERIC PRIORITY" not in out.stderr
    assert "card=NUMERIC-1" in out.stdout


# --- Invoke-Lane: the exit code must be the truth ------------------------------------------
# The script's own header says "it is alive because it is running and dead when it exits, and the
# exit code is the truth" -- and it did not honour that at its own boundary. It computed the
# lane's exit code, wrote it into the receipt, printed it, then fell off the end, which exits 0.
# Invoke-Workstream captured that 0 and logged laneExitCode=0, so a failed lane was recorded as a
# success. Measured 2026-09-05: two of twelve dispatches hit HTTP 429 ("You've hit your session
# limit"), produced nothing, carried exitCode 1 in their receipts and laneExitCode 0 in the log.

LANE_RUNNER = ROOT / "tools" / "coordination" / "Invoke-Lane.ps1"


def test_invoke_lane_propagates_its_exit_code_instead_of_falling_off_the_end():
    body = LANE_RUNNER.read_text(encoding="utf-8")
    assert "exit $propagated" in body, (
        "Invoke-Lane must exit with the lane's outcome; falling off the end exits 0 and makes "
        "every failed lane look successful to Invoke-Workstream's laneExitCode"
    )


def test_invoke_lane_maps_the_negative_sentinels_rather_than_passing_them_through():
    # -1 would surface as 255 through a process exit code, and -999 does not survive at all.
    body = LANE_RUNNER.read_text(encoding="utf-8")
    assert "-1      { 124 }" in body, "timeout sentinel is not mapped"
    assert "-999    { 127 }" in body, "never-completed sentinel is not mapped"


def test_the_timeout_code_matches_the_taxonomy_the_repo_already_uses():
    # boundedRunnerExitCodes already fixes timeout=124; a second private meaning for the same
    # condition is how two tools end up disagreeing about one event.
    config = json.loads((ROOT / "closeout.config.json").read_text(encoding="utf-8"))
    # The taxonomy lives under `locking`, not at the top level. Found by searching the config
    # rather than assuming: the first version of this test guessed the top level, and a test that
    # skips is a test that never fired.
    codes = config["locking"]["boundedRunnerExitCodes"]
    assert int(codes["timeout"]) == 124
    assert "-1      { 124 }" in LANE_RUNNER.read_text(encoding="utf-8")

DISPATCHER = ROOT / "tools" / "coordination" / "Invoke-Workstream.ps1"


def run_dispatcher(tmp_path, items, *extra):
    """Run the real dispatcher against a synthetic queue fixture, -DryRun and
    -NoLandingProbe so it never calls gh or Invoke-Lane.ps1 for real. Uses -QueuePath,
    which exists precisely so a fixture queue can be substituted (the script refuses to
    ever mutate the canonical one). A successful (non-empty-candidates) run still writes
    a real prompt file under the live repo's .claude-state\\fleet-runs\\prompts\\ -- that
    is the dispatcher's normal side effect even under -DryRun -- so the caller must clean
    up any path reported on a "WORKSTREAM: prompt=" line.
    """
    queue_path = tmp_path / "queue.json"
    queue_path.write_text(json.dumps({"schema": "dual-lane-queue.v1", "note": "test fixture", "items": items}))
    result = subprocess.run(
        [
            "pwsh.exe", "-NoLogo", "-NoProfile", "-NonInteractive", "-ExecutionPolicy", "Bypass",
            "-File", str(DISPATCHER), "-QueuePath", str(queue_path), "-NoLandingProbe", "-DryRun", *extra,
        ],
        text=True, capture_output=True,
    )
    prompt_path = None
    for line in result.stdout.splitlines():
        if line.startswith("WORKSTREAM: prompt="):
            prompt_path = Path(line.split("=", 1)[1].strip())
    if prompt_path and prompt_path.exists():
        prompt_path.unlink()
    return result


def test_booked_card_is_reported_not_dispatched():
    """B2-TOOLING-BASELINE (state=booked) burned a dispatch slot on 2026-09-05: the old
    $Terminal blocklist did not name 'booked', so it was treated as ordinary live work.
    queue.json's own note defines 'booked' as "named trigger, not schedulable". A card in
    that state must be reported via a NOT-SCHEDULABLE line and must never be selected,
    even when it is the only candidate on its track."""
    import tempfile
    with tempfile.TemporaryDirectory() as td:
        tmp_path = Path(td)
        result = run_dispatcher(
            tmp_path,
            [{"id": "TEST-BOOKED-1", "state": "booked", "track": "factory", "priority": 1}],
            "-Track", "factory",
        )
        assert "NOT-SCHEDULABLE card=TEST-BOOKED-1 state=booked" in result.stdout
        assert "WORKSTREAM: track=factory card=TEST-BOOKED-1" not in result.stdout
        assert result.returncode != 0


def test_queued_card_on_a_track_is_still_dispatched():
    """Control for the rule above: a card in a genuinely schedulable state (queued) with
    a real track must still be picked and dry-run dispatched, so the allowlist does not
    over-exclude ordinary live work."""
    import tempfile
    with tempfile.TemporaryDirectory() as td:
        tmp_path = Path(td)
        result = run_dispatcher(
            tmp_path,
            [{"id": "TEST-QUEUED-1", "state": "queued", "track": "factory", "priority": 1}],
            "-Track", "factory",
        )
        assert "WORKSTREAM: NOT-SCHEDULABLE" not in result.stdout
        assert "WORKSTREAM: track=factory card=TEST-QUEUED-1" in result.stdout
        assert result.returncode == 0


def test_untracked_card_is_not_auto_selected():
    """Root cause of the same defect: Get-Track returns 'UNSET' for a card with no track,
    and the default -Track auto pool never filtered by track at all, so an untracked card
    was always a candidate once the tracked pools ran dry. A schedulable-state card with
    no track (queue field) must be reported and skipped under the default auto selection."""
    import tempfile
    with tempfile.TemporaryDirectory() as td:
        tmp_path = Path(td)
        result = run_dispatcher(
            tmp_path,
            [{"id": "TEST-UNTRACKED-1", "state": "queued", "priority": 1}],
        )
        assert "NOT-SCHEDULABLE card=TEST-UNTRACKED-1" in result.stdout
        assert "no-board-track-set" in result.stdout
        assert "WORKSTREAM: track=" not in result.stdout
        assert result.returncode != 0


def test_dispatched_untracked_target_state_is_still_schedulable():
    """'dispatched-untracked-target' is a REAL state already live in the board's queue
    (SIDECAR-COVERAGE-1, track=factory, priority=7, owner=codex) - its own stateReason
    explains that "untracked" means the deliverable is gitignored and has no gate, which
    is unrelated to whether this script's `track` field is set. Excluding this state from
    the allowlist would silently strand a card a lane genuinely still owns, which is the
    exact class of defect this fix exists to remove, just aimed at a different card."""
    import tempfile
    with tempfile.TemporaryDirectory() as td:
        tmp_path = Path(td)
        result = run_dispatcher(
            tmp_path,
            [{"id": "TEST-DUT-1", "state": "dispatched-untracked-target", "track": "factory", "priority": 7}],
            "-Track", "factory",
        )
        assert "WORKSTREAM: NOT-SCHEDULABLE" not in result.stdout
        assert "WORKSTREAM: track=factory card=TEST-DUT-1" in result.stdout
        assert result.returncode == 0


def test_explicit_track_unset_still_dispatches_an_untracked_card():
    """Control for the rule above: Invoke-WorkstreamLoop.ps1 rotates through '-Track UNSET'
    on purpose as its own named track, so an explicit request for the UNSET pool must keep
    working even though the default AUTO pool now excludes it."""
    import tempfile
    with tempfile.TemporaryDirectory() as td:
        tmp_path = Path(td)
        result = run_dispatcher(
            tmp_path,
            [{"id": "TEST-UNTRACKED-2", "state": "queued", "priority": 1}],
            "-Track", "UNSET",
        )
        assert "WORKSTREAM: NOT-SCHEDULABLE" not in result.stdout
        assert "WORKSTREAM: track=UNSET card=TEST-UNTRACKED-2" in result.stdout
        assert result.returncode == 0


# --- queue-derive.ps1: the ref it MEASURES AGAINST must not itself be stale -----------------
# Every finding is "is <sha> an ancestor of -MasterRef". A -MasterRef behind its own upstream does
# not make the tool fail, it makes it QUIETLY WRONG in both directions. Measured 2026-09-05: the
# board's local `master` was 5 behind `fork/master`, so c043f6fc -- the commit that landed
# B2-TOOLING-BASELINE -- was not an ancestor of `master` while being one of `fork/master`. It was
# fast-forwarded by hand at 07:05Z and was behind AGAIN by 13:41Z. A hand fix is not a fix.

def stale_ref_repo(tmp_path):
    """A clone whose local branch is strictly BEHIND its upstream -- the exact live defect."""
    origin = tmp_path / "origin"
    origin.mkdir()
    subprocess.run(["git", "init", "-q", "-b", "main"], cwd=origin, check=True)
    subprocess.run(["git", "config", "user.email", "t@e.com"], cwd=origin, check=True)
    subprocess.run(["git", "config", "user.name", "T"], cwd=origin, check=True)
    (origin / "f.txt").write_text("one")
    subprocess.run(["git", "add", "-A"], cwd=origin, check=True)
    subprocess.run(["git", "commit", "-qm", "one"], cwd=origin, check=True)

    clone = tmp_path / "clone"
    subprocess.run(["git", "clone", "-q", str(origin), str(clone)], check=True)
    subprocess.run(["git", "config", "user.email", "t@e.com"], cwd=clone, check=True)
    subprocess.run(["git", "config", "user.name", "T"], cwd=clone, check=True)

    # origin moves on; the clone fetches, so origin/main ends up ahead of the clone's own main.
    for n in ("two", "three"):
        (origin / "f.txt").write_text(n)
        subprocess.run(["git", "commit", "-qam", n], cwd=origin, check=True)
    subprocess.run(["git", "fetch", "-q"], cwd=clone, check=True)

    queue = tmp_path / "queue.json"
    queue.write_text(json.dumps({"schema": "dual-lane-queue.v1", "items": []}))
    return clone, queue


def run_derive(clone, queue, ref, extra_env=None):
    e = dict(os.environ)
    # We run the branch's own copy of the script, which by construction differs from fork/master
    # while the PR is open; that is the currency guard's business, not this test's.
    e["MLV_ALLOW_STALE_TOOLS"] = "1"
    e.pop("MLV_ALLOW_STALE_MASTER_REF", None)
    if extra_env:
        e.update(extra_env)
    return subprocess.run(
        ["pwsh", "-NoProfile", "-NonInteractive", "-ExecutionPolicy", "Bypass",
         "-File", str(DERIVE), "-RepoRoot", str(clone), "-QueueFile", str(queue),
         "-MasterRef", ref, "-NoLandingProbe"],
        text=True, capture_output=True, env=e,
    )


def test_queue_derive_REFUSES_a_master_ref_that_is_behind_its_upstream(tmp_path):
    clone, queue = stale_ref_repo(tmp_path)
    r = run_derive(clone, queue, "main")
    assert r.returncode == 14, "expected refusal exit 14, got %d: %s%s" % (
        r.returncode, r.stdout, r.stderr)
    assert "STALE MEASURING REF REFUSED" in r.stderr
    assert "2 commit(s) behind" in r.stderr, r.stderr
    # The refusal must be ACTIONABLE, not merely correct: it names the fix and the escape hatch.
    assert "git -C" in r.stderr and "fetch" in r.stderr
    assert "MLV_ALLOW_STALE_MASTER_REF=1" in r.stderr


def test_the_stale_ref_refusal_has_a_working_escape_hatch(tmp_path):
    clone, queue = stale_ref_repo(tmp_path)
    r = run_derive(clone, queue, "main", {"MLV_ALLOW_STALE_MASTER_REF": "1"})
    assert r.returncode == 0, "escape hatch did not let the audit run: %s%s" % (r.stdout, r.stderr)
    assert "NO MISMATCHES" in r.stdout


def test_measuring_against_the_upstream_directly_is_never_refused(tmp_path):
    """The refusal must not fire on the one ref that is current by definition."""
    clone, queue = stale_ref_repo(tmp_path)
    r = run_derive(clone, queue, "origin/main")
    assert r.returncode == 0, "%s%s" % (r.stdout, r.stderr)


def test_a_branch_that_is_AHEAD_of_its_upstream_is_not_refused(tmp_path):
    """Unpushed local work is normal development, NOT the staleness defect. Refusing on it would
    make the tool unusable exactly when someone is building a change to the board."""
    clone, queue = stale_ref_repo(tmp_path)
    subprocess.run(["git", "merge", "-q", "origin/main"], cwd=clone, check=True)
    (clone / "f.txt").write_text("local")
    subprocess.run(["git", "commit", "-qam", "unpushed"], cwd=clone, check=True)
    r = run_derive(clone, queue, "main")
    assert r.returncode == 0, "a branch AHEAD of its upstream was refused: %s%s" % (
        r.stdout, r.stderr)


def test_the_stale_ref_guard_fails_OPEN_when_there_is_no_upstream(tmp_path):
    """Polarity copied from assert-script-currency.ps1: stop only on PROVEN drift. A repo with no
    remote cannot prove anything, and a guard that blocked on 'I could not check' would be worse
    than the bug it prevents."""
    repo = tmp_path / "solo"
    repo.mkdir()
    subprocess.run(["git", "init", "-q", "-b", "main"], cwd=repo, check=True)
    subprocess.run(["git", "config", "user.email", "t@e.com"], cwd=repo, check=True)
    subprocess.run(["git", "config", "user.name", "T"], cwd=repo, check=True)
    (repo / "f.txt").write_text("x")
    subprocess.run(["git", "add", "-A"], cwd=repo, check=True)
    subprocess.run(["git", "commit", "-qm", "x"], cwd=repo, check=True)
    queue = tmp_path / "q.json"
    queue.write_text(json.dumps({"schema": "dual-lane-queue.v1", "items": []}))
    r = run_derive(repo, queue, "main")
    assert r.returncode == 0, "guard blocked a repo with no upstream to compare: %s" % r.stderr
    assert "STALE MEASURING REF" not in r.stderr


def test_the_refusal_suggests_a_fix_that_actually_WORKS_when_the_ref_is_checked_out(tmp_path):
    """MEASURED while using this guard on 2026-09-05, minutes after writing it.

    It printed `git fetch fork master:master`. Git REFUSES that with "refusing to fetch into
    branch 'refs/heads/master' checked out at ..." -- and the board root had moved onto master
    hours earlier, so the single command the tool suggested was the one command that could not
    work. A refusal that names an impossible fix is a refusal nobody can act on, which is most of
    the way back to being silent.
    """
    clone, queue = stale_ref_repo(tmp_path)   # 'main' IS checked out in this clone
    r = run_derive(clone, queue, "main")
    assert r.returncode == 14
    assert "merge --ff-only origin/main" in r.stderr, r.stderr
    assert "is CHECKED OUT at that path" in r.stderr, r.stderr
    # The impossible form must not be OFFERED AS A COMMAND. It may still be NAMED in the prose
    # that explains why it is not offered -- an earlier draft of this test failed on exactly that
    # sentence, which is the test being wrong rather than the message.
    commands = [l for l in r.stderr.splitlines() if l.strip().startswith("git -C")]
    assert commands, "the refusal offered no command at all"
    assert not [l for l in commands if "main:main" in l], (
        "still OFFERING the refspec fetch that git refuses for a checked-out branch: %s" % commands
    )


def test_the_refusal_suggests_the_refspec_fetch_when_the_ref_is_NOT_checked_out(tmp_path):
    """The other half of the same decision. With the ref not checked out anywhere, the one-line
    refspec fetch is correct and is the better advice -- it does not touch any working tree."""
    clone, queue = stale_ref_repo(tmp_path)
    head = subprocess.run(["git", "rev-parse", "HEAD"], cwd=clone, text=True,
                          capture_output=True, check=True).stdout.strip()
    subprocess.run(["git", "checkout", "-q", "--detach", head], cwd=clone, check=True)
    r = run_derive(clone, queue, "main")
    assert r.returncode == 14
    assert "fetch origin main:main" in r.stderr, r.stderr
    assert "is CHECKED OUT at that path" not in r.stderr, r.stderr


# --- Invoke-Lane: a PROVIDER REFUSAL is a third outcome, never a completed run ----------
# Incident 2026-09-07T01:28Z (fleet-runs\20260907T012821Z): sol was dispatched for round 3 of
# the PR #79 review and codex refused it in 7.7 s with "You've hit your usage limit ... try
# again at Sep 10th". The receipt said exitCode=1, failure=null, complete=true, state=complete.
# Nothing downstream could tell "refused before any work" from "reviewed and objected", and the
# hub idled for an hour on what was actually an owner-side account rotation.
#
# Round 1 BLOCKER: codex echoes the prompt into stderr, and a text scan over the WHOLE
# transcript misclassified a review that merely quoted the incident. Round 2: BLOCKER x2 --
# (1) a completed review that READS this repo prints "api_error_status 429" into stderr via
# tool output and was classified refused even though it answered; (2) removing the echoed
# prompt line-by-line hid a genuine refusal identical to an echoed line. The fix (see
# lane-provider-refusal.ps1) is structural, never a phrase scan: codex is refused iff stdout
# is EMPTY and, after removing the echoed prompt as ONE BLOCK, a framed `ERROR:` line remains;
# claude is decided by its own JSON envelope fields (is_error / api_error_status), never text.

REFUSAL_HELPER = ROOT / "tools" / "coordination" / "lane-provider-refusal.ps1"

REAL_CODEX_USAGE_LIMIT_STDERR = (
    "2026-09-07T01:28:22.710128Z ERROR codex_models_manager::manager: failed to load models cache\n"
    "OpenAI Codex v0.147.0\n--------\nworkdir: C:\\!Layi Wkspc\\MLV-App\nmodel: gpt-5.6-sol\n"
    "--------\nuser\n# CROSS-FAMILY REVIEW: PR #79 at head 22483b33\n...\n"
    "ERROR: You've hit your usage limit. Visit https://chatgpt.com/codex/settings/usage to "
    "purchase more credits or try again at Sep 10th, 2026 8:09 PM.\n"
    "ERROR: You've hit your usage limit. Visit https://chatgpt.com/codex/settings/usage to "
    "purchase more credits or try again at Sep 10th, 2026 8:09 PM.\n"
)

REAL_CODEX_KNOWN_GOOD_STDERR = (
    "OpenAI Codex v0.147.0\n--------\nworkdir: C:\\!Layi Wkspc\\MLV-App\nmodel: gpt-5.6-sol\n"
    "provider: openai\napproval: never\nsandbox: read-only\nreasoning effort: high\n--------\n"
    "user\nReply with exactly: PROBE-OK\ncodex\nPROBE-OK\ntokens used\n22,014\nPROBE-OK\n"
)

# Measured 2026-09-05 on the claude engine, two dispatches of twelve (see Invoke-Lane's exit
# code comment): the JSON envelope carried this and exitCode 1.
REAL_CLAUDE_429_ENVELOPE = (
    '{"type":"result","subtype":"error","is_error":true,"api_error_status":429,'
    '"result":"You\'ve hit your session limit. Try again in 3 hours.","num_turns":0}\n'
)

CLAUDE_SUCCESS_QUOTING_ENVELOPE = (
    '{"type":"result","subtype":"success","is_error":false,'
    '"result":"ERROR: You\'ve hit your usage limit and api_error_status 429 in the quoted text",'
    '"num_turns":3}\n'
)


def _classify(tmp_path, text, engine, prompt="", answer=""):
    src = tmp_path / "lane-stderr.txt"
    src.write_text(text, encoding="utf-8")
    psrc = tmp_path / "lane-prompt.txt"
    psrc.write_text(prompt, encoding="utf-8")
    asrc = tmp_path / "lane-answer.txt"
    asrc.write_text(answer, encoding="utf-8")
    cmd = (
        f". '{REFUSAL_HELPER}'; "
        f"$t = [IO.File]::ReadAllText('{src}'); "
        f"$p = [IO.File]::ReadAllText('{psrc}'); "
        f"$a = [IO.File]::ReadAllText('{asrc}'); "
        f"$r = Get-ProviderRefusal -Text $t -Engine '{engine}' -Prompt $p -Answer $a; "
        "if ($null -eq $r) { 'NULL' } else { $r | ConvertTo-Json -Compress }"
    )
    out = subprocess.run(
        ["pwsh", "-NoLogo", "-NoProfile", "-NonInteractive", "-Command", cmd],
        text=True, capture_output=True,
    )
    assert out.returncode == 0, out.stderr
    line = out.stdout.strip().splitlines()[-1]
    return None if line == "NULL" else json.loads(line)


def test_provider_refusal_classifies_the_real_codex_usage_limit_stderr(tmp_path):
    r = _classify(tmp_path, REAL_CODEX_USAGE_LIMIT_STDERR, "codex", answer="")
    assert r is not None, "the 2026-09-07 refusal must not read as a completed run"
    assert r["kind"] == "provider-usage-limit"
    assert r["engine"] == "codex"
    assert r["retryAfter"] == "Sep 10th, 2026 8:09 PM"
    assert "usage limit" in r["match"]
    assert "rotates" in r["remedy"], "a usage limit is an owner-side rotation, and the receipt says so"


def test_provider_refusal_is_null_on_a_real_known_good_transcript(tmp_path):
    # The falsifier beside its subject: the probe that proved the rotated account worked.
    # It answered ("PROBE-OK" on stdout), so nothing in stderr is even inspected.
    assert _classify(tmp_path, REAL_CODEX_KNOWN_GOOD_STDERR, "codex", answer="PROBE-OK") is None


def test_provider_refusal_classifies_the_claude_429_session_limit(tmp_path):
    r = _classify(tmp_path, "", "claude", answer=REAL_CLAUDE_429_ENVELOPE)
    assert r is not None
    assert r["kind"] == "provider-usage-limit"
    assert r["retryAfter"] == "3 hours"


def test_provider_refusal_claude_success_envelope_quoting_the_phrase_is_null(tmp_path):
    # sol PR #80 R2 BLOCKER (1): a completed run that merely QUOTES the vocabulary -- in its own
    # result text, and with stderr full of the same phrase -- must not classify as refused. Only
    # the envelope's own is_error / api_error_status fields decide for claude; text is never
    # evidence.
    stderr_full_of_phrase = (
        "api_error_status 429\nYou've hit your usage limit\nERROR: You've hit your usage limit\n"
    ) * 3
    r = _classify(tmp_path, stderr_full_of_phrase, "claude", answer=CLAUDE_SUCCESS_QUOTING_ENVELOPE)
    assert r is None


def test_provider_refusal_claude_survives_diagnostic_noise_around_the_envelope(tmp_path):
    # sol PR #80 R3 MAJOR: whole-string ConvertFrom-Json fails OPEN -- the instant stdout carries
    # ANY non-whitespace diagnostic line before or after the envelope, the old code's single
    # try/catch swallowed a REAL refusal as null. Invoke-Lane harvests raw, unfiltered stdout, so
    # this is reachable whenever anything else writes to stdout alongside --output-format json.
    # sol's own repro, verbatim in shape: a "diagnostic" line before the envelope. The envelope
    # carries "type":"result" -- the real --output-format json shape (sol PR #80 R4 MAJOR below).
    envelope = (
        '{"type":"result","subtype":"error","is_error":true,"api_error_status":429,'
        '"result":"You\'ve hit your session limit"}'
    )
    r = _classify(tmp_path, "", "claude", answer="diagnostic\n" + envelope)
    assert r is not None, "a diagnostic line before the envelope must not hide a real refusal"
    assert r["kind"] == "provider-usage-limit"
    # Noise AFTER the envelope must not hide it either.
    r2 = _classify(tmp_path, "", "claude", answer=envelope + "\ntrailing diagnostic noise")
    assert r2 is not None
    assert r2["kind"] == "provider-usage-limit"


def test_provider_refusal_claude_requires_the_result_envelope_shape_not_any_json_line(tmp_path):
    # sol PR #80 R4 MAJOR (sol's own repro, verbatim in shape): the R3 fix trusted the FIRST
    # parseable JSON-object line, so a JSON-shaped diagnostic carrying its own is_error /
    # api_error_status fields -- printed BEFORE the real envelope -- was picked instead, producing
    # a FALSE refusal even though the run completed successfully. Only a line shaped like the real
    # --output-format json envelope (carries "type":"result") may be treated as the envelope.
    fake_diagnostic = (
        '{"is_error":true,"api_error_status":429,"result":"You have hit your usage limit"}'
    )
    real_envelope = (
        '{"type":"result","subtype":"success","is_error":false,'
        '"result":"ordinary completed answer","num_turns":2}'
    )
    r = _classify(tmp_path, "", "claude", answer=fake_diagnostic + "\n" + real_envelope)
    assert r is None, "a JSON-shaped diagnostic without type:result must never be mistaken for the envelope"
    # Mirror: a benign JSON diagnostic must not hide a later GENUINE refusal envelope either.
    benign_diagnostic = '{"note":"some other tool wrote this JSON line"}'
    real_refusal = (
        '{"type":"result","subtype":"error","is_error":true,"api_error_status":429,'
        '"result":"You\'ve hit your session limit"}'
    )
    r2 = _classify(tmp_path, "", "claude", answer=benign_diagnostic + "\n" + real_refusal)
    assert r2 is not None
    assert r2["kind"] == "provider-usage-limit"


# sol PR #80 round 1 BLOCKER (2026-09-07): codex echoes the prompt into stderr, and the first
# version scanned every line, so a review whose PROMPT quoted the incident classified itself as
# refused. sol PR #80 round 2 BLOCKER (finding 1): a completed review that READS this repo prints
# "api_error_status 429" into stderr via tool-output/citation lines and was classified refused
# even though it answered. Both repros are sol's own, verbatim in shape.
def test_provider_refusal_ignores_prose_that_merely_quotes_a_refusal(tmp_path):
    prompt = (
        "# CROSS-FAMILY REVIEW: PR #80\n"
        "The incident: ERROR: You've hit your usage limit. Visit ... try again at Sep 10th, 2026 8:09 PM.\n"
        "Verify it.\n"
    )
    answer = "Successful review quotes: You've hit your usage limit. Verdict APPROVE.\n"
    stderr = (
        prompt
        + "#   CITE-TXN-1 the receipt recorded api_error_status 429 \"You've hit your session limit\"\n"
        + "ERROR: this line is a reviewer quoting: You've hit your usage limit\n"
    )
    # It answered: the classifier never even reaches the text below, which is exactly why a
    # reviewer's citations and quotations cannot self-classify as a refusal.
    assert _classify(tmp_path, stderr, "codex", prompt=prompt, answer=answer) is None


def test_provider_refusal_ignores_the_echoed_prompt_but_still_sees_a_real_error_line(tmp_path):
    prompt = (
        "# CROSS-FAMILY REVIEW: PR #80\n"
        "The incident: ERROR: You've hit your usage limit. Visit ... try again at Sep 10th, 2026 8:09 PM.\n"
        "Verify it.\n"
    )
    # Echo only: the prompt's ERROR line appears in stderr because codex printed the prompt back.
    # No answer: a genuine refusal never produces stdout.
    echoed = "OpenAI Codex v0.147.0\n--------\nuser\n" + prompt + "codex\n"
    assert _classify(tmp_path, echoed, "codex", prompt=prompt, answer="") is None
    # Echo PLUS a genuine refusal line that is not in the prompt: still a refusal.
    refused = echoed + "ERROR: You've hit your usage limit. Visit x to purchase more credits or try again at Sep 11th, 2026 1:00 AM.\n"
    r = _classify(tmp_path, refused, "codex", prompt=prompt, answer="")
    assert r is not None and r["retryAfter"] == "Sep 11th, 2026 1:00 AM"


def test_provider_refusal_removes_the_echoed_prompt_as_one_block_not_line_by_line(tmp_path):
    # sol PR #80 R2 BLOCKER (finding 2): a genuine refusal line IDENTICAL to an echoed prompt
    # line must still be seen. Removing the prompt line-by-line would delete every occurrence,
    # including the provider's own, real, second one. Removing it as one verbatim block only
    # deletes the FIRST occurrence, leaving a later identical line intact.
    x = "ERROR: You've hit your usage limit. Visit x or try again at Sep 10th, 2026 8:09 PM.\n"
    prompt = x
    stderr_with_second_error = "user\n" + x + "\ncodex\n" + x + "\n"
    r = _classify(tmp_path, stderr_with_second_error, "codex", prompt=prompt, answer="")
    assert r is not None
    assert r["retryAfter"] == "Sep 10th, 2026 8:09 PM"
    # Only the echoed occurrence, once: nothing left after the block is removed.
    stderr_echo_only = "user\n" + x + "\n"
    assert _classify(tmp_path, stderr_echo_only, "codex", prompt=prompt, answer="") is None


def test_provider_refusal_requires_the_provider_frame_not_just_the_phrase(tmp_path):
    # Same words, no ERROR: frame -> not a refusal. With the frame -> refusal. Empty answer
    # required in both cases: a real answer means it ran.
    assert _classify(tmp_path, "you've hit your usage limit\n", "codex", answer="") is None
    assert _classify(tmp_path, "ERROR: you've hit your usage limit\n", "codex", answer="") is not None


def test_provider_refusal_requires_an_empty_answer_even_with_a_framed_line(tmp_path):
    # A non-empty answer means the lane ran and produced something: never a refusal, no matter
    # what the (irrelevant, historical) stderr says.
    assert _classify(tmp_path, "ERROR: you've hit your usage limit\n", "codex", answer="APPROVE") is None


def test_provider_refusal_treats_empty_output_as_no_refusal(tmp_path):
    # Silence is not a refusal; it is the -999/incomplete path, which the receipt already names.
    assert _classify(tmp_path, "", "codex", answer="") is None
    assert _classify(tmp_path, "", "claude", answer="") is None


def test_invoke_lane_records_a_provider_refusal_as_refused_not_complete():
    body = LANE_RUNNER.read_text(encoding="utf-8")
    assert "lane-provider-refusal.ps1" in body, "Invoke-Lane must dot-source the one classifier"
    assert "Get-ProviderRefusal -Text $stderrText -Answer $stdout" in body, (
        "classification must read the harvested stderr and the harvested answer separately, "
        "never a blended blob (sol PR #80 R1+R2 BLOCKERs)"
    )
    assert "-Prompt $Prompt" in body, "the echoed prompt must be excluded from classification (sol PR #80 R1 BLOCKER)"
    assert "providerRefusal = $providerRefusal" in body, "the receipt must carry the refusal verbatim"
    assert "elseif ($null -ne $providerRefusal) { 'refused' }" in body, "state must have a third value"
    assert "$null -eq $failure -and $null -eq $providerRefusal -and $processEnded -and $workEvidence.workCompleted -eq $true" in body, (
        "complete must be false on refusal, on failure, and without positive work evidence"
    )


# --- Invoke-Lane: `complete` is POSITIVE evidence the work finished, never "the process ended" ---
# Incident 2026-09-14, fleet-runs\ws-PLAY-COUNTERS-CPU-20260914T151951Z: exitCode 1 and this
# envelope (trimmed to the deciding fields), yet the receipt said state=complete, complete=true.
REAL_CLAUDE_MAX_TURNS_ENVELOPE = (
    '{"type":"result","subtype":"error_max_turns","is_error":true,"num_turns":66,'
    '"stop_reason":"tool_use","terminal_reason":"max_turns","total_cost_usd":4.87,'
    '"errors":["Reached maximum number of turns (65)"]}\n'
)
CLAUDE_SUCCESS_ENVELOPE = (
    '{"type":"result","subtype":"success","is_error":false,"num_turns":18,'
    '"stop_reason":"end_turn","terminal_reason":"completed","result":"done"}\n'
)
# Measured on this board: is_error=true while subtype still says success.
CLAUDE_API_ERROR_SUCCESS_SUBTYPE_ENVELOPE = (
    '{"type":"result","subtype":"success","is_error":true,"terminal_reason":"api_error","result":"x"}\n'
)


def _work_evidence(tmp_path, engine, answer, exit_code):
    asrc = tmp_path / "lane-answer.txt"
    asrc.write_text(answer, encoding="utf-8")
    cmd = (
        f". '{REFUSAL_HELPER}'; "
        f"$a = [IO.File]::ReadAllText('{asrc}'); "
        f"Get-LaneWorkEvidence -Engine '{engine}' -Answer $a -ExitCode {exit_code} | ConvertTo-Json -Compress"
    )
    out = subprocess.run(
        ["pwsh", "-NoLogo", "-NoProfile", "-NonInteractive", "-Command", cmd],
        text=True, capture_output=True,
    )
    assert out.returncode == 0, out.stderr
    return json.loads(out.stdout.strip().splitlines()[-1])


def test_work_evidence_falsifier_exit_1_max_turns_is_not_complete(tmp_path):
    r = _work_evidence(tmp_path, "claude", REAL_CLAUDE_MAX_TURNS_ENVELOPE, 1)
    assert r["workCompleted"] is False, r
    assert r["subtype"] == "error_max_turns" and r["terminalReason"] == "max_turns", r


def test_work_evidence_max_turns_is_not_complete_even_with_exit_0(tmp_path):
    # The envelope alone must defeat completion: exit code is necessary, never sufficient.
    r = _work_evidence(tmp_path, "claude", REAL_CLAUDE_MAX_TURNS_ENVELOPE, 0)
    assert r["workCompleted"] is False, r
    assert r["reason"] == "envelope-is-error", r


def test_work_evidence_requires_every_success_signal(tmp_path):
    assert _work_evidence(tmp_path, "claude", CLAUDE_SUCCESS_ENVELOPE, 0)["workCompleted"] is True
    assert _work_evidence(tmp_path, "claude", CLAUDE_API_ERROR_SUCCESS_SUBTYPE_ENVELOPE, 0)["workCompleted"] is False
    # No envelope on the claude engine is absence of evidence, not completion.
    assert _work_evidence(tmp_path, "claude", "", 0)["reason"] == "no-result-envelope"
    assert _work_evidence(tmp_path, "claude", "not json at all", 0)["workCompleted"] is False
    # codex exposes no envelope: exit 0 is the only observable, and non-zero defeats it.
    assert _work_evidence(tmp_path, "codex", "answer", 0)["workCompleted"] is True
    assert _work_evidence(tmp_path, "codex", "answer", 1)["workCompleted"] is False


def test_invoke_lane_receipt_separates_process_ended_from_complete():
    body = LANE_RUNNER.read_text(encoding="utf-8")
    assert "processEnded = $processEnded" in body
    assert "complete     = $workCompleted" in body
    assert "workEvidence = $workEvidence" in body
    assert "elseif ($workCompleted) { 'complete' }" in body, "state=complete must require work evidence"
    assert "elseif ($processEnded) { 'ended-incomplete' }" in body


def test_invoke_lane_propagates_a_refusal_as_125_ahead_of_the_child_code():
    # 124 timeout, 127 never-completed, 125 provider refused: three sentinels, three meanings.
    # The refusal clause must come FIRST and break, because PowerShell's switch runs every
    # matching clause and a refusal can arrive with any child exit code.
    body = LANE_RUNNER.read_text(encoding="utf-8")
    i_refused = body.index("{ $null -ne $providerRefusal } { 125; break }")
    i_timeout = body.index("-1      { 124 }")
    assert i_refused < i_timeout
# =============================================================================================
# TOOL-LOOP-PLUMBING-1: install-arg forwarding, kind-based lane resolution, the editing dispatch
# (worktree + composer + reservations + kill-switch recheck), and the pre-dispatch PR-review
# evidence exporter.
# =============================================================================================

LOOP_INSTALL_ARGS = ROOT / "tools" / "coordination" / "loop-install-args.ps1"
COMPOSE_CLI = ROOT / "tools" / "coordination" / "Compose-LanePrompt.ps1"
EXPORT_PR_EVIDENCE = ROOT / "tools" / "coordination" / "Export-PrReviewEvidence.ps1"
TEMPLATE_TEXT = (ROOT / "docs" / "lane-prompts" / "v2" / "product-card-TEMPLATE.md").read_text(encoding="utf-8")


def run_pwsh_command(cmd):
    return subprocess.run(
        ["pwsh", "-NoProfile", "-NonInteractive", "-ExecutionPolicy", "Bypass", "-Command", cmd],
        text=True, capture_output=True,
    )


# --- loop-install-args.ps1: -Install must persist EVERY schedulable parameter --------------
# UNTIL THIS CARD -Install persisted only four of the loop's own parameters (DailyBudget,
# MaxDispatchesPerCycle, TimeoutSec, StaleHours) and silently dropped -Tracks/-Lane/-AllowEdits,
# so a reinstall reset every one of them to its default -- the same class of defect that made the
# 2026-09-03 budget-flag omission an emergency. Get-InstallArgLine/Resolve-Tracks are pure and
# dot-sourced precisely so they can be tested without running the loop itself.

def call_get_install_arg_line(tracks, lane="", allow_edits=False):
    tracks_literal = "@(" + ",".join("'%s'" % t for t in tracks) + ")"
    cmd = (
        ". '%s'; Get-InstallArgLine -ScriptPath 'X:\\loop.ps1' -DailyBudget 12 "
        "-MaxDispatchesPerCycle 2 -TimeoutSec 1500 -StaleHours 12 -Tracks %s%s%s"
        % (
            LOOP_INSTALL_ARGS.as_posix(),
            tracks_literal,
            " -Lane %s" % lane if lane else "",
            " -AllowEdits" if allow_edits else "",
        )
    )
    return run_pwsh_command(cmd)


def test_install_arg_line_forwards_tracks_lane_and_allowedits():
    result = call_get_install_arg_line(["product", "playback"], lane="sonnet", allow_edits=True)
    assert result.returncode == 0, result.stderr
    line = result.stdout.strip()
    assert '-Tracks "product,playback"' in line, line
    assert "-Lane sonnet" in line, line
    assert "-AllowEdits" in line, line


def test_install_arg_line_omits_lane_and_allowedits_when_unset():
    result = call_get_install_arg_line(["playback"])
    assert result.returncode == 0, result.stderr
    line = result.stdout.strip()
    assert '-Tracks "playback"' in line, line
    assert "-Lane" not in line, line
    assert "-AllowEdits" not in line, line


def test_resolve_tracks_splits_a_comma_joined_single_string():
    """A pwsh -File scheduled-task action hands -Tracks back as ONE literal string; Resolve-Tracks
    is what the loop calls immediately after binding $Tracks to undo that."""
    cmd = ". '%s'; (Resolve-Tracks -Tracks @('product,playback')) -join '|'" % LOOP_INSTALL_ARGS.as_posix()
    result = run_pwsh_command(cmd)
    assert result.returncode == 0, result.stderr
    assert result.stdout.strip() == "product|playback"


def test_resolve_tracks_leaves_a_genuine_multi_element_array_alone():
    cmd = ". '%s'; (Resolve-Tracks -Tracks @('product','playback')) -join '|'" % LOOP_INSTALL_ARGS.as_posix()
    result = run_pwsh_command(cmd)
    assert result.returncode == 0, result.stderr
    assert result.stdout.strip() == "product|playback"


def test_resolve_tracks_does_not_split_a_single_track_that_has_no_comma():
    cmd = ". '%s'; (Resolve-Tracks -Tracks @('UNSET')) -join '|'" % LOOP_INSTALL_ARGS.as_posix()
    result = run_pwsh_command(cmd)
    assert result.returncode == 0, result.stderr
    assert result.stdout.strip() == "UNSET"


def test_a_dry_run_cycle_started_from_the_persisted_arg_line_reports_the_resolved_tracks():
    """Proves the round trip through REAL pwsh -File argument binding, not just the pure
    Resolve-Tracks function in isolation: -Tracks "product,playback" arrives at the loop as a
    single-element array and must still resolve and print as two comma-separated tracks.
    -MaxDispatchesPerCycle 0 means no dispatch is attempted regardless of the real board's queue
    state, and the kill switch (armed or not) is checked only AFTER this line is printed.

    MINOR 4 (sol round-1 review): this used to hand-reconstruct the scheduled-task argument list
    directly as a Python literal (-Tracks "product,playback" -Lane sonnet -AllowEdits), never
    actually calling Get-InstallArgLine -- so a regression in the arg-line BUILDER itself (a
    dropped flag, wrong quoting, wrong flag spelling) would not show up here even though this
    test's whole point is the exact-line install/reinstall contract. Now it calls
    Get-InstallArgLine for real, takes its ACTUAL returned string, and parses THAT string into an
    argv list -- exactly what a `pwsh -File` scheduled-task action does with a persisted
    Arguments string -- before starting the -DryRun cycle from it."""
    # -MaxDispatchesPerCycle 0 goes INTO the persisted line itself (not appended afterwards): the
    # loop's own param binder throws "specified more than once" if the same flag appears twice in
    # argv, and 0 makes the per-track dispatch loop break before attempting a single real
    # dispatch against the live board, which -DryRun alone would not prevent (it would still
    # forward -DryRun to up to MaxDispatchesPerCycle real dispatcher invocations).
    get_line_cmd = (
        ". '%s'; Get-InstallArgLine -ScriptPath '%s' -DailyBudget 12 -MaxDispatchesPerCycle 0 "
        "-TimeoutSec 1500 -StaleHours 12 -Tracks @('product','playback') -Lane sonnet -AllowEdits"
        % (LOOP_INSTALL_ARGS.as_posix(), LOOP.as_posix())
    )
    line_result = run_pwsh_command(get_line_cmd)
    assert line_result.returncode == 0, line_result.stdout + line_result.stderr
    arg_line = line_result.stdout.strip()
    assert '-Tracks "product,playback"' in arg_line, arg_line
    assert '-Lane sonnet' in arg_line, arg_line
    assert '-AllowEdits' in arg_line, arg_line

    # shlex with posix=False keeps Windows-style backslash paths intact (no backslash-escape
    # processing) while still respecting quotes for word-splitting; the quote characters
    # themselves are left attached and must be stripped before use as a real argv element,
    # since subprocess.run's list form passes each element through literally (no shell requoting).
    raw_tokens = shlex.split(arg_line, posix=False)
    argv = [t[1:-1] if len(t) >= 2 and t[0] == '"' and t[-1] == '"' else t for t in raw_tokens]

    result = subprocess.run(
        ["pwsh.exe", *argv, "-DryRun"],
        text=True, capture_output=True, timeout=120,
    )
    assert "LOOP: tracks=product, playback" in result.stdout, (
        "argLine=%r argv=%r\n%s%s" % (arg_line, argv, result.stdout, result.stderr)
    )


# --- kind/owner/scope-based lane resolution (deliverable 2) ---------------------------------

def test_kind_and_owner_resolve_the_lane_to_sonnet():
    """0.18 seeds every product/playback card with kind and owner=sonnet; the dispatcher must
    route it to the sonnet lane with no explicit -Lane, replacing the old needsShell-only
    heuristic for any card that names both fields."""
    with_tmp_result = run_dispatcher_lane_test(
        {"id": "TEST-KIND-1", "state": "queued", "track": "product", "kind": "product",
         "owner": "sonnet", "priority": 1},
        "-Track", "product",
    )
    assert "lane=sonnet" in with_tmp_result.stdout, with_tmp_result.stdout + with_tmp_result.stderr


def test_a_kind_only_card_is_not_selected_by_track_filtering():
    """S82: track SELECTION stays keyed on the queue's own `track` field, never on `kind` -- a
    card that carries kind=product but no track field is UNSET on track (Get-Track only reads
    `track`) and must not be picked by an explicit -Track product, even though its kind matches.
    An explicit non-auto -Track that matches nothing reports NO-LIVE-CARDS on that track (a
    distinct, pre-existing diagnostic from the auto-pool's own no-board-track-set) -- what this
    test actually pins is that the card is never selected, whichever diagnostic explains why."""
    result = run_dispatcher_lane_test(
        {"id": "TEST-KIND-ONLY-1", "state": "queued", "kind": "product", "owner": "sonnet", "priority": 1},
        "-Track", "product",
    )
    assert "WORKSTREAM: track=product card=TEST-KIND-ONLY-1" not in result.stdout, result.stdout + result.stderr
    assert result.returncode != 0


def test_a_recon_scope_prefix_routes_to_luna():
    result = run_dispatcher_lane_test(
        {"id": "TEST-RECON-1", "state": "queued", "track": "factory", "priority": 1,
         "scope": "RECON: survey the CI queue"},
        "-Track", "factory",
    )
    assert "lane=luna" in result.stdout, result.stdout + result.stderr


def test_a_review_scope_prefix_routes_to_fable():
    result = run_dispatcher_lane_test(
        {"id": "TEST-REVIEW-1", "state": "queued", "track": "factory", "priority": 1,
         "scope": "REVIEW: read the plan"},
        "-Track", "factory",
    )
    assert "lane=fable" in result.stdout, result.stdout + result.stderr


def test_explicit_lane_wins_over_kind_based_resolution():
    result = run_dispatcher_lane_test(
        {"id": "TEST-EXPLICIT-1", "state": "queued", "track": "product", "kind": "product",
         "owner": "sonnet", "priority": 1},
        "-Track", "product", "-Lane", "fable",
    )
    assert "lane=fable" in result.stdout, result.stdout + result.stderr


def run_dispatcher_lane_test(item, *extra):
    import tempfile
    with tempfile.TemporaryDirectory() as td:
        return run_dispatcher(Path(td), [item], *extra)


# --- deliverable 9 wiring: the dispatcher itself must call the pre-dispatch PR-review evidence
# exporter before a review-lane starts (MAJOR 1, sol round-1 review). The exporter script has its
# own standalone tests further down; these three exercise the DISPATCHER's wiring specifically,
# with a fake exporter shim standing in for the real one -- matching the existing fake-gh shim
# pattern rather than re-testing the exporter in isolation, which is exactly what sol's finding
# says the previous suite was missing (the only reference to the exporter outside its own test
# was in this file, never in a production caller).

FAKE_EXPORTER_SHIM = (
    "param([int]$PrNumber,[string]$RunDir,[string]$RepoRoot,[string]$GhExe)\n"
    "if (-not (Test-Path -LiteralPath $RunDir)) { New-Item -ItemType Directory -Path $RunDir -Force | Out-Null }\n"
    "Set-Content -LiteralPath (Join-Path $RunDir 'exporter-called.txt') "
    "-Value ($PrNumber.ToString() + '|' + $RunDir)\n"
    "exit 0\n"
)

FAKE_EXPORTER_SHIM_REFUSES = (
    "param([int]$PrNumber,[string]$RunDir,[string]$RepoRoot,[string]$GhExe)\n"
    "Write-Output \"REFUSED: pr-head-drift before=a after=b pr=$PrNumber\"\n"
    "exit 3\n"
)


def test_a_review_lane_dispatch_calls_the_pr_evidence_exporter_before_the_lane_starts(tmp_path):
    """The dispatcher must call Export-PrReviewEvidence.ps1 itself before a review-lane (routed
    here by the REVIEW: scope rule to fable) starts, rather than leaving the lane to call `gh`
    and hit the read-only sandbox's Access-is-denied wall. Run under -DryRun (via
    run_dispatcher_lane_test), so the exporter call must happen BEFORE the dry-run exit -- exactly
    like the generic hosted-evidence export it sits beside."""
    fake_exporter = tmp_path / "fake-exporter.ps1"
    fake_exporter.write_text(FAKE_EXPORTER_SHIM, encoding="ascii")
    result = run_dispatcher_lane_test(
        {"id": "TEST-REVIEW-EVIDENCE-1", "state": "queued", "track": "factory", "priority": 1,
         "scope": "REVIEW: read the plan", "prNumber": 81},
        "-Track", "factory", "-ExporterPath", str(fake_exporter),
    )
    assert "lane=fable" in result.stdout, result.stdout + result.stderr
    # NOT \S+: the real board root is "C:\!Layi Wkspc\MLV-App", which contains a space, and
    # runDir is the last field on its line -- capture to end-of-line, not to the first whitespace.
    export_line = next(
        (l for l in result.stdout.splitlines() if "pre-dispatch review-evidence export" in l), None
    )
    assert export_line, "no export line reported: %s" % result.stdout
    run_dir = Path(export_line.split("runDir=", 1)[1].strip())
    marker = run_dir / "exporter-called.txt"
    try:
        assert marker.exists(), "the dispatcher never invoked the exporter: %s" % result.stdout
        assert marker.read_text(encoding="utf-8").startswith("81|"), marker.read_text(encoding="utf-8")
    finally:
        shutil.rmtree(run_dir, ignore_errors=True)


def test_a_review_lane_dispatch_is_refused_when_the_exporter_refuses(tmp_path):
    """Fail CLOSED, the opposite polarity of the generic hosted-evidence export: unverified PR
    evidence is worse than no review at all, so an exporter refusal must stop the dispatch."""
    fake_exporter = tmp_path / "fake-exporter-fail.ps1"
    fake_exporter.write_text(FAKE_EXPORTER_SHIM_REFUSES, encoding="ascii")
    result = run_dispatcher_lane_test(
        {"id": "TEST-REVIEW-EVIDENCE-2", "state": "queued", "track": "factory", "priority": 1,
         "scope": "REVIEW: read the plan", "prNumber": 81},
        "-Track", "factory", "-ExporterPath", str(fake_exporter),
    )
    assert "REFUSED review-evidence-export-failed" in result.stdout, result.stdout + result.stderr
    assert result.returncode != 0
    assert "WORKSTREAM: track=factory card=TEST-REVIEW-EVIDENCE-2" not in result.stdout


def test_a_review_lane_dispatch_with_no_pr_number_does_not_call_the_exporter(tmp_path):
    """Scoped to cards that actually name a PR: a review-lane card with nothing to bind the
    exporter's mandatory -PrNumber to must not attempt the call at all."""
    fake_exporter = tmp_path / "fake-exporter-unused.ps1"
    fake_exporter.write_text(FAKE_EXPORTER_SHIM, encoding="ascii")
    result = run_dispatcher_lane_test(
        {"id": "TEST-REVIEW-EVIDENCE-3", "state": "queued", "track": "factory", "priority": 1,
         "scope": "REVIEW: read the plan"},
        "-Track", "factory", "-ExporterPath", str(fake_exporter),
    )
    assert "lane=fable" in result.stdout, result.stdout + result.stderr
    assert "pre-dispatch review-evidence export" not in result.stdout, result.stdout


# --- editing dispatch: procedure/worktree/composition/reservations (deliverables 2,3,5,6,7) -

def sha256_of(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def write_fields_text(card_id, extra_lines=""):
    return (
        "# FIELDS for %s\n"
        "CARD_ID: %s\n"
        "PRIORITY: 1\n"
        "CLIP_OR_NONE: none\n"
        "ALLOWED_PATHS: some/path.cpp\n"
        "DELIVERABLE: do the thing\n"
        "ACCEPTANCE: run the test\n"
        "VERIFY_FIRST: check first\n"
        "%s" % (card_id, card_id, extra_lines)
    )


def write_fields_card(dir_path, card_id, extra_lines=""):
    path = Path(dir_path) / ("fields-%s.md" % card_id)
    path.write_text(write_fields_text(card_id, extra_lines), encoding="ascii")
    return path


def editing_board(tmp_path, with_lane_shim=True):
    """A throwaway git repo playing the board root: a 'fork/master' ref for baseSha resolution
    and worktree-add, and the dual-lane coordination tree an editing dispatch reads from (the
    composer template, and by default a fake Start-EditingLane.ps1 shim so no real lane is ever
    started by a test)."""
    init_repo(tmp_path)
    subprocess.run(["git", "config", "user.email", "t@e.com"], cwd=tmp_path, check=True)
    subprocess.run(["git", "config", "user.name", "T"], cwd=tmp_path, check=True)
    (tmp_path / "seed.txt").write_text("seed\n")
    subprocess.run(["git", "add", "seed.txt"], cwd=tmp_path, check=True)
    subprocess.run(["git", "commit", "-qm", "seed"], cwd=tmp_path, check=True)
    head = git(tmp_path, "rev-parse", "HEAD")
    subprocess.run(["git", "update-ref", "refs/remotes/fork/master", head], cwd=tmp_path, check=True)

    dual = tmp_path / ".claude-state" / "coordination" / "dual-lane"
    (dual / "prompts" / "v2").mkdir(parents=True)
    (dual / "prompts" / "v2" / "product-card-TEMPLATE.md").write_text(TEMPLATE_TEXT, encoding="utf-8")
    (dual / "receipts").mkdir(parents=True, exist_ok=True)

    if with_lane_shim:
        shim = dual / "Start-EditingLane.ps1"
        shim.write_text(
            "param([string]$Lane,[string]$PromptFile,[string]$WorkDir,[string]$Card,"
            "[string]$RunDir,[string]$ExtraReadDir,[int]$TimeoutSec)\n"
            "Write-Output ('SHIM: lane=' + $Lane + ' card=' + $Card + ' workDir=' + $WorkDir)\n"
            "exit 0\n",
            encoding="ascii",
        )
    return dual, head


def editing_dispatch_env(tmp_path):
    e = dict(os.environ)
    e["MLV_BOARD_ROOT"] = str(tmp_path)
    return e


def cleanup_lane_worktree(board_root, card_id):
    """Best-effort cleanup: a real -DryRun (or shimmed real) editing dispatch creates then
    removes its own C:\\mlvtmp\\lane-<card>-<ts> worktree, but a failed assertion must not leak
    one if a bug ever leaves it behind."""
    for p in glob.glob(str(Path("C:/mlvtmp") / ("lane-%s-*" % card_id))):
        subprocess.run(["git", "-C", str(board_root), "worktree", "remove", p, "--force"],
                        capture_output=True)
        shutil.rmtree(p, ignore_errors=True)
    subprocess.run(["git", "-C", str(board_root), "worktree", "prune"], capture_output=True)


def run_editing_dispatch(tmp_path, queue_items, card_id, extra=(), dry_run=True):
    queue_path = tmp_path / "queue.json"
    queue_path.write_text(json.dumps({"schema": "test", "items": queue_items}))
    args = [
        "pwsh.exe", "-NoLogo", "-NoProfile", "-NonInteractive", "-ExecutionPolicy", "Bypass",
        "-File", str(WORKSTREAM), "-QueuePath", str(queue_path), "-CardId", card_id,
        "-AllowEdits", "-NoLandingProbe", *extra,
    ]
    if dry_run:
        args.append("-DryRun")
    return subprocess.run(args, text=True, capture_output=True, env=editing_dispatch_env(tmp_path))


def test_editing_dispatch_prints_workdir_lane_and_a_full_basesha(tmp_path):
    """Acceptance: a -DryRun dispatch of an eligible product card prints workDir=, lane=sonnet,
    and baseSha=<40 hex> -- baseSha is fork/master resolved at dispatch time in the board repo."""
    dual, head = editing_board(tmp_path)
    proc = write_fields_card(dual / "prompts" / "v2", "TEST-EDIT-A")
    item = {"id": "TEST-EDIT-A", "state": "queued", "track": "product", "kind": "product",
            "owner": "sonnet", "priority": 1,
            "procedure": ".claude-state/coordination/dual-lane/prompts/v2/fields-TEST-EDIT-A.md",
            "procedureSha256": sha256_of(proc)}
    try:
        result = run_editing_dispatch(tmp_path, [item], "TEST-EDIT-A")
        assert result.returncode == 0, result.stdout + result.stderr
        assert "lane=sonnet" in result.stdout, result.stdout
        m = re.search(r"WORKSTREAM: workDir=(\S+)", result.stdout)
        assert m, result.stdout
        m2 = re.search(r"WORKSTREAM: baseSha=([0-9a-f]{40})\b", result.stdout)
        assert m2, result.stdout
        assert m2.group(1) == head
    finally:
        cleanup_lane_worktree(tmp_path, "TEST-EDIT-A")


def _reuse_card(dual, card_id):
    proc = write_fields_card(dual / "prompts" / "v2", card_id)
    return {"id": card_id, "state": "queued", "track": "product", "kind": "product",
            "owner": "sonnet", "priority": 1,
            "procedure": ".claude-state/coordination/dual-lane/prompts/v2/fields-%s.md" % card_id,
            "procedureSha256": sha256_of(proc)}


def test_editing_dispatch_reuses_a_stale_branch_with_no_unique_commits_and_moves_it_to_base(tmp_path):
    """A branch left by an earlier attempt, sitting on an OLDER commit that is an ancestor of
    baseSha, must not block `worktree add` forever: it is reused and moved to baseSha, so the
    lane starts from the fresh base, never the stale tip."""
    dual, first = editing_board(tmp_path)
    subprocess.run(["git", "branch", "product/TEST-EDIT-REUSE-1", first], cwd=tmp_path, check=True)
    (tmp_path / "seed2.txt").write_text("more\n")
    subprocess.run(["git", "add", "seed2.txt"], cwd=tmp_path, check=True)
    subprocess.run(["git", "commit", "-qm", "second"], cwd=tmp_path, check=True)
    head = git(tmp_path, "rev-parse", "HEAD")
    subprocess.run(["git", "update-ref", "refs/remotes/fork/master", head], cwd=tmp_path, check=True)
    try:
        result = run_editing_dispatch(tmp_path, [_reuse_card(dual, "TEST-EDIT-REUSE-1")], "TEST-EDIT-REUSE-1")
        assert result.returncode == 0, result.stdout + result.stderr
        assert "reusing existing branch product/TEST-EDIT-REUSE-1" in result.stdout, result.stdout
        assert git(tmp_path, "rev-parse", "refs/heads/product/TEST-EDIT-REUSE-1") == head
    finally:
        cleanup_lane_worktree(tmp_path, "TEST-EDIT-REUSE-1")


def test_editing_dispatch_refuses_an_existing_branch_that_carries_work(tmp_path):
    """A branch with a commit NOT in baseSha holds work; the dispatcher must refuse (exit 6)
    and must not move or overwrite the branch."""
    dual, head = editing_board(tmp_path)
    subprocess.run(["git", "checkout", "-q", "-b", "product/TEST-EDIT-REUSE-2"], cwd=tmp_path, check=True)
    (tmp_path / "work.txt").write_text("lane work\n")
    subprocess.run(["git", "add", "work.txt"], cwd=tmp_path, check=True)
    subprocess.run(["git", "commit", "-qm", "lane work"], cwd=tmp_path, check=True)
    tip = git(tmp_path, "rev-parse", "HEAD")
    subprocess.run(["git", "checkout", "-q", "--detach", head], cwd=tmp_path, check=True)
    try:
        result = run_editing_dispatch(tmp_path, [_reuse_card(dual, "TEST-EDIT-REUSE-2")], "TEST-EDIT-REUSE-2")
        assert result.returncode == 6, result.stdout + result.stderr
        assert "REFUSED existing-branch-has-work card=TEST-EDIT-REUSE-2" in result.stdout, result.stdout
        assert git(tmp_path, "rev-parse", "refs/heads/product/TEST-EDIT-REUSE-2") == tip
    finally:
        cleanup_lane_worktree(tmp_path, "TEST-EDIT-REUSE-2")


def test_a_real_refused_dispatch_leaves_a_typed_attempt_receipt(tmp_path):
    """2026-09-14: ~90 PLAY-COUNTERS-CPU run dirs held only lane-prompt.md because a pre-launch
    refusal reached stdout alone. A NON-dry-run refusal must leave dispatch-attempt.json naming
    the cause, and no lane may have started (no lane receipt)."""
    dual, head = editing_board(tmp_path)
    subprocess.run(["git", "checkout", "-q", "-b", "product/TEST-EDIT-ATTEMPT"], cwd=tmp_path, check=True)
    (tmp_path / "work.txt").write_text("lane work\n")
    subprocess.run(["git", "add", "work.txt"], cwd=tmp_path, check=True)
    subprocess.run(["git", "commit", "-qm", "lane work"], cwd=tmp_path, check=True)
    subprocess.run(["git", "checkout", "-q", "--detach", head], cwd=tmp_path, check=True)
    try:
        result = run_editing_dispatch(tmp_path, [_reuse_card(dual, "TEST-EDIT-ATTEMPT")], "TEST-EDIT-ATTEMPT",
                                      dry_run=False)
        assert result.returncode == 6, result.stdout + result.stderr
        run_dirs = glob.glob(str(tmp_path / ".claude-state" / "fleet-runs" / "ws-TEST-EDIT-ATTEMPT-*"))
        assert len(run_dirs) == 1, run_dirs
        attempt = json.loads((Path(run_dirs[0]) / "dispatch-attempt.json").read_text(encoding="utf-8"))
        assert attempt["schema"] == "mlv-app/workstream-dispatch-attempt/v1"
        assert attempt["outcome"] == "refused-before-launch", attempt
        assert attempt["cause"] == "existing-branch-has-work", attempt
        assert attempt["exitCode"] == 6 and attempt["laneReceipts"] == [], attempt
    finally:
        cleanup_lane_worktree(tmp_path, "TEST-EDIT-ATTEMPT")


def test_every_workstream_exit_after_the_run_dir_is_named_writes_an_attempt_receipt():
    """Structural: once $runDir is named, every exit except the two DryRun exits is immediately
    preceded by Write-DispatchAttempt, and both launch paths record 'launched' before and after."""
    lines = WORKSTREAM.read_text(encoding="utf-8").splitlines()
    start = next(i for i, l in enumerate(lines) if "function Write-DispatchAttempt" in l)
    unreceipted = []
    for i in range(start, len(lines)):
        if re.match(r"^\s*exit\b", lines[i]):
            prev = lines[i - 1].strip()
            if "Write-DispatchAttempt" in prev or "DRY RUN" in prev or "WORKSTREAM: dispatched" in prev:
                continue
            unreceipted.append((i + 1, prev))
    assert not unreceipted, unreceipted
    body = "\n".join(lines)
    assert body.count("-Outcome 'launched' -Cause 'lane-starting'") == 2
    assert body.count("-Outcome 'launched' -Cause 'lane-returned'") == 2


def test_editing_dispatch_creates_a_new_branch_when_none_exists(tmp_path):
    dual, head = editing_board(tmp_path)
    try:
        result = run_editing_dispatch(tmp_path, [_reuse_card(dual, "TEST-EDIT-REUSE-3")], "TEST-EDIT-REUSE-3")
        assert result.returncode == 0, result.stdout + result.stderr
        assert "reusing existing branch" not in result.stdout
        assert git(tmp_path, "rev-parse", "refs/heads/product/TEST-EDIT-REUSE-3") == head
    finally:
        cleanup_lane_worktree(tmp_path, "TEST-EDIT-REUSE-3")


def test_two_editing_dispatches_get_two_distinct_worktree_paths(tmp_path):
    dual, head = editing_board(tmp_path)
    proc_a = write_fields_card(dual / "prompts" / "v2", "TEST-EDIT-B1")
    proc_b = write_fields_card(dual / "prompts" / "v2", "TEST-EDIT-B2")
    items = [
        {"id": "TEST-EDIT-B1", "state": "queued", "track": "product", "kind": "product",
         "owner": "sonnet", "priority": 1,
         "procedure": ".claude-state/coordination/dual-lane/prompts/v2/fields-TEST-EDIT-B1.md",
         "procedureSha256": sha256_of(proc_a)},
        {"id": "TEST-EDIT-B2", "state": "queued", "track": "product", "kind": "product",
         "owner": "sonnet", "priority": 1,
         "procedure": ".claude-state/coordination/dual-lane/prompts/v2/fields-TEST-EDIT-B2.md",
         "procedureSha256": sha256_of(proc_b)},
    ]
    workdirs = []
    try:
        for card_id in ("TEST-EDIT-B1", "TEST-EDIT-B2"):
            result = run_editing_dispatch(tmp_path, items, card_id)
            assert result.returncode == 0, result.stdout + result.stderr
            m = re.search(r"WORKSTREAM: workDir=(\S+)", result.stdout)
            assert m, result.stdout
            workdirs.append(m.group(1))
        assert workdirs[0] != workdirs[1]
    finally:
        cleanup_lane_worktree(tmp_path, "TEST-EDIT-B1")
        cleanup_lane_worktree(tmp_path, "TEST-EDIT-B2")


def test_editing_dispatch_refuses_a_codex_lane(tmp_path):
    editing_board(tmp_path)
    item = {"id": "TEST-EDIT-CODEX-1", "state": "queued", "track": "product", "priority": 1}
    result = run_editing_dispatch(tmp_path, [item], "TEST-EDIT-CODEX-1", extra=("-Lane", "luna"))
    assert result.returncode == 6, result.stdout + result.stderr
    assert "REFUSED codex-lane-never-edits" in result.stdout


def test_editing_dispatch_refuses_a_card_with_no_procedure(tmp_path):
    editing_board(tmp_path)
    item = {"id": "TEST-EDIT-NOPROC-1", "state": "queued", "track": "product", "priority": 1}
    result = run_editing_dispatch(tmp_path, [item], "TEST-EDIT-NOPROC-1")
    assert result.returncode == 6, result.stdout + result.stderr
    assert "REFUSED procedure-missing-or-drifted" in result.stdout


def test_editing_dispatch_refuses_a_drifted_procedure_sha(tmp_path):
    dual, head = editing_board(tmp_path)
    proc = write_fields_card(dual / "prompts" / "v2", "TEST-EDIT-DRIFT-1")
    item = {"id": "TEST-EDIT-DRIFT-1", "state": "queued", "track": "product", "priority": 1,
            "procedure": ".claude-state/coordination/dual-lane/prompts/v2/fields-TEST-EDIT-DRIFT-1.md",
            "procedureSha256": "0" * 64}
    result = run_editing_dispatch(tmp_path, [item], "TEST-EDIT-DRIFT-1")
    assert result.returncode == 6, result.stdout + result.stderr
    assert "REFUSED procedure-missing-or-drifted" in result.stdout
    assert sha256_of(proc) != "0" * 64


def test_editing_dispatch_refuses_an_unknown_field(tmp_path):
    dual, head = editing_board(tmp_path)
    proc = write_fields_card(dual / "prompts" / "v2", "TEST-EDIT-UNK-1", extra_lines="FOO: a stray field\n")
    item = {"id": "TEST-EDIT-UNK-1", "state": "queued", "track": "product", "priority": 1,
            "procedure": ".claude-state/coordination/dual-lane/prompts/v2/fields-TEST-EDIT-UNK-1.md",
            "procedureSha256": sha256_of(proc)}
    result = run_editing_dispatch(tmp_path, [item], "TEST-EDIT-UNK-1")
    assert result.returncode == 6, result.stdout + result.stderr
    assert "REFUSED unknown-field" in result.stdout


def read_reservation_rows(dual):
    path = dual / "receipts" / "dispatch-reservations.jsonl"
    if not path.exists():
        return []
    return [json.loads(l) for l in path.read_text(encoding="utf-8").splitlines() if l.strip()]


def test_a_real_editing_dispatch_reserves_before_it_starts_and_charges_after(tmp_path):
    """S76: a 'reserved' row is written BEFORE the process launches and a 'charged' row after,
    sharing one reservationId -- the loop counts only 'reserved' rows for today's spend."""
    dual, head = editing_board(tmp_path)
    proc = write_fields_card(dual / "prompts" / "v2", "TEST-EDIT-RES-1")
    item = {"id": "TEST-EDIT-RES-1", "state": "queued", "track": "product", "kind": "product",
            "owner": "sonnet", "priority": 1,
            "procedure": ".claude-state/coordination/dual-lane/prompts/v2/fields-TEST-EDIT-RES-1.md",
            "procedureSha256": sha256_of(proc)}
    try:
        result = run_editing_dispatch(tmp_path, [item], "TEST-EDIT-RES-1", dry_run=False)
        assert result.returncode == 0, result.stdout + result.stderr
        assert "SHIM: lane=sonnet card=TEST-EDIT-RES-1" in result.stdout, result.stdout
        rows = [r for r in read_reservation_rows(dual) if r["card"] == "TEST-EDIT-RES-1"]
        assert len(rows) == 2, rows
        assert rows[0]["state"] == "reserved", rows
        assert rows[1]["state"] == "charged", rows
        assert rows[0]["reservationId"] == rows[1]["reservationId"]
    finally:
        cleanup_lane_worktree(tmp_path, "TEST-EDIT-RES-1")


def test_a_real_dispatch_refuses_when_the_kill_switch_is_armed_immediately_before_start(tmp_path):
    """The kill switch is re-checked immediately before this lane starts, not only once at the
    top of the loop's own cycle -- a long cycle can dispatch several lanes, and the switch may be
    armed between the cycle's check and this particular start."""
    dual, head = editing_board(tmp_path)
    proc = write_fields_card(dual / "prompts" / "v2", "TEST-EDIT-KILL-1")
    (dual / "WORKSTREAM-LOOP-DISABLED").write_text("armed for test\n", encoding="utf-8")
    item = {"id": "TEST-EDIT-KILL-1", "state": "queued", "track": "product", "priority": 1,
            "procedure": ".claude-state/coordination/dual-lane/prompts/v2/fields-TEST-EDIT-KILL-1.md",
            "procedureSha256": sha256_of(proc)}
    try:
        result = run_editing_dispatch(tmp_path, [item], "TEST-EDIT-KILL-1", dry_run=False)
        assert result.returncode == 6, result.stdout + result.stderr
        assert "REFUSED kill-switch-armed" in result.stdout
        assert "SHIM:" not in result.stdout
        assert not [r for r in read_reservation_rows(dual) if r["card"] == "TEST-EDIT-KILL-1"]
    finally:
        cleanup_lane_worktree(tmp_path, "TEST-EDIT-KILL-1")


def test_kill_switch_armed_mid_run_by_the_lane_itself_blocks_the_next_dispatch(tmp_path):
    """MINOR 3 (sol round-1 review): the test above pre-arms the switch from Python BEFORE either
    dispatch runs, which an early one-time check (e.g. only at the top of a cycle, not
    immediately before each lane's own start) could also satisfy. Here the fake lane shim arms
    the switch itself, from inside what a real lane's own process would be, so only a check that
    re-reads the file at THIS dispatch's own start -- not a cached or once-per-cycle value -- can
    catch it before the second, separate dispatch."""
    dual, head = editing_board(tmp_path, with_lane_shim=False)
    proc_a = write_fields_card(dual / "prompts" / "v2", "TEST-EDIT-MIDRUN-A")
    proc_b = write_fields_card(dual / "prompts" / "v2", "TEST-EDIT-MIDRUN-B")
    kill_switch = dual / "WORKSTREAM-LOOP-DISABLED"
    shim = dual / "Start-EditingLane.ps1"
    shim.write_text(
        "param([string]$Lane,[string]$PromptFile,[string]$WorkDir,[string]$Card,"
        "[string]$RunDir,[string]$ExtraReadDir,[int]$TimeoutSec)\n"
        "Set-Content -LiteralPath '%s' -Value 'armed mid-run by the lane'\n"
        "Write-Output ('SHIM: lane=' + $Lane + ' card=' + $Card + ' workDir=' + $WorkDir)\n"
        "exit 0\n" % kill_switch.as_posix(),
        encoding="ascii",
    )
    items = [
        {"id": "TEST-EDIT-MIDRUN-A", "state": "queued", "track": "product", "kind": "product",
         "owner": "sonnet", "priority": 1,
         "procedure": ".claude-state/coordination/dual-lane/prompts/v2/fields-TEST-EDIT-MIDRUN-A.md",
         "procedureSha256": sha256_of(proc_a)},
        {"id": "TEST-EDIT-MIDRUN-B", "state": "queued", "track": "product", "kind": "product",
         "owner": "sonnet", "priority": 1,
         "procedure": ".claude-state/coordination/dual-lane/prompts/v2/fields-TEST-EDIT-MIDRUN-B.md",
         "procedureSha256": sha256_of(proc_b)},
    ]
    try:
        first = run_editing_dispatch(tmp_path, items, "TEST-EDIT-MIDRUN-A", dry_run=False)
        assert first.returncode == 0, first.stdout + first.stderr
        assert "SHIM: lane=sonnet card=TEST-EDIT-MIDRUN-A" in first.stdout, first.stdout
        assert kill_switch.exists(), "the shim did not arm the switch it was given"

        second = run_editing_dispatch(tmp_path, items, "TEST-EDIT-MIDRUN-B", dry_run=False)
        assert second.returncode == 6, second.stdout + second.stderr
        assert "REFUSED kill-switch-armed" in second.stdout
        assert "SHIM: lane=sonnet card=TEST-EDIT-MIDRUN-B" not in second.stdout
    finally:
        cleanup_lane_worktree(tmp_path, "TEST-EDIT-MIDRUN-A")
        cleanup_lane_worktree(tmp_path, "TEST-EDIT-MIDRUN-B")


def test_the_reservation_row_exists_before_the_lane_itself_finishes(tmp_path):
    """MINOR 3 (sol round-1 review): appending a 'reserved' row before start and a 'charged' row
    after would also pass a check that only inspects the file once the whole dispatcher process
    has already returned. Here the fake lane shim reads the reservation file FROM INSIDE its own
    run and asserts its 'reserved' row for THIS card is already on disk -- proving the row lands
    before the lane's own process even finishes, not merely before the dispatcher's return."""
    dual, head = editing_board(tmp_path, with_lane_shim=False)
    proc = write_fields_card(dual / "prompts" / "v2", "TEST-EDIT-RESORDER-1")
    reservations = dual / "receipts" / "dispatch-reservations.jsonl"
    shim = dual / "Start-EditingLane.ps1"
    shim.write_text(
        "param([string]$Lane,[string]$PromptFile,[string]$WorkDir,[string]$Card,"
        "[string]$RunDir,[string]$ExtraReadDir,[int]$TimeoutSec)\n"
        "$rows = @()\n"
        "if (Test-Path -LiteralPath '%s') {\n"
        "    $rows = @(Get-Content -LiteralPath '%s' | ForEach-Object { $_ | ConvertFrom-Json })\n"
        "}\n"
        "$mine = @($rows | Where-Object { $_.card -eq $Card -and $_.state -eq 'reserved' })\n"
        "if ($mine.Count -eq 0) { Write-Output 'SHIM: NO-RESERVED-ROW-YET'; exit 1 }\n"
        "Write-Output ('SHIM: lane=' + $Lane + ' card=' + $Card + ' sawReservedRow=' + $mine[0].reservationId)\n"
        "exit 0\n" % (reservations.as_posix(), reservations.as_posix()),
        encoding="ascii",
    )
    item = {"id": "TEST-EDIT-RESORDER-1", "state": "queued", "track": "product", "kind": "product",
            "owner": "sonnet", "priority": 1,
            "procedure": ".claude-state/coordination/dual-lane/prompts/v2/fields-TEST-EDIT-RESORDER-1.md",
            "procedureSha256": sha256_of(proc)}
    try:
        result = run_editing_dispatch(tmp_path, [item], "TEST-EDIT-RESORDER-1", dry_run=False)
        assert result.returncode == 0, result.stdout + result.stderr
        assert "sawReservedRow=" in result.stdout, result.stdout
        assert "NO-RESERVED-ROW-YET" not in result.stdout
    finally:
        cleanup_lane_worktree(tmp_path, "TEST-EDIT-RESORDER-1")


# --- Invoke-Lane.ps1's own allowlist-required refusal, reachable only if something calls it
# directly instead of going through Start-EditingLane.ps1 -- which is exactly why deliverable 3
# requires EVERY editing dispatch to go through the wrapper.

def test_invoke_lane_refuses_allowedtools_all(tmp_path):
    cmd = (
        "try { & '%s' -Lane sonnet -Prompt 'x' -WorkDir '%s' -AllowEdits -AllowedTools 'ALL' } "
        "catch { $_.Exception.Message }" % (LANE_RUNNER.as_posix(), tmp_path.as_posix())
    )
    result = run_pwsh_command(cmd)
    assert "allowlist-required" in (result.stdout + result.stderr)


def test_invoke_lane_refuses_editing_with_allowedtools_entirely_absent(tmp_path):
    """MINOR 2 (sol round-1 review): the existing test above only covers -AllowedTools 'ALL'.
    The implementation's guard is `IsNullOrWhiteSpace($AllowedTools) -or $AllowedTools -eq 'ALL'`
    -- an `-or` with two independently reachable branches -- and the absent-argument branch
    (which binds $AllowedTools to its default '') had no test of its own until now."""
    cmd = (
        "try { & '%s' -Lane sonnet -Prompt 'x' -WorkDir '%s' -AllowEdits } "
        "catch { $_.Exception.Message }" % (LANE_RUNNER.as_posix(), tmp_path.as_posix())
    )
    result = run_pwsh_command(cmd)
    assert "allowlist-required" in (result.stdout + result.stderr)


# --- Compose-LanePrompt.ps1 / compose-lane-prompt-core.ps1: determinism, PR_STEP literals, ---
# field echo, unknown-field refusal (deliverable 6)

PR_STEP_LANE_CAN_OPEN_PR = (
    'gh pr create -R layibabalola/MLV-App --head {branch} --title "<card id>: <subject>" '
    '--body "<what, why, red run, green run>"; then print PR-OPENED: <number> as your last line.'
)
PR_STEP_OTHERWISE = (
    'Do NOT call gh. Print PUSHED: {branch} <head sha> as your last line; the dispatcher opens the PR.'
)


def compose(procedure_path, gh_capability, work_dir="C:\\mlvtmp\\lane-x", base_sha="a" * 40,
            run_dir=None, ts="20260101T000000Z"):
    run_dir = run_dir or (Path(procedure_path).parent / "run")
    args = [
        "pwsh.exe", "-NoLogo", "-NoProfile", "-NonInteractive", "-ExecutionPolicy", "Bypass",
        "-File", str(COMPOSE_CLI), "-ProcedurePath", str(procedure_path),
        "-WorkDir", work_dir, "-BaseSha", base_sha, "-RunDir", str(run_dir), "-Ts", ts,
        "-GhCapability", gh_capability,
    ]
    return subprocess.run(args, text=True, capture_output=True)


def test_composition_is_deterministic_byte_for_byte(tmp_path):
    proc = write_fields_card(tmp_path, "DET-1")
    r1 = compose(proc, "lane-can-open-pr")
    r2 = compose(proc, "lane-can-open-pr")
    assert r1.returncode == 0, r1.stdout + r1.stderr
    assert r1.stdout == r2.stdout


def test_composed_fields_prompt_carries_the_lane_can_open_pr_literal_exactly(tmp_path):
    proc = write_fields_card(tmp_path, "PR-STEP-A")
    result = compose(proc, "lane-can-open-pr")
    assert result.returncode == 0, result.stdout + result.stderr
    assert PR_STEP_LANE_CAN_OPEN_PR.format(branch="product/PR-STEP-A") in result.stdout


def test_composed_fields_prompt_carries_the_otherwise_literal_exactly(tmp_path):
    proc = write_fields_card(tmp_path, "PR-STEP-B")
    result = compose(proc, "no-pr-capability")
    assert result.returncode == 0, result.stdout + result.stderr
    assert PR_STEP_OTHERWISE.format(branch="product/PR-STEP-B") in result.stdout


def test_composed_full_card_prompt_carries_the_lane_can_open_pr_literal_exactly(tmp_path):
    card = tmp_path / "card-PR-STEP-C.md"
    card.write_text("# CARD: PR-STEP-C\nSomething.\n{{PR_STEP}}\n", encoding="utf-8")
    result = compose(card, "lane-can-open-pr")
    assert result.returncode == 0, result.stdout + result.stderr
    assert PR_STEP_LANE_CAN_OPEN_PR.format(branch="product/PR-STEP-C") in result.stdout


def test_composed_full_card_prompt_carries_the_otherwise_literal_exactly(tmp_path):
    card = tmp_path / "card-PR-STEP-D.md"
    card.write_text("# CARD: PR-STEP-D\nSomething.\n{{PR_STEP}}\n", encoding="utf-8")
    result = compose(card, "no-pr-capability")
    assert result.returncode == 0, result.stdout + result.stderr
    assert PR_STEP_OTHERWISE.format(branch="product/PR-STEP-D") in result.stdout


def test_every_parsed_field_appears_byte_for_byte_in_the_composed_prompt(tmp_path):
    proc = write_fields_card(tmp_path, "FIELD-ECHO-1")
    text = proc.read_text(encoding="ascii")
    fields = {}
    for line in text.splitlines():
        m = re.match(r"^([A-Z][A-Z0-9_]*):\s?(.*)$", line)
        if m:
            fields[m.group(1)] = m.group(2)
    result = compose(proc, "lane-can-open-pr")
    assert result.returncode == 0, result.stdout + result.stderr
    for label, value in fields.items():
        if label == "CARD_ID":
            continue  # substituted in multiple places; covered implicitly by the branch/title
        assert value in result.stdout, "field %s=%r missing byte-for-byte from composed prompt" % (label, value)


def test_compose_cli_refuses_an_unknown_field(tmp_path):
    proc = write_fields_card(tmp_path, "UNK-CLI-1", extra_lines="ZORP: not a real field\n")
    result = compose(proc, "lane-can-open-pr")
    assert result.returncode == 3, result.stdout + result.stderr
    assert result.stdout.startswith("REFUSED: unknown-field")


# --- Export-PrReviewEvidence.ps1: pinned repo, byte-exact exports, drift refusal, missing --
# required context reported as a failure (deliverable 9)

FAKE_GH_REPO_STRING = "layibabalola/MLV-App"

FAKE_GH_SCRIPT = (
    "$stateDir = $env:FAKE_GH_STATE_DIR\n"
    "Add-Content -LiteralPath (Join-Path $stateDir 'call_log.txt') -Value ($args -join '|')\n"
    "function Get-NextLine([string]$Path, [string]$CounterPath) {\n"
    "    $n = 0\n"
    "    if (Test-Path -LiteralPath $CounterPath) { $n = [int](Get-Content -LiteralPath $CounterPath -Raw) }\n"
    "    $n = $n + 1\n"
    "    Set-Content -LiteralPath $CounterPath -Value $n\n"
    "    $lines = @(Get-Content -LiteralPath $Path)\n"
    "    return $lines[$n - 1]\n"
    "}\n"
    "if ($args.Count -ge 2 -and $args[0] -eq 'pr' -and $args[1] -eq 'view') {\n"
    "    Write-Output (Get-NextLine (Join-Path $stateDir 'pr_view.jsonl') (Join-Path $stateDir 'pr_view.count'))\n"
    "    exit 0\n"
    "}\n"
    "if ($args.Count -ge 1 -and $args[0] -eq 'api' -and (($args -join ' ') -match 'branches/master/protection')) {\n"
    "    Write-Output (Get-NextLine (Join-Path $stateDir 'protection.jsonl') (Join-Path $stateDir 'protection.count'))\n"
    "    exit 0\n"
    "}\n"
    "if ($args.Count -ge 2 -and $args[0] -eq 'pr' -and $args[1] -eq 'checks') {\n"
    "    Get-Content -LiteralPath (Join-Path $stateDir 'checks.json') -Raw\n"
    "    exit 0\n"
    "}\n"
    "Write-Error ('fake-gh: unrecognized args: ' + ($args -join ' '))\n"
    "exit 1\n"
)


def make_fake_gh(dir_path, pr_view_sequence, protection_sequence, checks_payload):
    state = dir_path / "fake-gh-state"
    state.mkdir()
    (state / "pr_view.jsonl").write_text("\n".join(json.dumps(x) for x in pr_view_sequence), encoding="utf-8")
    (state / "protection.jsonl").write_text("\n".join(json.dumps(x) for x in protection_sequence), encoding="utf-8")
    (state / "checks.json").write_text(json.dumps(checks_payload), encoding="utf-8")
    (state / "call_log.txt").write_text("", encoding="utf-8")
    shim = dir_path / "fake-gh.ps1"
    shim.write_text(FAKE_GH_SCRIPT, encoding="ascii")
    return shim, state


def pr_evidence_repo(tmp_path, with_fork_remote=True):
    """A repo with base/head commits and (by default) a REAL `fork` remote pinned at the base
    commit, so a genuine `git fetch fork` (MAJOR 2, sol round-1 review) succeeds instead of
    silently no-op'ing against a remote that was never more than a manually poked ref.

    The remote is a bare snapshot taken right after the base commit, not an alias to `repo`
    itself: `repo`'s own branch keeps moving (the head commit below), and if `fork` pointed at
    that same path, fetching AFTER the head commit would walk the live branch forward and
    silently repoint `fork/master` at head_sha instead of base_sha.
    """
    repo = tmp_path / "repo"
    repo.mkdir()
    init_repo(repo)
    # Named explicitly, not left to `init.defaultBranch`: the exporter hardcodes `fork/master`,
    # so the fork remote's default branch must actually be called `master` for a real fetch to
    # populate `refs/remotes/fork/master`.
    subprocess.run(["git", "symbolic-ref", "HEAD", "refs/heads/master"], cwd=repo, check=True)
    subprocess.run(["git", "config", "user.email", "t@e.com"], cwd=repo, check=True)
    subprocess.run(["git", "config", "user.name", "T"], cwd=repo, check=True)
    (repo / "f.txt").write_text("base\n")
    subprocess.run(["git", "add", "-A"], cwd=repo, check=True)
    subprocess.run(["git", "commit", "-qm", "base"], cwd=repo, check=True)
    base_sha = git(repo, "rev-parse", "HEAD")

    if with_fork_remote:
        fork_remote = tmp_path / "fork_remote.git"
        subprocess.run(["git", "clone", "-q", "--bare", str(repo), str(fork_remote)], check=True)
        subprocess.run(["git", "remote", "add", "fork", str(fork_remote)], cwd=repo, check=True)
    # else: no remote named 'fork' at all -- `git fetch fork` fails deterministically (exit 128,
    # "'fork' does not appear to be a git repository"), the fetch-failure case MAJOR 2 covers.

    (repo / "f.txt").write_text("head\n")
    subprocess.run(["git", "commit", "-qam", "head commit"], cwd=repo, check=True)
    head_sha = git(repo, "rev-parse", "HEAD")
    return repo, base_sha, head_sha


def run_exporter(repo, run_dir, gh_shim, state_dir, pr_number=99):
    env = dict(os.environ)
    env["FAKE_GH_STATE_DIR"] = str(state_dir)
    args = [
        "pwsh.exe", "-NoLogo", "-NoProfile", "-NonInteractive", "-ExecutionPolicy", "Bypass",
        "-File", str(EXPORT_PR_EVIDENCE), "-PrNumber", str(pr_number), "-RunDir", str(run_dir),
        "-RepoRoot", str(repo), "-GhExe", str(gh_shim),
    ]
    return subprocess.run(args, text=True, capture_output=True, env=env)


def test_exporter_pins_the_repository_on_every_gh_call(tmp_path):
    """MAJOR 3 (sol round-1 review): the old assertion only checked that the repository STRING
    occurred SOMEWHERE in the joined argv line, which a repo name appearing in the wrong place
    (or coincidentally inside the API endpoint's path) would also satisfy -- it could false-green
    a `-R` flag that carried the wrong value, or a `gh api` call with no repo pin at all. This
    checks the actual argv TOKEN immediately after `-R` on every `pr view`/`pr checks` call, and
    the exact `repos/<repo>/branches/master/protection` endpoint string on the `gh api` call --
    not a substring hit against the whole line."""
    repo, base_sha, head_sha = pr_evidence_repo(tmp_path)
    run_dir = tmp_path / "run"
    shim, state = make_fake_gh(
        tmp_path,
        pr_view_sequence=[
            {"number": 99, "headRefOid": head_sha, "body": "b", "state": "OPEN"},
            {"number": 99, "headRefOid": head_sha, "body": "b", "state": "OPEN"},
        ],
        protection_sequence=[["build"], ["build"]],
        checks_payload=[{"name": "build", "state": "SUCCESS", "link": "x"}],
    )
    result = run_exporter(repo, run_dir, shim, state)
    assert result.returncode == 0, result.stdout + result.stderr
    calls = (state / "call_log.txt").read_text(encoding="utf-8").splitlines()
    assert calls, "gh was never invoked"
    saw_pr_call = False
    saw_api_call = False
    for line in calls:
        tokens = line.split("|")
        if tokens[:2] in (["pr", "view"], ["pr", "checks"]):
            saw_pr_call = True
            assert "-R" in tokens, "no -R flag on a pr view/checks call: %s" % line
            idx = tokens.index("-R")
            assert tokens[idx + 1] == FAKE_GH_REPO_STRING, (
                "the -R flag does not carry the exact pinned repo (got %r): %s" % (tokens[idx + 1], line)
            )
        if tokens[0] == "api":
            saw_api_call = True
            endpoint = tokens[1]
            assert endpoint == "repos/%s/branches/master/protection" % FAKE_GH_REPO_STRING, (
                "the gh api endpoint is not the exact pinned repo path: %s" % endpoint
            )
    assert saw_pr_call, "no pr view/checks call was observed"
    assert saw_api_call, "no gh api call was observed"


def test_exporter_repo_is_a_hardcoded_pin_not_a_caller_supplied_parameter(tmp_path):
    """MAJOR 3 (sol round-1 review): '-Repo is caller-overridable' was the actual defect behind
    the weak test above -- a parameter with a safe-looking default is still an argv path that can
    carry a different value in. The fix removes the parameter entirely rather than merely
    validating it, so this asserts on the source that no such parameter exists."""
    text = EXPORT_PR_EVIDENCE.read_text(encoding="utf-8")
    # Scoped to the SCRIPT's own param() block, not the internal Get-PrView/Get-RequiredContexts
    # helper functions further down, which legitimately take a $Repo parameter fed from the one
    # pinned script-scope value below -- that is an implementation detail, not a caller-facing
    # argv path. \b so this also does not false-positive on the unrelated [string]$RepoRoot.
    script_param_block = text[text.index("[CmdletBinding()]"):text.index("$ErrorActionPreference")]
    assert not re.search(r"\[string\]\$Repo\b", script_param_block), (
        "the repository is still a caller-settable parameter: %r" % script_param_block
    )
    assert "'layibabalola/MLV-App'" in text, "the pinned repo is no longer a literal in the source"


def extract_json_array_bytes(raw, key):
    """Slice out the raw bytes of a top-level JSON array value for `key` (e.g. b'"checks":
    [...]'), by bracket-balancing rather than reparsing -- so the comparison below is a real
    byte comparison of what was WRITTEN, not a reparse-and-recompare of what was MEANT."""
    marker = ('"%s":' % key).encode("ascii")
    idx = raw.index(marker)
    start = raw.index(b"[", idx)
    depth = 0
    i = start
    while i < len(raw):
        c = raw[i:i + 1]
        if c == b"[":
            depth += 1
        elif c == b"]":
            depth -= 1
            if depth == 0:
                return raw[start:i + 1]
        i += 1
    raise AssertionError("unbalanced [ ] while extracting %r from JSON" % key)


def test_exporter_writes_both_exports_byte_exact(tmp_path):
    """MINOR 1 (sol round-1 review): a test named 'byte exact' that only reparses both files with
    json.loads and compares fields never actually compares a single byte -- BOM, whitespace,
    key ordering or any other byte-level change would still pass. The exporter writes the SAME
    `checks` array into both pr-99-checks.json and pr-99-review.json from the same $checks value,
    so their serialized `checks` bytes must be IDENTICAL; that is asserted here as a real
    raw-bytes comparison, on top of (not instead of) the existing semantic checks."""
    repo, base_sha, head_sha = pr_evidence_repo(tmp_path)
    run_dir = tmp_path / "run"
    checks_payload = [{"name": "build", "state": "SUCCESS", "link": "x"}]
    shim, state = make_fake_gh(
        tmp_path,
        pr_view_sequence=[
            {"number": 99, "headRefOid": head_sha, "body": "the body", "state": "OPEN"},
            {"number": 99, "headRefOid": head_sha, "body": "the body", "state": "OPEN"},
        ],
        protection_sequence=[["build"], ["build"]],
        checks_payload=checks_payload,
    )
    result = run_exporter(repo, run_dir, shim, state)
    assert result.returncode == 0, result.stdout + result.stderr
    checks_raw = (run_dir / "pr-99-checks.json").read_bytes()
    review_raw = (run_dir / "pr-99-review.json").read_bytes()

    # the actual byte comparison the test's name claims
    checks_bytes = extract_json_array_bytes(checks_raw, "checks")
    review_checks_bytes = extract_json_array_bytes(review_raw, "checks")
    assert checks_bytes == review_checks_bytes, (
        "the 'checks' array is not byte-identical between pr-99-checks.json and "
        "pr-99-review.json: %r != %r" % (checks_bytes, review_checks_bytes)
    )

    checks_doc = json.loads(checks_raw.decode("utf-8"))
    review_doc = json.loads(review_raw.decode("utf-8"))
    assert checks_doc["checks"] == checks_payload
    assert review_doc["headRefOidBefore"] == head_sha
    assert review_doc["headRefOidAfter"] == head_sha
    assert review_doc["requiredContextsBefore"] == ["build"]
    assert review_doc["requiredContextsAfter"] == ["build"]
    assert review_doc["body"] == "the body"
    assert review_doc["checks"] == checks_payload
    assert review_doc["missingRequiredContexts"] == []


def test_exporter_refuses_when_git_fetch_fails(tmp_path):
    """MAJOR 2 (sol round-1 review): `git fetch fork` used to be piped to Out-Null with its exit
    code never checked, so a fetch failure was indistinguishable from success and the exporter
    proceeded to review whatever objects happened to already be local. `with_fork_remote=False`
    means no remote named 'fork' exists at all, so the fetch fails deterministically."""
    repo, base_sha, head_sha = pr_evidence_repo(tmp_path, with_fork_remote=False)
    run_dir = tmp_path / "run"
    shim, state = make_fake_gh(
        tmp_path,
        pr_view_sequence=[{"number": 99, "headRefOid": head_sha, "body": "b", "state": "OPEN"}],
        protection_sequence=[["build"]],
        checks_payload=[{"name": "build", "state": "SUCCESS", "link": "x"}],
    )
    result = run_exporter(repo, run_dir, shim, state)
    assert result.returncode != 0
    assert "REFUSED: git-fetch-failed" in result.stdout, result.stdout + result.stderr
    assert not (state / "call_log.txt").read_text(encoding="utf-8").strip(), (
        "gh must never be called after a failed fetch"
    )
    assert not run_dir.exists() or not any(run_dir.iterdir())


def test_exporter_refuses_on_an_empty_head_sha(tmp_path):
    """MAJOR 2 (sol round-1 review): the old `if ($sha) { cat-file -e ... }` SKIPPED the
    commit-existence check entirely for a falsy value, so an empty headRefOid silently passed
    with no object ever verified. An empty or short sha must be a REFUSAL, not a skipped check."""
    repo, base_sha, head_sha = pr_evidence_repo(tmp_path)
    run_dir = tmp_path / "run"
    shim, state = make_fake_gh(
        tmp_path,
        pr_view_sequence=[{"number": 99, "headRefOid": "", "body": "b", "state": "OPEN"}],
        protection_sequence=[["build"]],
        checks_payload=[{"name": "build", "state": "SUCCESS", "link": "x"}],
    )
    result = run_exporter(repo, run_dir, shim, state)
    assert result.returncode != 0
    assert "REFUSED: pr-sha-invalid field=head" in result.stdout, result.stdout + result.stderr
    assert not (run_dir / "pr-99-checks.json").exists()
    assert not (run_dir / "pr-99-review.json").exists()


def test_exporter_refuses_on_a_non_40_hex_head_sha(tmp_path):
    """MAJOR 2 (sol round-1 review): a short or otherwise malformed value is just as unbindable
    as an empty one -- both must fail the same explicit shape check before any cat-file call."""
    repo, base_sha, head_sha = pr_evidence_repo(tmp_path)
    run_dir = tmp_path / "run"
    shim, state = make_fake_gh(
        tmp_path,
        pr_view_sequence=[{"number": 99, "headRefOid": "not-a-sha", "body": "b", "state": "OPEN"}],
        protection_sequence=[["build"]],
        checks_payload=[{"name": "build", "state": "SUCCESS", "link": "x"}],
    )
    result = run_exporter(repo, run_dir, shim, state)
    assert result.returncode != 0
    assert "REFUSED: pr-sha-invalid field=head value=not-a-sha" in result.stdout, result.stdout + result.stderr


def test_exporter_refuses_on_head_drift(tmp_path):
    repo, base_sha, head_sha = pr_evidence_repo(tmp_path)
    run_dir = tmp_path / "run"
    shim, state = make_fake_gh(
        tmp_path,
        pr_view_sequence=[
            {"number": 99, "headRefOid": head_sha, "body": "b", "state": "OPEN"},
            {"number": 99, "headRefOid": "f" * 40, "body": "b", "state": "OPEN"},
        ],
        protection_sequence=[["build"], ["build"]],
        checks_payload=[{"name": "build", "state": "SUCCESS", "link": "x"}],
    )
    result = run_exporter(repo, run_dir, shim, state)
    assert result.returncode != 0
    assert "REFUSED: pr-head-drift" in result.stdout
    assert not (run_dir / "pr-99-checks.json").exists()
    assert not (run_dir / "pr-99-review.json").exists()


def test_exporter_refuses_on_required_context_drift(tmp_path):
    repo, base_sha, head_sha = pr_evidence_repo(tmp_path)
    run_dir = tmp_path / "run"
    shim, state = make_fake_gh(
        tmp_path,
        pr_view_sequence=[
            {"number": 99, "headRefOid": head_sha, "body": "b", "state": "OPEN"},
            {"number": 99, "headRefOid": head_sha, "body": "b", "state": "OPEN"},
        ],
        protection_sequence=[["build"], ["build", "extra-check"]],
        checks_payload=[{"name": "build", "state": "SUCCESS", "link": "x"}],
    )
    result = run_exporter(repo, run_dir, shim, state)
    assert result.returncode != 0
    assert "REFUSED: required-context-drift" in result.stdout


def test_exporter_reports_a_missing_required_context_as_a_failure(tmp_path):
    """'lint' is required but never ran (absent from checks entirely, not merely non-SUCCESS)
    -- it must be reported, never silently folded into either verdict."""
    repo, base_sha, head_sha = pr_evidence_repo(tmp_path)
    run_dir = tmp_path / "run"
    shim, state = make_fake_gh(
        tmp_path,
        pr_view_sequence=[
            {"number": 99, "headRefOid": head_sha, "body": "b", "state": "OPEN"},
            {"number": 99, "headRefOid": head_sha, "body": "b", "state": "OPEN"},
        ],
        protection_sequence=[["build", "lint"], ["build", "lint"]],
        checks_payload=[{"name": "build", "state": "SUCCESS", "link": "x"}],
    )
    result = run_exporter(repo, run_dir, shim, state)
    assert result.returncode == 0, result.stdout + result.stderr
    review_doc = json.loads((run_dir / "pr-99-review.json").read_text(encoding="utf-8"))
    assert review_doc["missingRequiredContexts"] == ["lint"]
    assert "EXPORT: missing-required-context lint" in result.stdout


# Product ratio admission: disposable history and actual dispatcher fixtures.
import json
import os
import re
import subprocess
from datetime import datetime, timezone
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]
RATIO_SOURCE_GUARD = ROOT / "tools" / "coordination" / "Test-ProductRatioGuard.ps1"
RATIO_SOURCE_DISPATCHER = ROOT / "tools" / "coordination" / "Invoke-Workstream.ps1"
RATIO_SOURCE_AS_OF = 2_000_000_000
RATIO_SOURCE_DAY = 86400


def ratio_source_run(*args, cwd=None, env=None):
    return subprocess.run(args, cwd=cwd, env=env, text=True, capture_output=True)


def ratio_source_git(repo, *args, env=None):
    result = ratio_source_run("git", *args, cwd=repo, env=env)
    assert result.returncode == 0, result.stderr
    return result.stdout.strip()


def ratio_source_init_repo(path):
    path.mkdir()
    ratio_source_git(path, "init", "-q", "-b", "master")
    ratio_source_git(path, "config", "user.name", "Ratio Test")
    ratio_source_git(path, "config", "user.email", "ratio@example.invalid")
    return path


def ratio_source_commit(repo, subject, epoch, paths, author_epoch=None):
    for name, text in paths.items():
        target = repo / name
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text(text, encoding="utf-8")
    ratio_source_git(repo, "add", "-A")
    env = dict(os.environ)
    env["GIT_COMMITTER_DATE"] = f"@{epoch} +0000"
    env["GIT_AUTHOR_DATE"] = f"@{author_epoch if author_epoch is not None else epoch} +0000"
    ratio_source_git(repo, "commit", "-qm", subject, env=env)
    return ratio_source_git(repo, "rev-parse", "HEAD")


def ratio_source_invoke_guard(repo, *, as_of=RATIO_SOURCE_AS_OF, reservations=None, legacy=None, ref="master"):
    command = [
        "pwsh", "-NoProfile", "-NonInteractive", "-ExecutionPolicy", "Bypass",
        "-File", str(RATIO_SOURCE_GUARD), "-RepoRoot", str(repo), "-SourceRef", ref,
        "-AsOfEpoch", str(as_of),
    ]
    if reservations is not None:
        command += ["-ReservationsPath", str(reservations)]
    if legacy is not None:
        command += ["-LegacyDispatchPath", str(legacy)]
    result = ratio_source_run(*command)
    payload = json.loads(result.stdout)
    return result, payload


def ratio_source_populate(repo, count, product_indices, *, start=RATIO_SOURCE_AS_OF - RATIO_SOURCE_DAY):
    for index in range(count):
        path = f"src/p{index}.txt" if index in product_indices else f"docs/d{index}.txt"
        ratio_source_commit(repo, f"commit {index}", start + index, {path: str(index)})


@pytest.mark.parametrize("count,products,expected", [(10, set(range(6)), 0.60), (20, {3}, 0.05)])
def test_ratio_source_product_share_populations(tmp_path, count, products, expected):
    repo = ratio_source_init_repo(tmp_path / "repo")
    ratio_source_populate(repo, count, products)
    _, payload = ratio_source_invoke_guard(repo)
    assert payload["commitPopulation"] == count
    assert payload["productCommitCount"] == len(products)
    assert payload["productShare7d"] == pytest.approx(expected)
    assert payload["verdict"] == "RED"


def test_ratio_source_boundary_committer_date_docs_merge_and_mixed_commit(tmp_path):
    repo = ratio_source_init_repo(tmp_path / "repo")
    ratio_source_commit(repo, "included boundary", RATIO_SOURCE_AS_OF - 7 * RATIO_SOURCE_DAY, {"src/a": "a"}, author_epoch=RATIO_SOURCE_AS_OF - 30 * RATIO_SOURCE_DAY)
    ratio_source_commit(repo, "excluded old", RATIO_SOURCE_AS_OF - 7 * RATIO_SOURCE_DAY - 1, {"src/old": "old"}, author_epoch=RATIO_SOURCE_AS_OF)
    ratio_source_commit(repo, "docs denominator", RATIO_SOURCE_AS_OF - 10, {"docs/readme": "d"})
    ratio_source_commit(repo, "mixed once", RATIO_SOURCE_AS_OF - 9, {"src/m": "m", "docs/m": "m"})
    ratio_source_git(repo, "checkout", "-qb", "side", "HEAD~1")
    ratio_source_commit(repo, "side", RATIO_SOURCE_AS_OF - 8, {"platform/side": "s"})
    ratio_source_git(repo, "checkout", "-q", "master")
    env = dict(os.environ, GIT_COMMITTER_DATE=f"@{RATIO_SOURCE_AS_OF - 7} +0000", GIT_AUTHOR_DATE=f"@{RATIO_SOURCE_AS_OF - 7} +0000")
    ratio_source_git(repo, "merge", "--no-ff", "-qm", "ordinary merge", "side", env=env)
    _, payload = ratio_source_invoke_guard(repo)
    assert payload["commitPopulation"] == 4
    assert payload["productCommitCount"] == 3


def test_ratio_source_future_commit_is_excluded(tmp_path):
    repo = ratio_source_init_repo(tmp_path / "repo")
    ratio_source_commit(repo, "present", RATIO_SOURCE_AS_OF, {"src/a": "a"})
    ratio_source_commit(repo, "future", RATIO_SOURCE_AS_OF + 1, {"src/b": "b"})
    _, payload = ratio_source_invoke_guard(repo)
    assert payload["commitPopulation"] == 1
    assert payload["productCommitCount"] == 1


def test_ratio_source_github_merge_squash_dedup_and_batch_count_once(tmp_path):
    repo = ratio_source_init_repo(tmp_path / "repo")
    ratio_source_commit(repo, "base", RATIO_SOURCE_AS_OF - 100, {"docs/base": "base"})
    ratio_source_git(repo, "checkout", "-qb", "feature")
    ratio_source_commit(repo, "part one", RATIO_SOURCE_AS_OF - 90, {"src/a": "a"})
    ratio_source_commit(repo, "part two", RATIO_SOURCE_AS_OF - 80, {"src/b": "b"})
    ratio_source_git(repo, "checkout", "-q", "master")
    env = dict(os.environ, GIT_COMMITTER_DATE=f"@{RATIO_SOURCE_AS_OF - 70} +0000", GIT_AUTHOR_DATE=f"@{RATIO_SOURCE_AS_OF - 70} +0000")
    ratio_source_git(repo, "merge", "--no-ff", "-qm", "Merge pull request #91 from example/batch", "feature", env=env)
    ratio_source_commit(repo, "squashed product (#92)", RATIO_SOURCE_AS_OF - 60, {"platform/c": "c"})
    _, payload = ratio_source_invoke_guard(repo)
    assert payload["recognizedProductPrIds"] == [91, 92]
    assert payload["recognizedProductPrCount"] == 2


def test_ratio_source_unknown_product_landing_nulls_rate(tmp_path):
    repo = ratio_source_init_repo(tmp_path / "repo")
    ratio_source_commit(repo, "direct product landing", RATIO_SOURCE_AS_OF - 2, {"src/a": "a"})
    reservations = tmp_path / "reservations.jsonl"
    reservations.write_text(json.dumps({"state": "reserved", "recordedUtc": datetime.fromtimestamp(RATIO_SOURCE_AS_OF - 1, timezone.utc).isoformat()}) + "\n")
    _, payload = ratio_source_invoke_guard(repo, reservations=reservations)
    assert payload["unrecognizedProductLandings"]
    assert payload["dispatchesPerLandedProductPr7dLowerBound"] is None
    assert "UNAVAILABLE_LANDING_PROVENANCE" in payload["reasons"]


def test_ratio_source_missing_and_malformed_observations_are_partial_red(tmp_path):
    repo = ratio_source_init_repo(tmp_path / "repo")
    ratio_source_commit(repo, "docs", RATIO_SOURCE_AS_OF - 1, {"docs/a": "a"})
    missing = tmp_path / "missing"
    _, absent = ratio_source_invoke_guard(repo, reservations=missing, legacy=missing)
    assert absent["dispatchCoverage"] == "PARTIAL"
    assert absent["dispatchesObserved"] == 0
    assert absent["verdict"] == "RED"
    bad = tmp_path / "bad.jsonl"
    bad.write_text("not-json\n")
    _, malformed = ratio_source_invoke_guard(repo, reservations=bad, legacy=missing)
    assert malformed["malformedDispatchRows"] == 1
    assert malformed["verdict"] == "RED"
    assert "ERROR_DISPATCH_EVIDENCE_MALFORMED" in malformed["reasons"]


def test_ratio_source_reservations_take_precedence_and_are_not_combined_with_legacy(tmp_path):
    repo = ratio_source_init_repo(tmp_path / "repo")
    ratio_source_commit(repo, "docs", RATIO_SOURCE_AS_OF - 1, {"docs/a": "a"})
    stamp = datetime.fromtimestamp(RATIO_SOURCE_AS_OF - 1, timezone.utc).isoformat()
    reservations = tmp_path / "reservations.jsonl"
    legacy = tmp_path / "legacy.jsonl"
    reservations.write_text(json.dumps({"state": "reserved", "recordedUtc": stamp}) + "\n")
    legacy.write_text("\n".join(json.dumps({"dispatchedUtc": stamp}) for _ in range(4)) + "\n")
    _, payload = ratio_source_invoke_guard(repo, reservations=reservations, legacy=legacy)
    assert payload["dispatchEvidenceSource"] == "dispatch-reservations"
    assert payload["dispatchesObserved"] == 1


def test_ratio_source_invalid_ref_and_invalid_repo_are_error_exit_3(tmp_path):
    repo = ratio_source_init_repo(tmp_path / "repo")
    ratio_source_commit(repo, "base", RATIO_SOURCE_AS_OF - 1, {"docs/a": "a"})
    result, payload = ratio_source_invoke_guard(repo, ref="missing")
    assert result.returncode == 3
    assert payload["verdict"] == "ERROR"
    assert payload["errorCode"] == "ERROR_REF_UNRESOLVED"
    invalid = tmp_path / "not-repo"
    invalid.mkdir()
    result, payload = ratio_source_invoke_guard(invalid)
    assert result.returncode == 3
    assert payload["verdict"] == "ERROR"


def ratio_source_extract_decision_function(tmp_path):
    source = RATIO_SOURCE_GUARD.read_text(encoding="utf-8")
    match = re.search(r"(?ms)^function New-Decision \{.*?^\}", source)
    assert match
    script = tmp_path / "decision.ps1"
    script.write_text("param([double]$Rate)\n" + match.group(0) + "\nNew-Decision -ProductShare 0.6 -Coverage COMPLETE -EvidenceAvailable $true -ProvenanceComplete $true -HasProductLandings $true -DispatchRate $Rate | ConvertTo-Json -Compress\n", encoding="utf-8")
    return script


def test_ratio_source_synthetic_complete_decision_green_at_four_and_red_above(tmp_path):
    script = ratio_source_extract_decision_function(tmp_path)
    green = ratio_source_run("pwsh", "-NoProfile", "-File", str(script), "-Rate", "4")
    red = ratio_source_run("pwsh", "-NoProfile", "-File", str(script), "-Rate", "4.01")
    assert json.loads(green.stdout)["verdict"] == "GREEN"
    assert json.loads(red.stdout)["verdict"] == "RED"
    assert "RED_DISPATCH_RATE" in json.loads(red.stdout)["reasons"]


def test_ratio_source_caller_has_helper_at_both_post_kill_pre_reservation_seams():
    source = RATIO_SOURCE_DISPATCHER.read_text(encoding="utf-8")
    assert source.count("function Test-RatioDispatchPermission") == 1
    assert source.count("$ratioExit = Test-RatioDispatchPermission -Kind $cardKind") == 2
    for match in re.finditer(r"\$ratioExit = Test-RatioDispatchPermission -Kind \$cardKind", source):
        prefix = source[:match.start()]
        suffix = source[match.end():]
        assert prefix.rfind("if (Test-KillSwitchArmed)") > prefix.rfind("if ($DryRun)")
        assert suffix.find("Write-DispatchReservation") >= 0
        assert suffix.find("Write-DispatchReservation") < suffix.find("& pwsh")


def ratio_source_make_fake_guard(path, verdict, reasons=None, rate=None, malformed=False):
    rate = 4 if rate is None else rate
    payload = ratio_guard_payload(
        dispatchesPerLandedProductPr7dLowerBound=rate, dispatchesObserved=int(rate),
        dispatchCoverage="COMPLETE" if verdict == "GREEN" else "PARTIAL",
        verdict=verdict, reasons=reasons or [],
        errorCode="ERROR_GIT_HISTORY" if verdict == "ERROR" else None,
    )
    body = "Write-Output 'not-json'" if malformed else "Write-Output '" + json.dumps(payload, separators=(",", ":")) + "'"
    path.write_text(body + "\nexit " + ("3" if verdict == "ERROR" else "0") + "\n", encoding="utf-8")


def test_ratio_source_caller_helper_routing_and_malformed_output(tmp_path):
    source = RATIO_SOURCE_DISPATCHER.read_text(encoding="utf-8")
    match = re.search(r"(?ms)^function Test-RatioDispatchPermission \{.*?^\}", source)
    assert match
    harness = tmp_path / "harness.ps1"
    guard = tmp_path / "guard.ps1"
    harness.write_text("param($Guard,$Kind)\n$ProductRatioGuard=$Guard\n$RepoRoot='x'\n" + match.group(0) + "\nexit (Test-RatioDispatchPermission -Kind $Kind)\n", encoding="utf-8")
    for kind, expected in [("product", 0), ("playback", 0), ("factory", 6), ("", 6)]:
        ratio_source_make_fake_guard(guard, "RED", ["RED_DISPATCH_COVERAGE_PARTIAL"])
        result = ratio_source_run("pwsh", "-NoProfile", "-File", str(harness), "-Guard", str(guard), "-Kind", kind)
        assert result.returncode == expected
    ratio_source_make_fake_guard(guard, "ERROR", ["ERROR_GIT_HISTORY"])
    assert ratio_source_run("pwsh", "-NoProfile", "-File", str(harness), "-Guard", str(guard), "-Kind", "product").returncode == 3
    ratio_source_make_fake_guard(guard, "GREEN")
    assert ratio_source_run("pwsh", "-NoProfile", "-File", str(harness), "-Guard", str(guard), "-Kind", "factory").returncode == 0
    ratio_source_make_fake_guard(guard, "RED", malformed=True)
    assert ratio_source_run("pwsh", "-NoProfile", "-File", str(harness), "-Guard", str(guard), "-Kind", "product").returncode == 3


def test_ratio_source_dry_run_precedes_reservation_and_launch_in_both_paths():
    source = RATIO_SOURCE_DISPATCHER.read_text(encoding="utf-8")
    read_only = source[source.index("if (-not $AllowEdits)"):source.index("# ==================================================================== editing dispatch")]
    editing = source[source.index("# ==================================================================== editing dispatch"):]
    assert read_only.index("if ($DryRun)") < read_only.index("Write-DispatchReservation") < read_only.index("& pwsh")
    assert editing.index("if ($DryRun)") < editing.index("Write-DispatchReservation") < editing.index("& pwsh")


def test_ratio_source_dispatch_timestamp_boundaries_preserve_explicit_offsets(tmp_path):
    repo=ratio_source_init_repo(tmp_path/'repo')
    ratio_source_commit(repo,'base',RATIO_SOURCE_AS_OF-2,{'docs/base':'base'})
    records=[]
    for epoch in [RATIO_SOURCE_AS_OF-7*RATIO_SOURCE_DAY-1,RATIO_SOURCE_AS_OF-7*RATIO_SOURCE_DAY,RATIO_SOURCE_AS_OF,RATIO_SOURCE_AS_OF+1]:
        records.append({'state':'reserved','recordedUtc':datetime.fromtimestamp(epoch,timezone.utc).isoformat()})
    path=tmp_path/'reservations.jsonl'
    path.write_text('\n'.join(json.dumps(x) for x in records)+'\n')
    result,payload=ratio_source_invoke_guard(repo,reservations=path)
    assert result.returncode==0,result.stderr
    assert payload['dispatchesObserved']==2 and payload['malformedDispatchRows']==0


def test_ratio_source_old_history_does_not_spawn_a_process_per_commit(tmp_path):
    repo=ratio_source_init_repo(tmp_path/'repo')
    ratio_source_populate(repo,30,set(),start=RATIO_SOURCE_AS_OF-30*RATIO_SOURCE_DAY)
    ratio_source_commit(repo,'current',RATIO_SOURCE_AS_OF,{'src/current':'current'})
    trace=tmp_path/'git-trace.jsonl'
    env=dict(os.environ,GIT_TRACE2_EVENT=str(trace))
    result=ratio_source_run('pwsh','-NoProfile','-File',str(RATIO_SOURCE_GUARD),'-RepoRoot',str(repo),'-SourceRef','master','-AsOfEpoch',str(RATIO_SOURCE_AS_OF),env=env)
    assert result.returncode==0,result.stderr
    assert json.loads(result.stdout)['commitPopulation']==1
    starts=[json.loads(line) for line in trace.read_text().splitlines() if json.loads(line).get('event')=='start']
    assert len(starts)<=8,[(x.get('argv')) for x in starts]



# Prefix every addition to avoid collisions with the existing guardrail helpers.
RATIO_GUARD = ROOT / "tools" / "coordination" / "Test-ProductRatioGuard.ps1"
RATIO_WORKSTREAM = ROOT / "tools" / "coordination" / "Invoke-Workstream.ps1"
RATIO_LOOP = ROOT / "tools" / "coordination" / "Invoke-WorkstreamLoop.ps1"


def ratio_guard_payload(**overrides):
    payload = {
        "schema": "mlv-app/product-ratio-guard/v1",
        "asOfUtc": "2033-05-18T03:33:20.0000000Z",
        "windowStartUtc": "2033-05-11T03:33:20.0000000Z",
        "windowEndUtc": "2033-05-18T03:33:20.0000000Z",
        "sourceRef": "fork/master",
        "sourceSha": "1" * 40,
        "commitPopulation": 10,
        "productCommitCount": 6,
        "productShare7d": 0.6,
        "productShareThreshold": 0.5,
        "recognizedProductPrCount": 1,
        "recognizedProductPrIds": [101],
        "unrecognizedProductLandings": [],
        "landingProvenanceComplete": True,
        "hasProductLandings": True,
        "dispatchEvidenceSource": "dispatch-reservations",
        "dispatchCoverage": "PARTIAL",
        "dispatchEvidenceAvailable": True,
        "dispatchesObserved": 5,
        "malformedDispatchRows": 0,
        "dispatchesPerLandedProductPr7dLowerBound": 5.0,
        "dispatchRateThreshold": 4.0,
        "verdict": "RED",
        "reasons": ["RED_DISPATCH_COVERAGE_PARTIAL", "RED_DISPATCH_RATE"],
        "errorCode": None,
    }
    payload.update(overrides)
    return payload


def ratio_write_guard(path, payload, exit_code=0, raw=None):
    text = raw if raw is not None else json.dumps(payload, separators=(",", ":"))
    path.write_text("Write-Output '" + text.replace("'", "''") + "'\nexit %d\n" % exit_code, encoding="utf-8")


def ratio_extract_helper(tmp_path):
    source = RATIO_WORKSTREAM.read_text(encoding="utf-8")
    match = re.search(r"(?ms)^function Test-RatioDispatchPermission \{.*?^\}", source)
    assert match
    harness = tmp_path / "ratio-helper.ps1"
    harness.write_text("param($Guard,$Kind)\n$ProductRatioGuard=$Guard\n$RepoRoot='x'\n" + match.group(0) + "\nexit (Test-RatioDispatchPermission -Kind $Kind)\n", encoding="utf-8")
    return harness


def ratio_run_helper(tmp_path, payload, kind="factory", exit_code=0, raw=None):
    helper = ratio_extract_helper(tmp_path)
    guard = tmp_path / "ratio-fake-guard.ps1"
    ratio_write_guard(guard, payload, exit_code=exit_code, raw=raw)
    return subprocess.run(["pwsh", "-NoProfile", "-NonInteractive", "-File", str(helper), "-Guard", str(guard), "-Kind", kind], text=True, capture_output=True)


def test_ratio_dispatch_read_failure_is_named_partial_red_and_never_uses_legacy(tmp_path):
    repo = ratio_source_init_repo(tmp_path / "repo")
    ratio_source_commit(repo, "docs", 2_000_000_000 - 2, {"docs/a": "a"})
    ratio_source_commit(repo, "fix product (#101)", 2_000_000_000 - 1, {"src/a": "a"})
    reservations = tmp_path / "reservations"
    reservations.mkdir()
    legacy = tmp_path / "legacy.jsonl"
    legacy.write_text(json.dumps({"dispatchedUtc": "2033-05-18T03:33:19+00:00"}) + "\n", encoding="utf-8")
    command = ["pwsh", "-NoProfile", "-NonInteractive", "-File", str(RATIO_GUARD), "-RepoRoot", str(repo), "-SourceRef", "master", "-AsOfEpoch", "2000000000", "-ReservationsPath", str(reservations), "-LegacyDispatchPath", str(legacy)]
    result = subprocess.run(command, text=True, capture_output=True)
    assert result.returncode == 0, result.stderr
    payload = json.loads(result.stdout)
    assert payload["dispatchEvidenceSource"] == "unavailable"
    assert payload["dispatchCoverage"] == "PARTIAL"
    assert payload["dispatchEvidenceAvailable"] is False
    assert payload["dispatchesObserved"] == 0
    assert payload["recognizedProductPrIds"] == [101]
    assert payload["dispatchesPerLandedProductPr7dLowerBound"] is None
    assert payload["malformedDispatchRows"] == 0
    assert payload["verdict"] == "RED"
    assert "UNAVAILABLE_DISPATCH_EVIDENCE" in payload["reasons"]


@pytest.mark.parametrize("rate, expected", [(0.0, 3), (None, 0)])
def test_ratio_unavailable_evidence_requires_null_rate_for_product_admission(tmp_path, rate, expected):
    payload = ratio_guard_payload(dispatchEvidenceSource="unavailable",
        dispatchEvidenceAvailable=False, dispatchesObserved=0,
        dispatchesPerLandedProductPr7dLowerBound=rate,
        reasons=["UNAVAILABLE_DISPATCH_EVIDENCE", "RED_DISPATCH_COVERAGE_PARTIAL"])
    assert ratio_run_helper(tmp_path, payload, kind="product").returncode == expected


def test_ratio_empty_window_has_null_share_and_valid_red_is_typed_allowed(tmp_path):
    payload = ratio_guard_payload(commitPopulation=0, productCommitCount=0, productShare7d=None, recognizedProductPrCount=0, recognizedProductPrIds=[], hasProductLandings=False, dispatchesObserved=0, dispatchesPerLandedProductPr7dLowerBound=None, reasons=["RED_PRODUCT_SHARE", "RED_DISPATCH_COVERAGE_PARTIAL", "NO_PRODUCT_LANDINGS"])
    assert ratio_run_helper(tmp_path, payload, kind="product").returncode == 0
    assert ratio_run_helper(tmp_path, payload, kind="playback").returncode == 0
    assert ratio_run_helper(tmp_path, payload, kind="factory").returncode == 6


@pytest.mark.parametrize("mutation", [
    {"dispatchCoverage": "MAYBE"},
    {"verdict": "AMBER"},
    {"commitPopulation": "10"},
    {"productShare7d": float("nan")},
    {"productShare7d": 1.2},
    {"productCommitCount": 7},
    {"recognizedProductPrCount": 2},
    {"landingProvenanceComplete": False},
    {"dispatchEvidenceAvailable": False},
    {"unexpected": True},
    {"productShare7d": "0.6"},
    {"productShare7d": None, "productCommitCount": 0},
    {"productShareThreshold": "0.5"},
    {"dispatchRateThreshold": None},
    {"dispatchesPerLandedProductPr7dLowerBound": "5"},
    {"sourceSha": "not-a-sha"},
])
def test_ratio_unknown_types_ranges_and_count_contradictions_fail_closed(tmp_path, mutation):
    payload = ratio_guard_payload()
    payload.update(mutation)
    assert ratio_run_helper(tmp_path, payload, kind="product").returncode == 3


def test_ratio_contradictory_green_fails_closed_and_complete_green_passes(tmp_path):
    green = ratio_guard_payload(dispatchCoverage="COMPLETE", dispatchesObserved=4, dispatchesPerLandedProductPr7dLowerBound=4.0, verdict="GREEN", reasons=[])
    assert ratio_run_helper(tmp_path, green, kind="factory").returncode == 0
    partial = dict(green, dispatchCoverage="PARTIAL")
    assert ratio_run_helper(tmp_path, partial, kind="factory").returncode == 3
    missing_provenance = dict(green, landingProvenanceComplete=False)
    assert ratio_run_helper(tmp_path, missing_provenance, kind="factory").returncode == 3


def test_ratio_error_exit_three_blocks_every_kind(tmp_path):
    payload = ratio_guard_payload(sourceSha="", commitPopulation=0, productCommitCount=0, productShare7d=None, recognizedProductPrCount=0, recognizedProductPrIds=[], landingProvenanceComplete=False, hasProductLandings=False, dispatchEvidenceSource="unavailable", dispatchEvidenceAvailable=False, dispatchesObserved=0, dispatchesPerLandedProductPr7dLowerBound=None, verdict="ERROR", reasons=["ERROR_GIT_HISTORY"], errorCode="ERROR_GIT_HISTORY")
    for kind in ("factory", "product", "playback", ""):
        assert ratio_run_helper(tmp_path, payload, kind=kind, exit_code=3).returncode == 3


def ratio_full_dispatch_board(tmp_path, guard_payload, use_real_guard=False):
    dual, _ = editing_board(tmp_path, with_lane_shim=True)
    guard = tmp_path / "fake-product-ratio-guard.ps1"
    ratio_write_guard(guard, guard_payload)
    # The dispatcher resolves the guard beside itself; use a copied dispatcher plus fake guard,
    # while retaining fake lane/wrapper fixtures and a synthetic queue.
    tool_dir = tmp_path / "tools" / "coordination"
    tool_dir.mkdir(parents=True, exist_ok=True)
    shutil.copy2(RATIO_WORKSTREAM, tool_dir / "Invoke-Workstream.ps1")
    shutil.copy2(RATIO_GUARD if use_real_guard else guard, tool_dir / "Test-ProductRatioGuard.ps1")
    for dependency in ("landing-probe.ps1", "compose-lane-prompt-core.ps1", "Retire-LaneWorktree.ps1"):
        shutil.copy2(RATIO_WORKSTREAM.parent / dependency, tool_dir / dependency)
    (tool_dir / "Invoke-Lane.ps1").write_text("param($Lane,$PromptFile,$Card,$RunDir,$TimeoutSec)\nWrite-Output 'RATIO_FAKE_LANE'\nexit 0\n", encoding="ascii")
    (tool_dir / "Export-PrReviewEvidence.ps1").write_text("exit 0\n", encoding="ascii")
    return dual, tool_dir / "Invoke-Workstream.ps1"


@pytest.mark.parametrize("use_real_guard", [False, True])
def test_ratio_full_dispatcher_read_only_and_editing_apply_valid_red(tmp_path, use_real_guard):
    red = ratio_guard_payload()
    dual, dispatcher = ratio_full_dispatch_board(tmp_path, red, use_real_guard=use_real_guard)
    procedure = write_fields_card(dual / "prompts" / "v2", "RATIO-EDIT-PRODUCT")
    factory_procedure = write_fields_card(dual / "prompts" / "v2", "RATIO-EDIT-FACTORY")
    queue = tmp_path / "queue.json"
    items = [
        {"id": "RATIO-READ-PRODUCT", "state": "queued", "track": "product", "kind": "product", "owner": "sonnet", "priority": 1},
        {"id": "RATIO-READ-FACTORY", "state": "queued", "track": "factory", "kind": "factory", "owner": "sonnet", "priority": 1},
        {"id": "RATIO-EDIT-PRODUCT", "state": "queued", "track": "product", "kind": "product", "owner": "sonnet", "priority": 1, "procedure": ".claude-state/coordination/dual-lane/prompts/v2/fields-RATIO-EDIT-PRODUCT.md", "procedureSha256": sha256_of(procedure)},
        {"id": "RATIO-EDIT-FACTORY", "state": "queued", "track": "factory", "kind": "factory", "owner": "sonnet", "priority": 1, "procedure": ".claude-state/coordination/dual-lane/prompts/v2/fields-RATIO-EDIT-FACTORY.md", "procedureSha256": sha256_of(factory_procedure)},
    ]
    queue.write_text(json.dumps({"schema": "test", "items": items}), encoding="utf-8")
    queue_before = queue.read_bytes()
    base = ["pwsh", "-NoProfile", "-NonInteractive", "-File", str(dispatcher), "-QueuePath", str(queue), "-NoLandingProbe", "-TimeoutSec", "1"]
    env = editing_dispatch_env(tmp_path)
    read_product = subprocess.run(base + ["-CardId", "RATIO-READ-PRODUCT"], text=True, capture_output=True, env=env)
    read_factory = subprocess.run(base + ["-CardId", "RATIO-READ-FACTORY"], text=True, capture_output=True, env=env)
    try:
        edit_product = subprocess.run(base + ["-CardId", "RATIO-EDIT-PRODUCT", "-AllowEdits"], text=True, capture_output=True, env=env)
        edit_factory = subprocess.run(base + ["-CardId", "RATIO-EDIT-FACTORY", "-AllowEdits"], text=True, capture_output=True, env=env)
        assert read_product.returncode == 0 and "RATIO_FAKE_LANE" in read_product.stdout, read_product.stdout + read_product.stderr
        assert read_factory.returncode == 6 and "REFUSED ratio-guard-red kind=factory" in read_factory.stdout
        assert edit_product.returncode == 0 and "SHIM: lane=sonnet card=RATIO-EDIT-PRODUCT" in edit_product.stdout, edit_product.stdout + edit_product.stderr
        assert edit_factory.returncode == 6 and "REFUSED ratio-guard-red kind=factory" in edit_factory.stdout, edit_factory.stdout + edit_factory.stderr
        assert queue.read_bytes() == queue_before
        reserved = [json.loads(line) for line in (dual / "receipts" / "dispatch-reservations.jsonl").read_text().splitlines() if json.loads(line)["state"] == "reserved"]
        assert {row["card"] for row in reserved} == {"RATIO-READ-PRODUCT", "RATIO-EDIT-PRODUCT"}
        assert len(reserved) == 2
    finally:
        cleanup_lane_worktree(tmp_path, "RATIO-EDIT-PRODUCT")
        cleanup_lane_worktree(tmp_path, "RATIO-EDIT-FACTORY")


def test_ratio_actual_loop_foreach_body_skips_factory_exit_six_then_counts_product_zero(tmp_path):
    extractor = tmp_path / "extract-ratio-loop.ps1"
    extractor.write_text("param($Source)\n$tokens=$null;$errors=$null\n"
        "$ast=[System.Management.Automation.Language.Parser]::ParseFile($Source,[ref]$tokens,[ref]$errors)\n"
        "if($errors.Count){throw 'loop parse failed'}\n"
        "$loops=@($ast.FindAll({param($node) $node -is [System.Management.Automation.Language.ForEachStatementAst] -and $node.Variable.VariablePath.UserPath -eq 'track'},$true))\n"
        "if($loops.Count -ne 1){throw 'expected one track loop'}\n$loops[0].Extent.Text\n", encoding="utf-8")
    extracted = subprocess.run(["pwsh", "-NoProfile", "-NonInteractive", "-File", str(extractor), "-Source", str(RATIO_LOOP)], text=True, capture_output=True)
    assert extracted.returncode == 0, extracted.stderr
    body = extracted.stdout
    dispatcher = tmp_path / "ratio-fake-dispatcher.ps1"
    dispatcher.write_text("param($Track)\nif($Track -eq 'factory'){ Write-Output 'WORKSTREAM: REFUSED ratio-guard-red kind=factory'; exit 6 }; Write-Output ('WORKSTREAM: track=' + $Track + ' card=RATIO-NEXT'); exit 0\n", encoding="ascii")
    harness = tmp_path / "ratio-loop-body.ps1"
    harness.write_text("$Tracks=@('factory','product')\n$dispatched=@()\n$skipped=@()\n$MaxDispatchesPerCycle=1\n$DailyBudget=9\n$spentToday=0\n$Dispatcher='" + str(dispatcher).replace("'", "''") + "'\n$TimeoutSec=1\n$StaleHours=1\n$Lane=''\n$AllowEdits=$false\n$DryRun=$false\n" + body + "\n[ordered]@{dispatched=$dispatched;skipped=$skipped}|ConvertTo-Json -Depth 6 -Compress\n", encoding="utf-8")
    result = subprocess.run(["pwsh", "-NoProfile", "-NonInteractive", "-File", str(harness)], text=True, capture_output=True)
    assert result.returncode == 0, result.stderr
    payload = json.loads(result.stdout.splitlines()[-1])
    assert [row["track"] for row in payload["dispatched"]] == ["product"]
    assert payload["skipped"][0]["track"] == "factory"
    assert payload["skipped"][0]["reason"] == "exit-6"


# --- the refund rule: a dispatch that FAILED and spent NOTHING is refunded ------------------
# Measured 2026-09-05: two of twelve dispatches hit HTTP 429 "You've hit your session limit",
# and the loop then halted the rest of the UTC day at "daily budget exhausted (12/12)" having
# done ten units of work. The budget caps CONSUMPTION, not success -- so the test is
# costUsd == 0, NOT "it failed": CITE-TXN-1 failed after burning USD 2.24 and must still be
# charged, or the loop gets to spend the same money twice. And costReported must be TRUE,
# because an unreported cost read as free is the permissive branch of a fail-open.

LOOP_SCRIPT = ROOT / "tools" / "coordination" / "Invoke-WorkstreamLoop.ps1"


def test_the_dispatch_log_records_the_lane_spend_beside_its_exit_code():
    # Keep operator-visible spend in the legacy log. Budget authority is the
    # reservation ledger plus the bound receipt, not these copied log fields.
    body = WORKSTREAM.read_text(encoding="utf-8")
    assert "laneCostUsd" in body, "dispatch-log row does not carry the lane cost"
    assert "laneCostReported" in body, "dispatch-log row does not carry cost reportedness"


def test_an_unreadable_receipt_leaves_the_cost_UNREPORTED_not_zero():
    # Failing to read a cost must never look like a zero cost.
    body = WORKSTREAM.read_text(encoding="utf-8")
    assert "$laneCostReported = $false" in body, "cost reportedness does not default to false"


def test_the_refund_requires_all_three_conditions(tmp_path):
    rows = [_row('ZERO', 1, 0), _row('SUCCESS', 0, 0),
            _row('UNKNOWN', 1, 0, reported=False), _row('PAID', 1, 0.01)]
    assert _budget_of(tmp_path, rows) == 3


def test_the_refund_is_announced_rather_than_silent():
    # A budget that silently un-spends itself is indistinguishable from a miscount.
    assert "LOOP: refunded dispatch" in LOOP_SCRIPT.read_text(encoding="utf-8")


def _run_reservation_budget(tmp_path, rows):
    ledger = tmp_path / 'dispatch-reservations.jsonl'
    ledger.write_text('\n'.join(json.dumps(row) for row in rows) + '\n', encoding='utf-8')
    harness = tmp_path / 'run-budget.ps1'
    harness.write_text('''param($Source,$Ledger,$Board)
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
$tokens=$null;$errors=$null
$ast=[Management.Automation.Language.Parser]::ParseFile($Source,[ref]$tokens,[ref]$errors)
if($errors.Count){throw 'source parse failed'}
$functions=@($ast.FindAll({param($n) $n -is [Management.Automation.Language.FunctionDefinitionAst] -and $n.Name -eq 'Get-ReservationBudget'},$true))
if($functions.Count -ne 1){throw 'shipped budget function missing or ambiguous'}
$calls=@($ast.FindAll({param($n) $n -is [Management.Automation.Language.CommandAst] -and $n.GetCommandName() -eq 'Get-ReservationBudget'},$true))
if($calls.Count -ne 1){throw 'loop must call the shipped reducer'}
Invoke-Expression $functions[0].Extent.Text
$spent=Get-ReservationBudget -LedgerPath $Ledger -TodayUtc ([datetime]'2026-09-05T12:00:00Z') -BoardRoot $Board
Write-Output ('SPENT=' + $spent)
''', encoding='utf-8')
    result = subprocess.run(['pwsh', '-NoProfile', '-NonInteractive', '-File', str(harness),
        '-Source', str(LOOP_SCRIPT), '-Ledger', str(ledger), '-Board', str(tmp_path)], text=True, capture_output=True)
    assert result.returncode == 0, result.stdout + result.stderr
    return int(result.stdout.split('SPENT=')[-1].strip())


def _reservation_fixture(tmp_path, index=0, exit_code=1, cost=0.0, reported=True):
    # Disposable actual-schema evidence. These are fixtures, never live lane receipts.
    run = tmp_path / '.claude-state' / 'fleet-runs' / f'fixture-{index}'
    run.mkdir(parents=True, exist_ok=True)
    receipt_path = run / 'sonnet-001.receipt.json'
    receipt = {'schema': 'mlv-app/fleet-lane-receipt/v1', 'lane': 'sonnet', 'card': f'CARD-{index}',
        'startedUtc': '2026-09-05T09:00:01Z', 'endedUtc': '2026-09-05T09:00:02Z',
        'exitCode': exit_code, 'spend': {'costReported': reported, 'costUsd': cost},
        'promptPath': str(run / 'sonnet-001.prompt.txt'), 'outputPath': str(run / 'sonnet-001.last.txt')}
    receipt_path.write_text(json.dumps(receipt), encoding='utf-8')
    reserved = {'reservationId': f'ID-{index}', 'state': 'reserved', 'lane': 'sonnet', 'card': f'CARD-{index}',
        'runDir': str(run), 'recordedUtc': '2026-09-05T09:00:00Z'}
    terminal = {**reserved, 'state': 'refunded', 'recordedUtc': '2026-09-05T09:00:03Z',
        'receiptPath': str(receipt_path), 'receiptSha256': sha256_of(receipt_path),
        'laneExitCode': exit_code, 'laneCostReported': reported, 'laneCostUsd': cost}
    return reserved, terminal, receipt, receipt_path


def _budget_of(tmp_path, rows):
    # Exercise the production reservation reducer using the historical cases'
    # exit/cost facts, translated into the new receipt-bound ledger contract.
    ledger = []
    for index, row in enumerate(rows):
        reserved, terminal, _, _ = _reservation_fixture(tmp_path, index, row['laneExitCode'],
            row['laneCostUsd'], row['laneCostReported'])
        ledger.extend([reserved, terminal])
    return _run_reservation_budget(tmp_path, ledger)



def _row(card, exit_code, cost, reported=True):
    return {
        "cardId": card, "dispatchedUtc": "2026-09-05T03:49:26.0000000Z",
        "laneExitCode": exit_code, "laneCostUsd": cost, "laneCostReported": reported,
    }


def test_the_three_real_receipts_from_20260905_score_correctly(tmp_path):
    rows = [
        _row("GATE-FAMILY-BOOKED", 1, 0.0),        # 429 in 2.8s, bought nothing -> REFUND
        _row("CITE-TXN-1", 1, 2.244599),           # failed, but USD 2.24 is gone -> CHARGE
        _row("CLEANUP-1", 0, 2.048145),            # succeeded                    -> CHARGE
    ]
    assert _budget_of(tmp_path, rows) == 2


def test_a_failed_but_EXPENSIVE_lane_is_still_charged(tmp_path):
    assert _budget_of(tmp_path, [_row("X", 1, 2.24)]) == 1


def test_an_unreported_cost_is_never_treated_as_free(tmp_path):
    assert _budget_of(tmp_path, [_row("X", 1, None, reported=False)]) == 1


def test_a_successful_free_lane_is_still_charged(tmp_path):
    # exit 0 means work happened, whatever it cost.
    assert _budget_of(tmp_path, [_row("X", 0, 0.0)]) == 1


@pytest.mark.parametrize('mutation', [
    'charged', 'duplicate_terminal', 'duplicate_reserved', 'cross_day_terminal',
    'missing_receipt', 'wrong_hash', 'changed_receipt', 'foreign_lane', 'foreign_card',
    'foreign_prompt', 'foreign_output', 'missing_run', 'foreign_terminal_run',
    'bool_exit', 'string_cost', 'bool_cost', 'null_cost', 'string_reported',
    'receipt_before_reservation', 'receipt_after_terminal', 'copied_cost_mismatch',
])
def test_reservation_refund_rejects_ambiguous_or_unbound_evidence(tmp_path, mutation):
    reserved, terminal, receipt, path = _reservation_fixture(tmp_path)
    rows = [reserved, terminal]
    if mutation == 'charged': terminal['state'] = 'charged'
    elif mutation == 'duplicate_terminal': rows.append(dict(terminal))
    elif mutation == 'duplicate_reserved': rows.append(dict(reserved))
    elif mutation == 'cross_day_terminal': rows.append({**terminal, 'recordedUtc': '2026-09-06T01:00:00Z'})
    elif mutation == 'missing_receipt': path.unlink()
    elif mutation == 'wrong_hash': terminal['receiptSha256'] = '0' * 64
    elif mutation == 'changed_receipt': path.write_text('{}')
    elif mutation == 'foreign_lane': receipt['lane'] = 'opus'
    elif mutation == 'foreign_card': receipt['card'] = 'FOREIGN'
    elif mutation == 'foreign_prompt': receipt['promptPath'] = str(tmp_path / 'foreign.prompt.txt')
    elif mutation == 'foreign_output': receipt['outputPath'] = str(tmp_path / 'foreign.last.txt')
    elif mutation == 'missing_run': reserved.pop('runDir')
    elif mutation == 'foreign_terminal_run': terminal['runDir'] = str(tmp_path / 'foreign')
    elif mutation == 'bool_exit': receipt['exitCode'] = True
    elif mutation == 'string_cost': receipt['spend']['costUsd'] = '0'
    elif mutation == 'bool_cost': receipt['spend']['costUsd'] = False
    elif mutation == 'null_cost': receipt['spend']['costUsd'] = None
    elif mutation == 'string_reported': receipt['spend']['costReported'] = 'true'
    elif mutation == 'receipt_before_reservation': receipt['startedUtc'] = '2026-09-05T08:00:00Z'
    elif mutation == 'receipt_after_terminal': receipt['endedUtc'] = '2026-09-05T10:00:00Z'
    elif mutation == 'copied_cost_mismatch': terminal['laneCostUsd'] = 1
    if mutation.startswith(('foreign_lane', 'foreign_card', 'foreign_prompt', 'foreign_output',
                            'bool_', 'string_', 'null_', 'receipt_')):
        path.write_text(json.dumps(receipt), encoding='utf-8')
        terminal['receiptSha256'] = sha256_of(path)
    assert _run_reservation_budget(tmp_path, rows) == 1


def test_malformed_reservation_ids_each_count_without_sentinel_collision(tmp_path):
    reserved, _, _, _ = _reservation_fixture(tmp_path)
    rows = [{**reserved, 'reservationId': value} for value in (None, '', 23, '__malformed__0')]
    assert _run_reservation_budget(tmp_path, rows) == 4


def test_reservation_budget_preserves_utc_boundary_and_counts_unresolved_once(tmp_path):
    reserved, _, _, _ = _reservation_fixture(tmp_path)
    rows = [reserved, dict(reserved),
        {**reserved, 'reservationId': 'PREVIOUS', 'recordedUtc': '2026-09-04T23:59:59Z'},
        {**reserved, 'reservationId': 'OFFSET', 'recordedUtc': '2026-09-04T20:00:00-05:00'}]
    assert _run_reservation_budget(tmp_path, rows) == 2


@pytest.mark.parametrize('bad_row', [None, [], {'state': 'mystery', 'recordedUtc': '2026-09-05T09:00:00Z'},
    {'state': 'reserved', 'recordedUtc': 'not-a-date'}])
def test_malformed_ledger_cannot_create_budget(tmp_path, bad_row):
    with pytest.raises(AssertionError):
        _run_reservation_budget(tmp_path, [bad_row])


@pytest.mark.parametrize('exit_code,cost,reported,expected', [
    (1, 0, True, 'refunded'), (0, 0, True, 'charged'), (1, 2.24, True, 'charged'),
    (1, None, False, 'charged'), (1, '0', True, 'charged'), (1, False, True, 'charged'),
    (1, 0, 'true', 'charged'),
])
def test_dispatch_terminal_uses_actual_typed_receipt_bytes(tmp_path, exit_code, cost, reported, expected):
    reserved, _, _, path = _reservation_fixture(tmp_path, exit_code=exit_code, cost=cost, reported=reported)
    harness = tmp_path / 'terminal-writer.ps1'
    harness.write_text('''param($Source,$Board,$Run,$ExitCode)
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
$RepoRoot=$Board
$ReservationsPath=Join-Path $Board 'writer-ledger.jsonl'
$tokens=$null;$errors=$null
$ast=[Management.Automation.Language.Parser]::ParseFile($Source,[ref]$tokens,[ref]$errors)
if($errors.Count){throw 'source parse failed'}
foreach($name in @('Get-DispatchSpendEvidence','Write-DispatchReservation')) {
    $found=@($ast.FindAll({param($n) $n -is [Management.Automation.Language.FunctionDefinitionAst] -and $n.Name -eq $name},$true))
    if($found.Count -ne 1){throw 'writer function missing or ambiguous'}
    Invoke-Expression $found[0].Extent.Text
}
$row=Write-DispatchReservation -ReservationId 'TEST-ID' -State charged -Card 'CARD-0' -Kind product -Lane sonnet -RunDir $Run -ObservedExit ([int]$ExitCode)
$row|ConvertTo-Json -Depth 6 -Compress
''', encoding='utf-8')
    result = subprocess.run(['pwsh', '-NoProfile', '-NonInteractive', '-File', str(harness),
        '-Source', str(WORKSTREAM), '-Board', str(tmp_path), '-Run', reserved['runDir'],
        '-ExitCode', str(exit_code)], text=True, capture_output=True)
    assert result.returncode == 0, result.stdout + result.stderr
    terminal = json.loads(result.stdout.strip().splitlines()[-1])
    assert terminal['state'] == expected
    assert terminal['runDir'] == reserved['runDir']
    assert json.loads((tmp_path / 'writer-ledger.jsonl').read_text(encoding='utf-8-sig')) == terminal
    if expected == 'refunded':
        assert terminal['receiptSha256'] == sha256_of(path)
        assert (tmp_path / terminal['receiptPath']).resolve() == path.resolve()
        assert terminal['laneCostReported'] is True and terminal['laneCostUsd'] == 0
