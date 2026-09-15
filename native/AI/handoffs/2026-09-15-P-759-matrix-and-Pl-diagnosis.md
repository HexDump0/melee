# Handoff: the soak matrix (P-759) and the `Pl*` diagnosis (P-758 head)

**Date:** 2026-09-15
**Agent:** claude (opus-5)
**Commit:** `79239c60e` (this session: `765076645`, `beac769fe`, `79239c60e`)
**Tree state:** builds clean, **ctest 32/32**. No `src/`, `patches/` or
`decomp/` change, so the GameCube build's inputs are untouched.

## What I did

- **Widened the soak to fighters x stages.** `melee_decomp_boot` takes
  `MELEE_MATCH_P0` / `MELEE_MATCH_P1` (CKind) and `MELEE_MATCH_STAGE` (StKind);
  `soak.sh` takes `MELEE_SOAK_FIGHTERS` / `MELEE_SOAK_STAGES`. `=all` is 780
  runs in ten minutes on eight cores.
- **Ran it: 240 of 780 fail, ten distinct bugs**, filed as **P-764..P-773**.
- **Diagnosed the `Pl*` gap.** 82% of the cold data in the 34 `PlXx.dat` files
  is one struct that one broken reference chain hides. See below.
- Added `MELEE_UNWALKED` and `MELEE_DUMP` probes, and
  `workflows/burn_down_descriptors.md` for whoever runs P-758.

## Exact next action

**Fix stage bugs first — they are the cheapest and each is one repro.** Five of
the ten fail for *all 26 fighters* on one stage, which means the bug is in the
stage's own data or callbacks and no fighter interaction is involved:

```sh
MELEE_NO_CARD=1 MELEE_RNG_SEED=0x838169d0 MELEE_MATCH_STAGE=22 \
    ./build/native/melee_decomp_boot --boot-frames 900 --boot-timeout 90 --boot-match 20
```

`MELEE_MATCH_STAGE` 26 (Icetop, hang), 22 (Venom), 21 (Akaneia), 10 (Mute
City), 28 (Dream Land). Then the two fighter-wide ones, `MELEE_MATCH_P0=23`
(Roy) and `=2` (Fox).

**P-764 has a strong lead worth stating:** the three affected fighters are
Roy, Pichu and Ganondorf, and those are exactly the three clones
(Roy<-Marth, Pichu<-Pikachu, Ganondorf<-Falcon). No non-clone is affected.
Look at the clone data path, not at `ftparts.c`.

**P-770's assert (`memory.c:55 "adr"`) is the same class as P-725 and P-749** —
all three are `HSD_ObjAlloc` free-list failures. Diagnose them together; one
heap-accounting bug may close all three.

### P-758's head item, fully traced

Do this before the rest of the burn-down. In `PlMr.dat` (root `ftDataMario` =
0x8de4):

```
ftData->x1C = 0x2534            part-animation table
  -> entries 0x2518/0x2524/0x2530, 0xC bytes each
     conv_ft_data converts entry+0x00 and entry+0x02 (two u16) and STOPS
  -> entry+0x08 -> 0x8d14 / 0x8d34 / 0x8d48
     arrays of animation pointers interleaved with u8 part-id runs (21 22 .. 2d)
  -> HSD_AObjDesc {flags, end_frame, fobjdesc, obj_id}
  -> HSD_FObjDesc chains at 0x9db8..0xf93c, 0x14 bytes, linked through +0x00
```

**20,623 of the 22,271 unwalked descriptors in the 34 `PlXx.dat` files are
those `HSD_FObjDesc`s, carrying 82% of the cold words.** `conv_aobjdesc`
already exists and is called from ten places — nothing new needs writing, the
chain just never reaches it. **Bound the pointer array by `c->reloc[p]`**, the
way the `vis_table` walk in the same function does; a guessed count that
overruns a fighter table is exactly P-739, and what follows is the `CMD_BE`
command scripts.

## What I tried that did not work

- **Setting the match selection by writing `gmVsMelee_StartData` from the
  VI-frame hook.** It *looks* like it works — the global holds the selection
  from the next frame onward — but `onEnterDebugVs` and `gm_Scene_Vs_OnEnter`
  run in the **same game frame**, so the hook only ever sees the value before
  the transition or after the fighters have already loaded. I nearly shipped a
  matrix that re-ran one scenario 780 times and called it coverage. What caught
  it was printing what actually loaded (`[match] loaded p0=.. grkind=..`)
  instead of what was asked for; that line is worth keeping for exactly that
  reason. The fix is to wrap `gm_Mode_DebugVs_States[0].on_enter`, which is
  ordinary writable data.
