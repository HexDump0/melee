# Port architecture

## One-line model

```
disc image ──> demo_assets ──> raw HSD bytes ──> demo_model ──> vertex/texture arrays ──> OpenGL
                                                     ▲
user input ──> main SDL loop ──> demo_physics ───────┘ (positions)
```

## Layers

1. **Platform layer** (`main.c`) — SDL2 window, OpenGL 2.1 context, input,
   fixed 60 Hz accumulator, camera, HUD, screenshots, CLI flags.
2. **Asset layer** (`demo_assets.c`) — CISO/ISO/GCM block reading, FST path
   lookup, archive public-symbol enumeration. Returns malloc'd file bytes.
3. **Model layer** (`demo_model.c`) — parses HSD joints, DObj/PObj descriptors,
   vertex descriptors and GX display lists; reconstructs the bind pose using
   envelope groups; decodes embedded textures via `demo_texture.c`; material
   colors; fills a flat `DemoModelVertex` triangle list.
4. **Texture layer** (`demo_texture.c`) — GX tiled texture decode to RGBA8.
5. **Data layer** (`demo_attributes.c`) — reads `ftDataMario` -> `ftCo_DatAttrs`
   from `PlMr.dat` and maps fields into `DemoPhysicsAttrs`.
6. **Simulation layer** (`demo_physics.c`) — demo-only movement/jump/attack/
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
  compile_model()                            // GL texture objects + display list
  per frame:
    demo_physics_step()                      // 60 Hz, real Mario attrs
    draw_fighter() -> glCallList()           // textured bind-pose Mario
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
- **No animation yet.** The joint matrices are evaluated once at bind pose. The
  renderer currently bakes geometry into one GL display list, so adding
  animation will require per-joint primitives (see `TASKS.md` P-201).

## File map

| File | Responsibility |
|---|---|
| `native/main.c` | SDL/GL platform, input, camera, HUD, CLI, viewer mode |
| `native/demo_assets.c/.h` | Disc image + FST + archive symbol enumeration |
| `native/demo_model.c/.h` | HSD model decode, skinning, materials, textures |
| `native/demo_texture.c/.h` | GX texture formats -> RGBA8 |
| `native/demo_attributes.c/.h` | `ftDataMario` -> `DemoPhysicsAttrs` |
| `native/demo_physics.c/.h` | Demo fighter sandbox |
| `native/demo_text.h` | 5x7 bitmap font for the HUD |
| `native/CMakeLists.txt` | Build, sanitizer option |

## Deliberate constraints

- **OpenGL 2.1 fixed function.** Chosen because it runs on old laptops, in
  Mesa softpipe/llvmpipe and under SDL's offscreen driver, which is how CI and
  agents verify rendering. Do not move to core profiles without a decision
  entry and a headless fallback.
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
