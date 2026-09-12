# S1 boot skeleton: compiled `main()` under stubbed hardware (P-604)

Written 2026-09-11. This is the durable result of milestone S1
(`ROADMAP_DETAILS.md`): the decompilation's own `main()`
(`src/melee/gm/gmmain.c:130`) now compiles and runs on the host behind the
platform layer, reaches a controlled stop, and emits a triage log that is the
first concrete backend work list.

Evidence:

- Target: `melee_decomp_boot` (`native/decomp/boot/`), 32-bit per ADR-0012.
- Log: `native/AI/logs/2026-09-11-S1-boot-triage.md` (10 frames, clean).
- Tests: `ctest --test-dir build/native` = 4/4, including the new
  `decomp_boot` test which runs the boot with `--boot-frames 5`.

```
./build/native/melee_decomp_boot --boot-frames 10 --boot-timeout 30 \
    --boot-log /tmp/boot.log
```

The run is deterministic (virtual timebase, fixed RNG seed from
`OSGetTick`); two runs with the same arguments are byte-identical.
No `src/` or `extern/` file was modified: the whole S1 change is under
`native/`.

## What runs for real

| Backend | Implementation |
|---|---|
| OS arena/heap | Upstream `extern/dolphin/src/dolphin/os/OSAlloc.c` + `OSArena.c` compiled verbatim (they are portable C). `OSInit` seeds a 24 MB host arena and maps the cached hardware-register page. |
| GC hardware page | `mmap(MAP_FIXED_NOREPLACE)` at `0x80000000` with bus clock 162 MHz, core clock 486 MHz, physical/simulated memory 24 MB. `OS_TIMER_CLOCK` and friends are direct macro reads of that page. |
| OS timebase | Virtual 40.5 MHz counter in `native/platform/os.c`; advances 1 ms per read and one 60 Hz frame per `VIWaitForRetrace`. Keeps the boot and the RNG seed deterministic. |
| OS report/panic | `OSReport` shared with the triage stream; `OSPanic` ends in a controlled stop; `OSSetErrorHandler` table; `HSD_LogInit`/`HSD_Panic` replacement (`native/decomp/debug_port.c`). |
| OS interrupts/cache | Enable/disable bookkeeping; `DCFlush/DCStore/DCInvalidate*` are no-ops (x86 is coherent). |
| VI timing | `VISetPre/PostRetraceCallback` + `VIWaitForRetrace` run HSD's XFB state machine, advance the virtual clock, and charge the frame budget. `GXSetDrawDone` completes immediately so `HSD_VIGXSetDrawDone` can advance. |
| GX (small real parts) | `GXGetTexBufferSize` copied from `GXTexture.c`, the `GXNtsc480*` render modes from `GXFrameBuf.c`, `GXSetDrawDoneCallback` storage. |
| AR | Host ARAM model: 16 MB bump allocator at `0x10000000`, needed by `HSD_SynthInit`'s bank layout. |
| PAD/CARD answers | `PADRead` reports four disconnected pads; `CARDProbeEx` reports success. Both avoid the game's retry loops so boot init can finish. |
| SDK math | `native/decomp/sdk_math.c` (real `PSMTX*`/`PSVEC*`, `C_MTXLookAt`, `MTXRotRad`, `MTXLight*`) plus upstream `extern/dolphin/src/dolphin/mtx/mtx44.c`. |
| MWCC inline semantics | Compiled with `-fgnu89-inline`: MWCC emits `inline` functions as globals, C99 GCC does not. This recovered the whole `*_inline*`/`GetX118`-class symbol set without patches. |

## What is still a log-only stub

Every call is counted and listed in first-hit order by the triage summary.
Counts below are from the canonical 10-frame log.

