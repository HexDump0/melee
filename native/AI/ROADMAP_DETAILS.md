# Roadmap details (agent-facing)

Technical companion to [`ROADMAP.md`](ROADMAP.md). The human overview explains
*why*; this file explains *what to touch, how to verify, and what can go
wrong*. Read [`AGENTS.md`](AGENTS.md) first, claim work in
[`TASKS.md`](TASKS.md), and record decisions in [`DECISIONS.md`](DECISIONS.md).

Rule zero from AGENTS §0.1: `src/` is the specification. Port the actual
decompiled logic; do not invent equivalents.

---

## P-211 — Renderer rewrite (OpenGL 3.3 core, ES3-portable)

**Why now.** P-204 (TEV) cannot be expressed in fixed-function; doing it first
would be thrown away. ADR-0004's fixed-function decision is superseded.

**Deliverable.**
- `main.c` draw path: per-batch VAO/VBOs built from `DemoModelBatch` vertex
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

**Landed (see `learnings/hsd_tev_materials.md`).** `MObjMakeTExp` /
`TObjMakeTExp` state derivation in `demo_model.c` + shader evaluation:
material-vs-RAS initial stage, `TEX_COLORMAP_*`/`TEX_ALPHAMAP_*`,
DIFFUSE/SPECULAR/EXT lightmap phases (specular accumulates into
`mat.specular`, multiplied by the specular channel), `RENDER_DIFFUSE`, GX
channel lighting, `HSD_SetupPEMode` alpha compare/blend/Z, TEX0+TEX1, and
`ftData.model_scaling` applied as the root joint scale
(`Fighter_UpdateModelScale`). `--dump-tev` prints the parsed state.

**Remaining.** Real light colours/directions from `HSD_LObj` (`lobj.c`, stage
light lists); lightmap `repeat` chains; `HSD_TObjTev` active overrides (all 0
in the tested fighters); toon ramps; `x34_scale.z` (Game & Watch flattening,
mushroom/Giant scaling).

**Source of truth.** `src/sysdolphin/baselib/tev.c`, `tobj.c`, `mobj.c`,
`state.c`, `pobj.c`, and the `TObjDesc`/`MObjDesc` fields parsed in
`demo_model.c`. Derive state from those, never from screenshots.

---

## P-301 / P-302 — Real math and movement

**P-301:** compile pure HSD math and containers from `src/sysdolphin` behind a
thin platform shim (no GameCube headers in the port). Start with `mtx.c`,
`vec.c`, then `spline.c`. Replace `make_local_mtx`/`mtx_concat`/
`mtx_transform_*` in `demo_model.c` with the real functions once the shim is
proven (bind-pose bounds must stay identical).

**P-302:** port `ftCommon_*` movement and the fighter action state machine.
Blocked on an ADR about the `Fighter` struct: either compile decomp code with
a platform shim (large dependency surface) or maintain a reduced port-side
struct generated from `ft/types.h`. Whichever wins, per-action animation rate
(P-210, `frame_speed_mul`) and input handling come from this port.

**Verification:** a scripted input sequence produces numerically identical
positions to a reference run (or the deviation is documented). The current
sandbox should be kept runnable until the real path reaches parity.

---

## P-208 — IK joints

Port `resolveIKJoint1`/`resolveIKJoint2` and the `JOBJ_JOINT1/JOINT2/EFFECTOR`
branches of `HSD_JObjSetupMatrixSub` (`jobj.c`). Requires parsing `robj`/IK
hints for the port; currently the loader ignores `HSD_Joint.robjdesc`.
Acceptance: landing/ledge clips plant feet/hands like the game; no effect on
characters without IK joints.

## P-207 — Expression / visibility events

Expressions are **not** figatree `SETBYTE` channels (that callback list has no
registration API in the decomp). Port the action-driven path: `x5F4_arr`
variant state, `ftParts_80074B0C`/`ftParts_80074A4C`, `ftParts_80074B6C`/
`ftParts_80074D7C` (show/hide DObj lists), and the per-kind
`ftData_UnkIntBoolFunc0.model_events` table. Acceptance: Mario blinks and shows
damage faces; Pichu/Zelda variants switch.

## P-209 — Material animation

Parse `HSD_MatAnimJoint` (`Ply<Char>5K_Share_matanim_joint`, already a public
symbol) and evaluate its FObjs with `TObj`/`MObj` update semantics (`tobj.c`,
`mobj.c`, `ftanim.c:ftAnim_80070200`). Acceptance: a clip with texture/material
motion animates (e.g. Sheik/Zelda effects, stage-independent demos).

## Blending / shape sets / HSD_A_J_PATH

- **Blending** (`x8A4_animBlendFrames`): pose two skeletons (`parts[].joint` and
  `parts[].x4_jobj2`) and port `ftAnim_8006FE9C` interpolation. Acceptance:
  clip changes cross-fade instead of snapping.
- **Shape sets** (`POBJ_SHAPEANIM`): parse the shape-set list and evaluate the
  shape AObj to swap vertex data. Acceptance: parts that use shape animation
  change shape, not just transform.
- **`HSD_A_J_PATH`**: `JObjUpdateFunc` case 4 + `HSD_JObjMakeMatrix`'s
  `aobj->hsd_obj` translation override (`splArcLengthPoint`). Low priority for
  fighters; effects/stages use it.

---

## M4 — Stages and match loop

Decode `Gr*.dat` with the same HSD reader: stage joints/DObjs, collision lines
(`gr/` collision structs), spawn points, camera bounds, blast zones. Then KO/
respawn, a stock loop and results. Keep everything headless-verifiable
(scripted inputs, deterministic screenshots). P-206 camera polish belongs here.

## M5+ — Audio, frontend, targets

- **Audio:** ADR first (P-501). Options: reimplement AX/DSP, adopt an existing
  AX emulator, or a game-side mixer. Do not add dependencies without a
  decision entry.
- **Frontend:** mostly decompiled `mn/` code; depends on the M3 struct/compile
  strategy.
- **WASM:** P-502 memo; M2a's ES3 shader portability is deliberate groundwork.
  Known blockers: synchronous disc reads, threading, file access.
- **Netplay:** rollback experiment; needs M3+M4 determinism first.

## Cross-cutting requirements

- Every milestone keeps `--inspect` numbers stable or documents the change in
  `STATE.md`.
- Every milestone adds or preserves a headless check (`--inspect`,
  `--view --frames`, `--scripted --frames`, ASan build).
- Update `STATE.md` in the same commit as behavior changes; write learnings and
  gotchas as you go.
