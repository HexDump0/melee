# Learning: HSD lights and fog (scene state)

**Date:** 2026-09-10
**Agent:** opencode (deepseek-flash)
**Evidence:** `native/demo_light.c`, `--dump-lights`, rendered viewer shots.
Sources: `src/sysdolphin/baselib/lobj.h` / `lobj.c` (`HSD_LObjSetupInit`,
`setup_*_lightobj`), `state.c` (`HSD_SetupChannelMode`, `HSD_SetupChannel`),
`fog.h` / `fog.c` (`HSD_FogSet`), `src/melee/mn/mncharsel.c:4292` (scene load).

## Where scene lights come from

Lights are **not** in the model archive. The character-select scene loads:

- `MnSlChr.dat` / `MnSlChr.usd` (JP/US), public symbol `MnSelectChrDataTable`
  at `libArchive` time (`mncharsel.c:5341`).
- Table layout: `cam +0`, `light0 +4`, `light1 +8`, `fog +0xC` (data-relative
  pointers).

`--dump-lights` on Rev 2 US:

```
lights: 2
  [0] INFINITE flags=0x000d color=255,255,255,255 pos=[0.570 0.570 0.570]
  [1] AMBIENT  flags=0x0004 color=102,102,102,255
fog: type=2 start=500.00 end=1000.00 color=0,0,102,255
```

`flags=0x000d` = `LOBJ_INFINITE | LOBJ_DIFFUSE | LOBJ_SPECULAR`; the ambient
has only `LOBJ_DIFFUSE`. Stage/menu scenes each carry their own table; stages
are M4 work.

## `HSD_LightDesc` layout (data-relative)

| Offset | Field |
|---|---|
| +0x04 | `next` |
| +0x08 | `flags` (u16) |
| +0x0A | `attnflags` (u16) |
| +0x0C | `color` (GXColor) |
| +0x10 | `position` (`HSD_WObjDesc*`, `Vec3 pos` at +4) |
| +0x14 | `interest` (spot only) |
| +0x18 | union: `shininess`/`HSD_LightPointDesc`/`HSD_LightSpotDesc`/`HSD_LightAttn` |

Flags (`forward.h:163`): type = `flags & 3` (0 ambient, 1 infinite, 2 point,
3 spot); `LOBJ_DIFFUSE` 0x4; `LOBJ_SPECULAR` 0x8; `LOBJ_ALPHA` 0x10;
`LOBJ_HIDDEN` 0x20; `LOBJ_RAW_PARAM` 0x40.

## Selection and channel equation

`HSD_LObjSetupInit` sorts `current_lights` by priority, assigns non-ambient
lights to slots 0..7 (or the ambient light to slot 8), builds the diffuse /
specular / alpha masks, and special-cases specular-only and both-type lights.
`HSD_SetupChannelMode(rendermode & 7)` then:

- case 4 (`RENDER_DIFFUSE`): ambient input is `mat_ambient * ambient.color`
  **only if the ambient light has `LOBJ_DIFFUSE`**, else black; the diffuse
  mask lights contribute `light_color * N·L` (infinite lights use the
  normalized eye-space position as direction; GX attenuation is 1).
- `rendermode & 8` (`RENDER_SPECULAR`): a second channel with the specular
  mask; `HSD_LObjSetup` writes the per-material shininess into the specular
  light attenuation. GX's specular polynomial is approximated by
  `pow(N·H, shininess)` in the port.

The port transforms lights into the same eye space as the vertices and
evaluates the sum in the model vertex shader (`u_light_*` uniforms). The
viewer uses the character-select set as its reference scene; the sandbox uses
it too until stage lights exist.

## Fog

`HSD_FogDesc`: `type +0`, `fogadjdesc +4`, `start +8`, `end +0xC`, `color
+0x10`. `HSD_FogSet` maps it to `GXSetFog(type, start, end, near, far,
color)`. Types: 2 linear, 4 exp, 5 exp2, 6 revexp, 7 revexp2. The shader
computes the linear factor from the view distance and approximates the
exponential ones. The character-select fog (`500..1000`) is far behind the
viewer's camera range, so it does not tint a model; it would once the menu
background exists.

## Not ported yet

- Point/spot attenuation functions (`GX_DA_*`, `HSD_LightAttn`); parsed and
  dumped, evaluated as directional. No tested scene uses them yet.
- GX's exact specular attenuation polynomial (using Blinn-Phong instead).
- Pseudo-lights: Flat Zone's `ftCommonData->x7E4_scaleZ` (stage-specific
  flattening, `fighter.c:825`) and mushroom/Giant scaling (`ftLib_SetScale`
  changes `x34_scale.y` at runtime). Game & Watch's permanent width
  (`ftGw` attributes `x0_GAMEWATCH_WIDTH`) is handled with model scaling.
- Fog range adjustment (`GXSetFogRangeAdj`) and fog animation.

## How to verify

```sh
./build/native/melee-demo --dump-lights
SDL_VIDEODRIVER=offscreen ./build/native/melee-demo --view --frames 3 \
    --screenshot /tmp/view.bmp
```

Compare `--dump-lights` against `mncharsel.c:4292` and the parse table above.
