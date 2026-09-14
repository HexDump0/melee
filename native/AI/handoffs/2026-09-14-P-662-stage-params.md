# Handoff: P-662 — per-stage `yakumono_param` tables and `dynamicsdata_*`

**Date:** 2026-09-14
**Agent:** opencode (deepseek-v4.1-flash)
**Commit:** working tree (this commit)
**Tree state:** builds; `ctest --test-dir build/native` 19/19; asset test ASan
clean (`build/native-asan/test_decomp_assets`).

## What I did

- Audited all 65 disc archives that carry a `yakumono_param` public by
  extracting them with `melee_prototype --extract` and dumping their public
  symbols (scratch probe in `/tmp`, not committed).  `Grd<Stage>` marker
  collisions are real: GrVe carries GrdVenom and GrdCorneria, GrBb/GrNBr
  carry GrdCorneria, GrNKr/GrNSr carry GrdDonkey/GrdCastle fragments.
- Converter v78 (v77 was the first attempt; the `data == 0` record-base bug
  forced v78) adds a most-specific-first marker table and exact field tables
  for GrCn (Corneria), GrIz (Izumi), GrKg (Kongo), GrSt (Story), GrVe (Venom),
  GrOt (Onett) and GrI1 (Inishie 1), mixing u16/u32 fields; pointer fields
  stay with the relocation pass.
- Added `conv_dynamics_desc` for the `dynamicsdata_*` publics (GrCs flag3/4/6,
  GrRc shipflag): source `DynamicsDesc { data; count; Vec3 pos }` plus `count`
  0x3C-byte records.  This was the Princess Peach's Castle crash
  (`grCastle_801CD658 -> lb_8000FD48`, count 3/4/6 read as `0x0n000000`).
- `test_decomp_assets` gained `check_stage_params` (per-field raw-vs-converted
  diff; packed GrNBa/GrFs/GrFz must stay raw) and `check_castle_dynamics`
  (counts 3/4/6 and record[0] words).  Sensitivity verified with
  `MELEE_NO_ASSET_CACHE=1`: disabling the Corneria layout fails
  `yakumono_param+0 =61505 want=1106247680`; disabling the dynamicsdata branch
  fails `count=50331648 want=3`.

## Exact next action

P-658 is claimed next: add root registries for `sqEventInitDataLevelTbl`,
`gmIntroEasyTable`, `standScene`/`cut*Scene` and `dbLoadCommonData`.  Audit
method: `MELEE_NO_ASSET_CACHE=1` plus a temporary `[unknown]` print in
`convert_roots` over the archive set; the four families have their consumers
in `gm_1A45`/`gmintro`/`gm_16F1`/`gmregend` (read them for the field maps).

## What I tried that did not work

- First-hit marker scan: GrVe was classified Corneria because its publics
  start with shared Corneria texture names.  Every marker must be probed.
- Treating `data == 0` as NULL in `conv_dynamics_desc`: GrCs.dat flag3 stores
  data-section offset 0 as its record base; the record check caught it.
- Comparing raw-vs-converted for words that are relocation targets: the
  relocation pass legitimately swaps pointers in every archive; the
  "left raw" test skips relocation-backed words.

## Open questions

- Remaining `yakumono_param` layouts (target-test stages, adventure routes,
  GrBb/GrGb/GrGb/GrKr/GrMc/GrPu/GrSh/...) stay raw by design; each needs its
  own field table and a raw-vs-converted case.  Needs a human: no.

## Files touched / claimed

- `native/decomp/assets/hsd_convert.c`
- `native/tests/test_decomp_assets.c`
- `native/AI/{TASKS.md,STATE.md,HANDOFFS.md}`, `gotchas/GOTCHAS.md`

## Verification run

```
./build/native/test_decomp_assets            # exit 0; 7 layouts + 3 raw + GrCs ok
MELEE_NO_ASSET_CACHE=1 ./build/native/test_decomp_assets   # same, no cache
ctest --test-dir build/native                # 19/19 passed
MELEE_CARD_DIR=/tmp/cardfix melee --frontend ... --frames 2400  # exit 0 (Castle match survived)
ASan: build/native-asan/test_decomp_assets   # clean
```
