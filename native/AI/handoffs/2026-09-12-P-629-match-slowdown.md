# Handoff: P-629 — sustained match slowdown from viewer frame ~670

**Date:** 2026-09-12
**Agent:** opencode (deepseek-v4.1-flash)
**Commit:** working tree, includes this handoff
**Tree state:** builds clean, ctest 12/12, ASan 600-frame match clean

## What I did

- Moved the GX channel lighting (XF diffuse, specular attenuation,
  `GX_SRC_*`, paired alpha channels) out of per-vertex C in `gx_hle.c` and
  into the GL vertex shader (P-628, commit `fa4f39f01`): the HLE vertex now
  carries the view-space normal + `has_color`, and `gx_gl` uploads four
  channel slots and eight lights as uniforms.  Verified by ctest
  (`decomp_render`/`decomp_gx_direct` pass on their old references), a
  frame diff of RMSE <= 3.4/255, and ASan.  Match spikes dropped 8.5/s ->
  3.4/s.
- Made the interactive pacer stop fighting vsync and re-anchor instead of
  burst-catching-up (`ec2ffe36c`).
- Added match frame instrumentation to `viewer_main.c`: the 30-frame
  progress line prints `draws verts lists prims game cpu sleep render
  frame max`, and any frame over 25 ms logs a `[match] spike` line with the
  same fields.  `--dump-draws FRAME` lists every captured draw with its
  texture bindings, texgen types, blend state and NDC bounds.

## The open bug (P-629)

From **viewer frame ~670** the match halves in speed and Link's model
glitches; it recovers around frame ~1080 (deterministic in the scripted
match).  Measurements from `[match]` (offscreen, `SDL_VIDEODRIVER=offscreen
./build/native/melee_decomp_viewer --match`):

```
frame 660  draws=223 verts=61821 lists=218 prims=1074 game=3.73ms cpu=9.92ms render=2.75ms
frame 720  draws=223 verts=61821 lists=218 prims=1074 game=29.78ms cpu=28.80ms render=2.56ms
frame 780  draws=226 verts=61860 lists=221 prims=1077 game=28.94ms cpu=30.98ms render=2.53ms
```

- `game` is the wall time the game side takes between presents (excludes
  our sleep/swap); `cpu` is `CLOCK_PROCESS_CPUTIME_ID` per frame.  Both
  double (10 -> 29 ms), so it is real CPU work, not scheduling/frequency.
- **The capture counts are identical** across the transition (draws,
  vertices, display lists, primitives, texgens, and a full `--dump-draws`
  diff shows no change), and `render=` stays ~2.5 ms.  The extra ~25 ms is
  therefore in just a few per-vertex code paths whose *input mix* changed.
- Slow-phase `perf` (attach to the running viewer after 12 s, 8 s sample):
  `mtx3x3_mul_vec` 35%, `transform_vertex` 25% (position transform
  `mtx3x4_mul_vec`), `submit_triangle` 11%, `texgen_coord` 9%, and an
  inlined `decode_color` (`gx_hle.c` vertex-color decode with
  `expand5`/`expand6`) 35% self.  The `cpu_atom/cycles/Pu` event name is
  what the machine's PMU reports; the count/CPU-time data says the work is
  real, so ignore any migration theory.

Prime suspects, in order:

1. **Vertex-color decoding**: `read_vertex` decodes CLR0 with
   `decode_color` per colored vertex.  If the glitched Link (or the whole
   pass) switches to a colored format (RGB565/RGBA6), the cost jumps.  Add
   counters for colored vertices and format, then LUT `expand5/6`.
2. **NBT path**: `transform_vertex` does three `mtx3x3_mul_vec` calls
   (normal + binormal + tangent) unconditionally, even when `raw->has_nbt`
   is 0.  Gate the binormal/tangent transforms on `has_nbt` (they are zero
   otherwise) and only transform normals when a texgen or the channel path
   needs them.
3. **P-627** (broken Link pose) is almost certainly the same event: the
   glitch and the slowdown start together.  Inspect Link's model state at
   frames 660 vs 720: `--dump-draws`, his `motion_id`/`anim_id` and
   vertex formats (`GXSetVtxDesc` calls).

Longer-term, the remaining capture cost is CPU vertex read/transform +
texgen; the next architectural step is the GX matrix palette on the GPU
(raw object-space vertices + per-vertex matrix id + `pos_mtx[30]`/
`nrm_mtx[30]` uniforms), but the per-draw matrix snapshot must be solved
first (matrices change within a frame; consider capturing the palette for
only the matrix ids a draw references).

## Exact next action

Run `./build/native/melee_decomp_viewer --match` and watch the `[match]`
line: it flips from ~4 ms game to ~29 ms game at frame ~670.  Then add the
colored-vertex counter (or break on `decode_color` for frames 660 vs 720
and print the format) to confirm suspect 1; if it is NBT, apply the
`has_nbt` gate in `transform_vertex`.

## What I tried that did not work

- Drawing-texture/texgen/blend diff via `--dump-draws`: **identical** at
  660/720, so the visible draw list is not the cause.
- `perf` on the whole run: the fast phase dilutes it; attach after 12 s for
  the slow phase only (`perf record -p PID -- sleep 8`).
- Blaming the pacer: the `sleep=` field shows only 4-10 ms and the `cpu=`
  field doubles too.
- The `game` field initially included our pacing sleep; `game_start_ns` is
  now taken after the pacing block, so `game` is real game-side work.

## Open questions

- What changes in Link's model at frame ~670?  A screenshot of the glitch
  and the `[match] frame N` counter from the user would pin it — yes, but
  the deterministic run reproduces it offscreen, so not blocking.
- Is the glitch a *consequence* of the slow path (e.g., a vertex format
  misparse) or the *cause* (an HSD state change we mishandle)? — no human
  needed.

## Files touched / claimed

- `native/decomp/render/viewer_main.c` (instrumentation)
- `native/decomp/gx/gx_hle.{c,h}`, `native/decomp/gx/gx_gl.c` (P-628)
- `native/decomp/assets/hsd_convert.c` (v57, P-626)
- `src/melee/ft/types.h` (`FtStatusFlags`, P-625)
- `native/AI/*` docs

## Verification run

```
cmake --build build/native -j4                       # clean
ctest --test-dir build/native --output-on-failure    # 12/12
SDL_VIDEODRIVER=offscreen ./build/native/melee_decomp_viewer --match
  # [match] frame 660 ... game=~4ms
  # [match] frame 720 ... game=~29ms   <-- P-629
ASAN_OPTIONS=detect_leaks=0:allow_user_segv_handler=0 \
  ./build/native-boot-asan/melee_decomp_boot --boot-frames 600 \
  --boot-timeout 300 --boot-match 20                 # clean
```
