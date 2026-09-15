# Brief A: the symbol-adjacency bug class (G-176)

**You own `patches/src/**` and nothing else.** Another agent is working in
`native/decomp/assets/hsd_convert.c` at the same time. Do not edit it, do not
edit `native/decomp/**`, and do not edit `native/tests/**`.

Read `native/AI/AGENTS.md` first, then G-176 in `native/AI/gotchas/GOTCHAS.md`.
Claim your files in `AI/agent_communication.md` before you start, and re-read
that file before every commit.

## The class

The decompilation faithfully reproduces code that reaches one symbol by
offsetting from another:

```c
StageCallbacks* callbacks = (StageCallbacks*)((char*) &grVe_803E5348 + 0x44);
s32* base = (s32*) &grVe_803E5348;  ...  base[i + 170]
```

On the console this worked because the linker emitted those symbols
back-to-back at exactly those addresses. **The port builds with
`-fdata-sections`, so the adjacency is gone** and the read lands in unrelated
data. It produces no compiler warning: the cast is to a complete type, and GCC
cannot see that the object behind it is smaller.

This has now bitten seven times and is fatal when the bytes it reaches are
used as a function pointer.

## Worked examples — read both before starting

- `git show a6994701c` — P-767, Venom. `grVe_803E5348` is 0x38 bytes and
  `grVe_803E5380` the 0xC after it, so `+0x44` is exactly
  `grVe_StageCallbacks`. Every match on Venom segfaulted. The fix names the
  table under `PORT_PC` and leaves the console path alone.
- G-176 in GOTCHAS — `tydisplay.c`, the `TYDSP_*_TABLE` macros.

## Your work, in order

### 1. P-774 — four known sites, same file, same base symbol

`decomp/src/melee/gr/grvenom.c` around lines 1039, 1111, 1156 and 1431 do
`s32* base = (s32*) &grVe_803E5348;` and then index far past its 0x38 bytes,
e.g. `base[base[gp->u.venom.xC8 + 14] + 170]`.

Word 14 is byte offset 0x38, which is `grVe_803E5380[0]` — adjacent, so that
one is "correct" by accident. Word 170 is 0x2A8 away, several symbols out.

**The symbol names carry their console addresses.** `grVe_803E5348 + 0x2A8 =
0x803E55F0`, so look for a symbol named `*_803E55F0` or work out which static
in the file lands there by laying the statics out in order with their sizes.
Then name it under `PORT_PC`. Write the arithmetic in the comment, the way
P-767 does, so the next reader can check you.

If an expression genuinely reads *across* two symbols, do not force it into
one — say so in the task row and leave it. A wrong "fix" here is worse than
the bug, because it will look correct.

### 2. Sweep for the rest of the class

```sh
grep -rn "(char\*) *&\|(s32\*) *&\|(u8\*) *&\|(int\*) *&\|(u32\*) *&" \
    decomp/src/melee/ decomp/src/sysdolphin/ | grep -v PORT_PC
```

Most hits are innocent (`(u8*) &gp->u.foo.bar` inside one struct). You are
looking for a cast of the address of a **file-scope symbol** followed by
indexing beyond that symbol's own size. For each candidate, work out the
symbol's size and the largest offset used; if the offset exceeds the size, it
is an instance. Also grep for comments saying things are "emitted
back-to-back" — a doc comment claiming adjacency is a defect report.

File anything you find in `native/AI/TASKS.md` even if you do not fix it.

### 3. P-775 — Venom still crashes for 7 of 26 fighters

`SIGSEGV in grAnime_801C8138`, repro:

```sh
MELEE_NO_CARD=1 MELEE_RNG_SEED=0x838169d0 MELEE_MATCH_P0=15 MELEE_MATCH_P1=16 \
  MELEE_MATCH_STAGE=22 ./build/native/melee_decomp_boot \
  --boot-frames 900 --boot-timeout 90 --boot-match 20
```

Check whether P-774 fixes it before diagnosing separately — the four unfixed
overlays are in the same file. If it survives, get a real stack with gdb (see
Verification) and decide whether it is this class at all. **If it is not,
stop, write what you found in the task row, and do not fix it** — it may be a
converter bug, and that file belongs to the other agent.

## Verification — all of it, every commit

```sh
cmake --build build/native -j4
ctest --test-dir build/native                     # must be 32/32
cd decomp && python3 configure.py && ninja | tail -6   # must be 100.00% matched
```

**The GameCube match is the point of the `#ifdef PORT_PC` guard.** If it drops
below 100.00%, your change leaked into the console path — fix that before
anything else.

Then the stage that was failing, and a sweep of every fighter on it:

```sh
MELEE_SOAK_JOBS=4 MELEE_SOAK_SEEDS=1 MELEE_SOAK_FIGHTERS=all \
  MELEE_SOAK_STAGES=22 native/tests/soak.sh ./build/native/melee_decomp_boot /tmp/soak-a
```

Keep `MELEE_SOAK_JOBS=4`: the owner's machine thermally throttles at 8.

## The in-process backtrace lies about this bug class

When the PC lands in data, `[boot] backtrace` prints the nearest preceding
*exported symbol*, which is usually unrelated. P-767's said
`grYt_StageCallbacks+0x0` — Yoshi's Story, nothing to do with Venom. **Use gdb
for anything in this class:**

```sh
MELEE_NO_CARD=1 MELEE_RNG_SEED=... MELEE_MATCH_STAGE=... \
  gdb -batch -nx -ex 'handle SIGSEGV stop nopass' -ex run -ex 'bt 15' \
  --args ./build/native/melee_decomp_boot --boot-frames 900 --boot-timeout 300 --boot-match 20
```

## Stop and hand off rather than guess if

- you cannot determine which symbol a console offset lands on;
- the GameCube match drops and you cannot see why;
- the fix wants to touch anything outside `patches/src/**`.

Write a handoff in `native/AI/handoffs/` and update `TASKS.md`. One fix per
commit, imperative message, the offset arithmetic in it.
