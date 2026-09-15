# Handoff: P-774 landed; G-176 sweep started; P-775 verdict recorded

**Agent-a (G-176 brief, opencode/glm-5.3-flash), 2026-09-15.** Owner asked me to
stop mid-brief. This note carries everything the next agent needs to continue.

## Landed: P-774 (commit `ddbd1dd0f`)

The four `grvenom.c` overlays through `s32* base = (s32*) &grVe_803E5348`
(`grVenom_80204F20`, `grVenom_802052E0`, `grVenom_802053B0`, `grVenom_80205F30`)
are fixed under `PORT_PC` in `patches/src/melee/gr/grvenom.c.patch`. Every far
read now names its symbol, with the offset arithmetic written at each site.

Console layout, pulled from the linked DOL's symbol table (`nm
decomp/build/GALE01/main.elf`) and checked byte for byte against its data:

```
grVe_803E5348   +0x000  0x38   (the grVe_Data struct)
grVe_803E5380   +0x038  0xC
grVe_StageCallbacks +0x044  0x140
grVe_StageData  +0x184  0x64   <- console symbol the decomp file never names
grVe_803E5530   +0x1E8  0xD4   (== base + 0x7A words, exactly)
grVe_803E5644   +0x2FC  0x28
grVe_803E566C   +0x324  0x14
grVe_803E5680   +0x338  0x14
grVe_803E56A0   +0x358  0x18
```

Resolved reads: word 14..16 = `grVe_803E5380[slot]`; `base[v + 170]` =
`grVe_803E5530[48 + v]` (VA 0x803E55F0, entries 48..52 = 3,3,3,3,6);
`base[type + 0x7A]` = `grVe_803E5530[type]`; `base[idx0 + 0xD6]` =
`grVe_803E56A0[idx0]`; `grVe_AnimData`'s `anim_args`/`anim_ids` resolve to
`grVe_803E5644` (+0x2FC; the read is `grVe_803E5644[2*xF4 + fire_kind]`, xF4
1..4, fire_kind -1..1) and `grVe_803E56A0`; the spawn triple
`base + type*12 bytes + 0x218` = `grVe_803E5530[12 + type*3]` (arwing_type
1..11 -> words 12..45). `VenomSpawnData` loses its 0x218 pad under `PORT_PC`.

Full verification ran clean at the commit: native build warning-free,
`ctest` 32/32, GameCube build `100.00% matched, 100.00% linked (1130/1130)`,
and the stage-22 fighter sweep (below).

## P-775: verdict — NOT the symbol-adjacency class; do not fix it here

After P-774, the repro (`MELEE_RNG_SEED=0x838169d0 MELEE_MATCH_P0=15
MELEE_MATCH_P1=16 MELEE_MATCH_STAGE=22`) still SIGSEGVs, but the named crash
(`grAnime_801C8138`, which came through `grVenom_802053B0`'s `base[type +
0x7A]` garbage read) is gone. The same run now dies further down the newly
correct arwing-fire path:

```
#3 HSD_JObjAddAnim+0xe3
#5 Item_80268D34+0x5f
#6 Item_80268E5C+0x346
#7 it_802E7654+0x281
#8 grVenom_80205F30+0x43c
#9 HSD_GObj_RunProcs+0x11f
```

`it_802E7654` spawns the arwing laser item (`it/kinds/itarwinglaser.h`);
`Item_80268D34` then binds an animation to the article's jobj. There is no
`(Type*) &file-scope-symbol` arithmetic anywhere in that chain — the PC is in
real code and the in-process backtrace is trustworthy here (it was not, for
P-767). This is the **unwalked-article / animation-descriptor family** — the
same shape as P-771's diagnosis (HSD_JObjAddAnim on a malformed tree) — i.e.
an `hsd_convert.c` walker gap, which is **agent-b's file**. Not fixed on
purpose. The stage-22 sweep on this build:

- 20/26 clean (P-767 took Venom from 26/26 failing to 7; P-774 to 6)
- 4 runs `SIGSEGV in HSD_JObjAddAnim` — the stack above; examples
  `p0-1/g22`, `p11-12/g22`, `p15-16/g22` (this is P-775; update its row)
- 2 runs `SIGSEGV in HSD_DObjSetFlags` — **P-765** (Fox/G&W/Kirby
  fighter-bound, all stages), not Venom-specific

## Sweep (brief step 2): started, triage not done

The full grep ran (`rg '\((char\*|s32\*|u8\*|int\*|u32\*)\)\s*&[A-Za-z_]'
decomp/src/melee/ decomp/src/sysdolphin/`), but only the sysdolphin half
surfaced (output past 60 lines was not captured). First-pass triage of what
was captured:

- **Prime candidate — `sysdolphin/baselib/hsd_3B5C.c:297..310, 555, 660`**:
  `base = (u8*) &hsd_804D2E70;` then `((s32*) &base[0x818])[component] += dc;`
  — a 0x818+ word index off a file-scope symbol. Check `hsd_804D2E70`'s size
  against 0x81C+4*component; if smaller, an instance.
- **`sysdolphin/baselib/synth.c:1379`** — `(AXPBADPCMLOOP*)((u32*)&lbl_804C4540
  ...` — read the whole expression; check offset vs the symbol's size.
- **`sysdolphin/baselib/hsd_393C.c:291..307`** — `vi_base = (u8*)&HSD_VIData
  + i * 0x60;` — fine iff `HSD_VIData` is an array of 0x60-byte records;
  verify its declared size.
- **`sysdolphin/baselib/hsd_3B34.c:11`** — `#define HSD_804D2648_BUF
  ((u8*)&hsd_804D2648)` — audit every use's offset vs the symbol's size.
- **`debugconsole_main.c:2498`** — `u8* base = (u8*)&lbl_8040AB00;` — debug
  tool only; low priority.
- **Not the class**: `hsd_3A76.c:507/509` (negative offsets from a *stack*
  local — locals of one function stay adjacent on the host too);
  `*(s32*)&scalar` bit-twiddling in psdisp.c/particle.c/mtx.h/jobj.c
  (aliases one object's own bytes); `synth.c`'s `ratioHi` u16-pair writes
  (G-098, already handled by host helpers); `hsd_3A94.c`'s
  `fn_803AC168((s32*) &cmd)` — cmd is a local.

Remaining work (claim as a new task, files `patches/src/**` + `native/AI/**`):

1. Re-run the sweep capturing the `decomp/src/melee/` half (it was truncated);
   expect `grcorneria.c` to mirror Venom (`grVenom_80204F20`'s comments cite
   `grCn_803E214C` and Corneria already carries one fix, P-744's `xC4`).
2. For each candidate, pull the symbol's size from the release DWARF
   (`gdb -ex 'print sizeof(...)'` on the decomp build, as P-745 did) and
   compare against the largest offset used.
3. Grep for doc comments claiming adjacency (`back-to-back`, `back to back`,
   `adjacent`, `emitted.*before`) across `decomp/src` — a comment claiming
   adjacency is a defect report (G-176 rule).
4. File findings in `native/AI/TASKS.md`; fix instances the same way as
   P-767/P-774 (name the symbol under `PORT_PC`, arithmetic in the comment).

## Board state

- My claim was posted to the wrong file (`native/AI/agent_communication.md`,
  now deleted); the real board is the root `AI/agent_communication.md`.
- P-774's TASKS.md row is updated with the full layout and verdict.
- P-775's row: update it with this note's verdict (crash moved to the
  arwing-laser article path; converter family; hands to agent-b).
