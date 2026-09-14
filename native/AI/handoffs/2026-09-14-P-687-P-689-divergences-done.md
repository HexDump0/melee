# Handoff: P-687..P-689 — non-Metrowerks divergences fixed and verified

**Date:** 2026-09-14
**Agent:** opencode (deepseek-v4.1-flash)
**Commit:** `576ea030b` (P-689), `ca307e307` (P-688), `d2b3ab26c` (P-687);
this docs commit
**Tree state:** builds; `ctest --test-dir build/native` 22/22; GC `decomp/`
`ninja` reports `build/GALE01/main.dol: OK` (100.00% matched, 1130/1130
linked) with every patch applied.

## What I did

- **P-689** (`576ea030b`): `conv_ft_common_data` converts PlCo.dat `pData[8]`,
  the respawn-platform `{joint, anim}` pair (`ft_0D4D.c:139,148`), with
  `conv_joint` + `conv_anim_joint`; converter v82.  `test_decomp_assets`
  gained `check_respawn_platform`: the slot-8 joint flags/floats must match
  the raw archive and slot 16 (entry platform) stays as the converted
  control.  Fails before the fix (`flags 08000030 want 30000008`,
  `scale 4.6e-41 want 1`).
- **P-688** (`ca307e307`): `__fabs` -> `fabs` in the shim; `atanf` compiled
  under `PORT_PC` with `__fnmsubs(a, c, b) = -fmaf(a, c, -b)`
  (`patches/src/melee/lb/lbtrigf.c.patch`); new `ctest decomp_trig` proves the
  compiled function bit-identical to an explicitly-rounded transcription over
  54,590,184 sampled inputs (glibc fails 2,091,032 = 3.8%).  Owner adopted
  upstream item 4 (ADR-0018): compile `src/MSL/trigf.c` + `math_data.c` with
  `native/decomp/msl_port.c` (`fabsf__Ff` + constructor running
  `__sinit_trigf_c`, G-143).  MSL trig vs glibc over 101,854,860 inputs:
  `sinf` 41.9%, `cosf` 2.2%, `tanf` 44.0% differ (typically 1-4 ulp).
- **P-687** (`d2b3ab26c`): `PORT_PC` fallbacks for the five Big Blue `rlwimi`
  state inserts, `fn_80166A8C` (results screen `xE`) and `__cvt_dbl_usll`
  (training speed), as three new patches (ADR-0011 addendum).  `objdump`
  shows five `and $0x3` + `or $0x28`/`or $0x10` insert sites and a real
  conversion body; the GC DOL checksum stays OK (G-144).
- Docs: ADR-0011 addendum, ADR-0018, G-142..G-144, patch tables in
  `learnings/decomp_port.md`, STATE/TASKS updates.

## Exact next action

Owner visual check for P-689 (the agent machine has no display):

```sh
MELEE_VIEWER_TRIAGE=1 SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy \
    ./build/native/melee --match
```

Watch the rebirth window (~frames 470-520 of the default script): the
respawn platform must appear under the fighter and look like the entry
platform at match start.  The old `./build/native/melee_decomp_viewer` binary
is stale — the viewer main is built into `melee` now.  Headless before/after
evidence: `/tmp/opencode/p689_480_compare.png` (fixed left, unfixed right;
all non-rebirth frames byte-identical).

When upstream #3456 merges, drop `patches/src/melee/lb/lbtrigf.c.patch`,
`patches/src/melee/gr/grbigblue.c.patch`,
`patches/src/melee/gm/gm_1601_ml_fallback.patch` and
`patches/src/Runtime/runtime.c.patch`; the remaining
`placeholder.h`/`.nix` items are already handled by the shim and CMake.

## What I tried that did not work

- `./build/native/melee_decomp_viewer` is a stale Sep-13 binary; CMake builds
  the viewer main into the `melee` product now.  The first P-689 captures
  were taken with the stale binary and were redone with `./build/native/melee
  --match`.
- `-Dsinf=msl_sinf` to compare MSL and glibc in one binary breaks glibc's
  `<math.h>` (it renames the `__MATHCALL_VEC` declarations); used a
  sed-renamed copy of `trigf.c` plus a local `math.h` copy instead.
- `git -C decomp diff` for `gm_1601.c` also contains the pre-existing S6
  patch; generated `gm_1601_ml_fallback.patch` by diffing a saved before-copy
  against the edited file (header rewritten to `a/`/`b/`).
- The five Big Blue blocks are not identically indented (12 vs 20 spaces), so
  a literal 5x replace matched only one; used a regex.
- The first `atanf` reference used the sign-set bits in the signed threshold
  comparisons; the source clears the sign bit in place.  Fixed; the 54.6M
  sample sweep then matched exactly.

## Open questions

- P-689 respawn-platform visual — needs a human? yes (command above).
- Big Blue car state and the results screen are not runtime-reachable in the
  tests; only `objdump` + GC DOL checks.  An owner stage/results run would
  close it — needs a human? optional.
- Training mode (`__cvt_dbl_usll`) is latent; no test reaches it.
- MSL trig changes 0.17% of frontend pixels (RMSE 0.20/255).  No test
  baseline is pixel-exact, so none was updated; if a later owner check sees an
  artifact, ADR-0018's evidence table is the starting point.

## Files touched / claimed

- P-689: `native/decomp/assets/hsd_convert.c`,
  `native/tests/test_decomp_assets.c`
- P-688: `native/decomp/shim/placeholder.h`, `native/decomp/msl_port.c`,
  `native/tests/test_decomp_trig.c`, `native/CMakeLists.txt`,
  `patches/src/melee/lb/lbtrigf.c.patch`
- P-687: `patches/src/melee/gr/grbigblue.c.patch`,
  `patches/src/melee/gm/gm_1601_ml_fallback.patch`,
  `patches/src/Runtime/runtime.c.patch`
- Docs: `native/AI/{DECISIONS,STATE,TASKS,HANDOFFS}.md`,
  `native/AI/learnings/decomp_port.md`, `native/AI/gotchas/GOTCHAS.md`
- The decomp submodule working tree has all patches applied; the patch files
  are the source of truth.

## Verification run

```sh
ctest --test-dir build/native --output-on-failure
# 100% tests passed out of 22 (includes decomp_trig and decomp_assets)

# sensitivity: P-689 walk reverted
# decomp_assets: PlCo.dat pData[8] joint flags 08000030 want 30000008
#                scale[0]=4.6e-41 is denormal/zero (unconverted?)
# -> FAIL; restored -> PASS

./build/native/test_decomp_trig
# decomp_trig: PASS 54590184 samples bit-identical
# with glibc atanf: decomp_trig: FAIL 2091032/54590184 mismatches

cd decomp && ninja
# build/GALE01/main.dol: OK
# All: 99.99% fuzzy, 100.00% matched, 100.00% linked (1130 / 1130 files)

# P-689 headless match A/B (melee --match --frames 700 --record-every 10):
# frames 470-520 and 580-600 differ (0.13-0.19% of bytes); every other
# frame byte-identical; frame 480 shows the platform only in the fixed run.

MELEE_NO_ASSET_CACHE=1 ASAN_OPTIONS=detect_leaks=0 \
    ./build/native-asan/test_decomp_assets
# decomp_assets: PASS
```
