# Testing and verification

## Rule zero

**Build the baseline before you change anything.** If the baseline is broken,
that is your first task; do not pile changes on top.

## Smoke tests (run these every change)

```sh
# Configure + build (warnings must be clean for touched files)
cmake -S native -B build/native -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build/native -j4

# Parser test, no window, no GPU needed
./build/native/melee-demo --inspect
```

Expected tail:

```
Decoded PlMrNr.dat: 6328 triangles, 31 textures; bounds [-7.56 -0.28 -2.70] to [7.57 14.21 3.58]
```

```sh
# Animation clip table and a deterministic animated frame
./build/native/melee-demo --model PlMrNr.dat --list-clips | head
SDL_VIDEODRIVER=offscreen ./build/native/melee-demo --view --animate \
    --clip Wait1 --anim-frame 25 --frames 1 --screenshot /tmp/anim.bmp
```

Expected: 195 clips for Mario (`Wait1` 50 frames), and
`Rendered 1 viewer frames`. Screenshots at frames 0 and 25 must differ.

```sh
# Full loop + render, headless
SDL_VIDEODRIVER=offscreen ./build/native/melee-demo --scripted --frames 240 \
    --screenshot /tmp/smoke.bmp
```

Expected tail: `Completed 240 render frames, 240 simulation ticks`.

If either output changes, explain why in the commit and update `STATE.md`.

## Sanitizer run (required for parser/memory changes)

```sh
cmake -S native -B build/native-asan -DCMAKE_BUILD_TYPE=Debug -DDEMO_SANITIZE=ON
cmake --build build/native-asan -j4
SDL_VIDEODRIVER=offscreen ASAN_OPTIONS=detect_leaks=0 \
    ./build/native-asan/melee-demo --scripted --frames 600
```

Leaks are disabled because the GL driver leaks at exit; the port's own
allocations are freed in `destroy_visual()` and `demo_model_free()`.
Any ASan/UBSan report in port code is a release blocker.

## Visual verification (no X11 on the agent machine)

The reference machine has no display; SDL's `offscreen` driver plus Mesa works.
Always capture a screenshot and inspect it:

```sh
SDL_VIDEODRIVER=offscreen ./build/native/melee-demo --view \
    --angle 180 --elevation -5 --screenshot /tmp/model.bmp
magick /tmp/model.bmp /tmp/model.png
# then open/attach /tmp/model.png
```

Useful angles: `0` front, `90`/`270` sides, `180` back, `210` three-quarter.

For gameplay framing:

```sh
SDL_VIDEODRIVER=offscreen ./build/native/melee-demo --scripted --frames 120 \
    --screenshot /tmp/gameplay.bmp
```

## Cross-character regression matrix

Run before merging parser changes:

```sh
for m in PlMrNr.dat PlFxNr.dat PlPkNr.dat PlClNr.dat PlDkNr.dat; do
  ./build/native/melee-demo --model "$m" --inspect | tail -1
done
```

Capture the numbers into `STATE.md` if they change.

## Interpreting failures

| Symptom | First thing to check |
|---|---|
| `joint graph contained no supported triangles` | public symbol root detection; wrong pointer base |
| Triangles render as shards | `PNMTXIDX/3` group mapping, rigid vs blended groups, direct-attr offset |
| Model is a blob | rigid-group bind matrices missing/wrong (see learning on skinning) |
| Model has no textures | `imagedesc` offset, unsupported `CI` format |
| Colors wrong or UVs offset | material byte order, texture matrix, TObj flags |
| No ticks in scripted run | `--scripted` requires frame loop; check accumulator |
| GL context fails headlessly | keep OpenGL 2.1, `SDL_VIDEODRIVER=offscreen` |

## Human-in-the-loop checks

Agents cannot judge feel. When a change affects controls, camera or timing,
add a checklist to `TASKS.md` under "needs a human" and ask the owner to run:

```sh
./build/native/melee-demo
```

Checklist format: exact keys, expected result, and what a regression looks like.
