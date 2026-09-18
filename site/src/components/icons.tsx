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

export function GlobeIcon(props: IconProps) {
  return (
    <svg {...stroke} strokeWidth={1.7} {...props}>
      <circle cx="12" cy="12" r="9" />
      <path d="M3 12h18" />
      <path d="M12 3c2.5 2.6 3.8 5.7 3.8 9s-1.3 6.4-3.8 9c-2.5-2.6-3.8-5.7-3.8-9S9.5 5.6 12 3z" />
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

/** A tick in a circle, for "byte matched". */
export function CheckCircleIcon(props: IconProps) {
  return (
    <svg {...stroke} strokeWidth={1.7} {...props}>
      <circle cx="12" cy="12" r="9" />
      <path d="m8.5 12.3 2.5 2.5 4.5-5.2" />
    </svg>
  );
}

/** A display, for the renderer. */
export function MonitorIcon(props: IconProps) {
  return (
    <svg {...stroke} strokeWidth={1.7} {...props}>
      <rect x="3" y="4.5" width="18" height="12.5" rx="1" />
      <path d="M9 21h6m-3-4v4" />
    </svg>
  );
}

export function LockIcon(props: IconProps) {
  return (
    <svg {...stroke} strokeWidth={1.8} {...props}>
      <rect x="5" y="10.5" width="14" height="9.5" rx="1.5" />
      <path d="M8.5 10.5V7.75a3.5 3.5 0 0 1 7 0v2.75" />
    </svg>
  );
}

export function ReloadIcon(props: IconProps) {
  return (
    <svg {...stroke} strokeWidth={1.8} {...props}>
      <path d="M20 12a8 8 0 1 1-2.4-5.7" />
      <path d="M20 4v4h-4" />
    </svg>
  );
}

/** The Discord mark. Filled, unlike the line icons above. */
export function DiscordIcon(props: IconProps) {
  return (
    <svg viewBox="0 0 24 24" fill="currentColor" aria-hidden {...props}>
      <path d="M20.32 4.37a19.8 19.8 0 0 0-4.89-1.51.07.07 0 0 0-.08.04c-.21.37-.44.86-.61 1.25a18.3 18.3 0 0 0-5.48 0c-.17-.4-.41-.88-.62-1.25a.08.08 0 0 0-.08-.04 19.74 19.74 0 0 0-4.88 1.52.07.07 0 0 0-.04.03C.53 9.05-.32 13.58.1 18.06c0 .02.01.04.03.06a19.9 19.9 0 0 0 6 3.03.08.08 0 0 0 .08-.03c.46-.63.87-1.3 1.22-1.99a.08.08 0 0 0-.04-.11 13.1 13.1 0 0 1-1.87-.89.08.08 0 0 1 0-.13l.37-.29a.07.07 0 0 1 .08-.01c3.93 1.79 8.18 1.79 12.06 0a.07.07 0 0 1 .08.01c.12.1.25.2.37.29a.08.08 0 0 1 0 .13c-.6.35-1.22.64-1.88.89a.08.08 0 0 0-.04.11c.36.7.77 1.36 1.23 1.99a.08.08 0 0 0 .08.03 19.84 19.84 0 0 0 6-3.03.08.08 0 0 0 .03-.06c.5-5.18-.84-9.67-3.55-13.66a.06.06 0 0 0-.03-.03ZM8.02 15.33c-1.18 0-2.16-1.08-2.16-2.42 0-1.33.96-2.42 2.16-2.42 1.21 0 2.18 1.1 2.16 2.42 0 1.34-.96 2.42-2.16 2.42Zm7.97 0c-1.18 0-2.15-1.08-2.15-2.42 0-1.33.95-2.42 2.15-2.42 1.21 0 2.18 1.1 2.16 2.42 0 1.34-.95 2.42-2.16 2.42Z" />
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

