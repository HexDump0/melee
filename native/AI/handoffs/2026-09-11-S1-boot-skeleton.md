# Handoff: S1 boot skeleton — the decomp's `main()` runs to a controlled stop

**Date:** 2026-09-11
**Agent:** opencode (deepseek-flash)
**Task:** P-604 (S1, ADR-0010)
**Tree state:** prototype untouched and runnable (`--inspect`/viewer/scripted
unchanged); `ctest --test-dir build/native` 4/4 (the three S0/S1 tests plus the
new `decomp_boot`); no `src/` or `extern/` file modified.

## Result

`melee_decomp_boot` compiles the decompilation's own `main()`
(`src/melee/gm/gmmain.c:130`, renamed `gm_main` per-TU) together with 986
`src/*.c` files and three portable `extern/dolphin` C TUs, links them 32-bit
(ADR-0012) against the platform layer, and runs them. It reaches the game's
own "no disc" loading wait and stops on the S1 frame budget.

```
[boot] summary: frames=10 stub_calls=232 unique=57
[boot] STOP: frame budget reached
```

- Reproducible log: `native/AI/logs/2026-09-11-S1-boot-triage.md`
  (byte-identical across runs; virtual timebase, no wall clock).
- Analysis and backend work list: `native/AI/learnings/decomp_boot.md`.
- `decomp_boot` ctest runs the asset-free boot with `--boot-frames 5`.

## What S1 leaves behind

- `native/decomp/boot/` — harness (`boot_main.c`) and triage logger
  (`boot_triage.{c,h}`): `--boot-log/-frames/-stub-limit/-timeout/-trace`,
  signal + panic controlled stops, first-hit-ordered stub summary.
- `native/platform/` — the platform layer seed: `os.c` (arena from upstream
  `OSAlloc.c`/`OSArena.c`, virtual 40.5 MHz timebase, mapped GC hw page,
  report/panic, interrupt bookkeeping), `gx_vi.c` (VI retrace pacing + GX log
  stubs), `dvd.c`, `pad_card.c`, `audio.c` (host ARAM model + AX stubs),
  `font_stub.c`, `misc.c`.
- `native/decomp/debug_port.c` — replacement for the excluded
  `baselib/debug.c` (MSL `FILE` internals).
- `native/decomp/sdk_math.c` extended with the C twins from the asm
  `mtx.c` (`C_MTXLookAt`, `MTXRotRad`, `MTXLight*`, `PSMTXTranspose`).
- Build flags that matter: `-fgnu89-inline` (MWCC inline convention), the
  forced shim, `-w` for upstream TUs and `-Wall -Wextra -Wpedantic` for the
  platform TUs, `--gc-sections`.

## Where it stops, and the immediate next steps

The game queues its first sound-bank load in `lbAudioAx_80028690` and spins in
`HSD_SynthSFXWaitForLoadCompletion` while drawing the "insert disc" screen.
The load cannot progress because the DVD read fails and the AX callback never
fires. Next, in order:

1. **S2** — GX/VI HLE (146 GX calls in 10 frames): the boot already drives the
   full HSD render sequence.
2. **S3** — DVD + `HSD_DevCom`/`ARQPostRequest` completion: asset loads.
3. **S5** — `AXRegisterCallback` driving `HSD_SynthCallback` per audio frame.
4. **S6** — CARD/EXI (the card pump) and fonts.

## Known issues

- **Card pump vs host global layout.** From frame 3 the loading-screen callback
  calls `hsd_803AAA48`, which treats the 16-byte `hsd_804D1138` as the start of
  the 128-entry command ring that follows it on the GameCube. Under the
  sanitizer build (host globals are not contiguous) this faults; the normal
  build tolerates it. This is the S6 card backend's job. 32-bit ASan/UBSan
  builds are wired (`-DMELEE_SANITIZE=ON`) and clean through frame 2.
- Fonts for the loading screen are stubbed deliberately: the real atlases come
  from decomp build artifacts (`build/GALE01/include/*.inc`).

## Commands

```sh
cmake -S native -B build/native -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build/native -j4
ctest --test-dir build/native --output-on-failure   # 4 tests
./build/native/melee_decomp_boot --boot-frames 10 --boot-timeout 30 \
    --boot-log /tmp/boot.log
```
