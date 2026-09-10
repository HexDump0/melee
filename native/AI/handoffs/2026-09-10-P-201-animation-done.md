# Handoff: P-201 — animation playback landed, fidelity gaps remain

**Date:** 2026-09-10
**Agent:** opencode (deepseek-flash)
**Commit:** 6665bd5f2 (backend 0e1a974d2)
**Tree state:** builds warning-free; 240-frame scripted run and 600-frame
ASan+UBSan run clean

## What I did

- `demo_aobj.c/.h`: literal port of `fobj.c` (parseFloat/parseWait/parsePackInfo,
  CON/LIN/SPL0/SPL/SLP/KEY, `FObjUpdateAnim`, `HSD_FObjInterpretAnim`).
- `demo_anim.c/.h`: walks the `Pl<Char>AJ.dat` concatenated sub-archives, reads
  `FigaTree` clips, loads `ftPartsTable`/skip lists from `PlCo.dat`
  (`ftLoadCommonData`), binds nodes to joints like `ftAnim_8006F4C8`, and
  poses the model.
- `demo_model.c/.h`: keeps raw per-vertex skin inputs (position/normal/group
  selector), stores per-batch envelope groups and shared joints, and re-skins
  every evaluated frame with the engine's rigid/blended/shared matrix rules and
  the dynamic envelope `right` matrix.
- `main.c`: viewer animation UI (`--animate`, `--clip`, `--anim-frame`,
  `--anim-speed`, `--list-clips`, `--dump-clip`; keys `A`, `,`/`.`, `Z`/`X`,
  `M`; `Y` now toggles hidden parts), fixed 60 Hz animation accumulator, and a
  second independently decoded model so both sandbox fighters animate.
- Docs: `learnings/hsd_animation.md`, `STATE.md`, `README.md`, `TASKS.md`,
  `GOTCHAS.md` G-036..G-039, `AGENTS.md` §0.1 (port the decomp, do not
  reinterpret it).

## Exact next action

Pick up P-207 (expressions) from `TASKS.md`: `SETBYTE`/`SETFLOAT` channels are
received by `demo_fobj_interpret` but ignored in `demo_model_pose_channel`.
Wire them to `ftParts_80074B0C`-equivalent visibility, then verify blinking on
Mario's `Wait1`/`Damage` and Game & Watch's parts (P-412). P-208 (IK) and
P-209 (matanim) are the other fidelity gaps.

## What I tried that did not work

- A stateless "parse FObj into segments, evaluate at t" rewrite. It matched
  early frames but produced garbage at the clip end (`-6.3e32`), which was the
  "Mario going crazy" report. Replaced with the literal state machine; a
  differential test against a Python transcription of `fobj.c` now shows 0
  mismatches on 5661 samples (worst 6.4e-7).
- Treating the `Fighter_804D6540` skip list as a joint filter. It only inserts
  phantom part slots; node i still maps to joint i. Fixed.
- A node->joint shift search (`DEMO_ANIM_SHIFT` debug, removed): no constant
  shift produces a correct pose, confirming the 1:1 mapping.
- Kirby "turning away" looked like a bug but is correct: his Melee idle hops to
  look back (SmashWiki). Do not "fix" it.

## Open questions

- H-3: priority after the animation foundation — expressions vs audio vs WASM?
- Does the sandbox camera framing after 240 scripted frames match the owner's
  expectations (fighters can drift apart)? P-206.

## Files touched / claimed

- `native/demo_aobj.c/.h`, `native/demo_anim.c/.h` (new)
- `native/demo_model.c/.h`, `native/main.c`, `native/CMakeLists.txt`
- `native/AI/`: `AGENTS.md`, `STATE.md`, `TASKS.md`, `README.md`,
  `learnings/hsd_animation.md`, `gotchas/GOTCHAS.md`

## Verification run

```
cmake --build build/native -j4                       # clean
./build/native/melee-demo --inspect                  # 6328 tris, bounds unchanged
./build/native/melee-demo --list-clips               # 195 Mario clips
SDL_VIDEODRIVER=offscreen ./build/native/melee-demo --view --animate \
    --clip Wait1 --anim-frame 0/25 --frames 1 --screenshot /tmp/anim.bmp
SDL_VIDEODRIVER=offscreen ./build/native/melee-demo --scripted --frames 240
# Completed 240 render frames, 240 simulation ticks
ASAN_OPTIONS=detect_leaks=0 ./build/native-asan/melee-demo --scripted --frames 600
# Completed 600 render frames, 600 simulation ticks
```
