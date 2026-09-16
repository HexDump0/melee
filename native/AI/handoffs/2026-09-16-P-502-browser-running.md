# Handoff: the game runs in a browser — what works, what broke, what is next

**Date:** 2026-09-16
**Agent:** claude (opus-5, 1M), worktree `../melee-wasm`, branch `wasm`
**Toolchain:** Emscripten **6.0.9** (pinned), emsdk at `~/projects/emsdk`
**Branch state:** 8 commits ahead of master, tree clean, native ctest **33/33**

## Read this first

The written plan is **materially out of date, in our favour**. ADR-0025
scheduled gates W0–W4 with a measured spike between each, and put MEM1
rebasing first as "blocker #1". In practice most of it either already worked
or turned out unnecessary. Do not start from the ADR's ordering; start here,
then amend the ADR.

## What actually works, in a browser, today

Boots to the **memory card warning**, then the **title screen**. Audio plays.
Input responds. It runs **210 frames at 15–17 ms** (`game=3 ms render=3 ms`),
streaming a 1.4 GiB disc image off the user's local disk. It dies pressing
Enter at the title, on one alignment assertion (below).

| | |
|---|---|
| compile | 1,036 TUs, 0 failures |
| link | 0 undefined symbols |
| MEM1 at `0x80000000` | **reserved, not mapped** — 59 MB resident |
| disc | streamed by `Blob.slice`, **0 bytes in wasm memory** |
| sync reads from nested loops | Asyncify suspend/rewind, works |
| WebGL2 / audio / input | attached, unchanged from native |

## Reproduce it

```sh
source ~/projects/emsdk/emsdk_env.sh     # fish users: emsdk_env.fish
native/tools/wasm_census.sh /tmp/w       # compiles + links, ~1 min
cd /tmp/w && python3 -m http.server 8790
```

Open `http://localhost:8790/melee.html`, pick a `.ciso`, press Enter at the
title. **The page beams every line of game output to the dev server**, so read
the panic from the server log rather than the browser console — extensions and
console filters ate it three times:

```sh
tr -d '\0' < server.log | grep -oP 'GET /(L|ABORT)/\K[^ ]*' \
  | python3 -c "import sys,urllib.parse;[print(urllib.parse.unquote(l)) for l in sys.stdin]"
```

## The five things that made it work

1. **`ssize_t`** — `Runtime/platform.h` typedefs it `signed int`; musl says
   `long`. Identical types on glibc/i386, so nobody noticed; on wasm **every**
   TU failed. Guarded on `PORT_WASM`.
2. **`printf.h`** — the decomp includes MSL's; `src/MSL` is excluded from the
   build so glibc's has been silently answering. musl has none. Shimmed.
3. **`execinfo.h`** — glibc backtrace API, absent on musl. Shimmed.
4. **`-Wno-error=incompatible-function-pointer-types`** — clang errors where
   GCC warns; the native build already relaxes the GCC spelling.
5. **`-sMAX_WEBGL_VERSION=2`** — `gx_gl.c` calls `glBlitFramebuffer`, WebGL2
   only. The single remaining undefined symbol.

**`PORT_WASM` is layered on top of `PORT_PC`, not a sibling.** 60 patch files
are gated on `PORT_PC`; a sibling macro means editing all of them forever.

**Both shims `#include_next` the real header unless `PORT_WASM`.** The shim
directory is searched *before* the system one, so a header placed there is what
*every* build finds. The first `execinfo.h` would have silently turned the
port's symbolised crash triage into a no-op on the desktop while passing all
33 tests.

## MEM1: the rebase is not needed on desktop

ADR-0025 rejects keeping `0x80000000` because reaching the end of MEM1 needs
2,072 MiB. That is **address space, not RAM**. Linear memory is reserved and
committed lazily:

| | |
|---|---|
| peak resident (node) | **59 MB** |
| peak virtual | 9.6 GB |
| Chromium / Firefox / owner's browser | **all work** |

`os.c:map_gc_ram` therefore skips `mmap` under `PORT_WASM` and checks
`__builtin_wasm_memory_size` instead, so a refusal is a diagnostic rather than
a trap. Owner's decision: **desktop-first**; the rebase design stays on file as
the fallback and is not to be built until a device refuses.

**It has zero headroom** and that matters: MEM1 *begins* at exactly 2 GiB,
which was the historical browser cap, so the approach needs strictly more than
the number that used to be the limit. Mobile is untested and is the realistic
failure. Safari untested.

