# Handoff: the browser port is playable — what was wrong, and what to do next

**Date:** 2026-09-16
**Agent:** claude (opus-5, 1M), worktree `../melee-wasm`, branch `wasm`
**Toolchain:** Emscripten **6.0.9** (pinned), emsdk at `~/projects/emsdk`
**Supersedes:** `2026-09-16-P-502-browser-running.md` and
`2026-09-16-P-502-gate-zero-and-signatures.md` on every point below.

**Renumbered at the merge:** this branch's P-796..P-800 collided with
unrelated mainline work, so they became **P-806..P-810** (gate zero,
signatures, DevCom alignment, CP932, itanimlist) and the branch's
G-190..G-195 became **G-193..G-198**.  Commits and notes dated before
the merge may use the old numbers.

## State

Owner-confirmed on Firefox with his own `.ciso`: boots, memory-card screen,
title, character select, **plays a match**. Roughly 1,700 frames in one sitting.

| | |
|---|---|
| gate zero (command bit order) | **fixed**, 122,884 field reads green on GCC *and* Emscripten |
| `wasm-ld` signature mismatches | 0 |
| browser wasm | **9.2 MB** (was 21.2 MB at `-O0`) |
| native ctest | 33/33 |
| GameCube `main.dol` | **byte-identical to retail**, 1130/1130 files |

**It is playable, not finished.** One stage, two characters, one session, one
browser. The desktop soak matrix fails **59 of 754** fighter x stage runs and
the browser runs the same game code, so all of those are present there too.

## Read this before you believe the older handoffs

Three claims in them are wrong and each cost real time today.

1. **"`-sEMULATE_FUNCTION_POINTER_CASTS=1` is a diagnostic; fix the three
   declarations and remove it."** It is not, and you cannot.
   `-Wincompatible-function-pointer-types` sees only *implicit* conversions and
   reports **one** site -- which is itself a false positive, round-tripping
   through the port's AR backend to a matching call.
   `-Wcast-function-type-strict` also sees explicit casts and reports **130
   across 49 files**. It is the decompilation's idiom for storing
   heterogeneous callbacks in one table (`(GObj_RenderFunc) (Event) fn`). The
   flag is now a recorded decision (ADR-0022, second amendment). G-195.
2. **"Byte-swap the subaction stream at load."** Disproved: `Command_05` and
   `Command_07` carry relocated host pointers inline, so a blanket word swap
   corrupts every jump. G-193.
3. **"The GameCube build is proved by preprocessor token stream."** It was
   never *run* -- `decomp/orig/GALE01/sys/` was empty. It runs now, and it
   caught a regression the token argument could not: `CMD_U(c)` expands to
   `((c)->u)`, token-equivalent, but it made MWCC reload a pointer the original
   had already loaded (`it_802790C0`, 99.71%). **Anything touching an
   expression MWCC compiles needs the real build.**

## What was actually wrong between the title screen and a match

All three were latent on *every* target; wasm just refuses to ignore them.

**A DMA destination x86 was aligning by luck (G-196).**
`static u32 hsd_SynthSFXLoadBuf[0x20 / 4];` is handed to `HSD_DevComRequest`,
which asserts `dest % 32 == 0`. The console gets that from MEM1 section
alignment, so the declaration never said it; a host compiler aligns a `u32[8]`
to 4 and wasm-ld gave it a 16-byte boundary. Now `ATTRIBUTE_ALIGN(32)`, here
and on `lbl_804C4540`. **Look for others**: any static passed as `dest` to
`HSD_DevComRequest`, `DVDRead*` or `ARQPostRequest`. Heap destinations are safe
(`HSD_MemAlloc` -> `OSAllocFromHeap` is 32-aligned by construction).

**130 mismatched function-pointer casts (G-195).** Three of them in `synth.c`
would have passed *garbage* rather than surplus arguments and got real
adapters; the rest are covered by the flag.

**The browser had never been optimised (G-197).** `wasm_census.sh` began as a
compile census and passed no `-O`. Two traps when you fix that:
`wasm-opt --fpcast-emu` miscompiles under `-O2` (validator:
`call* param number must match`), so emulation and `-O2` must be **separate**
`wasm-opt` passes; and clang's `-O2` at compile time is where most of the win
is anyway.

**24,000 GL calls a frame (G-198).** `upload_draw_uniforms` issues 52
`glUniform*` per draw, 461 draws. Native GL shrugs; a browser pays ~0.5 us a
call across the JS boundary, ~12 ms of a 15 ms render. A per-location shadow
copy now skips **94.8%**.

