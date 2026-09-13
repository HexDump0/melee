# Match-path renderer performance (P-676, reinstated P-642)

**Date:** 2026-09-13
**Agent:** opencode (deepseek-v4.1-flash)
**Reference:** profile-first per P-642's warning that a decoded-display-list
cache was slower than decoding (array/state churn made hashing expensive).

## Benchmark method

Benchmark the **product** binary, not `melee_decomp_viewer` (that target is
stale since the S6 rename; the compiled frontend/viewer is `melee`):

```sh
SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy \
    perf stat -e cycles -- ./build/native/melee --match --frames 900
```

The PAD script loops deterministically, so the workload is identical run to
run; three alternating runs of two saved binaries give a stable A/B
(baseline spread ~0.3%, a clear separation shows up at >1%).

## Profile (perf record -g, 600-frame match)

Flat self time of the whole process (game logic is ~45%):

| Function | Self | Note |
|---|---:|---|
| `exec_primitive` (with inlined `read_vertex`/`transform_vertex`) | 21.05% | display-list decode + vertex transform |
| `read_comp` (out of line before P-676) | 8.52% | attribute fetch |
| `texgen_coord` | 5.72% | per-coord matrix chain |
| GL driver + `gx_gl_render_frame` | remainder | 229 draws, 61.9k verts/frame |

Steady-state `[match]` numbers: `draws=229 verts=61899 lists=224
cpu~7ms render~1-3ms` (60 Hz frame budget 16.6ms).

## What landed

**Redundant draw-state batching** (`gx_gl.c`): consecutive draws frequently
share the exact captured `GxHleDrawState` (measured hit rate 35.5k/183k =
**19.4%** over 900 frames). `upload_draw_uniforms`, `apply_viewport`,
`apply_draw_state` and the scissor setup are pure functions of that state, so
the last-applied state is remembered and those GL calls are skipped when it
matches. Textures are still resolved/bound every draw, and the memo is
invalidated by the frame clear (which disables scissor), EFB copies and the
depth-only Z-texture pass (different program).

Measured A/B (three alternating 900-frame runs, cycles): baseline
45.47e9 → 44.55e9, **-2.0%**, with match frames 600 and 718 byte-identical
between the two binaries and `test_decomp_render` RMSE 0.

## Reverted experiments (documented so they are not retried blindly)

- **`read_comp` inline + precomputed 1/2^frac exponent**: no measurable
  change (0.01% within noise); reverted.
- A decoded-display-list cache remains off the table (P-642); the remaining
  `exec_primitive` cost is dominated by the raw attribute fetch and
  transform, which change per frame with animation.

## Remaining opportunities

1. Skip redundant texture binds and the per-texture `lod_bias`/`dynamic_i4`/
   `size` uniform uploads when the resolved GL texture names are unchanged
   (needs care around cache invalidation).
2. Stream the frame vertex buffer with `glMapBufferRange` instead of
   `glBufferData` (~4.4 MB/frame) if a driver benchmark shows a win.
3. `texgen_coord` fast path for the common identity-matrix + one-coord case.
