# Handoff: P-704 — fighter names still missing on the real Classic VS splash

**Date:** 2026-09-14
**Agent:** claude (opus-5)
**Last commit:** `aa023c7ba` (P-703)
**Tree state:** builds clean; the two harness env vars (`MELEE_INTRO_US`,
`MELEE_CLASSIC_INTRO`) in `native/decomp/boot/match_boot.c` were kept
uncommitted while the fix was open and are **committed with it** (see
"Resolution").

## Resolution (2026-09-14, opencode/deepseek-v4.1-flash)

The `xEF` count-index lead below was a dead end: the throwaway probe showed
`xEF=1` and all three `x57C` rows sane (`pos=140,380`, `scale=1.0`).  The real
cause is one level down — `fn_80160DE8` (`gm_1601.c`) picks the US name width
with `lbl_803B75F8[ckind + 0x21]` (and `+0x42`/`+0x63`), **past the end of the
33-entry table**.  Retail resolves those into `lbl_803B767C` /
`lbl_803B7700` / `lbl_803B7784`, which the console linker placed back to
back; GCC's `-fdata-sections` puts each array in its own section, so the
reads return `0.0`, `HSD_SisLib_803A7548` stores a width of 0 as an x-scale
of 0 (G-152), and every glyph draws at zero width.  Only the US branch uses
the offsets — the JP path is in bounds — which is why the JP harness always
looked correct while the owner's US save showed nothing.

Fixed under `PORT_PC` in `patches/src/melee/gm/gm_1601.c.patch` by naming the
array each offset resolves to, as the sibling `gm_80160B40`/`gm_80160C90`
already do.  `ctest decomp_intro_names` forces US through `MELEE_INTRO_US` and
counts white pixels in the name row: **0 broken, 2457 fixed**.  The throwaway
`printf` probes are reverted; `ctest` 28/28; GameCube `ninja` 100.00% linked
with the patch applied.  New gotchas: G-154 (linker-adjacency reads) and G-155
(pick a probe statistic that flips).  The sections below are kept as the
diagnostic record.

## Why this exists

P-703 (`aa023c7ba`) fixed the *encoding* of the fighter-name literals
(`-fexec-charset=CP932`, G-150) and `ctest decomp_classic_names` proves the
table now holds Shift-JIS.  The owner rebuilt and reported that the names on
the Classic VS splash are **still not drawn**:

> "I still dont see the names in ./build/native/melee"

So there is a *second*, independent cause on top of the encoding one.  That is
this task.  The encoding fix is real and must stay — do not revert it.

## The screen and the code that draws it

The "VERSUS!!" splash (two fighters side by side, squiggle banner across the
top, names along the bottom) is **`GS_INTRO_EASY`**, `decomp/src/melee/gm/gm_1832.c`.
`gmscdata.c:283` registers `gm_Scene_IntroEasy_OnEnter` / `_OnFrame`.
`fn_80160DE8` (`gm_1601.c:833`) — the name writer — has exactly two callers,
both in `gm_1832.c`, so there is no other scene to look at.

Chain, end to end:

```
gmClassic_801B3500            gmclassic.c:824   fills gmClassicIntroData (0x20 B)
  -> gm_Scene_IntroEasy_OnEnter gm_1832.c:1204  copies it into lbl_8047368C
       -> fn_80186634           gm_1832.c:1116  loads GmIntEz.dat, switches on model_scale_kind
            -> fn_801861B8      gm_1832.c:970   CREATES the name HSD_Texts (x4[7+i], x4[10+i])
       ... later, state 0x5     gm_1832.c:441   fn_80160DE8(x4[7+i], xF1[i], ...) draws them
           state 0x6            gm_1832.c:490   same for the enemy names (x4[10+i])
```

### Field map (verified, both structs read side by side)

`gmClassicIntroData` (gmclassic.c:40) and `lbl_8047368C` (gm_1832.c) are the
same 0x20 bytes:

