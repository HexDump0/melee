/* The page's button: one line icon and one label, the comp's restraint. The
   tone picks the fill — violet is the path that exists, outline is the
   quieter door.

   `to` resolves through `useDestination` (off-site, or a toast when a link is
   unset). `href` is for in-page anchors, which need no resolution — the
   hero's Download scrolls to the section instead of leaving the page. */

import type { AnchorHTMLAttributes, ComponentType, SVGProps } from 'react';
import { useDestination } from '../hooks/useDestination.ts';
import type { LinkKey } from '../lib/links.ts';

type Common = {
  icon: ComponentType<SVGProps<SVGSVGElement>>;
  label: string;
  tone: 'violet' | 'white' | 'outline';
  className?: string;
};

type Props = Common & ({ to: LinkKey; href?: undefined } | { href: string; to?: undefined });

const TONE = {
  violet: 'bg-mu-violet text-mu-black hover:bg-mu-violet-hi',
  white: 'bg-mu-white text-mu-black hover:bg-mu-pale',
  outline: 'border border-mu-white/25 text-mu-white hover:border-mu-white',
} as const;

const BASE = [
  'inline-flex items-center justify-center gap-4',
  'px-8 py-4 text-sm font-extrabold tracking-[0.14em] uppercase',
  'lg:px-12 lg:py-6 lg:text-lg',
  'transition-colors',
].join(' ');

export function Button(props: Props) {
  return props.href !== undefined ? <Shell {...props} /> : <DestinationButton {...props} />;
}

function DestinationButton({ to, ...common }: Common & { to: LinkKey }) {
  const destination = useDestination(to);
  return <Shell {...common} {...destination} />;
}

function Shell({
  icon: Icon,
  label,
  tone,
  className = '',
  ...anchor
}: Common & AnchorHTMLAttributes<HTMLAnchorElement>) {
  return (
    <a {...anchor} className={`${BASE} ${TONE[tone]} ${className}`}>
      <Icon className="h-5 w-5 shrink-0 lg:h-6 lg:w-6" />
      <span>{label}</span>
    </a>
  );
}
