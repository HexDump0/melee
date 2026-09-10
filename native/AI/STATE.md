# State of the port

Last updated: 2026-09-10 (viewer + part isolation)

> Update this file whenever behavior changes. Keep it factual: what a fresh
> `git pull` + build does today.

## TL;DR

A playable two-player sandbox runs natively on Linux, rendering real disc
assets: Mario, Fox, Pikachu and Young Link all decode with textures. There is
also an interactive 3D model viewer with orbit/zoom/wireframe and per-part
isolation for inspecting any `Pl*Nr.dat` on the disc. Fighters are in bind (T)
pose because animation is not implemented. Movement uses real Mario attributes
read from the disc.

## Verified working

| Capability | Evidence |
|---|---|
| Disc read (CISO) | `--inspect` finds and extracts `PlMrNr.dat` (473,522 bytes) |
| FST lookup | Finds files in the root and one level deep |
| HSD joint/DObj/PObj walk | Mario: 68 PObjs, 6328 triangles |
| Bind-pose envelope skinning | Rendered Mario is a coherent T-pose; bounds `[-7.56 -0.28 -2.70]..[7.57 14.21 3.58]` |
| GX display lists | Strips/triangles/quads decoded; clean opcode histogram (only 0x80/0x90/0x98) |
| Textures | 30 textures for Mario (CMPR etc.), correct cap/overalls/face |
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
5. **Face expressions overlap.** Melee hides alternate expression DObjs through
   the animation-driven `ftParts`/`FtPartsVis` system. With no animation all
   expression meshes draw at once, so eyes/mustache look smeared. Workaround:
   viewer part isolation. Real fix: P-201 or parsing part visibility
   (`learnings/fighter_data.md`). This is visual only; geometry is correct.
6. **Paletted textures unsupported.** `CI4`/`CI8` + TLUT are skipped, so those
   parts fall back to material color. Character eyes and some effects use them.
   Workstream P-203.
7. **TEV approximated.** Rendering is `texture * material color` with fixed
   function lighting. Multi-texture, toon ramps, alpha test thresholds and
   additive blends will not match the GameCube. Workstream P-204.
8. **No audio, menus, items, stages, results, netplay, WASM.**
9. **Non-Mario physics values** are demo defaults, not per-character data.
10. **Windows/macOS untested.** Linux + Mesa is the only verified target.

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
