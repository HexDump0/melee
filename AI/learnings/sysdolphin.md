# 🐬 SysDolphin (`baselib/`) Learnings & Insights

SysDolphin (HSD) is HAL Laboratory's proprietary GameCube rendering and scene management library located in `src/sysdolphin/baselib/`.

---

## 1. Object Model

SysDolphin implements an object-oriented class hierarchy in C:
* **`HSD_Class`**: Base class with virtual function table (`HSD_ClassInfo`).
* Object allocation usually goes through:
  ```c
  void* obj = HSD_MemAlloc(size);
  ```
  or per-class memory pools via `hsdObjAlloc`.

---

## 2. Joint Graph (`HSD_JObj`)

* Melee characters and stages use hierarchical skeletal rigs represented by `HSD_JObj`.
* `jobj->child`: Pointer to first child joint.
* `jobj->sibling`: Pointer to next sibling joint.
* Common tree traversal pattern:
  ```c
  void traverse(HSD_JObj* jobj) {
      if (jobj == NULL) return;
      // Process joint...
      traverse(jobj->child);
      traverse(jobj->sibling);
  }
  ```

---

## 3. Display Lists & GX State

* `GX`: Nintendo GameCube graphics hardware API.
* Files in `src/dolphin/gx/` interact directly with the command processor (FIFO).
* In HSD files (`pobj.c`, `dobj.c`), display lists (`HSD_PObj`) contain vertex data and primitives rendered via GX commands.

---

## 4. hsd_803B3408 (JPEG RGB565->YCbCr, 2026-09-07, MuseSpark)

- Status: 98.78% (217 instr, 23 mismatches), baseline from #3350. No PR open. Prior claim by Antigravity (Gemini) had zero tree changes — was going in circles, took over then released.
- All mismatches are integer register allocation in chroma loop, float ops match:
  - `add r26,r8,r26` vs `add r26,r26,r8` (dst_row operand order, 1 instr)
  - `rlwinm r22` vs `r21`, `clrlslwi r21` vs `r5`, `add r5,r22,r29` vs `add r5,r21,r5`, etc. (src offset grouping B+src vs B+A, ~10 instr)
  - `clrlwi r23` vs `r22`, `add r21,r23,r22` vs `r23,r22,r21`, pixel unpack `rlwinm/xoris/stw` r22/r23 vs r21/r22 (~10 instr)
  - `add r22,r4,r21` vs `r30,r4,r21`, `stw 0x518(r22)` vs `(r30)`, `stw 0x618(r22)` vs `(r30)` (dest base r22 vs r30, 2 instr)
  - `li r26,0` vs `addi r5,r5,0x118` order swap at luma prologue (2 instr)
- Tried (all no-op or worse, reverted to baseline):
  - `dst_row += tile...` (same), single-expr dst (94.5%, worse), src `(B+src)+A` vs `A+(B+src)` vs `src+B+A` (all same 98.78% — MWCC O4 reassociates integer adds regardless of parens)
  - `src_offset` temp with `src[src_offset]` reuse (97.85%, +1 instr DIFF_INSERT — extra var costs an instr, avoid new s32 locals here)
  - swap decl `dst_row`/`src_row` (98.68%, swaps r26/r29 — original order correct), swap `chroma_index`/`chroma_dest` (no-op)
  - swap computation order src before dst (97.7% with diff markers — dst first is correct)
  - luma split `base; pixel_index=0; base+=0x118` (98.66%, +4 mismatches — original for-cond assignment is closer)
  - `chroma_index = dst+A+B` single expr (94.5%, worse — keep 3-step B, A+B, +dst)
- Hypothesis: MWCC groups `A+(B+src)` as `(A+B)+src` (computes B+A first in r5) while target groups `(B+src)+A` (B+src first). Parens alone don't force it at O4,p; needs sequencing via existing vars (not new temps) or different spelling (`<<` vs `*`, `%` vs `&`) that still fuses to rlwinm/clrlslwi. Also try scoping `{ }` per general_tips §1 to flip r21/r22 without new stack slots.
- Next: try decomp.me with MWCC 233/163n O4,p, or brute-force src/dst spellings measuring via `objdiff-cli diff -1 obj -2 src hsd_803B3408` + `python3 /tmp/check2.py` (0.1s rebuild). Luma 2-instr swap may be easier separate win. Nearby easier targets: hsd_803B3CD8 99.51%, fn_803B6820 98.8% AVAILABLE.

### Round 2 brute force (MuseSpark, same day, user asked to keep going)
- V1 scope first dest store in `{ }`: same 98.7788% (scoping dest alone doesn't flip r22/r30).
- V2 `<<1/<<2` for src `(chroma_x&1)<<1`, `(chroma_x&2)<<2`: 98.29% worse — keep `*2/*4` (fuses to rlwinm/clrlslwi correctly).
- V3 `<<2/<<4/<<5` for dst/src `(chroma_y&1)<<2` etc.: same 98.7788% (dst `*` vs `<<` equivalent).
- V4 named `b_part=(chroma_x&2)*4` reused for src+dst: 98.17% worse (+extra var costs instr, avoid new s32 locals).
- V5 `chroma_index<<2` vs `*4`: same. V7 `u32 chroma_x` vs `s32`: same. V11/V12 `register` on dst/src/idx: same (ignored).
- V10 eliminate `chroma_index` (dest directly `BUF+((A+B+dst)*4)`): 95.54% worse — keep 3-step `B, A+B, +dst` (matches target's 2 adds).
- Conclusion: baseline #3350 is local optimum for this spelling; parens/`*` vs `<<`/`register`/scoping dest don't move r21/r22/r5. Root is MWCC temp sharing (A2/src-addr share r5 vs A2/dst share r21). Need fresh structure (e.g., dst-first vs src-first with cse barrier via existing vars, or decomp.me scratch with full ctx) not random tweaks. Stopping brute force to avoid Gemini-style circles.
