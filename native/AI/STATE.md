# State of the port

Last updated: 2026-09-10 (correct models: right matrix, part visibility, TLUT)

> Update this file whenever behavior changes. Keep it factual: what a fresh
> `git pull` + build does today.

## TL;DR

A playable two-player sandbox runs natively on Linux, rendering real disc
assets. Characters decode with correct bind-pose skinning, the `right` matrix
that attaches PObjs on non-root joints, the game's own part visibility tables
(neutral face, hidden alternate expressions) and full GX texture support
including CI4/CI8 + TLUT. An interactive 3D viewer with orbit/zoom/wireframe,
part isolation and a hidden-part toggle inspects any `Pl*Nr.dat`. Fighters are
in bind (T) pose because animation is not implemented. Movement uses real Mario
attributes read from the disc.

## Verified working

| Capability | Evidence |
|---|---|
| Disc read (CISO) | `--inspect` finds and extracts `PlMrNr.dat` (473,522 bytes) |
| FST lookup | Finds files in the root and one level deep |
| HSD joint/DObj/PObj walk | Mario: 68 PObjs, 6328 triangles |
| Bind-pose envelope skinning | Rendered Mario is a coherent T-pose; bounds `[-7.56 -0.28 -2.70]..[7.57 14.21 3.58]` |
| `right` matrix | Link's sword/scabbard/shield sit on his back instead of the floor (bounds y-min rose from -6.14 to -0.01) |
| Part visibility | Mario hides 16 of 59 DObjs, Link 32 of 83; faces render in neutral pose |
| CI4/CI8 + TLUT | Mario eye atlas (190x190 CI8, palette RGB565) decodes; 31 textures total |
| GX display lists | Strips/triangles/quads decoded; clean opcode histogram (only 0x80/0x90/0x98) |
| Textures | 31 textures for Mario (CMPR + CI8), correct cap/overalls/face/eyes |
| Materials | Per-DObj diffuse color as vertex color |
| Cross-character | Fox 6658 tris/34 tex, Pikachu 4989/10, Young Link 7381/42 |
| Model enumeration | `--list-models` finds 33 `Pl*Nr.dat`; `--all-models` finds 273 |
| 3D viewer | Orbit/zoom, ground grid, wireframe, culling, auto-spin, screenshots |
| Part isolation | 68 batches for Mario; `--list-parts`, `[`/`]`, `V` modes |
| Mario attributes | accel .080, friction .060, run 1.500, gravity .095, terminal 1.70, air .045, jump 2.30, 2 jumps |
| Sandbox | Move, jump, shield, attack, damage, stocks, respawn, CPU, camera follow |
| Headless verify | `SDL_VIDEODRIVER=offscreen ... --frames N --screenshot` works |
| Sanitizers | 600-frame scripted run clean under ASan+UBSan (leaks disabled) |

## Known issues / gaps

Ordered by impact.

1. **No animation.** Fighters are frozen in bind pose. HSD `AObj` curves in the
   `*_matanim_joint` / animation joints are not evaluated, and the renderer
   bakes geometry into a single display list, which blocks per-joint transforms.
   Workstream P-200/P-201.
2. **`right` matrix not implemented.** For PObjs attached to non-skeleton-root
   joints, HSD multiplies by `_HSD_mkEnvelopeModelNodeMtx`. All Melee fighter
   DObjs hang off the skeleton root today, so it is unobservable, but any asset
   with a different topology needs it. Workstream P-202.
5. **Animated expressions not implemented.** The neutral pose is correct, but
   blinking/damage expressions need the animation system (P-201) to drive
   `ftParts_80074B0C` indices. Model visibility tables are already parsed.
6. **TEV approximated.** Rendering is `texture * material color` with fixed
   function lighting. Multi-texture, toon ramps, alpha test thresholds and
   additive blends will not match the GameCube. Workstream P-204.
7. **No audio, menus, items, stages, results, netplay, WASM.**
8. **Non-Mario physics values** are demo defaults, not per-character data.
9. **Windows/macOS untested.** Linux + Mesa is the only verified target.

## Baseline commands

```sh
cmake -S native -B build/native -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build/native -j4
./build/native/melee-demo --inspect
./build/native/melee-demo --list-models
SDL_VIDEODRIVER=offscreen ./build/native/melee-demo --view --frames 3 \
    --screenshot /tmp/viewer.bmp
SDL_VIDEODRIVER=offscreen ./build/native/melee-demo --scripted --frames 240 \
    --screenshot /tmp/baseline.bmp
```

Expected `--inspect` tail:

```
Decoded PlMrNr.dat: 6328 triangles, 30 textures; bounds [-7.56 -0.28 -2.70] to [7.57 14.21 3.58]
```

If those numbers move, say why in the commit and update this file.

## Environment assumptions

- Linux, GCC/Clang, CMake, pkg-config, SDL2 dev, Mesa (`libEGL_mesa`,
  `libGL`), OpenGL math.
- Disc image at `iso/Super Smash Bros. Melee (USA) (En,Ja) (Rev 2).ciso` for
  default runs. `iso/` is locally excluded from git.
- `ACGC-PC-Port/` is a local, untracked reference checkout.
- The offscreen SDL driver + Mesa `radeonsi`/llvmpipe renders correctly; there
  is no X11 server available to agents on this machine.
