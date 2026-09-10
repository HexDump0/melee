# Learning: OpenGL 3.3 core renderer and ES3-portable shaders (P-211)

**Date:** 2026-09-10
**Agent:** opencode (deepseek-flash)
**Evidence:** `main.c` draw path; parity harness in
`/tmp/opencode/p211/` (`before/` vs `after/` BMPs, `magick compare -metric
RMSE`); P-211 spike output `4.6 (Core Profile) Mesa 26.1.6-arch1.1`, renderer
`AMD Radeon RX 580 2048SP (radeonsi, polaris10, ACO)`.

## Fact

ADR-0009 replaces the fixed-function renderer with OpenGL 3.3 core + GLSL.
The pieces that matter:

### Context and entry points

- Request `SDL_GL_CONTEXT_MAJOR/MINOR_VERSION = 3/3` and
  `SDL_GL_CONTEXT_PROFILE_MASK = SDL_GL_CONTEXT_PROFILE_CORE`. SDL
  `offscreen` + Mesa gives a 4.6 core context on the reference machine; the
  context creation still fails loudly if unavailable.
- `#define GL_GLEXT_PROTOTYPES` before `#include <SDL_opengl.h>` gives the
  GL 2+ prototypes from SDL's bundled `SDL_opengl_glext.h` / system
  `GL/glext.h`. No GLEW/GLAD dependency.

### Shader sources

- The `#version` line and ES precision declarations are a compile-time header
  (`DEMO_GLSL_HEADER`, selected by `DEMO_GL_ES`); the bodies are one shared
  string pair per program and use only ES3 syntax: `layout(location=N)`,
  `in`/`out`, `texture()`, `gl_FrontFacing`, `discard`, no `gl_FragColor`.
- Model program replicates the old fixed-function look per **vertex**:
  `lit = clamp(vertex_color * (ambient + diffuse * max(N.L, 0)), 0, 1)`.
  The clamp is required (G-041). Two-sided lighting is handled by computing a
  front and a back colour per vertex and selecting with `gl_FrontFacing` in the
  fragment shader (equivalent to Mesa's per-primitive selection for planar
  triangles).
- The HSD texture matrix is `uniform mat4 u_texmtx`, applied in the vertex
  shader. No `glMatrixMode(GL_TEXTURE)`.

### Geometry

- Per batch (`DemoModelBatch` vertex range) two VAO+VBO pairs: an immutable
  bind-pose buffer (uploaded in `compile_model`) and a pose buffer
  (`GL_DYNAMIC_DRAW`) that `upload_batch` refreshes each animated frame from
  the CPU-skinned `DemoModel::vertices`. Static draws use the bind VAO, so an
  animation pass can never corrupt the bind pose.
- Attributes are interleaved `DemoModelVertex` (pos f32x3, normal f32x3,
  uv f32x2, colour u8x4 normalized); stride is `sizeof(DemoModelVertex)`.
- `glGenerateMipmap` replaces `GL_GENERATE_MIPMAP` (not in core).
- HUD/grid/platform are a second "overlay" program fed by a CPU vertex stream
  (`ov_begin`/`ov_vertex3f`/`ov_end`); `GL_QUADS`/`GL_TRIANGLE_FAN` are
  converted to `GL_TRIANGLES` on the CPU using Mesa's `(0,1,2),(0,2,3)`
  quad decomposition. `GL_LINES`/`GL_LINE_LOOP`/`GL_LINE_STRIP` pass through.
- `demo_text.h` now calls `demo_text_rect`; `main.c` emits the quad.

### Exactness traps (all found by the parity harness)

| Trap | Result |
|---|---|
| `glFrustum` has `m[15] == 0`, not 1 | 1 px projection shift, RMSE 0.049 |
| Lit colour must be clamped before texture modulate | bright red textures, RMSE 0.049 |
| `glFrontFace(GL_CW)` + `gl_FrontFacing` is required for two-sided | (already right) |

After fixing both, the parity numbers are:

| Screenshot | RMSE (0..65535) |
|---|---|
| `--view` bind (3 frames, default angle) | 0.147 (2.2e-6) |
| `--view --animate --clip Wait1 --anim-frame 25` | 0.207 (3.2e-6) |
| `--view --angle 180 --elevation -5` | 0 (0) |
| `--scripted --frames 240` | 110 (1.7e-3) |

The scripted residue is 88 differing pixels out of 1,024,000, all behind the
95 %-alpha fighter HUD panel; the simulation and HUD text are identical.

## How to verify

```sh
cmake -S native -B build/native -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build/native -j4
./build/native/melee-demo --inspect
# 6328 triangles, bounds [-7.56 -0.28 -2.70] to [7.57 14.21 3.58]

# same-frame parity vs a pre-P-211 build
SDL_VIDEODRIVER=offscreen ./build/native/melee-demo --view --frames 3 \
    --screenshot /tmp/after.bmp
magick compare -metric RMSE /tmp/before.bmp /tmp/after.bmp null:
```

`--frame`-identical runs of the same build are byte-identical (deterministic).

## Implication for the port

- P-204 can now add TEV state (alpha test, `RENDER_XLU`, multi-texture,
  material alpha) as uniforms and extra texture units without another
  structural rewrite. The model fragment shader already has
  `u_use_texture`, `u_material` and `u_alpha_test` hooks.
- A WASM/WebGL2 build can reuse the shader bodies; it needs a loader shim for
  the GL entry points that `GL_GLEXT_PROTOTYPES` provided on Linux.
- Keep bind-pose and pose buffers separate (G-035 equivalent for VBOs); never
  draw the bind path from the posed buffer.
