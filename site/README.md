# melee-unbound.site

The project's website. React + TypeScript + Tailwind, built with Vite and
**prerendered to static HTML**, so the shipped page is real markup that works
with JavaScript off. See [`ADR-0030`](../native/AI/DECISIONS.md).

```sh
cd site
npm install
npm run dev        # http://localhost:5173, hot reload
npm run build      # -> dist/   (typecheck, bundle, prerender)
npm run preview    # serve dist/ exactly as it will ship
npm run typecheck  # tsc --noEmit, on its own
```

`dist/` is the deployable artifact and is **not** committed. Point GitHub
Pages, Netlify or Cloudflare Pages at `site/` with build `npm run build` and
output `dist`.

## Where things are

```
site/
├── index.html            the shell Vite fills; loads Inter + Archivo
├── vite.config.ts        react + tailwind plugins
├── prerender.mjs         build step 3: render the App into dist/index.html
├── public/media/         brand art, served at /media/*
└── src/
    ├── main.tsx          client entry — hydrates, does not render
    ├── entry-server.tsx  build-time entry — never shipped
    ├── App.tsx           the violet edge, the hero block, the section order
    ├── content.tsx       ← every word on the page, typed
    ├── styles.css        ← the design tokens: @theme only, no component classes
    ├── lib/layout.ts     the two class strings (WRAP, BAND) every band shares
    ├── lib/links.ts      ← every off-site destination
    ├── sections/         Hero, Steps, TwoWays, Docs, Faq — one band each
    ├── components/       Header, Footer, Button, StageWindow, SectionHeading,
    │                     Wordmark, Toast, icons
    └── hooks/            useScrollSpy, useDestination
```

**Two files cover most edits.** Changing copy is `content.tsx`. Changing a
colour or a type size is the `@theme` block in `styles.css`. Adding or filling
a destination is `lib/links.ts`. The section components under `sections/` only
decide arrangement; they hold no copy, no URLs and no spacing of their own.

**Styling is Tailwind in the JSX.** There are no `.btn`, `.wrap` or `.band`
classes: buttons, grids and rhythm are utilities written where they are used,
and `styles.css` holds only the tokens, the base layer and the four
animations. That is the rule — round to the nearest Tailwind step rather than
inventing a component class.

## How the build works

```
npm run build
  1. tsc --noEmit                              types must pass first
  2. vite build                                -> dist/  client bundle + css
  3. vite build --ssr src/entry-server.tsx     -> .ssr/   the same App, for node
  4. node prerender.mjs                        -> rewrites dist/index.html
```

Step 4 renders the App to a string and drops it into `<div id="root">`, then
deletes `.ssr/`. The result: crawlers and JS-off visitors get the whole page,
the native `<details>` FAQ works before any JavaScript arrives, and React
hydrates on top for the scroll-spy. If you change `index.html`, keep the `<div id="root"></div>` exactly as-is — `prerender.mjs`
fails the build rather than silently shipping an empty page.

## The type scale is load-bearing

The comp's proportions live in the `@theme` block of `styles.css` —
`--text-display`, `--text-h2`, `--text-h3`, `--text-h4`, each carrying its
own line-height, tracking and weight, plus the two faces. That is why the
markup says `text-h2` and not a pile of `text-[...] leading-[...]
tracking-[...]`: retuning the design is one edit here, not twelve in the
components.

Those sizes were set by measuring **cap heights** against the design comp. If
you retune them, measure — eyeballing sent them the wrong way twice.

## Two faces, on purpose

Inter 900 uppercase with tight tracking is the page's typeface for headings,
body and UI (ADR-0028). The hero name is the exception: the comp sets it in a
heavy slanted grotesque, so `--font-display` is Archivo 900 italic, condensed
to `wdth 87.5` — the closest face on Google Fonts that stays serious
(ADR-0033). It is used on the hero `h1` and nowhere else. Adding a third face
needs a decision entry first.

## The vertical rhythm

`BAND` in `lib/layout.ts` is the padding every section gets (`py-12
lg:py-16`), and `WRAP` is the shared column (`max-w-[100rem]`, gutters
`px-5 md:px-10 lg:px-16`). Sections compose those with their own background
and `scroll-mt-4`. Inside a band, the spacing is plain Tailwind steps —
`mt-8 lg:mt-10` under a heading, `gap-5` between cards, `p-6 lg:p-8` inside
one — so if a band looks wrong, check it against its neighbours before adding
a number. `--radius-card` is `0`: the comp's posture is square corners, and
window chrome is the one place (`rounded-2xl`) the comp rounds.

One smaller rule holds the two-up grid together: the cards in "Browser or
desktop?" push their button down with a `grow` spacer, so both buttons land on
the same line however much copy sits above them.

## The hero is one viewport

`App` puts the header and the hero in one `min-h-svh` block; everything else
scrolls in under it. Inside the hero, at `xl` the window and the copy are
stacked in a single grid cell — the window hangs from the top right, the name
stands on the floor — because the comp overlaps the name with the window's
lower-left corner. Below `xl` they stack in reading order instead. The violet
edge in `App` is the comp's slim left rail: fixed, full height, decoration
only.

## Animation

Motion is CSS-only, it runs once when the page paints, and **nothing watches
the scroll position**. An earlier version scroll-revealed every band with GSAP
+ ScrollTrigger; it read as sloppy on the real page, and both the effect and
the dependency were removed (~70 kB gzipped off the bundle). If you are
tempted to add it back, don't — the page reads better without it.

What exists:

