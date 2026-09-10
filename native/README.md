# Melee native experiment

An early Linux native sandbox that loads real Super Smash Bros. Melee character
assets directly from your own disc image and renders them with OpenGL.

**This is not a port of the Melee engine.** The renderer, stage, camera, HUD and
movement model are original demo code. The model geometry, textures and Mario's
common movement attributes are read from the disc at runtime.

No game assets are distributed with this repository. The executable reads your
local disc image (CISO, ISO or GCM). The original GameCube build target is
unchanged and remains available.

## Status

| Area | State |
|---|---|
| Disc reading (CISO/ISO/GCM, FST) | Works |
| HSD model decode (joints, DObj/PObj, display lists) | Works |
| Bind-pose envelope skinning | Works |
| GX texture decode (I4/I8/IA4/IA8/RGB565/RGB5A3/RGBA8/CMPR) | Works |
| Mario movement attributes from `PlMr.dat` | Works |
| Sandbox movement, jumping, shield, one attack | Works |
| Gameplay animation from `Pl*.dat` animation tables | Not implemented |
| Audio, menus, combat states, knockback model, items | Not implemented |
| Other characters | Should work via `--model Pl**.dat`; untested |
| Windows / macOS / WASM | Untested |

The fighters are shown in their bind (T) pose. The demo controller is a small
original state machine inspired by `ftCommon_*` formulas, not the original
fighter state machine.

## Build

Requires a C compiler, CMake, pkg-config, SDL2 development files, OpenGL and
Math libraries. From the repository root:

```sh
cmake -S native -B build/native -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build/native -j4
```

## Run

```sh
./build/native/melee-demo
```

The default image is `iso/Super Smash Bros. Melee (USA) (En,Ja) (Rev 2).ciso`.
Pass `--disc /path/to/image.ciso` to use another image.

### Controls

| Player 1 | Key |
|---|---|
| Move | `A` / `D` |
| Jump | `W` or `Space` |
| Attack | `F` |
| Shield | `G` |

| Player 2 | Key |
|---|---|
| Move | `Left` / `Right` |
| Jump | `Up` |
| Attack | `K` |
| Shield | `L` |

`F2` toggles the CPU, `P` pauses, `R` respawns, `Esc` quits. A game controller
can also drive player 1.

### Diagnostics

```sh
# Parse the model and print counts/bounds without opening a window
./build/native/melee-demo --inspect

# Render an isolated bind-pose view to a BMP
./build/native/melee-demo --view --angle 180 --screenshot /tmp/mario.bmp

# Run a fixed number of frames headlessly (SDL offscreen video driver)
SDL_VIDEODRIVER=offscreen ./build/native/melee-demo --scripted --frames 240 \
    --screenshot /tmp/gameplay.bmp
```

`--model Pl**.dat` selects a different costume archive (for example
`PlFxNr.dat` for Fox). Model root selection uses the archive's public symbol
table, so model-only archives work without extra configuration.

## Implementation notes

- `demo_assets.c` reads CISO/ISO/GCM images and resolves FST paths.
- `demo_model.c` walks HSD joints, display lists and envelope groups and applies
  the same bind-pose transforms HSD uses at rest. HSD archive pointers are
  32-bit offsets from the start of the data section; every read is bounds
  checked so 64-bit hosts are safe.
- `demo_texture.c` decodes the GX texture formats used by the fighter costumes.
  Paletted (`CI4`/`CI8`) textures are skipped and fall back to material color.
- `demo_attributes.c` reads `ftDataMario`'s `ftCo_DatAttrs` from `PlMr.dat`.
- `demo_physics.c` is the sandbox controller. It uses real Mario values where
  available and clearly marked demo approximations elsewhere.

## Legal

This repository is a decompilation research project and ships no game assets.
You must provide your own legally obtained disc image. See the repository root
for license information. The CISO reader was informed by
`ACGC-PC-Port/pc/src/pc_disc.c` (MIT, FlyingMeta); see `THIRD_PARTY_NOTICES.md`.
