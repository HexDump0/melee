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

## Boss-intro camera (P-847)

```sh
MELEE_BOSSCAM=800 MELEE_NO_CARD=1 MELEE_MATCH_P0=0 MELEE_MATCH_P1=1 \
    MELEE_MATCH_STAGE=32 SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy \
    ./build/native/melee --match 600 --frames 1100 --no-items
```

Runs Master Hand's entry camera sequence (`ftmasterhandentry.c:70`) on an
ordinary VS match at frame 800.  The Classic Master Hand fight is the eleventh
round and no harness can win ten matches to reach it, so this is how that code
is exercised at all.  Expect one `[bosscam]` line (with
`MELEE_VIEWER_TRIAGE=1`) and a clean exit.  A panic at `lbvector.c:397`
`pos3d->x>-50000.0F` is P-847 back: `Camera_8002E234` is feeding an
uninitialised pitch into the camera position (G-205).

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

## Non-ASCII literals (Shift-JIS)

`melee_decomp_game` must compile with `-fexec-charset=CP932` so the game's
full-width literals reach the binary as Shift-JIS, the way sjiswrap arranges
for the GameCube build.  `ctest decomp_classic_names` asserts it; a bare
check is:

```sh
SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy MELEE_CLASSIC_TEST=1 \
    MELEE_NO_CARD=1 MELEE_VIEWER_TRIAGE=1 ./build/native/melee --match \
    --frames 700 --no-hud --shot /tmp/cl.bmp 2>&1 | grep name_lead
```

Expect `name_lead=82 sjis=1`.  `sjis=0` means the flag was lost and every
non-ASCII string will render as stray kana or vanish (G-150).

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

**Every `[intro]`/`[classic]`/`[gameover]` probe line needs
`MELEE_VIEWER_TRIAGE=1`.**  `viewer_main.c` points `boot_triage_note()` at
`/dev/null` without it, so a run that reached the scene looks identical to one
that never got there (G-151).  The `ctest` cases set it for you; ad-hoc runs
must set it themselves.

Two harness switches (committed with the P-704 fix):

- `MELEE_INTRO_US=1` — with `MELEE_INTRO_TEST`, forces
  `saved_language = LANG_US`.  Without it the debug route leaves the save as
  JP and the splash shows the Japanese name table, which looks like a bug and
  is not.
- `MELEE_CLASSIC_INTRO=1` — with `MELEE_CLASSIC_TEST`, steps `GM_CLASSIC` onto
  its own state id 0, i.e. the real `GS_INTRO_EASY` fed by
  `gmClassicIntroDataBuffer`.  Use this, not `MELEE_INTRO_TEST`, whenever the
  question is about the splash's *enter data* rather than its rendering.

## VS splash fighter names (P-704)

```sh
SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy MELEE_INTRO_TEST=1 \
    MELEE_INTRO_US=1 MELEE_NO_CARD=1 ./build/native/melee --match \
    --frames 420 --no-hud --shot /tmp/vs.bmp
```

Expect `[intro] names white=` in the thousands; `0` means `fn_80160DE8` is
reading the US name-width table past the end of `lbl_803B75F8` again and
storing an x-scale of 0 (G-154).  The probe counts **white**, not non-black:
the dark backdrop and the white "VS" logo keep a non-black count high in both
the broken and fixed builds, so only the white count flips (G-155).  The
`ctest` case is `decomp_intro_names`.

## Missing-`return` census

`src/` is compiled with `-w`, so `-Wreturn-type` never fires in a normal
build.  Re-run the census after every `decomp/` re-pin; the recipe, the
retail-DOL method for deciding each site, and the current verdict table are in
`learnings/decomp_port.md` ("P-695 missing-`return` census").  Use a real
compile, not `-fsyntax-only` (it finds 8 of 45).

## The opening movie, and why every harness skips it (P-844)

The product boots the way the console does: a cold boot returns reset code 0,
`skip_intro` stays false and `bootOnLoad` enters `GM_OPENING_MV`, so
`./build/native/melee` plays `MvOpen.mth` before the title.  (With no save on
the card the game's own `lbCardGame_DecideGameMode` override sends the boot to
`GM_MEMCARD` first, exactly as it does on hardware — the movie is what you get
once a save exists.)

**`MELEE_NO_OPENING=1` takes the reset-to-menu boot instead** (reset code
`0x80000000` -> `GM_TITLE`), and every harness wants it: the frontend input
scripts, the title probe's frame-400 window and the match tests' frame-600
position all count frames from the boot, and the movie shifts all of them.
`CMakeLists.txt` pins it for the whole suite next to `MELEE_RNG_SEED`, and the
shell harnesses set it themselves so they also work when run by hand.  Empty
and `0` count as unset, which is how `frontend_opening.sh` clears the pinned
value for the one run that is about the movie:

