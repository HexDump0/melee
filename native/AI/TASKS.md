# Tasks and claims

## Claim protocol

1. Pick a task below whose status is `open`.
2. Edit its row: `status` -> `claimed`, add your agent name and the date.
3. Add your files to "Files" so others know what is read-only.
4. Work, verify per `TESTING.md`, write a handoff if you stop.
5. On completion: `status` -> `done`, add the commit hash, and move the row to
   the Completed section at the bottom (keep history).

Do not claim more than one task. If a task is too big, split it into subtasks
and claim one.

Status values: `open`, `claimed`, `blocked`, `review`, `done`, `parked`
(`parked` = superseded by ADR-0010; do not resume, kept for history).

## Workstreams

- **P-1xx** core plumbing (build, platform, assets)
- **P-2xx** rendering and animation
- **P-3xx** engine integration (use decompiled code)
- **P-4xx** tooling, testing, docs
- **P-5xx** future targets (audio, WASM, netplay)
- **P-6xx** decompiled-port bring-up (ADR-0010; milestones S0..S7)

## Open tasks

| ID | Task | Status | Agent | Files | Notes / acceptance |
|---|---|---|---|---|---|
| P-619 | Stage viewer: wire `Gr*.dat` + a posed fighter into `melee_decomp_viewer` (stage list, camera/lights, `--stage`, HUD/keys) | done | opencode (deepseek-v4.1-flash), 2026-09-12 | `native/decomp/render/`, `native/decomp/hsd/hsd_scene.{c,h}`, `native/decomp/assets/hsd_convert.c` | Done (see Completed). |
| P-620 | S4: first match — compiled `ft`/`it`/`gr`/`gm` two fighters on a stage, deterministic headless | claimed | opencode (deepseek-v4.1-flash), 2026-09-12 | `native/platform/`, `native/decomp/` | Exit per ROADMAP: deterministic scripted match headless, numeric comparisons, 600-frame ASan clean. |
| P-621 | Verify stage material/lighting colors against the real game (P-619 renders Battlefield geometry; surface tones still look off) | open | — | `native/decomp/gx/gx_gl.c`, `native/decomp/assets/hsd_convert.c` | Repro: `./build/native/test_decomp_render --stage GrNBa.dat --fighter PlMrNr.dat --stage-cam --shot /tmp/stage.bmp`. Compare against a Dolphin capture of Battlefield; check the stage MObj/TObj TEV template and the map light list (`lb_80011AC4`). |
| P-616 | Captain Falcon (`PlCaNr.dat`) eyes do not render | open | — | `native/decomp/gx/`, `native/decomp/assets/hsd_convert.c` | Confirmed against the prototype 2026-09-12: the prototype shows white eyes under the visor, the compiled path shows a dark band. Ruled out: visibility (`MELEE_NO_VIS`), culling (`--no-cull`), alpha test (`--no-alpha-test`), and the TEV KONST tables (P-614 was correct; a reg/comp "fix" was a no-op and reverted). Narrowed to **batch 96 / dobj 77** (`PlCaNr.dat`, the only 2-TObj material: TEX0=tex49 face, TEX1=tex51 eye overlay), which renders via the standard 4-stage template (stage2 map=1 coord=1, `cin=C2,TEXC,KONST,ZERO`, `kc_sel=0x1D`=K1_A=1.0) that bodies use successfully for specular maps. The overlay geometry is drawn (visible in wireframe) but its result is black. Next: trace the stage-2 texture sample for that draw (bind the same texture via a TEXMAP_NULL variant, or dump `C2`/`CPREV`); compare with a Dolphin capture. Evidence: handoff 2026-09-12-P-615-P-612 and `logs/`. |
| P-617 | Indirect-texture shader evaluation + toon ramp evaluation (GX HLE) | open | — | `native/decomp/gx/gx_gl.c` | **S4-only.** `GXSetTevIndirect`/`GXSetIndTex*` state is captured per draw (P-612); the GLES fragment path does not yet apply the indirect offsets. Only `lb/lbrefract.c` (stage refraction) uses it, so it cannot be validated until S4 runs a stage. Toon (`GX_TG_SRTG`) currently passes the source value through; stage-only content. |
| P-601 | Full-tree GCC compile census + shim hardening (S0) | done | opencode (deepseek-flash), 2026-09-11 | `native/decomp/shim/`, `native/AI/learnings/decomp_port.md` | Done: 1021/1034 `src/*.c` compile; shims for `ssize_t`/`intptr_t`, GameCube `STATIC_ASSERT`, and `bool`=`int` callbacks. See Completed. |
| P-602 | Probe: decomp `HSD_ArchiveParse` on a real `PlMrNr.dat` (S0a) | done | opencode (deepseek-flash), 2026-09-11 | `native/decomp/`, `native/CMakeLists.txt`, `native/tests/` | Done: 2/2 public symbols and offsets match the hand parser. See Completed. |
| P-603 | Probe: decomp `HSD_JObjLoadJoint` bind-pose parity (S0b) | done | opencode (deepseek-flash), 2026-09-11 | `native/decomp/`, `native/tests/`, `native/AI/learnings/decomp_port.md` | Done: 61/61 joints loaded and world matrices bitwise-equal to the hand pose math. **S0 gate passed.** See Completed. |
| P-108 | Per-part isolation for the viewer (`HsdBatch`, batch lists) | done | follow-up | `native/hsd/model.*`, `native/main.c` | Added with the viewer; see Completed. |
| P-207 | Expression/part visibility events: port the per-kind `ftData_UnkIntBoolFunc0.model_events` path (`ftParts_80074B0C`/`ftParts_80074A4C`) | parked | — | `native/hsd/model.c`, `native/main.c` | **PARKED by ADR-0010** — compiled `ftparts.c` action code (S4) implements this; do not start. Historical detail: `SETBYTE`/`SETFLOAT` FObj channels have no callback registration in the decomp (`jobj.c` `ufc_callbacks` is a dead list), so expressions come from action code. The Bowser `Wait1` note in `learnings/hsd_animation.md` §7 is unresolved but off the critical path. |
| P-208 | Resolve IK joints (`resolveIKJoint1/2`, `JOBJ_JOINT`/`EFFECTOR`) during pose evaluation | parked | — | `native/hsd/model.c` | **PARKED by ADR-0010** — compiled `jobj.c` implements this in S2/S4. Foot/hand planting comes from the real engine. |
| P-209 | Material animation (`HSD_MatAnimJoint`) from `Pl*Nr.dat` / AJ clips | parked | — | `native/hsd/model.c` | **PARKED by ADR-0010** — compiled `tobj.c`/`mobj.c`/`ftAnim_80070200` implement this in S2/S3. |
| P-210 | Per-action animation rate (`frame_speed_mul`) instead of fixed 1.0 | parked | — | `native/main.c` | **PARKED by ADR-0010** — the compiled `ft` action code carries `frame_speed_mul`; the sandbox rate stays fixed until S4. |
| P-212 | Visual render interpolation for high-refresh displays (fixed 60 Hz sim) | open | — | `native/gx/render.c` | **Post-S4 (ADR-0010):** re-scope against the compiled simulation before starting; the hand renderer path may not survive S2. Deferred by the owner until the faithful 60 Hz port is complete; this is presentation only and must not touch simulation. Plan: keep previous/current pose snapshots, mix position/normal in the model vertex shader with a `u_interp` uniform, one tick of display latency, `--no-interp` for faithful mode. Requires an ADR first; `--scripted` must disable it so screenshots stay deterministic. |
| P-204 | TEV pass: material/RAS stage, colormap/alphamap, alpha test, XLU blend, TEX0+TEX1 | parked | — | `native/gx/render.c`, `native/hsd/model.c`, `native/hsd/model.h`, `native/hsd/parts.c` | **PARKED by ADR-0010** (claim released). The landed derivation/rendering is the GX HLE backend seed (S2); the remaining items are superseded by compiled `tobj`/stage code (S3/S4). Historical: `--dump-tev`, GX channel lighting, `HSD_SetupPEMode`, `TObjMakeTExp`, TEX0+TEX1, lightmap phases, `model_scaling` all landed. |
| P-205 | Per-TObj texture matrices (scale/translate/rotate) | parked | — | `native/hsd/model.c` | **PARKED by ADR-0010** — compiled `tobj.c` carries the texture matrices in S2. Note: the prototype already has `MakeTextureMtx` support (see `learnings/gx_textures.md`). |
| P-206 | Camera polish: zoom-to-fit both fighters, stage bounds, ledge visibility | parked | — | `native/main.c` | **PARKED by ADR-0010** — viewer-only polish; the compiled camera (S4) and S2 rendering supersede it. Viewer stays as a dev tool as-is. |
| P-302 | Replace `extras/physics.c` with real `ftCommon_*` formulas | parked | — | `native/extras/physics.c` | **PARKED by ADR-0010** — superseded by the compiled `ft` code (S4). The `Fighter` struct question is resolved: compile it, no reduced port-side struct. |
| P-401 | CI task: run `--inspect` + scripted frames in GitHub Actions with a dummy disc | open | — | `.github/`, `native/` | Still useful for the prototype, and should grow to cover the decompiled-port build once S1 links. Needs an asset-free path: checked-in tiny synthetic HSD fixture generated by a script, not game data. |
| P-402 | Fuzz the HSD/disc parsers with a mutation harness | open | — | `native/` | Still useful: the hand parser becomes the asset-pipeline oracle, so any crash is a bug. Keep fixtures in `native/AI/logs/` (small, synthetic only). |
| P-412 | Game & Watch residual pieces: identify why the collinear flat parts are not hidden (animation joint state vs vis tables) | parked | — | `native/hsd/model.c` | **PARKED by ADR-0010** — compiled joint/vis code (S2/S4) decides this; re-check against the compiled render before any investigation. |
| P-411 | Generic per-character attributes (`ftData<Char>`), not just Mario | parked | — | `native/game/attributes.c` | **PARKED by ADR-0010** — compiled `ftData` (S4) replaces the hand attribute path; `learnings/fighter_data.md` stays as the format reference. |
| P-502 | WASM feasibility memo | open | — | `native/AI/DECISIONS.md` | **S7 groundwork.** Emscripten + SDL2 + WebGL2 (ES3 shaders already portable). Identify blockers: disc-image size/delivery, threading, audio, 60 Hz pacing. |

