# GX light-object math and specular evaluation (P-673)

**Date:** 2026-09-13
**Agent:** opencode (deepseek-v4.1-flash)
**Ported from:** the SDK decompilation `decomp/extern/dolphin/src/dolphin/gx/
GXLight.c` (`GXInitLightDistAttn`, `GXInitLightSpot`), verified against Aurora
`lib/dolphin/gx/GXLighting.cpp` at pinned `749d6ee7a22bdfab78c8ece9047bca5d79aa72ca`.
The specular evaluation follows Dolphin `LightingShaderGen.cpp`
(`AttenuationFunc::Spec`) — read-only reference — cross-checked with Aurora
`lib/gx/shader.cpp:lighting_func`.

Evidence: `ctest decomp_gx_direct` (light-object math vs an inline
transcription of GXLight.c) and `ctest decomp_efb` pass 6 (a GPU fixture whose
specular channel must come out light-tinted). Sensitivity was flipped once per
mechanism (see below). The character-select screenshot is pixel-identical to
the P-672 build (the scene's only light is white/infinite, so no visible
change there).

## Bugs fixed

1. **`GXInitLightDistAttn`.** The old host version clamped `ref_br` into
   `[0,1]` and used `k2 = k1²` for MEDIUM/STEEP.  The SDK:
   - `ref_dist < 0` or `ref_br <= 0` or `ref_br >= 1` forces `GX_DA_OFF`;
   - GENTLE `k = (1, (1-b)/(b·d), 0)`;
   - MEDIUM `k = (1, (1-b)/(2·b·d), (1-b)/(2·b·d²))`;
   - STEEP `k = (1, 0, (1-b)/(b·d²))`.
2. **`GXInitLightSpot`.** An out-of-range cutoff must select `GX_SP_OFF`
   (a = (1,0,0)), not clamp the angle and keep the requested falloff.  HSD
   depends on this: `setup_point_lightobj` calls
   `GXInitLightSpot(lightobj, 0.0F, 0)`, and the SPOT channel attenuation then
   reduces to the pure distance polynomial `1 / k·(1,d,d²)`.
3. **Specular channels are light-tinted.** HSD configures channel 1 as
   `GX_AF_SPEC` — note the SDK enum order is `GX_AF_SPEC=0, GX_AF_SPOT=1,
   GX_AF_NONE=2`, so the omitted `HSD_Chan.attn_fn` initializer is *specular*,
   not "none" (an easy misread).  For that function the hardware sets the
   diffuse-field bits to 0 (DF_NONE) and evaluates
   `attn = max(0, (a·(1,t,t²)) / (k·(1,t,t²)))`, `t = (N·L >= 0) ? max(0, N·H) : 0`,
   accumulating `attn * light.color.rgb`.  The old shader averaged the light
   colour to grey, dropped the tint and clamped before the TEV spec material
   multiply.

## Conventions/notes

- **`GXInitLightDir` sign.** The SDK stores `-input`; our HLE stores the raw
  input because our vertex shader consumes the value as the half vector `H`
  (HSD computes `half = lvec + cdir` in `HSD_LObjSetupSpecularInit`).  This
  matches the prototype, which the owner validated, and yields correct
  highlights; flipping the store without also flipping the shader would break
  them.  Documented, not scheduled.
- The spec fixture uses `a = (0,0,1)`, `k = (k0,0,1-k0)` exactly like
  `HSD_LObjSetup` (`lobj.c:289`), with `N = H` so the polynomial is 1.

## Deviation filed

- **Spot-light cones** (`GX_AF_SPOT` with nonzero `a.y/a.z`): our shader
  attenuates by distance only; the hardware multiplies by a cosine
  polynomial.  HSD's point lights always end with `a = (1,0,0)` after the
  `GXInitLightSpot(..., 0, 0)` call, so this is exact for them.  Only
  `GrZebesRoute` references `LOBJ_SPOT`; filed as **P-681** with a Brinstar
  fixture once a stage harness can reach it.

## Re-run

```sh
ctest --test-dir build/native -R decomp_gx_direct
ctest --test-dir build/native -R decomp_efb
```

Flips: old `k2=k1²` fails
`dist attn case 1 k=(1.000000,0.005000,0.000025) want (1.000000,0.002500,0.000012)`;
clamping the cutoff fails `spot case 1 a=(0.0,1.0,0.0) want (1.0,0.0,0.0)`;
grey specular fails `spec pixel=128,128,128 (want tinted 255,128,0)`.
