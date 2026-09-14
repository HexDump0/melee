# Handoff: P-697 — black band across the top of the 1P clear screen

**Date:** 2026-09-14
**Agent:** claude (opus-5)
**Commit:** follows `37acc1442` (P-696); P-697 itself is **unfixed**, analysis only
**Tree state:** builds clean, `ctest` 23/23, `ninja` in `decomp/` 100.00% matched

## What I did

- Fixed the crash that hid this screen entirely (P-695, `2da21764e`) and the
  magnifier's 400 ms frames (P-696, `37acc1442`).  With those in, the owner
  reached the 1P clear screen and reported a black box over its top quarter;
  I reproduced it headlessly and localised it to a single draw.
- Ruled out the obvious suspects with direct evidence (see below).
- Did **not** fix it: settling what the draw should look like needs a
  Dolphin/hardware capture of the same screen, which this machine cannot make.

## Repro (headless, ~40 s)

```sh
SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy MELEE_GAMEOVER_TEST=1 \
    MELEE_NO_CARD=1 ./build/native/melee --match --frames 300 --no-hud \
    --shot /tmp/clear.bmp
magick /tmp/clear.bmp /tmp/clear.png
```

The band is GX rows `0..115` across the full width, constant from the first
clear-scene frame (256) through at least frame 850 — it is not animated and
not a fade.

## What the band actually is

`--dump-draws 256` identifies it exactly.  Frame 256 has 409 draws; the
screen-blur GObj (`fn_80013614` -> `lb_80012994`) is draws 252..273, all
full-screen `ndc x[-1,1] y[-1,1]`.  Then:

```
[draw 274] v=6 tex0=179 tex1=163 blend=0/4/5 z=1/3 gens=0 ndc x[1.09,3.53]  y[-1.12,-0.64]
[draw 275] v=6 tex0=179 tex1=163 blend=0/4/5 z=1/3 gens=0 ndc x[-0.96,1.48] y[0.52,1.12]
    tev0 order=255/255/255 cin=15,15,15,14 ain=7,7,7,1 aop=0/0/0/1 reg=0/0 kc=12 ka=0
    K0rgb=0.000,0.000,0.000
```

`ndc y = 0.52` is GX row `(1-0.52)/2*480 = 115.2` — the band's exact edge.

So draw 275 is a 6-vertex quad with **no texgen**, TEV order
`GX_TEXCOORD_NULL / GX_TEXMAP_NULL / GX_COLOR_NULL`, colour inputs
`ZERO,ZERO,ZERO,KONST` with `kc = GX_TEV_KCSEL_K0`, `K0 = (0,0,0)`, drawn with
`GX_BM_NONE`: **a solid opaque black rectangle**.  Draw 274 is the same quad
translated (+656, +394) px and lands entirely off-screen.

The konst trace shows the game writing, in order, immediately before each:

```
[kcolor] before-draw 274: K0 = 0,0,0,86
[kcolor] before-draw 275: K0 = 0,0,0,0
[kcolor] before-draw 276: K0 = 179,179,204,0
```

The alphas (86 = 34%, 0) read like translucent-panel colours, but the stage's
alpha comes from `GX_CA_A0` (`ain` d = 1), TEV register 0's alpha is 1.0, and
the blend mode is `GX_BM_NONE`, so alpha never reaches the framebuffer.
**That mismatch is the most promising thread**: either the alpha selector or
the render mode should be putting this quad in the XLU pass.

`HSD_SetupPEMode` takes its `pe == NULL` branch (the 4/5 blend factors are
hardcoded there), so the opaque/XLU decision came from the material's
`rendermode & RENDER_XLU (1 << 30)` being clear.

## What I ruled out, with evidence

- **Not the blur's tint scissor.** `lb_800138D8`/`fn_80013614` set
  `GXSetScissor(0, 110, 640, 290)` when `tint_factor != 0`, and 110 is
  temptingly close to 115.  Tracing every `GXSetScissor` call through the
  clear scene shows only `0 0 640 480`, `0 0 256 256` and `2 2 252 252`:
  **that call never happens**, `tint_factor` stays 0.
