# Playback Improvement Plan — Round 2 (executor handoff)

> **HISTORICAL EXECUTION PLAN.** Do not select new work from this document.
> Current priority and status live only in
> [the implementation roadmap](roadmap.md); this file preserves round-two
> specifications and validation history.

**Historical execution model (superseded by `roadmap.md`):** this used the same model as
[playback-improvement-plan.md](playback-improvement-plan.md) — each item is a self-contained spec
for an inexpensive executor session (Codex, or Claude Code on `/model sonnet`) with NO other
context. Executor prompt per item:
"Implement item N of docs/playback-improvement-plan-round2.md in C:\!Layi Wkspc\MLV-App, following
its Common Rules and the item's Validation section exactly. Return the diff and the validation
tables."
It used one item per session/work block, **in order** — item 0 was a blocking prerequisite for items 1-3.
A reviewer checked the diff and gate tables before finalize.

**Provenance:** derived from the round-1 review (2026-06-11). Round-1 verdicts: items 2a and 4
KEEP; 1a, 1b, 2b dead ends; item 3 dead-ended — but the round-2 review found that verdict UNSOUND:
both A/B arms failed the long-gap detector on the IDENTICAL `max_gap_ms=190289` because the
detector parsed the shared day-long trace log (`.claude-state/profiling/logs/mlvapp-20260611.log`)
and counted inter-run idle time as a mid-playback freeze. The actual playback evidence in both
arms was clean (`frozen_content_runs=0`, `hitch_frac=0`, max real interval 211 ms, flicker 0).
Artifacts: `.claude-state/profiling/20260611-item3-x1-smoke-current` / `-kill`.

**Current honest standings (M16 trio, 24 fps target):** x1 ~8-9, x2 Sharp ~13.5-15,
x4 ~19-22, x8 ~21-22.

## Common Rules (every item)

All Common Rules from [playback-improvement-plan.md](playback-improvement-plan.md) apply verbatim
(work blocks, build recipe + exe SHA, test clips, one-instance discipline, measurement rules,
STANDING GATES, ledger ATTEMPTS rows in
[playback-max-optimization-loop.md](playback-max-optimization-loop.md), fresh env reads,
ASCII-only .ps1). Additions for round 2:

- **Per-run trace isolation is mandatory:** every traced smoke run gets a FRESH `-OutputDir`; the
  detector must never be pointed at `.claude-state/profiling/logs/` (the shared day log). After
  item 0 lands, the detector enforces this itself.
- **Commit-subject honesty:** if an item dead-ends and its code is reverted, the work-block commit
  subjects must say so (e.g. "playback: attempt and revert X (item N, dead end)"). Round 1 left
  merge `6041816e` titled "implement half-res x1 playback proxy (item 3)" while containing no
  proxy code — do not repeat that.
- Items 1-3 are pixel-affecting and strictly PLAYBACK-ONLY: pause/scrub/export stay full quality.
  Pixel items end "pending user softness sign-off".
- **The playback-smoke acceptance gate is `PlaybackGatePolicy::evaluate`** (`platform/qt/PlaybackGatePolicy.h`),
  called once per session from `MainWindow::finishPlaybackSmokeTelemetry` with the session's
  cumulative frames-presented, decode-requests-issued, and GL-vs-oracle parity-match totals against
  the frames-expected target. Wall-clock fps is diagnostic only and must never gate a session; a
  session with an inactive parity probe fails closed (parity total 0) rather than passing on
  missing evidence.

## Item 0 — Detector session segmentation (harness fix; BLOCKING, no pixel changes)

**STATUS: LANDED 2026-06-11 (wb-d7392bd007c84a80) — all validations PASS; results in the ledger
("Round-2 Item 0 results"). No app trace line was needed: `run_metadata=` is already a guaranteed
per-launch first line. Scope grew by one harness defect found during validation: the smoke
wrapper's look-assist apply-evidence predicate was structurally broken since the async Look
Assist apply (master 8ddddce2) and is fixed in the same work block.**

Problem: `tools/profiling/detect-playback-artifacts.ps1` treats any >2000 ms gap with playback
events on both sides as a mid-playback freeze (FAIL). When the supplied `-TraceLog` contains
multiple app sessions (the per-day `mlvapp-<date>.log` appends across runs), inter-run idle time
is indistinguishable from a freeze and produces false FAILs — this killed round-1 item 3.

1. MAP first (read-only): determine what marker lines an app launch writes at the top of the
   crash-forensics/trace log (grep `MLVAPP_CRASH_FORENSICS_LOG_DIR` handling and the logger init
   in the app; quote file:line). If no reliable per-launch marker exists, add ONE trace line at
   startup (e.g. `app.session.start pid=<pid>`) — trace-only, no behavior change.
