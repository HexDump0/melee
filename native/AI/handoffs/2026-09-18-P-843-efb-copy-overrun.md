# Handoff — 2026-09-18, P-843 (the EFB copy that overran its buffer)

Read `native/AI/AGENTS.md`, then G-220 and the P-843 row in `TASKS.md`.
This closes out the crash the previous handoff
(`2026-09-17-P-843-stale-fighter-procs.md`) left unproven.

## What the owner said, and why it was the whole answer

> "this happens like in the middle of a 1p vs match like in the vs screen
> (where the player and opponent is shown with vs) it happens there but not on
> all rounds like only in the middle round"

The Classic intro scene branches on `model_scale_kind`
(`gm_1832.c:fn_80186634`). Four of the five branches draw the splash and stop.
**Only `case 4` calls `fn_80185A0C`**, and `fn_80185A0C` is the only thing in
the scene that allocates EFB capture buffers and copies into them —
`lb_800121FC(&img[3], 0x17C, 0x190, GX_TF_Z24X8, 0)` and
`HSD_ImageDescCopyFromEFB(&lbl_804735E8.x88[i], 0x82, 0, 1, 1)`.

`model_scale_kind` is `sd->x00`, set in `gmclassic.c:876`: `entry->x1 & 8`
→ 4. In `gmClassic_803DDEC8` exactly one round carries that flag —
`{ 0x07, 0x08, 0, 0, 300, 10, 4 }`, the **team battle**, and the bonus stages
(`0x80`) sit either side of it. One round, in the middle. That is the
report, and it names one code path.

## The defect

`native/decomp/gx/gx_gl.c`, both 64-byte-tile encoders:

```c
ar = tile * 64 + (y % 4) * 4 + (x % 4);            /* dest[ar * 2] -- byte 128/tile */
gb = tile * 64 + 32 + (y % 4) * 8 + (x % 4) * 2;   /* dest[gb]     -- byte  64/tile */
```

The first half of the tile was a half-word index and the second a byte
offset. Stride 128 over a 64-byte tile, so the encode reached `tiles * 128`
where `GXGetTexBufferSize` promised `tiles * 64`: **the buffer was overrun by
its own size.** For the splash's 380x400 Z24X8 copy that is 607,904 bytes
past a 608,000-byte allocation, up to three times (one per costume slot,
clamped at 3), filled with the depth high/mid pair — `0xFF, 0xFF` wherever the
splash is at the far plane — 32 bytes in every 128.

That is the corruption five reports described and none could source: live
`Fighter`s and `HSD_GObj`s with `0xFFFFFFFF` in fields nothing assigns, in two
`HSD_ObjAlloc` pools at once, one to two frames after a heavy transition.
`fp->item_gobj = 0xFFFFFFFF` passes `if (fp->item_gobj)` in
`Fighter_8006A360`, `itGetKind` reads `+0x2C`, and `0xFFFFFFFF + 0x2C` wraps
to `0x2b` — the fault address in P-816 and in P-843's dumps.

Fix: both halves are byte offsets, and the second is now written as
`first + 32` so they cannot drift again.

## Why it survived ASan and three investigations

`HSD_MemAlloc` is `OSAllocFromHeap` on the game's own arena — **one** host
allocation. There is no redzone between the image buffer and the fighter
pools, so the sanitizer sees a clean run while the arena is being shredded.
Same blind spot as G-219's neighbouring global. And the write is nowhere near
the crash: the copy happens on the splash, the fault happens whenever a
fighter next dereferences the poisoned pointer.

## Verification

- `test_decomp_render --efb` gained a pass that copies 8x8 (four tiles) in
  both 64-byte-tile formats into a buffer with a 256-byte guard band, and
  round-trips the RGBA8 copy through `gx_texture_decode`, which is the
  authority for the layout. Against the old encoder it prints
  `wrote 256 bytes past the buffer` for both formats and a wrong texel 4.
- ctest **33/33**; the ASan build's `--efb` is clean.

## What the owner should see

The team-battle VS screen should no longer poison the match that follows.
RGBA8/Z24X8 EFB copies also decode correctly for the first time — every tile
but the first was previously landing on its neighbour — so if anything on
that splash looked subtly wrong, it should look right now.

`MELEE_GOBJ_WATCH=1` stays armed and is still the right first move if any
`0xFFFFFFFF` field turns up again: it now checks `ground_or_air`, `item_gobj`
and dead fighter gobjs on GX link 5. If it fires *without* passing through the
team-battle splash, there is a second writer and the leads in the previous
handoff (`Player_80031EBC`'s deferred free; `on_create_fighter[16]` indexed by
`alloc_info->unk8`) are where to look.

## Not done

- The other Z24X8 copy user, `gmregtyfall.c` (trophy fall, 490x480), had the
  same overrun and is fixed by the same change, but has not been exercised.
- `ftdemo.c`'s `plAllocInfo.x5` fix from the previous session stands on its
  own merits; it was never the SIGSEGV and the row no longer implies it was.
