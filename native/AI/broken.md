# Broken / blocked right now — owner tracker

This file is for the **project owner**. Agents plan from `TASKS.md`; this page
answers "what is visibly wrong today, and what is it waiting on?".

**Last updated:** 2026-09-14

Status meanings:
- **BROKEN** — confirmed wrong, reproducible.
- **SUSPECTED** — looks wrong but not confirmed against real Melee; needs a
  hardware/Dolphin capture (see the item).
- **BLOCKED** — known gap, waits on another system before it can be fixed.
- **NOT A BUG** — explained; do not "fix" it.

---

## Needs your eyes (SUSPECTED)

| # | What you see | Where | What would settle it |
|---|---|---|---|
| B-20 | A black box covers the top quarter of the 1P clear ("GAME!!" results) screen | `MELEE_GAMEOVER_TEST=1 ./build/native/melee --match --frames 300 --no-hud --shot /tmp/clear.bmp`, or just finish a Classic stage | Pinned down to one draw (a solid opaque black quad, GX rows 0..115) but **I need a Dolphin capture of the same screen**: is there a translucent dark band across the top in retail?  Yes -> the quad is real and only its blending is wrong; no -> its geometry is wrong.  Full analysis and both next steps in `handoffs/2026-09-14-P-697-clear-screen-black-band.md`; tracked as P-697. |
| B-1 | Bowser's hair/mohawk looks mangled during `Wait1` (fine in T-pose) | `--model PlKpNr.dat --view --animate --clip Wait1` | H-5: capture the same frame/angle in Dolphin. Match -> the hunched pose is authored; mismatch -> a real animation gap (P-207/blending). **Owner has no Dolphin access right now and will compare later — do not change Bowser before then.** Investigation notes: `learnings/hsd_animation.md` §7. |

Resolved 2026-09-11 (owner): the face texture artifact is gone (was B-2), and
the viewer at 180 Hz is confirmed correct (H-4).

Resolved 2026-09-14: the crash one frame after the "GAME!!" announcer at the
end of every 1P stage (`lb_800138EC` had no `return`; G-145, P-695), and the
~2.5 fps stall whenever a fighter left the camera (the magnifier's
`HSD_ImageDesc` was never byte-swapped, so it asked for a 0x4000 x 0x4000 EFB
copy; G-146, P-696).

## Confirmed gaps (BROKEN / BLOCKED)

| # | What you see | Status | Blocked on | Tracked as |
|---|---|---|---|---|
| B-3 | Characters never blink / no damage or angry faces | BROKEN | action-driven visibility events | P-207 |
| B-4 | Feet/hands slip or float in landing and ledge clips (no IK) | BROKEN | IK joint port (`resolveIKJoint1/2`) | P-208 |
| B-5 | Textures/materials do not scroll, fade or swap during clips | BROKEN | `HSD_MatAnimJoint` evaluation | P-209 |
| B-6 | Some actions play at the wrong speed (rate fixed at 1.0) | BROKEN | per-action `frame_speed_mul` table | P-210 |
| B-7 | Clip changes snap instead of cross-fading | BLOCKED | animation blending (`ftAnim_8006FE9C`) | M5 |
| B-8 | Shape-set and spline-joint models do not deform | BLOCKED | `POBJ_SHAPEANIM` data, `HSD_A_J_PATH` | M5 |
| B-9 | Game & Watch renders as a flat silhouette; no face outline; a few thin slivers remain | BLOCKED | runtime outline TEV (`ftmaterial.c`, M3) + part visibility | G&W note in `TASKS.md`, P-412 |
| B-10 | Metal, invisibility and damage-flash material states missing | BLOCKED | Fighter state / runtime MObj swap (M3) | `learnings/hsd_tev_materials.md` |
| B-11 | Exact lighting/specular differ in matches | BLOCKED | stage `HSD_LObj` light lists (M4) | P-204 |
| B-12 | Toon ramps, point/spot light attenuation, `HSD_TObjTev` overrides | BLOCKED | stage data / no active overrides in fighters | P-204 notes |
| B-13 | Sandbox movement, attacks and CPU are fake demo code | BLOCKED | `ftCommon_*` + fighter state machine | P-301/P-302, M3 |
| B-14 | Non-Mario characters use Mario physics values | BROKEN | per-character attribute tables | P-411 |
| B-15 | No stages, no match loop, no camera bounds | BLOCKED | stage decode `Gr*.dat` | M4 |
| B-16 | ~~No audio~~ | RESOLVED 2026-09-12 (S5), owner confirmed 2026-09-13: boot/title HPS music and match SFX play | — | — |
| B-17 | No menus / character select / results | BLOCKED | M3/M4 + frontend work | M7 |
| B-18 | No WASM, no Windows/macOS builds, no netplay | BLOCKED | post-M4 | P-502, M8 |
| B-19 | High-refresh displays still show 60 Hz motion (no interpolation) | Deferred by owner | must wait for the faithful 60 Hz port | P-212 |

## Explained — NOT A BUG (do not chase)

| # | What you see | Explanation |
|---|---|---|
| N-1 | Master Hand's wrist goes see-through and the floor grid shows through it | The wrist is intentionally `RENDER_XLU` with no depth write; the grid is drawn after the model. In-game there is no grid. Use `--no-grid` / `G` when inspecting. See G-045. |
| N-2 | Bowser looks hunched with his head down in `Wait1` | `Wait1` is a bowed idle; the bind pose (T-pose) is a different model state. See B-1 for the reference check. |
| N-3 | Viewer screenshots are not identical between builds | The renderer intentionally changed look in P-204 (lighting, TEV, model scale). Old before/after parity only applies up to commit `b35dd102e`. |

## How to add to this file

Add a row when you (the owner) see something off, with the exact command and a
screenshot path. When a fix lands, delete the row (or move it to N-* with the
explanation). Keep it short; details belong in `learnings/` or `gotchas/`.
