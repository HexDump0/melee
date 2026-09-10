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
| `right` matrix (PObjs on non-root joints, e.g. Link's sword/shield) | Works |
| PObj types: skin, shared-vertex skin, shape animation | Works |
| Fighter part visibility (neutral expression, hidden alternates) | Works |
| Per-PObj GX culling (front/back/both) | Works |
| `JOBJ_HIDDEN` joints and material z-mode bits | Works |
| GX vertex colours (RGB565/RGB8/RGBX8/RGBA4/RGBA6/RGBA8) | Works |
| GX texture matrices (`MakeTextureMtx`: repeat/scale/rotate/translate) | Works |
| GX texture decode (I4/I8/IA4/IA8/RGB565/RGB5A3/RGBA8/CMPR/CI4/CI8+TLUT) | Works |
| Mario movement attributes from `PlMr.dat` | Works |
| Sandbox movement, jumping, shield, one attack | Works |
| Interactive 3D model viewer (orbit, zoom, parts) | Works |
| Fighter animation from `Pl*AJ.dat` FigaTree clips | Works (viewer playback + sandbox clip switching) |
| Animated face expressions (blinking, damage) | Not implemented; neutral pose is correct |
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

### 3D model viewer

```sh
./build/native/melee-demo --view
```

The viewer orbits any character model loaded from the disc with perspective
projection and a ground grid.

| Viewer control | Action |
|---|---|
| Left drag / arrow keys | Orbit |
| Mouse wheel / `+` / `-` | Zoom |
| `N` / `P` | Next / previous character model |
| `[` / `]` | Select previous / next mesh part |
| `V` | Part mode: all / only selected / hide selected |
| `A` | Play / pause the fighter animation |
| `,` / `.` | Step one animation frame (pauses) |
| `Z` / `X` | Previous / next animation clip |
| `M` | Cycle playback speed (0.25x / 0.5x / 1x / 2x) |
| `B` | Cycle visibility slot (body / reflection / metal) |
| `Y` | Reveal hidden parts |
| `T` / `L` / `W` / `C` | Toggle textures / lighting / wireframe / culling |
| `G` / `Space` | Toggle grid / auto-spin |
| `R` | Reset camera |
| `F12` | Save a screenshot |
| `H` | Help overlay |
| `Esc` | Quit |

The neutral face now uses the fighter's own part visibility tables from
`Pl<Char>.dat`, so alternate expression meshes are hidden exactly as the game
hides them before animation. `X` reveals hidden parts for debugging, and part
isolation is still useful to inspect individual meshes.

### Diagnostics

```sh
# Parse the model and print counts/bounds without opening a window
./build/native/melee-demo --inspect

# List every character model archive on the disc
./build/native/melee-demo --list-models
./build/native/melee-demo --all-models --list-models

# List the animation clips in the character's Pl*AJ.dat archive
./build/native/melee-demo --model PlMrNr.dat --list-clips

# Play a clip in the viewer and capture a deterministic frame
SDL_VIDEODRIVER=offscreen ./build/native/melee-demo --view --animate \
    --clip Wait1 --anim-frame 25 --frames 1 --screenshot /tmp/anim.bmp

# List the mesh parts of a model (index, vertex count, bounds)
./build/native/melee-demo --inspect --list-parts

# Dump the per-batch GX material state (rendermode, PEDesc, TObj chain)
./build/native/melee-demo --dump-tev

# Render an isolated view to a BMP, optionally isolating one part
./build/native/melee-demo --view --angle 180 --screenshot /tmp/mario.bmp
./build/native/melee-demo --view --part 21 --part-mode only --frames 3 \
    --screenshot /tmp/part.bmp

# Frame one part, zoom in, inspect hidden parts or a visibility slot
./build/native/melee-demo --view --part 23 --part-mode only --zoom 0.5
./build/native/melee-demo --view --no-cull --show-hidden --vis-slot 1

# Extract a disc file / dump decoded textures for offline analysis
./build/native/melee-demo --extract PlCo.dat /tmp/PlCo.dat
./build/native/melee-demo --dump-textures /tmp/tex

# Run a fixed number of frames headlessly (SDL offscreen video driver)
SDL_VIDEODRIVER=offscreen ./build/native/melee-demo --scripted --frames 240 \
    --screenshot /tmp/gameplay.bmp
```

`--model Pl**.dat` selects a costume archive (for example `PlFxNr.dat` for Fox).
`--model-index N` selects by position in the `--list-models` output. Model root
selection uses the archive's public symbol table, so model-only archives work
without extra configuration.

## Implementation notes

- `demo_assets.c` reads CISO/ISO/GCM images and resolves FST paths.
- `demo_model.c` walks HSD joints, display lists and envelope groups and applies
  the same transforms HSD uses (bind pose by default, animated on demand). It
  keeps the raw per-vertex skin inputs and re-skins each evaluated frame. HSD
  archive pointers are 32-bit offsets from the start of the data section; every
  read is bounds checked so 64-bit hosts are safe.
- `demo_texture.c` decodes the GX texture formats used by the fighter costumes,
  including paletted (`CI4`/`CI8` + TLUT) textures.
- `demo_aobj.c` is a literal port of the HSD FObj curve player
  (`src/sysdolphin/baselib/fobj.c`).
- `demo_anim.c` walks the `Pl<Char>AJ.dat` clip container, binds FigaTree nodes
  to joints exactly like `ftAnim_8006F4C8`, and poses the model.
- `demo_attributes.c` reads `ftDataMario`'s `ftCo_DatAttrs` from `PlMr.dat`.
- `demo_physics.c` is the sandbox controller. It uses real Mario values where
  available and clearly marked demo approximations elsewhere.

## Legal

This repository is a decompilation research project and ships no game assets.
You must provide your own legally obtained disc image. See the repository root
for license information. The CISO reader was informed by
`ACGC-PC-Port/pc/src/pc_disc.c` (MIT, FlyingMeta); see `THIRD_PARTY_NOTICES.md`.
