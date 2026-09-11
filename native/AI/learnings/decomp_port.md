# Full-game decompilation port: feasibility evidence (ADR-0010)

Written 2026-09-11 with the architecture pivot. This is the evidence behind
ADR-0010, not a plan; the plan lives in `ROADMAP.md` / `ROADMAP_DETAILS.md`.
Update this file as the census improves and patches land (ADR-0011 requires
every `src/` portability patch to be listed here).

## 1. Decompilation completeness (`build/GALE01/report.json`)

This is a local objdiff report against the retail DOL, not an estimate.

| Category | Functions matched | Code matched | Fuzzy |
|---|---|---|---|
| Game code | 17,699 / 17,704 (99.97%) | 99.01% | 99.995% |
| HSD code | 1,075 / 1,078 (99.72%) | 98.80% | 99.991% |
| Dolphin SDK | 837 / 837 (100%) | 100% | 100% |
| Gekko runtime | 209 / 209 (100%) | 100% | 100% |
| **Overall** | **19,820 / 19,828 (99.96%)** | 99.05% | 99.995% |
| Data | 1,211,168 / 1,211,168 (100%) | — | — |

The port therefore runs the retail game's logic, not a replica. The known
exceptions are small and enumerated:

- Incomplete units (5): `main/melee/gm/gmtoulib`, `main/melee/gr/grbigblue`,
  `main/melee/mn/mnsnap`, `main/sysdolphin/baselib/hsd_3B34`,
  `main/sysdolphin/baselib/hsd_3B5C`. All have source in `src/`; some functions
  are asm or fuzzy.
- Low-fuzzy functions (<99): `grBigBlue_801EE398` (2920 B, 99.0),
  `mnSnap_80257F24` (2588 B, 97.1), `hsd_803B3408` (868 B, 98.8),
  `fn_803B6820` (964 B, 98.8).
- `src/*.c` files containing Metrowerks asm: `hsd_397E.c`, `gm_1601.c`,
  `gmmain.c`, `grbigblue.c` (4 total).

## 2. GCC syntax census of `src/` (2026-09-11, after P-601 shim hardening)

Scratch probe: every `src/*.c` compiled with
`gcc -std=gnu11 -fsyntax-only -w -include native/decomp/shim/decomp_shim.h
-I native/decomp/shim -I src -I extern/dolphin/include` (add
`-I build/GALE01/include` for the two generated font includes).

Result: **1021 / 1034 files compile with zero errors.** The 13 excluded files:

- 12 `src/MSL/*.c` — the decomp's own GameCube libc; not compiled on PC (glibc
  provides the symbols), per ADR-0011.
- `src/sysdolphin/baselib/debug.c` — uses MSL `FILE` internals (`__io_proc`,
  `__idle_proc`, `__file_handle`); replaced by the port's OS/log layer.

Progress:

| Step | Clean files |
|---|---|
| Initial census | 834 / 1034 |
| `decomp_shim.h`: `<stdint.h>` + `ssize_t` guard; `Runtime/platform.h` shim neutralising `STATIC_ASSERT` | 954 |
| `stdbool.h` shim (`bool` = `int`, callback ABI) | 1019 |
| `-I build/GALE01/include` for the two generated font includes | 1021 |

Error classes found and their disposition:

- **180 × 32-bit `offsetof` static assertions** (`ToyED8Data` in
  `src/melee/ty/types.h`, 6 members × 30 TUs). True on the GC, false on
  64-bit; compile-time only. Neutralised by the `Runtime/platform.h` shim;
  never "fix" by moving fields.
- **59+ × `BOOL` (int) vs `bool` (`_Bool`) callback mismatches** in function
  pointer tables (`gr/types.h on_demo_init`, `grlib`, `grlast`, `gmscene`,
  `lbcardnew`, `itmewtwodisable`, `lbmthp`, ...). MWCC accepted them; GCC
  errors, and the x86-64 callback ABI differs for `_Bool` vs `int`. The
  `stdbool.h` shim defines `bool` as `int` for the port build so every TU
  agrees with `BOOL`; the GC build keeps C99 `_Bool`.
