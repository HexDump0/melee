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

The shader must evaluate `A/(B - z) - C` against the **GX screen depth**, not
GL's `gl_FragCoord.z`.  The SDK's `GXProject` performs the viewport transform
as `z_screen = far + z_ndc * (far - near)`; with the usual `[0,1]` depth range
that is `z_ndc + 1`, so the perspective `z_ndc ∈ [-1,0]` lands the visible
range on `[0,1]`.  GL maps the same `z_ndc` as `near + (z_ndc+1)/2 *
(far-near)`, i.e. `[0, 0.5]` — half the GX value.  Recovering the hardware
depth needs only the viewport near plane:

```
d = 2 * gl_FragCoord.z - near
```

(`u_depth_near` is `GXSetViewport`'s `nearz`, always 0 in HSD.)  This was
P-679's one real error and the reason every retail fog was under-applied by
half; see **P-690** below.  Aurora's reversed-Z form (`1.0 - in.pos.z`) is
the same quantity for `near = 0`, `far = 1`.

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

Flips: stubbing the coefficients to `A=0,B=.5,C=0` fails
`fog abc=(0,0.5,0) want (0.204082,1.020408,1.0)`; P-690's flip reverts the
shader to `float d = gl_FragCoord.z;` and pass 9 fails all four pixels with
the half-strength values (`fog LIN pixel=34,221,0 want ~147,108`,
`fog near pixel=12,243,0 want ~29,226`,
`fog EXP2 pixel=24,231,0 want ~215,40`,
`fog range adj pixel=68,187,0 want ~219,36`).

## P-690 correction (2026-09-14)

The P-679 shader read `gl_FragCoord.z` directly, on the false premise that GL
and the GX TEU map `z_ndc` identically.  They do not; see the formula above.
The fix adds the `u_depth_near` uniform and evaluates
`d = 2.0 * gl_FragCoord.z - u_depth_near`.  Pass 9 now spans GX screen depth
0.3..0.8 (`z = -0.7..-0.2` under the identity projection) with
`start/end = 0.1/0.5`, so both sampled ends are inside the fog ramp and a
half-depth mapping fails every check; the range-adjusted frame compares
against the SDK table's interpolated `k` instead of a saturated pixel.
`ctest` 21/21.  Effect in retail frames is small (the title's
`ScTitle_fog` only ramps above `z ≈ 0.99`), but the parity gap was real for
every stage whose fog volume the camera enters.
