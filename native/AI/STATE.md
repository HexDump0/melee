# State of the port

Last updated: 2026-09-12 (P-629/P-627: Link pose + NaN transform slowdown fixed; 32-bit FP uses SSE2)

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
camera orbits stop losing geometry (G-057).  **P-608 (2026-09-12):**
direct-mode draws (`GXBegin` + the inline `GXPosition*`/`GXColor*`/
`GXTexCoord*` writers used by `displayfunc.c`, `pobj.c`, `psdisp.c`, the
shadow/afterimage/effect code) are captured and decoded through the same
vertex path as display lists via the shadowing
`native/decomp/shim/dolphin/gx/GXVert.h`; `ctest decomp_gx_direct` is the
regression.  **P-612 (partial):** `GXSetScissor` (scaled from 640x480 EFB
pixels), `GXSetDstAlpha`, texture LOD bias/min-max LOD/anisotropy, and the
missing `HSD_VIData` render-mode init the scissor exposed (G-061) are in;
indirect/bump/toon and NBT binormal/tangent remain.  Next milestone:
**S3** (host-endian asset pipeline + DVD/ARQ).

**S3 passed (2026-09-12):** the platform now hosts the real disc/audio data
path and a generic host-endian converter.  `native/platform/dvd.c` mounts the
user's image, loads the FST into the GC boot-info page and runs the
decompilation's own `extern/dolphin` DVDFS; `native/platform/ar.c` plus the
compiled `arq.c` provide 16 MB of host ARAM with console offset semantics.
DVD/ARQ completions are delivered through `native/platform/complete.[ch]` when
interrupts are restored (the console's interrupt ordering, which DevCom/ARQ
depend on).  `native/decomp/assets/hsd_convert.c` converts archives in place:
the header/tables, every relocation target (authoritative pointer list), and
the numeric fields of joints, DObj/MObj/PObj/TObj, textures, animation trees,
RObs, FigaTrees and scene data/CObj/light/fog, with a content-hash + version
disk cache and a reloc-coverage desync check.  The boot passes the sound-bank
wait (real `.ssm` load over DVD/DevCom/ARQ), loads `NtMsgWin.dat`/`SdMsgBox.usd`
through the compiled loaders and reaches the memory-card/pad wait.
`ctest` is 7/7 including the new `decomp_assets` sweep: all 33 `Pl*Nr.dat`
plus `GrNBa`/`MnSlChr`/`IfAll`/`NtMsgWin` load through the compiled HSD path
with full relocation coverage.  ASan/UBSan is clean (the boot's DVD-cancel
stack-write bug and the `.ssm` overlapping copy were fixed; see G-063/G-064).

**S4 passed (2026-09-12):** the compiled game runs a deterministic headless
match.  `melee_decomp_boot --boot-match N` enters `GM_DEBUG_VS` (the game's own
Link/Mario match on Final Destination), and the harness sets stocks, installs
a frame-indexed scripted PAD source (`native/platform/pad_card.c`) and logs
player positions/flags; two runs at 600 frames are byte-identical and a
release vs ASan run reports the same position.  The compiled `ft`/`it`/`gr`/`gm`
code does the rest (engine physics, stage collision and lights, camera, HUD
stocks/nametags, respawns).  Converter v56 walks the remaining tables
(`grGroundParam`, `itemdata`, `map_plit`, `Stc_scemdls`/`_scene_models`,
`MapCollData.dynamic_*`, `FtPartsVis` model entries, `ftData` x24/x34/x38/x3C/
dynamics, `Ef*.dat` effect models, PlCo pData[16]/[20]); five `PORT_PC` patches
are listed in `learnings/decomp_port.md` (including two real host-only bugs:
the 0x10-byte card work area used as a 0x1510-byte `CardContext`, and a
use-after-free in the completion queue).  `ctest` is 12/12 including the new
600-frame `decomp_match` regression; ASan/UBSan 600-frame run is clean.  The
PAD backend and scripted input are the S4 input deliverable; the remaining
open items are P-616 (Falcon eyes), P-617 (indirect/toon) and P-621 (stage
colors), plus the boot/title scene re-entry noted in the handoff.

