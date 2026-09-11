# Compiled decompilation code (experiment)

This directory is reserved for building code from `src/` (the decompilation)
directly into the native port. It is empty until the first experiment lands.

## Plan (P-301)

1. Pick a self-contained source file with no hardware calls, starting with
   `src/sysdolphin/baselib/mtx.c` and `vec.c`.
2. Add a CMake target here that compiles those files with `-I` pointing at a
   shim include directory (`native/decomp/shim/`). The shim provides the
   GameCube headers the files include (`<dolphin/...>`, `Runtime/...>`,
   `sysdolphin/...>`) as PC-compatible replacements. For pure math the shim
   should be nearly empty.
3. Add a differential test (`tests/`) that runs the compiled function and the
   hand version in `gx/math.c` on the same inputs and compares results.
4. If the boundary is clean, replace the hand version and repeat with the next
   source file. If it is not, delete this experiment and keep hand-porting.

## Rules

- One source of truth per function: when a compiled version lands, the hand
  copy is deleted in the same commit.
- Never modify `src/`.
- The GCN build (`configure.py` + ninja) must stay green.
- The shim is portable C11, not platform-specific code injected into `src/`.