| off | gmClassicIntroData | lbl_8047368C | meaning |
|-----|--------------------|--------------|---------|
| 0x00 | `x00` s32 | `model_scale_kind` | 3 if `entry->x1 & 0x80`, else 4/1/2/0 |
| 0x04 | `x04` s32 | `game_type` | only set when `x00 == 3` |
| 0x08 | `x08` u8 | `xEC` | allstar slot |
| 0x09 | `x09` u8 | `xED` | **nametag id**, 0x78 = none |
| 0x0A | `x0A` u8 | `xEE` | stage index + 1 |
| 0x0B | `x0B` u8 | `xEF` | **ally / name count** (`ally_count`) |
| 0x0C | `x0C` u8 | `xF0` | enemy count |
| 0x0D | `x0D[3]` | `xF1[3]` | ally ckinds |
| 0x10 | `x10[3]` | `xF4[3]` | enemy ckinds |
| 0x13 | `x13[3]` | `xF7[3]` | ally costumes |
| 0x16 | `x16[3]` | `xFA[3]` | enemy costumes |
| 0x19 | `x19[3]` | `xFD[3]` | ally flag bit5 |
| 0x1C | `x1C[3]` | `x100[3]` | enemy flag bit5 |

## What I ruled out (with the evidence)

1. **The `ClassicModeEnterData` int/u8 mismatch is NOT an endianness hazard.**
   This was the open lead when the last session ended.  `gm_1832.c:1217` is
   `*(ClassicModeEnterData*) &lbl_8047368C.model_scale_kind = *arg0;` — a
   plain 0x20-byte struct assignment.  The `int x8/xC` declarations in
   `ClassicModeEnterData` (gm_1832.c:1187) only name the bytes; nothing reads
   them as ints on either side of the copy.  **Dead end, do not re-chase.**

2. **`model_scale_kind == 3` skipping the names is NOT it for normal stages.**
   `gmClassic_801B3500` sets `sd->x00 = 3` only when `entry->x1 & 0x80` (the
   bonus/target stages).  A normal 1-on-1 stage gives 0/1/2/4, and
   `fn_80186634`'s cases 0/1/2 and 4 both call `fn_801861B8()`.
   Related correction: `fn_80185D64` (gm_1832.c:861) does **not** create text —
   it is `Player_80036E20` model preloading.  The text creator is
   `fn_801861B8` (gm_1832.c:970).  My earlier note saying otherwise was wrong.

3. **The `MELEE_INTRO_TEST` harness exercises the same branch retail does.**
   `gm_1832.c:442` picks between `fn_80160DE8` (data-driven scale) and a
   `HSD_SisLib_803A70A0` + `803A7548(.., 1.0f, 1.0f)` shortcut on
   `i != 0 || xED == 0x78`.  `un_80301BA8` (`soundtest.c:2109`) sets
   `out->unk_9 = 0x78`, i.e. `xED = 0x78`, so the debug route takes the
   **`fn_80160DE8` branch**, exactly like a retail save with no name tag.
   The harness is therefore a valid reproduction of the draw path — which is
   why "it works in the harness" is genuinely surprising and points at the
   *data*, not the draw code.

4. **`gmIntroEasyTable` is converted, including the splash rows.**
   `conv_intro_easy_table` (`native/decomp/assets/hsd_convert.c:1926`) swaps
   `x57C[3]` as 12 f32 each.  A plain missing byte-swap on an in-range row is
   out.

5. **`lbl_803B75F8[ckind + 0x21]` data adjacency (from the last session).**
   Rewriting it to `lbl_803B767C[ckind]` changed nothing (964 vs 964 white
   pixels) — the linker keeps the tables adjacent.  Reverted; do not redo.

## The live lead: `xEF` used as a table *index*

`fn_801861B8` and the `case 0x5` draw both index the splash-layout table by
the **count**:

