# Line/point primitives and per-draw topology runs (P-680)

**Date:** 2026-09-13
**Agent:** opencode (deepseek-v4.1-flash)
**Ported from:** Aurora `lib/gx/pipeline.cpp` primitive/topology map and
`lib/gx/shader.cpp` line/point tex-offset notes; the SDK emit order is
`decomp/extern/dolphin/src/dolphin/gx/GXGeometry.c`.

Evidence: `ctest decomp_gx_direct` (run modes/counts) and `ctest decomp_efb`
pass 8 (a red window-row line and a 5 px green point).  The character-select
screenshot is byte-identical to the P-676 baseline (the run path merges
consecutive same-topology groups, so triangle-only draws keep one run and
render exactly as before).

## What changed

`exec_primitive` used to drop `GX_LINES` (0xA8), `GX_LINESTRIP` (0xB0) and
`GX_POINTS` (0xB8) in its `default:` branch.  Melee draws them from
`lb_0146.c`/`lb_00F9.c`/`psdisp.c` (HUD lines/points) — those pixels were
simply missing.

`GxHleDraw` now carries up to 16 topology runs (`GxHleRun`: first_vertex,
vertex_count, mode = TRIANGLES/LINES/POINTS).  Consecutive primitive groups
of the same topology *share* a run, so a normal triangle-only display list
still has exactly one run and the draw loop is a pure refactor for it; mixed
lists (e.g. a triangle strip followed by a point sprite) split only where the
topology changes.  Draws recorded before P-680 (run_count == 0) keep the
legacy whole-draw triangle path.

Emission rules (SDK GXGeometry semantics):
- `GX_LINES`: disjoint pairs (0,1), (2,3), ...;
- `GX_LINESTRIP`: (i-1, i) for every i >= 1;
- `GX_POINTS`: one vertex per point.

`GXSetLineWidth`/`GXSetPointSize` now capture into the draw state;
`apply_draw_state` applies `glLineWidth` (GLES may clamp wide lines to 1 —
documented) and the vertex shader writes `gl_PointSize` from the captured
point size.  `GXEnableTexOffsets` is still a documented no-op: only
`psdisp.c:1981` calls it, and the texel offset it enables is a sub-texel
detail for textured point sprites that no current fixture reaches.

## Why the run merger matters

A first cut opened a run per primitive group; draw 8 (1296 verts, 16 groups)
hit `GX_HLE_MAX_RUNS`, and the uncovered vertices silently disappeared
(54 pixels changed in the model screenshot).  The debug probe
(`RUNPROBE draw=8 verts=1296 covered=924 runs=16`) caught it.  Merging
same-topology groups keeps a single run per draw; the run cap then only
bounds *topology changes* inside one display list, which Melee never
exceeds.

## Re-run

```sh
ctest --test-dir build/native -R decomp_gx_direct
ctest --test-dir build/native -R decomp_efb
```

Sensitivity: dropping the three primitives again fails
`direct: FAIL line/point draws=1 (want 3)` and both `efb: FAIL ... pixel=13,15,23`
checks; forcing `point_size = 1` fails the off-center point-coverage pixel.
