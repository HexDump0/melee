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

export function PlusIcon(props: IconProps) {
  return (
    <svg {...stroke} strokeWidth={2.4} {...props}>
      <path d="M12 5v14M5 12h14" />
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
