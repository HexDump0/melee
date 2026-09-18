/* Browser or desktop. The violet card is the path that exists; the light one
   carries a badge saying what it is. The tone drives colour *and* the bullet
   treatment — ticks for the real option, dots for the planned one — so the
   two cards read as a claim and a caveat rather than two equal offers.

   The CTA is pushed to the bottom with `mt-auto` so both cards' buttons line
   up however much body copy sits above them. */

import { HEADINGS, WAYS, type WayCard } from '../content.tsx';
import { useDestination } from '../hooks/useDestination.ts';
import { ArrowIcon, CheckIcon } from '../components/icons.tsx';
import { SectionHeading } from '../components/SectionHeading.tsx';

/** The violet the tick is cut out of — see CheckIcon. */
const VIOLET = '#a08aff';

export function TwoWays() {
  const { eyebrow, title, sub } = HEADINGS.download;

  return (
    <section id="download" className="band scroll-mt-4 bg-mu-black">
      <div className="wrap">
        <SectionHeading eyebrow={eyebrow} title={title} tone="dark">
          <p className="mt-5 text-eyebrow uppercase tracking-[0.3em] text-mu-white/80">{sub}</p>
        </SectionHeading>

        <div className="mt-block grid gap-grid lg:grid-cols-2">
          {WAYS.map((way) => (
            <Way key={way.title.join(' ')} way={way} />
          ))}
        </div>
      </div>
    </section>
  );
}

function Way({ way }: { way: WayCard }) {
  const destination = useDestination(way.cta.to);
  const violet = way.tone === 'violet';
  const Icon = way.icon;

  return (
    <article
      className={`flex flex-col rounded-card p-card text-mu-black ${
        violet ? 'on-violet bg-mu-violet' : 'on-pale bg-mu-white'
      }`}
    >
      <header className="flex flex-wrap items-center gap-x-6 gap-y-3">
        <Icon className="w-12 shrink-0" />
        <h3 className="text-h3 uppercase">
          {way.title[0]}
          <br />
          {way.title[1]}
        </h3>
        {way.badge && (
          <span className="bg-mu-violet-deep px-3 py-1 text-[0.68rem] font-extrabold tracking-[0.16em] uppercase text-mu-white">
            {way.badge}
          </span>
        )}
      </header>

      <p className={`mt-6 text-lede ${violet ? '' : 'text-mu-ink-dim'}`}>{way.body}</p>

      <ul className={`mt-5 space-y-2.5 text-lede ${violet ? '' : 'text-mu-ink-dim'}`}>
        {way.points.map((point) => (
          <li key={point} className="flex items-start gap-3">
            {violet ? (
              <CheckIcon tick={VIOLET} className="mt-1 w-5 shrink-0" />
            ) : (
              <span className="mt-2.5 h-1.5 w-1.5 shrink-0 rounded-full bg-mu-violet-deep" />
            )}
            {point}
          </li>
        ))}
      </ul>

      {/* Minimum gap, then all remaining height — so both cards' CTAs sit
          on the same line however much body copy is above them. */}
      <div className="mt-block grow" />

      <a
        {...destination}
        className="group flex items-center justify-center gap-3 bg-mu-black px-6 py-4 text-micro uppercase text-mu-white transition-colors duration-200 hover:bg-mu-ink-dim"
      >
        {way.cta.label}
        <ArrowIcon className="w-5 transition-transform duration-300 group-hover:translate-x-1" />
      </a>
    </article>
  );
}
