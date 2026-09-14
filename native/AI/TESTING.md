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
- `ctest` includes `decomp_mtx` (64-bit, bitwise SRT parity), `decomp_hsd`
  (32-bit; needs the disc, SKIPs without it, runs from the repo root) and
  `decomp_gx_direct` (P-608 direct-mode capture; no disc needed).
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
./build/native/test_decomp_render --direct   # GXVert shim capture, no disc
SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy \
    ./build/native/melee_decomp_viewer --frames 1 --hidden --no-hud --shot /tmp/v.bmp
# /tmp/v.bmp must match /tmp/c.bmp (RMSE < 0.01); --no-hud keeps the viewer's
# overlay out of the comparison.  The windowed run is for the owner:
# ./build/native/melee_decomp_viewer
```

## Match end / 1P clear overlay

```sh
MELEE_GAMEOVER_TEST=1 MELEE_NO_CARD=1 ./build/native/melee_decomp_boot \
    --boot-frames 900 --boot-timeout 180 --boot-match 20
ctest --test-dir build/native -R decomp_gameover
```

`MELEE_GAMEOVER_TEST` sets the two 1P rule bits `onEnterDebugVs` leaves clear
(`rules.x4_4`, stock match) on the live VS controller and zeroes player 2's
stocks, which is the same `OUTCOME_ELIMINATION` the last KO of a stage
produces.  `fn_8016D634` then hands the result to `gmregclear.c`'s
`fn_80180630` — the path that crashed in G-145.  Expect:

```
[gameover] forced elimination at frame 140
[gameover] clear scene active frame=253 outcome=2
```

A missing second line means the run died inside the clear overlay.

## 1P Classic screens (approach, CSS, clear)

```sh
SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy MELEE_CLASSIC_TEST=1 \
    MELEE_NO_CARD=1 ./build/native/melee --match --frames 700 --no-hud \
    --shot /tmp/cl.bmp
```

`MELEE_CLASSIC_TEST` forces `GM_CLASSIC` from the boot hook the same way
`match_boot_force` reaches `GM_DEBUG_VS`, which is the only way to reach the
1P screens without driving the menus.  By frame ~700 it is sitting on the
Classic character-select screen.  Keep the branch **before** the debug-VS
stock/logging code in `match_boot_frame`: `log_match_state` walks
`Player_GetEntity`, which is stale once the VS scene is gone and segfaults.

## 1P character select text (SIS engine)

```sh
SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy MELEE_CLASSIC_TEST=1 \
    MELEE_NO_CARD=1 ./build/native/melee --match --frames 700 --no-hud \
    --shot /tmp/cl.bmp
```

Expect `LEVEL` to read `VERY EASY` and `[classic] score_lead=` well above
zero.  `score_lead=0` means SIS strings are losing their last character
(G-149).  Do not probe the level text itself: its box rescales to fit, so a
truncated string occupies almost the same pixels as a correct one.

## Classic splash screen (GS_INTRO_EASY)

```sh
SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy MELEE_INTRO_TEST=1 \
    MELEE_NO_CARD=1 ./build/native/melee --match --frames 420 --no-hud \
    --shot /tmp/intro.bmp
```

`MELEE_INTRO_TEST` forces `GM_DEBUG` and steps its state machine onto state
id 6, which carries the same `GS_INTRO_EASY` scene the 1P modes use.
`gm_SetNextGameModeStateId(n)` lands on state id `n` (it stores `n + 1` and
`gm_801A4014` takes `next_state_id - 1`), and `gm_GetCurrentSceneIndex()`
returns the **state id**, not the `GS_*` kind — both are easy to get wrong.
Expect `[intro] markers nonblack=` well over 10000; ~553 means the
stage-marker chain is not drawing (G-148).

## Missing-`return` census

`src/` is compiled with `-w`, so `-Wreturn-type` never fires in a normal
build.  Re-run the census after every `decomp/` re-pin; the recipe, the
retail-DOL method for deciding each site, and the current verdict table are in
`learnings/decomp_port.md` ("P-695 missing-`return` census").  Use a real
compile, not `-fsyntax-only` (it finds 8 of 45).

## Audio (S5)

```sh
./build/native/test_audio                       # disc-free mixer + .ssm test
ctest --test-dir build/native -R decomp_audio   # two matches, byte-identical PCM
./build/native/melee_decomp_boot --boot-frames 600 --boot-timeout 90 \
    --boot-match 20 --audio-dump /tmp/melee.wav  # logs frames=N hash=...
```

A quality probe (no listening): the WAV should have spectral flatness < 0.05
in loud windows, positive stereo correlation and no clipping.  The viewer's
`--match` plays through SDL3 (`viewer: audio 32000 Hz stereo` when a device
opens).  **Automated/offscreen viewer runs must set `SDL_AUDIODRIVER=dummy`**
so captures do not play through the user's speakers.  Only omit it for an
explicit listening test.  The owner listening check is TASKS.md P-637.

## Sanitizer run (required for parser/memory changes)

```sh
cmake -S native -B build/native-asan -DCMAKE_BUILD_TYPE=Debug -DMELEE_SANITIZE=ON
cmake --build build/native-asan -j4
SDL_VIDEODRIVER=offscreen ASAN_OPTIONS=detect_leaks=0 \
    ./build/native-asan/melee_prototype --scripted --frames 600
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
