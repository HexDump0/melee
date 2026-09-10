# Port architecture

## One-line model

```
disc image ──> demo_assets ──> raw HSD bytes ──> demo_model ──> vertex/texture arrays ──> OpenGL
                                                     ▲
user input ──> main SDL loop ──> demo_physics ───────┘ (positions)
```

## Layers

1. **Platform layer** (`main.c`) — SDL2 window, OpenGL 3.3 core context
   (ADR-0009), input, fixed 60 Hz accumulator, camera, HUD, viewer, screenshots,
   CLI flags. Shader sources live here.
2. **Asset layer** (`demo_assets.c`) — CISO/ISO/GCM block reading, FST path
   lookup, archive public-symbol enumeration. Returns malloc'd file bytes.
3. **Model layer** (`demo_model.c`) — parses HSD joints, DObj/PObj descriptors,
   vertex descriptors and GX display lists; reconstructs the bind pose using
   envelope groups; decodes embedded textures via `demo_texture.c`; parses
   MObj/TObj materials and PE descriptors; re-skins the CPU vertex buffer per
   animated frame.
4. **Texture layer** (`demo_texture.c`) — GX tiled texture decode to RGBA8.
5. **Parts layer** (`demo_parts.c`) — `ftData<Char>` part visibility tables and
   the per-character `model_scaling`.
6. **Animation layer** (`demo_aobj.c`, `demo_anim.c`) — literal ports of the
   HSD FObj player and FigaTree node->joint binding.
7. **Data layer** (`demo_attributes.c`) — reads `ftDataMario` -> `ftCo_DatAttrs`
   from `PlMr.dat` and maps fields into `DemoPhysicsAttrs`.
8. **Simulation layer** (`demo_physics.c`) — demo-only movement/jump/attack/
   shield/stocks. Approximates `ftCommon_*` formulas but is **not** the engine.

## Data flow in detail

```
main()
  demo_asset_load(disc, "PlMrNr.dat")        // disc -> malloc'd archive
  demo_model_load(model, bytes, size, 0)     // bytes -> triangles + textures
    joint_table_add()                        // HSD_Joint tree -> world binds
    walk_joint() -> parse_pobj()
      read_descs()                           // HSD_VtxDescList[32]
      load_env_groups()                      // envelope groups -> matrices
      display list loop:
        read_vertex()                        // attributes in descriptor order
        apply rigid group matrix if any
        emit() -> DemoModelVertex[]
    find_or_add_texture()                    // imagedesc -> RGBA8
    parse_material()                         // MObj/PE/TObj TEV state per batch
  demo_parts_apply(disc, model)              // vis tables + model_scaling
  demo_model_pose_apply(model)               // scale + re-skin bind pose
  compile_model()                            // GL textures + per-batch VAO/VBOs
  per frame:
    demo_physics_step()                      // 60 Hz, real Mario attrs
    demo_anim_apply()                        // FObj playback -> joints
    demo_model_pose_apply()                  // re-skin CPU vertices
    draw_fighter() -> draw_batch()           // shader, GX material state
```

## Key invariants

- **HSD pointers are data-relative.** Every pointer field in an HSD archive is a
  32-bit offset from the start of the data section (`file_base + 0x20`). Zero
  means NULL unless the field is an array base, where zero means "start of
  data". See `learnings/hsd_archive_format.md`.
- **Display list attributes are consumed in descriptor order**, one descriptor
  per attribute per vertex, including matrix indices. Any mismatch desyncs the
  whole stream.
- **`PNMTXIDX` is a GX matrix slot.** Envelope group index = `PNMTXIDX / 3`.
- **Bind pose is a sum, not a single matrix.** Rigid groups (first weight 1)
  transform by the joint's bind world matrix; blended groups sum to identity at
  rest. See `learnings/hsd_models_and_skinning.md`.
- **Raw parser must stay 64-bit safe.** Never cast file offsets to host
  pointers. All reads go through `rb16/rb32/rf32` with bounds checks.
- **Animation is a CPU re-skin.** `demo_anim_apply` poses the joints per 60 Hz
  tick and `demo_model_pose_apply` rewrites the vertex buffer; the renderer
  uploads that into a per-batch pose VBO. The bind pose lives in a separate
  immutable VBO (ADR-0008).
- **Materials follow the decomp.** `MObjMakeTExp`/`TObjMakeTExp` state
  (channel raster, colormap/alphamap, lightmap phases, alpha test, blend) is
  derived in `demo_model.c` and evaluated by the model shader; see
  `learnings/hsd_tev_materials.md`.

## File map

| File | Responsibility |
|---|---|
| `native/main.c` | SDL/GL platform, shaders, input, camera, HUD, CLI, viewer |
| `native/demo_assets.c/.h` | Disc image + FST + archive symbol enumeration |
| `native/demo_model.c/.h` | HSD model decode, skinning, materials/TEV, textures |
| `native/demo_texture.c/.h` | GX texture formats -> RGBA8 |
| `native/demo_parts.c/.h` | `ftData<Char>` visibility tables + `model_scaling` |
| `native/demo_aobj.c/.h` | Literal HSD FObj player (`fobj.c`) |
| `native/demo_anim.c/.h` | FigaTree clips, node->joint binding, pose evaluation |
| `native/demo_attributes.c/.h` | `ftDataMario` -> `DemoPhysicsAttrs` |
| `native/demo_physics.c/.h` | Demo fighter sandbox |
| `native/demo_text.h` | 5x7 bitmap font for the HUD |
| `native/CMakeLists.txt` | Build, sanitizer option |

## Deliberate constraints

- **OpenGL 3.3 core + GLSL.** ADR-0009 replaced the fixed-function path so TEV
  can be expressed at all; shader bodies stay in an ES3/WebGL2-portable subset.
  The headless guarantee (SDL `offscreen` + Mesa) is part of the decision —
  keep verifying with `SDL_VIDEODRIVER=offscreen`.
- **No engine code compiled yet.** The decomp sources drag in the GameCube
  toolchain and 32-bit assumptions. The path to using them is staged: first
  reuse pure math and data tables, then HSD subsystems, then the fighter state
  machine. See `ROADMAP.md`.
- **Assets stay on the user's disc.** No extracted models, textures, or ROM
  data may be committed. CI can only run `--inspect` when a disc is absent.

## Reference port

`ACGC-PC-Port/` (git-ignored, local) is an Animal Crossing PC port. It is useful
as a catalogue of solutions for SDL/GL/GX-adjacent problems and its
`pc/DOCUMENTATION.md` describes a similar architecture. **Do not copy blindly:**
its `GXCallDisplayList` interprets Animal Crossing's custom PC command stream,
not GameCube display lists, so Melee rendering cannot reuse it directly. Its
32-bit build requirement is also not something we want.
