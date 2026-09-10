# Roadmap

Milestones are outcomes, not dates. Do not start a milestone before the
previous one's acceptance check passes on a clean checkout.

## M0 — Asset sandbox (done)

Outcome: load and display real disc models and textures in a controllable
sandbox.

Acceptance:
- `--inspect` decodes `PlMrNr.dat` to 6328 triangles, 30 textures.
- Rendered Mario is textured and coherent in bind pose.
- `SDL_VIDEODRIVER=offscreen --frames 600` is ASan-clean.

## M1 — Animation

Outcome: fighters play their idle/walk/jump animations driven by real HSD
animation data.

Work:
- P-201 evaluate `AObj` curves (rotation/translation/scale) and drive joints.
- Rebuild the renderer so geometry is transformed per joint per frame.
- Parse `matanim` for materials if needed.

Acceptance:
- A looping idle animation plays for Mario at 60 Hz.
- A handoff screenshot shows at least two different poses.
- CPU load stays reasonable (< ~20% of one core on the reference machine).

## M2 — Faithful movement

Outcome: replace the sandbox controller with the decompiled movement formulas.

Work:
- P-301 compile real HSD math and `ftCommon_*` pure functions in the port.
- P-302 evaluate feasibility of a reduced `Fighter` struct layout.

Acceptance:
- Ground/air acceleration, friction, jump arcs and fast-fall match the
  formulas in `src/melee/ft/ftcommon.c` for Mario's attribute table.
- A scripted input sequence produces numerically identical positions to a
  reference run of the real game (or documented why not).

## M3 — Renderer fidelity

Outcome: look like Melee, not like unlit textured meshes.

Work:
- TLUT/CI textures (P-203), TEV approximation (P-204), transparency (P-204),
  toon ramps, z-order handling.

Acceptance:
- Side-by-side screenshot comparison of the same pose is recognisably the same
  character, with correct transparency on eyes/effects.

## M4 — Stages and game loop

Outcome: load a real stage archive, spawn two fighters on its collision, run a
match with damage/knockback/stocks and a simple results state.

Work: stage collision decode, spawn points, blast zones, KO/respawn, basic
fighter states (idle/walk/jump/attack/hitstun).

## M5 — Audio

Outcome: menu and in-match audio through a chosen backend. Requires a decision
entry first (P-501).

## M6 — Additional targets

Outcome: at least one of the following, chosen by the owner:
- WASM/browser build (P-502 memo first).
- Windows/macOS builds.
- Netplay experiment.

## Cross-cutting rules

- Keep the demo runnable at every milestone. Never land a change that breaks
  the M0 smoke test.
- Every milestone adds headless verification. If it cannot be verified
  headlessly, add a human checklist to `TASKS.md`.
- Prefer reusing decompiled code over reimplementing it, but only when the
  dependency surface is bounded (see `DECISIONS.md` ADR-0002).