- **Not the EFB copy source.** Dumping the `glReadPixels` result at
  `GXCopyTex` time gives a clean, full-screen, unbanded match frame
  (1024x768), alpha 255 everywhere.
- **Not the EFB copy encode.** The 640x480 RGB5A3 destination is fully
  populated: `total=614400 nonzero=613982 first=0 last=614399`.
- **Not the blur geometry or UVs.** All 22 blur quads are `ndc [-1,1]^2` with
  UVs 0..1 over the whole 640x480 copy.
- **Not an unconverted archive root.** `MELEE_ROOT_TRACE=1` over the clear
  scene reports only `SIS_ClearData` unhandled, and the SIS text on that
  screen ("TIME REMAINING", "SPECIAL BONUS", the score) renders correctly, so
  SIS data is evidently fine as-is.  (This was the mechanism behind P-696, so
  it was the first thing I checked.)
- **Not `lupe`/P-696.** The band predates and survives that fix.

## Exact next action

Get a Dolphin capture of the 1P clear screen (any Classic/Adventure stage
end) at the same moment and answer one question: **is there a translucent dark
panel across the top there?**

- **If yes** — the quad is real and only its blending is wrong.  Start at
  `HSD_MObjSetup` -> `HSD_SetupRenderModeWithCustomPE` -> `HSD_SetupPEMode`
  (`baselib/state.c:208`) and print `rendermode`/`pe` for this MObj *gated on
  the clear scene* (`frame_dcount` alone is not enough — it repeats every
  frame, which invalidated my first gdb reading).  Check `RENDER_XLU` and the
  material's `HSD_PEDesc`, then walk back to the archive field the converter
  produced.
- **If no** — the quad should not be on screen at all, and the lead is its
  geometry: it is 780 px wide (2.44 NDC) on a 640 px screen and its twin sits
  fully off-screen, which smells like a position/scale read from the wrong
  place.

Useful one-liner for either path (frame 256 is the first clear frame):

```sh
SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy MELEE_GAMEOVER_TEST=1 \
    MELEE_NO_CARD=1 ./build/native/melee --match --frames 258 --no-hud \
    --dump-draws 256 --shot /tmp/x.bmp 2>&1 | sed -n '/^\[draw 275\]/,/^\[draw 276\]/p'
```

Temporarily relax `viewer_main.c`'s `if (d->state.blend_type != 0 ||
d->state.num_stages > 1)` to `if (1)` to make it print the TEV block for
opaque draws; that is how the state above was captured.

## What I tried that did not work

- Chasing the 110 vs 115 coincidence with the tint scissor — dead end, the
  scissor is never set (above).
- `gdb` breakpoint on `HSD_SetupPEMode` conditioned on `frame_dcount == 274`:
  it hits during an ordinary match frame long before the clear scene, because
  `frame_dcount` resets every frame.  It reported `flags=0x00001014`
  (`RENDER_DIFFUSE|RENDER_TEX0|RENDER_TOON`), `pe=NULL` — **for the wrong
  draw**.  Gate on the scene, not the draw index.
- Backtracing `GXSetBlendMode` at draw 275: no call — the blend state is
  inherited from draw 274, whose `GX_BM_NONE` comes from
  `HSD_MObjSetup` -> `HSD_SetupPEMode`.

## Open questions

- Does retail show a translucent dark band across the top of the 1P clear
  screen? — **needs a human (Dolphin capture).** Everything else is blocked
  on this.
- Are the two quads (274/275) a matched pair whose layout we are computing
  wrongly, or is one of them correctly off-screen? — answerable once the
  reference exists.

## Files touched / claimed

None for P-697 (analysis only).  All diagnostic instrumentation was reverted;
`git status` is clean.  The reusable part of it landed in P-696 as
`MELEE_ROOT_TRACE`.

## Verification run

```
$ ctest --test-dir build/native
100% tests passed out of 23
$ ninja            # in decomp/
All: 99.99% fuzzy, 100.00% matched, 100.00% linked (1130 / 1130 files)
```
