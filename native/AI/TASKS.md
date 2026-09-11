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

Status values: `open`, `claimed`, `blocked`, `review`, `done`.

## Workstreams

- **P-1xx** core plumbing (build, platform, assets)
- **P-2xx** rendering and animation
- **P-3xx** engine integration (use decompiled code)
- **P-4xx** tooling, testing, docs
- **P-5xx** future targets (audio, WASM, netplay)

## Open tasks

| ID | Task | Status | Agent | Files | Notes / acceptance |
|---|---|---|---|---|---|
| P-108 | Per-part isolation for the viewer (`HsdBatch`, batch lists) | done | follow-up | `native/hsd/model.*`, `native/main.c` | Added with the viewer; see Completed. |
| P-207 | Expression/part visibility events: port the per-kind `ftData_UnkIntBoolFunc0.model_events` path (`ftParts_80074B0C`/`ftParts_80074A4C`) | open | — | `native/hsd/model.c`, `native/main.c` | `SETBYTE`/`SETFLOAT` FObj channels have no callback registration in the decomp (`jobj.c` `ufc_callbacks` is a dead list), so expressions come from action code, not the figatree. Blinking/damage faces, Pichu/Zelda variant parts. **Bowser note:** the animated `Wait1` hair looked odd; investigation (see `learnings/hsd_animation.md` §7) found no decode/pose/binding bug on the port side, so this may be the authored hunched idle rather than a missing event. Re-check against a reference capture before blaming P-207. |
| P-208 | Resolve IK joints (`resolveIKJoint1/2`, `JOBJ_JOINT`/`EFFECTOR`) during pose evaluation | open | — | `native/hsd/model.c` | Foot/hand planting in landing and ledge clips. |
| P-209 | Material animation (`HSD_MatAnimJoint`) from `Pl*Nr.dat` / AJ clips | open | — | `native/hsd/model.c` | Texture scrolls/fades; `matanim_joint` public symbol is parsed but unused. |
| P-210 | Per-action animation rate (`frame_speed_mul`) instead of fixed 1.0 | open | — | `native/main.c` | Rate currently 1.0, matching Wait; other actions can be 0.5/2.0. |
| P-212 | Visual render interpolation for high-refresh displays (fixed 60 Hz sim) | open | — | `native/gx/render.c` | Deferred by the owner until the faithful 60 Hz port is complete; this is presentation only and must not touch simulation. Plan: keep previous/current pose snapshots, mix position/normal in the model vertex shader with a `u_interp` uniform, one tick of display latency, `--no-interp` for faithful mode. Requires an ADR first; `--scripted` must disable it so screenshots stay deterministic. |
| P-213 | Extract the viewer and sandbox out of `main.c` into `extras/viewer.c`, `extras/sandbox.c` | open | — | `native/main.c`, `native/extras/`, `native/CMakeLists.txt` | `main.c` is down to ~1k lines after the renderer extraction; finish the split so the app shell is CLI + dispatch and the extras are self-contained. No behavior change; verify with pixel-identical screenshots and the scripted run. |
| P-204 | TEV pass: material/RAS stage, colormap/alphamap, alpha test, XLU blend, TEX0+TEX1 | claimed | opencode (deepseek-flash), 2026-09-10 | `native/gx/render.c`, `native/hsd/model.c`, `native/hsd/model.h`, `native/hsd/parts.c` | Landed: `--dump-tev`, GX channel lighting, `HSD_SetupPEMode` blend/alpha-test/Z, `TObjMakeTExp` colormap/alphamap, TEX0+TEX1, lightmap phases (DIFFUSE/SPECULAR/EXT) with specular accumulation, per-character `model_scaling`. Remaining (see `handoffs/2026-09-10-P-204-tev-materials.md`): real `HSD_LObj` light values, lightmap repeat chains, `HSD_TObjTev` active overrides, toon, `x34_scale.z`. |
| P-205 | Per-TObj texture matrices (scale/translate/rotate) | open | — | `native/hsd/model.c` | `HSD_TObjDesc` at +0x10..+0x30. Fixes facial/eye UV offsets if they turn out to be wrong. |
| P-206 | Camera polish: zoom-to-fit both fighters, stage bounds, ledge visibility | open | — | `native/main.c` | Keep it headless-screenshot verifiable. |
| P-301 | Compile pure HSD math (`mtx.c`, `vec.c`) behind a shim | open | — | `native/decomp/`, `native/CMakeLists.txt`, `native/tests/` | Staged experiment (see `native/decomp/README.md`): compile `src/sysdolphin/baselib/mtx.c`+`vec.c` with shim headers, differential-test against `gx/math.c` via `ctest`, then delete the hand copy and repeat. Stop and keep hand-porting if the shim gets ugly. |
| P-302 | Replace `extras/physics.c` with real `ftCommon_*` formulas | blocked | — | `native/extras/physics.c` | Blocked on P-301 and a decision on struct layout (`Fighter` is huge; see `learnings/fighter_data.md`). |
| P-401 | CI task: run `--inspect` + scripted frames in GitHub Actions with a dummy disc | open | — | `.github/`, `native/` | Needs an asset-free path. Proposal: checked-in tiny synthetic HSD fixture generated by a script, not game data. |
| P-402 | Fuzz the HSD/disc parsers with a mutation harness | open | — | `native/` | Any crash is a bug. Keep fixtures in `native/AI/logs/` (small, synthetic only). |
| P-412 | Game & Watch residual pieces: identify why the collinear flat parts are not hidden (animation joint state vs vis tables) | open | — | `native/hsd/model.c` | See STATE.md issue 6. |
| P-411 | Generic per-character attributes (`ftData<Char>`), not just Mario | open | — | `native/game/attributes.c` | Acceptance: `--model PlFxNr.dat` reports Fox attribute values. |
| P-403 | Document GX formats actually present in `Pl*.dat` | open | — | `native/AI/learnings/gx_textures.md` | Enumerate format counts across all 26 characters. |
| P-501 | Audio backend design memo | open | — | `native/AI/DECISIONS.md` | Options: reimplement AX/DSP, use an existing AX emulator, or replace with per-game mixer. Write an ADR before coding. |
| P-502 | WASM feasibility memo | open | — | `native/AI/DECISIONS.md` | Emscripten + SDL2 + WebGL1. Identify blockers: synchronous disc read, threading, file access. |