2. IMPLEMENT in `detect-playback-artifacts.ps1`:
   - Split the trace into sessions on the launch marker; analyze ONLY the latest session by
     default. Add `-Session <index|all>` for explicit control.
   - When more than one session is present in the supplied log, print a loud
     `MULTI-SESSION TRACE` warning naming the count and which session was analyzed.
   - In `run-release-gui-smoke.ps1`, keep passing the discovered log, but prefer the per-run
     `$logRoot` (already the default via fresh `-OutputDir`); no behavior change needed there
     beyond what falls out of the detector fix.
3. VALIDATE:
   - Re-run the detector against the archived round-1 logs that produced the false FAIL
     (`.claude-state/profiling/logs/mlvapp-20260611.log` if still present, else any multi-run day
     log): latest-session analysis must report NO 190289-ms-class gap; `-Session all` must
     reproduce the old behavior.
   - Synthetic check: concatenate two known-clean single-run logs into one file; detector on the
     concatenation (latest session) must match the verdict of the second log alone.
   - One fresh traced smoke run (any scale) end-to-end PASS, proving the harness still wires up.
   - No app pixel/timing changes: if a startup trace line was added, one traced smoke confirms
     fps within noise of the round-1 item-4 rows.

## Item 1 — Re-land the x1 half-res playback proxy (round-1 item 3, corrected gates)

**STATUS: LANDED 2026-06-12 (wb-88ca635c67fe4e4c) — KEEP both x1 modes, pending user softness
sign-off. Measured +13/+19/+29% Sharp and +50/+23/+19% Aggressive (on/off means per clip); the
13-15 fps expectation was NOT reached (full-res applyProcessingObject dominates the remaining
render — see the ledger results section). The diff was recovered from the round-1 executor
session rollout (it was never in git); its pre-existing-test edits were dropped as debugging
artifacts. New standing gate added mid-item: filmstrip + color-balance-trace visual sweep
(tools/profiling/filmstrip-balance-trace.ps1) beside every pixel item's measurement matrix.**

The implementation spec is **unchanged** from round-1
[playback-improvement-plan.md item 3](playback-improvement-plan.md): at scale==1 + playback
preview mode + not disabled: downsample bayer 2x (write the 2x variant of
`pl_downsample_bayer_to_bayer_4x` if absent, preserving the dual-ISO row phase via the same
16-aligned crop the quarter-res core uses) -> dual-ISO recon at half-res -> debayer -> bilinear
upscale (`mlv_rgb_u16_upscale_to_size`) -> path tag 6. Kill switch
`MLVAPP_DISABLE_HALFRES_X1_PREVIEW`. x1 prefetch stays OFF. Pause/scrub/export untouched.
Round-1 attempt context: work block `wb-6120f5d5c6074126` implemented this and reverted it on the
false detector FAIL; the prior diff may be recoverable from that work block's history — reuse it
if the tree state allows, otherwise re-implement to the same shape. Tests: clone the quarter-res
test shapes for x1 (path 6 engages under playback preview only; kill switch wins).

Expected: x1 ~8 -> ~13-15 fps.

VALIDATE (corrected vs round 1):
- Item 0 must be merged first; every run in a fresh `-OutputDir`.
- A/B interleaved on/off (kill switch), 2 reps, all three clips, x1 both quality modes. Cite
  `medianFps` AND presented fps from the SAME runs; single-clip deltas <20% are noise — the
  keeper case must hold on all three clips.
- `validate-visible-playback.ps1` at x1: span > 3, no glitch candidates (this is the gate that
  cleared round-1 item 4; it bypasses the long-gap ambiguity entirely).
- x2/x4/x8 no-regress singles; full STANDING GATES incl. x8 canary filmstrips on all three clips
  and the pipeline suite vs the 2026-06-10 baseline failure set.
- Softness: save one paused full-res frame vs one playback-proxy frame (same frame index) as PNGs
  for the user's softness sign-off; the lane ends "pending user softness sign-off".

## Item 2 — Re-A/B the indirect prefetch worker at x2 (post-quarter-res foreground)

**STATUS: DEAD END 2026-06-12 (wb-76d50eef9d8b472d) — keeper bar not met (+3/-24/+1% per clip).
The wash is budget-relative, not cost-relative: the quarter-res foreground (~33 ms) already fits
the 42 ms frame budget warm, so the worker buys render-ms but no median fps, and its core-split
slows warm runs (1347: on 13.0 vs off 22.2). Mechanism kept behind opt-in
MLVAPP_PREFETCH_INDIRECT_X2=1, byte-identity test-pinned, for future re-audits. Hit-rate when
enabled: 143/143 foreground requests served.**