| Category | Calls | What it is |
|---|---|---|
| gx | 146 | The GX command surface (state, TEV, lights, textures, draw). S2 replaces with GX HLE grown from the prototype renderer. |
| vi | 21 | `VIConfigure`/`VIFlush`/`VISetBlack`/`VISetNextFrameBuffer`; only the retrace path is real. S2. |
| dvd | 14 | `DVDConvertPathToEntrynum` (-1), open/read/status fail as "no disc". S3 replaces with the CISO-hosted DVD pipeline. |
| ax | 30 | AI/AX voice/DSP surface and the HSD AX driver's hardware half. S5. |
| hsd | 9 | `HSD_SisLib` text engine and font atlases (see exclusions). S6. |
| card | 4 | CARD/FIO/MCC; the HSD card command pump runs from `lb_8001B6F8`. S6. |
| pad | 3 | `PADSetSpec`/`PADSetSamplingRate` etc.; input is S4/S6. |
| ar | 3 | `ARQInit`/`ARQPostRequest`/`ARFree`; needed for streaming and the synth load path. S3/S5. |
| os | 2 | `OSCreateAlarm`/`OSSetPeriodicAlarm`; real alarm scheduling is S4. |

## Excluded sources (PC build)

| File(s) | Reason |
|---|---|
| `src/MSL/*` (23) | MSL libc/math; glibc provides the symbols (ADR-0011). |
| `src/MetroTRK/*` (20) | GameCube debugger transport. |
| `src/Runtime/__mem.c` | MSL `memcpy`/`memset` duplicates. |
| `src/sysdolphin/baselib/debug.c` | MSL `FILE` internals; replaced by `native/decomp/debug_port.c`. |
| `src/sysdolphin/baselib/axdriver.c` | Drives the DSP; replaced by `native/platform/audio.c` stubs. |
| `sislib_font.c`, `hsd_3915.c`, `hsd_3A76.c` | Need generated font atlases (`build/GALE01/include/*.inc`), which are build artifacts and must not be committed (ADR-0005). `native/platform/font_stub.c` carries the kerning tables and no-op text functions for S1. |

## Where the boot stops, and why it is the work list

At frame 10 the run stops via the frame budget. The compiled game is in
`gmMainLib_8015FBA4` → `lbAudioAx_80028690` →
`HSD_SynthSFXWaitForLoadCompletion(lb_800195D0)`: it queued its first
sound-bank load and is pumping the "insert disc" screen while waiting for the
load to finish. The load cannot finish because

1. `DVDConvertPathToEntrynum` returns -1 and `DVDFastOpen`/`DVDReadAsyncPrio`
   fail (no DVD backend), and
2. the synth's load progress is driven by the AX callback, which never fires
   (no AX backend).

That makes the ordered backend work list:

1. **S2 — GX + VI HLE**: the boot logo/loading path already issues the full
   HSD render sequence (146 GX calls in 10 frames). The prototype
   `native/gx/` is the seed; `GXSetDrawDone`/XFB pacing in
   `native/platform/gx_vi.c` is the skeleton.
2. **S3 — DVD + HSD DevCom/ARQ asset pipeline**: `HSD_SynthSFXLoad` needs
   entry numbers and async reads; `HSD_DevComRequest` (real, compiled) waits
   on `ARQPostRequest`, so the host DVD/ARQ backend must complete those
   callbacks.
3. **S5 — AX/DSP callback**: `AXRegisterCallback` must fire
   `HSD_SynthCallback` on the audio frame so load completion and voice state
   advance.
4. **S6 — CARD/EXI + fonts**: the card command pump runs every loading-screen
   frame (`hsd_803AAA48`); fonts need a real source instead of the generated
   atlases.
5. **S4 — threads/alarms**: `OSCreateAlarm`/`OSSetPeriodicAlarm` are stubs;
   the game installs a periodic alarm during init.

## Findings kept from S1

- **Absolute hardware reads are the first hard crash.** `OSSecondsToTicks`
  expands to a read of `*(u32*)0x800000F8`; the host must map the cached page
  before any SDK macro runs (`map_gc_hardware_page` in `native/platform/os.c`).
- **`-fgnu89-inline` is required** for the MWCC inline convention; without it
  ~15 `inline` functions (`fn_80180630_GetX118`, `eflib_..._add_appsrt`, ...)
  stay undefined.
- **The `stdbool.h`/`ssize_t` shims are pointer-width-sensitive.** The
  `__ssize_t_defined` predefine is now 64-bit-only in `decomp_shim.h`; on i686
  glibc's `ssize_t` already matches MSL and the predefine broke `<unistd.h>`.
