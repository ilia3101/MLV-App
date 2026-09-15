# FIELDS for product-card-TEMPLATE.md — composed by the dispatcher; both files are in the ratified manifest
CARD_ID: PLAY-COUNTERS-CPU-A
PRIORITY: 7
CLIP_OR_NONE: none
ALLOWED_PATHS: platform/qt/PlaybackGatePolicy.h, platform/qt/MLVApp.pro, tests/console/test_playback_gate_policy.cpp, tests/console/console_tests.pro, tools/repo_hygiene/test_playback_gate_wiring.py

DELIVERABLE:
POLICY ONLY; no MainWindow or RenderFrameThread change (that is PLAY-COUNTERS-CPU-B, which depends on this card).
Add a header-only `platform/qt/PlaybackGatePolicy.h` (`QT += core`), listed in `MLVApp.pro` HEADERS. Its pure function takes CUMULATIVE SESSION totals:
frames presented, decode requests issued, parity matches, and frames expected. It returns PASS or FAIL deterministically.
Wall-clock fps may be reported but never gates the result. The policy is evaluated ONCE per session, never per frame.
REFERENCE ONLY, never a branch base: `refs/archive/lane-wip/PLAY-COUNTERS-CPU-20260914T151951Z` (829b573e) holds an earlier
attempt with four confirmed defects: per-frame evaluation, a function-local static parity counter, a wiring test that
could not fail, and a queue-depth input. You may read its PlaybackGatePolicy.h, but do not copy those defects.

ACCEPTANCE:
- `tests/console/test_playback_gate_policy.cpp`, in `console_tests.pro` (header in HEADERS), must:
  - PASS on a full-parity, all-presented vector.
  - FAIL when presented < expected, and when parity < presented.
  - Cover the L-CHEAT case: one input replaced by a constant equal to expected must still FAIL, because another total
    disagrees.
  Prove the test can fail by inverting one comparison once.
- `tools/repo_hygiene/test_playback_gate_wiring.py` has two parts:
  - A checker function that parses the unique `PlaybackGatePolicy::evaluate` call in a given C++ source text and rejects
    numeric-literal arguments.
  - A test that calls THAT CHECKER on an in-test fixture holding a literal argument and asserts it is rejected. A string
    regex matched against the literal alone is not enough.
  While no call site exists in `platform/qt/MainWindow.cpp` (this card adds none), the real-file assertion is skipped with
  a reason naming PLAY-COUNTERS-CPU-B.
- Tests: build and run ONLY the console test target and this one pytest file, not the full suite (lane bound 65 turns /
  1500 s).

VERIFY_FIRST:
git -C . ls-tree {{BASE_SHA}} -- platform/qt/PlaybackGatePolicy.h     # empty today; if present, STOP with ALREADY-SHIPPED
git -C . show refs/archive/lane-wip/PLAY-COUNTERS-CPU-20260914T151951Z --stat   # reference only