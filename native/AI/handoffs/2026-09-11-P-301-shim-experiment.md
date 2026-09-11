# Handoff: P-301 — compile decomp math behind a shim (experiment)

**Date:** 2026-09-11
**Agent:** opencode (deepseek-flash)
**Tree state:** clean; `./build/native/melee` builds warning-free, ctest passes,
240-frame scripted and 600-frame ASan+UBSan clean.

## What just landed (context for the next session)

- The port is now layered: `platform/` (disc), `hsd/` (model, aobj, anim,
  parts, light), `gx/` (gl, shader, render, overlay, math, texture),
  `game/` (attributes), `extras/` (viewer, sandbox, physics, font),
  `decomp/` (empty, this task), `tests/` (CTest math harness), `main.c`
  (CLI + dispatch). Binary is `build/native/melee`.
- `gx/math.c` holds the hand-written matrix math and is the parity target.
- `tests/test_math.c` is the existing CTest harness; add to it or add a
  sibling test.
- Read `decomp/README.md`, `learnings/gl_shaders.md` and `AGENTS.md` §0.1
  first.

## Exact next action

Run the staged experiment, in order. **Do not change any rendering call
site** until step 4 passes; this task is additive so screenshots stay
byte-identical.

1. **Recon.** Read the include chains and decide the smallest source set:
   - `src/sysdolphin/baselib/mtx.c`/`.h` (HSD_Mtx* including `HSD_MtxSRT`).
   - `extern/dolphin/src/dolphin/mtx/mtx.c` and `vec.c` (the PSMTX/PSVEC
     primitives the HSD layer calls; headers in
     `extern/dolphin/include/dolphin/mtx.h`).
   - `src/Runtime/platform.h`, `src/sysdolphin/baselib/objalloc.h`,
     `src/sysdolphin/baselib/debug.h` — whatever the above include.
   List every symbol/type each file needs that is not in its own pair.
2. **Shim.** Create `native/decomp/shim/` with the minimal headers needed to
   satisfy those includes on the PC (e.g. `Runtime/platform.h`,
   `dolphin/mtx.h` passthrough or trimmed, `placeholder.h`, `debug.h`).
   Rules: never edit `src/` or `extern/`; the shim only changes which headers
   the *native* build sees. Keep the shim tiny; if it mushrooms, that is the
   experiment's answer.
3. **Build target.** In `native/CMakeLists.txt` add a separate target/object
   library (e.g. `melee_decomp_math`) compiling the selected upstream `.c`
   files with `-I native/decomp/shim` first and `-I src -I extern/dolphin/
   include` after. Upstream sources may need `-w`; the shim itself must be
   warning-free under `-Wall -Wextra -Wpedantic`.
4. **Parity test.** Add a CTest case (extend `tests/test_math.c` or add
   `tests/test_mtx_parity.c`) that:
   - builds the same inputs for the compiled function and for `gx/math.c`
     (fixed seed LCG for determinism, plus hand-computed cases);
   - compares results numerically and prints the max absolute error;
   - passes at a tight tolerance (start `1e-6`, loosen only with evidence).
   Good first pairs: `HSD_MtxSRT` vs the port's local-SRT path,
   `HSD_MtxInverse`/`HSD_MtxInverseConcat` vs `m4_*`, `PSVEC*` vs arithmetic.
5. **Decide and record.** If clean: write `learnings/decomp_shim.md` with the
   shim inventory, the build recipe, parity numbers, and what replacing
   `gx/math.c` would take. Then (and only then) replace call sites file by
   file, deleting the hand copy in the same commit. If ugly: delete the
   experiment, keep hand-porting, and record why in the same learning file
   and `decomp/README.md`.
6. Update `TASKS.md` (P-301 status/notes) and `STATE.md` if behavior changed.

## Verification

```sh
cmake -S native -B build/native -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build/native -j4                     # warning-free
ctest --test-dir build/native --output-on-failure
./build/native/melee --inspect
SDL_VIDEODRIVER=offscreen ./build/native/melee --scripted --frames 240
# Completed 240 render frames, 240 simulation ticks
SDL_VIDEODRIVER=offscreen ./build/native/melee --view --frames 3 \
    --screenshot /tmp/parity.bmp                   # must not change
```

If the ASan build is used: `-DMELEE_SANITIZE=ON` and
`ASAN_OPTIONS=detect_leaks=0`.

## Blockers / notes

- `src/sysdolphin/baselib/vec.c` does **not** exist; the vector code is
  `extern/dolphin/src/dolphin/mtx/vec.c`.
- The decomp `mtx.c` references `HSD_ObjAllocData` globals and
  `MTX*`/`VEC*` primitives; expect to compile the Dolphin SDK pairs too or
  stub only what the tested functions do not touch (prefer compiling over
  stubbing).
- Big-endian/32-bit issues should not matter for this file set (pure float
  math), but `Mtx` is `f32[3][4]`; check `dolphin/mtx.h` for type defs.
- Do not touch `src/` or `extern/`; shim only.
