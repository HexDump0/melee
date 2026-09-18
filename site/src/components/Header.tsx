/* The top bar: the lockup, a rule, the section links, and the repository.

   The links are Tailwind utilities, including the active state: the
   scroll-spy sets `data-active` and the `data-[active]:` variants underline
   it in violet, instantly — navigation state, not a fade. Below `sm` the
   full lockup would be under branding.md's 240px minimum, so the bar shows
   the icon on its own instead. */

import { NAV, type SectionId } from '../content.tsx';
import { WRAP } from '../lib/layout.ts';
import { LINKS } from '../lib/links.ts';
import { Wordmark } from './Wordmark.tsx';

const LINK = [
  'relative text-sm font-extrabold tracking-[0.2em] uppercase lg:text-base',
  'text-mu-white/60 transition-colors hover:text-mu-white',
  'data-[active]:text-mu-white',
  'data-[active]:after:absolute data-[active]:after:inset-x-0',
  'data-[active]:after:-bottom-1.5 data-[active]:after:h-0.5',
  'data-[active]:after:bg-mu-violet',
].join(' ');

export function Header({ active }: { active: SectionId | null }) {
  return (
    <header className="pt-5">
      <div className={`${WRAP} flex flex-wrap items-center gap-x-8 gap-y-4`}>
        <a href="#top" aria-label="Melee Unbound — top of page">
          <img src="/media/icon.svg" alt="" width={512} height={512} className="w-9 sm:hidden" />
          <Wordmark surface="dark" className="hidden w-60 sm:block lg:w-84" />
        </a>

        <span aria-hidden className="hidden h-px flex-1 bg-mu-white/15 sm:block" />

        <nav aria-label="Main" className="flex flex-wrap items-center gap-x-10 gap-y-2 lg:gap-x-14">
          {NAV.map(({ id, label }) => (
            <a key={id} href={`#${id}`} data-active={active === id ? '' : undefined} className={LINK}>
              {label}
            </a>
          ))}
          <a href={LINKS.repo} target="_blank" rel="noopener" className={LINK}>
            Source
          </a>
        </nav>
      </div>
    </header>
  );
}
