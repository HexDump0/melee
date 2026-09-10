# Handoff: P-204 increment 1 — real HSD material/TEV state

**Date:** 2026-09-10
**Agent:** opencode (deepseek-flash)
**Tree state:** builds warning-free; 240-frame scripted and 600-frame
ASan+UBSan runs clean; `--inspect` bounds/triangles unchanged (textures
31 -> 32, see below).

## What landed

- `demo_model.h/.c`: parse `HSD_MObjDesc` material (ambient/diffuse/specular/
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
- `--dump-tev` prints the per-batch material state; `--inspect --list-parts`
  now reports the forced `RENDER_TOON` bit in `rm`.

## Output changes (intentional)

- `--inspect` textures: **31 -> 32** (the TEX1 TObj image is now decoded).
  `TESTING.md` and `STATE.md` updated.
- Appearance: per-vertex `CLR0` is no longer multiplied into every material;
  materials use the decomp's channel/colormap state. This is closer to the
  game (Kirby/Fox/Pikachu/Marth screenshots in `/tmp/opencode/p211/after/`,
  not committed).

## Exact next action

1. **Specular (`RENDER_SPECULAR`, 1<<3).** `MObjMakeTExp` adds
   `mat.specular * RAS1` (secondary colour) where RAS1 is the specular
   lighting channel. This needs the scene's lights: port `HSD_LObj`
   (`lobj.c`) and the `HSD_SetupChannelMode` `arg0 & 8` branch
   (`state.c:152`), then feed `u_specular_light` from the active lights.
   Don't invent a highlight before the light set is real.
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
./build/native/melee-demo --inspect              # 6328 tris, 32 textures
./build/native/melee-demo --dump-tev | head
SDL_VIDEODRIVER=offscreen ./build/native/melee-demo --view --frames 3 \
    --screenshot /tmp/view.bmp
SDL_VIDEODRIVER=offscreen ./build/native/melee-demo --scripted --frames 240
# Completed 240 render frames, 240 simulation ticks
SDL_VIDEODRIVER=offscreen ASAN_OPTIONS=detect_leaks=0 \
    ./build/native-asan/melee-demo --scripted --frames 600
# Completed 600 render frames, 600 simulation ticks
```
```
