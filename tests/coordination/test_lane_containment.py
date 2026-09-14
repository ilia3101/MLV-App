import hashlib, json, os, re, subprocess, sys, time
from datetime import datetime
from pathlib import Path

import pytest

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
CANDIDATE = ROOT / "tools" / "coordination" / "Invoke-Lane.ps1"
pytestmark = pytest.mark.skipif(sys.platform != "win32", reason="Windows Job Object contract")
PWSH = "pwsh.exe"

# PR #105 round 4: Invoke-Lane.ps1 classifies containment.ownerAbsentReason into a
# CLOSED set of fixed tokens, chosen by WHERE a failure happened rather than by what
# its raw exception message said -- free text is not admissible evidence about a
# safety property. This test file cannot import a .ps1 file, so the tokens are
# pinned here as literals; keep them in sync BY HAND with Invoke-Lane.ps1's own
# $OWNER_ABSENT_* constants (declared beside $hostStarted, ~line 337-346).
NO_HOST_TOKENS = {"launch-budget-exhausted", "start-threw"}
POST_START_UNRECORDED = "post-start-unrecorded"

# PR #105 final: containment.ownerKillOutcome tokens, same hand-duplication problem
# as NO_HOST_TOKENS above -- kept in sync BY HAND with Invoke-Lane.ps1's own
# $OWNER_KILL_OUTCOME_* constants (declared beside $ownerKillAttempted, ~line 365-372).
# The cross-family review that added kill-wait-timeout noted this duplication drifts
# silently unless something pins the full set; test_kill_outcome_token_set_matches_
# producer_constants below asserts this literal against the source directly instead
# of trusting the hand-copy.
KILL_OUTCOME_TOKENS = {"already-exited", "killed", "kill-wait-timeout", "kill-threw"}


def assert_owner_absence_is_legitimate(containment, context):
    # The one place round-3, round-4, and round-5 tests all funnel through. Since
    # PR #105 round 5, a POST_START_UNRECORDED receipt carries a NON-NULL ownerPid
    # (captured on its own non-throwing line before the construction that failed),
    # so ownerPid presence/absence no longer distinguishes the states -- only
    # ownerAbsentReason does. See Invoke-Lane.ps1's catch block (~line 679-711, and
    # the pre-assignment kill block around ~line 674-696) for the producer side.
    assert containment is not None, f"ambiguous containment receipt: containment itself is None: {context}"
    reason = containment.get("ownerAbsentReason")
    if reason in NO_HOST_TOKENS:
        assert containment.get("ownerPid") is None, (
            f"a no-host token must never carry a pid -- no host ever existed: {context}"
        )
        return  # legitimate: no host was ever created
    pytest.fail(f"ambiguous or unrecognised containment receipt: ownerAbsentReason is {reason!r}, which is "
                f"not one of the closed-set no-host tokens {NO_HOST_TOKENS!r} -- either it is "
                f"{POST_START_UNRECORDED!r} (a host EXISTED; its pid is recorded so the orphan is never "
                f"invisible, but the receipt is still ambiguous and must never be treated as a legitimate "
                f"absence) or it is unrecognised entirely: {context}")


def wait_json(path, pred=lambda x: True, seconds=12):
    end=time.monotonic()+seconds; last=None
    while time.monotonic()<end:
        try:
            last=json.loads(path.read_text(encoding="utf-8"))
            if pred(last): return last
        except (FileNotFoundError,PermissionError,json.JSONDecodeError): pass
        time.sleep(.05)
    raise AssertionError(f"timeout {path}: {last!r}")


def identity(pid):
    # 1 s -TimeoutSec deadline race (evidence 2026-09-09): the fallback receipt
    # built in Invoke-Lane.ps1's catch block can carry containment.ownerPid=None
    # when the deadline fires before a contained host was ever started. There is
    # no process to look up in that case, so return None instead of int(None).
    if pid is None: return None
    q=f"$p=Get-Process -Id {int(pid)} -ErrorAction SilentlyContinue;if($null-eq $p){{exit 3}};$p.StartTime.ToUniversalTime().ToString('o')"
    r=subprocess.run([PWSH,"-NoProfile","-NonInteractive","-Command",q],text=True,capture_output=True,timeout=5)
    return r.stdout.strip() if r.returncode==0 else None


def wait_absent(item, seconds=10):
    end=time.monotonic()+seconds
    while time.monotonic()<end:
        if identity(item["pid"]) != item["createdUtc"]: return
        time.sleep(.05)
    raise AssertionError(f"still alive: {item}")