- **MSL/glibc conflicts** (`strtoul.h`, `_IO_FILE`, `fwrite`, `fpos_t`, MSL's
  32-bit `intptr_t`/`uintptr_t`): `<stdint.h>` force-include plus MSL
  exclusion.
- Per-file leftovers were only the two generated font includes and
  `debug.c` above.

### Shim inventory (`native/decomp/shim/`)

| File | Purpose |
|---|---|
| `decomp_shim.h` | Force-included: glibc `__ssize_t_defined` (platform.h's 32-bit `ssize_t`), `<stdint.h>` (host 64-bit `intptr_t`/`uintptr_t`) |
| `Runtime/platform.h` | `#include_next` the real header, then neutralise `STATIC_ASSERT` |
| `stdbool.h` | `bool` = `int` so callbacks match `BOOL` and the x86-64 ABI |

All three are port-only and are not on any prototype target's include path.

### Known exclusions / future work

- `debug.c` needs a port replacement for its MSL `FILE` internals (it is HSD's
  log/assert path). Not blocking S0.
- The `bool` = `int` decision changes `sizeof(bool)` in compiled structs;
  self-consistent because the whole game is compiled, but it must be revisited
  if any serialized structure contains `bool`.

## 3. Platform surface (unique symbols called from `src/`)

| API | Symbols | Notes |
|---|---|---|
| GX | 171 | renderer backend; 42 files call it |
| AX | 116 | audio HLE; only 3 files call it |
| OS | 63 | threads/timers/arena/interrupt stubs; 163 files, mostly `OSReport`/asserts |
| SI | 29 | controller/pad hardware |
| CARD | 24 | memory card/saves |
| AR | 17 | ARAM (audio streaming/tables) |
| VI | 13 | present/vsync/frame pacing |
| DVD | 10 | disc reads (the existing CISO reader becomes this) |
| PAD | 9 | input |
| AI | 4 | audio DMA |
| EXI | 2 | RTC/controllers |

No REL code modules are loaded at runtime; disc assets are data-only HSD
archives, so there is no executable relocation to emulate.

## 4. Runtime portability risks (not settled by the syntax census)

1. **Endianness.** Compiled code reads big-endian disc data directly. Needs a
   host-endian asset pipeline: structural u32/f32 words, FObj streams, display
   lists and textures each handled by format. The hand parser in
   `native/hsd/model.c` is the format oracle. ACGC does per-asset
   `SWAP_NONE/SWAP_U16/SWAP_U32/SWAP_VTX`; expect the same shape.
2. **64-bit layouts.** Self-consistent because the whole game compiles, but
   pointer-width assumptions need an audit: 127 `intptr_t`/`uintptr_t`, 2,137
   `(u32)`/`(s32)` casts, 1,273 bitfields in headers, 2 `#pragma pack` regions.
   Serialized formats (saves) stay 32-bit and are handled in the platform layer.
3. **PPC/MWCC semantics.** ~30 `__frsqrte`-class intrinsic call sites,
   `fabsf_bitwise`, float contraction differences vs MWCC. Use SSE, consider
   `-ffp-contract=off`, and differential-test physics against the DOL-derived
   reference where possible.
4. **Platform behavior.** 60 Hz tick, input polling order and RNG seeding must
   match the GC or frame data will drift even with correct code.

## 5. Reproduce

```sh
# full-tree syntax census
for f in $(find src -name '*.c'); do
  gcc -std=gnu11 -fsyntax-only -w \
      -include native/decomp/shim/decomp_shim.h \
      -I native/decomp/shim -I src -I extern/dolphin/include "$f"
done

# decomp accuracy report (requires a local decomp build)
python3 -c "import json;d=json.load(open('build/GALE01/report.json'));print(d['measures'])"
```
