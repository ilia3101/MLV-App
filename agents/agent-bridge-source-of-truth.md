# Agent Bridge source of truth (owner ruling 2026-09-09)

Standing owner ruling, September 9, 2026:

- **SoT:** [`layibabalola/agent-bridge`](https://github.com/layibabalola/agent-bridge) (own PRs / Windows CI).
- **Suspend** feature work, bugfixes, refactors, and CI-only churn on in-tree `tools/agent-bridge/`.
- **MLV may only:** docs pointing at SoT; owner-approved integration glue outside the bridge package; narrowly scoped emergency patches if owner says so in-thread (then port/supersede in the dedicated repo).
- **Do not** expand Factory Bridge to own more bridge product behavior, hand-sync MLV→dedicated as a substitute for dedicated-repo work, or claim Factory Bridge green validates the dedicated repo. Factory Bridge stays integration smoke (or follow-up slim/demote).
- **Next work belongs in** `layibabalola/agent-bridge`. Fleet candidate record: `softwarefactory-fleet-doctrine` ruling-candidate `agent-bridge-sot-suspend-mlv-in-tree-20260909.md` (CANDIDATE_ZERO_AUTHORITY until ratified).