```c
lbl_804D6604->x57C[lbl_8047368C.xEF].x00[i]   /* position  */
lbl_804D6604->x57C[lbl_8047368C.xEF].x18[i]   /* scale     */
```

but `x57C` is `ClassicSplashRow x57C[3]` — valid indices 0..2.  Same shape at
`gm_1832.c:202`: `lbl_804D6604->x00[lbl_8047368C.xEF - 1]` into
`ClassicSlotVals x00[2]`.

`xEF` is `ally_count` from `gmClassic_801B3500`:

```c
ally_count = 1;
for (i = 1; i < 3; i++) {
    sd->x0D[i] = gm_8017DB6C((gm_8017DB6C_arg0_t*) ad->x0.xC.x24, i - 1);
    if (sd->x0D[i] != 0x21) ally_count++;
}
sd->x0B = ally_count;
```

On a normal Classic stage retail gets `ally_count == 1` (the two ally slots
return `0x21` = "none").  **If our `ad->x0.xC.x24` block holds different bytes
— it is filled by `gm_8017DB88` just above — `ally_count` becomes 2 or 3, and
3 walks off the end of `x57C` into `pad_60C`/`x630`.**  A garbage scale there
is fatal in a silent way: `HSD_SisLib_803A7548` (`hsd_3A64.c:481`) stores the
scale as 8.8 fixed point (`(u8) scale`, `(u8) (256.0f * scale)`), so anything
in (0,1/256) or ≥ 256 quantises to **0** and the glyphs render at zero size —
invisible, no error, no missing draw.

That is the single most likely mechanism for "banner decoration present,
fighters present, text absent", and it explains the harness/real split: the
debug route's `xEF` comes from `un_803FA258.x10C` (a debug-menu value that is
in range) while the real route computes it.

## Exact next action

**Do not use gdb for this.**  I tried twice and both runs timed out before the
breakpoint fired: under `gdb -batch` the game manages roughly **0.6 frames per
second** through the menus, and `fn_801861B8` is ~500 frames past boot, so the
breakpoint is 10+ minutes away.  The same run without gdb reaches frame 900 in
well under a minute.  What the gdb runs *did* confirm before dying (with
`MELEE_VIEWER_TRIAGE=1`, G-151):

```
[classic] frame 110: mode 24        <- reached GM_CLASSIC
[classic] name_lead=82 sjis=1       <- P-703's encoding fix is live in this build
[classic] -> intro (state 112)      <- MELEE_CLASSIC_INTRO is stepping the state machine
```

So the harness route works; only the readout method was wrong.

Instead, print the numbers from inside the scene.  `lbl_8047368C` and
`lbl_804D6604` are both file-static in `gm_1832.c`, so the probe has to live
there — add a throwaway `printf` at the top of `fn_801861B8`
(`gm_1832.c:970`), rebuild (`ninja -C build/native melee`, incremental, a few
seconds), run without gdb, then **revert the decomp edit**:

```sh
SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy MELEE_VIEWER_TRIAGE=1 \
  MELEE_CLASSIC_TEST=1 MELEE_CLASSIC_INTRO=1 MELEE_NO_CARD=1 \
  ./build/native/melee --match --frames 900 --no-hud --shot /tmp/vs.bmp
git -C decomp checkout -- src/melee/gm/gm_1832.c   # afterwards
```

Print `xEF`, `xF0`, `model_scale_kind`, `xED`, `xF1[0..2]`,
`lbLang_IsSavedLanguageUS()`, and all three `x57C[0..2]` rows' `x00[0]`,
`x0C[0]`, `x18[0]`, `x24[0]`.  If this turns into the real fix, the probe
belongs in `match_boot.c` behind an accessor rather than in the decomp.

<details>
<summary>The gdb script I used, if you want it anyway (slow — see above)</summary>