```sh
# the shipped boot, headless
MELEE_NO_OPENING=0 MELEE_CARD_DIR=<dir with a save> SDL_VIDEODRIVER=offscreen \
    SDL_AUDIODRIVER=dummy ./build/native/melee --frontend --frames 300 \
    --shot /tmp/opening.bmp
```

Expect `mode=24 scene=0` in the log and a frame that is not black; `mode=0`
straight after `mode=40` means the boot skipped the movie.

## The Unbound opening movie (P-847)

`mods/unbound/files/MvUnbound.mth` is a real MTH file, played by the game's own
`lbmthp.c` before `MvOpen.mth`.  Regenerate it after any change to the logo
animation:

```sh
python3 scripts/make_boot_mth.py        # assets/*.mp4 -> mods/unbound/files/*.mth
```

The script asserts what `thp_dec.c` requires before writing anything: 4:2:0
baseline, all four Huffman tables, a chain that walks to exactly EOF.  **The
one rule that is not JPEG's:** THP scan data is *not* byte-stuffed, and the
port's bit reader unescapes nothing, so the packer strips the `FF 00` pairs
back out.  Leave them in and the flat-black first frame decodes while every
later frame returns `-1` -- which `lbmthp.c` hands to `THPDec_80331340` as a
state pointer, so it reads as a SIGSEGV four frames into the boot rather than
as a bad picture.

