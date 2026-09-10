# AGENTS.md — rules of engagement for the native port

These rules exist because each one has already been learned the hard way.
Follow them even when they feel slow.

## 0. Scope

- Work only under `native/` unless a task explicitly says otherwise. The
  GameCube build (`src/`, `configure.py`, `ninja`) must stay green.
- Never modify upstream decompiled code to make the port easier. If the engine
  code is wrong for a port, adapt the port layer around it or add an
  `#ifdef`-free wrapper under `native/`.
- Never use or commit game assets. Test against the user's local disc image.

## 1. Cold start (do this every session)

1. `git pull`
2. Read `STATE.md` and the task you intend to claim in `TASKS.md`.
3. Check `handoffs/` for the latest note and `gotchas/GOTCHAS.md`.
4. Build and run the baseline checks in `TESTING.md` **before** changing code,
   so you know whether a failure is yours.

## 2. Claim before you work

- Add a row to `TASKS.md` with status `claimed`, your agent name, date, and the
  files you expect to touch. One task per agent.
- If two agents touch the same file, the later one must coordinate through a
  handoff note or pick another task. Files claimed by someone else are read-only
  for you.
- Small fixes (typo, warning) may skip claiming; say so in the commit message.

## 3. Verify every change

- Build must be warning-free for the files you touched (`-Wall -Wextra -Wpedantic`).
- Run the headless smoke test from `TESTING.md` and paste the key output into
  `logs/` if it changed behavior.
- Any parser or memory change: run the AddressSanitizer/UBSan build for a
  scripted run. Never merge an ASan failure.
- Visual changes: capture a `--screenshot` and describe what changed. Attach
  the image path in your handoff note.

## 4. Python is a scratchpad, not a deliverable

Parsing reverse-engineered binary formats is far faster in a throwaway Python
script than in C. That is expected and encouraged, with rules:

1. Use Python only to discover and validate a format against real data.
2. Never leave Python in the runtime path of `native/` without a recorded
   decision in `DECISIONS.md`.
3. When a Python probe finds the truth, port the smallest correct version to C
   and **delete or move the probe to `logs/`**. Do not leave two sources of
   truth in the tree.
4. Write the finding into `learnings/` immediately; the next agent will not
   have your terminal.

## 5. Tests you cannot run

Some things need a human: real keyboard/controller feel, real window/GPU
behavior, audio, and "does it look right to a Melee player". When you need one:

1. Write the exact command and what to look for.
2. Ask the human once, with a concrete checklist.
3. Do not block on it; continue with headless-verifiable work and record the
   open question in `TASKS.md`.

## 6. Commits

- Commit often, atomically, with human imperative messages, e.g.
  `Decode HSD envelope groups for bind-pose skinning`.
- Do not use `Match ...` prefixes; those are for decomp commits.
- Never commit generated files, screenshots, disc images, or `build/`.
- Never commit `iso/`, `ACGC-PC-Port/`, or scratch dumps.
- Update `STATE.md` in the same commit when behavior changes.

## 7. Leave the campsite better

- If you spent more than 15 minutes figuring something out, it goes in
  `learnings/` or `gotchas/GOTCHAS.md`.
- If you abandon a task, write a handoff note. A half-finished task with no
  note is worse than an untouched task.
- If you find a bug you are not fixing, file it in `TASKS.md` with a repro.

## 8. Style

- Match the existing C style in `native/`: C11, 4-space indent, braces on
  their own line, `snake_case` for port code, HSD names kept as-is.
- No new external dependencies without a decision entry. The port currently
  depends on SDL2 + OpenGL + libm only.
- Prefer fixed-size, bounds-checked parsing over clever zero-copy tricks.
  Correct beats fast for the decoder; optimize later with a profiler.