def stop_exact(item):
    if identity(item["pid"]) == item["createdUtc"]:
        subprocess.run([PWSH,"-NoProfile","-NonInteractive","-Command",f"Stop-Process -Id {int(item['pid'])} -Force"],timeout=5,check=False)


@pytest.fixture
def fixture_tree(tmp_path):
    grand=tmp_path/"grand.ps1"; child=tmp_path/"child.ps1"; shim=tmp_path/"fake-claude.cmd"
    grand.write_text("$me=Get-Process -Id $PID;@{pid=$PID;createdUtc=$me.StartTime.ToUniversalTime().ToString('o')}|ConvertTo-Json|Set-Content -Encoding utf8NoBOM $env:MLV_FIXTURE_GRAND;Start-Sleep -Seconds 60\n",encoding="ascii")
    child.write_text(r'''$ErrorActionPreference='Stop'
$me=Get-Process -Id $PID
@{pid=$PID;createdUtc=$me.StartTime.ToUniversalTime().ToString('o')}|ConvertTo-Json|Set-Content -Encoding utf8NoBOM $env:MLV_FIXTURE_CHILD
$args|ConvertTo-Json|Set-Content -Encoding utf8NoBOM $env:MLV_FIXTURE_ARGS
$env:CLAUDE_CODE_EFFORT_LEVEL|Set-Content -Encoding utf8NoBOM $env:MLV_FIXTURE_EFFORT
if($env:MLV_FIXTURE_MODE -ne 'normal'){
  $g=Start-Process pwsh.exe -ArgumentList @('-NoProfile','-NonInteractive','-File',$env:MLV_FIXTURE_GRAND_SCRIPT) -WindowStyle Hidden -PassThru
  while(-not(Test-Path $env:MLV_FIXTURE_GRAND)){Start-Sleep -Milliseconds 20}
}
$text=[Console]::In.ReadToEnd()
$text|Set-Content -Encoding utf8NoBOM $env:MLV_FIXTURE_PROMPT
if($env:MLV_FIXTURE_MODE -ne 'normal'){Start-Sleep -Seconds 60}
[Console]::Out.Write('{"result":"fixture-result","total_cost_usd":0,"num_turns":1}')
[Console]::Error.Write('fixture-err')
exit 0
''',encoding="ascii")
    shim.write_text(f'@echo off\r\n"{PWSH}" -NoProfile -NonInteractive -File "{child}" %*\r\n',encoding="ascii")
    yield {"root":tmp_path,"shim":shim,"child":child,"grand":grand}
    for name in ("child.json","grand.json"):
        p=tmp_path/name
        if p.exists():
            try: stop_exact(json.loads(p.read_text(encoding="utf-8-sig")))
            except Exception: pass


def prepare(tree, mode, assignment_failure=False, editing=False, allowed_tools="", lane="sonnet", mutation=None):
    root=tree["root"]; script=root/"Invoke-Lane.ps1"
    text=CANDIDATE.read_text(encoding="utf-8")
    text=text.replace("$CLAUDE_EXE = Join-Path $env:APPDATA 'npm\\claude.cmd'", "$CLAUDE_EXE = '"+str(tree['shim']).replace("'","''")+"'")
    text=text.replace("$CODEX_EXE  = Join-Path $env:APPDATA 'npm\\codex.cmd'", "$CODEX_EXE = '"+str(tree['shim']).replace("'","''")+"'")
    if assignment_failure:
        text=text.replace("[MlvLaneJob]::AssignOrThrow($jobHandle, $proc.Handle)", "throw [ComponentModel.Win32Exception]::new(5, 'fixture-assignment-failure')")
    if mutation: text=mutation(text)
    script.write_text(text,encoding="utf-8")
    (root/"lane-provider-refusal.ps1").write_bytes((ROOT/"tools"/"coordination"/"lane-provider-refusal.ps1").read_bytes())
    if editing:
        hook=root/"tools"/"hooks"/"mlv-never-authorized.py"; hook.parent.mkdir(parents=True); hook.write_text("# fixture hook\n",encoding="ascii")
        rec=root/".claude-state"/"coordination"/"dual-lane"/"receipts"/"0.05-hook-enforced.json"; rec.parent.mkdir(parents=True)
        rec.write_text(json.dumps({"hookSha256":hashlib.sha256(hook.read_bytes()).hexdigest()}),encoding="utf-8")
    run=root/"run"; run.mkdir()
    env=os.environ.copy(); env.update({
      "MLV_BOARD_ROOT":str(root),"MLV_FIXTURE_MODE":mode,
      "MLV_FIXTURE_CHILD":str(root/"child.json"),"MLV_FIXTURE_GRAND":str(root/"grand.json"),
      "MLV_FIXTURE_GRAND_SCRIPT":str(tree["grand"]),"MLV_FIXTURE_ARGS":str(root/"args.json"),
      "MLV_FIXTURE_PROMPT":str(root/"prompt.txt"),"MLV_FIXTURE_EFFORT":str(root/"effort.txt")})
    cmd=[PWSH,"-NoLogo","-NoProfile","-NonInteractive","-ExecutionPolicy","Bypass","-File",str(script),"-Lane",lane,"-Prompt","fixture prompt","-WorkDir",str(root),"-RunDir",str(run),"-TimeoutSec","3" if mode=="timeout" else "30","-Card","FIXTURE","-ReasoningEffort","low"]
    if editing: cmd += ["-AllowEdits","-AllowedTools",allowed_tools]
    return cmd,env,run/(lane+"-001.receipt.json")


