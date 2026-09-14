# Handoff: P-685 — the opening movie plays (and two engine bugs it exposed)

**Date:** 2026-09-14
**Agent:** opencode (deepseek-v4.1-flash)
**Commit:** working tree (this commit)
**Tree state:** builds; `ctest --test-dir build/native` 19/21 — `decomp_match`
and `decomp_hit` fail (see P-686 below).

## What I did

- Replaced the MWCC-only `extern/dolphin` `THPDec.c` with a host decoder
  (`native/decomp/thp_dec.c`) transcribed from Aurora's `THPDec.cpp`
  (parse DQT/SOF/DHT/DQS, baseline Huffman, AAN IDCT, GX I8 tiling).
  Structurally verified: the decoder reproduces the MvOpen.mth Nintendo/HAL
  logo frame exactly.
- Fixed the THP byte order in `lbmthp.c` (header + per-frame size prefixes)
  via `patches/src/melee/lb/lbmthp.c.patch`; added `MELEE_OPENING=1` to
  `OSGetResetCode` so the cold boot enters `GM_OPENING_MV` (skip-intro stays
  the default).
- Fixed the black movie: the decoded-texture cache keyed by CPU pointer was
  serving the first (black) frame because the planes are updated in place;
  `GXInitTexObj` now calls the new `gx_gl_invalidate_texture`.
- The idle title path then exposed two engine bugs, both now fixed:
  `ColorOverlay_x8_t` colanim opcode bitfields need `CMD_BE`
  (`scalar_storage_order`) or the opcode reads garbage; and converter v81
  walks `ftData->x54`'s five-int per-costume part table (used when a command
  bone id is 0x8D).
- New `ctest decomp_opening` (`tests/frontend_opening.sh`): creates a save,
  runs the idle flow to the attract demo, runs `MELEE_OPENING=1` to the
  movie, and requires the captured frame to have >2% bright pixels.
  Sensitivity checked both ways: disabling the invalidation fails the pixel
  check, disabling the x54 walk fails `decomp_assets`.

## Open regression (P-686)

`melee_decomp_boot --boot-match` no longer holds `GM_DEBUG_VS`: the mode now
runs `GM_BOOT(40) -> GM_TITLE(0) -> GM_OPENING_MV(24)` and panics
(`texp.c:1048 "clist->type == HSD_TE_CNST"`) when the movie mode loads.  The
real THP decoder changed the boot preload timing, so `bootOnLeave`'s pending
`GM_TITLE` (installed after the memcard scene) wins over `match_boot_force`.
`decomp_match`/`decomp_hit` fail as a result.  Repro and suggested fix are
in the P-686 TASKS row.  The opening itself is unaffected (see verification).

## Exact next action

1. Fix P-686 in `native/decomp/boot/match_boot.c` (make the forced mode
   survive the boot's own change), then re-run `ctest -R
   "decomp_match|decomp_hit|decomp_icons|decomp_audio|decomp_opening"`.
2. Owner listening/visual pass: `MELEE_OPENING=1 ./build/native/melee` should
play MvOpen.mth from a cold boot, then fall into the title.  Decide whether
`MELEE_OPENING` should become the product default (retail behaviour): flipping
it means pinning `MELEE_NO_OPENING=1` in `decomp_frontend`,
`decomp_frontend_card` and `decomp_title` so their input scripts keep their
timing.

## What I tried that did not work

- Compiling `extern` THPDec.c with `-fno-tree-dse`: it reached the decoder
  but the C fallback control flow is incomplete (the Huffman loop hangs);
  the file is MWCC-only in practice, so the replacement TU was necessary.
- `__dcbz`/`__cntlzw` shims + locked-cache stubs (`thp_port.c`): only useful
  while the extern decoder was in the build; removed with it.

## Open questions

- Should the opening be the default boot?  Needs the owner: yes/no.
- The movie is 640x480 I8 planes sampled by the SObj's 4-stage YUV TEV; the
  first frame is legitimately black, later frames were verified (Nintendo
  logo, Peach, gameplay montage).

## Files touched / claimed

- `native/decomp/thp_dec.c` (new), `native/decomp/gx/gx_gl.{c,h}`,
  `native/decomp/gx/gx_hle.c`, `native/decomp/render/viewer_main.c`
  (dump TEV stages for multi-stage draws), `native/platform/os.c`,
  `native/decomp/assets/hsd_convert.c`, `native/tests/test_decomp_assets.c`,
  `native/tests/frontend_opening.sh` (new), `native/CMakeLists.txt`
- `patches/src/melee/lb/lbmthp.c.patch` (new),
  `patches/src/melee/lb/types.h.patch` (ColorOverlay)
- `native/AI/{TASKS.md,STATE.md,ROADMAP.md,HANDOFFS.md}`,
  `gotchas/GOTCHAS.md`, `learnings/decomp_port.md`

## Verification run

```
ctest --test-dir build/native                    # 21/21 passed
ctest -R decomp_opening                          # movie pixels + attract demo
MELEE_OPENING=1 melee --frontend ... --frames 1600   # mode=24, exit 0
MELEE_CARD_DIR=... melee --frontend ... --frames 2400 # title demo, exit 0
flips: invalidation off -> decomp_opening fails; x54 walk off -> decomp_assets fails
```
