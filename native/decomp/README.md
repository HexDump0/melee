# Compiled decompilation code

This directory builds files from `src/` (the decompilation) into the port
behind `shim/`. Under **ADR-0010** this is now the main path: the whole game is
compiled this way and `native/` provides the platform layer (OS, DVD, GX HLE,
AX, input). Read [`../AI/ROADMAP_DETAILS.md`](../AI/ROADMAP_DETAILS.md),
[`../AI/learnings/decomp_port.md`](../AI/learnings/decomp_port.md) and
[`../AI/learnings/decomp_shim.md`](../AI/learnings/decomp_shim.md) before
adding a source file.

## Status (P-301 + ADR-0010 pivot)

- `src/sysdolphin/baselib/mtx.c` is compiled verbatim by the
  `melee_decomp_math` object library (`shim/decomp_shim.h` force-included).
  `HSD_MtxSRT` is the single source of truth for `hsd/model.c`'s local SRT;
  `tests/test_decomp_mtx.c` proves bitwise parity with the deleted hand copy.
- The SDK pair `extern/dolphin/src/dolphin/mtx/{mtx.c,vec.c}` **cannot** be
  compiled: they are Metrowerks `asm` C. The ~15 primitives the HSD layer
  calls are provided by a portable backend under this directory (ADR-0011
  rule 3); never edit `extern/`.
- Only the referenced function sections survive the link; decomp TUs are
  compiled with `-ffunction-sections -fdata-sections` and the executables link
  with `-Wl,--gc-sections`.
- **Next (S0):** `melee_decomp_hsd` compiles
  `src/sysdolphin/baselib/archive.c` (+ allocator/class deps) and probes
  `HSD_ArchiveParse` / `HSD_JObjLoadJoint` against a real `PlMrNr.dat`, with a
  host-endian conversion for the structural sections (tasks P-602/P-603).

## Adding the next file

1. Check that the file has no Metrowerks asm (`rg '(^|\s)asm\s*(\{|void)'`).
2. Add it to `melee_decomp_math`; if it needs a header from the shim, keep the
   shim minimal and warning-free under `-Wall -Wextra -Wpedantic` (the upstream
   TU itself compiles with `-w`).
3. Add a differential CTest against a literal transcription of the current
   hand copy before deleting that copy.
4. Delete the hand copy and the call-site plumbing in the same commit as the
   compiled version lands.

## Rules

- One source of truth per function: when a compiled version lands, the hand
  copy is deleted in the same commit, after parity is proven.
- Never modify `extern/dolphin/`. `src/` is read-only by default; gated
  `#ifdef PORT_PC` portability fixes are allowed per ADR-0011 and must be
  listed in `learnings/decomp_port.md`.
- The GCN build (`configure.py` + ninja) must stay green.
- The shim is portable C11, not platform-specific code injected into `src/`.