- **32-bit ASan/UBSan is available** (`build/native-boot-asan`,
  `-DMELEE_SANITIZE=ON`). The boot is clean through frame 2; from frame 3 the
  loading-screen callback reaches `hsd_803AAA48`, which treats the 16-byte
  `hsd_804D1138` context as the base of the 128-entry card command ring that
  follows it in the GameCube address space. Host globals are not contiguous,
  so that path is a known S1 gap (card backend, S6), not a memory bug in the
  platform layer.

## Reproduce

```sh
cmake -S native -B build/native -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build/native -j4
ctest --test-dir build/native --output-on-failure          # 4 tests
./build/native/melee_decomp_boot --boot-frames 10 --boot-timeout 30 \
    --boot-log /tmp/boot.log

# sanitizer build (separate tree; 32-bit ASan/UBSan)
cmake -S native -B build/native-boot-asan -DCMAKE_BUILD_TYPE=Debug \
    -DMELEE_SANITIZE=ON
cmake --build build/native-boot-asan --target melee_decomp_boot -j4
./build/native-boot-asan/melee_decomp_boot --boot-frames 2 --boot-timeout 60
```

## S4 bring-up (P-620, in progress)

**Alarms and idle time.** `OSCreateAlarm`/`OSSetAlarm`/`OSSetPeriodicAlarm`/
`OSCancelAlarm` are real: 32 host slots fire from `advance_ticks`, which runs
on every `OSGetTime`/`OSGetTick` read and every VI frame, so a handler can
never observe a frozen clock.  The compiled game's periodic 1/60 s alarm
(`fn_800195FC`) renews the raw pad status; without it the scene loop spins
forever on an empty pad queue.  `DVDGetDriveStatus` advances the clock by
1 ms per call (`boot_platform_idle_tick`) because the idle pad-queue spin
only polls that; VI frames still advance the clock by one 60 Hz tick.

**Direct match entry.** `decomp/boot/match_boot.c` installs a VI frame hook
(`boot_platform_set_frame_hook`) and, at `--boot-match N`, clears the
state-machine override (`lbCardGame_DecideGameMode`, an S6 card router that
would otherwise swallow the request), posts `GM_DEBUG_VS` through
`gm_ChangeGameModeAfterCurrentScene` and ends the current scene with
`gm_801A4B60`.  `GM_DEBUG_VS` -> `gm_Mode_DebugVs_States[0]` ->
`onEnterDebugVs` is the game's own hardcoded Link vs Mario match; the VS
scene then loads effects, the stage (Zebes for `St_Kind_Last`), items and
fighters.
`OSGetResetCode` returns 0x80000000 so `skip_intro` routes `GM_BOOT` past
the unported opening movie (THP).

**Converter classes S4 needed beyond S3** (all in `hsd_convert.c`, version 20):
`HSD_TexAnim`/`MatAnim`/`ShapeAnim` chains (material animation), `LightAnim`
+ `WObjAnim` AObjs, effect PS banks (`eff*DataTable`, `map_ptcl`,
`map_texg`), stage `coll_data` (`MapCollData`/`MapLine`/`MapJoint`), and the
`map_head` `Ground_801C34AC` joint/pair tables plus `GrJoint[]`.  See
G-076..G-079 for the traps (ABI alignment, self-relocating banks, overlapping
`coll_data`/`map_ptcl`, host-order version reads).

**Match bring-up chain (all compiled game code).**  Order of blockers found
and fixed after the stage constructed: `ItCoData` (`itPublicData`
ItemCommonData limits + Article tables/hurtbones/model descs/dynamics/state
joints), `PlCo.dat` (`ftLoadCommonData`: ftCommonData struct + per-kind
`:PartsTable`), `PlMr.dat` `ftData` (wait-anim FigaTree offsets, x8 model
tables whose pointers can be data offset 0, costume TObj index arrays, x5C
costume joint tree), and the `ft_800852B0` adjacency patch (see
`decomp_port.md`).  Fighter creation now reaches part visibility
(`ftParts_800749CC` -> `HSD_DObjSetFlags`).

**Where it stops today:** `ftParts_80074D7C`/`HSD_DObjSetFlags` crashes
during `ftParts_800749CC` — the fighter DObj list built by
`ftParts_SetupParts` has a bad entry (model/part table conversion still
incomplete).