**P-623 (2026-09-12):** `melee_decomp_viewer --match` runs that match live:
the compiled `main()` runs inside the viewer process, a new VI present hook
(`boot_platform_set_present_hook`) renders the captured GX frame each game
frame, and the scripted PAD inputs loop (`pad_set_input_loop`) so the match
stays in action.  Without `--frames` it paces at 60 Hz and runs until
ESC/window close; `--frames N --shot F` captures a still; `--record FILE|-`
streams concatenated PPM (P6) frames for `ffmpeg -f image2pipe` (the 40 s
`/tmp/melee_match.mp4` was produced this way).  The S4 deterministic boot path
is unchanged (ctest 12/12, ASan 600-frame run byte-identical positions).
Follow-up fix: the "GO!" logo's black quad was the GX HLE asset table capping
at 8 archives, so later archives' CI textures hit a 64 KB fallback bound and
failed to decode; the table now grows on demand (G-088) and the logo renders.
A second fix bounds every texture decode by the GX-declared dimensions instead
of the containing archive (G-089), which removed the title screen's decode
spam and black logo rectangles.  Audio is S5.  See `gotchas/GOTCHAS.md` G-087
(viewer triage frame budget).

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
| GX HLE backend (S2) | `native/decomp/gx/gx_hle.c`: real GX state + `GXCallDisplayList` decode (68 lists, zero desync), XF/channel/texgen evaluation (hardware specular attenuation), per-draw snapshots; `gx_gl.c` evaluates up to 8 captured TEV stages with full KONST selects and textures/TLUTs from `native/gx/texture.c` on an EGL/GLES3 pbuffer, honours scissor/dst-alpha and applies per-TObj LOD bias/min-max LOD/anisotropy |
| Direct-mode capture (P-608) | `native/decomp/shim/dolphin/gx/GXVert.h` routes the decomp's inline FIFO writers to `GXPortWGFifo*`; `GXBegin` + 4-vertex quad through the shim decodes to exactly 2 triangles / 6 vertices with the source pos/color/UV in `ctest decomp_gx_direct` |
| Turn-stability (P-610) | Viewer static vs `--spin 360 --no-hud` RMSE 0.003/255 (Master Hand; residue is HSD lookat float rounding), frame1 vs frame240 and `--cycle 33` both RMSE 0.0 |
| Viewer model switch (G-071) | Switching models after frames have rendered keeps the compiled GX lighting: switched vs direct-load screenshots are pixel-identical (Mario->Mewtwo, Falcon, Kirby, Giga Koopa, Luigi pairs); the old alpha/colour channel-slot aliasing is covered by the channel block in `ctest decomp_gx_direct` |
| Hand translucency (G-072) | With lights on, Master Hand/Crazy Hand show their translucent wrist-forearm connector (raster alpha now comes from the paired `GX_ALPHA0/1` channel instead of 0); Mario/Kirby/Giga Koopa/Link screenshots byte-identical, `v->ras[3]` covered in `ctest decomp_gx_direct` |
| Disc/ARAM backends (S3) | `melee_decomp_boot` mounts the disc (FST 1212 entries), loads `audio/us/main.ssm` and the other boot banks through the compiled DVDFS + DevCom + ARQ, and passes `HSD_SynthSFXWaitForLoadCompletion`; the stop is now the memory-card/pad wait (`gm_Scene_MemCard_OnFrame`, input is S4) |
| Host-endian converter (S3) | `native/decomp/assets/hsd_convert.c`; every archive's relocation targets convert (`reloc_valid == reloc_total`), content-hash + version disk cache, `MELEE_ASSET_CACHE`/`MELEE_NO_ASSET_CACHE` controls |
| Asset sweep (S3) | `test_decomp_assets` (ctest `decomp_assets`): 33/33 `Pl*Nr.dat` + `GrNBa.dat` + `MnSlChr.dat` + `IfAll.dat` + `NtMsgWin.dat` parse, pose and convert with 0 desyncs |
| Common scene assets (S3) | `NtMsgWin.dat` `SceneDesc` camera/light/fog + CObj descriptors convert; the boot loads the scene camera and only then waits for pad input |
| Stage viewer (P-619) | `Gr*.dat` `map_head` converts (`UnkStageDat`/`UnkStageDat_x8_t`: per-map joint tree, camera, lights, fog, light anims, shape sets; converter v9); `hsd_scene_load_stage_all` loads every map as a Ground GObj (platform + background layers) like the game, `hsd_scene_load_stage` isolates one; `melee_decomp_viewer --stage GrNBa.dat --fighter PlMrNr.dat` renders the full stage with a posed fighter and `--stage-cam` through the stage's own camera; `M` model/stage, `N`/`P` cycle, `,`/`.` map, `F` fighter, `K` camera; all 71 `Gr*.dat` load headless and a 70-stage cycle runs clean; ctest `decomp_stage`/`decomp_stage_cam` |
| Sanitizers (S3) | 32-bit ASan/UBSan: `test_decomp_assets` PASS and `melee_decomp_boot` clean with 0 sanitizer errors; fixed a DVD-cancel write into a dead stack frame (G-063) and the `.ssm` overlapping copy (PORT_PC memmove, G-064) |
| EFB capture + Z-texture (P-615) | `GXCopyTex` reads the EFB back at its point in the command stream and re-encodes RGB565/RGBA8 tiled memory; `GXSetZTexture(REPLACE/ADD)` runs a dedicated depth-only program (`gl_FragDepth` is ignored inside the big TEV shader on Mesa, G-068).  Direct-mode quads now flush at every draw-affecting setter (G-069).  ctest `decomp_efb` |
| Bump texgen (P-612) | `GX_VA_NBT` keeps binormal/tangent; `GX_TG_BUMP0..7` implements the hardware emboss formula; Giga Koopa renders correctly (green/orange, previously magenta).  Indirect state is captured (P-617 evaluates it in S4).  `--direct` covers both |
| Owner visual checks | 180 Hz viewer animation speed confirmed correct; face texture artifact gone (2026-09-11) |
| Live match viewer (P-623) | `melee_decomp_viewer --match` runs the compiled game in-process (Link vs Mario, Final Destination): Ready countdown, both fighters walking/jumping, KO + `SCORE -1`, camera pan/zoom, respawn platforms; looping PAD script at 60 Hz until ESC; `--record -` piped to ffmpeg produces a 40 s H.264 of the same run |
| Match fighters visible (P-625) | The compiled fighters render fully textured in `--match`: `fighter.c`'s `x21FC_flag.u8 = 1` sets the MWCC `b7` bit only via the `FtStatusFlags` PORT_PC union (G-091), and the GL texture cache evicts LRU instead of returning black when full (G-092).  `--dump-draws FRAME` lists a captured frame's draws/textures/NDC bounds |
| Fighter animations loop (P-626) | Walk/run cycles wrap instead of freezing at the clip end: `conv_waitanim_flags` now bit-reverses the top byte of `x10_animCurrFlags` into the low byte, so `x594_b1_loop` reads the console bit and `ftAnim_8006EBE8` sets `AOBJ_LOOP` (converter v57, G-093) |
| GPU channel evaluation (P-628) | The GX channel/specular lighting now runs in the GL vertex shader (uniforms for 4 channels + 8 lights) instead of per-vertex C: `ctest decomp_render`/`decomp_gx_direct` pass, match-frame RMSE <= 3.4/255 vs the CPU path, spikes 8.5/s -> 3.4/s and worst frame 68 ms -> 26 ms |
| Match pacing (P-626) | Interactive `--match` no longer fights vsync (it skips the manual 60 Hz delay when the swap already blocked) and re-anchors instead of burst-catching-up after a slow frame; `[match] frame N draws=... render=Xms` reports the per-frame render cost |
| Host FP fidelity (P-629/P-627) | `native/decomp/shim/placeholder.h` corrects the upstream host fallback from `sqrt(x)` to reciprocal square root for `__frsqrte`; every 32-bit decomp target uses SSE2 scalar FP so `float`/`double` expressions do not retain x87 80-bit intermediates. Link's KO/respawn skeleton stays finite, frame 720 is visually coherent, and sampled game work from frames 600–1200 is 1.4–5.8 ms instead of the old sustained ~29 ms plateau |

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
4. **TEV mostly ported.** The compiled path evaluates the captured GX TEV
   state generically (up to 8 stages, swap tables, full KONST selects,
   alpha test, scissor/dst-alpha and per-TObj LOD), the channel-1 specular
   uses the hardware attenuation function, `GX_TG_BUMPn` emboss is faithful
   (Giga Koopa fixed) and the Z-texture/EFB effects are in (P-615).  The
   remaining gaps are the **indirect-texture shader evaluation** (state is
   captured; only stage refraction uses it) and **toon ramps** — both filed
   as P-617 and only verifiable once S4 renders stages.  Stage light lists
   (`src/melee/gr/*`) still supersede the viewer's stand-in lights at S4.
   See `learnings/hsd_tev_materials.md` and `decomp_s2_gx_hle.md`.