def test_read_only_argv_json_stdin_and_tool_denial(fixture_tree):
    cmd,env,receipt=prepare(fixture_tree,"normal")
    r=subprocess.run(cmd,env=env,text=True,capture_output=True,timeout=20)
    assert r.returncode==0,(r.stdout,r.stderr)
    q=json.loads(receipt.read_text(encoding="utf-8")); assert q["state"]=="complete" and q["complete"]
    assert q["containment"]["jobAssigned"] and q["containment"]["promptDelivered"]
    assert q["containment"]["childCreatedUtc"].endswith("Z") and "T" in q["containment"]["childCreatedUtc"]
    assert (fixture_tree["root"]/"prompt.txt").read_text(encoding="utf-8-sig").strip()=="fixture prompt"
    argv=json.loads((fixture_tree["root"]/"args.json").read_text(encoding="utf-8-sig"))
    i=argv.index("--disallowedTools"); assert argv[i+1]=="Agent,Task"
    j=argv.index("--allowedTools"); assert argv[j+1]=="Read,Grep,Glob"
    assert "--append-system-prompt" in argv
    notice=argv[argv.index("--append-system-prompt")+1]
    assert argv.count("--append-system-prompt")==1
    assert "only Read, Grep, and Glob" in notice and "do not call or retry" in notice
    assert q["authority"]["capabilityNotice"]==notice
    assert q["outputBytes"]>0 and q["spend"]["costUsd"]==0
    assert q["effort"]=="low"
    assert (fixture_tree["root"]/"effort.txt").read_text(encoding="utf-8-sig").strip()=="low"


def test_timeout_kills_owned_child_and_grandchild(fixture_tree):
    cmd,env,receipt=prepare(fixture_tree,"timeout")
    r=subprocess.run(cmd,env=env,text=True,capture_output=True,timeout=20)
    assert r.returncode==124,(r.stdout,r.stderr)
    q=json.loads(receipt.read_text(encoding="utf-8")); assert q["state"]=="complete" and q["timedOut"]
    wait_absent(json.loads((fixture_tree["root"]/"child.json").read_text(encoding="utf-8-sig")))
    wait_absent(json.loads((fixture_tree["root"]/"grand.json").read_text(encoding="utf-8-sig")))


