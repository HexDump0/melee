# Branding — Melee Unbound

The port ships under the name **Melee Unbound** (also the id of the default
mod, `mods/unbound/`). This file is the source of truth for the name, the
palette and the logo files. Anything that renders a logo, picks a colour or
writes the product name — the website, the launcher, the in-game UI, the
README, release artwork — follows this page.

Artwork lives in `assets/` at the repo root, not in this folder.

## Palette

Three colours. There is no secondary accent and no gradient.

| Token | Hex | Use |
|---|---|---|
| Black | `#000000` | Background. Always flat — no tint, no `#0A0A0F`-style near-blacks. |
| White | `#FFFFFF` | Body text, headings, the disc of the mark. |
| Purple | `#A08AFF` | Accent only: the orbit ring, the word `UNBOUND`, primary buttons, links, active states. |

Contrast, measured (WCAG 2.x):

| Pair | Ratio | Verdict |
|---|---|---|
| White on black | 21.00:1 | fine at any size |
| Purple on black | 7.57:1 | fine at any size |
| Black on purple | 7.57:1 | fine at any size |
| **White on purple** | **2.77:1** | **fails — never do this** |

So a filled purple button takes **black** label text, not white. That is the
one contrast trap in this palette.

Flat black is a deliberate choice, not an oversight: tinted backgrounds
(`#0A1426`, `#0B1C1B` and similar) were tried and rejected.

## Logo files

Two lockups — the wide banner and the 1:1 icon — each in four files: SVG and
PNG, on black and transparent. Eight files, one mark.

| File | Size | Background |
|---|---|---|
| `assets/melee-unbound-banner.svg` | 1280×400 | black |
| `assets/melee-unbound-banner.png` | 1280×400 | black |
| `assets/melee-unbound-banner-transparent.svg` | 1280×400 | none |
| `assets/melee-unbound-banner-transparent.png` | 1280×400 | none (RGBA) |
| `assets/melee-unbound-icon.svg` | 512×512 | black |
| `assets/melee-unbound-icon.png` | 512×512 | black |
| `assets/melee-unbound-icon-transparent.svg` | 512×512 | none |
| `assets/melee-unbound-icon-transparent.png` | 512×512 | none (RGBA) |
| `assets/melee-unbound-banner-light.svg` | 1280×400 | none (for light surfaces) |
| `assets/melee-unbound-icon-light.svg` | 512×512 | none (for light surfaces) |

Banner for READMEs, release headers, the site header and social previews; icon
for app icons, favicons, avatars and anywhere square.

The SVGs are pure paths — no embedded raster, no `<text>` — so they scale
cleanly and need no fonts installed.

### Which background

Default to the **black** files. Reach for a transparent one only when the
surface behind it is already dark and you want the logo to sit on it directly.

The transparent variants have a real catch: the wordmark and the disc are
white, so on a light surface the logo all but disappears and only the purple
ring survives. Transparent does not mean "works anywhere" — it means "bring
your own dark background".

