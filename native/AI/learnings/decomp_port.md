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

## 2. GCC syntax census of `src/` (2026-09-11)

Scratch probe: every `src/*.c` compiled with
`gcc -std=gnu11 -fsyntax-only -w -include native/decomp/shim/decomp_shim.h
-I native/decomp/shim -I src -I extern/dolphin/include`. Results:

- **834 / 1034 files compile with zero errors** as-is.
- 200 files / 608 errors initially; **324 errors after adding `<stdint.h>`**.
- Error classes (after `stdint`):
  - 180 × one header's 32-bit `offsetof` static assertions (`ToyED8Data`,
    6 members × 30 TUs) — must be disabled/re-baselined on the port build.
  - 59 × `void (*)(int)` vs `void (*)(bool)` callback mismatches — `BOOL` is
    `int` in `dolphin/types.h`, `platform.h` uses C `bool`; needs one decision.
  - ~52 × `src/MSL/*` (the decomp's GameCube libc): `strtoul.h` missing,
    `FILE.buffer_len`, `_IO_FILE` redefinition, `fwrite` signature. MSL is not
    compiled on PC; glibc provides these symbols.
  - Remaining few: `ARQPostRequest` arity, `HSD_DevCom_*_bufs`, float case
    labels — per-file fixes, not categories.
- Error files by area: `gr` 68, `gm` 39, `mn` 20, `lb` 15, `MSL` 12, `if` 12,
  `vi` 11, `sysdolphin` 10, `ty` 4, `ft` 3, rest 1 each.

### Shim essentials

- `native/decomp/shim/decomp_shim.h` pre-defines glibc's `__ssize_t_defined` so
  `src/Runtime/platform.h`'s `typedef int ssize_t` does not clash with
  `<stdio.h>`. This is the only clash in the HSD include chain.
- Add `<stdint.h>` for `intptr_t`/`uintptr_t` (127 uses in `src/`). The MSL
  `stddef.h` defines them as 32-bit `int`; the PC build must use host 64-bit.
- Layout assertions (`ASSERT_SIZE`/`ASSERT_OFFSET`, 192 in `src/`) assert
  GameCube 32-bit offsets (`Fighter`-adjacent `ToyED8Data` is the loud one).
  Disable on the port build; never "fix" by moving fields.

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
