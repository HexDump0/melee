/* Line icons, drawn on a 24x24 grid to match the design comp.

   They inherit colour (`stroke="currentColor"`) and size (`className`) from
   the caller, so a card decides how big its icon is, not this file. */

import type { SVGProps } from 'react';

type IconProps = SVGProps<SVGSVGElement>;

/** Shared geometry for the stroked icons. */
const stroke = {
  viewBox: '0 0 24 24',
  fill: 'none',
  stroke: 'currentColor',
  strokeLinecap: 'round',
  strokeLinejoin: 'round',
  'aria-hidden': true,
} as const;

export function DownloadIcon(props: IconProps) {
  return (
    <svg {...stroke} strokeWidth={1.8} {...props}>
      <path d="M12 3v11m0 0 4.2-4.2M12 14l-4.2-4.2" />
      <path d="M4.5 16.5v3A1.5 1.5 0 0 0 6 21h12a1.5 1.5 0 0 0 1.5-1.5v-3" />
    </svg>
  );
}

export function DocIcon(props: IconProps) {
  return (
    <svg {...stroke} strokeWidth={1.8} {...props}>
      <path d="M7 3h7l4 4v14H7z" />
      <path d="M14 3v4h4" />
      <path d="M10 12.5h5M10 16.5h5" />
    </svg>
  );
}

export function GlobeIcon(props: IconProps) {
  return (
    <svg {...stroke} strokeWidth={1.7} {...props}>
      <circle cx="12" cy="12" r="9" />
      <path d="M3 12h18" />
      <path d="M12 3c2.5 2.6 3.8 5.7 3.8 9s-1.3 6.4-3.8 9c-2.5-2.6-3.8-5.7-3.8-9S9.5 5.6 12 3z" />
    </svg>
  );
}

export function BookIcon(props: IconProps) {
  return (
    <svg {...stroke} strokeWidth={1.7} {...props}>
      <path d="M12 6.5C10.4 5.2 8.3 4.5 5.5 4.5H4v13h1.5c2.8 0 4.9.7 6.5 2m0-13c1.6-1.3 3.7-2 6.5-2H20v13h-1.5c-2.8 0-4.9.7-6.5 2m0-13v13" />
    </svg>
  );
}

export function ArrowIcon(props: IconProps) {
  return (
    <svg {...stroke} strokeWidth={2.4} {...props}>
      <path d="M4 12h15m0 0-6-6m6 6-6 6" />
    </svg>
  );
}

/**
 * The play ring from the window artwork: a violet ring with a white
 * triangle. Unlike the line icons, the triangle is always white — that is
 * the mark the design comp draws.
 */
export function PlayIcon(props: IconProps) {
  return (
    <svg viewBox="0 0 96 96" fill="none" aria-hidden {...props}>
      <circle cx="48" cy="48" r="44" stroke="currentColor" strokeWidth={3} />
      <path d="M38 30 66 48 38 66Z" fill="#ffffff" />
    </svg>
  );
}

export function ChevronIcon(props: IconProps) {
  return (
    <svg {...stroke} strokeWidth={2} {...props}>
      <path d="m6 9 6 6 6-6" />
    </svg>
  );
}

/** A cube, used wherever the copy means "mods". */
export function CubeIcon(props: IconProps) {
  return (
    <svg {...stroke} strokeWidth={1.7} {...props}>
      <path d="M12 3 4 7v10l8 4 8-4V7z" />
      <path d="M4 7l8 4 8-4M12 11v10" />
    </svg>
  );
}

export function HelpIcon(props: IconProps) {
  return (
    <svg {...stroke} strokeWidth={1.7} {...props}>
      <circle cx="12" cy="12" r="9" />
      <path d="M9.6 9.4a2.5 2.5 0 1 1 3.2 2.4c-.5.2-.8.7-.8 1.2v.4" />
      <path d="M12 17h.01" />
    </svg>
  );
}

/** The GitHub mark. Filled, unlike the line icons above. */
export function GitHubIcon(props: IconProps) {
  return (
    <svg viewBox="0 0 24 24" fill="currentColor" aria-hidden {...props}>
      <path d="M12 .5A11.5 11.5 0 0 0 .5 12a11.5 11.5 0 0 0 7.86 10.91c.58.11.79-.25.79-.55v-2.17c-3.2.7-3.88-1.37-3.88-1.37-.52-1.33-1.28-1.68-1.28-1.68-1.05-.72.08-.7.08-.7 1.16.08 1.77 1.19 1.77 1.19 1.03 1.77 2.7 1.26 3.36.96.1-.75.4-1.26.73-1.55-2.55-.29-5.23-1.28-5.23-5.68 0-1.25.45-2.28 1.19-3.08-.12-.29-.51-1.46.11-3.05 0 0 .96-.31 3.15 1.18a10.9 10.9 0 0 1 5.74 0c2.19-1.49 3.15-1.18 3.15-1.18.62 1.59.23 2.76.11 3.05.74.8 1.19 1.83 1.19 3.08 0 4.41-2.69 5.38-5.25 5.67.41.36.78 1.05.78 2.13v3.16c0 .3.2.67.79.55A11.5 11.5 0 0 0 23.5 12 11.5 11.5 0 0 0 12 .5z" />
    </svg>
  );
}

/**
 * The filled check used in the violet card's feature list. The disc is
 * `currentColor` and the tick is cut out of it in the surface colour, so the
 * caller passes the surface it sits on.
 */
export function CheckIcon({ tick, ...props }: IconProps & { tick: string }) {
  return (
    <svg viewBox="0 0 24 24" aria-hidden {...props}>
      <circle cx="12" cy="12" r="11" fill="currentColor" />
      <path
        d="m7 12.4 3.2 3.2L17 8.7"
        fill="none"
        stroke={tick}
        strokeWidth={2.4}
        strokeLinecap="round"
        strokeLinejoin="round"
      />
    </svg>
  );
}
