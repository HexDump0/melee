# Handoff: P-619 stage viewer (S4 next)

**Date:** 2026-09-12
**Agent:** opencode (deepseek-v4.1-flash)
**Commit:** this commit
**Tree state:** builds warning-free; `ctest --test-dir build/native` is 10/10
(new `decomp_stage`, `decomp_stage_cam`); model-mode screenshots unchanged.

## What I did

- Converter version 9: `conv_stage_maphead` walks `Gr*.dat`'s `map_head`
  `UnkStageDat` table (per-map joint tree, anim joints, camera desc, light
  list, fog desc).  Converter version 9 also walks light animations
  (`conv_aobjdesc_ref`), `POBJ_SHAPEANIM` shape sets
  (`conv_shapesetdesc`) and range-checks every light-list read; see G-074.
- `hsd_scene_load_stage_all`: a stage is one Ground GObj per `map_head` id
  (platform + background layers), so every map joint is loaded and drawn
  (`HsdScene.stage_roots[]`); the smallest-bounds map supplies the
  camera/lights/fog.  `hsd_scene_load_stage` still isolates one map.
- `hsd_scene_load_stage`: loads a stage map through the same
  `HSD_ArchiveParse` path, then `HSD_JObjLoadJoint` + `lb_80013B14` (camera) +
  `lb_80011AC4` (lights) + `HSD_FogLoadDesc` (fog), faithful to
  `Ground_GetStageGObj`/`Ground_801C1E94`.
- `render_scene` stage mode: `--stage`, `--fighter`, `--stage-map`,
  `--stage-cam`, `--no-fighter`; a posed fighter is loaded through the normal
  `Pl*Nr.dat` route (ftData scale + visibility) and placed at the origin.
  `M` toggles model/stage, `N`/`P` cycle (stages in stage mode), `,`/`.`
  cycle map layers, `F` fighter, `K` orbit/stage camera.  HUD shows mode,
  map layer, fighter and camera.
- All 71 `Gr*.dat` load headless (`--no-fighter --no-gl` sweep) and a
  70-stage interactive cycle is clean; ASan/UBSan clean on
  GrNBa/GrNSr/GrPs/GrNLa/GrZe.
- `test_decomp_render --stage/--fighter/--stage-map/--stage-cam` covers the
  path headless; ctest `decomp_stage` + `decomp_stage_cam` (10/10 total).
- Evidence shots: `/tmp/stagef2.bmp` (orbit, Mario on Battlefield),
  `/tmp/stagecam.bmp` (stage camera), `/tmp/allmaps.bmp` (platform +
  background layers).  Stage surface colors are not yet verified against the
  game (filed P-621).

## Exact next action

Start P-620 (S4).  First target: `PADRead` (see `2026-09-11-S1-boot-triage.md`
2nd phase) plus a deterministic scripted input source, then get past
`gm_Scene_MemCard_OnFrame` into a match.  The stage viewer's loading path
(`hsd_scene_load_stage`) is what the compiled `gr` code will replace.

## What I tried that did not work

- Treating `map_head` as a nested HSD archive (see G-073).
- Auto-skipping to the first non-empty map: GrNBa maps 1..5 are huge
  background layers; smallest-bounds selection is the right heuristic.

## Open questions

- P-621: are the stage material colors correct?  Needs a Dolphin capture.

## Files touched / claimed

`native/decomp/assets/hsd_convert.{c,h}`, `native/decomp/hsd/hsd_scene.{c,h}`,
`native/decomp/render/render_scene.{c,h}`,
`native/decomp/render/viewer_main.c`, `native/tests/test_decomp_render.c`,
`native/CMakeLists.txt`, `native/AI/*`.

## Verification run

```sh
cmake --build build/native -j4                       # warning-free
ctest --test-dir build/native --output-on-failure    # 10/10
./build/native/test_decomp_render --stage GrNBa.dat --fighter PlMrNr.dat \
    --shot /tmp/stagef2.bmp                          # PASS
./build/native/test_decomp_render --stage GrNBa.dat --fighter PlMrNr.dat \
    --stage-cam                                      # PASS
SDL_VIDEODRIVER=offscreen ./build/native/melee_decomp_viewer \
    --stage GrNBa.dat --fighter PlMrNr.dat --frames 2 --hidden \
    --shot /tmp/vstage.bmp
```
