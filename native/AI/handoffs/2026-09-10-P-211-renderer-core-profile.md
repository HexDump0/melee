# Handoff: P-211 — OpenGL 3.3 core renderer landed

**Date:** 2026-09-10
**Agent:** opencode (deepseek-flash)
**Commit:** `4eb7c1f2d` (ADR-0009/claim), `b35dd102e` (rewrite + docs)
**Tree state:** builds warning-free; 240-frame scripted and 600-frame
ASan+UBSan runs clean; no fixed-function or display-list calls remain.

## What I did

- **Spike:** `SDL_VIDEODRIVER=offscreen` + Mesa creates a 3.3 core context.
  `GL_VERSION: 4.6 (Core Profile) Mesa 26.1.6-arch1.1`,
  `GL_RENDERER: AMD Radeon RX 580 2048SP (radeonsi, polaris10, ACO, DRM
  3.64)`, `GLSL 4.60`; a shader triangle rendered (`/tmp/opencode/p211/spike.c`).
- **ADR-0009** (target/ES3/headless, supersedes ADR-0004).
- **`main.c`:** GL 3.3 core request; `renderer_init()` compiles one model
  program and one flat overlay program; per-batch VAO+VBO pairs (immutable
  bind pose + dynamic pose); `draw_batch` replaces `list_vertices`/display
  lists; CPU matrices (`m4_*`) replace the GL matrix stack; `ov_begin/
  ov_vertex/ov_end` replaces every `glBegin/glVertex`; `glGenerateMipmap`
  replaces `GL_GENERATE_MIPMAP`; `demo_text.h` now calls `demo_text_rect`.
- **`STATE.md`**, `learnings/gl_shaders.md`, GOTCHAS G-040/G-041,
  `learnings/README.md`.

## Exact next action

P-204 in `TASKS.md` is now **open**. The shader hooks already exist:
`u_material`, `u_alpha_test`, `u_use_texture`, two texture-less states are set
per batch. Start from the decomp, not screenshots:

1. Read `MObjDesc.rendermode` bits already parsed into `DemoModelBatch`
   (`RENDER_XLU` 1<<30, `RENDER_ZMODE_ALWAYS` 1<<27, `RENDER_NO_ZUPDATE`
   1<<29) and `DemoModelBatch.translucent`.
2. Alpha test from `TObjDesc`/`TEV` (`src/sysdolphin/baselib/tev.c`) — wire
   the reference value through `u_alpha_test` and `discard`.
3. `RENDER_XLU`: set blend factors and draw translucent batches after opaque
   ones; keep the parity harness from `learnings/gl_shaders.md` to catch
   regressions.
4. Then multi-texture (`TEX1`) and common TEV ops.

## Evidence

Pre-rewrite BMPs are in `/tmp/opencode/p211/before/`, post-rewrite in
`/tmp/opencode/p211/after/` (not committed). `magick compare -metric RMSE`:

| Screenshot | RMSE (0..65535) |
|---|---|
| `--view` bind, 3 frames, default angle/elevation | 0.147 (2.2e-6) |
| `--view --animate --clip Wait1 --anim-frame 25` | 0.207 (3.2e-6) |
| `--view --angle 180 --elevation -5` | 0 |
| `--scripted --frames 240` | 110 (1.7e-3) |

The scripted residue is 88/1,024,000 pixels behind the 95 %-alpha HUD panel;
simulation ticks, damage and HUD text are identical. Same-build screenshots
are byte-identical (`--view --frames 3` twice).

## What I tried that did not work

- Hand-written `m4_frustum` without `m[15]=0`: 1 px projection shift, parity
  RMSE 0.049. Caught by comparing against `glGetFloatv` in a compat context
  (`/tmp/opencode/p211/mtxcmp.c`). See G-040.
- Reproducing the `GL_LIGHT0` ambient+diffuse term in the fragment shader
  without the per-vertex clamp: textured surfaces blew out (cap at 239/255 vs
  180/255). See G-041.
- Selecting two-sided lighting per vertex by eye-space normal `z`: no better
  than `gl_FrontFacing`; keep the fragment-side selection.

## Open questions / human checks

- H-4 (new): on the owner's 180 Hz display, confirm the viewer animates at the
  same speed as the offscreen 60 Hz-accumulator build (G-038 untouched) and
  that nothing regressed visually. Run `./build/native/melee-demo --view
  --animate --clip Wait1`.

## Verification run

```
cmake --build build/native --clean-first -j4        # warning-free
./build/native/melee-demo --inspect                  # 6328 tris, bounds unchanged
./build/native/melee-demo --model PlMrNr.dat --list-clips | head
SDL_VIDEODRIVER=offscreen ./build/native/melee-demo --view --animate \
    --clip Wait1 --anim-frame 25 --frames 1 --screenshot /tmp/anim.bmp
SDL_VIDEODRIVER=offscreen ./build/native/melee-demo --scripted --frames 240
# Completed 240 render frames, 240 simulation ticks
SDL_VIDEODRIVER=offscreen ASAN_OPTIONS=detect_leaks=0 \
    ./build/native-asan/melee-demo --scripted --frames 600
# Completed 600 render frames, 600 simulation ticks
```
