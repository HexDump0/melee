# Handoff: interactive compiled-path viewer (P-611)

**Date:** 2026-09-12
**Agent:** opencode (deepseek-flash)
**Task:** P-611
**Tree state:** ctest 5/5; prototype untouched; headless `test_decomp_render`
screenshot unchanged (viewer vs test RMSE 0.002).

## Result

`melee_decomp_viewer` presents the compiled HSD + GX HLE frame in an SDL3
window: drag orbit, wheel zoom, `N`/`P` model cycle over the disc's
`Pl*Nr.dat`, `B` visibility slot, `V` variant, `Y` show game-hidden DObjs,
`L` lights, `T` textures, `F12` screenshot, `R` reset, `ESC` quit.  State
prints to stdout.  It shares `decomp/render/render_scene.c` with the headless
test, so both paths render identically.

Commands:

```sh
cmake -S native -B build/native -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build/native -j4
./build/native/melee_decomp_viewer            # interactive
./build/native/melee_decomp_viewer --frames 1 --hidden --shot /tmp/v.bmp
```

## Environment / dependency

Needs 32-bit SDL3 (`sudo pacman -S sdl3 lib32-sdl3`); see ADR-0014 and the
package table in `TESTING.md`.  Without `/usr/lib32/libSDL3.so` CMake skips
the target and everything else still builds.

## What is proven

- SDL3 Wayland window + GLES3 context; `gx_gl_attach` renders into it and the
  `--shot` BMP matches the EGL-pbuffer test render (RMSE 0.0016).
- Model cycling reloads through the compiled bridge (Luigi 49 draws, etc.).
- Texture/light toggles work via `gx_gl_set_options` (shader `u_tex_enable` /
  `u_ras_flat`).

## Open questions for the owner (visual)

1. Window opens and orbits/zooms smoothly on your compositor?
2. Are the texture/light toggles and model cycle intuitive? Missing a key you
   want (e.g. wireframe, part isolation like the prototype's `[`/`]`/`V`)?
3. Any flicker/alpha/compositor artifacts — the alpha-clear fix is new.

## Known limits / next tasks

- No HUD text; wireframe and part isolation keys not implemented
  (`learnings/decomp_viewer.md` lists why).
- Bugs found by eye should be filed or fixed against P-607 (specular/boots),
  P-610 (Falcon/POBJ_SKIN) or the GX HLE; the viewer is the tool, not the
  milestone.
