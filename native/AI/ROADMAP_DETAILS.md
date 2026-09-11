# Roadmap details (agent-facing)

Technical companion to [`ROADMAP.md`](ROADMAP.md). The human overview explains
*why*; this file explains *what to touch, how to verify, and what can go
wrong*. Read [`AGENTS.md`](AGENTS.md) first, claim work in
[`TASKS.md`](TASKS.md), and record decisions in [`DECISIONS.md`](DECISIONS.md).

Rule zero from AGENTS §0.1: `src/` is the specification. Port the actual
decompiled logic; do not invent equivalents.

---

## Architecture (2026-09-11): compile the decompilation, port the hardware

**ADR-0010 supersedes ADR-0001. ADR-0011 defines the `src/` patch policy.**

The product is now the retail game compiled from `src/`, with the GameCube
hardware replaced by a platform layer under `native/`:

- `src/` + `extern/dolphin/` compile into the port binary, read-only by default.
  Portability fixes only per ADR-0011 (`#ifdef PORT_PC`, GC build green, listed
  in `learnings/decomp_port.md`); `extern/` is never edited; excluded asm TUs
  are replaced from `native/decomp/`.
- The platform layer implements: OS (threads/timers/arena/interrupt stubs),
  DVD/asset loading with host-endian conversion, GX→OpenGL/Vulkan HLE,
  AX→audio HLE, PAD/VI/SI/EXI/CARD/AR, and the SDL/core plumbing.
- The prototype is repurposed: `gx/render.c` + shaders + TEV derivations are the
  GX backend seed; `platform/disc.c` is the DVD backend; `--inspect`/`--view`/
  `--scripted`/screenshots are dev tools and the per-layer parity oracle.
- One source of truth per function: a hand-ported engine file is deleted in the
  same commit as the compiled version that replaces it, once parity is proven
  (differential test, `--inspect` numbers, byte-identical screenshots while both
  paths coexist).

### Evidence (2026-09-11)

Full detail in `learnings/decomp_port.md`; summary:

- `build/GALE01/report.json`: 19,820/19,828 functions (99.96%) match the retail
  DOL, data 100%, fuzzy 99.995%. 5 incomplete units, 4 low-fuzzy functions.
- GCC syntax census: 834/1034 `src/*.c` compile clean; residual = missing
  `stdint` typedefs, 180 GameCube offset assertions, 59 `BOOL`/`bool` callback
  mismatches, ~52 `src/MSL` files (excluded on PC), a few per-file quirks.
- Platform surface: GX 171 unique symbols, AX 116, OS 63, SI 29, CARD 24,
  AR 17, VI 13, DVD 10, PAD 9, AI 4, EXI 2.
- Asm TUs: 4 in `src/` (`hsd_397E`, `gm_1601`, `gmmain`, `grbigblue` functions),
  20 in `extern/dolphin/src` (including the SDK `mtx.c`/`vec.c` math pairs).
- No REL code modules load at runtime; disc assets are data-only HSD archives.

### Milestones

#### S0 — Feasibility spike (next, gate)

- **Goal:** prove the decompilation's own data path runs on a 64-bit LE host.
- **Deliverable:** additive probe targets (`melee_decomp_hsd`) that compile
  `src/sysdolphin/baselib/archive.c` (+ allocator/class deps) and run
  `HSD_ArchiveParse` on a real `PlMrNr.dat`; then `HSD_JObjLoadJoint` with a
  host-endian conversion for the structural sections; joint world matrices
  compared to the hand port.
- **Exit:** symbol/joint tables match the hand parser; bind-pose world matrices
  match within a documented tolerance; prototype binary and `--inspect`
  untouched.
- **Risks:** endianness strategy may need per-format work earlier than planned;
  HSD allocator/class chain may pull more files than expected.
- **Tasks:** P-601 (census/shim), P-602 (S0a parse), P-603 (S0b joint parity).

#### S1 — Boot skeleton

- **Goal:** run the decomp's `main()` (`src/melee/gm/gmmain.c:130`) with stubbed
  OS/DVD/GX/VI and learn what it needs in order.
- **Deliverable:** OS/DVD stub layer that logs each unimplemented call; a boot
  log to a controlled stop.
- **Exit:** reproducible boot trace; backend work list derived from it.
- **Risks:** arena/thread/interrupt boot order; the 5 incomplete units.

#### S2 — HSD runtime + GX HLE

