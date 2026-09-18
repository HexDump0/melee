/* Everything the page says, in one place.

   Copy changes live here; the section components under `sections/` only
   decide how it is arranged. The types are the point: rename a field and
   every call site fails at `npm run typecheck` instead of rendering blank.

   One rule this file exists to protect: every claim on the page has to be
   checkable against the repo. The browser build is real — `/play` runs the
   port from the user's own disc — and the hero's Download door scrolls to
   "How to play" rather than pointing at a file that does not exist. */

import type { ComponentType, ReactNode, SVGProps } from 'react';
import type { LinkKey } from './lib/links.ts';
import {
  CheckCircleIcon,
  CubeIcon,
  DiscordIcon,
  DownloadIcon,
  GlobeIcon,
  MonitorIcon,
  PlayIcon,
} from './components/icons.tsx';

type Icon = ComponentType<SVGProps<SVGSVGElement>>;

/* --- navigation --------------------------------------------------------- */

/** Section ids, in page order. The header, the footer and the scroll-spy all
    read this, so adding a section to the page means adding it here once. */
export const SECTION_IDS = ['play', 'features', 'download', 'faq'] as const;
export type SectionId = (typeof SECTION_IDS)[number];

export type NavItem = { id: SectionId; label: string };

/** The header's in-page links. "Source" is off-site and added by the header
    itself, because it is not a section. */
export const NAV: NavItem[] = [
  { id: 'features', label: 'Features' },
  { id: 'download', label: 'Download' },
];

/* --- hero --------------------------------------------------------------- */

/** The project's tagline, one entry per line. The hero sets it under the
    buttons; the footer joins it back into one line. */
export const TAGLINE = ['game data is not distributed with this port.'] as const;

export const HERO = {
  /** The page's h1 — the headline, not display type; the window is the art. */
  statement: 'A true port of Super Smash Bros. Melee.',
  /** Sits under the headline, before the buttons. */
  lede: 'Built from the game’s own decompiled source and runs natively. Free, open, and yours to build.',
  /** Closes the hero's floor rule, where the section links used to sit. */
  status: 'Beta',
  /** The first door is the one that works today, so it is the filled one.
      Download scrolls to the "How to play" section; Discord is off-site. */
  doors: [
    { href: '#download', icon: PlayIcon, label: 'Play', tone: 'violet' },
    { to: 'discord', icon: DiscordIcon, label: 'Discord', tone: 'outline' },
  ],
} as const;

/* --- three steps -------------------------------------------------------- */

export type Feature = { icon: Icon; title: string; body: ReactNode };

export const FEATURES: Feature[] = [
  {
    icon: CheckCircleIcon,
    title: '100% byte matched',
    body: <>Compiled from the decompilation</>,
  },
  {
    icon: MonitorIcon,
    title: 'OpenGL renderer',
    body: <>Native GL/ES rendering</>,
  },
  {
    icon: GlobeIcon,
    title: 'Web support',
    body: <>Runs in the browser</>,
  },
  {
    icon: CubeIcon,
    title: 'Mods',
    body: <>Native mod support</>,
  },
];

/* --- browser or desktop ------------------------------------------------- */

export type WayCard = {
  /** `violet` is the path that exists today; `light` is the one that does not. */
  tone: 'violet' | 'light';
  /** Which window the cards draw above their copy. */
  frame: 'app' | 'browser';
  title: string;
  /** Rendered next to the title. Used to mark what has not shipped. */
  badge?: string;
  body: ReactNode;
  cta: { icon: Icon; label: string; tone: 'violet' | 'white' | 'outline' } & ({ to: LinkKey } | { href: string });
};

/** The browser leads, with the violet button, because it is the one the
    project is working toward; desktop follows with the quieter outline. */
export const WAYS: WayCard[] = [
  {
    tone: 'light',
    frame: 'browser',
    title: 'browser',
    body: <>Melee Unbound build for WASM, expect a bit of graphical issues and slightly less performance</>,
    cta: { href: '/play', icon: GlobeIcon, label: 'Open', tone: 'violet' },
  },
  {
    tone: 'violet',
    frame: 'app',
    title: 'desktop',
    badge: 'has more features',
    body: <>Runs natively, has more settings and is the recommended way to play.</>,
    cta: { to: 'repo', icon: DownloadIcon, label: 'Download', tone: 'outline' },
  },
];

/* --- faq ---------------------------------------------------------------- */

export type Faq = { q: string; a: ReactNode };

export const FAQS: Faq[] = [
  {
    q: 'Is Melee Unbound free?',
    a: (
      <>
        Absolutely! It is completely free and open source.
      </>
    ),
  },
  {
    q: 'Do I need to download anything to play in my browser?',
    a: (
      <>
        No, if you browser supports WA(which most browsers do), you just have to provide your own Melee disc image and you can play it in your browser.
      </>
    ),
  },
  {
    q: 'Can I contribute to the project?',
    a: (
      <>
        Yes, we are always looking for contributors! Please join our discord and introduce yourself.
      </>
    ),
  },
  {
    q: 'Does it need the original game?',
    a: (
      <>
        Yes. You need a disc image of Melee v1.02 (<code>GALE01</code>).We cannot help you in obtaining one
      </>
    ),
  },
  {
    q: "Where can I get help if something is broken?",
    a: (
      <>
        Make a bug report on discord
      </>
    ),
  },
];

/* --- section headings --------------------------------------------------- */

export const HEADINGS = {
  download: {
    eyebrow: 'Download',
    title: ['How to play'],
  },
  faq: {
    eyebrow: 'Questions',
    title: ['FAQ'],
  },
} as const;

/* --- footer ------------------------------------------------------------- */

export const FOOTER = {
  legal:
    'A fan project. Not affiliated with or endorsed by Nintendo. No game assets are distributed here.',
} as const;
