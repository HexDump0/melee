# GX fog coefficients, screen-depth evaluation and range adjustment (P-679)

**Date:** 2026-09-13
**Agent:** opencode (deepseek-v4.1-flash)
**Ported from:** the SDK decompilation
`decomp/extern/dolphin/src/dolphin/gx/GXPixel.c` (`GXSetFog`,
`GXInitFogAdjTable`, `GXSetFogRangeAdj`) and Aurora `lib/dolphin/gx/
GXSet.cpp:GXSetFog` + `lib/gx/shader.cpp:1537` (the fog family formulas), at
pinned `749d6ee7a22bdfab78c8ece9047bca5d79aa72ca`.

Evidence: `ctest decomp_gx_direct` (coefficient packing + adj table) and
`ctest decomp_efb` pass 9 (linear/EXP2 fog pixels against the formula and a
range-adjusted edge pixel).  The character-select screenshot is byte-identical
to P-680 (its fog volume is far beyond the model).

## The hardware formula

The SDK computes the perspective coefficients once:

```
A = farZ*nearZ / ((farZ-nearZ)*(endZ-startZ))
B = farZ/(farZ-nearZ)
C = startZ/(endZ-startZ)
```

and the hardware evaluates `fogCoord = A/(B - z_ndc)` followed by
`fog = clamp(fogCoord - C, 0, 1)` (then the `GX_FOG_*` family).  The old host
approximation used `(endZ - eyeDistance)/(endZ - startZ)` and `exp(-density*d)`
forms, which differ for every stage fog and for the non-linear families.
Derivation check: with `z_ndc` the screen depth (0 near, 1 far), `A/(B-z) - C`
equals `(eyeDistance - startZ)/(endZ - startZ)` exactly, which is what the SDK
packing is designed to produce.

Family mapping (`GX_FOG_*`, `type & 7`; the SDK always programs the
perspective `c_proj_fsel` bit 0, so HSD/Melee never uses orthographic fog):

| Family | `fogZ` |
|---|---|
| LIN (2) | `f` |
| EXP (4) | `1 - exp2(-8f)` |
| EXP2 (5) | `1 - exp2(-8f²)` |
| REVEXP (6) | `exp2(-8(1-f))` |
| REVEXP2 (7) | `1 - exp2(-8(1-f)²)` |

`color = mix(color, fogColor, clamp(fogZ, 0, 1))`.

The shader reads `gl_FragCoord.z` for `z_ndc`.  That is the post-viewport
screen depth, matching Dolphin (`rawpos.z`) and the hardware.  (GL maps NDC
[-1,1] to [0,1] exactly like the GX TEU, so a real GX projection matrix gives
the correct screen depth without extra work.)

## Range adjustment

`GXInitFogAdjTable` is the SDK transcription: for a projection matrix it
solves the near-plane distance and horizontal side extent and fills
`r[i] = (u32)(256*sqrt(1 + (x_i/nearZ)²)) & 0xFFF` for `x_i = (i+1)*32`
scaled by `2/width`.  `GXSetFogRangeAdj` latches the table (as `r/256`) and
the center.  The shader follows Dolphin's per-pixel adjustment:

```
offset  = 2*(x_pixel - center)/width
index   = clamp(9 - |offset|*9, 0, 9)
k       = mix(table[i], table[i+1], frac)
base   *= sqrt(offset² + k²)/k
```

Dolphin's constants multiply the table by an extra 4 (marked "TODO: guess"),
which we do not copy; the SDK's `r/256` is the documented `k`.  Because
`HSD_FogDesc.fogadjdesc` is currently nulled by the asset converter
(`hsd_convert.c:161`), no retail scene reaches `GXSetFogRangeAdj` yet — the
renderer side is complete and unit-tested, the converter side remains a
follow-up (P-658/P-662 family).  The deviation is noted in the matrix.

## Re-run / sensitivity

```sh
ctest --test-dir build/native -R decomp_gx_direct
ctest --test-dir build/native -R decomp_efb
```

Flips: using `v_dist` instead of `gl_FragCoord.z` fails
`fog LIN pixel=0,255,0 want ~189,66`; stubbing the coefficients to `A=0,B=.5,C=0`
fails `fog abc=(0,0.5,0) want (0.204082,1.020408,1.0)`.