- **Goal:** compiled HSD renders.
- **Deliverable:** full `src/sysdolphin` compile; GX backend for the
  FIFO/vertex/TEV/texture path HSD uses, grown from the prototype renderer
  (keep the ES3-portable shader work, ADR-0009).
- **Exit:** character render through compiled HSD+GX with screenshot parity;
  deviations explained.
- **Risks:** GX surface larger than the prototype batch model; display-list
  endianness handled in the backend.

#### S3 — Asset pipeline

- **Goal:** every real disc asset loads through compiled loaders.
- **Deliverable:** host-endian conversion pipeline (structural words, FObj
  streams, display lists, textures; per format), cached/versioned, based on the
  existing `learnings/`.
- **Exit:** 26 character archives + stages + common assets load; bounds match
  `STATE.md` or deviations are explained.
- **Risks:** least forgiving area; wrong swap = garbage/silent desync. The hand
  parser is the oracle.

#### S4 — First match

- **Goal:** the game's own match loop with real fighters/items/stages
  (subsumes old M3+M4).
- **Deliverable:** compiled `ft`/`it`/`gr`/`gm`; two fighters on a stage with
  engine physics, collision, camera, stocks.
- **Exit:** deterministic scripted match headless; 600-frame ASan clean;
  numeric comparisons documented.
- **Risks:** 64-bit/float divergence appears only in long simulations; tick and
  input order must match the GC.

#### S5 — Audio

- **Goal:** in-match + menu audio.
- **Deliverable:** AX/DSP HLE or equivalent, chosen by P-501/ADR first; ARAM and
  audio tables via S3.
- **Risks:** biggest single unknown; may adopt an existing approach.

#### S6 — Frontend + saves

- **Goal:** the game as a product.
- **Deliverable:** compiled `mn`, saves/memory card via the platform layer.
- **Exit:** boot → menu → select → match → results → save/load.

#### S7 — Platforms + mods (stretch)

- Android (SDL2+GLES3), web (Emscripten+WebGL2), Windows/macOS parity, mod
  hooks/asset overrides, netplay once deterministic.

### Frozen work (do not start)

`P-204` leftovers, `P-205`..`P-210`, `P-302`, `P-411`, `P-412` are `parked` in
`TASKS.md`: compiled `jobj`/`fobj`/`ftanim`/`tobj`/`ftData` replace them. Do not
extend the hand HSD parser/renderer beyond what the bring-up itself needs.

---

## Legacy hand-port track (frozen 2026-09-11 — kept for reference)

The sections below document the prototype plan. ADR-0010 froze new
engine-feature work here; they remain the reference for what the prototype does
and for the platform backend it seeds.

---


## P-211 — Renderer rewrite (OpenGL 3.3 core, ES3-portable) — DONE

**Status:** landed in `b35dd102e`; ADR-0009 supersedes ADR-0004. See
`learnings/gl_shaders.md` and `handoffs/2026-09-10-P-211-renderer-core-profile.md`.

**Why now.** P-204 (TEV) cannot be expressed in fixed-function; doing it first
would be thrown away.

**Deliverable.**
- `main.c` draw path: per-batch VAO/VBOs built from `HsdBatch` vertex
  ranges; one base shader (position, normal, colour, uv, texture sample,
  material colour, optional alpha test); uniforms for the HSD texture matrix
  (currently `glMatrixMode(GL_TEXTURE)`), material colour/alpha and
  `RENDER_ZMODE_ALWAYS`/`RENDER_NO_ZUPDATE`.
- Shader source written for GLSL 330 with a small `#ifdef GL_ES` / precision
  header so the same file compiles as GLSL ES 3.00 later (WASM/WebGL2).
- Remove display lists and immediate mode from the render path; keep the
  `--view` and `--scripted` code paths and deterministic screenshots.
- Lighting: replace `GL_LIGHT0`/`glColorMaterial` with a simple directional
  term in the shader (match the current look, improve later).
- Vertex colours + `glColor4ubv` semantics move into the vertex format.

**Steps.**
1. Spike: `SDL_VIDEODRIVER=offscreen` + Mesa must create a 3.3 core context on
   the reference machine. Print `glGetString(GL_VERSION/GL_RENDERER)` and
   confirm a triangle renders. Report the strings in the commit/handoff.
2. `DECISIONS.md`: ADR-0009 (target, ES3 portability, headless guarantee);
   mark ADR-0004 superseded.
