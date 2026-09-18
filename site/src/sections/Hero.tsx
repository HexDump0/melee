/* The masthead: lockup, display headline, and the two doors.

   The oversized mark behind it is translated and scaled only — branding.md
   forbids rotating, stretching or recolouring the lockup. Below `lg` it
   drops to 10% opacity and becomes texture, because at that width there is
   no room for it to be artwork without colliding with the copy. */

import { HERO, type HeroCard } from '../content.tsx';
import { useDestination } from '../hooks/useDestination.ts';
import { ArrowIcon } from '../components/icons.tsx';
import { Wordmark } from '../components/Wordmark.tsx';

export function Hero() {
  return (
    <section id="play" className="band relative overflow-hidden scroll-mt-4 bg-mu-black">
      <img
        src="/media/icon.svg"
        alt=""
        aria-hidden
        className="pointer-events-none absolute top-[-6%] right-[-14%] w-[86%] opacity-10 select-none lg:top-[4%] lg:right-[-4%] lg:w-[42%] lg:opacity-100"
      />

      <div className="wrap relative">
        <div className="flex items-start justify-between gap-8">
          <a href="#top" className="block min-w-0">
            <Wordmark surface="dark" width="min(40rem, 74vw)" />
          </a>

          <div className="hidden pt-1 text-right sm:block">
            <p className="text-micro uppercase text-mu-dim">
              {HERO.kicker.map((word, i) => (
                <span key={word}>
                  {word}
                  {i < HERO.kicker.length - 1 && <br />}
                </span>
              ))}
            </p>
            <span className="mt-3 ml-auto block h-px w-9 bg-mu-violet" />
          </div>
        </div>

        <h1 className="mt-block text-display uppercase">
          {HERO.headline.map((line) => (
            <span key={line} className="block">
              {line}
            </span>
          ))}
        </h1>

        <p className="mt-5 text-eyebrow uppercase text-mu-white/80 lg:tracking-[0.3em]">
          {HERO.sub}
        </p>

        <div className="mt-block grid gap-grid sm:grid-cols-2">
          {HERO.cards.map((card, i) => (
            /* The first card is the door that works today, so it gets the
               violet; the second is the quieter white. */
            <HeroCta key={card.to + card.title[0]} card={card} tone={i === 0 ? 'violet' : 'white'} />
          ))}
        </div>
      </div>
    </section>
  );
}

function HeroCta({ card, tone }: { card: HeroCard; tone: 'violet' | 'white' }) {
  const destination = useDestination(card.to);
  const Icon = card.icon;

  return (
    <a
      {...destination}
      className={`group flex items-center gap-6 rounded-card p-card text-mu-black transition-transform duration-300 hover:-translate-y-1 ${
        tone === 'violet' ? 'on-violet bg-mu-violet' : 'on-pale bg-mu-white'
      }`}
    >
      <Icon className="w-11 shrink-0 lg:w-12" />

      <span className="min-w-0 flex-1">
        <strong className="block text-h3 uppercase">
          {card.title[0]}
          <br />
          {card.title[1]}
        </strong>
        <span className="mt-3 block text-micro uppercase">{card.sub}</span>
      </span>

      <ArrowIcon className="w-6 shrink-0 self-center transition-transform duration-300 group-hover:translate-x-1" />
    </a>
  );
}
