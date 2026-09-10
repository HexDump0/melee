# Native port roadmap (human overview)

This is the "where are we going and why" document, written to be read by the
project owner. The agent-facing companion with concrete task IDs, decomp
references, acceptance commands and risks is
[`ROADMAP_DETAILS.md`](ROADMAP_DETAILS.md). Live work claims are in
[`TASKS.md`](TASKS.md) and current behavior is in [`STATE.md`](STATE.md).

**Current position: M2 (renderer fidelity) is in progress — M2a (core-profile
rewrite) is done, M2b (TEV/materials) has landed its first increments.**

---

## The goal

A faithful PC port of Super Smash Bros. Melee (v1.02 / Rev 2) that runs from the
user's own disc image, reusing the decompiled engine code where practical, on
Linux first, then WASM and other platforms, with netplay as the long-term
stretch. "Faithful" means the port follows `src/` (the decompilation) function
by function, not an approximation that merely looks similar.

## Milestones at a glance

| # | Milestone | State | Roi / why it matters | Rough size |
|---|---|---|---|---|
| M0 | Asset sandbox | done | Proves disc -> HSD model -> GX texture -> GL | — |
| M1 | Animation | done | Fighters move like the game; basis for all later states | — |
| M2 | Renderer fidelity | **next** | Makes *everything* look like Melee and unblocks effects | 1-2 weeks |
| M3 | Faithful movement | later | Replaces sandbox physics with `ftCommon_*` | 3-6 weeks |
| M4 | Stages + match loop | later | A playable, complete match on real stages | 2-4 weeks |
| M5 | Animation polish | parallel | IK, faces, material animation, blending | 1-2 weeks |
| M6 | Audio | later | Menu + in-match sound (needs an ADR first) | 2-4 weeks |
| M7 | Frontend | later | Menus, character select, results | 2-4 weeks |
| M8 | Targets + netplay | stretch | WASM, Windows/macOS, rollback experiment | open ended |

---

## M0 — Asset sandbox (done)

Reads CISO/ISO/GCM disc images, walks the FST, decodes HSD joint/DObj/PObj
archives and GX textures (including CI4/CI8 + TLUT), reconstructs the bind pose
with the engine's asymmetric envelope rule, and renders a two-player sandbox.
This is the pipeline every later milestone builds on.

**Owner-visible:** `--inspect`, `--view`, model cycling, part isolation all work
against a retail disc.

## M1 — Animation (done 2026-09-10)

FigaTree clips from `Pl<Char>AJ.dat` are loaded and evaluated with a literal
port of the engine's FObj player; nodes bind to joints exactly like
`ftAnim_8006F4C8`; joints are posed and every vertex re-skinned per frame with
the engine's rigid/blended/shared rules and the dynamic envelope `right`
matrix. The viewer plays/pauses/scrubs/cycles clips; the sandbox switches
Wait/Walk/Dash/Jump/Fall per fighter.

**Owner-visible:** `--view --animate`, `--list-clips`, `A`/`,`/`.`/`Z`/`X`/`M`.

**Still open (tracked as P-207..P-210):** IK clipping, expression faces,
material animation, per-action playback rates, animation blending, shape sets,
spline (`HSD_A_J_PATH`) joints.

## M2 — Renderer fidelity (NEXT)

**M2a is done.** The renderer is OpenGL 3.3 core with GLSL 330 shaders written
in an ES3/WebGL2-portable subset; per-batch VAO/VBOs replaced the display
lists and the matrix stack (ADR-0009). Headless SDL `offscreen` + Mesa works
and the pre-rewrite frames were reproduced pixel-for-pixel before the material
work intentionally changed them.

**M2b is underway.** The decomp's material state is now evaluated by the
shader: `MObjMakeTExp`/`TObjMakeTExp` initial channel/material stage,
TEV colormap/alphamap, the DIFFUSE/SPECULAR/EXT lightmap phases, alpha test,
`RENDER_XLU` blend factors and GX Z state, plus TEX0+TEX1. Each fighter is
scaled by its `ftData.model_scaling` (`Fighter_UpdateModelScale`). Still open:
real `HSD_LObj` light values (stage code), lightmap repeat chains,
`HSD_TObjTev` overrides, toon ramps, Game & Watch's `x34_scale.z`.

**Owner-visible:** Luigi's face, Bowser/Kirby proportions and Marth/Kirby
materials now read correctly; translucent parts blend instead of drawing
opaque.

**Main risk:** exact lighting/specular depends on stage-created `HSD_LObj`
data, so it will stay approximate until stages (M4) are ported.

## M3 — Faithful movement and fighter states

Replace `demo_physics.c` (explicitly original sandbox code) with the real
engine: compile the pure HSD math from `src/sysdolphin` (P-301), then port
`ftCommon_*` movement and the fighter action state machine (P-302). This is
where per-action animation rates (P-210) come from for free, and where
knockback, hitstun, shielding and jumps become faithful instead of demo
approximations.

**Owner-visible:** movement feels like Melee; frame data and animations line up.

**Main risk:** the `Fighter` struct is huge and drags in GameCube headers; an
ADR decides between compiling decomp code with a platform shim versus a
reduced port-side struct.

## M4 — Stages and the match loop

Load real `Gr*.dat` stages: geometry, collision lines, camera bounds, spawn
points, blast zones. Add KO/respawn, damage/knockback (shared with M3), a stock
match loop and a simple results state.

**Owner-visible:** a complete match on a real stage against a CPU.

## M5 — Animation polish (can run in parallel)

P-208 IK (`resolveIKJoint1/2`) for planted feet/hands, P-207 expression/part
visibility events for blinking and damage faces, P-209 material animation,
animation blending between states, and shape sets. These are independent of
M3/M4 and can be picked up by another agent without touching the same files as
the movement work.

## M6 — Audio

Needs a decision memo first (P-501): reimplement AX/DSP, use an existing AX
emulator, or build a game-side mixer. Audio depends on M4's game loop being
stable enough to trigger sounds.

## M7 — Frontend

Menus, character select, results screens. Most of this is decompiled `mn/`
code; like M3 it depends on the platform/struct strategy.

## M8 — Targets and networking

WASM/WebGL2 (the ES3 shader work in M2a is deliberate groundwork), Windows and
macOS builds, and a netplay experiment (rollback) as a stretch.

---

## Parallel workstreams (any time)

| Workstream | Why |
|---|---|
| P-301 compile real HSD math from `src/` | Removes hand-written math; prerequisite for M3 |
| P-401/P-402 CI + parser fuzzing | Keeps the tree honest without a disc |
| P-403 GX format census | Cheap documentation that helps M2b |
| P-411 per-character attributes | Small, improves non-Mario fighters |
| P-206 camera polish | Makes the sandbox presentable |

## Critical path

`M2 (renderer) -> M3 (movement) -> M4 (stages/match) -> M6 (audio) -> M7 (frontend)`,
with M5 running alongside and M8 on top.

## What "done" looks like

A stock match on a real stage against a CPU or a second player, with faithful
movement, hitboxes, knockback and animation; correct GX-style rendering;
original audio; menus; saves; and eventually online play — all running from the
user's own disc image on multiple platforms.