def test_owner_loss_closes_job(fixture_tree):
    cmd,env,receipt=prepare(fixture_tree,"ownerloss")
    outer=subprocess.Popen(cmd,env=env,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
    q=wait_json(receipt,lambda x:x.get("state")=="running" and x.get("containment",{}).get("promptDelivered"))
    assert q["containment"]["childCreatedUtc"]==identity(q["containment"]["childPid"])
    child=wait_json(fixture_tree["root"]/"child.json"); grand=wait_json(fixture_tree["root"]/"grand.json")
    outer_identity={"pid":outer.pid,"createdUtc":identity(outer.pid)}
    stop_exact(outer_identity); outer.wait(timeout=8)
    wait_absent(child); wait_absent(grand)
    q=json.loads(receipt.read_text(encoding="utf-8")); assert q["state"]=="running" and not q["complete"]


def test_assignment_failure_starts_no_provider(fixture_tree):
    cmd,env,receipt=prepare(fixture_tree,"normal",True)
    r=subprocess.run(cmd,env=env,text=True,capture_output=True,timeout=15)
    assert r.returncode==127,(r.stdout,r.stderr)
    q=json.loads(receipt.read_text(encoding="utf-8")); assert q["state"]=="failed" and not q["complete"]
    assert not q["containment"]["jobAssigned"] and q["containment"]["assignmentErrorCode"]==5
    assert not (fixture_tree["root"]/"child.json").exists()
    assert not (fixture_tree["root"]/"prompt.txt").exists()
    wait_absent({"pid":q["containment"]["ownerPid"],"createdUtc":q["containment"]["ownerCreatedUtc"]})


def test_editing_argv_preserves_allowlist_and_denies_nested_tools(fixture_tree):
    cmd,env,receipt=prepare(fixture_tree,"normal",editing=True,allowed_tools="Read,Write,Edit")
    r=subprocess.run(cmd,env=env,text=True,capture_output=True,timeout=20)
    assert r.returncode==0,(r.stdout,r.stderr)
    argv=json.loads((fixture_tree["root"]/"args.json").read_text(encoding="utf-8-sig"))
    assert argv[argv.index("--permission-mode")+1]=="acceptEdits"
    assert argv[argv.index("--allowedTools")+1]=="Read,Write,Edit"
    assert argv[argv.index("--disallowedTools")+1]=="Agent,Task"
    assert "--append-system-prompt" not in argv
    q=json.loads(receipt.read_text(encoding="utf-8")); assert q["authority"]["disallowedTools"]==["Agent","Task"]


@pytest.mark.parametrize("bad",["Agent"," task ","Read, AGENT ,Write","Read,Task"])
def test_editing_explicit_nested_tool_is_rejected_before_reservation(fixture_tree,bad):
    cmd,env,receipt=prepare(fixture_tree,"normal",editing=True,allowed_tools=bad)
    r=subprocess.run(cmd,env=env,text=True,capture_output=True,timeout=10)
    assert r.returncode!=0 and "nested-agent-tool-forbidden" in r.stderr
    assert not receipt.exists() and not (fixture_tree["root"]/"child.json").exists()


def test_codex_launch_stays_direct_without_claude_flags(fixture_tree):
    cmd,env,receipt=prepare(fixture_tree,"normal",lane="sol")
    r=subprocess.run(cmd,env=env,text=True,capture_output=True,timeout=20)
    assert r.returncode==0,(r.stdout,r.stderr)
    argv=json.loads((fixture_tree["root"]/"args.json").read_text(encoding="utf-8-sig"))
    assert argv[0]=="exec" and "--disallowedTools" not in argv and "--allowedTools" not in argv
    assert "--append-system-prompt" not in argv
    assert argv[argv.index("-s")+1]=="read-only"
    assert 'model_reasoning_effort=low' in argv or 'model_reasoning_effort="low"' in argv
    q=json.loads(receipt.read_text(encoding="utf-8"))
    assert q["containment"] is None and q["effort"]=="low" and q["complete"]


def test_startup_consumes_same_deadline_without_starting_provider(fixture_tree):
    def delay(text):
        return text.replace("$line=[Console]::In.ReadLine(); if([string]::IsNullOrWhiteSpace($line)){throw 'launch-frame-missing'}", "Start-Sleep -Seconds 8\n$line=[Console]::In.ReadLine(); if([string]::IsNullOrWhiteSpace($line)){throw 'launch-frame-missing'}")
    cmd,env,receipt=prepare(fixture_tree,"normal",mutation=delay)
    cmd[cmd.index("-TimeoutSec")+1]="1"
    r=subprocess.run(cmd,env=env,text=True,capture_output=True,timeout=15)
    assert r.returncode==124,(r.stdout,r.stderr)
    q=json.loads(receipt.read_text(encoding="utf-8"))
    assert q["timedOut"]
    assert not (fixture_tree["root"]/"child.json").exists()
    # PR #105 round 2 (sol blocker): Invoke-Lane.ps1 now records containedHost.pid
    # the instant Process::Start returns, before any call that can throw -- so a
    # null ownerPid means no host exists (either the launch budget was already
    # gone, or Start itself threw). This fixture's delayed stdin read happens
    # AFTER a successful Start, so it must land in the "owner present" branch;
    # ownerPid==None here would itself be the round-2 regression.
    #
    # PR #105 round 3: a null ownerPid alone still can't tell "budget exhausted
    # before Start was ever called" (Invoke-Lane.ps1:470-472, legitimate) apart
    # from an ambiguous, unexplained absence. containment.ownerAbsentReason
    # (Invoke-Lane.ps1's catch block, ~661-676) now names WHY, so classify on
    # that instead of guessing from ownerPid alone.
    containment=q.get("containment")
    owner_pid=containment.get("ownerPid") if containment else None
    if containment is not None and owner_pid is not None:
        # Unchanged from round 2: a pid was recorded, so a host definitely exists
        # (or existed) and must be reaped.
        if containment["ownerCreatedUtc"] is None:
            # A pid with no createdUtc means Start succeeded but StartTime read threw;
            # there is no createdUtc to compare against, so the only provable check is
            # that the pid is not (or no longer) an alive process.
            assert identity(owner_pid) is None
        else:
            wait_absent({"pid":q["containment"]["ownerPid"],"createdUtc":q["containment"]["ownerCreatedUtc"]})
    else:
        # PR #105 round 4: ownerAbsentReason must be one of the closed-set no-host
        # tokens, never an arbitrary truthy string (round 3's "if reason: pass" let
        # a POST_START_UNRECORDED-shaped failure pose as a legitimate absence).
        assert_owner_absence_is_legitimate(containment, q)


def test_zero_timeout_exhausts_budget_before_spawn_and_names_the_reason(fixture_tree):
    # Reachability proof for the legitimate null-owner branch (PR #105 round 3,
    # task item 4): -TimeoutSec 0 means the budget is already spent by the time
    # execution reaches Invoke-Lane.ps1:470, so that line's throw fires BEFORE
    # Process::Start is ever called -- no mutation/mock needed, this is the real
    # code path. No host exists, so ownerPid must be null with ownerAbsentReason
    # naming why.
    cmd,env,receipt=prepare(fixture_tree,"normal")
    cmd[cmd.index("-TimeoutSec")+1]="0"
    r=subprocess.run(cmd,env=env,text=True,capture_output=True,timeout=15)
    assert r.returncode==124,(r.stdout,r.stderr)
    q=json.loads(receipt.read_text(encoding="utf-8"))
    assert q["timedOut"]
    assert not (fixture_tree["root"]/"child.json").exists()
    containment=q["containment"]
    assert containment["ownerPid"] is None
    assert containment["ownerAbsentReason"]=="launch-budget-exhausted"


def test_start_threw_is_classified_as_no_host(fixture_tree):
    # Reachability proof for the other no-host token (PR #105 round 4): make
    # Process::Start itself throw for the claude engine. $hostStarted is never set
    # (it is assigned on the line immediately AFTER Start returns), so the catch
    # block must land in the "Start never returned" branch and pick the generic
    # start-threw token, not the budget token (this is not a TimeoutException) and
    # not POST_START_UNRECORDED (no host ever existed).
    def break_start(text):
        old = "$proc = [Diagnostics.Process]::Start($psi)"
        assert text.count(old) == 2
        # Replace ONLY the first occurrence -- the claude-engine branch (~line 517),
        # which runs before $hostStarted is set. The second occurrence is the
        # non-claude branch and must stay untouched.
        return text.replace(old, "throw 'fixture-start-threw'", 1)
    cmd,env,receipt=prepare(fixture_tree,"normal",mutation=break_start)
    r=subprocess.run(cmd,env=env,text=True,capture_output=True,timeout=15)
    assert r.returncode==127,(r.stdout,r.stderr)
    assert not (fixture_tree["root"]/"child.json").exists()
    q=json.loads(receipt.read_text(encoding="utf-8"))
    containment=q["containment"]
    assert containment["ownerPid"] is None
    assert containment["ownerAbsentReason"]=="start-threw"
    assert containment["ownerAbsentDetail"]=="fixture-start-threw"
    assert_owner_absence_is_legitimate(containment, q)


def test_post_start_unrecorded_is_named_not_hidden(fixture_tree):
    # Reachability proof for the genuinely-ambiguous branch (PR #105 round 4, task
    # item 4): inject a throw between Process::Start returning (a host now EXISTS)
    # and $containedHost being built, using the same fixture-mutation mechanism
    # test_setup_origin_and_expired_budget_are_deterministic already uses to inject
    # text at a specific line. This is exactly the failure the cross-family review
    # found: without $hostStarted, this exception's message would pose as a
    # legitimate no-host reason. With it, the catch block must name it
    # POST_START_UNRECORDED instead -- and the test below must FAIL LOUD on that
    # receipt if a caller naively treated it as legitimate (proven by calling the
    # shared assertion helper and expecting it to raise).
    #
    # PR #105 round 5 (this packet): $hostPid is now captured on its own
    # non-throwing line BEFORE the $containedHost build that this fixture breaks,
    # so a post-start-unrecorded receipt must carry the REAL host pid, not null --
    # a null ownerPid here would itself be the round-5 regression, since the pid
    # is the only channel by which anyone later learns the orphan existed. The
    # mutation also drops a marker file with $hostPid's value (via the same
    # Write-Utf8NoBom helper the production code already uses) so the test can
    # assert the receipt's ownerPid equals the REAL pid, not merely "non-null".
    marker = fixture_tree["root"] / "host-pid.txt"
    def break_containedHost_build(text):
        old = "$containedHost = [ordered]@{ pid=$hostPid; createdUtc=$null }"
        assert text.count(old) == 1
        marker_literal = str(marker).replace("'", "''")
        return text.replace(old, "Write-Utf8NoBom '%s' ([string]$hostPid)\n    throw 'fixture-post-start-unrecorded'" % marker_literal)
    cmd,env,receipt=prepare(fixture_tree,"normal",mutation=break_containedHost_build)
    r=subprocess.run(cmd,env=env,text=True,capture_output=True,timeout=15)
    assert r.returncode==127,(r.stdout,r.stderr)
    assert not (fixture_tree["root"]/"child.json").exists()
    q=json.loads(receipt.read_text(encoding="utf-8"))
    containment=q["containment"]
    real_pid = int(marker.read_text(encoding="utf-8-sig").strip())
    assert containment["ownerPid"] == real_pid
    assert containment["ownerAbsentReason"]==POST_START_UNRECORDED
    assert containment["ownerAbsentDetail"]=="fixture-post-start-unrecorded"
    assert containment["ownerAbsentReason"] not in NO_HOST_TOKENS
    # The whole point: this receipt must NOT be accepted as a legitimate absence,
    # even though a pid is now present.
    with pytest.raises(pytest.fail.Exception):
        assert_owner_absence_is_legitimate(containment, q)


def test_pre_assignment_kill_failure_is_recorded_not_swallowed(fixture_tree):
    # Falsifier for the OTHER half of this packet (PR #105 round 5, sol PR #105
    # blocker): at the pre-assignment site (Invoke-Lane.ps1 ~line 674-696), the
    # host is still OUTSIDE the job, so a swallowed Kill failure there leaves a
    # GENUINE orphan -- unlike the post-timeout kill at ~line 602, where the job
    # is kill-on-close and the tree is already terminated. Combine the same
    # post-start-unrecorded trigger (so the pre-assignment kill path is reached
    # at all: $jobAssigned is still false) with a forced Kill failure, and prove
    # the receipt records ownerKillAttempted/ownerKillOutcome instead of the bare
    # `catch { }` this repo used to have there silently discarding it.
    def break_kill(text):
        old_throw = "$containedHost = [ordered]@{ pid=$hostPid; createdUtc=$null }"
        assert text.count(old_throw) == 1
        text = text.replace(old_throw, "throw 'fixture-post-start-unrecorded'")
        # 1b4a82ab split the old single `Kill($true); [void]WaitForExit(5000)`
        # statement into two lines so WaitForExit's bool return could be
        # captured instead of discarded -- the anchor now spans both lines.
        # `$proc.Kill($true)` alone is NOT unique (the post-timeout kill at
        # ~line 626 also calls it), so the second line's exact indentation is
        # part of the anchor, same discipline as every other anchor here.
        old_kill = "$proc.Kill($true)\n                $exitedWithinWait = $proc.WaitForExit(5000)"
        assert text.count(old_kill) == 1
        return text.replace(old_kill, "throw 'fixture-kill-failed'")
    cmd,env,receipt=prepare(fixture_tree,"normal",mutation=break_kill)
    r=subprocess.run(cmd,env=env,text=True,capture_output=True,timeout=15)
    assert r.returncode==127,(r.stdout,r.stderr)
    q=json.loads(receipt.read_text(encoding="utf-8"))
    containment=q["containment"]
    # The whole point: the failed kill is VISIBLE, never silent.
    assert containment["ownerKillAttempted"] is True
    assert containment["ownerKillOutcome"]=="kill-threw"
    assert containment["ownerKillDetail"]=="fixture-kill-failed"
    # Forcing the kill to throw means the real kill never ran -- this test, not
    # production code, is responsible for reaping the host it just orphaned.
    # ownerPid is guaranteed non-null by this same packet's other remedy.
    owner_pid = containment["ownerPid"]
    assert owner_pid is not None
    subprocess.run([PWSH,"-NoProfile","-NonInteractive","-Command",
                     f"Stop-Process -Id {int(owner_pid)} -Force -ErrorAction SilentlyContinue"],
                    timeout=5, check=False)


def test_pre_assignment_kill_wait_timeout_is_recorded_not_killed(fixture_tree):
    # Falsifier for PR #105 final (cross-family review of 0f8ba40a): WaitForExit(Int32)
    # RETURNS a bool -- true iff the process exited within the timeout -- and round 5
    # discarded that return with [void], recording 'killed' unconditionally. A host
    # that outlives the bounded wait -- exactly the orphan this whole change exists to
    # make visible -- was therefore reported as killed: manufactured evidence, worse
    # than the bare `catch { }` this whole packet replaced.
    #
    # PR #105 round 6 (falsifier hardening, 2026-09-09): the prior construction shrank
    # the real wait to WaitForExit(0), racing a real WaitForExit call against real OS
    # process teardown on the bet that 0ms wouldn't be enough time for the process to
    # actually exit. It was not reliable: reproduced 3 of 3 in isolation on this host
    # (36 processes, 27% CPU -- well below saturation) as an ASSERTION failure, not a
    # timeout, because WaitForExit(0) sometimes observed the process as already gone.
    # A falsifier for "false observations are classified correctly" that can itself
    # observe true is not a proof.
    #
    # Constructing a real process that genuinely SURVIVES Process.Kill(entireProcessTree:
    # true) plus a real bounded wait is not achievable on demand on this platform --
    # TerminateProcess cannot be caught, ignored, or reliably outlasted by the target,
    # so there is no cheap, reliable way to make a real teardown race land on the false
    # branch every time. Per this packet's own instructions, an unreliable variant is
    # worse than no variant (a 3-of-3-failing test teaches a reader to ignore red), so
    # instead of shrinking the wait, this test now mutates the runner's own source to
    # force the OBSERVED boolean itself to $false -- the same fixture-mutation
    # discipline every other test in this file already uses to reach its own branch
    # (see test_start_threw_is_classified_as_no_host,
    # test_post_start_unrecorded_is_named_not_hidden). The real Kill($true) call is
    # left untouched; only the captured WaitForExit result is forced.
    #
    # WHAT THIS PROVES: the CLASSIFICATION LOGIC -- that a false WaitForExit observation
    # is recorded as 'kill-wait-timeout' and is never silently upgraded to 'killed'.
    # WHAT THIS DOES NOT PROVE: that a real process can outlive a real Kill($true) plus
    # a real five-second WaitForExit on this platform. Whether a genuinely slow-to-die
    # host is always caught inside a realistic multi-second window is a timing property
    # of the OS, not a property of this code path, and is not exercised here.
    def force_wait_false(text):
        old = "$exitedWithinWait = $proc.WaitForExit(5000)"
        assert text.count(old) == 1
        return text.replace(old, "$exitedWithinWait = $false")
    cmd,env,receipt=prepare(fixture_tree,"normal",assignment_failure=True,mutation=force_wait_false)
    r=subprocess.run(cmd,env=env,text=True,capture_output=True,timeout=15)
    assert r.returncode==127,(r.stdout,r.stderr)
    q=json.loads(receipt.read_text(encoding="utf-8"))
    containment=q["containment"]
    # The whole point: an observed-not-exited result must never be reported as killed.
    assert containment["ownerKillAttempted"] is True
    assert containment["ownerKillOutcome"]=="kill-wait-timeout", (
        f"expected the observed-timeout token, got {containment['ownerKillOutcome']!r} -- "
        "this is the exact false-evidence defect this test exists to catch"
    )
    # Kill() itself was real and unmodified (only the captured wait result is forced),
    # so the host is gone or about to be -- reap defensively like the sibling
    # kill-threw test does.
    owner_pid = containment["ownerPid"]
    assert owner_pid is not None
    subprocess.run([PWSH,"-NoProfile","-NonInteractive","-Command",
                     f"Stop-Process -Id {int(owner_pid)} -Force -ErrorAction SilentlyContinue"],
                    timeout=5, check=False)


def test_kill_outcome_token_set_matches_producer_constants():
    # Guard against the exact drift the cross-family review flagged: this file's
    # KILL_OUTCOME_TOKENS is a hand-copy of Invoke-Lane.ps1's $OWNER_KILL_OUTCOME_*
    # constants because this file cannot import a .ps1. Pin the full set against the
    # source directly so an added/renamed/removed token fails this test loudly
    # instead of only failing closed by accident via an exact-string assertion
    # elsewhere.
    text = CANDIDATE.read_text(encoding="utf-8")
    found = set(re.findall(r"\$OWNER_KILL_OUTCOME_\w+\s*=\s*'([^']+)'", text))
    assert found == KILL_OUTCOME_TOKENS, f"producer constants {found!r} != pinned set {KILL_OUTCOME_TOKENS!r}"


def test_owner_absence_helper_fails_loud_on_post_start_or_unrecognised_tokens():
    # Prove the test-side guardrail itself is reachable and fires (not just
    # written): both the named-ambiguous token and a wholly unrecognised string
    # must be rejected, never silently tolerated the way round 3's bare
    # "if reason: pass" tolerated any truthy string.
    for reason in (POST_START_UNRECORDED, "something-unrecognised", None):
        with pytest.raises(pytest.fail.Exception):
            assert_owner_absence_is_legitimate({"ownerPid": None, "ownerAbsentReason": reason}, {"case": reason})
    # And the closed set itself must still pass.
    for reason in NO_HOST_TOKENS:
        assert_owner_absence_is_legitimate({"ownerPid": None, "ownerAbsentReason": reason}, {"case": reason})


@pytest.mark.parametrize("elapsed_ms,expected_exit", [(0, 0), (4000, 124)])
def test_setup_origin_and_expired_budget_are_deterministic(fixture_tree, elapsed_ms, expected_exit):
    marker = fixture_tree["root"] / "start-attempt.txt"
    def clocks(text):
        text = text.replace("$startedUtc = (Get-Date).ToUniversalTime()",
            "$startedUtc = [datetime]::Parse('2000-01-01T00:00:00Z').ToUniversalTime()", 1)
        old = "$sw         = [System.Diagnostics.Stopwatch]::StartNew()"
        assert text.count(old) == 1
        text = text.replace(old, "$sw = [pscustomobject]@{Elapsed=[timespan]::FromMilliseconds(%d)}\n$sw | Add-Member ScriptMethod Stop {}" % elapsed_ms)
        old = "$proc = [Diagnostics.Process]::Start($psi)"
        assert text.count(old) == 2
        return text.replace(old, "Write-Utf8NoBom '%s' 'start'\n    %s" % (str(marker).replace("'", "''"), old))
    cmd, env, receipt = prepare(fixture_tree, "normal", mutation=clocks)
    cmd[cmd.index("-TimeoutSec") + 1] = "3"
    result = subprocess.run(cmd, env=env, text=True, capture_output=True, timeout=20)
    assert result.returncode == expected_exit, (result.stdout, result.stderr)
    assert marker.exists() == (expected_exit == 0)
    q = json.loads(receipt.read_text(encoding="utf-8"))
    assert datetime.fromisoformat(q["startedUtc"]) == datetime.fromisoformat("2000-01-01T00:00:00+00:00")
    assert datetime.fromisoformat(q["containment"]["deadlineUtc"]) == datetime.fromisoformat("2000-01-01T00:00:03+00:00")
    assert q["timedOut"] == (expected_exit == 124)
    if expected_exit == 124:
        assert not q["containment"]["jobAssigned"]
        assert q["containment"]["ownerPid"] is None
        # PR #105 round 3: this parametrization mocks $sw.Elapsed to already exceed
        # the budget, so Invoke-Lane.ps1:470-472 throws before $proc = ...Start()
        # (proven by marker.exists() is False above) -- the same code path
        # -TimeoutSec 0 reproduces for real in
        # test_zero_timeout_exhausts_budget_before_spawn_and_names_the_reason.
        assert q["containment"]["ownerAbsentReason"]=="launch-budget-exhausted"
        assert not (fixture_tree["root"] / "child.json").exists()


def test_final_receipt_io_failure_cannot_keep_descendants_alive(fixture_tree):
    def fail_final_write(text):
        old='Write-Utf8NoBomAtomic $rcptPath (($receipt | ConvertTo-Json -Depth 6))'
        assert text.count(old)==1
        return text.replace(old,"throw 'fixture-final-receipt-write-failed'")
    cmd,env,receipt=prepare(fixture_tree,"timeout",mutation=fail_final_write)
    r=subprocess.run(cmd,env=env,text=True,capture_output=True,timeout=20)
    assert r.returncode!=0 and 'fixture-final-receipt-write-failed' in r.stderr
    q=json.loads(receipt.read_text(encoding="utf-8"))
    assert q['state']=='running' and not q['complete']
    wait_absent(json.loads((fixture_tree["root"]/"child.json").read_text(encoding="utf-8-sig")))
    wait_absent(json.loads((fixture_tree["root"]/"grand.json").read_text(encoding="utf-8-sig")))
