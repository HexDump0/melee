# Handoff: P-647 SisLib fonts — engine works, atlas extraction blocked

**Date:** 2026-09-13
**Agent:** opencode (deepseek-v4.1-flash)
**State:** tree clean at `42fdae55a`, all 15 ctests pass, product builds.
**Goal:** make the game's dynamic text (dialog messages, titles, names) render.

## TL;DR for the next agent

The real SisLib text engine is compiled and running **without crashes**. The
only remaining blocker is that the font **atlases are still all zeros** because
`platform_disc_load_file("sys/main.dol")` fails: **the DOL is not an FST entry**
(it is a raw region addressed by the disc header). Implement a DOL reader in
`native/platform/dvd.c` (details below), rebuild, and the dialog text should
appear. Everything else is already fixed.

## What works (committed)

- `src/sysdolphin/baselib/hsd_3A76.c` and `hsd_3915.c` compile for real
  (previously replaced by `platform/font_stub.c`, now deleted).
- `native/decomp/fonts.c` defines the two writable atlases
  (`HSD_SisLib_FontAtlas[287]`, `HSD_DebugFontAtlas[128]`) and fills them from
  the user's DOL via `platform_disc_load_file()`.
- The DOL byte ranges are correct:
  `HSD_DebugFontAtlas .data:0x804088B8 size 0x1C00` (128 x 0x38) and
  `HSD_SisLib_FontAtlas .data:0x8040CD40 size 0x23E00` (287 x 0x200)
  (`decomp/config/GALE01/symbols.txt`). Verified with a Python DOL parser:
  entries 0x1D/0x20/0xBD/0xCB have real pixels.
- SIS text buffers are **console big-endian** and the parser dispatches on the
  *first byte* (`>= 0x20` = glyph), so buffers must NOT be reordered. Patched
  all sis-buffer reads in `hsd_3A76.c` through `SIS_U16/S16/S32` accessors
  added to `sislib.h` (PORT_PC patch). Message text now parses correctly.
- I4 texture decode high nibble fixed (`native/gx/texture.c`): it was
  `v & 0xf0` instead of `(v >> 4) * 17`, so all I4 textures were ~1/16
  brightness.
- With the still-empty atlas, glyph quads for atlas-backed glyphs are fully
  transparent (alpha = TEXA = 0), while archive-backed font glyphs render.

## The blocker

`[boot] cannot read 'sys/main.dol' from disc: file not found` (run with
`MELEE_VIEWER_TRIAGE=1`). Evidence:

- The FST has 1209 entries and **no `main.dol`** (verified by temporarily
  logging `files[i].name` in `dvd.c`); the DOL lives outside the FST.
- GDB at `texture_for` shows atlas entries decode to
  `nonzero_alpha=0` (e.g. `0x56dc7c00` = `HSD_SisLib_FontAtlas + 0x3A00` =
  entry 0x1D), while the DOL-derived bytes have content.

**Fix (small):** in `native/platform/dvd.c`:

1. At mount, read 0x2C bytes of the disc header (currently 0x20) and store
   `disc_dol_offset = be32(header + 0x420)` (the standard GCM DOL offset; the
   FST offset/size are at 0x424/0x428, though the existing bi2 reads work).
2. In `platform_disc_load_file()`, special-case `"sys/main.dol"`/`"main.dol"`:
   read the DOL header (0x100 bytes) at `disc_dol_offset`, compute the total
   size from the 18 section headers (text offsets at 0x00/0x90, data at
   0x1C/0xAC), allocate and read the whole DOL with `disc_image_read()`, and
   return it. (There was a half-finished patch for exactly this reverted on
   purpose; `native/decomp/fonts.c` parses the DOL sections itself once it
   receives the buffer, so only the raw read is needed.)

**Acceptance:**

```sh
# no input script movement: the memcard error dialog stays up
SDL_AUDIODRIVER=dummy SDL_VIDEODRIVER=offscreen \
  ./build/native/melee --frontend --input native/tests/no_events.txt \
  --frames 400 --shot /tmp/dialog.bmp
```
Dialog text must be visible white (compare `convert ... -crop ... -auto-level`).
Then re-run `ctest --test-dir build/native` (expect 15/15) and commit.

## Debug aids used (not in the tree)

- `MELEE_VIEWER_TRIAGE=1` — un-silences the boot triage log in the viewer.
- `--dump-draws FRAME` — prints the captured frame's draws, textures, TEV
  stages and vertex data; the text draws are the 6-vertex quads with 32x32 I4
  textures (`tex0: ... fmt=0`), stage `cin=ZERO,ZERO,ZERO,C0`,
  `ain=ZERO,TEXA,A0,ZERO`, blend SRCALPHA/INVSRCALPHA.
- gdb scripts in `/tmp/opencode/` (`draws.log`, `uvdbg2.gdb`, `texdbg2.gdb`)
  are throwaway; the technique (break `gx_gl.c:1030`, inspect
  `frame_draws[]`/`frame_verts[]`/`rgba`) is useful and worth re-creating.
- To prove the quads rasterize, setting `u_tex_enable = 0` made solid white
  text quads appear; that debug hook was reverted.

## Dead ends (do not retry)

1. Rearranging SIS buffers to little-endian: the parser's `*cursor >= 0x20`
   dispatch then reads the low byte as an opcode and misparses. Use the
   BE accessors.
2. Normalizing font-table buffers at `HSD_SisLib_803A6368`/`HSD_SisLib_803A6478`:
   entries 0/1 are the kerning/texture tables, not messages; normalizing them
   corrupts the font. Reverted.
3. Inline vs deferred card callbacks: unrelated to fonts, but do not regress
   `platform/card.c` (P-646 deadlocks by design until fixed; cards are
   opt-in via `MELEE_CARD_DIR`).

## Files touched by this work

`native/decomp/fonts.c`, `native/platform/dvd.c`,
`native/platform/platform.h`, `native/CMakeLists.txt`,
`patches/src/sysdolphin/baselib/{hsd_3915.c,hsd_3A76.c,sislib.h}.patch`,
`native/gx/texture.c`.
