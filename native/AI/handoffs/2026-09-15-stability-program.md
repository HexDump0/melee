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

138 of 861 archives are already at 100%.

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

## Suggested order

P-759 (soak) and P-757 (DWARF cross-check) are both cheap and independent --
either is a good first move. P-758 is the grind, and the coverage table above
says where to point it. P-760 is the biggest multiplier but the largest build;
do it once crashes stop arriving faster than they can be diagnosed.
