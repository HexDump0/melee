# Handoff — 2026-09-18, P-848/P-849 (Battlefield's shards)

Read `native/AI/AGENTS.md`, then G-222 and the P-848 row.

## What it turned out to be

Not the material alpha, which is where the first pass of P-848 pointed. One
`u16` **inside a GX display list** had been byte-swapped by the converter: a
`GX_DRAW_TRIANGLESTRIP`'s vertex count went from `0x0008` to `0x0800`, the
renderer read 2048 vertices out of a 1480-byte tail, and the leftover bytes
came out as 37 invented triangles across the screen.

The writer is `conv_orphan_matanim_trees` — the heuristic that accepts any
relocation target which "looks like" an unconverted `HSD_MatAnimJoint`. It
walked a `HSD_TexAnim` whose `+0x14` landed inside a `HSD_PObj`'s display
list. **This is the same heuristic and the same false-positive class that
P-830 already caught**, and two of the six affected archives (`GrTKb.dat`,
`GrTMs.dat`) are the exact pair `conv_imagedesc`'s comment names.

## The fix, and why it is where it is

P-830 guarded `conv_imagedesc`. This bug walked past that guard into something
else. So the guard now sits on the thing that must not be written: `conv_pobj`
records each display list's extent (`n_display << 5`, as `pobj.c:1293` calls
it) in `Conv::dl_span`, and `conv_u16`/`conv_u32` refuse to write inside one,
count it in `HsdConvertStats::dl_guarded`, and report the first refusal.

The file's header has always stated the invariant — "GX display lists ... stay
big-endian" — and nothing enforced it. Now something does, for every walker,
including ones not written yet.

Converter **v137**. Descriptor coverage unchanged at 83.13%; ctest 34/34;
Battlefield verified in the game (`MELEE_MATCH_STAGE=31`), not only the viewer.

## Six archives, not one

`GrNBa` (Battlefield), `GrTKb`, `GrTMs`, `GrTMt`, `GrTPk`, `GrTPr`. `GrTMs`
alone had six damaged lists — the first-refusal message understates it, so
read `dl_guarded`, not the message count.

## Tools this added, and how to use them

Three, in the order that cracked it (all in `workflows/debug_rendering.md`):

1. `test_decomp_render --hide-draw N` / `--only-draw N`. Sweeping `--hide-draw`
   and diffing the shots found the single draw responsible in one pass.
2. `--wire` first, always. It drew Battlefield perfectly *before* the fix,
   which ruled out the joint tree and the vertex arrays and pointed straight
   at the display list.
3. `MELEE_DL_TRACE=1` prints every primitive with the bytes it consumed. The
   vertex size is constant within a POBJ, so the malformed row names itself.

From there: a standalone driver that calls `hsd_asset_convert` on one archive,
under gdb with a watchpoint on the byte, gives the offending walker's whole
stack. That driver is four lines and worth rebuilding when the next one of
these turns up.

## Worth doing next

`conv_orphan_matanim_trees` has now produced false positives twice. The guards
contain the damage but the heuristic is still wrong; someone should measure how
many trees it accepts that no typed walk reaches, and whether the ones it
genuinely finds (P-753) can be reached another way.
