# Repo file map (port-relevant)

Only the parts of the decompilation that matter for porting. Everything else is
decomp business.

## Port code (our deliverable)

```
native/
├── main.c                  entry point, CLI, mode dispatch
├── platform/
│   ├── disc.c/.h           disc images, FST, archive symbol enumeration
│   └── screenshot.c/.h     framebuffer -> BMP
├── hsd/                    hand-rebuilt engine layer
│   ├── model.c/.h          HSD parse, skinning, materials, batches
│   ├── aobj.c/.h           FObj curve player
│   ├── anim.c/.h           FigaTree clips + joint binding
│   ├── parts.c/.h          ftData visibility + model scaling
│   └── light.c/.h          HSD_LightDesc / HSD_FogDesc
├── gx/                     GX replacement (ours forever)
│   ├── gl.h                OpenGL include point
│   ├── shader.c/.h         GLSL compile/link + version header
│   ├── render.c/.h         shaders, buffers, GX material state, Visual
│   ├── overlay.c/.h        HUD/grid/debug geometry
│   ├── math.c/.h           temporary hand matrix math (P-301 target)
│   └── texture.c/.h        GX texture decode
├── game/attributes.c/.h    ftData -> movement attributes
├── extras/                 port-only features, not the game
│   ├── viewer.c/.h         interactive model viewer
│   ├── sandbox.c/.h        placeholder match loop
│   ├── physics.c/.h        placeholder movement model
│   └── font.h              5x7 HUD font
├── decomp/                 build glue + shims for src/ (P-301, empty)
├── tests/test_math.c       CTest math harness
├── CMakeLists.txt          build; -DMELEE_SANITIZE=ON
└── AI/                     this knowledge base
```

## Brand assets

```
assets/                     logo artwork, banner + icon, SVG and PNG,
                            each on black, transparent, and light-surface
assets/melee-unbound-boot.mp4   boot animation, generated from the banner SVG
scripts/make_boot_animation.py  regenerates it; re-run if the mark changes
site/                       the project website; React + Vite, `npm run build`
                            (ADR-0030). Copy lives in src/content.tsx, the
                            design system in src/styles.css.
```

Rules for using them — palette, contrast, clear space, what not to do — are in
[`branding.md`](branding.md). Do not recolour or redraw the mark ad hoc.

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
