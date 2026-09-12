# State of the port

Last updated: 2026-09-12 (S2 complete; viewer P-611; P-610 fixed, TEV stage limit 8)

> Update this file whenever behavior changes. Keep it factual: what a fresh
> `git pull` + build does today.

## Direction (2026-09-11): pivot to a decompilation-based port

**ADR-0010 supersedes ADR-0001.** The project is moving from the hand-written
prototype described below to a full-game port that compiles `src/` itself and
replaces only the GameCube hardware under `native/` (OS, DVD/asset loading,
GX→graphics HLE, AX→audio, input). The decomp is essentially complete
(19,820/19,828 functions, 99.96%, match the retail DOL) and 834/1034 `src`
files already compile behind the P-301 shim — see
[`learnings/decomp_port.md`](learnings/decomp_port.md).

Until S2/S3 land, **this file describes the prototype**, which stays runnable
as the dev tool and the per-layer parity oracle. Hand-port engine work
(P-204 leftovers, P-205..P-210, P-302, P-411, P-412) is `parked` in
`TASKS.md`. **S0 passed (2026-09-11):** the compiled decomp's own
`HSD_ArchiveParse` and `HSD_JObjLoadJoint` run on a retail `PlMrNr.dat` and
reproduce the prototype's bind-pose world matrices bitwise (61/61 joints); see
`learnings/decomp_port.md` §6. The port builds 32-bit for compiled code
(ADR-0012); **S1 passed (2026-09-11):** `melee_decomp_boot` compiles the
decompilation's own `main()` (`src/melee/gm/gmmain.c:130`) and runs it behind
the OS/DVD/GX/VI platform layer to a controlled stop, with the triage log and
backend work list in `learnings/decomp_boot.md` and
`logs/2026-09-11-S1-boot-triage.md`. The next milestone is **S2**
(HSD runtime + GX HLE). No `src/` or `extern/` file was changed in S1.

**S2 passed (2026-09-12):** `test_decomp_render` loads a retail `PlMrNr.dat`
through the compiled `HSD_ArchiveParse`/`HSD_JObjLoadJoint`/`HSD_JObjDispAll`
path and renders it through the new GX HLE + GLES3 backend
(`native/decomp/gx/`, `native/decomp/hsd/`) with the compiled `ftData` part
visibility (16/59 DObjs hidden) and the prototype's camera/lights.  Screenshot
parity against the prototype viewer is RMSE 10.53/255 over the model region;
the residual is specular shading (explained in
`learnings/decomp_s2_gx_hle.md`).  The S1 boot target now runs the GX command
surface for real (`94 stub_calls / 33 unique`).  Still no `src/`/`extern/`
edits. **P-611 (2026-09-12):** `melee_decomp_viewer` presents the same
compiled HSD + GX HLE frame in an SDL3 window (orbit/zoom/model cycle, part
visibility, texture/light toggles, F12 screenshot); SDL3 is a new compiled-
target dependency (ADR-0014, install list in `TESTING.md`). **P-610
(2026-09-12):** Falcon's silver body was the P-607 `out_reg` fix; Giga Koopa's
limb noise was a `GX_TG_TEXCOORDn` coord chain folded onto the 0/1 UV varyings
(G-058); the draw state now captures 8 TEV stages (Master Hand uses 6, G-059)
and each frame restores the depth/color write masks before `glClear` so
camera orbits stop losing geometry (G-057). Next milestone:
**S3** (host-endian asset pipeline + DVD/ARQ).

## TL;DR

A playable two-player sandbox runs natively on Linux, rendering real disc
assets. Characters decode with correct bind-pose skinning, the `right` matrix
that attaches PObjs on non-root joints, the game's own part visibility tables
(neutral face, hidden alternate expressions) and full GX texture support
including CI4/CI8 + TLUT. An interactive 3D viewer with orbit/zoom/wireframe,
part isolation and a hidden-part toggle inspects any `Pl*Nr.dat`. Fighters now
play their real `Pl<Char>AJ.dat` FigaTree clips: the viewer can play, pause,
scrub and cycle clips, and the sandbox switches Wait/Walk/Dash/Jump/Fall clips
per fighter. Animation runs through a literal port of the engine's FObj state
machine, joint transforms and envelope/shared/rigid skinning. Movement uses
real Mario attributes read from the disc. Rendering is OpenGL 3.3 core with
GLSL shaders written in an ES3/WebGL2-portable subset (ADR-0009); the
fixed-function/display-list path is gone. Materials follow the decomp's
`MObjMakeTExp`/`TObjMakeTExp` state (channel raster, TEV colormap/alphamap and
lightmap phases, alpha test, XLU blend) and every fighter is scaled by its
`ftData.model_scaling` like `Fighter_UpdateModelScale` (Bowser 0.69, Kirby
0.92, Mario 1.10).

