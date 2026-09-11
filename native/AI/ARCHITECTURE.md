# Port architecture

## One-line model

```
disc image ──> platform/disc ──> raw HSD bytes ──> hsd/* ──> CPU vertices ──> gx/render ──> OpenGL
                                                      ▲
input ──> extras (viewer/sandbox) ──> game/* ─────────┘
```

Two things to keep separate in your head:

- **The port** — everything that will still exist when the game runs:
  `platform/`, `hsd/`, `gx/`, `game/`, and eventually `decomp/`.
- **The extras** — port-only conveniences (`extras/`): the model viewer and the
  temporary movement sandbox. They may be deleted or kept, but they are not
  part of the game.

## Layers

1. **`platform/`** — PC-side glue. `disc.c` reads CISO/ISO/GCM images, walks
   the FST and enumerates archive symbols. Later: window/input/time/audio.
2. **`hsd/`** — the rebuilt GameCube engine layer, transcribed from `src/`:
   - `model.c` parses HSD archives (joints, DObj/PObj, display lists,
     materials) and evaluates poses with the engine's skinning rules.
   - `aobj.c` is a literal port of the FObj curve player (`fobj.c`).
   - `anim.c` loads `Pl<Char>AJ.dat` FigaTree clips and binds nodes to joints.
   - `parts.c` applies `ftData` part visibility and per-character model scaling.
   - `light.c` parses the scene `HSD_LightDesc`/`HSD_FogDesc` descriptors.
   As `decomp/` starts compiling the real sources, files here shrink; the raw
   archive parser stays ours (the decomp expects relocated in-memory structs).
3. **`gx/`** — the GX replacement, ours forever:
   - `texture.c` decodes GX tiled texture formats to RGBA.
   - `render.c` owns the GL renderer: shaders, per-batch buffers, GX material
     state (alpha/blend/Z/TEV inputs) and one `Visual` per character.
   - `overlay.c` provides the CPU-side 2D/3D drawing used by extras.
   - `math.c` is temporary hand matrix math, to be replaced by compiled
     `mtx.c`/`vec.c` (P-301); `tests/` compares them.
   - `shader.c` compiles the ES3-portable GLSL (ADR-0009).
4. **`game/`** — game-domain data and logic. Today only `attributes.c` (Mario
   movement values); future home of fighter states, stages and menus.
5. **`extras/`** — port-only features:
   - `viewer.c` interactive model viewer,
   - `sandbox.c` placeholder match loop; `physics.c` its movement model
     (delete when real movement lands),
   - `font.h` HUD bitmap font.
6. **`decomp/`** — build glue and shims for compiling `src/` files natively.
   Starts with `mtx.c`/`vec.c` behind an almost-empty shim (P-301).
7. **`main.c`** — entry point and CLI; dispatches to the viewer or sandbox.

## Data flow

```
main()
  disc_load(disc, "PlMrNr.dat")             // platform
  hsd_model_load(model, bytes, size, 0)     // hsd/model.c
    joint/matrix setup + display-list walk
    parse material/PE/TObj TEV state
  parts_apply(disc, model)                  // hsd/parts.c
  hsd_model_pose_apply(model)               // bind pose + model scale
  visual_compile(visual)                    // gx/render.c: GL textures + VBOs
  per frame:
    sandbox_* / viewer input                // extras
    anim_apply(anim, model, frame)          // hsd/anim.c + aobj.c
    render_set_view(...); render_batch(...) // gx/render.c
    overlay draws + screenshots             // gx/overlay.c, platform/
```

## Key invariants

- **HSD pointers are data-relative.** Every pointer field is a 32-bit offset
  from the data section (`file + 0x20`). Zero is NULL for link pointers and
  "start of data" for array bases. See `learnings/hsd_archive_format.md`.
- **Display list attributes are consumed in descriptor order**; a mismatch
  desyncs the stream.
- **`PNMTXIDX` is a GX matrix slot**; envelope group index = `PNMTXIDX / 3`.
- **Animation is a CPU re-skin.** `anim_apply` poses joints on the fixed 60 Hz
  tick and `hsd_model_pose_apply` rewrites the vertex buffer; the renderer
  uploads it to a per-batch pose VBO. The bind pose lives in a separate
  immutable VBO (ADR-0008).
- **Materials follow the decomp.** `MObjMakeTExp`/`TObjMakeTExp` state is
  derived in `hsd/model.c` and evaluated by the model shader; see
  `learnings/hsd_tev_materials.md`.
- **Raw parser stays 64-bit safe.** Never cast file offsets to host pointers;
  all reads are bounds checked.
- **One source of truth.** When a `decomp/`-compiled function replaces a hand
  version, the hand version is deleted in the same commit.

## Deliberate constraints

- **OpenGL 3.3 core + GLSL** (ADR-0009), shader bodies in an
  ES3/WebGL2-portable subset. Keep verifying with `SDL_VIDEODRIVER=offscreen`.
- **No new dependencies** without a decision entry. Current: SDL2, OpenGL,
  libm, C11.
- **Assets stay on the user's disc.** No extracted models, textures or ROM
  data may be committed.

## Reference port

`ACGC-PC-Port/` (git-ignored, local) is an Animal Crossing PC port, useful as a
catalogue of SDL/GL/GX-adjacent solutions. **Do not copy blindly:** it
interprets Animal Crossing's custom PC command stream, not Melee's display
lists, and requires a 32-bit build.
