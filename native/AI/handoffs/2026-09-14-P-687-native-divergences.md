# Handoff: P-687..P-689 — non-Metrowerks divergence fixes (upstream #3456)

**Date:** 2026-09-14
**Agent:** opencode (deepseek-v4.1-flash) — investigation and task filing only
**Commit:** this commit (docs only)
**Tree state:** builds; this commit changes no code. NOTE: the working tree
carries another agent's uncommitted graphics WIP in `native/decomp/gx/gx_gl.c`
and `native/tests/test_decomp_render.c`; it is not part of this work and was
not staged or committed.

## Background

Another port (`alexscott2718-gif`) filed an upstream decompilation issue on
2026-09-11:

- **doldecomp/melee#3456** —
  <https://github.com/doldecomp/melee/issues/3456>
  "Non-Metrowerks builds silently diverge in four places: atanf guard,
  `__frsqrte` fallback, two asm blocks without `#else`, MSL/trigf.c not in the
  .nix target".
- Triage: ribbanya applied the `portability` and `ai-assisted` labels the same
  day (no comment). There is **no upstream PR**; the reference implementation
  is only on the fork branch
  `alexscott2718-gif/melee:p0/native-build-fixes` (5 files, small diffs). The
  author's notes live in `alexscott2718-gif/melee-port` commits `6e3c98f6` and
  `6b79bcff` (`p0/` docs only).
- The current decomp pin is `40012f51f` and **still contains all four items**.
- The upstream fix suggestions below are quoted from that fork branch; none of
  them change the GameCube build (they are `#else` / `#ifndef __MWERKS__` /
  `.nix`-only paths).

Every claim was re-verified in this tree and against the built binaries
(`build/native/melee_decomp_boot`). Two extra host-only divergences were found
while cross-checking (items 5 and 6 below); item 5 is the same bug class as
upstream item 3, item 6 is port-only.

No fixes were implemented. This handoff is the specification; claim one task
per session per `TASKS.md`.

## Upstream items and port status

| # | Divergence | Upstream fix (fork branch) | Port status |
|---|---|---|---|
| 1 | `atanf` body is `#ifdef __MWERKS__` with no fallback, so `atanf` (and `atan2f`/`acosf`/`asinf` in the same file) bind to host libm | define `__fnmsubs(a,c,b)` as `-fmaf(a,c,-(b))` for non-MWERKS and drop the guard | **live**: `nm` shows `U atanf@GLIBC_2.0`; no patch, no mention in `native/AI/` |
| 2 | `placeholder.h` maps `__frsqrte` to `sqrt(x)` (wrong quantity) and `__fabs` to `fabsf` (narrows doubles) | `__frsqrte(x) (1.0 / sqrt((double)(x)))`, `__fabs(f) fabs(f)` | `__frsqrte` already fixed by the port shim (G-094); **`__fabs` is still live** |
| 3 | two places with inline asm and no fallback: `grbigblue.c` five `rlwimi` blocks, `gm_1601.c` `fn_80166A8C` (empty non-void body) | portable `#else` branches (below) | neither patched; Big Blue verified *compiled out* in the port binary |
| 4 | `.nix` native target never builds `src/MSL/trigf.c`, so `sinf`/`cosf`/`tanf` bind to host libm | `list(APPEND SOURCES src/MSL/trigf.c)` | port does not use `.nix`; the port excludes all of `src/MSL/` by ADR-0011 and has **363** `sinf`/`cosf`/`tanf` call sites on host math |

## Port-only findings

5. **`__cvt_dbl_usll` is an empty non-void function on the host.**
   `decomp/src/Runtime/runtime.c:544` declares `ASM u64 __cvt_dbl_usll(double x)`
   and the entire body is `#ifdef __MWERKS__ ... #endif` with no `#else`
   (`ASM` expands to nothing under GCC). The compiled port has
   `__cvt_dbl_usll` at `0x117a70` as a **single `ret`**. It is called from
   `decomp/src/melee/gm/gm_1884.c:784` (training-mode speed selection), which
   passes the result to `lb_80019880`; the caller gets undefined data. Same
   bug class as upstream item 3, not covered by #3456. The in-file precedent
   for the fix is `__cvt_fp2unsigned` (`runtime.c:7`), which already has
   `#else return (unsigned long) d; #endif` at `runtime.c:35`.

