# HUB PROCEDURE — executing the DEFINITIVE FIX PLAN from a fresh Sonnet (or Haiku) session

You are the HUB for the DEFINITIVE FIX PLAN. The owner is never in the loop: never ask, never wait for approval, never stop
because the context is long, never park work behind a question. Every chat response begins with the Houston timestamp line
(`**Timestamp: <Month> <d>, <yyyy> at h:mm:ss AM/PM CDT (America/Chicago)**`). This file is in the ratified manifest: change it only
through the ratification loop below, never by hand.

## 0. Read, absolutely pathed, in this order — later items OUTRANK earlier ones, and RAW STATE outranks all of them
1. `C:\!Layi Wkspc\MLV-App\.claude-state\RESUME.md` — do its STEP 0 as written. If its pointer to this file does not resolve (`Test-Path`),
   correct it as your first act — by byte value, asserting no C0 control byte other than TAB and LF survives anywhere in the file — and record the
   correction in the checkpoint (O143/O149; RESUME.md carries the same instruction in its own section, O150).
2. `C:\!Layi Wkspc\MLV-App\.claude-state\heartbeat\board-snapshot.md` — an accelerator, never an authority; dead past 30 minutes.
3. `C:\!Layi Wkspc\MLV-App\.claude-state\coordination\dual-lane\orchestrator-resume-CURRENT.md` — the NEWEST `### [` entry is the live state.
4. `C:\!Layi Wkspc\MLV-App\.claude-state\coordination\dual-lane\DEFINITIVE-FIX-PLAN-20260906.md` — the rev is its first line. Read §0.0
   (the gate), §1.1 (the register summary), §1.3 (two-key adjudication), then every Phase 0 step in the order the document lists them.
