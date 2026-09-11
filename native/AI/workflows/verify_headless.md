# Workflow: verify headlessly

Agents have no display. This is the loop that proves rendering works.

## The five commands

```sh
# 1. Build
cmake -S native -B build/native -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build/native -j4

# 2. Parse-only (fastest, no GPU)
./build/native/melee --inspect

# 3. Model viewer frame(s) + screenshot
SDL_VIDEODRIVER=offscreen ./build/native/melee --view --frames 3 \
    --screenshot /tmp/view.bmp

# 4. Gameplay frames + screenshot
SDL_VIDEODRIVER=offscreen ./build/native/melee --scripted --frames 240 \
    --screenshot /tmp/game.bmp

# 5. Sanitizers (parser/memory changes)
cmake -S native -B build/native-asan -DCMAKE_BUILD_TYPE=Debug -DMELEE_SANITIZE=ON
cmake --build build/native-asan -j4
SDL_VIDEODRIVER=offscreen ASAN_OPTIONS=detect_leaks=0 \
    ./build/native-asan/melee --scripted --frames 600
```

## Viewing the result

```sh
magick /tmp/view.bmp /tmp/view.png
```

Then attach `view.png` to the handoff note with one sentence about what
changed. A screenshot with no description is not verification.

## Deterministic frames

- `--scripted` forces one simulation tick per rendered frame, so `--frames N`
  is exactly N ticks regardless of wall-clock speed. Use it for reproducible
  captures.
- The default camera (`--view`) starts at `--angle 210 --elevation -15`. For
  comparisons, always pass the same angle/elevation.
- `--part N --part-mode only|hide` makes captures deterministic for a part.

## What headless cannot verify

- Input feel, timing, and controller mapping.
- Audio.
- Window resize/DPI, vsync behavior.
- Whether a Melee player thinks it looks right.

Put those in `TASKS.md` under "needs a human" with an exact checklist instead of
guessing.
