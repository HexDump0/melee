# Handoff: renderer parity session (P-671..P-678, P-672..P-675)

**Date:** 2026-09-13
**Agent:** opencode (deepseek-v4.1-flash)
**Reference:** Aurora pinned `749d6ee7a22bdfab78c8ece9047bca5d79aa72ca`
(local `aurora-reference/`, git-ignored; copy in `/tmp/opencode/aurora-reference`).

## Mission and result

ADR-0017 keeps the 32-bit GLES renderer and ports Aurora's algorithms into it.
This session produced the coverage matrix and closed five rows with
sensitivity-flipped regressions:

| Commit | Task | What changed |
|---|---|---|
| `b21f513e6` | P-671 | `learnings/gx_coverage_matrix.md`: all ~100 GX functions Melee calls, with status, our location and Aurora symbol; gap list P-672..P-680 |
| `40c8cca55` | P-678 | `GXGetProjectionv` returns the SDK packed `{type,A..F}`; `GXSetProjectionv` implemented (G-131) |
| `81a628178` | P-672 | Indirect fragment evaluation, `GXSetTevDirect`, `GX_TG_MTX3x4` q-divide + normalize, SRTG lit-raster toon |
| `2d4d714de` | P-673 | SDK `GXInitLightDistAttn`/`GXInitLightSpot` math; `GX_AF_SPEC` channel tinted with the hardware polynomial |
| `447826c1d` | P-674 | BT.601 I4/I8/IA4/IA8 copies, RGB5A3 copies, shared tiling encoder |
| `3bdf7945a` | P-675 | Bit-replication channel expansion (image+TLUT), per-object `GXTexObj` state |

## Coverage matrix delta

Closed (now EXACT or documented):
- §2 `GXGetProjectionv`/`GXSetProjectionv` (P-678).
- §1/§3 texgen `GX_TG_MTX3x4`, `normalize`, SRTG; indirect evaluation,
  `GXSetTevDirect` (P-672).
- §4 `GXInitLightDistAttn`, `GXInitLightSpot`, `GX_AF_SPEC` specular
  (P-673).  `GXInitLightDir` stays a documented sign convention.
- §5 3/4/5/6-bit expansion, per-object `GXTexObj` (P-675).
- §6 `GXCopyTex` I4/I8/IA4/IA8/RGB5A3, copy clear (P-674).

Still red (each has a task):
- **P-676** match-path performance (reinstated P-642).
- **P-677** parity harness breadth.
- **P-679** fog a/b/c formulas + `GXSetFogRangeAdj`/`GXInitFogAdjTable`
  (converter currently nulls `HSD_FogDesc.fogadjdesc`, `hsd_convert.c:161`).
- **P-680** `GX_LINES`/`GX_LINESTRIP`/`GX_POINTS` are dropped;
  `GXSetLineWidth`/`GXSetPointSize`/`GXEnableTexOffsets` are stubs.  Melee
  uses them in `lb_*` HUD/effects and `psdisp` particles — the biggest
  remaining rendering gap.
- **P-681** spot-light cones (`LOBJ_SPOT`, only `GrZebesRoute`).
- **P-682** Z24X8 EFB depth snapshots + `GX_ZT_ADD`/bias edges.

Documented deviations (no task): EFB always RGBA8 (`GXSetPixelFmt`),
XFB/VI copies and logic ops, texture residency, generated mips, edge-lod
and bias-clamp, `GXInitLightDir` sign, `GXSetCopyClear`.

## Evidence / exact commands

```sh
ctest --test-dir build/native --output-on-failure          # 17/17
MELEE_NO_ASSET_CACHE=1 ./build/native/test_decomp_assets   # PASS
./build/native/test_decomp_render --direct                 # texgen/indirect/light/texobj
./build/native/test_decomp_render --efb                    # 7 passes incl. new 5-7
ASAN_OPTIONS=detect_leaks=0 ./build/native-asan/test_decomp_render --direct
ASAN_OPTIONS=detect_leaks=0 ./build/native-asan/test_decomp_render --efb
```

Screenshot deltas (1280x800, `test_decomp_render --shot`):
- P-672 vs P-671: RMSE 0.00091, 19 px — Mario's reflection map now honors
  `MTX3x4`/normalize.
- P-675 vs P-674: RMSE 0.00056, 82 px — 1-LSB texture rounding.
- P-673 and P-674: pixel-identical to their parents on this scene.

Every regression was flipped once to prove sensitivity; the exact failing
outputs are in each learning under `native/AI/learnings/`
(`gx_indirect_toon.md`, `gx_lighting_specular.md`, `gx_efb_copy.md`,
`gx_texture_parity.md`, G-131 in `gotchas/GOTCHAS.md`).

## Suggested next slice

1. **P-680 lines/points** first: it is the only STUB that drops geometry
   Melee actually draws.  Add per-draw primitive runs to `GxHleDraw`
   (triangles stay the default), map `GX_LINES`/`GX_LINESTRIP`/`GX_POINTS`
   in `exec_primitive`, draw each run with its GL mode, add `gl_PointSize`
   for `GXSetPointSize`.  Fixtures: a direct-mode run assertion plus an EFB
   pixel read of a line and a point.
2. **P-679 fog**: port the `GXSetFog` a/b/c packing and the five
   `1-exp2(-8f)`/`exp2(-8(1-f))` families from Aurora `shader.cpp:1537`;
   `GXInitFogAdjTable` needs the converter follow-up noted in the task.
3. **P-676 perf** after that (profile under a quiet system first; the P-642
   notes warn that a decoded-display-list cache was slower).

## Notes for the next agent

- The `aurora-reference/` checkout is at the pinned commit and git-ignored;
  do not commit it or add it as a dependency.
- The GL shader is compiled at runtime: GLSL errors only appear on the first
  `--shot`/`--efb` run, not at build time.
- `GX_TG_MTX2x4` now forces `z=1` before the post matrix and normalize runs
  between matrices; changing either affects every fighter reflection map.
- The `GXTexObj` slot table is keyed by caller pointer with LRU eviction; if
  a future change hands the HLE an object it has not seen, the pending-object
  fallback applies.
