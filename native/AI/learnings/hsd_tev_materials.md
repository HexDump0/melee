# Learning: HSD materials, GX channels and TEV combiner (P-204)

**Date:** 2026-09-10
**Agent:** opencode (deepseek-flash)
**Evidence:** `native/demo_model.c` (`parse_material`), `native/main.c`
(MODEL_VS/MODEL_FS, `draw_batch`), `--dump-tev` on 9 `Pl*Nr.dat` archives,
screenshots under `/tmp/opencode/p211/after/` (not committed). Sources:
`src/sysdolphin/baselib/mobj.c` (`MObjLoad`, `MObjMakeTExp`), `tobj.c`
(`TObjMakeTExp`), `state.c` (`HSD_SetupPEMode`, `HSD_SetupChannelMode`),
`tev.c` (`HSD_SetupTevStage`), `texp.h` (`HSD_TevConf`/`HSD_TevDesc`).

## Facts

### Where a material's state comes from

`HSD_MObjDesc` (mobj.h): `rendermode` +4, `texdesc` +8, `mat` +0xC,
`pedesc` +0x14. `HSD_MObjLoad` copies the material and **forces
`RENDER_TOON` (1<<12) on every material** (mobj.c:158); it has no effect
unless a global toon texture is registered (`HSD_MObjSetToonTextureImage`,
only used by `grpura.c` for a stage).

`HSD_TObjDesc` (tobj.h): `id` +8, `src` +0xC, `wrap_s` +0x34, `wrap_t` +0x38,
`repeat_s/t` +0x3C/0x3D, `blend_flags` +0x40 (becomes `HSD_TObj.flags`),
`blending` +0x44, `imagedesc` +0x4C, `tlutdesc` +0x50, `tev` +0x58.

`blend_flags` bit layout (tobj.h:60-124): bits 0-3 `tobj_coord`; bits 16-19
`TEX_COLORMAP_*`; bits 20-23 `TEX_ALPHAMAP_*`; bits 4-8 `TEX_LIGHTMAP_*`
(DIFFUSE/SPECULAR/AMBIENT/EXT/SHADOW).

### Channel selection (lighting)

`MObjMakeTExp` picks the initial TEV input:
- `RENDER_VERTEX` (1<<1): raster colour (`RAS`) with alpha `RASA`.
- otherwise: `mat->diffuse` constant and `mat->alpha`.

`HSD_SetupChannelMode(rendermode & 7)` decides what `RAS` is (`state.c:143`):
- `== 4` (`RENDER_DIFFUSE`, 1<<2): the **lit** channel `_60`, whose ambient is
  `matstate.ambient * ambient_light` and whose `mat_color` is white; the
  lighting equation is `ras = mat_ambient*ambient_light + Σ light*N·L`.
  Note the registered material colour is used, **not** the per-vertex colour.
- `== 2` / default: channel disabled -> unlit vertex colour.

Then `RENDER_DIFFUSE` adds a final stage `color *= RAS` (and alpha `*= RASA`).

**Most Melee fighter materials have `rendermode & 7 == 4` and *not*
`RENDER_VERTEX`**, so per-vertex `CLR0` is ignored: the initial colour is the
material constant and the texture colormap usually REPLACEs it. The old
fixed-function port multiplied by `CLR0` anyway, which is why colours drifted.

### TEV combiner

The GX equation is `out = d + (1-c)*a + c*b` (+bias, ×scale, optional clamp);
`GX_TEV_SUB` subtracts the middle term. HSD's `HSD_TExpColorIn` maps its
arguments straight to GX A/B/C/D (`TExp2TevDesc`). `TObjMakeTExp` never uses
`(a-b)*c+d`, so MODULATE works out as `texture * prev` only under this
ordering (see G-042).

Port mappings (all with clamp enabled, bias 0, scale 1):

