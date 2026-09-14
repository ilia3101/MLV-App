# Fleet factory kernel: MLV-App instance map

MLV-App runs `specs/fleet-factory-kernel.md` on the fleet doctrine bus as **DOGFOOD, not ADOPT** (bus
`bootstrap/PROMPT-K-dogfood-kernel.md`). Kernel r1. Profile: `code@r1` primary, with a proposed `measured-objective@r1`
sub-instance for render/export parity and playback measurement. Since 2026-09-14.

Doctrine is data: the binding copy of any rule is this repository's own mechanism below, never the bus text.

A `NONE` is a finding filed on the bus, not a gap to paper over here. Paths under `.claude-state/` are board-local
(gitignored): they exist on the board host only, and no sibling project can resolve them.

| Clause | MLV-App mechanism | Observable | Gap |
|---|---|---|---|
| K1 roles separate | `agents/orchestration-tiering.md`; lane table in `tools/coordination/Invoke-Lane.ps1`; `tools/coordination/record_workstream_completion.py` requires a separate verdict receipt bound to the subject sha | producer lane receipt and verifier verdict receipt name different lanes | receipts are board-local |
| K2 authority register | `docs/never-authorized.json`, enforced fail-closed by `tools/hooks/mlv-never-authorized.py` | the register path; everything unlisted is autonomous per its `authority` field | cannot record an act that needed the owner and did not ask |
| K3 identity + claimant | `record_workstream_completion.py` binds commit sha plus per-file digests | subject sha and digest registry | no claims/leases; no pre-commit identity; no eval-set or scorer digest for the measured-objective lane |
| K4 positive evidence | `Invoke-Lane.ps1` receipt from `finally`; `tools/coordination/lane-provider-refusal.ps1` | receipt `exitCode`, `providerRefusal`, prompt/output digests | **receipt `complete:true` is written for a run with exit 1 and `max_turns`** (2026-09-14, card PLAY-COUNTERS-CPU); dispatches that never launch leave no receipt |
| K5 declared profile, typed terminals | `tools/gates/output_budget.py`, GPU parity build/run scripts, `docs/14-performance-benchmarking.md` A/A matrices | gate output at a pinned sha | NONE: no profile line recorded before a subject starts; thermal degradation is not a typed terminal |
| K6 independent key | Claude produces, Sol (Codex) verifies; GitHub required checks | two provider families on one accepted subject | none |
| K7 delivery | `tools/repo_hygiene/brokered_closeout.py` (repo-closed postcondition), target `fork/master` | closeout blocks the final response until the repo is closed | NONE: no typed `CLOSURE_INCOMPLETE` state for accepted-but-undelivered work |
| K8 capacity | `lane-provider-refusal.ps1`; `docs/ROTATION.md` (rotation is an owner act) | last quota event and what in-flight work did | no automatic park/rotate |
| K9 resume | `.claude-state/RESUME.md`, OS task `MLV-BoardStateHeartbeat`, `tools/session-checkpoint.py` | heartbeat `-Status` and a fresh snapshot | resume state is board-local; survives account rotation, not machine loss |
| K10 inventory + parity | `~/.claude/machine-inventory.yaml`; `~/.claude/hooks/check-account-drift.ps1` at SessionStart | `probed_under`, parity verdict | machine-local; a probe can be derived and still wrong (PATH fault read as unavailability) |
| K11 honest reports | `CLAUDE.md` behavioural rule 1; `agents/error-remediation.md` | quoted rule lines | NONE: no tracked tool emits an R9 `posture:` line in this repo |
| K12 feedback | bus `adjudications/factory-kernel/mlv-app.md` | the filing and its harvest status | filed from this board; harvest pending |

Re-file weekly while the kernel is a CANDIDATE, and whenever the kernel or profile revision changes (PROMPT-K §6).
