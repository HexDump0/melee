# Interactive compiled-path viewer (P-611, SDL3)

Written 2026-09-12 with the viewer landing. This is the dev tool for finding
renderer bugs by eye: it presents the same compiled HSD + GX HLE frame as
`test_decomp_render` in a window, with orbit/zoom/model cycling and display
toggles.

## Files

| File | Role |
|---|---|
| `native/decomp/render/render_scene.{c,h}` | shared scene: bootstrap, asset bridge, `ftData` scale/visibility, prototype camera framing, compiled `HSD_LObj` lights, capture |
| `native/decomp/render/viewer_main.c` | SDL3 window/input/event loop, calls the shared scene + `gx_gl` |
| `native/decomp/gx/gx_gl.{c,h}` | `gx_gl_attach()` (external SDL context) + `gx_gl_set_options()` (textures/lights/only-draw) + texture-cache clear |
| `native/tests/test_decomp_render.c` | headless ctest, same shared scene |

## Dependency (ADR-0014)

- 32-bit target uses **SDL3**, not SDL2: Arch's multilib only ships
  `lib32-sdl3` (SDL3 3.4.x); headers come from the 64-bit `sdl3` package.
  Install list in `TESTING.md`.
- CMake looks for `/usr/lib32/libSDL3.so` and skips the viewer target when it
  is absent, so the rest of the port builds without multilib SDL3.

## Gotchas found

- **`bool` ABI clash.** The decomp shim's `<stdbool.h>` defines `bool` as
  `int` (ADR-0011), but SDL3's `SDL_stdinc.h` asserts `sizeof(bool) == 1`.
  `native/decomp/shim/stdbool.h` now honors `MELEE_SHIM_REAL_STDBOOL`, and
  `viewer_main.c` is the only TU built with it. It never calls a decomp
  function that takes/returns `bool`, so no ABI boundary is crossed; if that
  changes, move the SDL event loop into its own TU that doesn't include game
  headers.
- **Window alpha.** The window surface's alpha channel is never written by GX
  (`GXSetAlphaUpdate(0)`), so a Wayland compositor can show the desktop
  through the model. `gx_gl_render_frame` now forces a full color mask before
  `glClear` (the pbuffer path never noticed).
- **Context ownership.** Headless uses the EGL pbuffer (`gx_gl_init`); the
  viewer uses SDL's EGL/GLES3 context and `gx_gl_attach` (program + VBO only).
  `gx_gl_shutdown` is a no-op for the attached path.
- Model switching must call `gx_hle_reset_assets()` + `gx_gl_clear_textures()`
  (done in `render_scene_close`/`scene_load_model`) or a recycled asset
  address can hit a stale GL texture cache entry.

## Controls

drag = orbit, wheel = zoom, `N`/`P` = next/prev `Pl*Nr.dat`, `[`/`]` = part,
`V` = part mode (ALL/ONLY/HIDE), `shift+V` = variant, `B` = visibility slot,
`Y` = show game-hidden DObjs, `L` = lights, `T` = textures, `W` = wireframe,
`C` = cull (default on; wireframe forces it off), `H` = HUD, `F12` =
screenshot (or `--shot FILE`), `R` = reset view, `ESC` = quit.
`--frames N --hidden --shot F` runs without a visible window for smoke tests;
`--wire`, `--unlit`, `--no-cull`, `--no-alpha-test`, `--no-hud`, `--part N`,
`--part-mode all|only|hide` mirror the keys for scripted shots.

The HUD is `decomp/render/hud.c`: batched pixel quads over the port's own 5x7
alphabet (`extras/font.h`, uppercase/`0-9`/`%:-/.+` only) and shows
TEX/LIGHT/WIRE/CULL state.  Wireframe draws each captured triangle as a
`GL_LINE_LOOP` (GLES has no `glPolygonMode`) with culling disabled so the
back/interior edges stay visible (the prototype viewer defaults CULL OFF);
part isolation is the existing `GxGlOptions.only_draw` / `hide_draw` draw
filter.

## Known limits

- Bind pose only: animation arrives with the compiled `fobj`/AJ path (S3/S4).
- The viewer inherits the S2/S3 renderer deviations: indirect/bump/toon,
  Z-texture/EFB effects: see `learnings/decomp_s2_gx_hle.md`.
