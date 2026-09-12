# Handoff: S2 complete — compiled HSD renders through the GX HLE

**Date:** 2026-09-12
**Agent:** opencode (deepseek-flash)
**Task:** P-606 (S2, ADR-0010)
**Tree state:** prototype untouched and deterministic (`--inspect` tail,
`--view`/`--scripted` unchanged); `ctest --test-dir build/native` 5/5; no
`src/` or `extern/` file modified.

## Result

A retail `PlMrNr.dat` loads through the compiled decompilation
(`HSD_ArchiveParse`, `HSD_JObjLoadJoint`, `HSD_JObjDispAll`) and renders
through the new GX HLE + GLES3 backend.  The compiled screenshot matches the
prototype viewer screenshot at the same camera/lights to RMSE 10.94/255 over
the model region (worst differences are speculative; see below).  **S2 exit
gate passed.**

- Harness: `native/tests/test_decomp_render.c` (+ ctest `decomp_render`,
  `--no-gl` mode for sanitizer runs).
- Backend: `native/decomp/gx/gx_hle.{c,h}`, `native/decomp/gx/gx_gl.{c,h}`,
  `native/decomp/hsd/hsd_scene.{c,h}`.
- Boot: `native/platform/gx_vi.c` now only owns VI/render modes/draw-done;
  the GX command surface is real in `gx_hle.c`, so S1's triage log loses its
  146 GX stub calls (`94 stub_calls / 33 unique` in the new log).
- Evidence and deviations: `learnings/decomp_s2_gx_hle.md` +
  `logs/2026-09-12-S2-render.md`.

## What is proven

- Byte-order bridge: descriptors converted, byte-defined GX data left BE; the
  compiled loaders see host-order structs and the HLE reads BE arrays/lists/
  textures.  World bounds equal the prototype's exactly (scale 1.10).
- Part visibility: the `ftData` vis tables applied to the compiled tree hide
  16 of 59 DObjs, matching the prototype.
- GX HLE decodes PObj display lists with zero desync across 68 lists and
  reproduces the prototype's per-batch vertex counts.
- The render is deterministic (no wall clock/RNG in the path).
- ASan/UBSan clean for boot, S0 probe, headless render and full GL render.

## Known deviations / open work (not S2 blockers)

1. **Specular shading.** The port evaluates a Blinn-Phong channel-1 specular
   approximation (the prototype's formulation) instead of GX's rational
   polynomial; Mario's boots still read grey rather than brown because the
   compiled TEV spec stage blends the light spec map over the leather.  The
   next step is a faithful hardware specular implementation and/or checking
   the channel-1 value/register routing against `ft` material cases.  File in
   TASKS.md if it matters for S4; it is cosmetic for S2.
2. **Direct-mode GX** (`GXBegin` + inline vertex writes) bypasses the backend
   (the platform maps the FIFO page as scratch).  Only needed for HUD,
   particles and shape-anim; S4.
3. **Fog** captured but not evaluated (no effect at the tested camera).
4. **The `hsd_scene.c` converter is S2-scope**, not the S3 pipeline: no
   FObj/FigaTree/shape-set conversion, `robjdesc` nulled, single-archive only.
5. **POBJ_SHAPEANIM** (Kirby G&W variants) untested.

## Commands

```sh
cmake -S native -B build/native -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build/native -j4
ctest --test-dir build/native --output-on-failure          # 5/5
./build/native/test_decomp_render --width 1280 --height 800 --shot /tmp/c.bmp
```

## Next milestone

S3: the real host-endian asset pipeline (DVD + `HSD_DevCom`/ARQ) — the
`hsd_scene.c` converter is the seed and `learnings/decomp_assets.md` is the
spec.  Do not extend the S2 bridge instead of S3; it has no cache and only
covers the character-display descriptor graph.