## P-201 result

Done in `0e1a974d2` (backend) and `6665bd5f2` (viewer/sandbox). Clips come from
`Pl<Char>AJ.dat` FigaTree archives; `hsd/aobj.c` is a literal port of the
`fobj.c` player and `hsd/anim.c` binds nodes to joints exactly like
`ftAnim_8006F4C8`. Read `learnings/hsd_animation.md` before touching it. The
remaining fidelity items (P-207..P-210) are parked by ADR-0010; the compiled
`fobj`/`jobj` path replaces this in S2/S4.

## S1 backend work list (P-604 triage)

The S1 boot log (`native/AI/logs/2026-09-11-S1-boot-triage.md`, analysis in
`learnings/decomp_boot.md`) reaches a controlled stop while the compiled game
waits for its first sound-bank load. Ordered by what unblocks the boot:

1. **S2 — GX + VI HLE** (DONE 2026-09-12, P-606). The GX command surface is
   real in `native/decomp/gx/gx_hle.c`; the boot log now shows 94 stub calls /
   33 unique (GX no longer triaged). S2 follow-ups P-607/P-608/P-610 are done;
   P-613 (faithful specular), P-614 (TEV KONST), P-612 (bump emboss +
   indirect state capture) and P-615 (Z-texture/EFB copy-read) landed;
   P-617 carries the S4-only indirect/toon shader evaluation, and P-616
   (Captain Falcon eyes) is an open render bug.