6. **PlCo `pData[8]` (respawn-platform joint + animation) is never converted.**
   `native/decomp/assets/hsd_convert.c:2233` (`conv_ft_common_data`) walks
   pData 0 (the common struct), 4 (`ftPartsTable`), 5 (`Fighter_804D6540`),
   16 (`Fighter_804D6514`) and 20 (`Fighter_804D6504`). Slot 8
   (`Fighter_804D6534`, bound at `decomp/src/melee/ft/fighter.c:196`) is
   missing, so its joint tree and its animation tree stay big-endian in the
   converted cache. The decomp reads it as a model + animation pair in the
   respawn path: `decomp/src/melee/ft/ft_0D4D.c:139` (joint through
   `ftCommon_SetAccessory`) and `:148` (anim through `ftCommon_8007E690`).
   Slot 16 is the *entry/trophy* platform (`decomp/src/melee/ft/ft_0C31.c:101`,
   `fighter.h:91`), which the converter comment at `hsd_convert.c:2270`
   describes; slot 16 is a different model. `conv_joint` (`hsd_convert.c:565`)
   is what would byte-swap flags, the nine rot/scale/translate floats, the
   matrix and the attached DObj/MObj descriptors; `conv_anim_joint`
   (`hsd_convert.c:925`) handles the animation half.

## Task P-687 — host fallbacks for the silent asm/empty-body bugs

**Files:** new patches `patches/src/melee/gr/grbigblue.c.patch`,
`patches/src/melee/gm/gm_1601.c.patch`,
`patches/src/Runtime/runtime.c.patch`; update
`native/AI/learnings/decomp_port.md` (ADR-0011 patch list).
**No `native/` code change.**

1. **Big Blue, five blocks** (`decomp/src/melee/gr/grbigblue.c` lines 3250,
   3264, 3280, 3297, 3314, inside `grBigBlue_801ECB50`). Upstream adds:

   ```c
   #ifdef MUST_MATCH
       asm { rlwimi byte, st_val, 2, 24, 29 }
   #else
       byte = (byte & ~0xFC) | ((st_val & 0x3F) << 2);
   #endif
   ```

   The field is bits 2..7 of byte `0xD4`. Without it the load/store is a
   no-op and the cars' 6-bit state never becomes 10 (closest car) or 4.
   Verification is easy in the binary: `grBigBlue_801ECB50` currently has only
   two `0xD4` accesses and no bit-insert; after the patch it must write the
   masked value.

2. **Results screen** (`decomp/src/melee/gm/gm_1601.c:3081`,
   `fn_80166A8C`, static). Upstream adds:

   ```c
   #else
       /* psq_st with W=1 through GQR3 (float type, no scale) is a
        * single-element float store */
       float x = src->x;
       dst->x = x;
       return x;
   #endif
   ```

   Without it the function is empty and the caller at `gm_1601.c:2986` reads
   `sp48_x` (declared at `:2951`, never otherwise initialised) into
   `player_standings[i].xE`. Note `gm_1601.c` is on the "files containing
   Metrowerks asm" list in `learnings/decomp_port.md`; the same TU already has
   two S6 `PORT_PC` patches (`gm_80168B34`/`gm_80168BF8`).

3. **`__cvt_dbl_usll`** (`decomp/src/Runtime/runtime.c:544`). Port-only; add

   ```c
   #else
       return (u64) x;
   #endif
   ```

   mirroring `__cvt_fp2unsigned`. The training-mode argument is a small
   positive frame-interval, so C truncation matches the intended value.

**ADR-0011 note.** All existing `src/` patches are `#ifdef PORT_PC`-gated;
the upstream fixes use `#else`/`#ifndef __MWERKS__`. Mirroring upstream keeps
the patch small and lets it fall away when #3456 merges, but is a deliberate
deviation from the gating convention — note it in `decomp_port.md` and keep
the GameCube build green either way (`ninja` -> `main.dol: OK`). Prefer
upstream's shape if the patch will be dropped on re-pin; prefer `PORT_PC` if
the tree must stay bit-identical for other non-MWERKS consumers.

**Acceptance:** port builds; `objdump` shows the Big Blue bit-insert and a
non-empty `__cvt_dbl_usll`; if reachable, a Big Blue car changes state and the
results-screen statistic is plausible; `ctest` green; GC `ninja` green;
patches listed in `decomp_port.md`.

## Task P-688 — host-math divergence (`__fabs`, `atanf`, MSL trig)

**Files:** `native/decomp/shim/placeholder.h`, plus (optionally) a new
`patches/src/melee/lb/lbtrigf.c.patch`; decision in `native/AI/DECISIONS.md`
if MSL trig is adopted.