## P-201 result

Done in `0e1a974d2` (backend) and `6665bd5f2` (viewer/sandbox). Clips come from
`Pl<Char>AJ.dat` FigaTree archives; `hsd/aobj.c` is a literal port of the
`fobj.c` player and `hsd/anim.c` binds nodes to joints exactly like
`ftAnim_8006F4C8`. Read `learnings/hsd_animation.md` before touching it. The
remaining fidelity work is P-207..P-210 above.

## Blocked / needs a human

| ID | Question | Requested from |
|---|---|---|
| H-1 | Does the interactive window/controller feel correct on real hardware? | project owner |
| H-3 | Pick priority: animation vs audio vs WASM after M2 | project owner |
| H-5 | Capture Bowser `Wait1` in real Melee/Dolphin for comparison: `./build/native/melee --model PlKpNr.dat --view --animate --clip Wait1 --anim-frame 15 --angle 30 --elevation 5 --no-grid --frames 1 --screenshot /tmp/kp.bmp`, then the same frame/angle on hardware. Match = the hunched pose is authored; mismatch = P-207/blending gap. **Owner has no Dolphin access right now; will compare later. Still open — do not "fix" Bowser before this.** | project owner |

Resolved human checks (owner, 2026-09-11): **H-4** — 180 Hz viewer
animation speed confirmed "perfect, looks awesome". **H-2** — the face
texture artifact is gone; no regression.

## Completed

| ID | Task | Agent | Commit | Date |
|---|---|---|---|---|
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
