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
| P-671 | Renderer parity kickoff: build the GX coverage matrix (every `GX*`/register Melee calls → implemented exactly / approximated / stubbed, with the Aurora reference symbol at the pinned commit), stand up the untracked `aurora-reference` checkout, and file the prioritized gap list | done | opencode (deepseek-v4.1-flash), 2026-09-13 | `native/AI/workflows/renderer_parity.md`, `native/decomp/gx/`, `native/AI/learnings/` | Done: `learnings/gx_coverage_matrix.md` committed as `b21f513e6`; gap list P-672..P-680 filed. Reference commit `749d6ee7a22bdfab78c8ece9047bca5d79aa72ca`. |
| P-672 | Indirect texturing + toon ramps in the GLES renderer (stage refraction, `GXSetTevIndirect`/`GXSetIndTex*`, `GX_TG_SRTG`, `GX_TG_MTX3x4`/normalize) | done | opencode (deepseek-v4.1-flash), 2026-09-13 | `native/decomp/gx/gx_gl.c`, `native/decomp/gx/gx_hle.c`, `native/tests/test_decomp_render.c` | Done (this commit): indirect fragment evaluation (Aurora `shader.cpp`/`GXBump.cpp`), `GXSetTevDirect` teardown, `GX_TG_MTX3x4` q-divide + normalize, SRTG lit-raster UVs; `ctest decomp_gx_direct`/`decomp_efb` pass 5, sensitivity flipped; learning `gx_indirect_toon.md`. |
| P-673 | Channel/lighting/specular parity: verify `GXSetChanCtrl`/`GXSetChanMatColor`/light evaluation against Aurora (`GXLighting.cpp`, `shader_info.cpp`), including attenuation and ambient/diffuse/specular combinations | done | opencode (deepseek-v4.1-flash), 2026-09-13 | `native/decomp/gx/gx_gl.c`, `native/decomp/gx/gx_hle.c`, `native/tests/test_decomp_render.c` | Done (this commit): SDK `GXInitLightDistAttn`/`GXInitLightSpot` math, tinted `GX_AF_SPEC` evaluation; `ctest decomp_gx_direct` light cases + `decomp_efb` pass 6, sensitivity flipped; learning `gx_lighting_specular.md`; spot cones → P-681. |
| P-674 | EFB copy/Z-texture parity: copy filters (`tex_copy_conv.cpp`), R4 shadow filter, Z-texture REPLACE/ADD edge cases; keep the existing `decomp_efb` behavior checks | open | — | `native/decomp/gx/gx_hle.c`, `native/decomp/gx/gx_gl.c` | ADR-0017, G-068/G-102..104. |
| P-675 | Texture/sampler parity: CI/TLUT bounds, LOD bias/min-max/anisotropy, mip generation, format edge cases against Aurora `lib/gfx/texture*.cpp` | open | — | `native/decomp/gx/gx_gl.c`, `native/gx/texture.c` | ADR-0017, G-088/G-089. Regression unit test per format/edge case. |
| P-676 | Match-path renderer performance parity: state-change batching, avoid redundant GL binds, decoded-display-list/direct-array costs; measure with `[match] ... render=Xms` | open | — | `native/decomp/gx/gx_hle.c`, `native/decomp/gx/gx_gl.c` | ADR-0017, replaces P-642. Correctness first; keep frame-718 pixel parity. |
| P-677 | Parity regression harness: broaden `test_decomp_render`/viewer checks across characters, stages, effects; per-slice before/after screenshots + draw-dump assertions; report template | open | — | `native/tests/`, `native/decomp/render/` | ADR-0017. Acceptance: one command per slice producing a pass/fail parity artifact. |
| P-678 | `GXGetProjectionv` layout: return the SDK packed form `{projType, A,B,C,D,E,F}` (SDK `GXTransform.c`/Aurora `GXGet.cpp`); the current 4x4-diagonal return breaks `psdisp.c:1936` particle billboard axes and `fog.c:56`; capture `GXSetProjectionv` too | done | opencode (deepseek-v4.1-flash), 2026-09-13 | `native/decomp/gx/gx_hle.c`, `native/tests/` | Done (this commit): packed coefficients stored by `GXSetProjection`, `GXSetProjectionv` implemented, `ctest decomp_gx_direct` asserts both layouts and fails on the old getter (G-131). |
| P-679 | Fog parity: hardware `GXSetFog` a/b/c coefficients and `1-exp2(-8f)`/`exp2(-8(1-f))` type formulas (Aurora `GXSet.cpp:GXSetFog`, `shader.cpp:1537`), plus `GXSetFogRangeAdj`/`GXInitFogAdjTable` (SDK `GXPixel.c`); note `HSD_FogDesc.fogadjdesc` is nulled by the converter (`hsd_convert.c:161`) so range adj needs a converter follow-up | open | — | `native/decomp/gx/gx_hle.c`, `native/decomp/gx/gx_gl.c` | P-671 matrix §7. Regression: synthetic depth ramp drawn with each fog type, coefficients asserted against a transcription. |
| P-680 | Line/point primitives: `exec_primitive` silently drops `GX_LINES`/`GX_LINESTRIP`/`GX_POINTS` (used by `lb_*` HUD/effects and `psdisp` particles), and `GXSetLineWidth`/`GXSetPointSize`/`GXEnableTexOffsets` are stubs | open | — | `native/decomp/gx/gx_hle.c`, `native/decomp/gx/gx_gl.c` | P-671 matrix §1; Aurora `pipeline.cpp` primitive map + `shader.cpp` line/point tex offsets. Regression: `test_decomp_render --direct` line/point fixture. |
| P-681 | Spot-light cones: `GX_AF_SPOT` with nonzero `a.y/a.z` needs the hardware cosine polynomial (`GXInitLightSpot` a/cutoff) instead of distance-only attenuation; only `GrZebesRoute` references `LOBJ_SPOT` | open | — | `native/decomp/gx/gx_gl.c` | P-673 learning `gx_lighting_specular.md`. Regression: lighting fixture with a cone light + screenshot of the Brinstar route stage. |
| P-663 | S8.0 spike: build Aurora 64-bit on the reference machine (Vulkan/Mesa), verify headless offscreen rendering (`GXCreateFrameBuffer` + screenshot), size Dawn/nod packaging, licenses, build time, and compare "port our tree" vs "rebase on jonrosner/melee-native" | parked | — | `native/AI/DECISIONS.md`, `native/AI/logs/`, `build/` (scratch) | ADR-0015. Acceptance: offscreen frame captured, sizing note in `logs/`; do not start S8.1+ before this gate. **Parked by ADR-0017 (renderer-parity program supersedes the Aurora dependency/migration).** |
| P-664 | S8.1: 64-bit compiled game target — compat header + generated SDK headers, portability scan (adapt `jonrosner/melee-native`'s `scan_portability.py`), fix LP64 pointer-width/bitfield/global-overlay classes; GC DOL stays byte-identical | parked | — | `native/CMakeLists.txt`, `native/decomp/shim/`, `native/decomp/boot/`, `patches/` | ADR-0015. Acceptance: full 64-bit link + headless boot to title; `ctest` (32-bit) stays green. **Parked by ADR-0017 (renderer-parity program supersedes the Aurora dependency/migration).** |
| P-665 | S8.2: schema asset materializer — port the MIT `asset_schema.c`/`archive_runtime.cpp`/`archive_bridge.c`/`stage_numeric_layouts.hpp` family by family (fighters → items → stages → menus/effects); converter stays the test oracle | parked | — | `native/decomp/assets/`, `native/tests/test_decomp_assets.c` | ADR-0015. Acceptance: every `Pl*`/`ItCo`/`Gr*`/menu archive materializes and loads like today; raw-vs-materialized tests per family. **Parked by ADR-0017 (renderer-parity program supersedes the Aurora dependency/migration).** |
| P-666 | S8.3: Aurora GX backend behind `native/decomp/shim/dolphin/gx` with `MELEE_GX_BACKEND=aurora|gles`; present via the existing hook; keep the EGL/GLES path for tests/web | parked | — | `native/decomp/gx/`, `native/decomp/shim/dolphin/gx/`, `native/CMakeLists.txt` | ADR-0015. Acceptance: `decomp_frontend`/`decomp_match`/`decomp_hit`/`decomp_icons` pass on Vulkan; screenshot delta logged. **Parked by ADR-0017 (renderer-parity program supersedes the Aurora dependency/migration).** |
| P-667 | S8.4: desktop platform adapters — Aurora core/VI/app + PAD bridge; keep our DVD/CISO+ARQ, AX/audio and CARD; decide each API's owner explicitly | parked | — | `native/platform/`, `native/audio/` | ADR-0015. Acceptance: interactive Linux run, deterministic tests unchanged. **Parked by ADR-0017 (renderer-parity program supersedes the Aurora dependency/migration).** |
| P-668 | S8.5: web path — spike Aurora + `emdawnwebgpu` on wasm32 (browser WebGPU, Emscripten 4.0.10+) behind the GX shim; keep wasm32+GLES green throughout and use it as the fallback until the spike renders a real Melee frame | parked | — | `native/`, `native/AI/ROADMAP_DETAILS.md` | ADR-0015/0016, P-502. Acceptance: either an Aurora wasm build rendering the frontend flow, or a written no-go with the blockers and the GLES fallback kept. **Parked by ADR-0017 (renderer-parity program supersedes the Aurora dependency/migration).** |
| P-670 | S8.5b: mobile targets — Android (Aurora Vulkan + SDL3) then iOS/tvOS (Metal): window/events/PAD via Aurora, our OS/DVD/ARQ/AX/CARD behind it, disc-image access via SAF/document picker, mobile audio route | parked | — | `native/platform/`, `native/`, `native/CMakeLists.txt` | ADR-0016. Acceptance: Android build boots to title on device/emulator with the user's image; no bundled assets. **Parked by ADR-0017 (renderer-parity program supersedes the Aurora dependency/migration).** |
| P-669 | S8.6: rebaseline and CI — backend-agnostic render tests (retire/replace `decomp_gx_direct`/`decomp_efb` command-level checks), pin Aurora, licenses, perf numbers, docs; revisit submodule+patches vs fork | parked | — | `.github/`, `native/tests/`, `native/AI/` | ADR-0015. Acceptance: dual-backend CI green. **Parked by ADR-0017 (renderer-parity program supersedes the Aurora dependency/migration).** |
| P-662 | Audit the remaining per-stage `yakumono_param` structs (GrCn/GrIz/GrKg/GrSt/GrVe/GrOt/GrI1 pass the float-shape test; many target-test stages are packed data) and add converter field tables keyed on the stage's own `Grd<Stage>*` publics, with a raw-vs-converted regression per stage | open | — | `native/decomp/assets/hsd_convert.c`, `native/tests/test_decomp_assets.c`, `decomp/src/melee/gr/*.c` | Do not convert generically: `GrNBa`/`GrFs`/`GrFz` store offsets or packed bytes in the same symbol. Template is P-661/GrYt (converter v76). |
| P-658 | Convert the remaining unwalked public roots (unknown-root scan, G-127): `sqEventInitDataLevelTbl` (GmEvent level structs + `StartMeleeRules` MSB-first bitfields), `gmIntroEasyTable` (GmIntEz float/s16 layout), `standScene`/`cut*Scene` (GmRgStnd/GmRegEnd scene descs), `dbLoadCommonData` (debug only) | open | — | `native/decomp/assets/hsd_convert.c`, `native/tests/test_decomp_assets.c` | Repro: temporary `[unknown]` print in `convert_roots` over all archives; each needs its own field registry before walking. Product doesn't reach these modes yet (post-S6). |
| P-619 | Stage viewer: wire `Gr*.dat` + a posed fighter into `melee_decomp_viewer` (stage list, camera/lights, `--stage`, HUD/keys) | done | opencode (deepseek-v4.1-flash), 2026-09-12 | `native/decomp/render/`, `native/decomp/hsd/hsd_scene.{c,h}`, `native/decomp/assets/hsd_convert.c` | Done (see Completed). |
| P-646 | Memory card save data: `native/platform/card.c` implements the SDK CARD API over `$MELEE_CARD_DIR/card_a`, but the game's own card filesystem (`hsd_3A94.c` command pump) deadlocks once a card is present (state `hsd_804D799C=2`, empty queue, `lb_8001BC18` spins with `x8AC=1`); cards are opt-in via `MELEE_CARD_DIR`/`MELEE_CARD=1` until fixed | open | — | `native/platform/card.c`, `native/decomp/assets/hsd_convert.c`, `src/sysdolphin/baselib/hsd_3A94.c` (read-only) | Repro: `MELEE_CARD_DIR=/tmp/c melee --frontend ...` hangs after `open 'SuperSmashBros0110290334' -> -4`; with debug logging the pump never issues CREATE/READ/WRITE. `CARDOpen` matches the SDK (`NOFILE` -4); `CARDFreeBlocks`/mount/check all succeed. Next: trace `ctx->x8` (hsd command completion callback) after case 0 returns with state 2, and which queued command the HSD expects to be async. |
| P-642 | Further match-render optimization: move repeated vertex transform/texgen work toward a GPU/object-space path, or specialize the common GX display-list decoder without hashing/caching dynamic arrays | open | — | `native/decomp/gx/gx_hle.c`, `native/decomp/gx/gx_gl.c` | **Reinstated by ADR-0017 as P-676 (renderer parity).** Profile first under a quiet system. Current top CPU costs are `exec_primitive`/`read_vertex`, `read_comp`, `transform_vertex`, and `texgen_coord`. A decoded-display-list cache was tested and reverted because live arrays/state churn made hashing slower than decoding. Preserve direct/dynamic geometry and frame-718 pixel parity. |
| P-624 | S6: enter the title screen in the retail state (`GM_TITLE` ordering / `gm_804D67EC` past 5400) — the debug flow freezes the logo on its frame-0 reveal card (G-090) | open | — | `native/decomp/boot/`, `src/melee/gm/gmtitle.c` (read-only) | Repro: `./build/native/melee_decomp_viewer --match --frames 70 --shot /tmp/t.bmp`; the grey card behind the logo must not exist once the title is entered after the opening movie. Full analysis in G-090; do not patch the GL layer for it. |
| P-638 | Optional: port `reverb_hi`/`chorus` (`native/decomp/axfx/axfx_port.c` stubs them; Melee never registers either) and refine the mixer's ITD ramp | open | — | `native/decomp/axfx/`, `native/audio/ax_mixer.c` | Not on the critical path: `lbAudioAx_8002838C` registers reverb_std + delay only. |
| P-617 | Indirect-texture shader evaluation + toon ramp evaluation (GX HLE) | open | — | `native/decomp/gx/gx_gl.c` | **Reinstated by ADR-0017 as P-672 (GLES renderer parity).** **S4-only.** `GXSetTevIndirect`/`GXSetIndTex*` state is captured per draw (P-612); the GLES fragment path does not yet apply the indirect offsets. Only `lb/lbrefract.c` (stage refraction) uses it, so it cannot be validated until S4 runs a stage. Toon (`GX_TG_SRTG`) currently passes the source value through; stage-only content. |
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
3. **S5 — AX/DSP HLE. DONE 2026-09-12.** The compiled AX stack + software
   mixer drive `HSD_SynthCallback` on the 5 ms/200 Hz frame derived from VI,
   and the `.ssm`/`.sem`/`.hps` converters feed real voices. See Completed
   (P-632..P-636) and `learnings/decomp_audio.md`. Remaining owner check:
   P-637 passed (owner, 2026-09-13).
4. **S6 — CARD/EXI + fonts**. Card command pump (`hsd_803AAA48`) and a font
   source replacing the generated atlases.
5. **S4 — alarms/threads** (`OSCreateAlarm`/`OSSetPeriodicAlarm` are stubs;
   the boot installs a periodic alarm during init).

## S5 result

ADR-0013 option A landed in four commits (`a658deb92`, `41a427348`,
`c455ffd1e`, `bce5a84ba`).  The compiled SDK AX layer drives
`native/audio/ax_mixer.c`; `platform/{ssm,sem,hps}.c` convert the audio
assets; `--audio-dump`/SDL3 sinks and two ctest regressions guard it.  The
DSP-ADPCM frame geometry, the engine's u16-pair aliasing, and the three
formats are documented in `learnings/decomp_audio.md`; the four portability
patches are in `learnings/decomp_port.md` (S5 section).

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
| P-661 | Fix Yoshi's Story Lucky-Block freeze: `GrYt.dat`'s `yakumono_param` is `YorsterParams` (4 f32 + 4 s32) and was left big-endian, so the bump threshold read `-4.3e8` (always true) and the bump velocity read a denormal ~0, stopping the fighter mid-air.  Converter v76 keys the layout on the archive's `GrdYorster*` publics; `test_decomp_assets` checks x00=0.8/x10=2/x14=5/x1C=140 (first word fails before, G-130) | opencode (deepseek-v4.1-flash) | this commit | 2026-09-13 |
| P-644 | Fix every player's HUD stock icon showing Captain Falcon: `gm_80168B34` read an uninitialized `base` (retail keeps `ckind` in `r3`; GCC picked the Popo constant 14) and `gm_80168BF8` omitted its `return` (GCC deletes the side-effect-free call and returns 0.0), so `HSD_TObjReqAnimAll` always got frame 0 = `CKind_Captain`.  `PORT_PC` fixes in `patches/src/melee/gm/gm_1601.c.patch`; `ctest decomp_icons` checks Mario=8/DK=1 and different frames for the two live players (G-129) | opencode (deepseek-v4.1-flash) | this commit | 2026-09-13 |
| P-616 | Captain Falcon eyes: owner confirmed 2026-09-13 that `PlCaNr.dat` renders correctly now (the P-650+ fixes resolved the dark eye band); no code change | project owner | owner check | 2026-09-13 |
| P-621 | Stage material/lighting colors: owner confirmed 2026-09-13 that `test_decomp_render --stage GrNBa.dat` looks accurate enough against their expectation; no code change | project owner | owner check | 2026-09-13 |
| P-659 | Harden the converter: `in_data` wrapped on 32-bit (`off + need`), `conv_u16`/`conv_u32` accepted unaligned or pointer-overlapping offsets, and `conv_ef_dat` walked 1024 descriptors into unrelated data when both particle banks are null (167 corrupted pointers across 22 `Ef*Data.dat`, plus an ASan heap overflow).  Converter v75 fixes all three; `test_decomp_assets` sweeps all 861 HSD archives for reloc integrity and pins each effect table's `effect_descs` (EfCoData 50 vs 47 without the bound) | opencode (deepseek-v4.1-flash) | this commit | 2026-09-13 |
| P-657 | Convert two missed public roots found by an unknown-root scan over all 1,209 disc archives: `ScGamRegStaffrollNames_scene_modelset` (DynamicModelDesc** → `_modelset` branch) and `gmKumiteSystemTable*` (`RegClearSpawnEntry[]`, sentinel 0x3E7 → factory walk).  Converter v74; `test_decomp_assets` checks the ten modelset joints' flags and every Stadium spawn row against a raw copy (G-127) | opencode (deepseek-v4.1-flash) | this commit | 2026-09-13 |
| P-645 | Fix results-screen trophy lookup: the converter dispatched `tyModelFileTbl`/`tyModelFileUsTbl` with name lengths 15/17 for 14/16-character symbols, so neither TyDataf table was ever converted and `Toy_8030813C`'s id scan matched entry 0 for every character.  Correct the lengths (converter v73); `test_decomp_assets` now checks all 293+5 trophy ids against the raw archive (G-126) | opencode (deepseek-v4.1-flash) | this commit | 2026-09-13 |
| P-656 | Convert the `ftData->x48_items` per-fighter special `Article` arrays: the six pointers were relocated but their `ItemAttr`/hurt/state/model/dynamics pointees stayed big-endian, so Ness/Peach/Game & Watch/Link specials spawned with denormal speeds and wrong state trees.  Converter v72 walks the leading relocation-backed run (NULL holes legal) and only entries whose `attr` passes an `ItemAttr` sanity probe, because several fighters keep unrelated pointer tables after the run; `test_decomp_assets` diffs all 31 `ItemAttr` words of every entry for eleven fighters (G-125) | opencode (deepseek-v4.1-flash) | this commit | 2026-09-13 |
| P-655 | Fix unconverted `ftData` numeric pointees: `x40` `itPickup` (12 grab-offset floats; byte-swapped they are denormals, so the pickup volume sat at the world origin) and `x4C_sfx` `FtSFX` (11 sound ids + three `FtSFXArr` count/id tables; ids read `0xnn000000` and found no bank).  Converter v71 walks both; `test_decomp_assets` diffs every field against a raw copy for Mario/Ness/G&W/Peach/Fox (34 mismatches each before, G-124) | opencode (deepseek-v4.1-flash) | 3c608c225 | 2026-09-13 |
| P-654 | Fix item attribute flags: `ItemAttr`'s two flag bytes are MSB-first on the console (`itIsHeavy` `extrwi ...,1,24` = 0x80, `it_8026B30C` `extrwi ...,4,25` = 0x78, `itGetHoldKind` `clrlwi ...,29` = 0x07) but GCC packed them LSB-first, so heavy/hold/camera flags read unrelated bits; a `PORT_PC` reverse-order declaration lands them on the console bits, and `test_decomp_assets` compares all nine compiled fields against the raw article bytes for every common item (G-123) | opencode (deepseek-v4.1-flash) | this commit | 2026-09-13 |
| P-653 | Fix attacks doing no damage: `spawn_hitbox_skip.xF_b4` is bit 3 of command byte 0xF (retail `extrwi. r0,r0,1,28`), but GCC's unaligned `u32` bitfield read bit 4, so every hitbox spawn took the skip path; the `PORT_PC` struct now packs the flags in a console-ordered byte, and `ctest decomp_hit` (`MELEE_HIT_TEST=1`) requires the scripted opponent to take damage (G-122) | opencode (deepseek-v4.1-flash) | this commit | 2026-09-13 |
| P-652 | Fix Fox's Classic landing crash: the converter's `Fighter_WaitAnimData` walk overshot the array end and `conv_waitanim_flags` rewrote valid part-animation `HSD_AnimJoint*` entries with garbage (`0x00700313`), so `ftAnim_80070904` dereferenced a bogus tree; converter v70 stops at the first record with no name pointer, and `test_decomp_assets` compares every relocation field against a raw copy plus validates every part-animation tree for Mario/Link/Fox/Pikachu (G-121) | Codex | this commit | 2026-09-13 |
| P-651 | Fix Pokémon Stadium match-init crash in `grAnime_801C6F50`: under `PORT_PC` the AObj callback dispatcher now follows the declared `AObj_Arg_Type` shapes.  The retail code called `AOBJ_ARG_A` through `((Event) func)()` and relied on PowerPC's `r3` surviving the call; on i386 the callback received its own address as `aobj` and `grAnime_801C77FC` -> `fn_801C6F2C` faulted.  Classic/Stadium now enters the match (G-120) | Codex | this commit | 2026-09-13 |
| P-630 | Fix fighter part-animation crashes: converter v69 walks the leading relocation-backed `ftData->x1C` descriptors (bounded by five runtime slots) and byte-swaps their `u16` first-part/count fields; Mario and Link four-slot asset regressions reproduce `0x2900`/`3072` before the fix (G-119) | Codex | this commit | 2026-09-13 |
| P-643 | Fix random item-model crashes: converter v68 derives each article's variable-length `ItemStateDesc` count from the DAT layout instead of walking a fixed eight entries into adjacent article/model metadata; all 40 common-item model roots load through compiled `HSD_JObjLoadJoint`, items enabled by default (G-118) | Codex | this commit | 2026-09-13 |
| P-648 | Menu BGM no longer stops after the first HPS page: `ax_collapse_addr_sync` keeps the header `loopFlag` through the coalesced `AXSetVoiceAddr`+address-field sync window, the mixer's end test is crossing-based (no re-wrap buzz when the loop target sits above the lagging `endAddress`), and `hps_fix_read` byte-swaps the page table's `AXPBADPCMLOOP` contexts so seams are sample-continuous (G-115..G-117; before/after energy probe + page-machine/wrap traces in `logs/2026-09-13-P-648-hps-ring.md`) | opencode (deepseek-v4.1-flash) | this commit | 2026-09-13 |
| P-650 | Title/menu image-sequence textures: convert `HSD_TexAnim.id` (converter v67, G-114) so nonzero-map TexAnims bind — title logo fire cycles (tex1img changes 450→550 in one run), tunnel detail layers bind, main-menu 1-P preview shows its 4 submenu lines; `--dump-draws` gains tex1/uv1/quad/texgen-matrix details; `ctest` 15/15, ASan title smoke clean | Muse Spark | this commit | 2026-09-13 |
| P-649 | Main-menu 1-P preview submenu lines now render (same TexAnim.id root cause as P-650; owner-verified, panel shows 4 text bands post-fix; see P-649 handoff for the full investigation) | Muse Spark | this commit | 2026-09-13 |
| P-647 | SisLib fonts: read `sys/main.dol` as a raw disc region at header `0x420` (not an FST entry, G-112) so `HSD_SisLib_FontAtlas`/`HSD_DebugFontAtlas` fill and dialog text renders; correct DOL section-size offsets to `0x90`/`0xAC` in `native/decomp/fonts.c`; `MELEE_VIEWER_TRIAGE=1` boot no longer logs `cannot read 'sys/main.dol'`; `ctest` 15/15 | Muse Spark | this commit | 2026-09-13 |
| P-641 (root cause) | Stage effects rendered opaque: (1) combined `GXSetChanCtrl(COLOR0A0/COLOR1A1)` now mirrors `mat_src` into the paired alpha slot so vertex-alpha gradients (Final Destination glow, Battlefield core) fade; (2) converter v61 walks the stage map `AnimJoint`/`MatAnimJoint`/`ShapeAnimJoint` arrays (G-109/G-110); (3) match viewer follows WM window-size changes and initial size fits the display; `ctest decomp_gx_direct`/`decomp_assets` regressions | Codex | this commit | 2026-09-13 |
| P-641 | Fix Final Destination's opaque center effects: the shader now maps GX `REG0/1/2` from uniform slots 1/2/3 (slot 0 is PREV), restoring animated `GX_CA_A0` transparency; synthetic blended-pixel regression and richer draw dump | Codex | this commit | 2026-09-13 |
| P-631 | Fix Link's rogue cap polygon: converter v59 byte-swaps the packed 0x3C-byte `BoneDynamicsDesc` solver records consumed by `lb_80011710`; `PlLk.dat` dynamics regression and frame-718 visual check | Codex | this commit | 2026-09-13 |
| P-640 | Stage shadows: per-draw `GXSetViewport`, frame-boundary `HSD_StateInvalidate`, EFB-texture cache invalidation, and a third GL texture unit for base + two fighter shadows (G-102..G-104) | opencode (deepseek-v4.1-flash) | this commit | 2026-09-13 |
| P-639 | Stage shadow maps: implement `GX_CTF_R4` EFB copies so HSD's dynamic shadow texture is real (Final Destination's black platform band fixed); `ctest decomp_efb` pass 3 | opencode (deepseek-v4.1-flash) | this commit | 2026-09-13 |
| P-637 | Owner listening check: boot/title music and match SFX confirmed "pretty fine" (2026-09-13) | project owner | — | 2026-09-13 |
| P-632 | S5.1/S5.2: compile the SDK AX bookkeeping, add `native/audio/{ax_hle,ax_mixer}.c` (DSP-ADPCM, SRC, mix/VE/ITD, loop/end/state write-back, aux returns), VI-derived 200 Hz pump, `.ssm` record conversion, disc-free `ctest audio` | opencode (deepseek-v4.1-flash) | `a658deb92` | 2026-09-12 |
| P-633 | S5.3: compile `axdriver.c` + AXFX, port `reverb_std` to C, convert `smash2.sem`/`.hps`, fix the u16-pair endianness and the synchronous-loader completion pump | opencode (deepseek-v4.1-flash) | `41a427348` | 2026-09-12 |
| P-634 | S5.4: fix the DSP-ADPCM decode (8-byte/14-sample frames), add `--audio-dump` WAV + hash and the viewer's SDL3 sink, `ctest decomp_audio` | opencode (deepseek-v4.1-flash) | `c455ffd1e` | 2026-09-12 |
| P-635 | S5: fix the `.ssm` address pairs (u16 Hi/Lo) and the header read order; regression test; match SFX audible | opencode (deepseek-v4.1-flash) | `bce5a84ba` | 2026-09-12 |
| P-636 | S5.5: `learnings/decomp_audio.md`, gotchas G-097..G-100, STATE/TASKS/TESTING updates, ASan/UBSan 300-frame match clean | opencode (deepseek-v4.1-flash) | this commit | 2026-09-12 |
| P-627 | Fix Link's missing/glitched legs: `native/decomp/assets/hsd_convert.c` v58 byte-swaps the `ftData_x58_t` leg-IK lengths (`x4`/`xC`/`x18`). The raw big-endian words read as `-490 / -1e27 / 7.7e35`, so `ft_80089B08` fed degenerate targets to `lbBgFlash_80021410`; its `acos` outputs went NaN and poisoned leg JObj matrices (parts 6–10, 12–16). Frame 720 now renders both legs, `--dump-draws 720` has zero non-finite NDC bounds, frames 600–1200 stay under 10 ms game time, ASan boot-match clean. Corrects the earlier P-629 overclaim | codex | this commit | 2026-09-12 |
| P-629 | Fix the sustained 29 ms match plateau: native `__frsqrte` now has reciprocal-square-root semantics, all 32-bit decomp targets use SSE2 scalar FP instead of x87 excess precision, near-identical quaternion interpolation has a regression, and the pacer immediately re-anchors after a missed deadline. This did not fix Link's legs (P-627) | codex | 381ec2f68 | 2026-09-12 |
| P-620 | S4: first match — deterministic headless Link/Mario match, PAD backend + scripted input, 600-frame ctest `decomp_match`, ASan clean | opencode (deepseek-v4.1-flash) | this commit | 2026-09-12 |
| P-622 | S4: archive action-command scripts read via `scalar_storage_order("big-endian")` on the `CmdUnion` structs (G-082) | opencode (deepseek-v4.1-flash) | this commit | 2026-09-12 |
| P-623 | S4: live match in the viewer — `--match` runs the compiled game in-process with a VI present hook, looping PAD script and 60 Hz pacing; `--record FILE\|-` streams PPM frames for ffmpeg (G-087) | opencode (deepseek-v4.1-flash) | this commit | 2026-09-12 |
| P-625 | S4: match fighters render fully textured — `FtStatusFlags` fixes the MWCC `x21FC_flag` draw bit (G-091) and `gx_gl` evicts its GL texture cache LRU instead of binding black (G-092); new `--dump-draws FRAME` | opencode (deepseek-v4.1-flash) | this commit | 2026-09-12 |
| P-628 | S4: move GX channel evaluation to the GL vertex shader (was 22.5% of match CPU; uniforms for 4 channels + 8 lights, `GxHleVertex` keeps the view normal + has_color) | opencode (deepseek-v4.1-flash) | this commit | 2026-09-12 |
| P-626 | S4: walk/run animations loop — `conv_waitanim_flags` bit-reverses the top byte of `x10_animCurrFlags` into the low byte so `x594_b1_loop` reads the console bit and `AOBJ_LOOP` is set (converter v57, G-093) | opencode (deepseek-v4.1-flash) | this commit | 2026-09-12 |
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
