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
| P-501 | Audio backend design memo | open | — | `native/AI/DECISIONS.md` | **On the critical path for S5.** Options: reimplement AX/DSP, adopt an existing AX/DSP interpreter (ACGC/Dolphin lineage), or replace with a per-game mixer. 116 AX symbols + AR in `src/`. Write the ADR before coding. |
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

1. **S2 — GX + VI HLE** (146 GX + 21 VI calls in 10 frames). Real GX FIFO,
      state/TEV, textures; real VI present. Seed: `native/gx/`, skeleton:
      `native/platform/gx_vi.c`.
2. **S3 — DVD + HSD DevCom/ARQ**. `DVDConvertPathToEntrynum`/open/read and
      synchronous `ARQPostRequest` callbacks so `HSD_DevComRequest` can finish
      asset loads. Seed: `native/platform/disc.c`.
3. **S5 — AX/DSP callback**. `AXRegisterCallback` must drive
      `HSD_SynthCallback` on the audio frame so synth loads complete.
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
