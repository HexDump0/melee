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

Textures are applied in `TObjMakeTExp` in phases keyed by `tobj_lightmap`:

1. **DIFFUSE/AMBIENT (0x10/0x40)** modify the diffuse accumulator. The fighter
   main texture always carries 0x10, so it is applied here with `repeat = 0`
   (both colour and alpha).
2. **SPECULAR (0x20)** modify the specular accumulator: it starts at
   `mat.specular`, then each spec map applies its colormap, then the result is
   multiplied by the specular lighting channel `RAS1`
   (`mat.specular * clamp(spec_light * pow(N·H, shininess))`), and finally
   added to the diffuse result. Examples: Mario's batches 39/61 and Luigi's
   face (`0x30020`/`0x40020`) — a spec map blended into diffuse turns the face
   grey (G-044).
3. **EXT (0x80)** modify the `ext = diff` accumulator last; these are the
   reflection maps (Kirby `0x30081`, coord 1, `GX_TG_TEX1`).

The shader gets a `u_tex_phase[2]` per texture slot and routes each sampled
texture to the right accumulator. Fighters use at most one map of each kind.
`repeat = (lightmap_done & tobj_lightmap)` skips the **alpha** map for a phase
already applied; the port passes `u_tex_repeat[1]` per slot.

### Texture filters and LOD

`HSD_TObjSetup` picks the min filter from `HSD_TexLODDesc.minFilt` (default
`GX_LIN_MIP_LIN` when the descriptor is NULL), clears the mip bits when
`ImageDesc.mipmap == 0`, and downgrades `GX_LIN_MIP_LIN` to `GX_LIN_MIP_NEAR`
for CI images (`tobj.c:1239`). Mag filter is `TObjDesc.magFilt`. The port
applies these per draw (`gx_min_filter`/`gx_mag_filter`) and feeds
`HSD_TexLODDesc.LODBias` to `texture(sampler, uv, bias)` because core GL has
no texture LOD bias. `GXAnisotropy` maps to
`GL_EXT_texture_filter_anisotropic` when the driver exposes it.

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

- **Stage light sets.** The character-select `HSD_LObj` set is used for the
  viewer (see `learnings/hsd_lights_fog.md`); match lighting comes from
  stage-created light lists (`src/melee/gr/*`) and is M4 work.
- **Point/spot attenuation** (`GX_DA_*`, `HSD_LightAttn`) and GX's exact
  specular attenuation polynomial (Blinn-Phong is used).
- **`HSD_TObjTev` active overrides** (`MakeColorGenTExp`). Every TObj in the
  9 tested fighter archives has `active == 0`; the values are parsed and
  dumped but the expression graph is not evaluated.
- **Alpha changes from SPECULAR/EXT maps** (their alphamap result is dropped;
  all fighter specs use alphamap NONE).
- **Toon texture** (`tobj_toon`): registered per stage, not per model.
- **Runtime material swaps**: metal, invisibility, giant/mushroom and damage
  flashes do not live in `Pl*Nr.dat`. `ftMaterial_800BF2B8`
  (`src/melee/ft/ftmaterial.c`) swaps in `ft_804D6580`/`ft_804D6588` and ORs
  `RENDER_XLU | RENDER_NO_ZUPDATE`; `ftmetal.c` reads `is_metal`. A raw model
  viewer will never show the metal look — that needs the fighter state (M3).