## Asyncify: measured, and it passes

| | |
|---|---|
| binary size | 6.8 MB → **18 MB** (2.7×) |
| frame time | **15–17 ms over 210 frames** |

Costs size, not speed. **The Web Worker + SharedArrayBuffer alternative can
stay on the shelf** — it would avoid Asyncify entirely and also dissolve the
`emscripten_set_main_loop_timing` complaint, but it needs COOP/COEP headers and
OffscreenCanvas, and there is now no performance case for paying that.

## Open, in priority order

### 1. `dest % 32 == 0` at `devcom.c` — the current crash

Fires at the title → menu transition. A DevCom DMA destination arrives
unaligned in the browser and not on the desktop. The arena is not the cause:
`OSSetArenaLo/Hi` are `0x80003000`/`0x81800000`, both aligned and identical on
both targets.

**A diagnostic is already in the build** (`patches/src/sysdolphin/baselib/devcom.c.patch`)
printing `DevCom UNALIGNED: file= src= dest= size= type=` before the assert.
Run the repro and read that line — `type` distinguishes a main-RAM destination
from an ARAM one, which is most of the diagnosis. Note `lbfile.c:126` picks the
type by `dst >= 0x80000000`, so an ARAM destination is a small offset, not a
pointer.

### 2. Three cross-TU function signature mismatches

`wasm-ld` type-checks function signatures across translation units; **no other
toolchain we use reports this**. It found three:

```
ftLib_800876B4     (i32) -> i32   vs   (i32) -> void
gm_801677E8
mnCharSel_802640A0
```

A caller expecting a return value from a function declared `void` reads
whatever is in the return register. GCC accepts all three silently. In wasm an
*indirect* call through a mismatched signature **traps** — that was a hard
blocker until `-sEMULATE_FUNCTION_POINTER_CASTS=1` was added.

**That flag is a diagnostic, not a fix.** It costs size (18 → 20 MB) and speed.
Fix the three declarations and remove it. Expect more of this class: the
linker only sees disagreements between *declarations*, not casts at call sites,
and this engine is full of callback tables.

### 3. Gate zero: `CMD_BE` bit order — still unfixed

emcc decodes a real subaction command as **opcode 4 instead of 17**. Pinned by
ctest `bit_order`, green on GCC, red on wasm. The port currently runs with
wrong command decoding, which is very likely implicated in bugs downstream.

**The obvious fix is disproven.** `scalar_storage_order` carries byte order
*as well as* MSB-first allocation; padding and reversing the field order gives
opcode 0 where the attribute gives 17, because a field spanning two bytes is
non-contiguous after a swap.

**The viable route, measured:** `sizeof(CmdUnion) == 4` and `NEXT_CMD` is
`++cmd->u`, so a subaction script is a **uniform array of 32-bit words** and
can be byte-swapped at load losslessly. With host-order bytes, pad-and-reverse
*becomes* correct. That work lands in `hsd_convert.c`, so coordinate — another
agent has been active there — and check whether G-178's "command streams stay
raw" still binds.

### 4. CP932 — not started

15 compiled files carry non-ASCII literals and clang rejects
`-fexec-charset=CP932`. Needs a build-time source view and a byte-level test
using the existing Shift-JIS probe.

### 5. Housekeeping

- Run a full `ninja` GameCube build before this branch merges. Equivalence was
  proved by preprocessor token stream, which is sound but is not a built binary.
- `MAXIMUM_MEMORY` is dead in the link line (only meaningful with
  `ALLOW_MEMORY_GROWTH`, which is off). Drop it rather than leave something
  that reads as load-bearing.
- Fold these numbers into ADR-0025 and mark it accepted-with-revisions; the
  ADR review is in `2026-09-16-P-502-W0-progress.md`.

## Things that will waste your time if you rediscover them

- `pkill -f <pattern>` matches the shell's own command line and kills your
  session. Kill by PID.
- Headless Chromium with a long `--virtual-time-budget` hangs the tool call.
  The owner testing manually in Firefox is faster; the server beacon means you
  still get the output.
- Truncating a log the server already has open leaves null bytes at the front
  and defeats `grep`. Use `tr -d '\0'` or a fresh file.
- The emscripten default shell runs `main()` on load. The game opens the disc
  almost immediately, so the harness must set `noInitialRun` and call
  `callMain` after a file is chosen — and export `callMain`.
