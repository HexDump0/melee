# AGENTS.md — rules of engagement for the native port

These rules exist because each one has already been learned the hard way.
Follow them even when they feel slow.

## 0. Scope

- The port now **compiles `src/`** (ADR-0010). Work under `native/` and, for
  the decompiled-port build, read `src/` and `extern/dolphin/`. The GameCube
  build (`src/`, `configure.py`, `ninja`) must stay green after any change.
- `src/` and `extern/` are read-only by default. The only allowed edits are
  minimal, `#ifdef PORT_PC`-gated portability fixes per ADR-0011 (shim first,
  patch second, replacement TU last), each listed in
  `learnings/decomp_port.md`. Never fork the tree; never edit `extern/`.
- Never use or commit game assets. Test against the user's local disc image.

## 0.1 This is a port, not a reinterpretation

**The decompilation under `src/` is the specification. Port the game's actual
logic; do not invent an equivalent that "looks about right".**

**The specification is now also the product (ADR-0010).** Engine behavior comes
from compiling `src/`, not from transcription. Hand transcription is a fallback
only for modules the compiled path cannot take yet; when a compiled function
proves parity with the hand copy, delete the hand copy in the same commit. The
port implements platform behavior (GX, AX, OS, DVD, input, timing), never
gameplay approximations.

- When transcription is needed (a module the compiled path cannot take yet),
  find the function that implements it (`rg` for the symbol, read the
  `.c`/`.h`) and translate its structure, naming, control flow and arithmetic
  verbatim into `native/`. Keep the original function names in comments (e.g.
  `/* HSD_FObjInterpretAnim */`). Prefer compiling the file.
- Do not replace state machines with "cleaner" stateless models. If the engine
  plays increments a byte stream (FObj), the port plays the same stream with
  the same state. If the engine walks a tree a certain way (ftParts), the port
  walks it the same way. Reinventions drift and produce wrong output.
- Reuse the decomp's constants, offsets and formulas verbatim (fixed-point
  encodings, spline kernel, matrix convention, flag semantics). When a
  formula is copied, cite the source file/function in a comment.
- When experimenting, validate against the decomp's behavior, not against
  "it looks plausible": differential tests against a literal transcription of
  the decomp function are the expected proof (see `TESTING.md`).
- The known-good engine sources for the animation work are:
  `src/sysdolphin/baselib/fobj.c` + `aobj.c` (curve playback),
  `jobj.c` (`JObjUpdateFunc`, `HSD_JObjMakeMatrix`), `mtx.c` (`HSD_MtxSRT`,
  `splGetHelmite` in `spline.c`), `displayfunc.c`
  (`_HSD_mkEnvelopeModelNodeMtx`), `pobj.c` / `src/melee/ft/ftparts.c`
  (envelope/shared/rigid matrix setup), `src/melee/lb/lbanim.c` (FigaTree),
  `src/melee/ft/ftanim.c` (node -> joint binding).

## 1. Cold start (do this every session)

1. `git pull`
2. Read `STATE.md`, the current milestone in `ROADMAP_DETAILS.md`, and the task
   you intend to claim in `TASKS.md`.
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
- **Compiled-engine changes prove parity before replacing anything.** A hand
  copy may only be deleted in the commit where the compiled version passes a
  differential test (or `--inspect`/screenshot comparison) against it, and the
  evidence goes in the commit/handoff.
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
- No new external dependencies without a decision entry. The prototype
  (64-bit) depends on SDL2 + desktop OpenGL; the compiled targets (32-bit)
  depend on SDL3 (`lib32-sdl3`, see ADR-0014) + EGL/GLESv2 (Mesa) + libm.
  Machine setup and the exact Arch package names are in `TESTING.md`.
- Prefer fixed-size, bounds-checked parsing over clever zero-copy tricks.
  Correct beats fast for the decoder; optimize later with a profiler.
