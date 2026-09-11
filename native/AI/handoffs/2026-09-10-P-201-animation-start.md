# Handoff: P-201 — start HSD animation evaluation

**Date:** 2026-09-10
**Agent:** opencode (deepseek-flash)
**Commit:** ce17963a0
**Tree state:** builds; 240-frame scripted run and 120-frame ASan run clean

## What I did

- Fixed invisible player 2 in the match (`5ba65adde`): a `Visual` was copied
  before `compile_model`, so P2's `batch_lists` were all zero. See G-035.
- Fixed the texture pipeline: `MakeTextureMtx` applied per batch (`9cbdc9b59`,
  Mario's mirrored cap "M" is whole) and mipmapped trilinear filtering
  (`f25ede4b1`).
- Honoured `JOBJ_HIDDEN` and material z-mode bits, added visibility slot
  cycling, part framing, `--extract`, `--zoom`, `--no-cull` (`d2e2e3d0b`,
  `fb3960603`, `4a66488f8`).
- Refreshed `STATE.md`, `README.md`, `GOTCHAS.md` (G-035) and wrote the P-201
  brief into `TASKS.md` (`ce17963a0`).

## Exact next action

Implement P-201 per the brief in `native/AI/TASKS.md`:

1. Read `src/sysdolphin/baselib/aobj.c/.h` and find the animation root on
   Mario's archive (`PlyMario5K_Share_matanim_joint`). Confirm with
   `./build/native/melee --inspect --list-parts` and `--extract`.
2. Add `native/hsd/aobj.c/.h`: parse `HSD_AnimJoint` → `HSD_AObjDesc` →
   keys, evaluate step/linear/bezier curves at time `t` (rotation,
   translation, scale per joint).
3. Recompute joint local/world matrices per tick instead of baking one static
   batch list; for envelope parts apply `currentJoint * inverseBind * right`
   at draw time (keep the bind-pose fast path for `--view`).
4. Add `--animate [clip]` to the viewer and screenshot at two times; expect
   different poses, build clean, ASan clean.

## What I tried that did not work

- Copying the `Visual` before `compile_model` to share it — leaves
  `batch_lists` empty; copy after compile or share list IDs explicitly.
- Guessing vertex/UV layout from renders — always use
  `--inspect --list-parts` and `--dump-textures` first.
- Treating visibility slot 1 as a rendering bug — it is the in-game
  reflection/shadow pass; see `STATE.md` and the slot table in `README.md`.

## Open questions

- First clip: Mario Wait is the recommendation. Which clip do you want? — needs a human: no, default to wait.
- Should the viewer loop animation by default? — needs a human: no, yes.

## Files touched / claimed

- `native/main.c` — draw loop, P2 fix, viewer input/flags
- `native/hsd/model.c/.h` — batches carry `dobj_index`, joint tables
- `native/hsd/parts.c/.h` — visibility slots
- `native/gx/texture.c/.h` — decode + texture matrix inputs
- `native/AI/STATE.md`, `TASKS.md`, `README.md`, `gotchas/GOTCHAS.md`
- To create: `native/hsd/aobj.c/.h`

## Verification run

```
cmake --build build/native -j4                     # clean
SDL_VIDEODRIVER=offscreen ./build/native/melee --scripted --frames 240
# Completed 240 render frames, 240 simulation ticks
ASAN_OPTIONS=detect_leaks=0 ./build/native-asan/melee --scripted --frames 120
# Completed 120 render frames, 120 simulation ticks
```
