# Handoff — 2026-09-17, P-843 (stale fighter procs on the Classic intro screen)

Read `native/AI/AGENTS.md`, then the P-843 row in `TASKS.md` and G-219.

## What landed

- **The crash dumper was mislabelling menu objects as fighters** (G-219).
  `HSD_GOBJ_CLASS_FIGHTER` is 4 and the menus use the same number; only
  `fp->gobj == gobj` tells them apart. Fixed in `match_boot.c`.
- **`MELEE_GOBJ_WATCH=<frames>`**, a sampled proc-queue integrity check, armed
  in `viewer_main.c`'s frontend probe list.
- **`ftdemo.c:initFighter` left `plAllocInfo.x5` uninitialised.** Fixed,
  `#ifdef PORT_PC`, mirrored to `patches/src/melee/ft/ftdemo.c.patch`.

ctest **33/33**. No `src/` behaviour change outside the PORT_PC gate.

## What is NOT done

**The SIGSEGV is not root-caused.** The `x5` defect is real and lives on the
crashing screen, but the value measured there is 0, which is in range for
`ft_8045993C[6]` -- so on the paths reachable here it corrupts one bookkeeping
bit rather than writing out of bounds. Do not write it up as the fix.

Leads, in order:

1. Run the owner's next report through `MELEE_GOBJ_WATCH=1`. Both invariants
   it checks are the ones that break; neither has ever been observed live.
2. `Player_80031EBC` frees both `player_entity[]` slots but relies on
   `Fighter_Unload` -> `Player_80031FB0` to NULL them, and `HSD_GObjFree`
   *defers* the free when the gobj is the one currently in a proc. I read
   through that interaction and did not find the hole, but it is where a
   stale entry would come from and it is not cleared.
3. `ftDemo_CreateFighter` indexes `on_create_fighter[16]` with
   `alloc_info->unk8`, and `vi0501.c:110` passes `i + 0xA` from a loop.
   Unchecked; not on the Classic intro path, but the same shape.

## Coordination

- `patches/src/melee/ft/ftdata.c.patch` belongs to **P-841** (melee-opus). A
  bound check in `ftData_800859A8` is the obvious defence-in-depth for the
  array this fix stops feeding garbage to, and it is theirs to add. The two
  tasks are the same subsystem: P-841's `fighter figatree over!` is
  `gFtDataList`, which is the object immediately below `ft_8045993C` in bss.
- `patches/src/sysdolphin/baselib/gobj.c.patch` belongs to **P-836**. If the
  watch shows a leaked proc, hardening `HSD_GObj_RunProcs` against it goes
  there, not here.

## Update — the owner's third report, with the watch armed

`MELEE_GOBJ_WATCH=1` ran (the dump header reads `frame 15788`, not `frame 0`,
which is how you know `match_boot_init` was reached) and printed **nothing**.
That negative is worth as much as a hit:

- the proc queue was intact — **no leaked proc**, so P-836's stated producer
  is not what is happening here;
- the crashing fighter passed `fp->gobj == gobj`, so it is a **live**,
  correctly allocated `Fighter`, not a freed or reused one;
- `x61C` was in range.

What the dump does say, identically in two consecutive reports (same fighter,
same `cambox=0x806f19ac`, same everything but one word) is that a live
`Fighter` has exactly three fields set to `0xFFFFFFFF`:

| field | offset | value |
|---|---|---|
| `ground_or_air` | +0xE0 | -1 (never legal: nothing in the game assigns it) |
| `gr_vel` | +0xEC | -nan |
| `item_gobj` | +0x1974 | 0xFFFFFFFF |

`item_gobj` is the one that kills it: `Fighter_8006A360` guards with
`if (fp->item_gobj)`, which `0xFFFFFFFF` passes, and `itGetKind` reads +0x2C
off it — `0xFFFFFFFF + 0x2C` wraps to **`0x2b`**, the fault address, in both
this report and P-816.

**These are written after creation, not left over from it.**
`ftDemo_CreateFighter` calls `Fighter_UnkProcessDeath_80068354`
unconditionally (ftdemo.c:161), which calls `Fighter_UnkInitReset_80067C98`,
which sets `item_gobj = 0`.

**And `0xFFFFFFFF` is not what a free looks like.** `HSD_ObjFree` puts the
pool's free-list link in the block's first word, which is a live pointer into
`fighter_alloc_data`. Dump entry `#0` — a real fighter gobj, `p_link=8`,
`remove_fn=Fighter_Unload_8006DABC` — had `fp->gobj = 0xffffffff`, and `#2`
had `user_data = 0xffffffff`. Both gobj memory and Fighter memory are being
**overwritten with 0xFF**, in two different `HSD_ObjAlloc` pools that
interleave in the arena (pools grow one object at a time, so a gobj at X and
a `Fighter` at X+0x60 is normal, not a clue).

### Landed this session in response

- The watch now also checks `ground_or_air`, `item_gobj` and, separately,
  **GX link 5 for dead fighter gobjs that no longer carry a proc** — which is
  how `#0` and `#2` slipped past the first version. Keyed on `p_link == 8`
  and `user_data_remove_func == Fighter_Unload_8006DABC` so menu widgets
  cannot trip it (G-219).

### Next

1. **Owner re-runs with `MELEE_GOBJ_WATCH=1`.** The new checks fire on the
   frame `item_gobj` or `ground_or_air` goes bad, which is the one fact five
   reports have never had: whether it happens during the match, at the
   transition, or on the intro screen itself.
2. Then find the writer. `0xFF` over a range is the shape of a fill or a DMA
   with a bad destination, not of a stray pointer store — and the crash lands
   1-2 frames after a heavy transition (`render=27.55ms swap=13.20ms`).
   G-146 has form here: an `HSD_ImageDescCopyFromEFB` with byte-reversed
   dimensions once asked for a 0x4000 x 0x4000 copy.
3. Not reproduced headlessly. `MELEE_INTRO_TEST` reaches `GS_INTRO_EASY` with
   demo fighters and stays clean for 1,800 frames; the Classic intro
   (`gm_1832.c:fn_80186634`) is a different entry and the harness cannot get
   there naturally — it has to win matches. A knob that advances Classic
   would probably close this out.