3. Rewrite `Visual` GL resource setup (`compile_model`, `list_vertices`,
   `render_viewer`, `draw_fighter`) as VBO + shader draws. Upload the posed
   vertex buffer (animated path already produces a CPU array) and the bind
   vertex array (static path). Batches keep their texture/wrap/texmtx/cull/
   z-mode state; draw order stays identical.
4. Verify: `--inspect` numbers unchanged; bind and animated screenshots match
   the pre-rewrite frames (attach before/after paths in the handoff);
   240-frame scripted and 600-frame ASan runs clean.

**Acceptance.**
- No fixed-function/display-list calls remain in the draw path.
- `--view` bind pose, `--view --animate`, and scripted match render.
- `SDL_VIDEODRIVER=offscreen` works, screenshots deterministic.
- Warning-free build; ASan/UBSan clean.

**Risks / notes.**
- Some drivers only give a compatibility profile; request `SDL_GL_CONTEXT_
  PROFILE_CORE` with version 3.3 and fail loudly if unavailable.
- Don't change animation timing: the viewer uses a fixed 60 Hz accumulator and
  the user's display is 180 Hz (G-038).
- Keep `list_vertices`' per-texture-run binding behavior; it can become one
  draw per contiguous texture run.

---

## P-204 — TEV approximation (in progress)

**Landed (see `learnings/hsd_tev_materials.md`, `hsd_lights_fog.md`).**
`MObjMakeTExp` / `TObjMakeTExp` state derivation in `hsd/model.c` + shader
evaluation: material-vs-RAS initial stage, `TEX_COLORMAP_*`/`TEX_ALPHAMAP_*`,
DIFFUSE/SPECULAR/EXT lightmap phases (specular accumulates into
`mat.specular`, multiplied by the specular channel), `RENDER_DIFFUSE`, GX
channel lighting, `HSD_SetupPEMode` alpha compare/blend/Z, TEX0+TEX1,
per-TObj `HSD_TexLODDesc` filters/LOD bias/anisotropy, `ftData.model_scaling`
(`Fighter_UpdateModelScale`) and Mr. Game & Watch's width/costume diffuse.
Real scene lights/fog come from the character-select `HSD_LObj` set
(`MnSlChr`, `--dump-lights`). `--dump-tev` prints the parsed state.

**Remaining.** Stage light lists (`src/melee/gr/*`, M4); exact GX specular
attenuation polynomial; point/spot attenuation; lightmap `repeat` chains;
`HSD_TObjTev` active overrides (all 0 in the tested fighters); toon ramps;
Flat Zone's `x7E4_scaleZ`.

**PARKED (ADR-0010).** These leftovers are superseded by GX HLE (S2) and the
compiled `tobj`/stage code (S3/S4); the landed prototype behavior stays as the
GX backend's fallback oracle. Do not start new work here.

**Source of truth.** `src/sysdolphin/baselib/tev.c`, `tobj.c`, `mobj.c`,
`state.c`, `pobj.c`, and the `TObjDesc`/`MObjDesc` fields parsed in
`hsd/model.c`. Derive state from those, never from screenshots.

---

## P-301 / P-302 — Real math and movement

**P-301 — DONE (`c903e5282`), see `learnings/decomp_shim.md`.** Pure-C HSD math
does compile behind a near-empty shim (`native/decomp/shim/decomp_shim.h`), and
`HSD_MtxSRT` is now built verbatim from `src/sysdolphin/baselib/mtx.c` and
replaced the hand copy in `hsd/model.c` (bitwise parity, screenshots
byte-identical). However, `extern/dolphin/mtx/{mtx.c,vec.c}` are Metrowerks asm
and **cannot** be compiled by GCC/Clang, so the SDK `PSMTX*`/`PSVEC*`
primitives stay hand-ported. `mtx_concat`/`mtx_transform_*` still use the port's
hand copies until a primitive backend exists.

**P-302 — SUPERSEDED by ADR-0010.** Hand-porting `ftCommon_*` was the old M3
plan; the compiled `ft` code (S4) is the real thing. P-301's verdict is now
historical context: pure-C HSD files compile (and the whole tree mostly does,
see the architecture section above); the SDK `PSMTX*`/`PSVEC*` primitives stay
hand-provided because the SDK math TUs are Metrowerks asm (ADR-0011 rule 3).
The per-action animation rate, input handling and physics all come from the
compiled engine.