- **A first reporter that fired on the wrong match.** The boot sequence loads
  fighters of its own before GM_DEBUG_VS sticks, so `Player_GetEntity() != NULL`
  is not "the match is up". Gate on `gm_GetCurrentGameMode() == GM_DEBUG_VS`.
- **Editing `soak.sh` while a 780-run sweep was using it.** bash reads a script
  incrementally; the edit landed mid-file and the run completed all 780 children
  and then died in the report with `rintf: command not found`. Don't edit a
  running script — copy it first.
- **Believing my own first measurement of the gap.** I counted "raw words" as
  non-pointer words, which silently counted words a walker *had* converted.
  Recounting against `c->num[]` (the `cold` column) is what makes the number
  mean "still big-endian". It happened to confirm the metric is honest — 95% of
  unwalked descriptors really do contain unconverted data — but the first
  number was not evidence for that.
- **Reading `conv_ft_data` to find which `ftData` fields it skips.** Grepping
  `off + 0x` misses `off + (pass == 0 ? 0x0C : 0x14)`, so I briefly concluded
  `xC`/`x14` were never read when they are. Measuring which descriptors end up
  cold is reliable; reading a 600-line walker is not.

## Open questions

- The matrix runs one seed per cell. Whether the ten bugs are seed-independent
  is unverified — worth one `MELEE_SOAK_SEEDS=5` matrix run (50 min) before
  anyone concludes a fix is complete. Needs a human? **no**.
- Stage ids 0 (Dummy) and 1 (Test) are excluded from `all`, and so are the
  non-playable CKinds 26+ (Master Hand, wireframes, Giga Bowser). Those are
  reachable in 1P modes the soak never enters. Needs a human? **no**.
- Nothing here tests rendering. Every open visual bug (P-699, P-702, P-730,
  P-732, P-740, P-741, the shield bubble, the foliage artifact in
  `f67643a76`) is invisible to this harness, and gate 4 of ADR-0023 — the owner
  playing an evening — is still the only detector for that class. Needs a
  human? **yes, eventually.**

## Files touched / claimed

- `native/decomp/boot/match_boot.c` (fighter/stage selection)
- `native/tests/soak.sh` (matrix mode, culprit filter, overnight recipe)
- `native/decomp/assets/hsd_convert.c`, `native/tests/test_decomp_assets.c`
  (`MELEE_UNWALKED`, `MELEE_DUMP`; no conversion change, no version bump)
- `native/AI/{STATE,TASKS,TESTING}.md`,
  `native/AI/workflows/burn_down_descriptors.md`, `AI/agent_communication.md`

## Verification run

```
$ ctest --test-dir build/native
100% tests passed out of 32

$ MELEE_SOAK_SEEDS=1 MELEE_SOAK_FIGHTERS=all MELEE_SOAK_STAGES=all \
      native/tests/soak.sh ./build/native/melee_decomp_boot /tmp/soak-matrix
soak: 780 runs -- 1 seeds (... base 0x838169d0), 26 fighters, 30 stages, 900 frames
soak: 540 passed, 240 failed, 595s wall

   54  ftparts.c:793 assertion "0"                  Roy/Pichu/Ganondorf, 27 stages
   52  SIGSEGV in HSD_DObjSetFlags                  Fox/G&W/Kirby, 26 stages
   26  hang: boot timeout (SIGALRM) in Ground_801C0754   all fighters, Icetop
   26  SIGSEGV in grYt_StageCallbacks               all fighters, Venom
   26  SIGSEGV in Ground_801C0754                   all fighters, Akaneia
   24  mplib.c:4804 assertion "0"                   25 fighters, Mute City
   22  memory.c:55 assertion "adr"                  24 fighters, Dream Land
    5  SIGSEGV in HSD_JObjAddAnim                   10 fighters, Corneria
    3  lbvector.c:383 position sanity               6 fighters, Zebes + Onett
    2  hang: boot timeout (SIGALRM) in HSD_Randi    4 fighters, Icicle Mountain

$ # Pl* diagnosis
PlXx.dat (34 files):  20.2% walked, 22,271 unwalked, 84,563 cold words
  of those, 20,623 descriptors / 68,984 cold words (82%) are 0x14-byte
  HSD_FObjDesc -- confirmed field for field against fobj.h:53 with MELEE_DUMP
costume files (207): 75.9% walked
```
