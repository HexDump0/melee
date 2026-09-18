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
