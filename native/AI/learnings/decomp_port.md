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

## 6. S0 results (2026-09-11): the compiled HSD path runs on the host

Probe: `tests/test_decomp_hsd.c`, ctest `decomp_hsd`, built **32-bit**
(ADR-0012). Run from the repo root so it finds the disc; it SKIPs when the
image is absent.

```
decomp_hsd: PlMrNr.dat size=473522 data=467728 reloc=1423 public=2 extern=0 hand_symbols=2
decomp_hsd: S0a matched 2/2 symbols, 2 offsets correct
decomp_hsd: S0b root=PlyMario5K_Share_joint descriptors=61 objects=61 posed=61 world_worst=0 pose_failures=0
decomp_hsd: PASS
```

- **S0a (parse).** The decomp's own `HSD_ArchiveParse` parses a retail
  `PlMrNr.dat`, runs the 1,423-entry relocation pass, and its public-symbol
  table matches the hand parser exactly (names and data offsets).
- **S0b (load + pose).** The decomp's `HSD_JObjLoadJoint` builds all 61 JObj
  objects, and the world matrices after `HSD_JObjSetupMatrixSub` match a
  literal transcription of the port's `make_local_mtx` + `mtx_concat` math
  **bitwise** (worst error 0, 61/61). The go/no-go gate passes.
- **Endianness recipe used.** Swap the u32 words of the structural prefix
  (header + data section + relocation/public/extern tables), leave the symbols
  section as bytes. That makes descriptor pointers and SRT floats correct.
  Descriptor strings and u16 fields are corrupted by the word swap, so the
  probe nulls `class_name`/`dobjdesc`/`robjdesc`/`mtx` before loading. Full
  semantic conversion (strings, u16 fields, display lists, FObj streams,
  textures) remains S3.
- **Bootstrap required** (this is the seed of the S1 platform layer):
  an arena via `HSD_ObjSetHeap`, `HSD_VecInitAllocData` /
  `HSD_MtxInitAllocData` / `HSD_IDInitAllocData`, `JObjInfoInit()` for the
  class chain, plus `OSAllocFromHeap` / `HSD_GetHeap` / `OSReport` /
  `__assert` shims. `HSD_ObjAllocAddFree` divides by `data->size`, so a
  missing `*InitAllocData` is an immediate SIGFPE.
- **Closure finding.** Linking `HSD_JObjLoadJoint` drags in the whole display
  surface (GX/TEV/LObj/bytecode/Perf: 62 undefined symbols) because class
  method tables keep those functions reachable through function pointers, so
  `--gc-sections` cannot prune them. `native/decomp/sdk_math.c` implements the
  SDK math primitives for real (the pose check also validates `PSMTXConcat`
  bitwise); `native/decomp/hsd_port_stubs.c` provides **probe-only** no-op
  stubs for the display surface. Never link the stubs into a product target;
  S1/S2 replace them.
- **Pointer width.** The same probe on 64-bit truncates pointers in
  `archive.c:Locate` and cannot overlay the 4-byte-pointer descriptors; hence
  ADR-0012 (32-bit product build).

## 7. S1 starting point

First tasks, in order: (1) grow the platform layer out of the probe shims
(OS heap/log/assert, init sequencing, `gmmain` boot with stubbed GX/DVD/VI);
(2) replace `hsd_port_stubs.c` with real SDK math/GX-HLE increments; (3) keep
the probe as the regression test for the compiled data path.

## 8. ADR-0011 portability patches in `src/` (as of 2026-09-12)

Every entry is `#ifdef PORT_PC`-gated; the GameCube build and output are
unchanged.  The compiled-port targets define `PORT_PC=1`.

| File / line | Patch | Why |
|---|---|---|
| `src/sysdolphin/baselib/synth.c:107` (`HSD_SynthSFXSampleLoadCallback`) | `memmove` instead of `memcpy` for the `.ssm` group move | The retail `.ssm` layout makes the group copy's source and destination overlap (destination advances 8 bytes more per group); MWCC's memcpy tolerated it, glibc's is UB.  The copied fields are re-patched below, so behavior matches the console (G-064). |

Replacement TUs stay under `native/decomp/`: `sdk_math.c` for the Metrowerks
SDK math asm, `debug_port.c` for MSL `debug.c`, `sdk_math`/`hsd_port_stubs.c`
for the S0 probe.  No `extern/` file has been edited.

## S4 `src/` portability patches (P-620)

| File | Patch | Reason |
|---|---|---|
| `src/melee/ft/ftdata.c` (`ft_800852B0`, `ft_800852B0_Reset_ft_8045993C`) | `#ifdef PORT_PC` to reference `ftData_Table_Unk0`/`ft_8045993C` directly | The retail code computes both addresses by pointer arithmetic from `CostumeListsForeachCharacter` (`+5940`, `gFtDataList[Ft_Kind_Max]`), relying on GameCube data/BSS adjacency. On the host the writes clobbered `ftMObj.head.info_init` (Zeba/`MObj` class), crashing `hsdChangeClass`. |
| `src/melee/lb/lbanim.c` (`fn_8001E60C`) | under `PORT_PC`, write `fobj->next = NULL` only when `first != NULL` | The loop advances `track` only when it allocates, so a joint whose first track is obj_type 5/6/7 allocates nothing and `fobj` is uninitialized. On the console that writes a stale stack slot; on the host it faults (DK landing, frame 93). |
| `src/melee/cm/camera.c` (`Camera_ApplyQuake`) | under `PORT_PC`, read `cm_803BCB64` instead of `(&cm_803BCB18)->desc` | The cast relies on `cm_803BCB18/3C/50/64` being adjacent in declaration order; GCC reorders statics, so the host read unrelated data (aspect 0, viewport -30905) and the camera translation became NaN (G-081). |

