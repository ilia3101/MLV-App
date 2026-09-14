# MLV-App Working Index

## Document Map

Standing September 7 workflow: automatically remediate recoverable errors with
hub/wisdom adjudication and a bounded read-only Luna swarm, preserving authority
gates. Follow [agents/error-remediation.md](agents/error-remediation.md).
Standing September 8 tiering: Fable reviews, Opus hubs and adjudicates, Sonnet
implements, Haiku reports, Sol/Luna review and recon. Follow
[agents/orchestration-tiering.md](agents/orchestration-tiering.md).

Standing September 9 owner ruling: Agent Bridge product SoT is
[`layibabalola/agent-bridge`](https://github.com/layibabalola/agent-bridge); suspend in-tree `tools/agent-bridge/` feature/bugfix/refactor/CI churn.
Factory Bridge stays integration smoke only. Follow
[agents/agent-bridge-source-of-truth.md](agents/agent-bridge-source-of-truth.md).

Index only; details live in claude/. Policy: [fragmentation limits](docs/22-doc-fragmentation-policy.md), 8KB soft/12KB hard. Check with:

```bash
py -3 tools/docs/check_pinned_tokens.py
```

Section headings are stable IDs. Demoted sections and where they went:

| Section (stable ID) | Now lives in |
|---|---|
| Agent Bridge — Session Closeout | [claude/session-closeout.md](claude/session-closeout.md) |
| Architecture (Locked — Do Not Deviate) | [claude/batch-cli-spec.md](claude/batch-cli-spec.md) |
| Target Export Format | [claude/batch-cli-spec.md](claude/batch-cli-spec.md) |
| Settings / Receipt Strategy (PHASED — Critical Design Decision) | [claude/batch-cli-spec.md](claude/batch-cli-spec.md) |
| Build Environment | [claude/batch-cli-spec.md](claude/batch-cli-spec.md) |
| Key Technical Constraints | [claude/batch-cli-spec.md](claude/batch-cli-spec.md) |
| File Structure | [claude/batch-cli-spec.md](claude/batch-cli-spec.md) |
| Implementation Phases (Execute In Order — Do Not Skip Ahead) | [claude/batch-cli-spec.md](claude/batch-cli-spec.md) |
| Exit Code Reference | [claude/batch-cli-spec.md](claude/batch-cli-spec.md) |
| CLI Usage (Target) | [claude/batch-cli-spec.md](claude/batch-cli-spec.md) |
| .NET Orchestrator Integration (Later — Not Claude Code's Job) | [claude/batch-cli-spec.md](claude/batch-cli-spec.md) |

### Pinned contract tokens

`tools/repo_hygiene/brokered_closeout.py` and `tools/repo_hygiene/test_brokered_closeout.py` assert these exact substrings against this file; losing one raises `closeout_tooling_stale` or reds the test suite. They stay resident here as stable IDs even though the surrounding prose was demoted. The list is derived from both surfaces, never hand-maintained -- re-derive and re-check with:

```bash
py -3 tools/docs/check_pinned_tokens.py
```

- `/api/closeout/actions` -- detail in claude/session-closeout.md
- `/api/closeout/actions/preview` -- detail in claude/session-closeout.md
- `/api/closeout/actions/requests` -- detail in claude/session-closeout.md
- `action-request-history` -- detail in claude/session-closeout.md
- `agentRemediationQueue.queueRoots` -- detail in claude/session-closeout.md
- `boundedRunnerExitCodes` -- detail in claude/session-closeout.md
- `canonical dashboard contract` -- detail in claude/session-closeout.md
- `Closeout actors must be bounded at the process boundary` -- detail in claude/session-closeout.md
- `Closeout Remediation Freeze` -- detail in claude/session-closeout.md
- `closeoutCleanTruth` -- detail in claude/session-closeout.md
- `dashboard-action-requests` -- detail in claude/session-closeout.md
- `Evidence-preserving transaction prune` -- detail in claude/session-closeout.md
- `Hard-clean final responses are blocked unless the repo-closed postcondition passes` -- detail in claude/session-closeout.md
- `human content-review gate, which remains mandatory in both phases` -- detail in claude/session-closeout.md
- `is now active (`enabled=true`,
`requireReadyForFinalize=true`)` -- detail in claude/session-closeout.md
- `non-authenticating process evidence and never replace` -- detail in claude/session-closeout.md
- `PowerShell 7+` -- detail in claude/session-closeout.md
- `protected-target-dirty-recovery` -- detail in claude/session-closeout.md
- `protected-target-noop-closeout` -- detail in claude/session-closeout.md
- `repo_closed_for_final_response` -- detail in claude/session-closeout.md
- `repoClosedAuditHash` -- detail in claude/session-closeout.md
- `repoStateLedger` -- detail in claude/session-closeout.md
- `rollbackPolicy` -- detail in claude/session-closeout.md
- `round-delta note` -- detail in claude/session-closeout.md
- `semantic success authority` -- detail in claude/session-closeout.md
- `start-closeout-dashboard.ps1` -- detail in claude/session-closeout.md
- `tests, and tooling-baseline inventory are
already mandatory` -- detail in claude/session-closeout.md
- `validate-rollback-manifest.ps1` -- detail in claude/session-closeout.md
- `was deliberately dormant (`enabled=false`, `requireReadyForFinalize=false`)` -- detail in claude/session-closeout.md
- `webDashboardSpec` -- detail in claude/session-closeout.md
- `workBlockBootstrap.autoBranchFromProtectedTarget` -- detail in claude/session-closeout.md
- `workBlockBootstrap.requireIntegratedStartHeadForFinalize` -- detail in claude/session-closeout.md
- `worktree-inspection.v1` -- detail in claude/session-closeout.md

## Purpose
Batch CDNG CLI Phases 0-6 are SHIPPED (`66549181`). Evidence: `receiptOpt` in `platform/qt/main.cpp`, `skipped=` logging in `src/batch/BatchRunner.cpp`. Next product work only from [roadmap](docs/roadmap.md).

---

## Memory Policy (project-level content, machine-level pointers)

Durable memory CONTENT lives at the PROJECT level. Write findings, methodologies,
and cross-session continuity notes to `.claude-state/project-memory/<slug>.md` and
index them in `.claude-state/project-memory/README.md`. The machine-level memory
store (`~/.claude/projects/<proj>/memory/`, indexed by `MEMORY.md`) holds ONLY
pointer entries: a strong `description:` line for description-based recall plus a
short body that links to the canonical `.claude-state/project-memory/...` file. Do
NOT write bulky content into the machine store — it is the recall index, not the
system of record; duplicating content there causes drift. Exceptions that may stay
machine-only: cross-project `user`/`feedback` working preferences and
machine-specific facts (host access, local paths). When saving something new:
(1) write full content to `.claude-state/project-memory/`, update that README;
(2) create/keep a slim machine-level pointer and index it under the right
`index-*.md`.

---

## Agent Bridge — Session Startup (Hook-Driven)

This repo uses an agent-bridge to coordinate with a peer Codex session. The
`SessionStart` hook in `.claude/settings.local.json` runs
`tools/agent-bridge/bootstrap_session.py` automatically at the start of every
session — its stdout is injected into your context. The bootstrap registers
this session as the active Claude bridge owner (superseding any older Claude
session), drains messages from the previous session, sends a HANDSHAKE to
Codex, and updates the watcher config.

After the hook fires, do these in order:
1. **Read `drained_previous_messages`** in the hook output — surface any unread
   messages from the previous session to the user before proceeding.
2. **Use the returned `session_id`** as your active Claude bridge GUID for this session.
3. **If `check_inbox` returns a `SESSION_UPDATE: superseded` control message at any
   point**, stop all bridge sends immediately — a newer Claude session has taken over.
4. **Read `active_session_unread`** in the hook output — these are unread rows
   already sitting in the new active session bucket. Surface them, then mark each
   read by id after handling.
5. **Start the bridge Monitor** — the Monitor is Claude's inbox wake mechanism and does
   NOT survive context compaction. Start it every session, no exceptions:
   ```
   Monitor(persistent=True, command="<python> -u tools/agent-bridge/bridge_monitor_poll.py --state-dir <bridge-state-dir> --agent claude --session-id <active-guid> --project mlv-app --poll-interval-seconds 2")
   ```
   Use `bridge_monitor_poll.py` for the Monitor. Do not substitute
   `probe_server.py`; probes are diagnostics and will not keep Claude's inbox
   wake path armed. Before saying "waiting for Codex," verify the Monitor task
   is active. If no Monitor is running, start one before waiting.
   If a bridge message arrives with `TYPE: CONTROL` and
   `SUBJECT: MONITOR_RESTART_REQUIRED`, stop any stale Monitor task handle and
   immediately start a fresh `bridge_monitor_poll.py` Monitor with the command
   shown in the message. The watcher sends this control when it detects a stale
   or missing Monitor heartbeat, or when bridge Monitor-related code changes.

When a Monitor notification fires, call `mcp__agent-bridge__check_inbox` with
`agent=claude`, `session_id=<active-guid-or-mlv-app>`, `mark_read=False`, then mark
each message read explicitly by id.

If the hook output is missing from your session-start context (broken JSON,
deleted file, hook failure), fall back to running `bootstrap_session.py`
manually with the command stored in `.claude/settings.local.json`.

Bridge protocol details: `tools/agent-bridge/BRIDGE_PROTOCOL.md`
Hardening plan and audit log: `tools/agent-bridge/BRIDGE_HARDENING.md`

---

## Behavioral Rules for Claude Code

1. **No speculation** — search the repo and quote exact code before writing
2. **No new exporter APIs** — reuse the existing CDNG export code path
3. **Smallest diff possible** — surgical changes only
4. **Show full diffs** before applying to any existing file
5. **One phase per response** — do not jump ahead
6. **Compile after every change** — `cd platform/qt && qmake && mingw32-make -j8`
7. **Never use QCoreApplication** — always QApplication
8. **Never create circular includes** — BatchTypes.h is the shared type header
9. **Treat CDNG as frame-sequence** — per-frame error handling, subfolder output
10. **CDNG likely does NOT use FFmpeg** — Phase 0 must prove this with evidence
11. **No receipt parsing until Phase 6** — use defaults for Phases 0-5
12. **Use BatchPrompts helper class** — no inline if/else for dialog replacement
13. **Patch only CDNG export path dialogs** — do NOT globally disable message boxes

---
