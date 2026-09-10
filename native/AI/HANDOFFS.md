# Handoffs

A handoff is required whenever you stop with work in progress, discover
something the next agent must know, or finish a task that changes interfaces.

## Protocol

1. Copy `templates/handoff.md` to `handoffs/YYYY-MM-DD-<task-id>-<slug>.md`.
2. Fill every section. If a section does not apply, write "n/a" — do not delete
   the heading; the structure is what makes handoffs skimmable.
3. Update `TASKS.md` (status, files, blockers).
4. Commit the handoff together with the work (or as its own commit if you are
   stopping mid-task and the tree must stay buildable).
5. If the work stops mid-edit, make the tree build first. Never leave a broken
   build for the next agent without a `blocked` task row and a handoff.

## What makes a good handoff

- **Exact next action.** "Run X, expect Y, then do Z." Not "continue work".
- **State of the tree.** Commit hash, whether it builds, which tests fail.
- **Dead ends.** What you tried that did not work and why. This saves the most
  time.
- **Open questions.** Things only a human can answer, with the exact command to
  reproduce.
- **File claims.** Which files you touched so the next agent knows what to
  re-read.

## Index

| Date | Task | Agent | File |
|---|---|---|---|
| 2026-09-10 | P-107 bind-pose fix | follow-up | _this entry is the code itself; no separate note_ |
| 2026-09-10 | P-201 animation start | opencode (deepseek-flash) | [`2026-09-10-P-201-animation-start.md`](handoffs/2026-09-10-P-201-animation-start.md) |
| 2026-09-10 | P-201 animation landed | opencode (deepseek-flash) | [`2026-09-10-P-201-animation-done.md`](handoffs/2026-09-10-P-201-animation-done.md) |

## Template

See [`templates/handoff.md`](templates/handoff.md).
