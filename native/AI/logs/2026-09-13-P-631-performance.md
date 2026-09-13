# P-631 follow-up: match rendering performance

Date: 2026-09-13

## Landed checkpoint

- The SDL viewer keeps Final Destination's `GX_CTF_R4` shadow copies resident
  on the GPU. The old live path synchronously read the EFB to the CPU, packed
  tiled I4 bytes, decoded them, and uploaded a GL texture twice per frame.
  The headless EGL byte-verification path retains the CPU fallback.
- The GPU blit temporarily disables the inherited GX scissor; without this,
  the destination texture is clipped to black. Dynamic I4 samples reproduce
  the old intensity/alpha interpretation in the TEV shader.
- The permanent VBO/VAO vertex layout is configured once instead of issuing
  16 attribute calls for every draw (roughly 220 draws in steady gameplay).
- Vertex conversion builds a decode plan once per primitive and avoids
  repeated component-count/size decisions for every vertex. Unused NBT
  transforms and texgens are skipped.

In an unthrottled frame-718 capture, ordinary render calls fell from roughly
3-5 ms to about 0.6-1.5 ms; the unusually large frame-82 render fell from
roughly 28-33 ms to about 16 ms. The corrected GPU-copy capture is visually
equivalent to the materialized I4 path (normalized RMSE 0.0029), and later
decoder planning is pixel-identical to that capture.

Paced spike counts on 2026-09-13 were noisy because the desktop was under
substantial concurrent CPU/I/O/GPU load. Use a quiet system for acceptance;
do not hide spikes by changing the logging threshold.

## Next optimization work (P-642)

`perf` identifies GX vertex processing as the remaining CPU concentration:
`exec_primitive`/`read_vertex`, endian/fixed-point `read_comp`,
`transform_vertex`, and `texgen_coord`. The strongest next design is to keep
object-space attributes and matrix indices in a GPU-friendly buffer and do
position/normal/texgen transforms in the vertex shader. A smaller alternative
is generated/specialized decoders for the few common descriptor formats.

A general decoded-display-list cache was prototyped and rejected: most live
fighter arrays are dynamic or unregistered, while hashing large GX state and
linearly searching entries made a paced 1200-frame run slower. Do not revive
that approach without a compact state key, O(1) lookup, immutable-array proof,
and before/after workload measurements.

All automated viewer commands must include `SDL_AUDIODRIVER=dummy` in addition
to an offscreen video driver.