2. **S3 — DVD + HSD DevCom/ARQ**. `DVDConvertPathToEntrynum`/open/read and
   synchronous `ARQPostRequest` callbacks so `HSD_DevComRequest` can finish
   asset loads. Seed: `native/platform/disc.c`.
3. **S5 — AX/DSP HLE**. `AXRegisterCallback` must drive
   `HSD_SynthCallback` on the 5 ms/200 Hz audio frame for voice/mix state and
   the AXDriver command clock (ADR-0013). The boot bank wait itself is
   unblocked by S3's DVD/ARQ callbacks, not by AX.
4. **S6 — CARD/EXI + fonts**. Card command pump (`hsd_803AAA48`) and a font
   source replacing the generated atlases.
5. **S4 — alarms/threads** (`OSCreateAlarm`/`OSSetPeriodicAlarm` are stubs;
   the boot installs a periodic alarm during init).

## Blocked / needs a human

| ID | Question | Requested from |
|---|---|---|
| H-1 | Does the interactive window/controller feel correct on real hardware? | project owner |

Resolved human checks (owner, 2026-09-11): **H-4** — 180 Hz viewer
animation speed confirmed "perfect, looks awesome". **H-2** — the face
texture artifact is gone; no regression. **H-3** — priority question answered
by ADR-0010: the S0..S7 port bring-up sequence is the plan. **H-5** (Bowser
`Wait1` capture) is parked with the hand viewer; re-evaluate against the
compiled render in S2/S4 instead.

## Completed

| ID | Task | Agent | Commit | Date |
|---|---|---|---|---|
| P-619 | Stage viewer: `Gr*.dat` `map_head` conversion (v9: maps, light anims, shape sets) + all-map Ground GObj layout + stage camera/lights/fog + posed fighter; `--stage/--fighter/--stage-map/--stage-cam`, `M`/`F`/`K`/`,`/`.`, HUD; ctest `decomp_stage`/`decomp_stage_cam` | opencode (deepseek-v4.1-flash) | 49fad9947 | 2026-09-12 |

