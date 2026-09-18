/* The pale "three steps" band. The numeral is decoration — the list is an
   <ol>, so the order is in the markup and the big violet digit is
   aria-hidden.

   One rule keeps the three columns even: the divider is on the column, not
   inside it, so a step with two lines of title does not shift its rule. */

import { HEADINGS, STEPS } from '../content.tsx';
import { SectionHeading } from '../components/SectionHeading.tsx';

export function Steps() {
  const { eyebrow, title, note } = HEADINGS.setup;

  return (
    <section id="setup" className="band on-pale scroll-mt-4 bg-mu-pale text-mu-ink">
      <div className="wrap">
        <SectionHeading eyebrow={eyebrow} title={title} note={note} tone="pale" />

        <ol className="mt-block grid gap-x-10 gap-y-9 sm:grid-cols-2 lg:grid-cols-3 lg:gap-y-0">
          {STEPS.map((step, i) => (
            <li
              key={step.title.join(' ')}
              className="flex items-start gap-4 border-mu-ink/15 lg:border-l lg:pl-10 lg:first:border-l-0 lg:first:pl-0"
            >
              <span className="text-numeral tabular-nums text-mu-violet-deep" aria-hidden>
                {i + 1}
              </span>

              <div className="min-w-0 border-l border-mu-ink/15 pt-1 pl-5">
                <h3 className="text-h4 uppercase">
                  {step.title[0]}
                  {step.title[1] && (
                    <>
                      <br />
                      {step.title[1]}
                    </>
                  )}
                </h3>
                <p className="mt-3 text-lede text-mu-ink-dim">{step.body}</p>
              </div>
            </li>
          ))}
        </ol>
      </div>
    </section>
  );
}
