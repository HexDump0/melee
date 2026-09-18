/* The page's button: one line icon and one label, the comp's restraint. The
   tone picks the fill — violet is the path that exists, outline is the
   quieter door. */

import type { ComponentType, SVGProps } from 'react';
import { useDestination } from '../hooks/useDestination.ts';
import type { LinkKey } from '../lib/links.ts';

type Props = {
  to: LinkKey;
  icon: ComponentType<SVGProps<SVGSVGElement>>;
  label: string;
  tone: 'violet' | 'white' | 'outline';
  className?: string;
};

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

export function Button({ to, icon: Icon, label, tone, className = '' }: Props) {
  const destination = useDestination(to);

  return (
    <a {...destination} className={`${BASE} ${TONE[tone]} ${className}`}>
      <Icon className="h-5 w-5 shrink-0 lg:h-6 lg:w-6" />
      <span>{label}</span>
    </a>
  );
}