1. **`__fabs` (easy, do first).** `decomp/src/placeholder.h:16` maps the
   double intrinsic to `fabsf`. The port shim already shadows `__frsqrte`;
   add the same override:

   ```c
   #undef __fabs
   #define __fabs(f) fabs(f)
   ```

   Live call site: `decomp/src/sysdolphin/baselib/generator.c:884`
   (`__fabs(r0 - M_PI) < eps`, sphere emission). `MSL/math.h`'s uses are
   MWERKS-only and MSL is excluded. Shim-first per ADR-0011: no `src/` patch.

2. **`atanf`.** `decomp/src/melee/lb/lbtrigf.c:147-243` is guarded with no
   fallback. Direct callers in game code: `ft/ftcoll.c:2903` (fighter
   collision angle, quantised to whole degrees via `(s32) MTXRadToDeg(...)`),
   `lb/lb_00CE.c:143,155`, `mn/mncharsel.c:3502,3610`,
   `sysdolphin/baselib/bytecode.c:186`; plus `atan2f`/`acosf`/`asinf` in the
   same file (`ftcommon.c` uses `atan2f`, `lb_sqrtf`/`acosf` use the shimmed
   `__frsqrte`). The fork's patch:

   ```c
   #ifndef __MWERKS__
   #define __fnmsubs(a, c, b) (-fmaf((a), (c), -(b)))
   #endif
   ```

   plus removing the `#ifdef __MWERKS__` guard and the trailing `#endif`.
   `-fmaf(a,c,-b)` (not `fmaf(-a,c,b)`) matters for signed zero.

   **Caveat specific to this port.** The fork verified bit-identical results
   with clang 14 at `-m32 -ffp-contract=on`, but the port compiles
   `-m32 -msse2 -mfpmath=sse` with no `-mfma`
   (`native/CMakeLists.txt:30,283-285`). The explicit `fmaf` becomes a
   correctly-rounded libm call, but GCC will not contract the Horner chain
   without FMA hardware support, so the fix is *closer* to the console but not
   provably identical as built. Before accepting it, run a differential test
   (the fork compared 64.5M sampled inputs against a DOL-derived reference;
   the issue reports host libm differs on ~10% of inputs). Decide explicitly
   whether to add `-mfma`/AVX (raises CPU requirements), keep separate
   mul/add, or accept the residual. Do not mark it done on "it compiles".

3. **MSL `trigf.c` (owner decision).** `src/MSL/trigf.c` is plain C
   (`sinf`:24, `cosf`:68, `tanf`:126) with no MWERKS guards; it needs
   `fabsf__Ff` (`MSL/math.h:81`, defined in `MSL/math_1.c:3`). ADR-0011 says
   MSL is not compiled and glibc provides those symbols. Adopting upstream
   item 4 for the port means changing that decision (compile `trigf.c` + a
   `fabsf__Ff` shim) and re-baselining 363 call sites. No action until the
   owner chooses; record the choice in `DECISIONS.md`.

**Acceptance:** `__fabs` shim committed; `atanf` decision + differential
evidence (or an explicit "wait for upstream merge" decision) recorded; trig
decision recorded. Note `extern/dolphin/include/dolphin/types.h` types `u32`
as `long`, which misbehaves on LP64 for the `BITWISE()` puns in `lbtrigf.c` —
irrelevant at `-m32`, relevant to the parked 64-bit work (P-664).

## Task P-689 — converter: walk PlCo `pData[8]`

**Files:** `native/decomp/assets/hsd_convert.c`,
`native/tests/test_decomp_assets.c`.

In `conv_ft_common_data` (`hsd_convert.c:2233`), after the slot 16/20 block,
read the pair at slot 8 and convert both halves, mirroring the existing
pattern:

```c
uint32_t pair = rd32(c, off + 8 * 4);
if (pair != 0 && in_data(c, pair, 8)) {
    uint32_t joint = rd32(c, pair + 0x00);
    uint32_t anim = rd32(c, pair + 0x04);
    if (joint != 0 && in_data(c, joint, HSD_JOINT_SIZE)) {
        conv_joint(c, joint);
    }
    if (anim != 0) {
        conv_anim_joint(c, anim);
    }
}
```

Then bump `HSD_CONVERTER_VERSION` (`hsd_convert.c:34`, currently `81u`) so the
`build/native/asset-cache/hsd-v81-*` images regenerate.

**Visual verification (owner asked for this).** Run
`MELEE_VIEWER_TRIAGE=1 ./build/native/melee_decomp_viewer --match`; the stderr
log prints `[match] slot N stocks=...` and a stock drop is the KO. The
respawn platform should appear under the fighter as they fall back in; the
entry platform at match start (slot 16, already converted) is the control. If
the respawn platform is missing/microscale/garbage while entry is correct,
that is the slot-8 fingerprint. Capture with `--frames N --shot FILE` or
`--record FILE` (P-623 handoff has the ffmpeg pipe). Failure mode to expect
if unconverted: the joint's `1.0f` scales read as denormals (~4.6e-41), so the
platform collapses/disappears.

