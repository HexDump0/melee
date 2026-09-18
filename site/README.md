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
├── index.html            the shell Vite fills
├── vite.config.ts        react + tailwind plugins
├── prerender.mjs         build step 3: render the App into dist/index.html
├── public/media/         brand art, served at /media/*
└── src/
    ├── main.tsx          client entry — hydrates, does not render
    ├── entry-server.tsx  build-time entry — never shipped
    ├── App.tsx           the [rail | content] grid and the section order
    ├── content.tsx       ← every word on the page, typed
    ├── styles.css        ← the design system: @theme tokens, base, components
    ├── lib/links.ts      ← every off-site destination
    ├── sections/         Hero, Steps, TwoWays, Docs, Faq — one band each
    ├── components/       Rail, Footer, SectionHeading, Wordmark, Toast, icons
    └── hooks/            useScrollSpy, useDestination
```

**Three files cover most edits.** Changing copy is `content.tsx`. Changing a
colour, a type size or the band rhythm is `styles.css`. Adding or filling a
destination is `lib/links.ts`. The section components under `sections/` only
decide arrangement; they hold no copy, no URLs and no spacing of their own.

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
`--text-display`, `--text-h2`, `--text-h3`, `--text-numeral`, each carrying
its own line-height, tracking and weight, plus `--spacing-rail` and
`--spacing-gutter`. That is why the markup says `text-h2` and not a pile of
`text-[...] leading-[...] tracking-[...]`: retuning the design is one edit
here, not twelve in the components.

Those sizes were set by measuring **cap heights** against the design comp. If
you retune them, measure — eyeballing sent them the wrong way twice.

## The space scale is load-bearing too

Four tokens in the same `@theme` block set the entire vertical rhythm, and no
component picks a number of its own:

| token              | what it is                        |
| ------------------ | --------------------------------- |
| `--spacing-band`   | the padding every band gets       |
| `--spacing-block`  | heading → content, inside a band  |
| `--spacing-grid`   | the gap between sibling cards     |
| `--spacing-card`   | a card's inner padding            |

In the markup that is `class="band"` on the `<section>`, then `mt-block`,
`gap-grid` and `p-card`. If one band looks wrong next to another, the fix is
one of these four values, not a `py-11` somewhere. `--radius-card` is `0`:
the comp's posture is square corners, and it is a token so changing our mind
is one edit rather than thirty.

Two smaller rules hold the grids together. The step numerals are
`tabular-nums`, so `1` takes the same advance as `2` and the three text
columns start at the same x. The two cards in "Browser or desktop?" push
their CTA down with a `mt-block grow` spacer, so both buttons land on the
same line however much copy sits above them.

## Animation

There is deliberately almost none, and no animation library. An earlier
version scroll-revealed every band with GSAP + ScrollTrigger; it was removed
in favour of a page that is simply *there*, and the dependency went with it
(~70 kB gzipped off the bundle). If you are tempted to add it back, the page
reads better without it.

What remains:

- **Hover lifts and arrow nudges**, CSS transitions on the cards.
- **The FAQ answer**, one `@keyframes` in `styles.css` on `.faq-answer`. The
  accordion itself is a native `<details name="faq">`, so it works on the
  prerendered page before any JavaScript arrives, and keeps working if the
  bundle never does.

Both are off under `prefers-reduced-motion: reduce`.

`useScrollSpy` is the only JavaScript that watches the scroll position, and
it is an `IntersectionObserver` with a midline `rootMargin` — one pixel band
at 55% down the viewport, which a section intersects exactly while it is the
one under the reader. It stays on under reduced motion, because it is
navigation state rather than decoration.

## Where the page differs from the comp, on purpose

1. **The two doors.** The comp's hero offers "Play in browser" (violet) and
   "Download desktop". Neither exists: there is no browser build — the
   WebAssembly in the project runs *mods*, not the game — and there are no
   binary releases. The page keeps the comp's exact card geometry but the
   violet one is "Build for desktop" and the white one is "Read the docs",
   and the browser-or-desktop section marks the browser card **Planned**. Do
   not let the page promise a build that has not shipped.
2. **The rail's active link.** The comp paints it white on violet, which is
   2.77:1 and fails. It is black and 900-weight here instead; the inactive
   links sit at 60% opacity. Same read, passing contrast.
3. **The footer lockup.** The comp shows a dark lockup on the light band.
   That variant did not exist when the comp was drawn; it does now
   (`public/media/wordmark-light.svg`), and its ring is the darkened
   `#6A54D8` rather than the comp's `#A08AFF`, for the same contrast reason.

## Before it goes live

1. **Discord.** `LINKS.discord` is empty, so nothing on the page points at
   one. If the community gets a server, add the button and fill the entry in;
   an unset link tells the visitor rather than doing nothing.
2. **Open Graph image.** The cards have no `og:image` yet. The 1280×400
   banner in `/assets` is the intended one; it needs a hosted URL first.

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

Inter, loaded from Google Fonts (400–900). Headings are Inter 900, uppercase,
with tight negative tracking — recorded in `branding.md` and ADR-0028.