5. **No audio, menus, items, stages, results, netplay, WASM.** Audio is S5;
   the S3 data path (DVD/DevCom/ARQ, `.ssm` header/record conversion) is in
   place but `AXDriver_*`/AX are still stubs.
6. **Captain Falcon's eyes do not render** in the compiled path (P-616); the
   rest of the head now matches the prototype.  See TASKS.md.
7. **Non-Mario physics values** are demo defaults, not per-character data.
8. **Debug title freezes on the logo reveal card.**  `--match` reaches
   `GM_TITLE` with `gm_804D67EC == 0`, so the logo stays at animation frame 0
   and its opaque grey reveal card is visible; the retail title starts the
   logo at frame 400.  S6/P-624; full analysis in G-090.
9. **Windows/macOS untested.** Linux + Mesa is the only verified target.

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
./build/native/melee_decomp_boot --boot-frames 10 --boot-timeout 10 \
    --boot-log /tmp/boot.log      # hangs in the memory-card/pad wait; the
                                 # watchdog gives a controlled SIGALRM stop
./build/native/test_decomp_assets   # 33 Pl*Nr + stage + common assets
SDL_VIDEODRIVER=offscreen ./build/native/melee --view --frames 1 --no-grid \
    --screenshot /tmp/viewer.bmp
./build/native/test_decomp_render --width 1280 --height 800 \
    --shot /tmp/compiled.bmp --dump
./build/native/test_decomp_render --direct   # direct-mode capture (P-608)
./build/native/test_decomp_render --no-gl   # asset bridge + GX capture only
./build/native/melee_decomp_viewer          # interactive (needs lib32-sdl3)
./build/native/melee_decomp_viewer --frames 1 --hidden --shot /tmp/v.bmp
./build/native/melee_decomp_viewer --match  # live match, 60 Hz, ESC quits
./build/native/melee_decomp_viewer --match --frames 2400 --record - 2>/dev/null \
    | ffmpeg -y -f image2pipe -framerate 60 -i - -c:v libx264 -crf 21 \
      -pix_fmt yuv420p /tmp/melee_match.mp4
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
