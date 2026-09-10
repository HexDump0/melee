# Glossary

| Term | Meaning |
|---|---|
| **AObj** | HSD animation object; keyframed curves for joints/materials |
| **AObjDesc** | Serialized animation data in a `.dat` archive |
| **blended group** | Envelope group with weights < 1; at bind pose its matrix sum is identity |
| **bind pose** | The model's rest pose stored in the archive; T-pose for fighters |
| **CISO** | Compressed ISO; 0x8000 header + block map + packed 0x200000-byte blocks |
| **DObj** | HSD drawable object: material + PObj list; `HSD_DObj` |
| **ECB** | Environmental collision box (fighter physics concept; not used yet) |
| **envelope** | Weighted joint influences for a vertex (skinning) |
| **FST** | GameCube file system table |
| **GX** | GameCube GPU API |
| **HSD** | The game's engine (formerly SysDolphin); model/anim/render system |
| **Joint** | HSD transform node; parent of DObjs and child joints |
| **MObj** | HSD material object |
| **PObj** | HSD polygon object: vertex format + display list; `HSD_PObj` |
| **PNMTXIDX** | Per-vertex position-matrix index (slot 0,3,6,...,27) |
| **right matrix** | Envelope-model node matrix for PObjs not on the skeleton root (not implemented) |
| **skin** | HSD PObj type using a single joint transform |
| **STRIP** | Triangle strip draw opcode 0x98 |
| **TEV** | GX texture environment unit (texture combiners) |
| **TLUT** | Texture lookup table (palette) for CI4/CI8 textures |
| **TObj** | HSD texture object; `HSD_TObjDesc` serialized form |
| **vtxfmt** | GX vertex format index encoded in the low bits of a draw opcode |
| **WASM** | WebAssembly target (future) |

## Acronyms in this repo

- **ft** — fighter (`src/melee/ft`)
- **gr** — stage/ground (`src/melee/gr`)
- **it** — item (`src/melee/it`)
- **gm** — game mode (`src/melee/gm`)
- **mn** — menu (`src/melee/mn`)
- **baselib** — HSD engine (`src/sysdolphin/baselib`)
