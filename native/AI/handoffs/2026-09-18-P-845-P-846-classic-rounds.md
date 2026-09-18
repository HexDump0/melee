# Handoff — 2026-09-18, P-845 and P-846 (the two bugs after the team battle)

Read `native/AI/AGENTS.md`, then G-220, G-221 and the P-845/P-846 rows.
Follows `2026-09-18-P-843-efb-copy-overrun.md` from earlier the same day; the
owner retested that build and reported both of these.

## P-845 — `grpushon.c:681` panic, fixed

`assertion "0" failed` from `gm_Scene_Vs_OnEnter` -> `fn_8016E730` ->
`fn_8017C7EC` -> `grPushOn_80219230`. The stage is **Race to the Finish**
(`GrNPo.dat`), Classic round `0x08`, immediately after the team battle.

`GrNPo.dat` matched no `stage_param_markers` entry, so its `yakumono_param`
was left big-endian. `grPushOn_80219230` scans that block's 0x21-slot
`{ key, value }` table for the player's character kind and returns that
character's time limit, and `fn_8017C7EC` is the stage's
`rules.on_match_start` — so **every** entry to the stage ran it. Nothing
matched a byte-reversed key, the scan ran past the `-1` terminator, and the
assert fired.

Converter **v136** adds `STAGE_PARAM_PUSHON`, keyed on `GrdPushon` (37 of the
archive's 45 publics, no other archive on the disc). Layout verified against
the disc, not inferred: six relocated descriptor pointers at 0x24 stride, a
count at +0x18, thirty `{ s32, s16, s16 }` at +0x1C, and the lookup table at
+0x10C reading `0,39  1,43 ... 25,54`. `GrNPo.dat` joins
`check_stage_params`.

Same family as P-707 (GrCs), P-770 (GrOp), P-791 (GrNFg). **Nine more archives
with a `yakumono_param` still match no marker** — GrEF1/2/3, GrGb, GrHe,
GrNBr, GrNSr, GrNZr, GrPu, GrSh, GrZe and the Target Test set — and each is a
panic waiting for whoever plays that stage. Worth a sweep rather than another
crash report.

**Hazard, and it cost a measurement here:** `hsd_asset_convert` reads the disk
cache first and the cache is keyed on converter version, so a new layout can
appear to pass before it is wired in. Re-check converter work with
`MELEE_NO_ASSET_CACHE=1` (G-199).

## P-846 — the black splash sprites, fixed

`GXSetZTexture` replaces the depth, not the colour. The port sent every
`ztex_op != 0` draw to a depth-only program that emitted the **vertex** colour
and took its Z from the **first** TEV stage.

`HSD_SObjLib_803A4A68`'s secondary-image branch — what the splash's ten tiled
sprites draw with — has TEXMAP0 as the captured opponent, TEXMAP1 as the
captured depth, two TEV stages, and **no colour attribute in the vertex
description**. Both assumptions fail at once, so the sprites painted
`vec4(0,0,0,1)`.

The main shader now writes `gl_FragDepth` from the last stage's texture; the
dedicated program is deleted. Its comment claimed Mesa/radeonsi ignores
`gl_FragDepth` from the big TEV shader — not on Mesa 26.1.6, where the
depth-erase test that justified it passes unchanged through the main shader.

`--efb` gained a pass that reproduces the sprite's draw exactly and measures
both halves of the old behaviour: vertex colour gives `left=0,0,0`, first-stage
Z gives `right=0,255,0`.

## State of the suite

ctest **32/32**, ASan clean on `--efb` and `test_decomp_assets`.
**`decomp_opening` (33) segfaults on another session's in-flight opening-movie
work** (`mod_opening.c`, `boot_main.c`, `lbmthp.c`, `dvd.c`, all uncommitted at
the time of writing). Verified with this session's changes stashed: it fails
identically without them. Do not attribute it to the renderer change.

## What the owner should see

- Classic now survives the round after the team battle.
- That team-battle VS splash's sprites should show the opponent rather than
  black rectangles. They are EFB captures whose depth capture was *also*
  mis-tiled until this morning's P-843 fix, so this is the first build where
  both halves of that screen are right.