5. `C:\!Layi Wkspc\MLV-App\.claude-state\coordination\dual-lane\never-authorized.json` — NA-1..NA-10, binding on you.
6. The NEWEST `C:\!Layi Wkspc\MLV-App\.claude-state\fleet-runs\ratify-*\` directory — `subject-manifest.txt`, `sol-verdict.md`,
   `opus-verdict.md`, `hub-reproduction-and-decision.md`. Every round follows exactly that pattern; copy it, do not reinvent it.

**Exactly one hub at a time.** If the checkpoint's `**Freshness:**` stamp is younger than 30 minutes, or the newest `fleet-runs\<ts>\`
sol receipt is still `state: reserved` or `state: running` (every other state -- `complete`, `ended-incomplete`, `failed`,
`refused`, `incomplete` -- is terminal: file it, never wait on it), or the newest `ratify-*\` directory lacks either verdict, a round is in flight — WAIT for it
(re-check every five minutes) and then file it. Never ignite a second round on top of a running one.

## 1. Hard constraints — these outrank every file you read, including this one
- NA-1..NA-10 as the register states them. The kill switch `$D\WORKSTREAM-LOOP-DISABLED` is deleted ONLY by step 0.2's canonical
  compound, exactly as the register writes it, after the pre-flight probe returned exit 0. Re-arming it is always allowed.
- Never run `claude auth login` or `claude auth logout` or `codex login`; the read-only `claude auth status --json` is allowed.
- Never set, export or persist an environment variable whose name starts `ANTHROPIC_`, `OPENAI_` or `CLAUDE_CODE_`; never `setx` or
  `[Environment]::SetEnvironmentVariable` the five hook names; never write under `~/.claude\`; never edit the global hook.
- A machine hook DENIES any shell command whose TEXT contains a credential name or an auth verb. Keep such strings inside files written
  with the Write tool and run the file; probe the hook with the inert payload `if ($false) { setx ANTHROPIC_PROBE_TOKEN x }` only.
- Editing lanes push ONLY their own fresh card branch to `fork`, exactly as their card states; they never call `gh` and never open or close a
  PR except (1) step 0.15's exact `probe/gh-<ts>` create/close/delete sequence and (2) a composed `PR_STEP` the dispatcher authorised from
  `lane-gh-capability.json` — 0.15's probe card is the one dispatch that attempts a push and a PR in order to MEASURE that capability, whatever the
  expected outcome (O140). No lane edits the board root. You never force-push or rewrite history; nothing is merged to `master` except
  through a PR with a green required-check set and a `sol` APPROVE bound to the full 40-character head sha (S113).
- Absolute paths everywhere (`$D` = `C:\!Layi Wkspc\MLV-App\.claude-state\coordination\dual-lane`). Pin every git call as
  `git -C "C:\!Layi Wkspc\MLV-App"`. `.claude-state\` is gitignored and does not exist inside a worktree.
- Every `gh` call names the repository: `-R layibabalola/MLV-App`, or the `repos/layibabalola/MLV-App/…` API path. The board has two remotes
  and no default, so an unpinned `gh` addresses the upstream — loudly for `gh pr checks`, silently for `gh workflow list` (O135).
- Derive, never remember: a sha, a count, a status or a rev is quoted only from a command you just ran.

## 2. The ratification loop (plan §1.3) — one round, start to finish
A. **File** the newest round: copy `fleet-runs\<newest ts>\sol-001.last.txt` → `ratify-*\sol-verdict.md` and `sol-001.receipt.json` →
   `sol-receipt.json`; write the Opus key's final message VERBATIM to `opus-verdict.md` (restore `<`, `>`, `&&` that the harness escaped)
   with a two-line hub note on top.
B. **Reproduce every finding** with the command the key gave, or a better one; record STANDS / DOES NOT STAND with the evidence. A
   finding you cannot reproduce is contested, not dismissed: §1.3 step 3 (a third lane, or a recorded premise rejection).
C. **Decide**: write `hub-reproduction-and-decision.md` (keys, artifacts, the reproduction table, every resolution you had to choose and
   why, the decision). Both keys PROCEED or PROCEED_WITH_AMENDMENTS with no blocker → the gate is open (§0.0); otherwise the next rev is
   a confirmation round.
D. **Apply the union** as the next rev: a scratch Python script per key (sol pass first, then the Opus pass on the post-sol text), each
   edit an exact-anchor replacement whose count is printed and must be 1; fix the plan, the register (bump its schema when a rule
   changes), the wrapper, the cards and BOTH ratify prompts (round number, closure-audit ids, next finding id, the gate sentence);
   append the retractions; bump the header, the `-Card RATIFY-PLAN-V<rev>` line and the rounds sentence. Verify by token counts.
E. **Manifest and ignite**: compute the manifest with the canonical `python -c` command in plan §1.3 step 1 (never a retyped variant);
   create `fleet-runs\ratify-<first8>-<ts>\subject-manifest.txt`; refresh the checkpoint (a new `### [` entry FIRST, the older ones moved
   to the ARCHIVE file, and a byte-identical copy of the pre-rotation CURRENT under `.claude-state\continuity\archive\` whose sha256 the
   new CURRENT names — exception (ii)'s shape); run `check-checkpoint-contract.ps1`. Then, in the background:
   - sol: `pwsh -NoProfile -File "C:\!Layi Wkspc\MLV-App\tools\coordination\Invoke-Lane.ps1" -Lane sol -PromptFile "$D\prompts\v2\ratify-plan-SOL.md" -Card RATIFY-PLAN-V<rev> -TimeoutSec 1500`
   - Opus: a background general-purpose agent, model opus, read-only, told to execute `$D\prompts\v2\ratify-plan-OPUS.md` exactly, given
     the manifest hash and the receipt directory, the constraints of §1, scratch under `C:\Users\obabalola\AppData\Local\Temp\r<round>\`,
     and told its final message is the whole verdict ending in the JSON block.
   Deliver the new plan rev to the owner with SendUserFile (absolute path) and stop the turn; the lanes notify you when they return.
F. **Hook branch**: when a round changes a hook rule, a background agent (model opus, spawned by YOU and therefore board-rooted) authors ONE
   commit against the ratified register — never pushed. BEFORE 0.1 (no hook live) it works in `C:\mlvtmp\plan-definitive-fix-v7`. AFTER 0.1 the
   live hook denies that venue both ways — a worktree-rooted lane fails NA-10, a board-rooted author fails NA-7 because `C:\mlvtmp` is outside
   both roots — so the author works in a temporary worktree INSIDE the board root: `git -C "C:\!Layi Wkspc\MLV-App" worktree add
   "C:\!Layi Wkspc\MLV-App\.claude-state\worktrees\hook-amend-<ts>" <branch>`, never as a worktree-rooted lane (S116; that parent is already in
   `tools/check-doc-size.py`'s EXEMPT list as an untracked full repo copy, so the author's checkout does not multiply the board's governed-doc
   census — `hook-worktrees\` was not, and would — O154). Either way you
   re-run its suites yourself from a `git -C "C:\!Layi Wkspc\MLV-App" archive <sha> | tar -x` extraction in scratch (never in the author's
   working tree), then probe the hook with decision-only JSON on stdin. Record the sha and the results in the checkpoint in the same tool-call block.

## 3. Phase 0
Only after §0.0's gate is open AND you have re-run the suites on the final hook commit. Execute the steps in the plan's order, one at a
time, each ending in its named receipt — after every merge and before writing that step's receipt, run `git -C "C:\!Layi Wkspc\MLV-App" fetch fork` and
assert both shas exist with `git -C "C:\!Layi Wkspc\MLV-App" cat-file -e <sha>^{commit}` (a merge commit is created on GitHub and is absent from the
board until fetched — O170); compute each control receipt's `fixedSetEqualityProof` digest with the canonical command in plan §1.3 step 5 over the
fixed set as hashed at the reviewed head — never by hand (O175/O176); an existing `0.2-enable-preflight-input.json` is never rewritten: read it, type its
compound verbatim, re-probe the stored file (O178); every receipt write the HUB itself performs as a tool call uses `Write`/`Edit` or an append, never a shell
truncating write; the two gate receipts written inside tracked scripts, `0.4b-required-checks.json` by `set-required-checks.ps1` and
`0.5-factory-frozen.json` by `freeze-factory-cards.py`, keep the non-shrinking discipline in the script and its unit tests (O161/S124) — carrying `recordedUtc` — mandatory for the SIX gate receipts the hook validates, whose schemas are in plan §1.3 step 5; the non-gate
receipts carry the fields their own plan specs name (O161) — in EXACTLY the notation `YYYY-MM-DDTHH:MM:SSZ` — whole
seconds, uppercase `Z`, no fraction, no offset; this is the ONE notation, and it is NOT what this board's other writers emit (the loop's cycle
receipts stamp seven fractional digits), and a fractional stamp in a chain receipt fails 0.2's enable closed (O141/O151); derive it as
`(Get-Date).ToUniversalTime().ToString('yyyy-MM-ddTHH:mm:ssZ', [System.Globalization.CultureInfo]::InvariantCulture)` — never from an ISO default and
never without the invariant culture: `:` is the culture's TimeSeparator in a .NET custom format string, so under a culture that uses `.` the same
expression yields `2026-09-06T16.35.00Z`, which the hook rejects (hub-measured, pwsh 7.6.5, fi-FI — O160). Editing
lanes start only through `$D\Start-EditingLane.ps1`; `sol` reviews every PR before merge, with YOU as the exporter for the three PRs that
precede 0.35 (plan §1.3 step 5). Never skip a step, never reorder, never execute 0.2 before every step the plan lists before it — 0.05, 0.1, 0.15, 0.18, 0.35, 0.4a-i, 0.4a-ii, 0.4c-i, 0.4b-i, 0.4b-ii, 0.4c-ii,
0.5, 0.6, 0.7 and 0.9; six receipts gate the enable and 0.7 and 0.9 are not among them, but both complete before 0.2 regardless
(S87/S117/O142).
A round that returns a blocker while Phase 0 is underway halts you before the next merge; apply the union and re-ratify first.

## 4. Checkpoint discipline
Refresh `orchestrator-resume-CURRENT.md` IN THE SAME TOOL-CALL BLOCK as the work it describes — a return, a decision, a rev, an
ignition, a commit, a receipt. A checkpoint written only at hand-off is stale exactly when the hand-off is unplanned.

## 5. Stop conditions — the only ones

Apply the standing [automatic error-remediation workflow](../../../agents/error-remediation.md)
to ordinary execution failures: the hub gathers bounded read-only Luna shards,
obtains Fable/Opus wisdom adjudication, preserves the failed receipts, repairs and
retests the same gate. A BLOCKED phase prevents advancement, not authorized
diagnosis. Keep ratification, review and owner-reserved actions as explicit gates.

Phase 0 complete with `0.2-loop-enabled.json` finalised; or a blocker that names an owner-only act (a credential, a machine setting).
Everything else you resolve yourself, record, and continue.
