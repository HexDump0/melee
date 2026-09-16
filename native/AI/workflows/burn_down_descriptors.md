# Workflow: burn down unwalked descriptors (P-758)

This is the volume half of ADR-0023 phase 1. It is repetitive on purpose: the
four gates below make a wrong answer fail a test instead of shipping, which is
the only reason this work is safe to do at speed.

**Read `native/AI/AGENTS.md` first.** Rule 0 (never commit assets or the disc
image) and rule 0.1 (the decompilation under `src/` is the specification) apply
to every change here.

---

## What you are doing

The port converts every disc archive from big-endian to host order at load.
The converter walks the pointer graph and byte-swaps the numeric fields of each
struct it recognises. A struct it does not recognise stays big-endian, and the
game then reads garbage out of it — a count of 1 reads as 16,777,216, a scale
of 1.0f reads as 4.6e-41.

**You add walkers for the structs nobody walks yet.** 197,138 words of the disc
(about 770 KB) are still big-endian.

You will not be guessing what the structs are. The decompilation under
`decomp/src/` declares all of them, and the probes below tell you which ones
are missed and where.

---

## The four gates

Every change must leave all four green. They exist so that a wrong offset fails
here instead of in someone's game.

1. **`ctest --test-dir build/native`** — must be 32/32.
2. **Descriptor coverage** must go *up*, and you raise the floor to match:
   `MELEE_COVERAGE_FLOOR` in `native/tests/test_decomp_assets.c`.
   **Never lower it to make a change pass.**
3. **`decomp_layout`**, the DWARF cross-check. Every walker you add gets a
   `/* DWARF: <TypeName> */` comment above it, and you raise the floor
   (51 as of 2026-09-16) in `native/CMakeLists.txt`. If the tool says it has no DWARF
   for your type, add that type's header to `native/tools/dwarf_types.c` — an
   annotation naming a type the tool cannot find is a **failure**, not a pass.
4. **`decomp_soak`** stays green.

And one rule that is not a gate but will waste a day if you forget it:

> **Bump `HSD_CONVERTER_VERSION` in `native/decomp/assets/hsd_convert.c`
> whenever you change a walk.** The cache in `~/.cache/melee/assets` is keyed
> by it. Without the bump, stale entries answer instead of your code and you
> will chase ghosts (G-177, G-178).

---

## Working in parallel with another agent (read this first)

Another Claude session is fixing crashes from the P-759 soak matrix at the same
time, and **its fixes land in the same file as yours** -- three of the last four
were in `hsd_convert.c`. The split that keeps you both out of trouble:

- **You own `native/decomp/assets/hsd_convert.c` for adding walkers.** The other
  session has agreed not to edit it while you are running; if it finds a
  converter bug it will spec it into `TASKS.md` for you instead of editing.
- **You do not touch** `patches/src/**`, `decomp/src/**`, `src/**`,
  `native/decomp/boot/**`, or `native/tests/soak.sh`. Those are the other
  session's.
- **`HSD_CONVERTER_VERSION` is the one guaranteed conflict.** You both bump it.
  If you hit a merge conflict on that line, take the **higher** number and move
  on -- the value only has to increase, it does not have to be contiguous.
- **`git pull --rebase` before every commit**, and re-read
  `AI/agent_communication.md`. Claim your files there before you start.
- **Do not run the full soak matrix.** It is 754 runs and pins four cores for
  nine minutes, and the owner's machine thermally throttles. Use the targeted
  form in Verification below. The other session owns the full-matrix runs.

## Setup

```sh
cmake -S native -B build/native -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build/native -j8
ctest --test-dir build/native            # expect 32/32 before you touch anything
```

Baseline coverage — note it, you will compare against it:

```sh
MELEE_NO_ASSET_CACHE=1 ./build/native/test_decomp_assets "iso/<image>.ciso"
```

Two coverage lines come out. **Use the second one** (`descriptors=...`). The
first counts image data and vertex buffers, which are pointed at and correctly
never walked.

---

## The loop

### 1. Pick a target

Worst families first, from `native/AI/TASKS.md` P-758 — but read "What is
already settled" below before you trust the family sizes in that row. The head
item is **done** (converter v104-v106), and with it the `Pl*` family: what
looks like the biggest remaining number on the disc is 92% padding and
unreachable nodes. **`Vi*` is the live target.**

### 2. Find what is missed

```sh
MELEE_UNWALKED=1 MELEE_NO_ASSET_CACHE=1 \
    ./build/native/test_decomp_assets "iso/<image>.ciso" 2>unwalked.txt >/dev/null
```

Each line is:

