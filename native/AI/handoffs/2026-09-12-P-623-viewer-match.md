# Handoff: P-623 — live match in the compiled viewer + PPM recording

**Date:** 2026-09-12
**Agent:** opencode (deepseek-v4.1-flash)
**Commit:** this commit
**Tree state:** builds clean (`melee_decomp_viewer`, `-Wall`); ctest 12/12;
ASan 600-frame match run clean and position-identical to release

## What I did

- Added `boot_platform_set_present_hook` (`native/platform/gx_vi.c` +
  `platform.h`), called from `VIWaitForRetrace` after the frame hook, and
  `hsd_asset_set_register_hook` (`native/decomp/assets/hsd_convert.[ch]`) so
  the viewer can register parsed textures with the GX HLE.
- `melee_decomp_viewer --match [FRAME]` runs the compiled `main()` in the
  viewer process: `run_match` sets the present hook, `match_boot_init(FRAME)`
  (default 20), loops the S4 scripted input, and `gm_main()` runs the real
  match.  `match_present` renders the previous frame, records if asked, swaps,
  starts the next HLE frame, and paces interactive sessions at 60 Hz.
- Added `--record FILE|-` + `--record-every N`: `gx_gl_write_ppm` appends P6
  frames; `-` redirects logs to /dev/null and hands the real stdout fd to the
  recorder, so `| ffmpeg -f image2pipe -framerate 60 -i -` works directly.
- Added `pad_set_input_loop` so the finite S4 script repeats instead of
  freezing on its last frame (boot/ctest paths keep the clamp default).
- Verified on the real Wayland display (`DISPLAY=:1`): window opens, 4 s gives
  210 presented frames (~60 Hz).  Rendered `/tmp/melee_match.mp4` (2400
  frames, 40 s, 960x600 H.264) showing Ready, walking/jumping, a KO with
  `SCORE -1`, camera pan/zoom and respawns.
- Fixed the viewer triage frame budget (G-087): the default 60-frame budget
  killed `--match` early; `run_match` now sets `limit + 240`.
- Follow-up fix (same session): the GO! logo's black quad + CI decode spam
  was the GX HLE asset table capping at 8 archives (G-088); it now grows with
  `realloc`, so every parsed archive bounds its textures.  `--match` at
  frame 215 now shows the real fire-textured GO! logo, no decode errors.

## Exact next action

Watch `/tmp/melee_match.mp4` (or `--match` live) and pick the next visual
gap.  The GO! artifact from this handoff is fixed; the remaining known gaps
are P-616 (Falcon eyes), P-617 (indirect/toon) and P-621 (stage colors).
Audio is S5.

## What I tried that did not work

- First `--frames 150` run looked like a hang: it was `boot_triage_stop`
  calling `exit(0)` at the default 60-frame budget (G-087).  A gdb breakpoint
  on `exit` found it; `OSResetSystem`/`gm_main` return was a red herring.
- Piping PPM to ffmpeg initially failed ("Output file does not contain any
  stream") because the viewer's GL-init `printf`s landed on stdout before the
  redirect; the dup/freopen now happens before SDL init.

## Open questions

- Is the black GO! quad the announcer logo texture failing to decode (CI8
  palette?), or an address collision in the texture cache?  Needs a texture
  dump of that draw — nobody has looked yet. — no human needed
- Full-match length: `onEnterDebugVs` uses `MatchKind_Time` with
  `time_limit = 0` (infinite), so the match never ends by itself; the demo
  loops the 15 s input script.  If a real "GAME SET → results" run is wanted,
  set a time limit from the viewer (or use stock rules). — human preference
- Audio is silent (S5) — known, not a regression.

## Files touched / claimed

- `native/platform/gx_vi.c`, `native/platform/platform.h`
- `native/platform/pad_card.c`
- `native/decomp/assets/hsd_convert.c`, `hsd_convert.h`
- `native/decomp/gx/gx_gl.c`, `gx_gl.h`
- `native/decomp/render/viewer_main.c`
- `native/CMakeLists.txt` (viewer target + `match_boot.c`)

## Verification run

```
cmake --build build/native -j4                    # clean
ctest --test-dir build/native --output-on-failure # 12/12 passed
SDL_VIDEODRIVER=offscreen ./build/native/melee_decomp_viewer --match \
    --frames 150 --shot /tmp/match150.bmp         # Ready + HUD at frame 150
timeout 4 ./build/native/melee_decomp_viewer --match   # frame 210, real window
SDL_VIDEODRIVER=offscreen ./build/native/melee_decomp_viewer --match \
    --frames 2400 --record - 2>/dev/null | ffmpeg -y -f image2pipe \
    -framerate 60 -i - -c:v libx264 -crf 21 -pix_fmt yuv420p \
    /tmp/melee_match.mp4                          # 40 s, 23 MB
SDL_VIDEODRIVER=offscreen ./build/native/melee_decomp_viewer --match \
    --frames 215 --shot /tmp/go215.bmp            # GO! logo, 0 decode errors
ASAN_OPTIONS=detect_leaks=0:allow_user_segv_handler=0 \
    ./build/native-boot-asan/melee_decomp_boot --boot-frames 600 \
    --boot-timeout 300 --boot-match 20            # clean, positions match
```
