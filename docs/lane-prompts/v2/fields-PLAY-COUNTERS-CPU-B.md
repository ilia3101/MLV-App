# FIELDS for product-card-TEMPLATE.md — composed by the dispatcher; both files are in the ratified manifest
CARD_ID: PLAY-COUNTERS-CPU-B
PRIORITY: 7
CLIP_OR_NONE: none
DEPENDS_ON: PLAY-COUNTERS-CPU-A
ALLOWED_PATHS: platform/qt/MainWindow.h, platform/qt/MainWindow.cpp, platform/qt/RenderFrameThread.h, platform/qt/RenderFrameThread.cpp, tools/repo_hygiene/test_playback_gate_wiring.py, docs/playback-improvement-plan-round2.md

DELIVERABLE:
Depends on PLAY-COUNTERS-CPU-A (PlaybackGatePolicy.h merged). Wire the gate to CUMULATIVE session totals.
- `render_thread_decode_request_count_at_request` is NOT a total. It is `m_decodeRequests.size()`, the live in-flight
  queue depth (`RenderFrameThread.cpp` around line 1054). Add a monotonic decode-requests-issued counter to RenderFrameThread,
  incremented where a decode request is enqueued, with a thread-safe read accessor. Leave the existing queue-depth
  telemetry field unchanged.
- Add a MainWindow MEMBER parity-match counter. Never use a function-local static.
- In `beginPlaybackSmokeTelemetry`, reset the parity member alongside `m_playbackSmokePresentedFrames`; do NOT reset the RenderFrameThread counter - snapshot it into a new `m_playbackSmokeStartDecodeRequestsIssued` member there (the existing `m_playbackSmokeStartRequestSerial` pattern, MainWindow.cpp:22237) and use counter-minus-snapshot as the session total. The only enqueue site is `m_decodeRequests.push_back` (RenderFrameThread.cpp:1855).
- Call `PlaybackGatePolicy::evaluate` EXACTLY ONCE, in `finishPlaybackSmokeTelemetry`, with the three session totals plus a frames-expected input (`m_playbackSmokeTargetPresentedFrames`). Never call it
  in `notePlaybackSmokePresentedFrame`.
- Update the round-2 plan doc's "Common Rules" to name the policy as the gate.

ACCEPTANCE:
- `tools/repo_hygiene/test_playback_gate_wiring.py` un-skips the real-file assertion. The unique evaluate call must sit
  inside `finishPlaybackSmokeTelemetry`, must not sit inside `notePlaybackSmokePresentedFrame`, and its arguments must be
  the member totals or member-minus-start-snapshot deltas (no literals, no `decodeRequestCountAtRequest`).
- A source-contract assertion checks that the new decode counter is only ever incremented, never assigned from `.size()`.
- The gate FAILS a session whose GL parity probe was inactive (parity total 0 < presented): the smoke must run with the probe active. Never PASS on missing parity evidence.
- `Batch Compile` compiles the call site. Build the Qt target once; do not run the full suite (lane bound 65 turns /
  1500 s).

VERIFY_FIRST:
git -C . grep -n "decodeRequestCountAtRequest =" {{BASE_SHA}} -- platform/qt/RenderFrameThread.cpp
git -C . grep -n -E "void MainWindow::(begin|finish)PlaybackSmokeTelemetry|void MainWindow::notePlaybackSmokePresentedFrame" {{BASE_SHA}} -- platform/qt/MainWindow.cpp
git -C . ls-tree {{BASE_SHA}} -- platform/qt/PlaybackGatePolicy.h     # must be PRESENT (CPU-A merged); if absent, STOP with DEPENDENCY-UNMET