```
[unwalked] 0x<target> <- field 0x<field> words=N ptr=N conv=N cold=N chg=N
```

`[unwalked-file] <name>` lines separate the archives. **Sort by `chg`.**

`cold` is the number of words still big-endian, and `chg` is the subset of
those a byte swap would actually change. A cold word whose bytes read the same
both ways — every zero word — is already correct however it was stored, so
converting it changes nothing. **Disc-wide, 75% of `cold` is that kind of
padding, and for `Gr*` it is 89%**, so sorting by `cold` sends you after
families that are mostly zeros. A descriptor with `chg=0` is unwalked only in
the bookkeeping sense; giving it a walker raises coverage without changing a
byte, which is gaming the metric.

`chg` is still an upper bound, not a worklist. It counts words that *would*
change, not words the game *reads*. See the `Pl*` entry under "What is already
settled" below for the case where those differ by 16,000 words.

**The honest disc-wide total is 31,629 changeable words** (2026-09-16, v106),
not the 197,138 at the top of this document, which counts every word of every
unrecognised struct including its padding and its extent overrun.

### 3. Identify the struct

```sh
MELEE_UNWALKED=1 MELEE_DUMP=0x<target>:24 MELEE_NO_ASSET_CACHE=1 \
    ./build/native/test_decomp_assets "iso/<image>.ciso" 2>&1 >/dev/null | grep dump
```

`PTR` marks a relocation field. Read the shape — pointers, small counts,
plausible floats — and match it against a struct in `decomp/src/`. Confirm
**field by field**, including the size: if the struct is 0x14 bytes, the
descriptor's extent should be 0x14.

Do not invent a struct. If you cannot name it in the decompilation, say so in
your handoff and move to the next item.

### 4. Find where the chain is broken

```sh
MELEE_UNWALKED=2 ...     # also prints [walked] descriptors
MELEE_FIND_PTR=0x<target> ...   # every pointer field aimed at that offset
MELEE_ROOT_TRACE=1 ...          # `root 0x<offset> <name>` for every public symbol
```

**The fix goes where the reference chain is broken, not where the data is.**
P-762's crash was in `pobj.c` and the bug was in `conv_itemdata`, four levels
up; the PObj walker had been correct all along, that PObj was simply never
reached. Before adding a new walker, check whether one already exists and just
is not called — `conv_aobjdesc` and friends are reached from ten places each.

A descriptor with **no referrer**, or two referrers and nothing above them, is
usually an element of an array whose base is the real root.

### 5. Write the walker

Copy the shape of the walkers already in `hsd_convert.c`:

```c
/* DWARF: HSD_ExampleDesc */
static void conv_example(Conv* c, uint32_t off)
{
    if (!in_data(c, off, HSD_EXAMPLEDESC_SIZE) || !mark(c, off)) {
        return;
    }
    conv_u32(c, off + 0x04);     /* count  */
    conv_u16(c, off + 0x08);     /* flags  */
    /* ... */
}
```

Rules, each of which is a bug someone already shipped:

- **Never `conv_u32` a pointer field.** On-disc pointers are relocation
  targets and the relocation pass already byte-swapped them; swapping again
  corrupts them. `decomp_layout` will catch this, but know why.
- **Never convert a byte stream.** FObj `ad` data, GX display lists, vertex
  arrays, pixel and TLUT data, FigaTree node bytes, and the `CMD_BE` command
  scripts are read raw and must stay big-endian. Swapping them trades a crash
  for silently wrong behaviour, which is worse (G-178).
- **Bound arrays by evidence, not by a guessed count.** Use `c->reloc[p]` to
  detect where a pointer array stops, or `next_pointed_at_after(c, off)` for an
  untyped object's extent. Walking one element past the end of a fighter table
  is what caused P-739 — it ran into the command scripts and broke every
  special move in the game. The `vis_table` walk in `conv_ft_data` is the
  pattern to copy.
- **Four-byte fields that are really four `u8`s stay put.** `HSD_FObjDesc`'s
  `type/frac_value/frac_slope/dummy0` at +0x0C is one word and four bytes;
  swapping it reverses them.
- **Bit-fields are not yours.** If a field is a bit-field group, leave it and
  note it — MWCC allocates the first bit at the MSB and GCC at the LSB, and
  that is handled elsewhere (`PORT_BF_BE`, G-180/G-181/G-188). Do not add
  `PORT_BF_BE` to anything, and never to the `Fighter` fp+594 union.

### 6. Verify, then commit

```sh
# bump HSD_CONVERTER_VERSION first
cmake --build build/native -j8
MELEE_NO_ASSET_CACHE=1 ./build/native/test_decomp_assets "iso/<image>.ciso" | tail -3
# raise MELEE_COVERAGE_FLOOR to the new number, and the decomp_layout floor
ctest --test-dir build/native
```

