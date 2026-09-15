# Brief B: three converter-side crashes (P-765, P-769, P-770)

**You own `native/decomp/assets/hsd_convert.c` and `native/tests/test_assets_*.c`.**
Another agent is working in `patches/src/**` at the same time. Do not edit
`patches/src/**`, `decomp/src/**` or `src/**` — if a fix needs one of those,
stop and hand off.

Read `native/AI/AGENTS.md`, then
`native/AI/workflows/burn_down_descriptors.md` for the probe loop and the
rules about what may and may not be byte-swapped. Claim your files in
`AI/agent_communication.md` first and re-read it before every commit.

**Bump `HSD_CONVERTER_VERSION` whenever you change a walk** or the cache in
`~/.cache/melee/assets` will answer with stale data and you will chase ghosts.

## What you are fixing

All three are crashes the fighter x stage soak found. Each has a one-line
repro and a diagnosis already done — your job is to finish them, not to start
from scratch.

### P-765 — Fox, G&W and Kirby segfault on every stage (56 of 780 runs)

```sh
MELEE_NO_CARD=1 MELEE_RNG_SEED=0x838169d0 MELEE_MATCH_P0=2 MELEE_MATCH_P1=3 \
  MELEE_MATCH_STAGE=31 ./build/native/melee_decomp_boot \
  --boot-frames 900 --boot-timeout 90 --boot-match 20
```

`HSD_DObjSetFlags(dobj=0x40)` <- `ftParts_80074D7C` (ftparts.c:665) <-
`ftParts_800750C8` <- `ftDrawCommon_80081200` <- `lbShadow_8000F38C`.

At the crash the `TempS.x0` loop bound reads **184549376 = 0x0B000000, which
is 11 byte-swapped**, and the u8 index it then reads (202) runs off a
`dobj_list` of 116. So **a count inside `FtPartsVisLookup` / `TempS` is still
big-endian.** `conv_ft_vis_lookup` is the walker. Two suspect guards:

```c
if (temps != 0 && count > 0 && count <= 64) {        /* (1) */
    for (i = 0; i < count; i++) {
        uint32_t t = temps + (uint32_t) i * 8;
        if (!in_data(c, t, 8) || !mark(c, t)) break;  /* (2) */
        conv_u32(c, t + 0x00);
```

(1) A legitimately larger `count` skips the whole `TempS` loop, leaving every
`TempS.x0` unconverted. (2) `mark` returning false means *already visited*, and
`break` then abandons the rest of the array instead of skipping one entry —
if two lookups share a `TempS` region, everything after the shared entry stays
big-endian.

G&W's `model_num` is **11**, which matches the observed value exactly, and
Fox/G&W/Kirby are the three fighters with unusual model counts. Start by
printing, for `PlGw.dat`, the `model_num`, each per-model `count`, and which
of the two guards trips. Fix the one that actually fires; do not "fix" both
speculatively.

Be careful with (2): if `mark` legitimately means "another walker already
converted this", then `continue` is right and `break` is wrong — but verify
that rather than assuming, because converting something twice un-swaps it.

### P-769 — Mute City asserts `mplib.c:4804 "0"` (24 runs)

```sh
MELEE_NO_CARD=1 MELEE_RNG_SEED=0x838169d0 MELEE_MATCH_STAGE=10 \
  ./build/native/melee_decomp_boot --boot-frames 900 --boot-timeout 90 --boot-match 20
```

`mpJointUpdateDynamics(joint_id=4)` <- `mpLib_80055E24` <-
`grMuteCity_801F0D20` (grmutecity.c:1073).

The assert is the final `else` of a slope classification: it fires **only when
a collision line's two endpoints have `dx == 0` and `dy == 0`** — a degenerate
line. So this is bad vertex data or a bad vertex index on dynamic collision
joint 4 (Mute City's moving platforms), not a logic bug in `mplib`.

Next step: break at `mplib.c:4804` in gdb and print `temp->v0_idx`,
`temp->v1_idx`, and the two `groundCollVtx[...].pos` values. Two outcomes:
the **indices** are wrong (something upstream is unconverted) or the
**positions** are zero/garbage (the vertex array is unconverted). That tells
you which walker to look at. `GrMc.dat` is 85.3% walked, so there is room for
an unconverted structure.

### P-770 — Dream Land asserts `memory.c:55 "adr"` (22 runs)

```sh
MELEE_NO_CARD=1 MELEE_RNG_SEED=0x838169d0 MELEE_MATCH_STAGE=28 \
  ./build/native/melee_decomp_boot --boot-frames 900 --boot-timeout 90 --boot-match 20
```

`HSD_MemAlloc(size=1024)` fails inside `hsdAllocMemPiece(136)` <-
`HSD_JObjAlloc` <- `JObjLoadJointSub` <- `HSD_JObjLoadJoint` <-
`Ground_GetStageGObj(map_id=2)` <- `grOldPupupu_802108B4(2)`.

The HSD heap is **exhausted**, not corrupted by a bad pointer. Two candidates,
and one measurement separates them: **count the `HSD_JObjAlloc` calls before
the failure** (a gdb breakpoint with `commands`/`continue`, or a counter).

- Thousands of allocations -> an unconverted `child`/`next` field makes the
  joint tree effectively unbounded. That is a converter bug and it is yours.
- A few hundred -> the heap is legitimately too small for this stage. That is
  **not** a converter bug; write it up and hand it off rather than growing a
  heap to paper over it.

`GrOp.dat` is 90.5% walked. P-725 and P-749 are the same `HSD_ObjAlloc` family
and may share a cause — say so in the task row if you find one.

## Verification — every commit

```sh
# bump HSD_CONVERTER_VERSION first
cmake --build build/native -j4
MELEE_NO_ASSET_CACHE=1 ./build/native/test_decomp_assets "iso/<image>.ciso" | tail -3
ctest --test-dir build/native                     # must be 32/32
```

Then the case you fixed, across every fighter:

```sh
MELEE_SOAK_JOBS=4 MELEE_SOAK_SEEDS=1 MELEE_SOAK_FIGHTERS=all \
  MELEE_SOAK_STAGES=10 native/tests/soak.sh ./build/native/melee_decomp_boot /tmp/soak-b
```

Keep `MELEE_SOAK_JOBS=4`: the owner's machine thermally throttles at 8.

**On descriptor coverage.** It may legitimately go *down* when you stop a walk
that was wandering (that just happened in `363a14a36`, -0.01 pt). The floor has
a 0.05 tolerance for exactly that. **Never lower `MELEE_COVERAGE_FLOOR`** — if
a correctness fix costs more than the tolerance, stop and hand off, because
that is a big enough change to need a second opinion.

Add a regression check to `native/tests/test_assets_*.c` for each fix, in the
style of `check_stage_item_articles`: assert the *structural* property that was
violated, not that the game no longer crashes.

## Stop and hand off rather than guess if

- the fix wants to touch `patches/src/**`, `decomp/src/**` or `src/**`;
- you cannot name a struct in `decomp/src/`;
- a test fails and you would have to weaken it to pass;
- coverage drops by more than the tolerance.

Write a handoff in `native/AI/handoffs/` and update `TASKS.md`. One fix per
commit, imperative message, with the measurement that proved it.