### P-620 S4 follow-up patches (2026-09-12)

The archive action-command scripts are big-endian **bitfield** words (MWCC packs
MSB-first; GCC reads LSB-first, G-082).  Rather than repacking the data, the
command structs are marked with GCC's
`__attribute__((scalar_storage_order("big-endian")))` under `PORT_PC`, so bitfield
loads read the raw big-endian words with the console's field order.

| File | Patch | Reason |
|---|---|---|
| `src/melee/lb/types.h` | `#define CMD_BE __attribute__((scalar_storage_order("big-endian")))` (empty otherwise); the 76 `union CmdUnion` member structs use `struct CMD_BE <name> {` | Fixes every fighter/item action command (`ftAction_*`, `lbCommand_*`) without data conversion. Pointer-only members `Command_05`/`Command_07` are left unannotated (their words are host pointers fixed by `Locate`). |
| `src/melee/ft/types.h` | `struct CMD_BE gmScriptEventDefault` | The dispatcher reads the opcode through this struct. |
| `src/sysdolphin/baselib/synth.c` (`HSD_SynthSFXHeaderLoadCallback`) | under `PORT_PC`, when the target bank has less room than the group load, drop the group and run the load-queue completion | Audio is stubbed (S5); the ARAM bank sizes from `lbAudioAx_8002785C` can be smaller than a group in the port. Completing the queue keeps `HSD_SynthSFXWaitForLoadCompletion` from spinning. |
| `src/melee/gr/types.h` (`StageCallbacks`) | under `PORT_PC`, declare the `flags_b0..b7` aliases at bit positions 31..24 (`u32 pad_hi:24; u32 flags_b7:1; ... flags_b0:1;`) | Compiled tables initialize `flags` with `0x80000000`/`0x40000000`; MWCC's `u8` bitfields are MSB-first, so `flags_b0` is bit 31. `scalar_storage_order` does not reorder bitfields within a byte (G-084). |
| `src/melee/ft/types.h` (`Fighter.x21FC_flag`) | under `PORT_PC`, the field is `FtStatusFlags`, a union whose `b7..b0` aliases are declared in reverse so `b7` is host bit 0 | `fighter.c` sets the draw-enable flag with `x21FC_flag.u8 = 1` and `ftdrawcommon.c` reads it as `b7`. MWCC's MSB-first `u8` bitfields make that bit 0 = `b7`; GCC's LSB-first layout left `b7` clear, so no fighter model was ever drawn (G-091). Do not reverse the shared `UnkFlagStruct`: stage code mixes raw `u8` tests with its aliases. |
| `src/sysdolphin/baselib/hsd_4D11.c` (`hsd_804D1138`) | under `PORT_PC`, define the card work area as `u8[0x1510]` instead of `u8[0x10]` | `hsd_3A94.c` casts it to `CardContext` (0x1510); the console tiles `hsd_804D1138`, `hsd_804D1148` and `hsd_804D2348` contiguously, GCC may not. ASan caught the 0x20 write on every mode change; release clobbered adjacent globals (G-085). |

Platform-layer (not `src/`) fixes in the same batch:
`native/platform/complete.c` clears a completion slot before its callback runs
(heap-use-after-free when `DVDClose` cancels from inside a callback, G-086);
`native/platform/pad_card.c` implements the frame-indexed scripted `PADRead`.

## S5 `src/` portability patches (audio, 2026-09-12)

All `#ifdef PORT_PC`-gated; see `learnings/decomp_audio.md` for the formats.

| File | Patch | Reason |
|---|---|---|
| `src/sysdolphin/baselib/synth.c` (`HSD_SynthSFXSampleLoadCallback`) | addresses are read/poked as `(Hi<<16)|Lo` via `MELEE_PORT_AX_GET/SET_U16PAIR` instead of `*(u32*)(e+0x14)` | The record's loop/end/current fields are Hi/Lo u16 pairs. A u32 store on the little-endian host puts the low half in `Hi`; the console relies on big-endian adjacency. |
| `src/sysdolphin/baselib/synth.c` (`HSD_SynthSFXGroupDataReaddress`) | same pair handling for `q+0x14/0x18/0x1C` | Same reason (bank readdress after unload). |
| `src/sysdolphin/baselib/synth.c` (`HSD_Synth_80389334`, `HSD_Synth_8038B120`, `HSD_SynthPStreamHeaderCallback`) | `*(u32*) &HSD_Synth_80407FD8.ratioHi = x` becomes `MELEE_PORT_AX_SET_RATIO(HSD_Synth_80407FD8, x)` | `ratioHi`/`ratioLo` is a 16.16 value; the console writes it as a big-endian u32 over the two u16 fields. The host u32 store swapped the halves (pitch became 1/65536). |
| `src/sysdolphin/baselib/synth.c` (`stopRange`, `HSD_Synth_8038ADD0`) | `*(size_t*)&...currentAddressHi` becomes `MELEE_PORT_AX_GET_ADDR(...pb.addr)` | Same big-endian pair aliasing; the page-advance/halt logic compared a byte-swapped address. |

The `MELEE_PORT_AX_*` macros live in `native/decomp/shim/decomp_shim.h`
(force-included on the host only), so the GC build sees the original code.