**Automated check:** extend `test_decomp_assets.c` (ctest `decomp_assets`)
with a PlCo slot-8 check in the style of `check_ft_part_anims`/
`check_ft_data_tables` (`test_decomp_assets.c:262,476`): after conversion, the
slot-8 pair must resolve to a joint whose rot/scale/translate are finite
non-denormal host-order floats and to a convertible anim pointer.

**Acceptance:** `ctest decomp_assets` green with the new check; visual respawn
platform correct (or the visual result documented if it differs); converter
version bumped; no other cache consumers broken.

## Verification run

Commands used while investigating (all read-only):

- `nm build/native/melee_decomp_boot | grep atanf` ->
  `U atanf@GLIBC_2.0`.
- `objdump -d build/native/melee_decomp_boot --disassemble=__cvt_dbl_usll` ->
  `117a70: c3 ret` (empty body).
- `objdump -d ... --disassemble=grBigBlue_801ECB50` -> only two `0xD4`
  accesses, no `rlwimi` insert.
- `gh api repos/doldecomp/melee/issues/3456/comments` -> `[]` (labels only;
  the acknowledgement is the triage).
- `gh api repos/alexscott2718-gif/melee/compare/master...p0/native-build-fixes`
  -> the 5-file reference diff quoted above.
- Source checks: `decomp/src/placeholder.h:13,16`;
  `native/decomp/shim/placeholder.h:15-16`;
  `decomp/src/melee/gr/grbigblue.c:3250..3314`;
  `decomp/src/melee/gm/gm_1601.c:2986,3081`;
  `decomp/src/Runtime/runtime.c:7,35,544`;
  `native/decomp/assets/hsd_convert.c:34,565,925,2233,2270`;
  `decomp/src/melee/ft/ft_0D4D.c:139,148`;
  `decomp/src/melee/ft/fighter.c:196`; `decomp/src/melee/ft/ft_0C31.c:101`.

## Exact next action

Pick one task (P-687, P-688 or P-689) and claim it in `TASKS.md`. Recommended
order: **P-689** (user asked for the visual check first and the fix is
self-contained), then **P-688 item 1** (`__fabs`, one shim line), then
**P-688 item 2** (needs the differential decision), then **P-687** (patches,
needs a gating-shape decision). Re-read the relevant upstream fork patch
before editing so the port fix and the eventual upstream merge agree.

## What I tried that did not work

n/a — investigation only. Notes that cost time:

- The issue URL says `alexscott2718-gif/melee`, but GitHub's event stream
  references `alexscott2718-gif/melee-port`; the latter holds the `p0/` docs,
  the former the code branch. Fetch the code branch from `melee`.
- `gh api .../issues/3456/comments` is empty; do not read the missing comment
  as "unacknowledged" — check the `events` endpoint for the `labeled` events.

## Open questions

- ADR-0011 gating for the new `src/` patches: mirror upstream `#else`, or
  `#ifdef PORT_PC`? Needs the owner. (no)
- `atanf` bit-exactness under GCC/SSE2 without FMA: is a differential test
  against the 64.5M-input reference available, or do we accept "closer than
  libm"? Needs the owner. (yes)
- MSL `trigf.c`: change ADR-0011 or keep host trig? Needs the owner. (yes)
- Respawn platform visual: is the symptom visible today? The owner said they
  will confirm visually; record the result in P-689. (yes)
- Training mode reachability for `__cvt_dbl_usll`: is `gm_1884` reachable in
  the port frontend today, or is this latent? (no)

## Files touched / claimed

Docs only (this commit): this handoff, `native/AI/TASKS.md`,
`native/AI/HANDOFFS.md`, `native/AI/learnings/decomp_port.md`.

Files the fixes will claim (per task): `native/decomp/assets/hsd_convert.c`,
`native/tests/test_decomp_assets.c` (P-689); `native/decomp/shim/placeholder.h`
and optionally `patches/src/melee/lb/lbtrigf.c.patch`, `native/AI/DECISIONS.md`
(P-688); `patches/src/melee/gr/grbigblue.c.patch`,
`patches/src/melee/gm/gm_1601.c.patch`,
`patches/src/Runtime/runtime.c.patch`, `native/AI/learnings/decomp_port.md`
(P-687).
