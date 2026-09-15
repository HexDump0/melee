# Handoff: the stability program (ADR-0023)

Written 2026-09-15 by claude (opus-5), at the end of the session that landed
P-751..P-756. Read this before picking up P-757..P-761.

## Why this exists

The owner asked a fair question: after a week of work the port boots, runs
matches, renders stages and plays audio -- but bugs kept arriving and there was
no way to know whether the tail was ten bugs or a thousand. The only detector
was him playing the game.

That is the actual problem. Not the bug count, the **absence of a denominator**.

## What changed

1. **The RNG was frozen** (P-751). `gmmain.c:156` seeds `HSD_Rand` once from
   `OSGetTick()`, and the port's timebase is a virtual counter starting at
   zero, so every boot replayed the identical game. Fixed by seeding from the
   host clock; `MELEE_RNG_SEED` pins it, and every ctest pins `tick`.
2. **Unfreezing it immediately exposed three latent crashes** (P-752, P-753,
   P-754) on one arbitrary seed. All three had been reachable for the whole
   life of the project. That is the evidence for P-759.
3. **Descriptor coverage now exists** (P-756) and is enforced on every
   `decomp_assets` run.

## The three bug classes (ADR-0023)

Do not treat "bugs" as one thing. They need different instruments:

- **A. Data representation** -- the converter never walked a struct, or walked
  it with the wrong layout. About half the volume. Instrument: coverage
  (P-756, landed), DWARF cross-check (P-757), generation (P-758).
- **B. Platform semantics** -- our OS/DVD/AX/GX behaves differently from
  console. Where the diagnosis hours actually go. Instrument: differential
  trace against the matched GameCube build (P-760).
- **C. Compiler/linker divergence** -- MWCC bit-field allocation, and
  `-fdata-sections` breaking the symbol adjacency the console linker gave us.
  Rare, weird, and **produces no compiler warning** in its worst form.
  Instrument: static scans promoted to tests (P-761).

## How to read the coverage number

```sh
MELEE_COVERAGE_JSON=/tmp/cov.json MELEE_NO_ASSET_CACHE=1 \
  ./build/native/test_decomp_assets "iso/<image>.ciso"
```

Two lines come out. Use the **second**:

```
coverage descriptors=209261 walked=154019 (73.60%) struct-roots=1964 unhandled=833
```

Raw target coverage (the first line, 59.10%) counts image data, display lists
and vertex buffers, which are pointed at and *correctly* never walked. A
"descriptor" is a target whose own first word is a pointer -- it points at
something else, so leaving it big-endian corrupts a graph rather than a
texture. That is the class-A bug surface.

`MELEE_COVERAGE_FLOOR` in `test_decomp_assets.c` is a ratchet. **Raise it when
coverage climbs; never lower it to make a change pass.** A walker deleted or a
root rule broken must fail a test, not a playthrough.

## Where the gap is (start here for P-758)

| family | files | walked / descriptors | gap |
|---|---|---|---|
| `Pl*` | 241 | 46158 / 81315 (56.8%) | **35157** |
| `Gr*` | 76 | 41352 / 53736 (77.0%) | 12384 |
| `It*` | 2 | 22788 / 26439 (86.2%) | 3651 |
| `Vi*` | 11 | 10 / 1106 (**0.9%**) | 1096 |
| `Gm*` | 58 | 11264 / 12082 (93.2%) | 818 |
| `Ty*` | 357 | 10445 / 11155 (93.6%) | 710 |

35 of the 758 archives that contain descriptors are fully walked; 103 archives
contain no descriptors at all.  A dashboard of the per-file numbers is published
at https://claude.ai/artifact/UDDq394MGXEKxEyxLHKuz1 (regenerate its data with
`MELEE_COVERAGE_JSON`).

35 of the 758 archives that contain descriptors are fully walked, and 103 archives contain no descriptors at all.

Two observations worth acting on:

- `Vi*` is at 0.9% -- an entire family essentially untouched. That smells like
  one missing root rule, so it is probably the cheapest 1,096 descriptors on
  the board.
- Several fighters sit under 20% (`PlGn` 5.5%, `PlKp` 8.9%, `PlZd` 9.7%,
  `PlGk` 13.8%, `PlYs` 15.0%) while `ItCo` is at 86%. That pattern suggests
  per-character `ftData` sub-structures are reached for some kinds and not
  others, rather than 241 independent problems. Find the shared cause before
  grinding file by file.

## Things that are settled -- do not re-litigate

- **Do not adopt `scalar_storage_order` for disc data** (ADR-0022). It is
  GCC-only, clang *silently ignores* it rather than erroring, and it forecloses
  WebAssembly. It also costs the pristine `src/` that ADR-0011 protects.
- **Do not put `PORT_BF_BE` on the `Fighter` fp+594 union.** It aliases one
  word as bytes *and* as a value from opposite ends; reordering it moves
  `x594_bits`, the mask deciding which bones an animation drives, and every
  fighter's skeleton comes apart. Tried, caught by the owner in the running
  game, reverted. See G-188.
- **`HSD_Rand` is console-exact** and must not be "fixed" (P-750, P-751).
- **The engine's synchronous loaders depend on inline completion delivery.**
  Holding deferred completions until a later pump deadlocks the boot at
  `mode=40`; `AXDriver_8038DA70` and `HSD_SynthSFXWaitForLoadCompletion` spin
  with no pump point inside the loop. See G-186.

## Order (revised, and the order is load-bearing)

**1. P-759 soak harness, and 2. P-757 DWARF cross-check.** Both cheap, both
gates. Then **3. P-758**, the burn-down. Then **4. P-760**, the console diff.
P-761 runs alongside whenever.

P-758 is 55,242 items of transcription -- ideal volume work for a **cheap
model**, and a poor use of an expensive one. But that is only true once a wrong
answer *fails a test instead of shipping*, which takes four gates: the coverage
ratchet (a walker cannot be silently deleted), the DWARF cross-check (an offset
cannot be silently wrong), the soak (a crash cannot be silently introduced),
and the existing suite plus the `100.00%` GameCube match. Without them a cheap
model raises coverage while corrupting data in scenes nobody tested -- worse
than not doing the work at all. **So do not start P-758 before P-757 lands.**

Division of labour that follows: expensive model designs the gates, diagnoses
novel crashes, builds the console-diff harness, and makes architecture calls;
cheap model runs soaks, triages duplicates, executes the burn-down, and writes
regression tests from template. Diagnosis is the expensive half -- the fixes
this session were one line (`<` to `<=`), two lines, and one scan function,
against about an hour of diagnosis each.

## Soak economics (measured 2026-09-15, not estimated)

A 900-frame headless match is **1.25 s on one core**: `melee_decomp_boot` runs
game logic with no renderer, so no GPU, no window, no display. The bug classes
above are logic and data bugs, not drawing bugs, so this finds them.

```sh
MELEE_NO_CARD=1 MELEE_RNG_SEED=<seed> ./build/native/melee_decomp_boot \
    --boot-frames 900 --boot-timeout 90 --boot-match 20
```

40 random seeds cost 50 seconds and found **P-762** (`pobj->u.jobj` at
`pobj.c:411`) at a 1-in-6 rate -- seven failures, all the same assertion. No
dedicated machine is needed. If one is wanted, the disc image can never go on a
public CI runner (AGENTS.md rule 0), so it must be self-hosted.

**Two traps for the harness:** the process exits **0** even when the assertion
fires, so grep the log rather than trusting the exit code; and dedupe by
assertion text, or one bug looks like seven.

## Definition of done

Four machine-checkable gates, not "no bugs": zero crashes across the soak
matrix; descriptor coverage >= 95%, ratcheted; console-diff identical game
state on the core scenarios; and the owner plays an evening without incident.
The first three need nobody to play the game.
