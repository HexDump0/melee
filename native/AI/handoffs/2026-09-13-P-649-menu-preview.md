# Handoff: P-649 main-menu 1-P preview submenu (RESOLVED 2026-09-13)

**Resolution:** fixed by the P-650 `HSD_TexAnim.id` conversion (converter
v67, G-114). The panel text bands now render (verified in
`/tmp/menu_post.bmp`: 4 text bands under the emblem where rows 310+ were
empty before) and the owner confirmed. The BRANCH-track/fterm analysis
below was a red herring for the visible symptom — the lines were
two-texture quads whose nonzero-map TexAnim never bound, same as the title
fire. Kept for the debugging record.

**Agent:** Muse Spark. **State:** investigation parked, no code changed.
**Task row:** P-649 in `native/AI/TASKS.md` (status `blocked`, this note linked).
**User-visible symptom:** main menu with 1-P Mode highlighted shows an empty
teal preview panel on the right; Dolphin GALE01 shows 4 lines there
(Regular Match / Event Match / Stadium / Training).

## Repro (deterministic, headless)

`native/tests/frontend_vs.txt` does NOT reach this state (it navigates into
the VS submenu). Use a minimal script that enters the menu and stops:

```
# /tmp/tomain2.txt (throwaway, NOT committed)
channels 1
240 * a
245 * -
300 * a
305 * -
660 * start
665 * -
```

```sh
SDL_AUDIODRIVER=dummy SDL_VIDEODRIVER=offscreen \
  ./build/native/melee --frontend --no-items --input /tmp/tomain2.txt \
  --frames 950 --shot /tmp/tomain2_950.bmp
./build/native/melee --frontend --no-items --input /tmp/tomain2.txt \
  --frames 950 --dump-draws 950 > /tmp/dumpT.log 2>&1
```

At 950: `mn_804A04F0.cur_menu=0` (MAIN), `hovered=0` (1-P) via gdb.
`/tmp/tomain2_950.bmp`: yellow banners render (~37k yellow px), bottom
"Solo Smash!" glyph line renders (dim but present, y≈660-684), title glyphs
render, right panel shows border + emblem only, no 4 lines.
`--dump-draws` (`/tmp/dumpT.log`, `/tmp/dump900.log`, `/tmp/dump950.log`):
only 15 title glyphs + 10 bottom-line glyphs + NEXTSCREEN fragments as
32x32 I4 quads; NO glyph draws exist for the submenu lines.

## Choreography decoded (don't re-derive)

- `GM_TITLE=0, GM_MENU=1, GM_VS=2` (`decomp/src/melee/gm/forward.h`).
- `MENU_KIND_MAIN=0, _1P=1, _VS=2, ...` (`decomp/src/melee/mn/forward.h`).
- frontend_vs timeline: menu entered ~660, down@700 moves MAIN 1-P→VS,
  start@760 enters VS (`cur=2, prev=0` at 760+). Frame 900 of frontend_vs is
  the VS menu, NOT MAIN+1P. Verified via gdb prints of `mn_804A04F0`.
- `match_view.frames` (`viewer_main.c:797`) tracks game frames 1:1 here.

## What the preview is (key narrowing)

The right preview is NOT SisLib text. The only text `mnmain.c` creates is
the bottom description (`mn_80229A7C` → `HSD_SisLib_803A5ACC/803A6368`,
works). The preview is joint/texture animation on `MainMenuData.tree[14]`
(`hover_jobj`), driven per-frame by `mn_8022ADD8` → `mn_8022ED6C` with
`anim_loop[hovered]` (`mn_803EB6B0`, static, verified sane:
`EB3FC[0]={0,49,20}`, count=5).

## Ruled out (with evidence)

1. **Font atlas content**: Python CISO scan of the DOL region: 286/287
   SisLib entries nonzero (only #227 empty), mean fill 33%. Atlas is full.
2. **TEV stage leak**: `gx_gl.c` clamps with `if (i >= u_stages) break`
   (`u_stages = s->num_stages`). The C1/K0 difference between working and
   dim glyph draws is captured-but-unused state (glyph TEV uses C0+TEXA).
3. **FObjDesc corruption**: `HSD_FObjDesc` is
   `{next@0, length@4, startframe@8, type@12, frac_v@13, frac_s@14,
   dummy@15, ad@16}`; `conv_aobjdesc` swaps only +4/+8, leaves type/frac
   bytes alone (correct). `parseFloat` in `fobj.c` assembles bytes
   explicitly (endian-neutral); fighters animate fine.
4. **Pointer corruption**: walked 19 t14-subtree joints + whole 96-joint
   display tree in gdb; all child/next/aobj/dobj pointers sane.
5. **Missing flag-op code path**: traced all `HSD_JObj{Set,Clear}Flags{,All}`
   for 950 frames — nothing ever touches the hidden joints below.

## Prime suspect (unconfirmed): 5 hidden joints with BRANCH tracks

Full-tree sweep finds 16 hidden joints; 5 under `tree[14]`
(`0x80e74be0/c80/d20/dc0/e60`, 0xA0 spacing, heap addresses stable across
runs but re-derive per run) each with aobj+dobj, animating (frame 26→27):

| joint | track | fterm | texture (decoded from MnMaAll.dat @disc 0x55d58000) |
|---|---|---|---|
| …be0 | BRANCH CON p0=0 p1=1 | 600 | 56x80 trophy icon |
| …c80 | BRANCH CON p0=0 p1=1 | 450 | 216x144: 4 columns icon+label+button |
| …d20 | BRANCH CON p0=0 p1=1 | 900 | 144x112: bordered text box + grid |
| …dc0 | BRANCH CON p0=0 p1=1 | 1000 | 64x64 |
| …e60 | BRANCH CON p0=0 p1=1 | 1050 | 32x32 |

BRANCH fires (`ClearFlags HIDDEN`) only when `time >= fterm`, but the
preview anim loops in [0,49] (`mn_8022ED6C` with loop=20), so these can
never fire in this state — same math on console. Either they belong to
other states (their fterms fit the 1P submenu's 400–649 range better), or
the time base is misunderstood. The 216x144 rotated 90° would give 4 rows,
so it may still be the 1P content shown through a rotated texmtx.

Other hidden groups (type 9 = non-visibility tracks, 128x48 textures x5)
were not pursued.

## Suggested next steps

1. Sample the same subtree across a full flow (MAIN → 1P submenu → back)
   to see which joints unhide in which state (probes in /tmp are gone;
   recreate from the recipes above; heap addresses are deterministic).
2. If a set unhides in 1P submenu, compare its fire frames vs Dolphin to
   find the timing/eval gap (`HSD_A_J_BRANCH/NODE` in `jobj.c:420`).
3. If nothing ever unhides, suspect the preview-content selection
   (`anim_loop` index vs `mn_80229A04` unlocked mapping) or matanim
   texture selection (parked P-209).

## Gotcha candidates (not yet filed)

- White-pixel counting misses yellow banners and dim text; always count
  per-color and ascii-render regions before concluding "nothing renders".
- `frontend_vs.txt` frame 900 is the VS menu, not MAIN+1P; it needs its
  own minimal script (above) for the MAIN+1P state.
- 32-bit relays: `match_view.frames` gate + `continue` pattern for gdb;
  heap addresses observed deterministic across runs; prefer `printf`
  over python-in-gdb (python `call` of game functions segfaulted the
  inferior once — gdb artifact, game itself is fine).
