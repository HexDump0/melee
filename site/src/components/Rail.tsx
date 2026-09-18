/* The violet left rail: mark, section links, tagline, and the wordmark in
   text pinned to the bottom. Below `lg` it becomes a sticky top bar.

   The active link is black and 900-weight, not white. The design comp paints
   it white on violet, which is 2.77:1 and fails — see the contrast note at
   the top of styles.css.

   The rail's own rhythm is its own: it is a fixed-width column of small
   type, so it uses plain steps rather than the band tokens, which are sized
   for the content column. */

import { NAV, RAIL_TAGLINE, type SectionId } from '../content.tsx';

export function Rail({ active }: { active: SectionId | null }) {
  return (
    <aside className="on-violet bg-mu-violet text-mu-black">
      <div className="sticky top-0 z-30 flex items-center gap-6 bg-mu-violet px-5 py-4 lg:h-screen lg:flex-col lg:items-start lg:gap-0 lg:px-7 lg:py-8">
        <a href="#top" className="block shrink-0" aria-label="Melee Unbound — top of page">
          <img src="/media/icon.svg" alt="" width={48} height={48} className="w-9 lg:w-11" />
        </a>

        <nav
          aria-label="Main"
          className="flex flex-wrap items-center gap-x-5 gap-y-2 lg:mt-12 lg:flex-col lg:items-start lg:gap-5"
        >
          {NAV.map(({ id, label }) => (
            <a
              key={id}
              href={`#${id}`}
              aria-current={active === id ? 'true' : undefined}
              data-active={active === id ? '' : undefined}
              className="text-micro uppercase opacity-60 transition-opacity duration-200 hover:opacity-100 data-active:font-black data-active:opacity-100 lg:text-rail"
            >
              {label}
            </a>
          ))}
        </nav>

        <div className="ml-auto hidden lg:mt-12 lg:ml-0 lg:block">
          <span className="mb-5 block h-px w-9 bg-mu-black/40" />
          <p className="text-rail leading-[1.9] uppercase">
            {RAIL_TAGLINE.map((word, i) => (
              <span key={word}>
                {word}
                {i < RAIL_TAGLINE.length - 1 && <br />}
              </span>
            ))}
          </p>
        </div>

        <div className="hidden lg:mt-auto lg:block">
          <span className="mb-4 block h-px w-9 bg-mu-black/40" />
          <p className="text-rail leading-[1.7] uppercase">
            Melee
            <br />
            Unbound
          </p>
        </div>
      </div>
    </aside>
  );
}