| `TEX_COLORMAP_*` | formula (prev, texture) |
|---|---|
| NONE/PASS (0/6) | prev |
| ALPHA_MASK (1) | `mix(prev, tex.rgb, tex.a)` |
| RGB_MASK (2) | `mix(prev, tex.rgb*tex.rgb, tex.rgb)` |
| BLEND (3) | `mix(prev, tex.rgb, blending)` |
| MODULATE (4) | `prev * tex.rgb` |
| REPLACE (5) | `tex.rgb` |
| ADD (7) | `clamp(prev + tex.rgb)` |
| SUB (8) | `clamp(prev - tex.rgb)` |

`TEX_ALPHAMAP_*`: NONE/PASS prev; ALPHA_MASK `mix(prev, tex.a*tex.a, tex.a)`;
BLEND `mix(prev, tex.a, blending)`; MODULATE `prev*tex.a`; REPLACE `tex.a`;
ADD/SUB add/sub.

Textures are applied in `TObjMakeTExp` in two passes keyed by
`tobj_lightmap`: DIFFUSE/AMBIENT first, then SPECULAR, then EXT. The fighter
main texture always carries `TEX_LIGHTMAP_DIFFUSE` (0x10), so it is applied in
the first pass with `repeat = 0` (both colour and alpha). A second texture is
usually an EXT/reflection map using `GX_TG_TEX1` coordinates.

### Alpha test, blend and Z (`HSD_SetupPEMode`, state.c:203)

Without a custom `HSD_PEDesc`:
- blend `GX_BM_BLEND` with `GX_BL_SRCALPHA`/`GX_BL_INVSRCALPHA` **only when
  `RENDER_XLU` (1<<30)**; otherwise `GX_BM_NONE`.
- when `RENDER_XLU && !RENDER_NO_ZUPDATE`: alpha compare `GX_GREATER, ref 0`
  (discard alpha == 0), z compare before texture.
- Z: always enabled, `GX_ALWAYS` when `RENDER_ZMODE_ALWAYS` (1<<27) else
  `GX_LEQUAL`; Z writes disabled by `RENDER_NO_ZUPDATE` (1<<29).

With a `HSD_PEDesc`, all of the above (including `alpha_comp0/op/comp1` and
the GX blend factors/topology) comes from the descriptor.

### Multi-texture

The VBO vertex format now carries both `GX_VA_TEX0` and `GX_VA_TEX1`; the
shader picks per texture unit from `TObjDesc.src` (4 = TEX0, 5 = TEX1). The
second TObj's `MakeTextureMtx` is uploaded as `u_texmtx[1]`.

## How to verify

```sh
./build/native/melee-demo --dump-tev | head     # per-batch GX material state
SDL_VIDEODRIVER=offscreen ./build/native/melee-demo --view --frames 3 \
    --screenshot /tmp/view.bmp
```

`--dump-tev` prints `rm`, material ambient/diffuse/specular/alpha/shininess,
PE fields and every TObj (texture, id, src, flags, colormap/alphamap, active
override). Cross-check a few against `MObjMakeTExp` branches.

Decoding the TEX1 TObj adds one texture to `PlMrNr.dat` (31 -> 32), so the
`--inspect` tail changed; that is expected and documented in `STATE.md`.

## Not ported yet

- **Specular** (`RENDER_SPECULAR`, 1<<3): `MObjMakeTExp` adds
  `mat.specular * RAS1` (secondary colour = specular lighting channel).
  RAS1 comes from the scene's specular lights (`HSD_LObj`), which the port
  does not have yet; adding an invented highlight would be worse than none.
  (All Mario materials have specular 255 and shininess 50.)
- **Lightmap repeat semantics** (`lightmap_done`) and SPECULAR/EXT lightmap
  chains beyond a single EXT texture. Fighters use one DIFFUSE + at most one
  EXT map.
- **`HSD_TObjTev` active overrides** (`MakeColorGenTExp`). Every TObj in the
  9 tested fighter archives has `active == 0`; verify before relying on it.
- **Toon texture** (`tobj_toon`): registered per stage, not per model.
- **Per-scene `HSD_LObj` setup and fog**: render-side state that lives in game
  code, not the model archive. The shader currently uses the viewer's
  stand-in light set for the channel equation.
