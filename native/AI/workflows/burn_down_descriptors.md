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
   (currently 49) in `native/CMakeLists.txt`. If the tool says it has no DWARF
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

Worst families first, from `native/AI/TASKS.md` P-758. Start with its **head
item**, which is already diagnosed end to end — it is 82% of the `Pl*` gap.

### 2. Find what is missed

```sh
MELEE_UNWALKED=1 MELEE_NO_ASSET_CACHE=1 \
    ./build/native/test_decomp_assets "iso/<image>.ciso" 2>unwalked.txt >/dev/null
```

Each line is:

```
[unwalked] 0x<target> <- field 0x<field> words=N ptr=N conv=N cold=N
```

`[unwalked-file] <name>` lines separate the archives. **Sort by `cold`,** not
by count: `cold` is the number of words that are still big-endian, and a
descriptor with `cold=0` is already correct — giving it a walker would raise
coverage without changing a byte, which is gaming the metric.

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
