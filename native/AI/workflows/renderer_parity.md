# Workflow: renderer parity with Aurora (ADR-0017)

Mission: make the compiled port's GX renderer (`native/decomp/gx/`) behave like
Aurora's GX for every feature Melee uses, **without** taking Aurora as a
dependency and **without** leaving the 32-bit in-place-converter architecture.
We keep our renderer; we port Aurora's algorithms into it.

This brief is for the dedicated parity agent. Claim one task at a time from
`TASKS.md` (P-671.., one active claim), and follow `AGENTS.md`/`TESTING.md`.

## Non-goals
- No 64-bit migration, no schema materializer, no Aurora link, no C++ in
  product targets (ADR-0017 parks ADR-0015/0016's S8 tasks).
- Do not touch `src/` except ADR-0011 portability patches.
- Do not chase Dolphin pixel parity without an oracle; screenshot baselines are
  internal consistency, not retail proof (`broken.md` policy).

## Reference checkout (not committed)
```sh
git clone https://github.com/encounter/aurora aurora-reference
git -C aurora-reference checkout 749d6ee7a22bdfab78c8ece9047bca5d79aa72ca
```
Pinned commit `749d6ee7a22bdfab78c8ece9047bca5d79aa72ca` is the revision the
`jonrosner/melee-native` port validates, so its behavior notes line up with the
source.  Keep the checkout untracked (like `ACGC-PC-Port/`).

Where to read in Aurora:
- `lib/gx/command_processor.cpp`, `lib/gx/regs.cpp`, `lib/gx/pipeline.cpp` —
  command/register semantics and state validation.
- `lib/gx/shader.cpp`, `lib/gx/shader_info.cpp` — TEV/channel/texgen → GPU
  program generation (the reference for formulas we approximate in GLSL).
- `lib/dolphin/gx/GX*.cpp` — the SDK API surface, including `GXCpu2Efb.cpp`,
  `GXExtra.cpp`, `GXPixel.cpp`, `GXTexture.cpp` (EFB copies, Z-texture, LOD).
- `lib/gfx/texture_convert.cpp`, `tex_palette_conv.cpp`, `tex_copy_conv.cpp`,
  `texture.cpp` — decode, palettes, copy filters, sampler state.
- `lib/gfx/pipeline_cache.cpp`, `recording.cpp`, `resource_cache.cpp` —
  performance structure (what a state-change-heavy game costs).
- `lib/gfx/depth_peek.cpp`, `frame.cpp`, `clear.cpp` — EFB/depth handling.

The companion notes `jonrosner/melee-native` `native/PORTING_NOTES.md` and
`native/stage_numeric_layouts.hpp` are the same-project evidence of which
behaviors actually matter for Melee (including renderer-independent bugs).

## Our architecture (the port target)
- `native/decomp/gx/gx_hle.c` — GX command surface, per-draw state capture,
  display-list/direct-mode decode, texture/TLUT capture, EFB copy/Z-texture,
  `--dump-draws`.
- `native/decomp/gx/gx_gl.c` — GLES3 program, TEV stage evaluation, channel
  lighting, texture cache, depth/scissor/dst-alpha handling.
- `native/decomp/render/` — scene/render harnesses used by tests.
- `native/decomp/shim/dolphin/gx/GXVert.h` — direct-mode FIFO shim.
- Tests: `ctest decomp_render`, `decomp_gx_direct`, `decomp_efb`,
  `decomp_stage*`, plus `test_decomp_render --direct/--no-gl` and the viewer's
  `--frames/--shot`/`--dump-draws`.

## Method
1. **Coverage matrix first (P-671).** Enumerate the GX API/register surface
   Melee calls (grep `GX[A-Z]` in `src/` and the compiled shim) and mark for
   each: implemented exactly / approximated / stubbed / n/a, with the Aurora
   reference symbol. This matrix is the worklist and the acceptance metric.
2. **Port loop per feature.** Read the Aurora implementation → write a test
   that fails on our approximation (screenshot crop, `--dump-draws` field, or
   ctest) → port the math/state to C/GLSL → document in `learnings/` or
   `gotchas/` → commit atomically. Flip the test once to prove sensitivity.
3. **Keep the toggles honest.** `--direct`, `--no-gl`, `MELEE_NO_*` switches
   used by tests must keep working; add a toggle only with a test that uses it.
4. **Perf work comes last** in each slice: correctness first, then batch state
   reduction, then measure with `[match] frame N ... render=Xms`.

## Known gaps to start from (owner-visible or documented)
- Indirect texturing and toon ramps: P-617 / P-672 (stage refraction, ramps).
- Channel/specular/attenuation and lighting differences: P-621 notes, P-673.
- EFB copies, R4 shadow filter, Z-texture edge cases: P-674 (G-068/G-102..104).
- Texture decode/sampler edge cases (LOD, anisotropy, CI/TLUT bounds,
  mipmaps): P-675 (G-088/G-089).
- Match-path performance and redundant state: P-676 (supersedes P-642).
- Screenshot/parity harness breadth across characters, stages, effects: P-677.

## Attribution rules
- Cite the source in comments for ported math/tables, e.g.
  `/* aurora lib/gx/shader.cpp: <symbol> */`.
- `native/licenses/aurora-MIT.txt` holds the notice; extend it if more
  material is copied.
- Do not paste Aurora C++ structure into C targets; port the algorithm, not
  the design.

## Acceptance
- Coverage matrix has no "stubbed" row for any GX feature Melee calls, and
  "approximated" rows are either removed or explicitly documented as
  deviations in `learnings/`.
- `ctest` green (currently 17/17), ASan clean for parser/memory-adjacent work.
- A parity report per slice: command used, before/after screenshots or draw
  dumps, and the regression name.
