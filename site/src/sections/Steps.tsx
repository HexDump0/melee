/* The pale "three steps" band, after the owner's comp: a numbered disc, a
   title with an arrow pointing at the next step, and the body under it. The
   numeral is decoration — the list is an <ol>, so the order is in the markup
   and the disc is aria-hidden. */

import { HEADINGS, STEPS } from '../content.tsx';
import { WRAP } from '../lib/layout.ts';
import { ArrowIcon } from '../components/icons.tsx';
import { SectionHeading } from '../components/SectionHeading.tsx';

export function Steps() {
  const { eyebrow, title } = HEADINGS.setup;

  return (
    <section id="setup" className="on-pale scroll-mt-4 bg-mu-pale py-12 text-mu-ink lg:py-16">
      <div className={WRAP}>
        <SectionHeading eyebrow={eyebrow} title={title} tone="pale" />

        <ol className="mt-8 grid gap-x-10 gap-y-10 md:grid-cols-3 lg:mt-10">
          {STEPS.map((step, i) => (
            <li key={step.title.join(' ')} className="flex items-start gap-5">
              <span
                aria-hidden
                className="flex h-11 w-11 shrink-0 items-center justify-center rounded-full border border-mu-ink/20 text-h4 text-mu-violet-deep"
              >
                {i + 1}
              </span>

              <div className="min-w-0">
                <div className="flex items-center gap-5">
                  <h3 className="text-h4 uppercase">
                    {step.title[0]}
                    {step.title[1] && (
                      <>
                        <br />
                        {step.title[1]}
                      </>
                    )}
                  </h3>
                  {i < STEPS.length - 1 && (
                    <ArrowIcon className="hidden w-6 shrink-0 text-mu-ink-dim md:block" />
                  )}
                </div>

                <p className="mt-3 text-lede text-mu-ink-dim">{step.body}</p>
              </div>
            </li>
          ))}
        </ol>
      </div>
    </section>
  );
}
