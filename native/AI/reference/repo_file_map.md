# Repo file map (port-relevant)

Only the parts of the decompilation that matter for porting. Everything else is
decomp business.

## Port code (our deliverable)

```
native/
├── main.c                SDL/GL loop, sandbox, viewer, CLI, HUD
├── demo_assets.c/.h      disc images, FST, archive symbol enumeration
├── demo_model.c/.h       HSD parse, skinning, materials, textures, batches
├── demo_texture.c/.h     GX texture decode
├── demo_attributes.c/.h  ftData -> movement attributes
├── demo_physics.c/.h     demo controller
├── demo_text.h           5x7 HUD font
├── CMakeLists.txt        build; -DDEMO_SANITIZE=ON
└── AI/                   this knowledge base
```

## Engine reference (read-only)

| Path | Why it matters |
|---|---|
| `src/sysdolphin/baselib/archive.c/.h` | HSD archive format and relocation |
| `src/sysdolphin/baselib/jobj.c/.h` | Joint layout, `HSD_JObjMakeMatrix`, skeleton |
| `src/sysdolphin/baselib/dobj.c/.h` | `HSD_DObj`, `DOBJ_HIDDEN` |
| `src/sysdolphin/baselib/pobj.c/.h` | Vertex formats, envelope groups, `PObjSetupMtx` |
| `src/sysdolphin/baselib/mobj.c/.h` | Materials, TEV descriptions |
| `src/sysdolphin/baselib/tobj.c/.h` | Texture descriptors and matrices |
| `src/sysdolphin/baselib/mtx.c` | `HSD_MtxSRT`, inverse-transpose, helpers |
| `src/sysdolphin/baselib/displayfunc.c` | Display traversal, `_HSD_mkEnvelopeModelNodeMtx` |
| `src/melee/ft/ftcommon.c` | Movement formulas (`CalcGroundAccel`, `Fall`, drift) |
| `src/melee/ft/ftparts.c` | DObj part assignment and visibility |
| `src/melee/ft/types.h` | `ftData`, `ftCo_DatAttrs`, `Fighter` |
| `extern/dolphin/include/dolphin/gx/GXEnum.h` | Attribute/format enum values |

## Local-only (never committed)

| Path | Notes |
|---|---|
| `iso/` | disc images |
| `ACGC-PC-Port/` | reference Animal Crossing port |
| `build/`, `build/native-asan/` | build outputs |
| `/AI/` (root) | decomp agent hub; different workflow |

## Commands that touch the decomp build

Do not break these; they are upstream CI:

```sh
python configure.py && ninja
```
