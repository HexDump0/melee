# Testing and verification

## Build environment / dependencies (fresh machine)

The reference machine is Arch Linux.  Exact packages (as of 2026-09-12):

```sh
# Toolchain + CMake
sudo pacman -S base-devel cmake pkgconf

# Prototype (64-bit): SDL2 API (Arch ships sdl2-compat over SDL3) + desktop GL
sudo pacman -S sdl2-compat mesa

# Compiled decomp targets (32-bit, ADR-0012):
#   gcc already supports -m32 once lib32-gcc-libs is present
sudo pacman -S lib32-gcc-libs
#   32-bit EGL/GLESv2 (libglvnd loader + Mesa drivers) for the GX renderer
sudo pacman -S lib32-libglvnd lib32-mesa
#   32-bit SDL3 for the interactive compiled viewer (ADR-0014; needs the
#   64-bit sdl3 headers, which the sdl3 package installs)
sudo pacman -S sdl3 lib32-sdl3
```

What each target needs:

| Target | Bits | Needs |
|---|---|---|
| `melee`, `test_math`, `test_decomp_mtx` | 64 | SDL2, desktop GL, libm |
| `test_decomp_hsd`, `test_decomp_render`, `melee_decomp_boot` | 32 | lib32 glibc/gcc, 32-bit EGL/GLESv2 (`test_decomp_render`) |
| `melee_decomp_viewer` (P-611) | 32 | additionally `lib32-sdl3` + `sdl3` headers |

The viewer target is skipped by CMake when `/usr/lib32/libSDL3.so` is missing,
so the rest of the port still builds on machines without multilib SDL3.
The disc image path and its local-only rule are in `STATE.md`.

## Rule zero

**Build the baseline before you change anything.** If the baseline is broken,
that is your first task; do not pile changes on top.

## Smoke tests (run these every change)

```sh
# Configure + build (warnings must be clean for touched files)
cmake -S native -B build/native -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build/native -j4

# Parser test, no window, no GPU needed
./build/native/melee --inspect

# Unit tests (CTest: hand matrix math, compiled HSD_MtxSRT parity, S0 HSD probe)
ctest --test-dir build/native --output-on-failure
```

Expected tail:

```
Decoded PlMrNr.dat: 6328 triangles, 32 textures; bounds [-8.31 -0.31 -2.97] to [8.32 15.63 3.94]

`32` (was `31`) since P-204 decodes the TEX1 TObj; see `STATE.md`.
```

```sh
# Animation clip table and a deterministic animated frame
./build/native/melee --model PlMrNr.dat --list-clips | head
SDL_VIDEODRIVER=offscreen ./build/native/melee --view --animate \
    --clip Wait1 --anim-frame 25 --frames 1 --screenshot /tmp/anim.bmp
```

Expected: 195 clips for Mario (`Wait1` 50 frames), and
`Rendered 1 viewer frames`. Screenshots at frames 0 and 25 must differ.

```sh
# Full loop + render, headless
SDL_VIDEODRIVER=offscreen ./build/native/melee --scripted --frames 240 \
    --screenshot /tmp/smoke.bmp
```

Expected tail: `Completed 240 render frames, 240 simulation ticks`.

If either output changes, explain why in the commit and update `STATE.md`.

## Decompiled-port bring-up (S0+, ADR-0010)

The full-tree syntax census is the cheap portability check:

```sh
for f in $(find src -name '*.c'); do
  gcc -std=gnu11 -fsyntax-only -w \
      -include native/decomp/shim/decomp_shim.h \
      -I native/decomp/shim -I src -I extern/dolphin/include "$f"
done
```

Current numbers and error classes are recorded in
`learnings/decomp_port.md`; update that file when the census improves.

Rules for this track:

- Targets that compile upstream decomp code build **32-bit** (`-m32`) per
  ADR-0012 (`test_decomp_hsd` is the example). Do not "fix" pointer-truncation
  symptoms by going 64-bit; see `learnings/decomp_port.md` §6.
- `ctest` includes `decomp_mtx` (64-bit, bitwise SRT parity) and `decomp_hsd`
  (32-bit; needs the disc, SKIPs without it, runs from the repo root).
- `native/decomp/sdk_math.c` is the portable SDK math backend (real code).
  `native/decomp/hsd_port_stubs.c` is **probe-only** display no-ops: never link
  it into a product target.
- Any `src/` change is a gated portability fix per ADR-0011 and must leave the
  GC build green (`python configure.py`, then `ninja` if the MWCC toolchain is
  available).
- New compiled TUs get a differential CTest against the hand copy/oracle before
  the hand copy is deleted (`tests/test_decomp_mtx.c` is the pattern).
- Keep the prototype binary runnable: `--inspect`, `--view --frames` and
  `--scripted --frames` must keep working until the compiled path replaces them.
- Compiled render + viewer smoke test (viewer needs `lib32-sdl3`):

```sh
./build/native/test_decomp_render --width 1280 --height 800 --shot /tmp/c.bmp
./build/native/melee_decomp_viewer --frames 1 --hidden --shot /tmp/v.bmp
# /tmp/v.bmp must match /tmp/c.bmp (RMSE < 0.01); the windowed run is for the
# owner: ./build/native/melee_decomp_viewer
```

## Sanitizer run (required for parser/memory changes)

```sh
cmake -S native -B build/native-asan -DCMAKE_BUILD_TYPE=Debug -DMELEE_SANITIZE=ON
cmake --build build/native-asan -j4
SDL_VIDEODRIVER=offscreen ASAN_OPTIONS=detect_leaks=0 \
    ./build/native-asan/melee --scripted --frames 600
```

Leaks are disabled because the GL driver leaks at exit; the port's own
allocations are freed in `destroy_visual()` and `hsd_model_free()`.
Any ASan/UBSan report in port code is a release blocker.

## Visual verification (no X11 on the agent machine)

The reference machine has no display; SDL's `offscreen` driver plus Mesa works.
Always capture a screenshot and inspect it:

```sh
SDL_VIDEODRIVER=offscreen ./build/native/melee --view \
    --angle 180 --elevation -5 --screenshot /tmp/model.bmp
magick /tmp/model.bmp /tmp/model.png
# then open/attach /tmp/model.png
```

Useful angles: `0` front, `90`/`270` sides, `180` back, `210` three-quarter.

For gameplay framing:

```sh
SDL_VIDEODRIVER=offscreen ./build/native/melee --scripted --frames 120 \
    --screenshot /tmp/gameplay.bmp
```

## Cross-character regression matrix

Run before merging parser changes:

```sh
for m in PlMrNr.dat PlFxNr.dat PlPkNr.dat PlClNr.dat PlDkNr.dat; do
  ./build/native/melee --model "$m" --inspect | tail -1
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
./build/native/melee
```

Checklist format: exact keys, expected result, and what a regression looks like.
