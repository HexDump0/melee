# GX indirect texturing, toon ramps and projective texgen (P-672)

**Date:** 2026-09-13
**Agent:** opencode (deepseek-v4.1-flash)
**Ported from:** Aurora pinned `749d6ee7a22bdfab78c8ece9047bca5d79aa72ca` —
`lib/gx/shader.cpp` (`build_fragment_source` indirect block ~L1297-1530,
`lighting_func`), `lib/gx/shader_info.cpp`, `lib/dolphin/gx/GXBump.cpp`
(`GXSetTevIndirect`/`GXSetIndTexOrder`/`GXSetIndTexMtx`); cross-checked against
the SDK decompilation in `decomp/extern/dolphin/src/dolphin/gx/GXBump.c`.
Dolphin's `PixelShaderGen.cpp` (GPL, read-only reference) settled the integer
domain question below.

Evidence: `ctest decomp_gx_direct` (CPU capture + texgen) and
`ctest decomp_efb` pass 5 (GPU indirect fragment evaluation + GXSetTevDirect
teardown). Screenshot delta: Mario base vs P-672 RMSE 59.7/65535
(0.00091), 19 pixels at (576,385)..(669,419) — the cap/face reflection map's
UVs now go through `GX_TG_MTX3x4` normalize + projective divide.

## What Melee actually uses

| Feature | Call sites | Notes |
|---|---|---|
| Indirect texturing | `lbrefract.c:641-646` only (`lbRefract_80022998`) | The refraction/stealth fighter material. `GXSetIndTexOrder(0, TEXCOORD0, TEXMAP0)`, `GXSetIndTexMtx(GX_ITM_0, texture_offset, 1)` with `texture_offset = {{-0.5,0,0},{0,-0.5,0}}`, `GXSetTevIndirect(stage0, ind0, GX_ITF_8, GX_ITB_ST, GX_ITM_0, ITW_OFF, ITW_OFF, 0, 0, OFF)`. |
| `GXSetTevDirect` | `lbrefract.c:453/464` | Teardown; must clear the indirect fields (SDK `GXBump.c` implements it as `GXSetTevIndirect(..., GX_ITM_OFF, GX_ITW_OFF, ..., 0, 0, 0)`). |
| Toon ramp (`GX_TG_SRTG`) | `tobj.c:538` + `grpura.c:678/888` via `HSD_MObjSetToonTextureImage` | Stage-only (Pura/`GrPu`); the toon TObj is prepended to the material chain and gets a sequential texmap (0..2 in practice), not the static descriptor's TEXMAP7. |
| `GX_TG_MTX3x4` + `normalize` | `tobj.c:481-489` (REFLECTION/HILIGHT) and `lbrefract.c:620-624` | Every reflection map on fighters goes through this; previously our texgen dropped the third row, the normalize and the q-divide. |

## The algorithm we ported (normalized-UV form)

GX texture coordinates are produced by `postmtx * mtx * source`; `GX_TG_MTX2x4`
forces `z = 1` before the post matrix, `GX_TG_MTX3x4` keeps the third row and
the hardware divides `xy / q` per pixel (Dolphin `VertexShaderGen` q==0 quirk:
`clamp(xy/2, -1, 1)`). `normalize` runs after the first matrix, before the
post matrix (Aurora `shader.cpp`).

Indirect, per TEV stage, matching Aurora (`shader.cpp` ~L1330-1530) and the
SDK bit fields:

1. Sample the indirect map with the stage's texcoord, scaled by
   `GXSetIndTexCoordScale` (`GX_ITS_1..256` = divide by `2^0..2^8`), take
   `.abg` (A→S, B→T, G→U), scale to 0..255 and round.
2. Format shift: `ITF_8` none, `ITF_5` `>>3`, `ITF_4` `>>4`, `ITF_3` `>>5`.
3. Bias: `ITF_8` subtracts 128, other formats add 1, on the components named
   by `GX_ITB_*`.
4. Static matrix (`GX_ITM_0..2`): `offset = M · coord * 2^scale_exp`; the SDK
   stores `1024 * offset` in an 11-bit signed field, so `GXSetIndTexMtx` now
   quantizes to 1/1024 like the hardware. Dynamic S/T matrices and alpha-bump
   (`GX_ITBA_*`) are not used by Melee and are not implemented.
5. Base coordinate wrap (`ITW_*`) and optional `add_prev` accumulation.
6. The offset is in destination texels; our shader keeps UVs normalized, so
   the offset is divided by the destination map's size (Aurora divides the
   whole result by `size_bias`). This is the one domain adaptation from the
   reference (Aurora/Dolphin track a 128-per-repeat fixpoint domain).

Toon: the fragment shader now substitutes the *lit* raster (`v_ras0.xy`,
channel 0 output) whenever the stage's texcoord was generated with
`GX_TG_SRTG`. That is the hardware behavior (the toon ramp is sampled by the
lighting result), and it supersedes the old pass-through of the raw vertex
color. Only `GrPu` exercises it.

## Deviations / not ported

- **Dynamic indirect matrices** (`GX_ITM_S0..S2`, `GX_ITM_T0..T2`) and
  `GX_ITBA_*` alpha bump: unused in Melee (`rg` is empty).
- **Indirect maps above TEXMAP2**: Melee's three GL texture units already
  cover the refraction setup (map0 = screen copy, map1 = offset map) and the
  toon map; `gx_sample_map` falls back to unit 0 for maps 3..7. Documented,
  not scheduled.
- The wrap math uses the destination size to stay in normalized UVs; for
  ITW_OFF (the only mode Melee uses) it is an exact identity.

## Regression and how to re-run

```sh
ctest --test-dir build/native -R decomp_gx_direct   # texgen + capture + GXSetTevDirect
ctest --test-dir build/native -R decomp_efb         # pass 5: GPU indirect/green->magenta
./build/native/test_decomp_render --direct
./build/native/test_decomp_render --efb
```

Sensitivity was flipped once per mechanism: disabling the shader indirect
block reads the base texel (`indirect offset pixel=0,255,0`), making
`GXSetTevDirect` a no-op fails the capture assertion, and removing the q
divide reads `uv=(0.2500,0.6250)` instead of `(0.5000,1.2500)`.
