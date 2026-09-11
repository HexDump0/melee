# Handoff: P-204 increment 1 — real HSD material/TEV state

**Date:** 2026-09-10
**Agent:** opencode (deepseek-flash)
**Tree state:** builds warning-free; 240-frame scripted and 600-frame
ASan+UBSan runs clean; `--inspect` bounds/triangles unchanged (textures
31 -> 32, see below).

## What landed

- `hsd/model.h/.c`: parse `HSD_MObjDesc` material (ambient/diffuse/specular/
  alpha/shininess), `HSD_PEDesc`, and the full `HSD_TObjDesc` chain (id, src,
  wrap, `blend_flags`, `blending`, `HSD_TObjTevDesc`). Derived GX state:
  `channel_lit`, `initial_ras`, `diffuse_mul`, `specular_tev`, alpha-test,
  blend factors, Z func/update. `GX_VA_TEX1` coordinates are decoded into the
  vertex format and a second `MakeTextureMtx` is stored per batch.
- `main.c`: the model shader now implements `MObjMakeTExp`/`TObjMakeTExp`:
  material-constant vs RAS initial stage, colormap/alphamap formulas, the
  `RENDER_DIFFUSE` lit stage, two texture units (`TObjDesc.src` selects TEX0/
  TEX1), GX channel lighting (`mat_ambient*ambient_light + light*N·L`),
  `GXCompare`/`GXAlphaOp` discard, and `HSD_SetupPEMode` blend/Z via GL state.
- Texture filters/LOD: per-TObj `HSD_TexLODDesc` min/mag filters with the
  `TObjSetup` CI/non-mipmapped rules, shader LOD bias, anisotropy when the
  driver exposes the extension; lightmap `repeat` skips the alpha map.
- Mr. Game & Watch: `ftGameWatchAttributes.x0` flattening (root X = 0.01) and
  the costume-0 diffuse override (`ftMaterial_800BFB4C`). His outline/face TEV
  is runtime state and still missing.
- Per-character model scale: `parts_apply` now reads
  `ftCo_DatAttrs.model_scaling` (+0x8C) from `ftData<Char>` and
  `hsd_model_pose_apply` applies it to the root joint, matching
  `Fighter_UpdateModelScale`. Before this, Bowser/DK rendered at raw archive
  size (Bowser 1.8x too tall) and Kirby/Pikachu too big. Mario's scale is
  1.10, so `--inspect` bounds changed to
  `[-8.31 -0.31 -2.97]..[8.32 15.63 3.94]` (documented in `STATE.md`).
- Lightmap phases: a TObj's `TEX_LIGHTMAP_*` bits route it to the diffuse
  (0x10/0x40), specular (0x20) or EXT (0x80) accumulator, matching
  `TObjMakeTExp`. This fixed normal Luigi/Mario rendering grey because a
  SPECULAR lightmap was being blended into the lit colour (G-044). The
  specular accumulator is `mat.specular` -> map colormap -> specular light
  channel -> added to the diffuse result.
- `--dump-tev` prints the per-batch material state; `--inspect --list-parts`
  now reports the forced `RENDER_TOON` bit in `rm`.

## Output changes (intentional)

- `--inspect` textures: **31 -> 32** (the TEX1 TObj image is now decoded).
- `--inspect` Mario bounds: **`[-7.56 -0.28 -2.70]..[7.57 14.21 3.58]` ->
  `[-8.31 -0.31 -2.97]..[8.32 15.63 3.94]`** because `ftData.model_scaling`
  (Mario 1.10) is now applied like `Fighter_UpdateModelScale`.
  `TESTING.md` and `STATE.md` updated.
- The viewer grid is a fixed world-space grid (1.5-unit cells at y=0) so
  character sizes are comparable and it no longer rescales during animation.
- Appearance: per-vertex `CLR0` is no longer multiplied into every material;
  materials use the decomp's channel/colormap/lightmap state, and specular
  lightmaps no longer tint the diffuse colour. Closer to the game
  (screenshots in `/tmp/opencode/p211/after/`, not committed).

## Exact next action

1. **Real light values.** The channel/specular math is ported, but the light
   colours/directions are the viewer's stand-in set. Port `HSD_LObj`
   (`lobj.c`), `HSD_LObjSetup`/`HSD_SetupChannelMode` and a stage's light
   list so ambient/diffuse/specular match the game.
2. **Lightmap chains.** Port `TObjMakeTExp`'s `lightmap_done`/`repeat`
   semantics and the SPECULAR/EXT passes so multi-lightmap materials compose
   exactly.
3. **`HSD_TObjTev` active overrides** (`MakeColorGenTExp`, `tobj.c:625`).
   All 9 tested fighter archives have `active == 0`, so this is unverified
   ground; dump first.
4. **Toon textures.** `tobj_toon` is registered per stage
   (`grpura.c`), not per fighter; wire it when stages land.

## Verification run

```
cmake --build build/native --clean-first -j4     # warning-free
./build/native/melee --inspect              # 6328 tris, 32 textures
./build/native/melee --dump-tev | head
SDL_VIDEODRIVER=offscreen ./build/native/melee --view --frames 3 \
    --screenshot /tmp/view.bmp
SDL_VIDEODRIVER=offscreen ./build/native/melee --scripted --frames 240
# Completed 240 render frames, 240 simulation ticks
SDL_VIDEODRIVER=offscreen ASAN_OPTIONS=detect_leaks=0 \
    ./build/native-asan/melee --scripted --frames 600
# Completed 600 render frames, 600 simulation ticks
```
```