## How to build, serve and debug it

```sh
source ~/projects/emsdk/emsdk_env.sh
native/tools/wasm_census.sh /tmp/w          # compiles, links, runs gate zero
$HOME/projects/emsdk/upstream/bin/wasm-opt -O2 \
    --enable-bulk-memory --enable-bulk-memory-opt \
    --enable-call-indirect-overlong --enable-multivalue \
    --enable-mutable-globals --enable-nontrapping-float-to-int \
    --enable-reference-types --enable-sign-ext \
    /tmp/w/melee.wasm -o /tmp/w/melee.o2.wasm && mv /tmp/w/melee.o2.wasm /tmp/w/melee.wasm
cd /tmp/w && python3 -m http.server 8801 > srv.log 2>&1 &
curl -s -o /dev/null -w '%{size_download}\n' http://localhost:8801/melee.wasm
stat -c%s /tmp/w/melee.wasm        # these two MUST agree -- see below
```

> The `-O2` post-pass is not yet in `wasm_census.sh`; the script stops at the
> link. Fold it in when someone is confident it belongs there.

**Three ways the browser lies to you, all of which cost time today:**

- **Check the port is yours.** Previous sessions leave servers on 8741, 8752,
  8790. A backgrounded `http.server` on a taken port dies with
  `Address already in use` into its own log, and `curl` then answers **200 from
  somebody else's directory**. The owner ran a stale build and the result
  looked like a measurement. Compare byte sizes.
- **`OSReport` does not print** -- it fills a ring buffer and only echoes when
  `MELEE_LOG_REPORTS` is set, which a browser cannot set. Use
  `fprintf(stderr, ...)`; the page shell beams `printErr` to the dev server.
- **Print before you block.** `lbfile.c:waitForDisc` is a bare
  `do {} while (!discIsDone());`, which pins the only thread, so the `fetch()`
  beacons carrying your log cannot run. The last line that arrives is the
  operation that hung. G-194.

Ask the owner to run it; he plays it himself and that is faster than any
headless harness. Tell him to **hard-reload** -- the browser caches
`melee.wasm` at that URL.

## Switches worth knowing

| | |
|---|---|
| `MELEE_GX_UNI_CACHE=0` | force every uniform upload; a seeded match must stay **pixel-identical** either way |
| `MELEE_GX_UNI_STATS=1` | print the uniform-cache hit rate every 30 frames |
| `MELEE_LOG_REPORTS=1` | echo `OSReport` to stderr (desktop) |
| `MELEE_GX_UNI_*` | both live in `native/decomp/gx/gx_gl.c` |

## Next, in the order I would do it

1. **P-802 -- `glBufferData` per draw.** 461 vertex-buffer reallocations a
   frame, the largest remaining per-draw cost. One dynamic VBO with
   `glBufferSubData` at offsets. One function, no shader involvement.
2. **P-804 -- audio choppiness.** Reported against the `-O0` build with
   `frame=29ms sleep=0`, so measure on the optimised build first; it may
   simply have been the frame budget.
3. **P-805 -- soak the browser.** Everything verified so far is one stage and
   two characters. The obstacle is driving the build headlessly: the disc
   arrives through a `<input type=file>` picker, so a harness needs either a
   `File` synthesised from a fetch or a dev-only path in `wasm_shell.html`.
   Weigh that against just asking the owner.
4. **P-809 -- CP932.** Untouched. 15 compiled files carry non-ASCII literals
   and clang rejects `-fexec-charset=CP932`.
5. **Mobile and Safari.** Untried, and the 2.25 GiB `INITIAL_MEMORY`
   reservation is the realistic failure. The MEM1 rebase design stays on file
   as the fallback.

**Do not** build a UBO for the TEV state (P-803). It was the obvious sequel to
P-801 and the measurement killed it: with 94.8% of uploads already skipped the
upside is under a millisecond, against hand-matching `std140` packing.

## Things that will waste your time

- `pkill -f <pattern>` matches the shell's own command line and kills your
  session. Kill by PID.
- Run the game with `SDL_AUDIODRIVER=dummy SDL_VIDEODRIVER=offscreen`. The
  owner's speakers are the same speakers.
- The owner's machine is shared and interactive. Keep builds to `-j2` and ask
  before a multi-core sweep.
- `melee --match` needs `MELEE_RNG_SEED` pinned before two screenshots can be
  compared; without it the HUD damage differs and a clean change looks like a
  regression.
