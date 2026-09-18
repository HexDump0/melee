/* The pale band under the FAQ. It starts at the rail's right edge, which is
   what the grid in App.tsx gives it for free. */

import { FOOTER, NAV } from '../content.tsx';
import { Wordmark } from './Wordmark.tsx';

export function Footer() {
  return (
    <footer className="band on-pale bg-mu-pale text-mu-ink">
      <div className="wrap">
        <div className="flex flex-wrap items-center gap-x-12 gap-y-7">
          <Wordmark surface="light" width="min(22rem, 52vw)" />

          <nav aria-label="Footer" className="flex flex-wrap items-center gap-x-8 gap-y-2">
            {NAV.map(({ id, label }) => (
              <a
                key={id}
                href={`#${id}`}
                className="text-micro uppercase transition-colors duration-200 hover:text-mu-violet-deep"
              >
                {label}
              </a>
            ))}
          </nav>

          <p className="text-micro uppercase text-mu-ink-dim lg:ml-auto lg:border-l lg:border-mu-ink/20 lg:pl-12">
            {FOOTER.tagline.map((line, i) => (
              <span key={line}>
                {line}
                {i < FOOTER.tagline.length - 1 && <br />}
              </span>
            ))}
          </p>
        </div>

        <p className="mt-9 max-w-3xl border-t border-mu-ink/15 pt-5 text-xs leading-relaxed text-mu-ink-dim">
          {FOOTER.legal}
        </p>
      </div>
    </footer>
  );
}
