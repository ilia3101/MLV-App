# MLV-App product roadmap

Seeded by DEFINITIVE-FIX-PLAN-20260906.md step 0.18. This is a compact mirror of the
PHASE 1 product backlog (`.claude-state/coordination/dual-lane/queue.json`, `kind` ∈
`product`/`playback`). The queue is the dispatch authority; this file is a tracked,
hosted-CI-visible pointer to it — `.claude-state/` is gitignored and absent from every
hosted checkout, so this is the only place a reviewer without local state can see the
backlog shape. Where this file and the queue disagree, the queue wins; re-derive from
it, do not edit priorities here.

Each row's procedure lives in `docs/lane-prompts/v2/` (the tracked mirror of the
ratified card/fields files). `USECASE-1` is a closure receipt only — never dispatched,
not listed below.

| P | card | kind | procedure |
|---|---|---|---|
| 1 | PROD-TLS-VERIFY-1 | product | `card-PROD-TLS-VERIFY-1.md` |
| 1 | PROD-TLS-VERIFY-1b | product | `fields-PROD-TLS-VERIFY-1b.md` |
| 2 | PROD-CLAUDEMD-TRUTH | product | `card-PROD-CLAUDEMD-TRUTH.md` |
| 3 | HYG-PROFILING-MEDIA-1 | product | `card-HYG-PROFILING-DNGS-1.md` |
| 4 | PROD-ENVFLAG-1 | product | `fields-PROD-ENVFLAG-1.md` |
| 5 | PROD-DUALISO-GUARD-TEST | product | `fields-PROD-DUALISO-GUARD-TEST.md` |
| 6 | PROD-TOOLCHAIN-1 | product | `fields-PROD-TOOLCHAIN-1.md` |
| 7 | PLAY-COUNTERS-CPU-A | playback | `fields-PLAY-COUNTERS-CPU-A.md` |
| 7 | PLAY-COUNTERS-CPU-B | playback | `fields-PLAY-COUNTERS-CPU-B.md` |
| 7 | PLAY-COUNTERS-GPU | playback | `fields-PLAY-COUNTERS-GPU.md` |
| 8 | PLAY-C2-SUBMIT-2-ACCEPT | playback | `fields-PLAY-C2-SUBMIT-2-ACCEPT.md` |
| 9 | PROD-UPSTREAM-SYNC-1 | product | `fields-PROD-UPSTREAM-SYNC-1.md` |
| 10 | PROD-CDNG-DECOUPLE-1 | product | `fields-PROD-CDNG-DECOUPLE-1.md` |
| 11 | HYG-EVIDENCE-RATCHET-1 | product | `fields-HYG-EVIDENCE-RATCHET-1.md` |
| 12 | PROD-BATCHTYPES-SPLIT-1 | product | `fields-PROD-BATCHTYPES-SPLIT-1.md` |
| 14 | PROD-README-FORK-1 | product | `fields-PROD-README-FORK-1.md` |

16 cards. Parity with `docs/lane-prompts/v2/*` (every file carrying exactly one
`ALLOWED_PATHS:` line is dispatchable and must appear above; every row above must name
a file that exists there) is enforced in CI by
`tools/repo_hygiene/test_roadmap_queue_parity.py`. The queue-side check (`kind`,
`track == kind`, `scope`, on `queue.json` itself) is
`tools/coordination/check_roadmap_queue_parity.py --queue <path>`, run locally by the
hub — `.claude-state/` is not visible to hosted CI so this half cannot run there.
