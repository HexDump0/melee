# Handoff: P-618 — GX channel slots + lit raster alpha

**Date:** 2026-09-12
**Agent:** opencode (deepseek-flash)
**Commit:** `f8a8b48d2`
**Tree state:** builds warning-free; `ctest --test-dir build/native` 8/8;
32-bit ASan/UBSan `test_decomp_render --direct` and full `PlMhNr` render clean.

## What I did

- **G-071 model-switch lighting.** The viewer's first model was lit, but
  `N`/`P` switches made the new model render flat (identical to `L` lights
  off).  Cause: `GXSetChanCtrl`/`GXSetChanAmbColor`/`GXSetChanMatColor`
  collapsed `GX_ALPHA0/1` onto the colour-0/1 slots; HSD emits COLOR0 (diffuse
  mask) then ALPHA0 (`enable=0, mask=0`) per material, so the alpha write
  wiped the colour light mask.  First load masked it because
  `HSD_LObjSetupInit` raises the mask from 0 to 1 after
  `HSD_StateInvalidate`; after a switch HSD's `prev_ch` cache skips the
  COLOR0 emit and the reset slot survives.  State now keeps four channel
  slots (COLOR0, COLOR1, ALPHA0, ALPHA1); A0/A1 mirror their alpha.
- **G-072 lit raster alpha.** Master Hand's/Crazy Hand's translucent wrist
  connector vanished with lighting on (`out[3] = 0` on the lit path) while
  lights-off showed it as a flat haze (the flat override forces `ras = 1`).
  `channel_raster` now evaluates raster alpha from the paired alpha channel
  (sources, light mask, diffuse function) for the diffuse, unlit and
  specular outputs.  Shared light direction/attenuation moved into
  `light_view_dir`/`light_diffuse_term`; the colour path stays bit-identical.
- Regression: `test_decomp_render --direct` checks the COLOR0 mask survives a
  following ALPHA0 write and that a lit vertex's `ras[3]` is 1.0.
- Repro recipe that worked: a temporary viewer `--switch-after N` flag
  (renders N frames before cycling) — `--cycle` alone cannot reproduce,
  because the first model must have rendered before the switch.

## Exact next action

P-616 is still open (Captain Falcon's eyes, `PlCaNr.dat` batch 96/dobj 77,
TEX1 overlay renders black).  Start with the debug path in the P-615/P-612
handoff; the channel/alpha fixes here did not change Falcon (its screenshots
are byte-identical except the hand models, so it remains a distinct TEV
texture bug).

## What I tried that did not work

- `--cycle N` for the switch bug: matches a direct load because the engine
  has not rendered a frame yet; the poison only survives when HSD's caches
  are warm (see the repro recipe above).
- Suspecting `HSD_LObj` dirty flags / texture cache: instrumenting
  `GXSetChanCtrl` and the captured `ch_light_mask` showed the mask go 0 only
  after the ALPHA0 write, which pointed at the channel-slot aliasing.

## Open questions

- Does any HSD material use `GX_CA_RASA` on a `GX_COLOR1A1` stage?  The
  specular template never sets alpha in/out, so changing channel 1's raster
  alpha from `spec` to the ALPHA1 value did not move any tested screenshot;
  a stage-only capture would settle it — needs no human.

## Files touched / claimed

`native/decomp/gx/gx_hle.{c,h}`, `native/tests/test_decomp_render.c`,
`native/AI/*` (STATE, GOTCHAS, learnings, TASKS, HANDOFFS, this note).

## Verification run

```sh
cmake --build build/native -j4                       # warning-free
ctest --test-dir build/native --output-on-failure    # 8/8
./build/native/test_decomp_render --direct           # channel + ras[3] PASS
SDL_VIDEODRIVER=offscreen ./build/native/melee_decomp_viewer --frames 1 \
    --hidden --model PlMhNr.dat --shot /tmp/mh.bmp   # translucent wrist lit
# switched-after-frames vs direct load: pixel-identical for
# Mario->Mewtwo, Falcon, Kirby, Giga Koopa, Luigi pairs (0 changed pixels)
# Mario/Kirby/Giga Koopa/Link/Luigi screenshots byte-identical after the
# alpha fix; only PlMhNr/PlChNr change
cmake --build build/native-boot-asan --target test_decomp_render -j4
ASAN_OPTIONS=detect_leaks=0 ./build/native-boot-asan/test_decomp_render --direct
ASAN_OPTIONS=detect_leaks=0 ./build/native-boot-asan/test_decomp_render \
    --model PlMhNr.dat --shot /tmp/asan_mh.bmp      # PASS
```