`ctest decomp_unbound_opening` covers the clip, the hand-off to MvOpen.mth, a
button skipping into that movie (mode stays 24; GM_TITLE would mean the press
also reached the scene's own Start/A branch), the deferred music, and
`MELEE_NO_MODS=1` booting retail.  `decomp_opening` pins `MELEE_NO_MODS=1` for
the same reason: it is the *retail* movie's regression.

To watch it by hand:

```sh
MELEE_VIEWER_TRIAGE=1 MELEE_CARD_DIR=<dir with a save> ./build/native/melee
```

Expect `[unbound] opening: playing MvUnbound.mth before MvOpen.mth`, then
`holding track 0x3e`, then `clip finished at frame 239` (or `skipped at frame
N`) followed by `starting track 0x3e`.  `MELEE_UNBOUND_MOVIE=<path>` points the
clip somewhere else; `MELEE_NO_MODS=1` removes it.

## Title-demo CPU attacks

`ctest decomp_opening` now covers the idle title demo as well as the opening
movie. Its idle run sets `MELEE_CPU_TEST=1` and requires both:

```text
[cpu] tables checked=4 valid=4 ... ok=1
[cpu] hit slot=N damage=N attack_entries=N motion=N
```

The first line catches byte-order regressions in `PlCo.dat` pData[22]
(`Fighter_804D64FC`); the second proves the four level-9 CPUs selected and
executed an attack without scripted PAD input. The run disables the asset
cache so a stale converter-v86 file cannot hide a flipped test.

## Soaking (P-759)

`ctest decomp_soak` runs a small fixed-seed set on every build. The sweeps
below are the discovery tool and are meant to be run by hand.

```sh
# seeds only -- varies the stage, keeps Link vs Mario (10 s)
MELEE_SOAK_SEEDS=40 MELEE_SOAK_SEED_BASE=random \
    native/tests/soak.sh ./build/native/melee_decomp_boot /tmp/soak

# the matrix -- 26 fighters x 30 stages, 780 runs (10 min on 8 cores)
MELEE_SOAK_SEEDS=1 MELEE_SOAK_FIGHTERS=all MELEE_SOAK_STAGES=all \
    native/tests/soak.sh ./build/native/melee_decomp_boot /tmp/soak-matrix
```

**Seeds are not coverage.** `onEnterDebugVs` hardcodes Link vs Mario, so a seed
varies the stage and never the fighters: 200 clean seeds coexisted with P-725
(Ness) and P-755 (Kirby) open, and the first matrix run failed 240 of 780 runs
in ten distinct bugs. Sweep the matrix before believing a green soak.

### Overnight

Seeds multiply the matrix, so N seeds is N x 780 runs at about 0.77 s each on
eight cores. **50 seeds is roughly 8.5 hours**, which is one night:

```sh
nohup env MELEE_SOAK_SEEDS=50 MELEE_SOAK_SEED_BASE=random \
    MELEE_SOAK_FIGHTERS=all MELEE_SOAK_STAGES=all \
    native/tests/soak.sh ./build/native/melee_decomp_boot /tmp/soak-night \
    > soak-night.log 2>&1 &
```

No GPU, no display, no second machine. Lower `MELEE_SOAK_JOBS` (default
`nproc`) if you want the machine back while it runs. The harness prints the
seed base it drew, so any failure replays exactly; failing runs keep their log
under the work directory and passing runs delete theirs, so it stays small
however long it runs.

If you do move it to another box, **the disc image can never go on a public CI
runner** (AGENTS.md rule 0) — a machine you own is the only correct home. Such
a box needs the 32-bit toolchain from the top of this file (`lib32-gcc-libs`),
the disc image, and nothing else; the renderer packages are not used.

## Tracing an unwalked descriptor (P-762)

Two probes in the converter, both off unless set:

```sh
MELEE_FIND_PTR=0x79634 ...    # every pointer field aimed at that data offset
MELEE_ROOT_TRACE=1 ...        # "root 0x<offset> <name>" for every public symbol
```

Given a structure the game read as garbage, `MELEE_FIND_PTR` walks the
reference chain back towards whatever should have reached it, and
`MELEE_ROOT_TRACE` names the nearest root at or below that offset.  When a
chain ends with two referrers and nothing pointing at them, the structures are
array elements and the array base is the root worth walking.

## Descriptor-walk coverage (P-756)

The primary metric for the conversion bug class (ADR-0023).  `decomp_assets`
measures it on every run:

```sh
MELEE_COVERAGE_JSON=/tmp/cov.json MELEE_NO_ASSET_CACHE=1 \
  ./build/native/test_decomp_assets "iso/<image>.ciso"
```

```
coverage archives=861 targets=774413 walked=457693 (59.10%) roots=7030 unhandled=4806
coverage descriptors=209261 walked=154019 (73.60%) struct-roots=1964 unhandled=833
```

**Use the second line.**  Raw targets include image data, display lists and
vertex buffers, which are pointed at and correctly never walked.  A
*descriptor* is a target whose own first word is a pointer, so it points at
something else -- leaving one big-endian corrupts a graph, not a texture.

`MELEE_COVERAGE_FLOOR` in `test_decomp_assets.c` fails the test on regression.
It is a ratchet: raise it when coverage climbs, never lower it to make a change
pass.  `MELEE_COVERAGE_JSON` writes per-file JSON for the dashboard.

## Pinning the RNG

The game seeds `HSD_Rand` from the host clock (P-751), so a 1P run draws a
different opponent and stage every boot.  Every boot prints the seed it drew:

```
[rng] seed=0x2ab61edc (MELEE_RNG_SEED=0x2ab61edc replays this run, tick=0x001537a4)
```

Feed that value back to replay the run exactly -- the same matchup, the same
item drops, the same CPU decisions:

```sh
MELEE_RNG_SEED=0x2ab61edc ./build/native/melee --frontend
```

`MELEE_RNG_SEED=tick` pins the bare virtual-timebase value, which is the seed
the port used before P-751.  Every ctest runs with it (one
`ENVIRONMENT_MODIFICATION` over the whole suite in `native/CMakeLists.txt`), so
a new harness is deterministic without doing anything; do not hand a test its
own seed unless it is deliberately probing a different stream.

Other streams reach code the pinned one never does -- `0x13371337` segfaults
the sound engine (P-752) -- so an unexplained crash is worth re-running under
its printed seed before anything else.

## Audio (S5)

### Intermittent SFX spam capture

Run the normal interactive frontend with the opt-in recorder:

```sh
MELEE_SFX_DEBUG=melee-sfx-debug.log ./build/native/melee --frontend
```

Reproduce the loud repeating sound, then exit with Escape so the final
summary is written. Send `melee-sfx-debug.log`; do not redirect the ordinary
viewer output into it. The recorder is line-buffered and also writes one
`[sfx-window]` checkpoint per second, so most evidence survives even if the
process must be killed.

`[sfx-alert]` blocks distinguish four signals: four requests for one ID in
two seconds, three wraps of one mapped SFX voice in three seconds, sustained
PCM clipping, and voice saturation. Each alert includes the rolling request
history, original and internal sound IDs, AX address/loop/rate/gain state,
the current scene and fighters, and a symbolized call stack for request
bursts. This diagnostic never suppresses, stops, or changes a sound. The
value `MELEE_SFX_DEBUG=1` uses the same default filename; `-` writes to
stderr.

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
