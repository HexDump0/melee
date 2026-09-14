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
  provides the symbols), per ADR-0011.  P-688/ADR-0018 added the one
  exception: `MSL/trigf.c` + `MSL/math_data.c` compile so `sinf`/`cosf`/`tanf`
  match the console tables.
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
| `src/melee/ft/types.h` (`Fighter.x21FC_flag`) | under `PORT_PC`, the field is `FtStatusFlags`, a union whose `b7..b0` aliases are declared in reverse so `b7` is host bit 0 | `fighter.c` sets the draw-enable flag with `x21FC_flag.byte = 1` and `ftdrawcommon.c` reads it as `b7`. MWCC's MSB-first `u8` bitfields make that bit 0 = `b7`; GCC's LSB-first layout left `b7` clear, so no fighter model was ever drawn (G-091). Do not reverse the shared `UnkFlagStruct`: stage code mixes raw `u8` tests with its aliases. |
| `src/sysdolphin/baselib/hsd_4D11.c` (`hsd_804D1138`) | under `PORT_PC`, define the card work area as `u8[0x1510]` and alias `hsd_804D1148`/`hsd_804D2348` onto it at +0x10/+0x1210 | `hsd_3A94.c` casts it to `CardContext` (0x1510); the console symbols overlap (command ring at +0x10, dispatch queue at +0x1210), and GCC will not overlap distinct arrays. G-085 sized the base; P-646/G-132 added the aliases after the split queue deadlocked the card pump. |

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

## S6 `src/` portability patches (frontend, 2026-09-13)

