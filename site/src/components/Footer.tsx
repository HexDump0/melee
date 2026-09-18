/* The footer: the lockup, the same in-page links as the header, the
   repository, and the small print. Black, so it closes the page the way the
   header opens it, with a rule to separate it from the FAQ. */

import { FOOTER, NAV, TAGLINE } from '../content.tsx';
import { WRAP } from '../lib/layout.ts';
import { LINKS } from '../lib/links.ts';
import { GitHubIcon } from './icons.tsx';
import { Wordmark } from './Wordmark.tsx';

const LINK =
  'text-sm font-extrabold tracking-[0.2em] uppercase text-mu-dim transition-colors hover:text-mu-white';

export function Footer() {
  return (
    <footer className="border-t border-mu-white/10 bg-mu-black">
      <div className={`${WRAP} flex flex-wrap items-center gap-x-10 gap-y-6 py-10`}>
        <a href="#top" aria-label="Melee Unbound — top of page">
          <Wordmark surface="dark" className="w-52" />
        </a>

        <nav aria-label="Footer" className="flex flex-wrap items-center gap-x-10 gap-y-2">
          {NAV.map(({ id, label }) => (
            <a key={id} href={`#${id}`} className={LINK}>
              {label}
            </a>
          ))}
        </nav>

        <a
          href={LINKS.repo}
          target="_blank"
          rel="noopener"
          aria-label="Melee Unbound on GitHub"
          className="ml-auto text-mu-dim transition-colors hover:text-mu-white"
        >
          <GitHubIcon className="w-5" />
        </a>
      </div>

      <div
        className={`${WRAP} flex flex-wrap items-center justify-between gap-x-10 gap-y-3 border-t border-mu-white/10 py-6`}
      >
        <p className="text-micro uppercase text-mu-dim">{TAGLINE.join(' ')}</p>
        <p className="max-w-2xl text-xs leading-relaxed text-mu-dim">{FOOTER.legal}</p>
      </div>
    </footer>
  );
}
