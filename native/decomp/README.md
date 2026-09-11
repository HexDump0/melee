# Compiled decompilation code

This directory builds selected files from `src/` (the decompilation) directly
into the native port behind a shim.  The P-301 experiment landed `HSD_MtxSRT`
and settled what is and is not compilable; read
[`../AI/learnings/decomp_shim.md`](../AI/learnings/decomp_shim.md) before adding
another source file.

## Status (P-301)

- `src/sysdolphin/baselib/mtx.c` is compiled verbatim by the
  `melee_decomp_math` object library (`shim/decomp_shim.h` force-included).
  `HSD_MtxSRT` is the single source of truth for `hsd/model.c`'s local SRT;
  `tests/test_decomp_mtx.c` proves bitwise parity with the deleted hand copy.
- The SDK pair `extern/dolphin/src/dolphin/mtx/{mtx.c,vec.c}` **cannot** be
  compiled: they are Metrowerks `asm` C.  The `C_MTX*`/`C_VEC*` pure-C twins
  live in the same translation units, so they are unreachable too.  The ~15
  primitives the HSD layer calls stay hand-ported.  Do not plan around
  compiling those files without an ADR for a patched-copy + PC-backend
  strategy.
- Only the referenced function sections survive the link; a decomp TU is
  compiled with `-ffunction-sections -fdata-sections` and the executables link
  with `-Wl,--gc-sections`.

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
  copy is deleted in the same commit.
- Never modify `src/` or `extern/dolphin/`.
- The GCN build (`configure.py` + ninja) must stay green.
- The shim is portable C11, not platform-specific code injected into `src/`.
