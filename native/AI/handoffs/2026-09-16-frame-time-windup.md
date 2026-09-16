# Handoff: frame time — what landed, what was measured and rejected, what is open

**Date:** 2026-09-16
**Agent:** claude (opus-5, 1M)
**Brief:** the owner asked for the game to run faster. Wound up at his request.

## Result

A match's CPU work is **down 49.5%** — 66.04e9 → 33.34e9 instructions over a
900-frame headless match, pinned seed, pinned core. Owner confirms it "feels
much much better". His `game=` phase went 13.8 ms → 5.7 ms in a Giant round.

## Landed

| Commit | What | Worth |
|---|---|---|
| `4a9d64be6` | P-813: `gx_hle_begin_frame` reset `frame_tcount` without clearing `frame_tex_hash` | **-19.7%** |
| `661e14e0a` | P-814: `GXInitTexObj` dropped the decoded GL texture on **every** call | **-37.1%** |
| `5065f21a7` | P-816: texgen matrices resolved once per primitive, not per vertex | -0.77% |
| `6bf477bcc` | P-818: `GX_HLE_MAX_DRAWS` 1024 → 4096; both geometry caps now report | — |
| `201dae594` | P-819: print `swap=`; `MELEE_SWAP_INTERVAL` policy lever | — |

Everything is byte-identical against the pre-change binary. Captures at
frames 200/420/700 and across stages 2/8/24; ctest 33/33 throughout.

## The two that mattered, and why they hid

Both were **caches that had stopped being caches**, with no symptom but frame
time.

P-813 stayed *correct* the whole time — `frame_tex_find` rejects stale
indices — while degenerating into a scan, and because `GXLoadTexObj` only
records a key when the lookup returns an **empty** slot, insertions quietly
stopped as the table filled. Cost grew with accumulated history, not with the
scene, which showed up first as **benchmark variance** (0.35% spread on a
pinned seed, → 0.02% after the fix). Unexplained variance was evidence.

P-814: 26,187 decodes per 300 frames, 18,198 of them immutable CMPR disc art,
against **166** evictions. See G-201.

## Measured and rejected — do not retry on reasoning alone

| | Reasoning | Measured |
|---|---|---|
| P-812 `texobj_find` hash | 128-slot linear scan, 7.8% of cycles | **wash** (7.8% was the dedup inlined into `GXLoadTexObj`) |
| P-817 shrink the vertex | `uv[8][3]` is 96 of 144 B, 21 MB/frame | **0.17%** |
| `read_comp` inline | earlier session | 0.01%, reverted |
| P-820 `-O3` | free 2.5% | **2.5%, but changes rendered output**; not FP contraction |

G-202 and G-203 are the lessons. Short version: a line is hot because that is
where the work is, not because there is waste in it; and a byte-identical
capture only proves anything at a **fixed compiler configuration**, so it is
the wrong instrument for judging `-O3`/LTO/PGO.

## Open

- **P-821 — web-only wrong opponent costume.** Owner's live bug, still
  unexplained. I attributed it to the converter; that attribution was wrong,
  because the converter is shared and native renders costumes correctly. The
  unrun discriminating test is in the row. **Do not close it against P-820.**
- **P-822 / P-820 — the vis_table NULL-hole ordering difference was real and
  is fixed** (melee-a4, `80effa94f`, converter v130). It measured 13 of 17
  tables stopping on a NULL hole with 2–14 live slots behind. Worth separating
  from the paragraph above: the *defect* was found by reading code and was
  correctly flagged as needing measurement before anyone acted on it; what was
  wrong was pinning a browser-only symptom on it without first checking the
  symptom reproduced on the same target.
- **P-802 — should be re-scoped or closed.** Its premise (21 MB/frame vertex
  upload is expensive) measured at 0.17%.
- **The only large win left** is transform + texgen in a vertex shader.
  Ablation bounds it: removing texgen is **-18.2%** of all instructions,
  removing the whole transform is **-28.9%** — so roughly half of render.
  Minus new per-draw uniform cost, and it cannot be verified with
  byte-identical captures, which needs solving up front.

## Method note

Benchmark harness that made all of this legible:

```sh
SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy MELEE_NO_CARD=1 \
  MELEE_RNG_SEED=0x7f41a695 taskset -c 2 \
  perf stat -e instructions ./build/native/melee --match --frames 900 --no-hud
```

**Pin the seed.** Without it the tree's `MELEE_RNG_SEED=tick` gives a
different match each run and the spread is 2.2% — wide enough to hide
everything I found. Pinned, it is 0.04%.

New probes: `MELEE_GX_TEX_STATS=1` (hits/misses/evictions/decodes/
invalidations), `MELEE_GX_TEX_INVALIDATE=all` (restores pre-P-814 behaviour
for A/B), `MELEE_SWAP_INTERVAL`.