**The light-surface variant now exists** (2026-09-18, for the site's footer
band, which the owner's comp shows light). It is not a redraw: it is the
transparent SVG with two colour substitutions, so it cannot drift from the
mark.

```sh
sed -e 's/#FFFFFF/#000000/g' -e 's/#A08AFF/#6A54D8/g' \
  assets/melee-unbound-banner-transparent.svg > assets/melee-unbound-banner-light.svg
```

White becomes black; the violet becomes the darkened `#6A54D8`, because that
is the only violet that clears contrast on a light band — the comp draws the
ring in `#A08AFF` and this deliberately does not. Re-run the substitution after
any change to the mark. **Do not** reach for a CSS filter, `mask` or
`invert()` to fake this; use the file.

## Boot animation

`assets/melee-unbound-boot.mp4` — 1920×1080, 60fps, 4.0s, H.264 / yuv420p,
silent, ~98 KiB (it is mostly flat black, so it compresses to nothing).

The disc in the artwork is cut where the ring crosses in front of it, so on its
own it looks sliced. For the animation the generator redraws it from its own
geometry — circle minus the two dividing bars, measured off the artwork
(`disc_states`) — so it enters as the clean four-quadrant mark. The ring's sweep
then blends that back to the traced version, cutting the slash in exactly as the
violet arrives over it, and the final held frame matches the logo. The SVGs are
untouched; do not "fix" the disc in them.

The sequence, in four beats: the mark fades and scales up **centred in frame**;
the orbit ring sweeps around it as if orbiting into place; the mark slides left
and `MELEE` rises into the space it vacates, with `UNBOUND` a beat behind; hold,
then fade to black. No glow, no flare, no sound — same restraint as the logo.

### The cue (off by default)

The animation ships **silent**. Pass `--audio` to mux the fanfare in.

A short regal fanfare, scored to the same four beats rather than laid under
them: a low drone as the mark appears, a timpani roll accelerating with the
ring, two brass pickups (A then D) leaning into the ring closing, then the
downbeat on the wordmark — full D major brass with timpani and a crash — an
answering A above on `UNBOUND`, and the chord ringing out through the hold to
silence exactly as the picture fades.

The brass is additive with a spectrum that opens on the attack, which is what
makes it read as a horn instead of a sawtooth. Everything is synthesised in the
generator from a seeded RNG, so it renders identically every run.

Levels are deliberate and build: drone 0.10, roll 0.10, pickups 0.19, downbeat
0.41, ring-out 0.09, silence. The downbeat is four times the bed — that
contrast is the whole effect. Peak normalised to -1.5 dBFS, no clipping. If you
retune it, keep that ordering.

It is generated, not hand-animated:

```sh
python3 scripts/make_boot_animation.py                    # -> assets/melee-unbound-boot.mp4 (silent)
python3 scripts/make_boot_animation.py --audio            # ...with the fanfare
python3 scripts/make_boot_animation.py --frames out/      # numbered PNG sequence
```

The script slices `assets/melee-unbound-banner.svg` into its four coloured
groups (disc, ring, `MELEE`, `UNBOUND`) and animates those, so the animation
cannot drift from the logo. **Re-run it after any change to the mark.** Timing
lives in the `T_*` constants at the top; resolution and duration just above
them. Needs `rsvg-convert`, `ffmpeg` (libx264), Pillow and NumPy.

### It does not play in the port yet

The port links no video decoder — no libav, no THP/MTH reader — so nothing in
`native/` can play this MP4 today. As it stands it is a marketing asset: site
hero, README, release posts, Discord.

Three ways it could become a real in-engine boot, cheapest first:

1. **Draw it procedurally.** The whole sequence is four textured quads with
   alpha, an angular mask and some easing. The GL path can already do that, and
   it costs one PNG atlas instead of a decoder.
2. **Bake a frame sequence.** `--frames` emits PNGs; stack them into an atlas
   or a strip and step it.
3. **Replace `MvOpen.mth`.** The game's own opening movie plays from
   `GM_OPENING_MV` (see the `MELEE_OPENING` note in `native/platform/os.c`).
   Faithful, but it means writing an MTH encoder — by far the most work.

Pick one deliberately; do not add a video decoder to the port just to show a
logo.

### Usage rules

- **Clear space:** keep at least the height of the `M` in `MELEE` free on all
  sides of the lockup. The SVGs already carry it inside the file: the ink box
  is `200,103..1080,296` of the `1280x400` viewBox, i.e. 15.625% of the width
  each side and 25.75% of the height top and bottom. A layout that needs the
  mark flush to a gutter may crop that padding away — `site` does, with the
  `.wordmark` class — but it then owes the mark real clear space in margins.
- **Minimum size:** banner no narrower than 240px; icon no smaller than 32px.
  Below that the orbit ring breaks up.
- **Do not** recolour, add glow/bevel/drop-shadow, outline it, rotate it,
  stretch it non-uniformly, or rebuild the lockup by setting the wordmark in
  some other typeface.
- **Do not** place the icon inside another shape (rounded-rect masks, circles).
  It is already a finished square tile.

## Typography

**Inter**, loaded from Google Fonts, weights 400–900. Headings are Inter 900,
uppercase, with tight negative tracking (`-0.03em`); body is Inter 400. The
wordmark is outlined vector paths, so it is unaffected by this choice.
The site tried a second display face for the hero name and reverted it
(ADR-0033, amended); Inter is the only face the product ships.


This was settled by the website, the first real UI surface, on 2026-09-17
(ADR-0028). The in-game UI and the launcher have not picked yet; use Inter
there too rather than inventing a second face, and record any change here and
in `DECISIONS.md`.

## Naming

- Product: **Melee Unbound** — two words, both capitalised.
- Not: "MeleeUnbound", "Melee: Unbound", "melee-unbound" in prose.
- `melee-unbound` (kebab) is fine for filenames, slugs and package ids.
- `unbound` alone refers to the default mod (`mods/unbound/`), not the product.

## Known risk: the mark resembles the Smash Bros. symbol

Say it plainly, because it affects whether this artwork survives contact with
the outside world: the disc in the mark is the Super Smash Bros. divided-circle
symbol with an orbit ring added. It is close enough that Nintendo could
reasonably treat it as their trademark.

The owner has seen this and chosen the mark anyway, so **use it as-is** and do
not quietly redraw it. But:

- Do not make it more Nintendo-like — no official Smash colours, no fighter
  art, no Nintendo wordmarks anywhere near it.
- Expect that a takedown or a rebrand is possible. Reference the logo through
  `assets/` in one place per surface, so swapping it later is a file
  replacement rather than a hunt.
- If a redraw is ever commissioned, the direction on file is: keep the orbit
  ring and the purple-on-black palette, drop the divided disc.

## History

The palette and the mark came out of a design exploration run outside the repo
(scratch files, not preserved). The decisions that survived are the ones on
this page; the exploration artefacts are gone and do not need recovering.
