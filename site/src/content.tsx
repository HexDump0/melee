/* Everything the page says, in one place.

   Copy changes live here; the section components under `sections/` only
   decide how it is arranged. The types are the point: rename a field and
   every call site fails at `npm run typecheck` instead of rendering blank.

   One rule this file exists to protect: every claim on the page has to be
   checkable against the repo. There is no browser build — the WebAssembly in
   the project runs *mods*, not the game — and there are no binary releases,
   so nothing here offers a download. Do not let the page promise a build
   that has not shipped. */

import type { ComponentType, ReactNode, SVGProps } from 'react';
import type { LinkKey } from './lib/links.ts';
import { BookIcon, DocIcon, DownloadIcon, GlobeIcon } from './components/icons.tsx';

type Icon = ComponentType<SVGProps<SVGSVGElement>>;

/* --- navigation --------------------------------------------------------- */

/** Section ids, in page order. The rail, the footer and the scroll-spy all
    read this, so adding a section to the nav means adding it here once. */
export const SECTION_IDS = ['play', 'setup', 'download', 'docs', 'faq'] as const;
export type SectionId = (typeof SECTION_IDS)[number];

export type NavItem = { id: SectionId; label: string };

export const NAV: NavItem[] = [
  { id: 'play', label: 'Play' },
  { id: 'download', label: 'Download' },
  { id: 'setup', label: 'Setup' },
  { id: 'docs', label: 'Docs' },
];

/* --- hero --------------------------------------------------------------- */

export type HeroCard = {
  to: LinkKey;
  icon: Icon;
  /** Two lines, stacked. The comp sets both card titles on two lines. */
  title: [string, string];
  sub: string;
};

export const HERO = {
  /** Top-right, above the rule. One word per line. */
  kicker: ['Play', 'anywhere', 'together'],
  /** The display headline, one entry per clipped line. */
  headline: ['Pick up', 'and play'],
  sub: 'Same game. More places.',
  /** The violet card is the door that works today. */
  cards: [
    {
      to: 'repo',
      icon: DownloadIcon,
      title: ['Build for', 'desktop'],
      sub: 'Build from source. Runs natively.',
    },
    {
      to: 'docs',
      icon: DocIcon,
      title: ['Read the', 'docs'],
      sub: 'Setup, architecture, tests.',
    },
  ] satisfies [HeroCard, HeroCard],
} as const;

/* --- three steps -------------------------------------------------------- */

export type Step = { title: [string, string?]; body: ReactNode };

export const STEPS: Step[] = [
  {
    title: ['Bring your', 'disc'],
    body: (
      <>
        You supply your own copy of Melee v1.02 (<code>GALE01</code>). Nothing
        here ships Nintendo's assets, and nothing here will.
      </>
    ),
  },
  {
    title: ['Build it'],
    body: (
      <>
        Clone with submodules, configure with CMake, build. The configure step
        applies the portability patches for you.
      </>
    ),
  },
  {
    title: ['Run it'],
    body: (
      <>
        Point the binary at your image and play. Mods live in <code>mods/</code>;{' '}
        <code>MELEE_NO_MODS=1</code> returns it to vanilla.
      </>
    ),
  },
];

/* --- browser or desktop ------------------------------------------------- */

export type WayCard = {
  /** `violet` is the path that exists today; `light` is the one that does not. */
  tone: 'violet' | 'light';
  icon: Icon;
  title: [string, string];
  /** Rendered next to the title. Used to mark what has not shipped. */
  badge?: string;
  body: ReactNode;
  /** Ticks on the violet card, dots on the light one — see the components. */
  points: string[];
  cta: { to: LinkKey; label: string };
};

export const WAYS: WayCard[] = [
  {
    tone: 'violet',
    icon: DownloadIcon,
    title: ['Build for', 'desktop'],
    body: (
      <>
        Compiled from the game's own decompiled source and run natively — not
        emulated. The same build for everyone, from the repository.
      </>
    ),
    points: [
      'Highest performance',
      'Widescreen, mods and your own disc image',
      'Free, and built in the open',
    ],
    cta: { to: 'repo', label: 'Open the repository' },
  },
  {
    tone: 'light',
    icon: GlobeIcon,
    title: ['Play in', 'browser'],
    badge: 'Planned',
    body: (
      <>
        A web build is a goal, not a release. The WebAssembly in the project
        today runs <em>mods</em>, not the game. When that changes, this card
        will say so.
      </>
    ),
    points: [
      'The mod host runs in Chrome and Firefox',
      'WebAssembly loads mods from the disc',
      'The game build is the remaining work',
    ],
    cta: { to: 'repo', label: 'Follow the work' },
  },
];

/* --- documentation ------------------------------------------------------ */

export type DocCard = { to: LinkKey; icon: Icon; title: string; sub: string };

export const DOC_CARDS: DocCard[] = [
  {
    to: 'setup',
    icon: BookIcon,
    title: 'Setup guide',
    sub: 'Step-by-step build instructions, from clone to launch.',
  },
  {
    to: 'docs',
    icon: DocIcon,
    title: 'Documentation',
    sub: 'Architecture, testing, and how the port is put together.',
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
  setup: {
    eyebrow: 'Get started',
    title: ['Three steps', 'to play'],
    note: ['Simple setup.', 'Real games.'],
  },
  download: {
    eyebrow: 'Two ways to play',
    title: ['Browser or desktop?'],
    sub: 'Same game. Your choice.',
  },
  docs: {
    eyebrow: 'Setup & documentation',
    title: ['Everything you need'],
    note: ['Guides.', 'Answers.', "You're covered."],
  },
  faq: {
    eyebrow: 'Quick answers',
    title: ['FAQ'],
    note: ['Still have questions?', 'Check the docs.'],
  },
} as const;

/* --- rail and footer ---------------------------------------------------- */

export const RAIL_TAGLINE = ['Same', 'game', 'further', 'together'];

export const FOOTER = {
  tagline: ['Same game.', 'Further together.'],
  legal:
    'A fan project. Not affiliated with or endorsed by Nintendo. No game assets are distributed here.',
} as const;
