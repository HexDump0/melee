/* Everything the page says, in one place.

   Copy changes live here; the section components under `sections/` only
   decide how it is arranged. The types are the point: rename a field and
   every call site fails at `npm run typecheck` instead of rendering blank.

   One rule this file exists to protect: every claim on the page has to be
   checkable against the repo. There is no browser build — the WebAssembly in
   the project runs *mods*, not the game. The hero's Download button points
   at `LINKS.releases`, the repository's releases page, which is where a
   binary will appear; never point it at a file that does not exist yet. */

import type { ComponentType, ReactNode, SVGProps } from 'react';
import type { LinkKey } from './lib/links.ts';
import {
  CheckCircleIcon,
  CubeIcon,
  DiscordIcon,
  DownloadIcon,
  GlobeIcon,
  MonitorIcon,
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
  lede: 'Built from the game’s own decompiled source and run natively, not emulated. Free, open, and yours to build.',
  /** Closes the hero's floor rule, where the section links used to sit. */
  status: 'Beta',
  /** The first door is the one that works today, so it is the filled one.
      Download scrolls to the "How to play" section; Discord is off-site. */
  doors: [
    { href: '#download', icon: DownloadIcon, label: 'Play', tone: 'violet' },
    { to: 'discord', icon: DiscordIcon, label: 'Discord', tone: 'outline' },
  ],
} as const;

/* --- three steps -------------------------------------------------------- */

export type Feature = { icon: Icon; title: string; body: ReactNode };

export const FEATURES: Feature[] = [
  {
    icon: CheckCircleIcon,
    title: '100% byte matched',
    body: <>The decompilation builds matched to retail, function for function.</>,
  },
  {
    icon: MonitorIcon,
    title: 'OpenGL renderer',
    body: <>Native GL/ES rendering — the game is ported, not emulated.</>,
  },
  {
    icon: GlobeIcon,
    title: 'Web support',
    body: <>The WebAssembly mod host runs in the browser today; the game build is next.</>,
  },
  {
    icon: CubeIcon,
    title: 'Mods',
    body: <>WebAssembly mods, loaded straight from your disc image.</>,
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
  cta: { to: LinkKey; icon: Icon; label: string; tone: 'violet' | 'white' | 'outline' };
};

/** The browser leads, with the violet button, because it is the one the
    project is working toward; desktop follows with the quieter outline. */
export const WAYS: WayCard[] = [
  {
    tone: 'light',
    frame: 'browser',
    title: 'browser',
    body: <>Melee Unbound build for WASM, except a bit of graphical issues and slightly less performance</>,
    cta: { to: 'repo', icon: GlobeIcon, label: 'Play', tone: 'violet' },
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
        There is nothing to buy and no store page. It is a fan project built in
        the open; you supply your own copy of the game.
      </>
    ),
  },
  {
    q: 'Do I need to download anything to play in my browser?',
    a: (
      <>
        There is no browser build yet — it is a goal, not a release. Today you
        build the desktop port from source.
      </>
    ),
  },
  {
    q: 'What do I need to build it?',
    a: (
      <>
        A Linux machine with a 32-bit toolchain, SDL3 and Mesa (EGL/GLESv2),
        plus CMake and Ninja. The reference setup is Arch Linux; Windows and
        macOS are not supported yet.
      </>
    ),
  },
  {
    q: 'Does it need the original game?',
    a: (
      <>
        Yes. You need a disc image of Melee v1.02 (<code>GALE01</code>). No
        Nintendo data is distributed here.
      </>
    ),
  },
  {
    q: "Where can I get help if something isn't working?",
    a: (
      <>
        Open an issue on GitHub with your platform, the command you ran and the
        output. Crashes are worth a stack trace.
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
    eyebrow: 'Quick answers',
    title: ['FAQ'],
  },
} as const;

/* --- footer ------------------------------------------------------------- */

export const FOOTER = {
  legal:
    'A fan project. Not affiliated with or endorsed by Nintendo. No game assets are distributed here.',
} as const;