Then a **targeted** soak -- one stage, every fighter, four jobs:

```sh
MELEE_SOAK_JOBS=4 MELEE_SOAK_SEEDS=1 MELEE_SOAK_FIGHTERS=all \
  MELEE_SOAK_STAGES=31 native/tests/soak.sh ./build/native/melee_decomp_boot /tmp/soak-758
```

29 runs, about 40 seconds. Pick the stage your walker's data actually affects.
**Expect some failures**: the matrix is not green yet (81 of 754 fail as of
2026-09-16, in nine known bugs listed in `TASKS.md` as P-770..P-777). What
matters is that your change does not *add* one -- compare against the same
command run before your change, not against zero.

If coverage went up and all 32 tests pass, commit. One walker (or one family)
per commit, with the struct named and the coverage delta in the message.

**If `ctest` fails, do not commit and do not "fix" the test.** A failing gate
means the walker is wrong. Revert it and write down what you saw.

---

## What is already settled — do not re-derive

**`Pl*` is done, and the number that is left is not what it looks like
(measured 2026-09-16, converter v106).** P-758 opened with `Pl*` as the worst
family, 35,157 of the gap. After `conv_ft_part_anim` the 34 `PlXx.dat` still
report 16,773 unwalked descriptors — but only **17,325 changeable words**, and
**16,025 of those are one field in one struct that the engine never reads.**

Here is why, because it is the trap: `x8[i]` in `ftData_x1C` does not point at
the root of its `HSD_AnimJoint` tree. It points **into the middle of it**, at
the node for part `x0` — `root = x8[i] - x0 * sizeof(HSD_AnimJoint)`, which
holds for 317 of the 320 arrays on the disc. The nodes before it are the
earlier parts of the same serialized animation, **nothing in the archive points
at them**, and `ftAnim_GetNextAnimJointInTree` (`ftanim.c:26`) cannot reach
them: it goes child, then next, and pops upward only through its own stack of
nodes it already visited, which starts empty at `x8[i]`. So the traversal never
goes above the node it was handed.

Each of those unreachable nodes reports `cold=3` — `aobjdesc`, `robj_anim`,
`flags` — of which the first two are null. **One word per node can be wrong and
no one ever reads it.** Walking them would raise coverage by several percent
and change nothing, which is the definition of gaming the metric.

**So `Pl*` has roughly 4,500 changeable words genuinely left, not 20,597.**

**`Vi*` is not the answer either, and the reason is worth knowing.** It first
measured at 13,892 changeable words, which would have made it the densest
family on the disc. Almost all of that was one miscount: the report's extent
ran from a descriptor to the next thing anything *points* at, and a public
symbol is not pointed at. In `Vi1201v2.dat` the object at 0x4fae8 therefore
swallowed `visual1201v2Scene` and `ftDemoVi1201V2MotionFileGkoopa` and reported
4096 words, nearly all of them the motion file's **byte stream, which must
never be swapped**. Clamping the extent at the next public symbol drops `Vi*`
to **1,262** and the disc-wide total from 82,596 to 31,629.

That is the same mistake as the walker bound two paragraphs up, made in the
instrument instead of the walker. **Whenever you bound anything in this file by
`next_pointed_at_after`, ask whether a public symbol can fall inside the
range.**

**Current worklist, by changeable words:**

| family | descriptors | cold | chg | note |
|---|---|---|---|---|
| `Pl*` | 16,773 | 68,365 | 20,597 | ~16,025 of these are the unreachable `HSD_AnimJoint.flags`; ~4,500 real |
| `It*` | 3,651 | 15,318 | **7,430** | **the best target left.** `ItCo.dat`/`ItCo.usd` are 3,715 each, and one descriptor — 0x4ec8, 3013 words, 66 pointers — holds 2,368 of them |
| `Gr*` | 5,518 | 14,506 | 1,528 | 89% padding |
| `Vi*` | 1,096 | 3,333 | 1,262 | 62% padding |

Everything else on the disc is under 300 words each.

## When to stop and ask

Stop and write a handoff rather than guessing, if:

- you cannot name the struct in `decomp/src/`;
- coverage goes up but a test fails;
- the fix seems to need a change under `src/` or `patches/` (it almost
  certainly does not — this work lives entirely in
  `native/decomp/assets/hsd_convert.c`);
- you would have to lower a floor.

Coordinate in `AI/agent_communication.md`: add a claim row before you start,
and re-read it before each commit. Another agent may be in the same file.