- **The hero, once, on load.** The copy and the window rise or fade in
  through the `animate-enter` / `animate-enter-media` tokens in `styles.css`,
  staggered with an inline `animationDelay` written next to each element in
  `Hero.tsx`.
- **The window.** The stage loop plays (see below) with the play ring pulsing
  (`animate-ring`) and the status caret blinking (`animate-caret`).
- **Hover feedback**, CSS transitions on the buttons, docs rows and FAQ rows.
- **The FAQ answer**, the `animate-faq-answer` token, and the chevron turning
  over on open. The accordion itself is a native `<details name="faq">`, so it
  works on the prerendered page before any JavaScript arrives, and keeps
  working if the bundle never does.

Every animation carries `motion-reduce:animate-none`, and `index.html` sets
`scroll-smooth motion-reduce:scroll-auto` — so reduced motion leaves each
element at its finished position, with no reveal to get stuck behind.

`useScrollSpy` is the only JavaScript that watches the scroll position, and
it is an `IntersectionObserver` with a midline `rootMargin` — one pixel band
at 55% down the viewport, which a section intersects exactly while it is the
one under the reader. It stays on under reduced motion, because it is
navigation state rather than decoration.

## Where the page differs from the comp, on purpose

1. **The two doors.** The comp's hero offers "Play in browser" (violet) and
   "Download desktop". Neither exists: there is no browser build — the
   WebAssembly in the project runs *mods*, not the game — and there are no
   binary releases. The buttons here are "Build for desktop" and "Read the
   docs", and the browser-or-desktop section marks the browser card
   **Planned**. Do not let the page promise a build that has not shipped.
2. **The footer lockup.** The comp's page was drawn for a light footer; ours
   closes on black to mirror the header, so it uses the dark lockup. The
   light variant (`public/media/wordmark-light.svg`) stays for light surfaces
   elsewhere.
3. **The hero window.** The comp fills it with concept art; here it is a real
   video element. It plays `/media/gameplay.mp4` when the owner drops a
   capture in, and otherwise falls back to `/media/stage-loop.mp4`, a slow
   camera move generated from `stage.svg` — so the window is always moving
   and the page never ships Nintendo footage. The two-ways windows keep the
   drawn frame, so only one video ever decodes.
4. **The lockups are pre-cropped.** `public/media/wordmark*.svg` are the
   brand files with the clear space trimmed (`viewBox="200 103 880 193"`), so
   they size with a plain `w-*` class instead of the old negative-margin crop.
   The masters in `/assets` keep their clear space; do not recolour either.

## The stage artwork

`public/media/stage.svg` is hand-generated line art, not game art: a
one-point perspective floor, a Battlefield-like platform on a pedestal, side
and top platforms, and the mark's orbit swept around the stage in violet.
Stroke widths and the palette are the page's (`#a08aff` on near-black faces;
no gradients). It is cropped (`viewBox="140 120 1320 780"`) so the stage fills
the window, used as the video poster in the hero and as the frame in both
two-ways windows — the page's most-seen image, so change it deliberately. The
windows crop it again with `object-cover`; keep the stage centred and the
outer 5% clear of anything you need read.

The stage and the orbit carry a slow float in inline CSS, guarded by
`prefers-reduced-motion`, so the poster moves like a held camera. That is the
only motion inside the drawing; do not add more.

`public/media/stage-loop.mp4` is generated from that SVG, not filmed: a 16s,
1600px x264 loop on the window's `#161617`, a slow sinusoidal push-in and
drift, seamless at the loop point. 16 s at 30 fps is 480 frames, which is the
period of every `sin` in the filter. Regenerate it after any change to the
drawing:

```sh
rsvg-convert --background-color='#161617' public/media/stage.svg -w 3200 -o /tmp/stage.png
ffmpeg -y -loop 1 -framerate 30 -i /tmp/stage.png \
  -vf "zoompan=z='1.04+0.04*sin(2*PI*on/480)':x='(iw-iw/zoom)/2+20*sin(2*PI*on/480)':y='(ih-ih/zoom)/2-12*sin(2*PI*on/240)':d=1:s=1600x946:fps=30,format=yuv420p" \
  -frames:v 480 -c:v libx264 -crf 26 -preset slow -movflags +faststart \
  public/media/stage-loop.mp4
```

Keep the height even (946, not 945) — x264 rejects odd heights with yuv420p.

## Before it goes live

1. **Discord.** `LINKS.discord` is empty, so nothing on the page points at
   one. If the community gets a server, add the button and fill the entry in;
   an unset link tells the visitor rather than doing nothing.
2. **Open Graph image.** The cards have no `og:image` yet. The 1280×400
   banner in `/assets` is the intended one; it needs a hosted URL first.
3. **Gameplay capture.** Drop `gameplay.mp4` (H.264/AAC or silent, 16:9) in
   `public/media/` and the hero switches to it with no code change. Until
   then the stage poster stands in.

## Copy

Every claim on the page is checkable against the repo. There are no binary
releases, so the page never offers a download; the real path is building from
source. `content.tsx` says this at the top, where the copy actually lives.

## Palette and type

Branding rules — palette, contrast, logo use — are in
[`native/AI/reference/branding.md`](../native/AI/reference/branding.md),
mirrored into the `@theme` block at the top of `styles.css`. The violet is
only ever used with black text on it; white on violet fails at 2.77:1. On the
light bands violet text and icons use the darkened `#6a54d8`, the only violet
that clears contrast there.

Inter, loaded from Google Fonts (400–900), plus Archivo 900 italic condensed
for the hero name — recorded in `branding.md` and ADR-0028/0033.