**Verification (prototype, until S4):** a scripted input sequence produces
numerically identical positions to a reference run (or the deviation is
documented). The sandbox stays runnable until the compiled path reaches parity.

---

## P-208 — IK joints (PARKED, ADR-0010)

**Superseded by the compiled `jobj.c` (`HSD_JObjSetupMatrixSub`) in S2/S4.**
Kept for reference: `resolveIKJoint1`/`resolveIKJoint2` and the
`JOBJ_JOINT1/JOINT2/EFFECTOR` branches. The current loader ignores
`HSD_Joint.robjdesc`; the compiled loader will not.

## P-207 — Expression / visibility events (PARKED, ADR-0010)

**Superseded by compiled `ftparts.c` action code in S4.** Kept for reference:
expressions are action-driven (not figatree `SETBYTE` channels) via `x5F4_arr`
variant state, `ftParts_80074B0C`/`ftParts_80074A4C`,
`ftParts_80074B6C`/`ftParts_80074D7C` and the per-kind
`ftData_UnkIntBoolFunc0.model_events` table.

## P-209 — Material animation (PARKED, ADR-0010)

**Superseded by compiled `tobj.c`/`mobj.c` + `ftAnim_80070200` in S2/S3.**
Kept for reference: `HSD_MatAnimJoint` (`Ply<Char>5K_Share_matanim_joint`) and
its FObj evaluation.

## P-212 — Visual render interpolation (deferred by the owner)

Under ADR-0010 this applies to the compiled simulation (S4+); re-scope it then.
The ADR-first requirement and the "never touch simulation" rule still hold.

**Scope.** Presentation only; after the faithful 60 Hz port is complete. The
simulation keeps stepping at 1/60 and stays deterministic; render frames mix
the previous and current tick's skinned vertices (or joint transforms) by
`alpha = accumulator / (1/60)`. One tick of display latency, `--no-interp`
for the authentic cadence, `--scripted` always off. Needs an ADR first (the
presentation-vs-faithfulness decision). Not on the critical path.

## Blending / shape sets / HSD_A_J_PATH (hand-port reference; superseded by compiled HSD in S2)

- **Blending** (`x8A4_animBlendFrames`): pose two skeletons (`parts[].joint` and
  `parts[].x4_jobj2`) and port `ftAnim_8006FE9C` interpolation. Acceptance:
  clip changes cross-fade instead of snapping.
- **Shape sets** (`POBJ_SHAPEANIM`): parse the shape-set list and evaluate the
  shape AObj to swap vertex data. Acceptance: parts that use shape animation
  change shape, not just transform.
- **`HSD_A_J_PATH`**: `JObjUpdateFunc` case 4 + `HSD_JObjMakeMatrix`'s
  `aobj->hsd_obj` translation override (`splArcLengthPoint`). Low priority for
  fighters; effects/stages use it.

All three are implemented by the compiled `fobj`/`jobj`/`aobj` code once S2
lands; this section is retained only to explain the prototype's gaps.

---

## M4 — Stages and match loop (SUPERSEDED by S3/S4, ADR-0010)

The old plan decoded `Gr*.dat` with the hand HSD reader. Under ADR-0010 the
compiled `HSD`/`gr` code loads stages and runs the match loop (S3/S4); the hand
reader remains the format oracle for the asset pipeline. P-206 camera polish is
parked with the rest of the viewer-only work.

## M5+ — Audio, frontend, targets (SUPERSEDED by S5/S6/S7, ADR-0010)

- **Audio:** P-501 memo/ADR stays on the critical path for S5. Options:
  reimplement AX/DSP, adopt an existing AX/DSP interpreter, or a game-side
  mixer. The platform surface is 116 AX symbols + AR.
- **Frontend:** compiled `mn` code (S6); no separate hand port.
- **WASM:** P-502 memo; the ES3 shader work and thin platform layer are the
  groundwork. Blockers: asset size/delivery, threading, audio.
- **Netplay:** rollback experiment; needs S4 determinism first.

## Cross-cutting requirements

- Every milestone keeps `--inspect` numbers stable or documents the change in
  `STATE.md`.
- Every milestone adds or preserves a headless check (`--inspect`,
  `--view --frames`, `--scripted --frames`, ASan build).
- Update `STATE.md` in the same commit as behavior changes; write learnings and
  gotchas as you go.