| ID | Task | Agent | Commit | Date |
|---|---|---|---|---|
| P-618 | GX HLE channel state: four slots so `GX_ALPHA0/1` no longer clobbers COLOR0/1 (model switches keep their lighting), and lit raster alpha evaluated from the paired alpha channel (hand wrist connector stays translucent). G-071/G-072 | opencode (deepseek-flash) | f8a8b48d2 | 2026-09-12 |
| P-609 | S3: host-endian asset pipeline + DVD/ARQ completion | opencode (deepseek-flash) | d4fc2f9b3, ec408b535, 297921685, 37b718104 | 2026-09-12 |
| P-615 | Z-texture and EFB copy/read (`GXSetZTexture`, `GXCopyTex`, `GXCopyDisp`) | opencode (deepseek-flash) | 2b2fd837a, 697d90d9c | 2026-09-12 |
| P-612 | GX HLE polish: faithful `GX_TG_BUMPn` emboss + indirect state capture | opencode (deepseek-flash) | 8f5396cac, 697d90d9c | 2026-09-12 |
| P-614 | TEV KONST parity: full KCSEL/KASEL select tables (scalar fractions, K0..K3, per-channel K?_R/G/B/A) in the fragment shader | opencode (deepseek-flash) | 7139e2762 | 2026-09-12 |
| P-613 | Faithful GX specular: hardware attenuation function `dot(a,(1,t,t^2))/dot(k,(1,t,t^2))` with H from the spec light object, replacing Blinn-Phong | opencode (deepseek-flash) | 7139e2762 | 2026-09-12 |
| P-608 | Direct-mode GX capture: `native/decomp/shim/dolphin/gx/GXVert.h` shadows the SDK header and routes the inline `GXPosition*`/`GXColor*`/`GXTexCoord*` writers to `GXPortWGFifo*`; `GXBegin` starts a draw snapshot and the big-endian capture is decoded by the display-list path at the next command/frame boundary. Regression `ctest decomp_gx_direct` (`test_decomp_render --direct`) | opencode (deepseek-flash) | ed606344f | 2026-09-12 |
| P-610 | POBJ_SKIN shared-vertex gaps: Falcon silver fixed by the P-607 `out_reg` fix; Giga Koopa limb noise fixed by folding `GX_TG_TEXCOORDn` texgen chains onto the 0/1 UV varyings (G-058); Bowser's magenta prototype look confirmed parity (2-texture archive) | opencode (deepseek-flash) | 347bf81f6 | 2026-09-12 |
| P-607 | GX specular/material parity: boots brown, Luigi/Link correct — fixed by emulating TEV `out_reg` (register writes keep the previous-stage chain) and invalidating HSD's GX caches when the backend resets; overall prototype RMSE 10.57/255. The hardware specular polynomial is still approximated by the prototype's Blinn-Phong (documented) | opencode (deepseek-flash) | e773eff93 | 2026-09-12 |
| P-611 | Interactive compiled-path viewer: `melee_decomp_viewer` (SDL3 window + EGL/GLES3; orbit/zoom, N/P model cycle, slot/variant, texture/light toggles; `--frames/--hidden/--shot` smoke path) sharing `render_scene.c` with the headless test; SDL3 dependency in ADR-0014 + `TESTING.md` | opencode (deepseek-flash) | 983f1eaf3 | 2026-09-12 |
| P-501 | Audio backend design memo: ADR-0013 selects host-side AX HLE — compile the game's `src/sysdolphin/baselib/axdriver.c` plus the in-tree SDK AX voice layer, replace only `AXOut`/DSP with a 5 ms software mixer; contains rejected options, S5.1-S5.5 task plan and headless validation | opencode (deepseek-flash) | 801662d90 | 2026-09-12 |
| P-606 | S2: compiled HSD renders through the GX HLE (`native/decomp/gx/` + `hsd_scene.c` + `test_decomp_render`, ctest `decomp_render`; world bounds equal the prototype, screenshot RMSE 10.94/255, deviations in `learnings/decomp_s2_gx_hle.md`) | opencode (deepseek-flash) | e20356d97 | 2026-09-12 |
| P-605 | S3 prep: host-endian conversion spec per asset format (535-line spec; `learnings/decomp_assets.md`) | opencode (docs session) | 805adb250 | 2026-09-11 |
| P-403 | GX format census across 33 `Pl*Nr.dat` (967 textures; CMPR 883, CI8 47, RGBA8 12, I4 17, CI4 5) | opencode (docs session) | d8376e30e | 2026-09-11 |
| P-604 | S1: platform boot skeleton — compiled `main()` runs under OS/DVD/GX/VI stubs to a controlled triage stop (`melee_decomp_boot`, `decomp_boot` ctest, log + work list in `learnings/decomp_boot.md`) | opencode (deepseek-flash) | f527f7572 | 2026-09-11 |
| P-603 | S0b probe: compiled `HSD_JObjLoadJoint` bind-pose parity (61/61 joints bitwise, S0 gate passed) | opencode (deepseek-flash) | 62c87e117 | 2026-09-11 |
| P-602 | S0a probe: compiled `HSD_ArchiveParse` on real `PlMrNr.dat` (symbol/offset parity) | opencode (deepseek-flash) | 62c87e117 | 2026-09-11 |
| P-601 | Full-tree GCC compile census + shim hardening: 1021/1034 files compile; `ssize_t`/`intptr_t`, `STATIC_ASSERT`, `bool`=`int` shims | opencode (deepseek-flash) | 543ff20b7 | 2026-09-11 |
| P-301 | Compile pure HSD math behind a shim (SDK mtx/vec are asm; `HSD_MtxSRT` compiled, bitwise parity, hand copy deleted) | opencode (deepseek-flash) | `c903e5282` | 2026-09-11 |
| P-213 | Extract the viewer and sandbox out of `main.c` into `extras/viewer.c`, `extras/sandbox.c` | opencode (deepseek-flash) | `d6dc4fc49` | 2026-09-11 |
| P-211 | OpenGL 3.3 core + ES3-portable shaders; per-batch VAO/VBOs, no fixed function | opencode (deepseek-flash) | `4eb7c1f2d`, `b35dd102e` | 2026-09-10 |
| P-201 | HSD FigaTree animation playback, per-frame skinning, viewer + sandbox | opencode (deepseek-flash) | `0e1a974d2`, `6665bd5f2` | 2026-09-10 |
| P-101 | Disc reader + FST (`platform/disc.c`) | Codex | `f70d50cce` | 2026-09-09 |
| P-102 | HSD model decode + envelope bind pose | Codex + follow-up | `f70d50cce` | 2026-09-10 |
| P-103 | GX texture decode to RGBA8 | Codex | `f70d50cce` | 2026-09-09 |
| P-104 | Mario `ftCo_DatAttrs` from disc | Codex | `f70d50cce` | 2026-09-09 |
| P-105 | SDL/GL sandbox with physics and HUD | Codex | `f70d50cce` | 2026-09-09 |
| P-106 | Headless screenshot verification + ASan pass | follow-up | `f70d50cce` | 2026-09-10 |
| P-107 | Fix bind-pose bug (`PNMTXIDX/3`, rigid groups, direct-attr offset) | follow-up | `f70d50cce` | 2026-09-10 |
| P-108 | Interactive 3D viewer with orbit/zoom/toggles and model cycling | follow-up | _pending_ | 2026-09-10 |
| P-109 | Per-part isolation (`--part`, `--part-mode`, `--list-parts`, `[`/`]`/`V`) | follow-up | _pending_ | 2026-09-10 |
| P-110 | FST enumeration (`--list-models`, 33/273 archives) + fast CISO block ordinals | follow-up | _pending_ | 2026-09-10 |
| P-111 | `right` matrix for PObjs on non-root joints (Link sword/shield) | follow-up | _pending_ | 2026-09-10 |
| P-112 | Static part visibility from `ftData` (`hsd/parts.c`), neutral face | follow-up | _pending_ | 2026-09-10 |
| P-113 | TLUT + CI4/CI8 texture decode (Mario eye atlas) | follow-up | _pending_ | 2026-09-10 |
| P-114 | PObj SKIN/SHAPEANIM transforms + per-PObj culling + GX colour enum fix | follow-up | _pending_ | 2026-09-10 |
| P-115 | `JOBJ_HIDDEN`, material z-mode, `MakeTextureMtx` (mirrored logos), mipmapped LOD | follow-up | _pending_ | 2026-09-10 |
| P-116 | Visibility slot cycling (`B`, `--vis-slot`), part framing, `--extract`, `--zoom`, `--no-cull` | follow-up | _pending_ | 2026-09-10 |
| P-117 | Fix invisible player 2 (Visual copied before GL lists existed) | follow-up | `5ba65adde` | 2026-09-10 |