Context: the processed8 indirect prefetch worker (ledger row wb-a5315ef858a645b2) is gated to
scale>=4 because at x2 the worker's render cost equaled the then-full-cost foreground render and
split cores (wash-to-negative). Item 2a has since made the x2 playback foreground quarter-res
(~15-25 ms). The contention math changed; the wash verdict's baseline has fallen
(kill-category: attribution/measurement — re-audit is legitimate per the dead-end audit rules).

1. MAP first: locate the scale gate in the worker task path in `src/mlv/video_mlv.c` (near
   `mlv_processed8_prefetch_indirect_enabled()` / the task-level gate around line ~2283; quote
   file:line) and confirm what resolution path the worker would render for an x2 playback-preview
   request post-2a (it must take the SAME quarter-res path the foreground takes — if it would
   render the old full-cost x2 path, that is the first thing to fix, mirroring the
   preview-policy-envelope hoist documented in the wb-a5315ef ledger row).
2. IMPLEMENT: extend the worker gate to include x2 Sharp playback preview, behind a fresh-read env
   `MLVAPP_PREFETCH_INDIRECT_X2` (default ON for the A/B; final default decided by the verdict).
   Aggressive x2 stays excluded (its incompatible lanes skip the main cache — pure contention,
   per the round-1 finding). No change to x4/x8 behavior.
3. VALIDATE:
   - Interleaved A/B on/off, 2 reps, all three clips, x2 Sharp. Keeper bar: >=15% median-fps win
     on at least 2 of 3 clips, no clip regressing beyond noise.
   - Worker-hit byte-identity test for the x2 quarter-res worker render (clone the existing
     worker-hit identity test shape).
   - x1/x4/x8 no-regress singles; prefetch hit-rate cited from `MLVAPP_PREFETCH_DEBUG`; full
     STANDING GATES. If the wash repeats, record DEAD END honestly and keep the scale>=4 gate.

Expected if it flips: x2 Sharp 13.5-15 -> toward 18-20.

## Item 3 — x1 proxy + prefetch composition (CONDITIONAL on items 1 and 2 both keeping)

**STATUS: N/A 2026-06-12 — prerequisite failed (item 1 KEPT, but item 2 dead-ended). Recorded in
the ledger; the budget-relative wash mechanism applies a fortiori at x1 (foreground 62-96 ms,
already over budget). Not attempted, per the plan.**

Only if item 1 kept AND item 2 flipped to a win: A/B the indirect worker at x1 with the half-res
proxy active (worker renders the same half-res proxy path; env `MLVAPP_PREFETCH_INDIRECT_X1`,
default off until the verdict). Same gates as item 2 scaled to x1. Expected: x1 13-15 -> toward
18+. If either prerequisite failed, record this item N/A in the ledger — do not attempt.

## Item 4 — Stall-tail diagnosis (read-mostly; report deliverable, no keeper expected)

**STATUS: DELIVERED 2026-06-12 (wb-767c872b724749f5) — the tail is PROCESSING (94-99% of
slow-decile render on all four runs), not decode/queue/present. Full attribution table + the
three ranked levers are in the ledger ("Round-2 Item 4 results"). Note: MLVAPP_PHASE3_TEL_PATH
was inert in normal playback until this item's trace-only opener fix; use playback_smoke.frame
telemetry + tools/profiling/analyze-frame-telemetry.py for attribution, not the stage CSV.**

The remaining cross-scale perceptual defect is the stall TAIL, not the median: known
`m_frameStillDrawing` render-thread busy spikes of 80-209 ms on some frames (round-1 era
diagnosis), which median fps hides.

1. Use the existing per-frame stage telemetry (`MLVAPP_PHASE3_TEL_PATH` env — per-frame stage CSV,
   NO rebuild required) during traced x1 and x2 Sharp playback on M16-1327 and M16-1347.
2. Deliverable: a table attributing the p90/p99 frame time to stages (decode / recon / debayer /
   processing / upscale / draw) per scale, plus the top-3 candidate levers ranked by expected
   p99 reduction, each with a one-line "holds only if...". Append to the ledger as a diagnosis
   row (no KEEP/DEAD-END — instrument rows take verdict N/A).
3. No code changes in this item beyond env-gated instrumentation if a stage is missing from the
   CSV (any addition must be trace-only and behind an env check, default off).

## Reviewer checklist (per item, before finalize)

1. Read the full diff — reject scope creep beyond the item's files/shape.
2. Check validation tables against the gate thresholds; for items 1-3 confirm the A/B was
   interleaved and per-run-isolated (item 0 must be in the build).
3. View x8 canary PNGs (all three clips) for items 1-3; view the x1 softness pair for item 1.
4. Confirm ledger ATTEMPTS rows + CURRENT updates are written and honest.
5. Confirm commit subjects describe the actual outcome (incl. reverts).
6. Finalize the work block; pixel items end "pending user softness sign-off".