```sh
cat > /tmp/intro.gdb <<'G'
set pagination off
set confirm off
break fn_801861B8
run
printf "xEF=%d xF0=%d msk=%d gt=%d xED=%d\n", lbl_8047368C.xEF, lbl_8047368C.xF0, \
       lbl_8047368C.model_scale_kind, lbl_8047368C.game_type, lbl_8047368C.xED
printf "ck0=%d ck1=%d ck2=%d\n", lbl_8047368C.xF1[0], lbl_8047368C.xF1[1], lbl_8047368C.xF1[2]
printf "row[xEF] x00=%f x0C=%f x18=%f x24=%f\n", \
       lbl_804D6604->x57C[lbl_8047368C.xEF].x00[0], lbl_804D6604->x57C[lbl_8047368C.xEF].x0C[0], \
       lbl_804D6604->x57C[lbl_8047368C.xEF].x18[0], lbl_804D6604->x57C[lbl_8047368C.xEF].x24[0]
printf "US=%d\n", lbLang_IsSavedLanguageUS()
kill
quit
G
SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy MELEE_VIEWER_TRIAGE=1 \
  MELEE_CLASSIC_TEST=1 MELEE_CLASSIC_INTRO=1 MELEE_NO_CARD=1 \
  gdb -q -batch -x /tmp/intro.gdb --args \
  ./build/native/melee --match --frames 900 --no-hud
```
</details>

Read the result like this:

- `xEF != 1` on a normal stage → the bug is upstream in `gm_8017DB88` /
  `gm_8017DB6C` / `ad->x0.xC.x24` (an allstar/ally block we are filling or
  converting wrongly).  Fix it there; do **not** clamp the index.
- `xEF == 1` but `x18`/`x24` are 0 or absurd → the converted `GmIntEz.dat`
  row is wrong after all; re-check `conv_intro_easy_table`'s offsets against
  the struct at `gm_1832.c:127` (note the `pad_570[0xC]` before `x57C`).
- Everything sane → the text exists and the failure is downstream, in
  `HSD_SisLib_803A70A0`'s entry write or `fn_803A6FEC`'s entry lookup; put the
  next breakpoint on `HSD_SisLib_803A7548` and check that `entry != NULL`.

## Traps that cost me time (do not repeat)

- **`boot_triage_note()` output is swallowed unless `MELEE_VIEWER_TRIAGE=1`
  is set.**  `viewer_main.c:1063` points the triage stream at `/dev/null`
  otherwise.  My first Classic run printed no `[classic]` lines at all and I
  briefly thought the harness had not reached the scene.  This applies to
  **every** `[gameover]`/`[intro]`/`[classic]` probe line in `match_boot.c`.
- Gate gdb breakpoints on a scene-specific static, never on `frame_dcount`
  (it resets every frame) — see the P-697 handoff.
- Pixel probes are a bad oracle for this screen: text inside a fitted box
  rescales, and a broken encoding renders garbage glyphs rather than nothing
  (G-149, G-150).  Check the data, not the pixels.

## Working tree

`native/decomp/boot/match_boot.c` carried these two switches uncommitted while
P-704 was open (the owner's rule: a commit lands a completed fix, not a
harness); they are committed with the fix:

- `MELEE_INTRO_US` — forces `gmMainLib_GetGamePrefs()->saved_language = LANG_US`
  in the `MELEE_INTRO_TEST` route, so the debug splash exercises the US name
  table instead of the JP one.  (This is why the earlier `names.png` showed
  Japanese names — a harness artifact, not a bug.)
- `MELEE_CLASSIC_INTRO` — steps `GM_CLASSIC` onto its own state id 0, the real
  `GS_INTRO_EASY` with `gmClassicIntroDataBuffer`, instead of borrowing the
  debug state table.

Fold both into whatever commit lands the P-704 fix.

## Still open alongside this

- **P-699** — ~13 px sliver at the left of the clear-screen banner backdrop.
- **P-702** — the player name plate on the CSS is an empty black box
  (`mnNameDefaultName`/`mnNameAutoName`/`mnNameRefuseName` are unhandled
  converter roots).
