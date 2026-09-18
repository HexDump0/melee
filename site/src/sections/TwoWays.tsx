/* Browser or desktop, after the owner's comp: two windows — the port's
   launcher and a browser — with the stage art inside, and the honest button
   under each. The browser window is what is planned, so it is quieter and
   carries the badge; the desktop window is the path that exists. */

import { HEADINGS, WAYS, type WayCard } from '../content.tsx';
import { Button } from '../components/Button.tsx';
import { StageWindow } from '../components/StageWindow.tsx';
import { SectionHeading } from '../components/SectionHeading.tsx';
import { WRAP } from '../lib/layout.ts';

export function TwoWays() {
  const { eyebrow, title } = HEADINGS.download;

  return (
    <section id="download" className="scroll-mt-4 bg-mu-black py-12 lg:py-16">
      <div className={WRAP}>
        <SectionHeading eyebrow={eyebrow} title={title} tone="dark" />

        <div className="mt-8 grid gap-x-10 gap-y-14 lg:mt-10 lg:grid-cols-2">
          {WAYS.map((way) => (
            <Way key={way.title.join(' ')} way={way} />
          ))}
        </div>
      </div>
    </section>
  );
}

function Way({ way }: { way: WayCard }) {
  const planned = way.tone === 'light';

  return (
    <article className="flex flex-col">
      <StageWindow variant={way.frame} url={way.frame === 'browser'} />

      <div className="mt-8 flex flex-wrap items-center gap-x-5 gap-y-3">
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
      </div>

      <p className={`mt-5 max-w-md text-lede ${planned ? 'text-mu-dim' : 'text-mu-white/80'}`}>
        {way.body}
      </p>

      {/* Minimum gap, then all remaining height — so both buttons sit on the
          same line however much copy is above them. */}
      <div className="mt-8 grow lg:mt-10" />

      <Button {...way.cta} className="w-full" />
    </article>
  );
}