| File | Patch | Reason |
|---|---|---|
| `src/melee/gr/granime.c` (`grAnime_801C6F50`) | under `PORT_PC`, dispatch `AOBJ_ARG_A`..`AOBJ_ARG_AOTU` through the exact declared callback shapes (`(HSD_AObj*)`, `(HSD_AObj*, f32)`, `(HSD_AObj*, void*, u32)`, ...) | The retail dispatcher calls `func` through a common oversized prototype (`((Event) func)()` for `AOBJ_ARG_A`).  PowerPC keeps the first integer argument in `r3` across that call, so the no-argument callee still receives the `HSD_AObj*` the caller passed.  i386 cdecl passes every argument on the stack; `fn_801C6F2C` then saw the function pointer itself as `aobj` and Pokémon Stadium faulted during `grStadium_OnInit` (G-120).  The GameCube build keeps the original calls. |
| `src/melee/lb/types.h` (`spawn_hitbox_skip`) | under `PORT_PC`, declare the five flags as a single console-ordered `u8` (`pad:3; xF_b4; xF_b3; ...`) instead of the unaligned `u32` bitfield | The retail code tests bit 3 of byte 0xF (`lbz` + `extrwi. r0, r0, 1, 28`); MWCC packs the flags MSB-first from bit 7.  GCC placed the `u32` storage differently and `xF_b4` read bit 4, which is set in normal attack commands, so every `spawn_hitbox` took the skip path and no hitbox ever activated (G-122). |
| `src/melee/it/types.h` (`ItemAttr`) | under `PORT_PC`, declare the two flag bytes' fields in reverse order so GCC's LSB-first allocation lands them on the console bits (`x0_hold_kind:3; x0_78:4; x0_is_heavy:1; x1_8:1; x1_67_cam_kind:2; x1_5:1; x1_4:1; x1_3:1; x1_1:2`) | The article bytes are loaded verbatim from `ItCo.dat` and read as MWCC MSB-first bitfields: retail `itIsHeavy` is `extrwi ...,1,24` (0x80), `it_8026B30C` `extrwi ...,4,25` (0x78), `itGetHoldKind` `clrlwi ...,29` (0x07).  GCC's LSB-first layout gave every field the wrong bits, so heavy/hold/camera flags were wrong (G-123). |
| `src/melee/gm/gm_1601.c` (`gm_80168B34`, `gm_80168BF8`) | under `PORT_PC`, initialize `base = ckind` and `return gm_80168B34(...)` | Both decompiled functions relied on MWCC register leftovers: `base` is uninitialized on the `ckind <= CKind_Seak` fallthrough (retail keeps `ckind` in `r3`; GCC picked the Popo constant) and `gm_80168BF8` has no `return` (retail's `f1` survives the epilogue; GCC deletes the side-effect-free call and returns 0).  Every stock icon then requested frame 0 = Captain Falcon (G-129, P-644). |

Other S6 frontend patches landed with their own tasks (P-641 camera/results
tables, P-647 SisLib fonts, P-650 TexAnim); see `STATE.md`.

## S6 `src/` portability patches (opening movie, 2026-09-14, P-685)

| File | Patch | Reason |
|---|---|---|
| `src/melee/lb/lbmthp.c` | under `PORT_PC`, byte-swap the 0x40-byte THP header fields after the file read and the 4-byte packed-size prefix of every frame (`lbmthp_be32`) | The THP header and each frame record's leading size word are big-endian file data; the console reads them natively, the host reads raw bytes.  Without the swap `x_size`/`y_size`/`num_frames` are byte-reversed, the frame sizes are garbage and the player walks off the file (G-136).  The JPEG payload itself is byte-oriented and stays untouched. |
| `src/melee/lb/types.h` (`ColorOverlay_x8_t`) | under `PORT_PC`, mark the union's three bitfield member structs `CMD_BE` (`scalar_storage_order("big-endian")`) | Colanim scripts are archive data read MSB-first; the colanim opcode is `unk:6` of the command word.  GCC's LSB-first layout read a garbage opcode, dispatched out of `ftCo_803C6AD0[opcode - 0x15]` and crashed the title attract demo (G-136, same class as G-082). |
| `src/melee/ft/ftmaterial.c` (`ftMaterial_800BF534`, `ftMaterial_800BF6BC`) | under `PORT_PC`, copy the named `ftMaterial_803C69D0`/`ftMaterial_803C6A44` templates instead of fields past `ftMObj` through `struct ft_MObjInfo` | The DOL lays the two templates immediately after the smaller declared `ftMObj`. Host link layout changed when the THP decoder TU landed, so the cast read unrelated zeroed data and the first fighter material asserted `clist->type == HSD_TE_CNST` before spawn (P-686/G-137). |

`native/decomp/thp_dec.c` replaces the MWCC-only
`extern/dolphin/src/dolphin/thp/THPDec.c` (excluded from the PC build); it is
a C transcription of Aurora's `lib/dolphin/thp/THPDec.cpp`.

## Non-Metrowerks divergence census (2026-09-14, P-687..P-689)

Findings from reviewing upstream `doldecomp/melee#3456` (filed 2026-09-11 by
`alexscott2718-gif`; ribbanya applied the `portability` + `ai-assisted` labels;
no upstream PR; reference diff on
`alexscott2718-gif/melee:p0/native-build-fixes`).  The pin `40012f51f` still
contains all four upstream items; all were re-verified against the port build.
The tasks and exact fix specs are in
`handoffs/2026-09-14-P-687-native-divergences.md`.

| Divergence | Evidence in this tree | Task |
|---|---|---|
| `atanf` body is `#ifdef __MWERKS__` with no fallback (`src/melee/lb/lbtrigf.c:147-243`), so `atanf`/`atan2f`/`acosf`/`asinf` bind to host libm | `nm build/native/melee_decomp_boot` -> `U atanf@GLIBC_2.0`; six direct game callers incl. `ftcoll.c:2903` (quantised to degrees) | P-688 |
| `src/placeholder.h:13,16` maps `__frsqrte` to `sqrt(x)` and `__fabs` to `fabsf` | `__frsqrte` already fixed by `native/decomp/shim/placeholder.h` (G-094); `__fabs` still live at `generator.c:884` | P-688 |
| Big Blue: five `asm { rlwimi }` under `#ifdef MUST_MATCH` with no `#else` (`grbigblue.c:3250..3314`) | `grBigBlue_801ECB50` in the built binary has no bit-insert; the cars' 6-bit state byte is never written | P-687 |
| `gm_1601.c:3081` `fn_80166A8C` is an empty non-void function outside `MWERKS_GEKKO` | caller `gm_1601.c:2986` reads uninitialised `sp48_x` into `player_standings[i].xE` | P-687 |
| `Runtime/runtime.c:544` `__cvt_dbl_usll` is an empty non-void function on the host (port-only) | compiled symbol at `0x117a70` is a single `ret`; called from `gm_1884.c:784` (training speed) | P-687 |
| PlCo `pData[8]` respawn-platform joint+anim pair never converted (port-only) | `hsd_convert.c:2233` walks 0/4/5/16/20, not 8; `ft_0D4D.c:139,148` reads the pair | P-689 |
| `.nix` native target omits `src/MSL/trigf.c`, so `sinf`/`cosf`/`tanf` bind to host libm | port excludes all of `src/MSL/` per ADR-0011; 363 `sinf`/`cosf`/`tanf` call sites in game code | P-688 |

## P-688 math patches (2026-09-14)

Owner decisions and differential evidence are in ADR-0018.  All patches are
`PORT_PC`-gated; the GameCube build and output are unchanged.

| File | Patch | Reason |
|---|---|---|
| `src/melee/lb/lbtrigf.c` (`atanf`) | compile the body under `PORT_PC` and define `__fnmsubs(a, c, b) = -fmaf((a), (c), -(b))` | The body was `#ifdef __MWERKS__` with no fallback, so the host silently linked glibc's `atanf` (`atan2f`/`acosf`/`asinf` call it): 3.8% of 54.6M sampled inputs differ.  `ctest decomp_trig` compares the compiled function against an explicitly-rounded transcription and fails on glibc (`FAIL 2091032/54590184`). |

The rest of P-688 needs no `src/` edit: `native/decomp/shim/placeholder.h`
now maps `__fabs` to `fabs` (upstream's `fabsf` narrows the double at
`generator.c:884`), and the owner adopted upstream item 4 for the port by
compiling `src/MSL/trigf.c` + `src/MSL/math_data.c` with
`native/decomp/msl_port.c` (`fabsf__Ff` plus a constructor that runs
`__sinit_trigf_c`, because `SECTION_CTORS` is empty off-Metrowerks — G-143).
Full evidence table in ADR-0018.

## P-687 non-Metrowerks asm/empty-body fallbacks (2026-09-14)

Upstream #3456 items 3a/3b plus the port-only `__cvt_dbl_usll`; the ADR-0011
addendum records why these use `#ifdef PORT_PC` instead of upstream's
`#ifndef __MWERKS__`/`#else`.  With all patches applied, `ninja` in `decomp/`
reports `build/GALE01/main.dol: OK` (100.00% matched, 1130/1130 linked).

| File | Patch | Reason |
|---|---|---|
| `src/melee/gr/grbigblue.c` (`grBigBlue_801ECB50`) | the five `asm { rlwimi byte, st_val, 2, 24, 29 }` blocks gain `#elif defined(PORT_PC)` with `byte = (byte & ~0xFC) | ((st_val & 0x3F) << 2);` | The asm sat under `#ifdef MUST_MATCH` with no fallback, so the host store was a no-op and the cars' 6-bit state byte could never become 10 (closest car) or 4.  `objdump` of the port now shows five `and $0x3` + `or $0x28`/`or $0x10` insert sites. |
| `src/melee/gm/gm_1601.c` (`fn_80166A8C`) | `patches/src/melee/gm/gm_1601_ml_fallback.patch`: under `PORT_PC`, store `src->x` to `dst->x` and return it | The body was `#ifdef MWERKS_GEKKO` with no fallback, so the function was empty; the caller `gm_80166378` read the uninitialised `sp48_x` into `player_standings[i].xE` (results screen). |
| `src/Runtime/runtime.c` (`__cvt_dbl_usll`) | under `PORT_PC`, `return (u64) x;` after the asm block | Empty non-void body compiled to a bare `ret`; `gm_1884.c:784` passes the result to `lb_80019880` (training-mode speed).  Mirrors the in-file `__cvt_fp2unsigned` fallback directly above. |


## P-695 missing-`return` census (2026-09-14)

Owner report: every 1P stage segfaulted one frame after the "GAME!!"
announcer, in `lb_800138D8` with `gobj == 0`, reached from `gmvs.c`'s
`fn_8016D634` -> `gmregclear.c`'s `fn_80180630`.  The cause is a decompiled
function with **no `return` statement at all** (`lb_800138EC`,
`HSD_GObj*`), which is undefined behaviour: MWCC leaves the useful value in
`r3`/`f1`, GCC returns whatever is in `eax`/`xmm0`.  Same family as G-129
(`gm_80168BF8`).

### How to run the census

`melee_decomp_game` compiles `src/` with `-w` (ADR-0011: `src/` must not be
modified, so upstream warnings are suppressed), which hides this class
completely.  Re-compile the same command lines with `-Wreturn-type`:

```sh
cmake -S native -B build/native -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
# for every melee_decomp_game entry in compile_commands.json:
#   replace -w with -Wreturn-type, -o <obj> with -o /dev/null, keep -c
```

**Do not use `-fsyntax-only`.**  It disables the CFG pass, so only the
parse-time "no return statement in function returning non-void" fires and the
far more common "control reaches end of non-void function" is missed: 8 sites
instead of 45.

Census at pin `40012f51f`: **45 sites** (43 in `src/`, 1 in
`extern/dolphin`, 1 is `gmmain.c`'s `main`).

### How to decide what a site should return

The decompilation is 100% matched, so the retail DOL is the authority.  Build
it (`ninja` in `decomp/`) and read the epilogue:

```sh
decomp/build/binutils/powerpc-eabi-objdump -d \
    --start-address=0x<sym> --stop-address=0x<next> decomp/build/GALE01/main.elf
```

Three outcomes, and only the first two justify an edit:

1. **`r3`/`f1` provably holds a specific value on the fall-through path** ->
   port that expression.  This is a faithful port, not a guess.
2. **The value is indeterminate in retail too, but the path is unreachable and
   a consumer would dereference it** -> return the value the loop condition
   already guarantees (`NULL`), which is strictly safer than either garbage.
   Say so in the comment.
3. **Indeterminate in retail and the path is unreachable** -> leave it.
   Inventing a default is a reinterpretation (AGENTS.md §0.1).

### Reachability evidence

Every one of the 45 sites was instrumented with a one-shot `puts` just before
the closing brace and the real flows were run (`decomp_match`, `decomp_hit`,
`decomp_icons`, the new `decomp_gameover`, and the retail `--frontend` flow
through title -> menu -> CSS -> SSS -> match).  Only two ever fired:
`lbspdisplay.c:753` and `extern/.../axfx/delay.c:94`.

### Patched (6)

| Site | Function | Retail `r3` on the fall-through | Fix |
|---|---|---|---|
| `src/melee/lb/lbspdisplay.c:753` | `lb_800138EC` | **`gobj`**: `GObj_SetupGXLinkMax` (`0x8039075c`) and `GObj_GXReorder` (`0x8039063c`) only read through `r3`, never write it | `return gobj;` — **the owner-reported crash** |
| `src/melee/sfx/crowdsfx.c:512` | `un_803224DC` | **`un_8032201C`'s result** (`0x8032257c: bl`, then straight to the epilogue) | `return un_8032201C(spawn_id, cat);` |
| `src/melee/sfx/crowdsfx.c:535` | `un_80322598` | **`un_8032201C`'s result** (`0x8032260c: bl`, then the epilogue) | `return un_8032201C(arg0, cat);` |
| `src/melee/ft/ftanim.c:800` | `ftAnim_8006F994` | garbage (`r3 = &joint` from `0x8006fa20`, then clobbered by `ftAnim_GetNextJointInTree`) | `return joint;` (`NULL` by the loop condition); both consumers drive `while (joint != NULL)` with it |
| `src/melee/lb/lb_00B0.c:745` | `lb_8000CDC0` | garbage (`HSD_LObjGetFlags`' flags word) | `return cur;` (`NULL` by the loop condition) |
| `src/melee/gm/gm_1798.c:484` | `fn_8017A318` | `gobj` when `slot != 0`; `fn_8017A078`'s inner camera GObj when `slot == 0` | `return gobj;`.  Matches retail on the `slot != 0` path; the one caller stores it in `ResultsPlayerData::camera`, which nothing in the game reads back |

### Live on the host, currently correct by accident (1)

`extern/dolphin/src/dolphin/axfx/delay.c:94` `AXFXDelayInit` fires on every
boot (`axdriver.c:1005` tests `== 1`).  On the console `r3` holds
`AXFXDelaySettings`' `1`; at `-O2` GCC emits a tail `jmp` to the same
function, so `eax` is also `1`.  `extern/` is never edited (AGENTS.md §0), and
there is nothing to fix today — but this is one inlining decision away from
silently dropping the AUX delay effect.  If AXFX delay ever goes missing,
check this first.

### Left alone (38)

Retail is indeterminate too and the path is unreachable, so there is nothing
faithful to port.  The interesting ones, with the reason:

| Site | Function | Why it is left |
|---|---|---|
| `ft/ftanim.c:571` | `ftAnim_8006F3DC` | `f1` is **never written** on the not-found path (`0x8006f468`), so retail returns the caller's `f1`.  Needs every fighter part to lack an `HSD_AObj`; never observed |
| `it/kinds/itsscope.c:137` | `it_80291DAC` | `r3` still holds the incoming `gobj` pointer at `0x80291f04`; retail returns a pointer as a charge level.  Needs `xD4C > 0` but smaller than level 1's cost |
| `mn/mnmain.c:1761` / `:1713` | `mn_8022C010` / `mn_8022BFBC` | the `switch` covers `MENU_KIND_MAIN..MULTI_VS` (0..33); only `MENU_KIND_34` is missing and no code ever assigns it.  `mn_8022BFBC` only ever sees `mn_8022C010`'s 0..4 |
| `mn/mnstagesw.c:227` | `mnStageSw_80235C58` | the final `for (i = 1; found; i++)` never clears `found`, so the end is unreachable by construction (it spins instead) |
| `pl/pltrick.c:27` | `pl_80037B2C` | the only caller (`plbonus.c:494`) passes `k` in `1..0x10`, always `< 0x64` |
| `ty/toy.c:1531`, `it/kinds/itsscope.c:95`, `mn/mndiagram2.c:581`, `mn/mnname.c:851`, `mn/mndiagram.c:1184` | — | `switch`/loop covers every value any caller passes |
| `gm/gm_1601.c` x4, `gm/gmresult.c:344`, `gr/grcorneria.c:1704`, `gr/gricemt.c:1607`, `it/itzako.c:237`, `it/kinds/itarwinglaser.c:297`, `it/kinds/itlinkarrow.c:146`/`:305`, `it/kinds/itmewtwoshadowball.c:112`, `it/kinds/itkyasarinegg.c:136`, `it/kinds/itkusudama.c:195`, `ft/kinds/ftPopo/ftpopospecialhi.c:126`, `pl/player.c:1700`, `if/soundtest.c` x8 | — | declared non-void but no caller reads the result ("fake return type" in the decomp's own comments) |
| `Runtime/Gecko_setjmp.c:38`, `Runtime/__va_arg.c:58` | — | bodies are `#ifdef MWERKS_GEKKO` only; dead on the host (GCC lowers `va_arg` itself) |
| `gm/gmmain.c:220` | `main` | renamed `gm_main`; `run_match` ignores the result |

Re-run the census after every submodule re-pin; upstream may add or remove
sites.


## P-700 libc strictness: `vsnprintf(buf, -1, ...)` (2026-09-14)

The SIS text engine builds every string with

```c
vsnprintf((char*) buffer, -1, fmt, args);   /* hsd_3A64.c x2, textlib.c */
```

`-1` means "unbounded" to the console's MSL.  glibc documents sizes above
`INT_MAX` as unsupported and writes **one byte fewer** than asked, on 32- and
64-bit alike.  Because the engine's strings are Shift-JIS (two bytes per
Latin letter), losing the last byte truncates mid-character; the SJIS lookup
in `HSD_SisLib_803A67EC` then finds nothing for the orphaned lead byte, emits
no glyph, and the renderer runs on into whatever follows.  "VERY EASY" came
out as "VERY EASE••" (G-149).

Patched to `sizeof(buffer)` under `PORT_PC` — every destination is a fixed
local array, so the bound is exact.

**This is the class to sweep next.** The console's MSL was laxer than glibc in
several places, and the failures are silent:

```sh
grep -rn "vsnprintf\|vsprintf\|snprintf\|strncpy\|memcpy" decomp/src \
    | grep -E ", *-1|, *~0|0xFFFFFFFF|SIZE_MAX"
```

After P-700 the three known sites are fixed; re-run the grep after every
submodule re-pin.


## P-703 source encoding: the decomp is UTF-8, the console is Shift-JIS

`src/` stores the game's non-ASCII literals as UTF-8 — the character-name
tables in `gm_1601.c` (`"Ｍａｒｉｏ"` = `EF BC AD ...`), menu strings, and so
on.  The GameCube build converts them with **sjiswrap**
(`decomp/configure.py --sjiswrap`) so MWCC emits the console's Shift-JIS
bytes, which is what `HSD_SisLib_803A67EC` looks up two bytes at a time.

The port gets the same result from `-fexec-charset=CP932` on
`melee_decomp_game`.  GCC converts at codegen, after parsing, so the
backslash-as-trail-byte hazard sjiswrap exists to handle does not arise.

Two things to know:

- Use **CP932**, not `SHIFT-JIS`.  iconv's strict `SHIFT-JIS` rejects
  characters the JP name table uses and the build fails outright with
  "converting to execution character set: Invalid or incomplete multibyte or
  wide character".
- Shift-JIS is ASCII-compatible, so plain literals are byte-identical and the
  flag is safe to apply to the whole compiled decomp.

The failure mode is nasty because it is *not* silent-but-blank: partial UTF-8
byte pairs accidentally match SJIS table entries, so text renders as plausible
kana.  See G-150.

**Check this after every re-pin**, and whenever a new TU with non-ASCII
literals starts being compiled:

```sh
grep -rlP '[^\x00-\x7f]' decomp/src --include=*.c | head
```

## P-704 name-width tables: out-of-bounds reads into adjacent arrays (2026-09-14)

| File | Patch | Why |
|---|---|---|
| `src/melee/gm/gm_1601.c` (`fn_80160DE8`) | `patches/src/melee/gm/gm_1601.c.patch`: under `PORT_PC`, read `lbl_803B767C`/`lbl_803B7700`/`lbl_803B7784[tmp_ckind]` instead of `lbl_803B75F8[tmp_ckind + 0x21/0x42/0x63]` | The four name-width tables are 33-entry `static const float` arrays that the console linker placed back to back, so the US branch indexes the first one past its end.  GCC's `-fdata-sections` puts each array in its own section, so the reads land in padding and return `0.0`; `HSD_SisLib_803A7548` stores a 0 width as an x-scale of 0, drawing every fighter name on the US VS splash at zero width.  Same class as P-686 (G-154). |

Only the US branch uses the out-of-bounds offsets; the JP path reads
`lbl_803B75F8[tmp_ckind]` in bounds, which is why the JP harness rendered names
while the owner's US save did not.  Regression test: `ctest
decomp_intro_names` with `MELEE_INTRO_US=1` (`[intro] names white=`; 0 broken,
2457 fixed).  G-155 covers why the probe counts white rather than non-black.