## Verified working

| Capability | Evidence |
|---|---|
| Disc read (CISO) | `--inspect` finds and extracts `PlMrNr.dat` (473,522 bytes) |
| FST lookup | Finds files in the root and one level deep |
| HSD joint/DObj/PObj walk | Mario: 68 PObjs, 6328 triangles |
| Bind-pose envelope skinning | Rendered Mario is a coherent T-pose; bounds `[-8.31 -0.31 -2.97]..[8.32 15.63 3.94]` (includes the game's 1.10 model scale) |
| Model scaling | `Fighter_UpdateModelScale` from `ftData.model_scaling`: Bowser 0.69, Kirby 0.92, Mario 1.10, Luigi 1.25; Mr. Game & Watch's `x34_scale.z` (0.01) flattening |
| Texture filtering | Per-TObj `HSD_TexLODDesc` min/mag filters, CI downgrade, LOD bias in the shader, anisotropy when the driver supports it |
| `right` matrix | Link's sword/scabbard/shield sit on his back instead of the floor (bounds y-min rose from -6.14 to -0.01) |
| Part visibility | Mario hides 16 of 59 DObjs, Link 32 of 83; faces render in neutral pose |
| CI4/CI8 + TLUT | Mario eye atlas (190x190 CI8, palette RGB565) decodes; 32 textures total (incl. one TEX1 map) |
| PObj types | SKIN (shared two-slot and rigid) and SHAPEANIM handled; Kirby, Link, Falcon, Game & Watch colors correct |
| Vertex colours | GX colour enum (RGB565/RGB8/RGBX8/RGBA4/RGBA6/RGBA8) decoded; fixes desync on coloured meshes |
| Per-PObj culling | GX cull modes + clockwise front faces; Master Hand renders solid |
| Hidden joints | `JOBJ_HIDDEN` skipped; fixes Mario's cap emblem/face smear and cuts most of Game & Watch's extra pieces |
| Material z-mode | `RENDER_ZMODE_ALWAYS` / `RENDER_NO_ZUPDATE` honoured per batch |
| Texture matrix | `MakeTextureMtx` (`repeat_s/t`, scale, rotate, translate) applied per batch; Mario's mirrored cap "M" is complete |
| Mipmaps | `glGenerateMipmap` on upload; filtering per TObj as above |
| Renderer | OpenGL 3.3 core, GLSL 330 in an ES3 subset; per-batch VAO/VBOs, no display lists/immediate mode |
| Render parity | Bind/anim/back-view screenshots RMSE <= 3.2e-6 vs the pre-rewrite build; scripted 88/1,024,000 pixels (HUD alpha) |
| Visibility slots | `FtPartsVis` slot semantics documented; viewer `B` / `--vis-slot N` cycles them |
| GX display lists | Strips/triangles/quads decoded; clean opcode histogram (only 0x80/0x90/0x98) |
| Textures | 32 textures for Mario (CMPR + CI8 + TEX1), correct cap/overalls/face/eyes |
| Materials | `MObjMakeTExp`/`TObjMakeTExp` common path: material constant/RAS initial stage, colormap/alphamap, RENDER_DIFFUSE lit stage |
| GX channels | `HSD_SetupChannelMode` case 4 lighting (`mat_ambient*ambient + light*N·L`), unlit vertex-colour default |
| Scene lights | `HSD_LightDesc` sets from `MnSlChr` (`--dump-lights`): white infinite + 0.4 ambient; shader evaluates the GX channel from real light objects |
| Fog | `HSD_FogDesc` from the same scene (linear 500-1000) evaluated in the shader |
| Alpha/blend/Z | `HSD_SetupPEMode`: XLU blend factors, alpha->0 discard, custom `PEDesc`, per-material Z func/update |
| Multi-texture | TEX0+TEX1 decoded; second TObj colormap/alphamap + its `MakeTextureMtx` uniformly sampled |
| Cross-character | Fox 6658 tris/34 tex, Pikachu 4989/10, Young Link 7381/42 |
| Model enumeration | `--list-models` finds 33 `Pl*Nr.dat`; `--all-models` finds 273 |
| 3D viewer | Orbit/zoom, ground grid, wireframe, culling, auto-spin, screenshots |
| Part isolation | 68 batches for Mario; `--list-parts`, `[`/`]`, `V` modes |
| Mario attributes | accel .080, friction .060, run 1.500, gravity .095, terminal 1.70, air .045, jump 2.30, 2 jumps |
| Sandbox | Move, jump, shield, attack, damage, stocks, respawn, CPU, camera follow |
| FigaTree clips | `--list-clips` finds 195 clips for `PlMrNr.dat` (Wait1 50 frames) |
| FObj playback | `hsd/aobj.c` matches a literal `fobj.c` transcription on 5661 samples (worst 6.4e-7) |
| Animation viewer | `--view --animate --clip Wait1`; `A`, `,`/`.`, `Z`/`X`, `M`; HUD shows clip/frame |
| Match animation | Both fighters pose independently; `Wait1`/`WalkMiddle`/`Dash`/`JumpF`/`Fall` by movement |
| Headless verify | `SDL_VIDEODRIVER=offscreen ... --frames N --screenshot` works |
| Sanitizers | 600-frame scripted run clean under ASan+UBSan (leaks disabled) |
| Unit tests | `ctest --test-dir build/native` (hand matrix math + compiled `HSD_MtxSRT` bitwise parity) |
| Compiled decomp math | `HSD_MtxSRT` built verbatim from `src/sysdolphin/baselib/mtx.c` behind `native/decomp/shim/`; SDK mtx/vec pairs are Metrowerks asm and stay hand-ported (P-301, `learnings/decomp_shim.md`). Bind/animate/scripted BMPs byte-identical |
| Compiled boot skeleton (S1) | `melee_decomp_boot` runs the decomp's `main()` for 10 frames under the platform stubs, reaches the game's own loading wait, and stops on the frame budget with a deterministic triage log (`logs/2026-09-11-S1-boot-triage.md`); `ctest decomp_boot` is the regression |
| Compiled HSD + GX HLE (S2) | `test_decomp_render` loads `PlMrNr.dat` through the compiled HSD display path and the `native/decomp/gx/` backend, applies the compiled `ftData` part visibility (16/59 hidden) and `Fighter_UpdateModelScale`, and renders with GLES3; world bounds equal the prototype's exactly, screenshot RMSE 10.94/255 (HUD excluded). `ctest decomp_render` is the regression; `logs/2026-09-12-S2-render.md` is the evidence |
| Interactive compiled viewer (P-611) | `melee_decomp_viewer` (SDL3 window + EGL/GLES3 via `gx_gl_attach`) renders the compiled scene with drag orbit, wheel zoom, `N`/`P` model cycle, `[`/`]` + `V` part isolation, `B` slot, `V`/`shift+V` variant, `Y` show-hidden, `L` lights, `T` textures, `W` wireframe, HUD (`H`), `F12` screenshot; `--frames N --hidden --shot F` is the non-interactive smoke path and its BMP matches `test_decomp_render` to RMSE 0.000 |
| GX HLE backend (S2) | `native/decomp/gx/gx_hle.c`: real GX state + `GXCallDisplayList` decode (68 lists, zero desync), XF/channel/texgen evaluation, per-draw snapshots; `gx_gl.c` evaluates up to 8 captured TEV stages with textures/TLUTs from `native/gx/texture.c` on an EGL/GLES3 pbuffer |
| Turn-stability (P-610) | Viewer static vs `--spin 360 --no-hud` RMSE 0.003/255 (Master Hand; residue is HSD lookat float rounding), frame1 vs frame240 and `--cycle 33` both RMSE 0.0 |
| Owner visual checks | 180 Hz viewer animation speed confirmed correct; face texture artifact gone (2026-09-11) |

## Known issues / gaps

Ordered by impact.

> Items 1–3 are prototype gaps superseded by the compiled engine (ADR-0010,
> S2/S4) and are parked. Item 4 (TEV) rolls into GX HLE; item 6 (character
> data) into compiled `ftData`; item 5 is the port plan itself. They stay
> listed because the prototype remains the fallback oracle until parity lands.

1. **Animation fidelity gaps.** Clips play and skin correctly, but
   `SETBYTE`/`SETFLOAT` channels (expressions, blinking, `ftParts_80074B0C`),
   IK joint resolution (`resolveIKJoint1/2`), material animation
   (`matanim`), shape sets and animation blending are not ported. Playback
   rate is fixed at the engine default 1.0 instead of per-action
   `frame_speed_mul`. See P-207..P-210.
2. **Animated expressions not implemented.** The neutral pose is correct, but
   blinking/damage expressions need the `SETBYTE` callbacks from item 1.
3. **Game & Watch residual slivers.** After honouring hidden joints he is
   recognisable, but a few thin edge-on pieces remain (x=0, y 13.6..21.9) that
   in-game are hidden through animation/joint state the port does not evaluate
   yet. P-201/P-412.
4. **TEV partially ported.** The common `MObjMakeTExp`/`TObjMakeTExp` path is
   in (material/RAS initial stage, colormap/alphamap, `RENDER_DIFFUSE`,
   specular phase with specular-lightmap textures, alpha-test/blend/Z,
   TEX0+TEX1), but the actual **light values** are still the viewer's
   stand-in set: in-game they come from `HSD_LObj` objects created by stage
   code (`src/melee/gr/*`), so `lobj.c` + stage light lists are the next step.
   `HSD_TObjTev` active overrides and toon textures are unhandled (inactive /
   stage-only in the tested fighter archives). See
   `learnings/hsd_tev_materials.md`. Still P-204. **S2 update:** the compiled
   path now evaluates the captured GX TEV state generically (up to 8 stages,
   swap tables, konst, alpha test) in `native/decomp/gx/gx_gl.c`; the
   remaining TEV gaps are the specular channel approximation and fog (see
   `learnings/decomp_s2_gx_hle.md`).
5. **No audio, menus, items, stages, results, netplay, WASM.**
6. **Non-Mario physics values** are demo defaults, not per-character data.
7. **Windows/macOS untested.** Linux + Mesa is the only verified target.

## Baseline commands

```sh
cmake -S native -B build/native -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build/native -j4
./build/native/melee --inspect
./build/native/melee --list-models
./build/native/melee --list-clips
SDL_VIDEODRIVER=offscreen ./build/native/melee --view --frames 3 \
    --screenshot /tmp/viewer.bmp
SDL_VIDEODRIVER=offscreen ./build/native/melee --view --animate \
    --clip Wait1 --anim-frame 25 --frames 1 --screenshot /tmp/anim.bmp
SDL_VIDEODRIVER=offscreen ./build/native/melee --scripted --frames 240 \
    --screenshot /tmp/baseline.bmp
./build/native/melee_decomp_boot --boot-frames 10 --boot-timeout 30 \
    --boot-log /tmp/boot.log
SDL_VIDEODRIVER=offscreen ./build/native/melee --view --frames 1 --no-grid \
    --screenshot /tmp/viewer.bmp
./build/native/test_decomp_render --width 1280 --height 800 \
    --shot /tmp/compiled.bmp --dump
./build/native/test_decomp_render --no-gl   # asset bridge + GX capture only
./build/native/melee_decomp_viewer          # interactive (needs lib32-sdl3)
./build/native/melee_decomp_viewer --frames 1 --hidden --shot /tmp/v.bmp
```

Expected `--inspect` tail:

```
Decoded PlMrNr.dat: 6328 triangles, 32 textures; bounds [-8.31 -0.31 -2.97] to [8.32 15.63 3.94]
```

If those numbers move, say why in the commit and update this file.

## Environment assumptions

- Linux, GCC/Clang, CMake, pkg-config, SDL2 dev, Mesa (`libEGL_mesa`,
  `libGL`), OpenGL math. The renderer needs a GL 3.3 core driver.
- Compiled targets (32-bit): `lib32-gcc-libs`, `lib32-libglvnd`,
  `lib32-mesa` (EGL/GLESv2) and, for the viewer, `sdl3` headers +
  `lib32-sdl3` (ADR-0014). Exact Arch package list in `TESTING.md`.
- Disc image at `iso/Super Smash Bros. Melee (USA) (En,Ja) (Rev 2).ciso` for
  default runs. `iso/` is locally excluded from git.
- `ACGC-PC-Port/` is a local, untracked reference checkout.
- The offscreen SDL driver + Mesa `radeonsi`/llvmpipe renders correctly (Mesa
  26.1.6 reports a 4.6 core context); there is no X11 server available to
  agents on this machine.
