/* The heading block every band below the hero opens with: a ruled eyebrow, a
   display title on one or more lines, and an optional note set to the right.

   The note's top margin is not a guess — it is the eyebrow's line box plus
   the gap under it, so the note's first line sits level with the title's
   first line rather than floating somewhere above or below it.

   `tone` picks the text colour for the note, which is the only thing that
   differs between the black and pale bands. */

import type { ReactNode } from 'react';

type Props = {
  eyebrow: string;
  /** One entry per line of the title. */
  title: readonly string[];
  /** Small tracked text on the right, one entry per line. */
  note?: readonly string[];
  tone: 'dark' | 'pale';
  /** Rendered under the title — the tagline line the hero-ish bands use. */
  children?: ReactNode;
};

export function SectionHeading({ eyebrow, title, note, tone, children }: Props) {
  return (
    <div className="flex flex-wrap items-start justify-between gap-x-12 gap-y-6">
      <div className="min-w-0">
        <p
          className={`flex items-center gap-4 text-eyebrow uppercase ${
            tone === 'pale' ? 'text-mu-ink-dim' : 'text-mu-dim'
          }`}
        >
          <span className="eyebrow-rule" />
          {eyebrow}
        </p>

        <h2 className="mt-5 text-h2 uppercase">
          {title.map((line, i) => (
            <span key={line} className="block">
              {line}
              {i < title.length - 1 && <br />}
            </span>
          ))}
        </h2>

        {children}
      </div>

      {/* `basis-full` below lg: the note drops onto its own line in every
          band rather than squeezing beside a short title in some and not
          others. From lg it sits right, and 1.22rem of eyebrow line box plus
          the 1.25rem gap under it puts its first line level with the title. */}
      {note && (
        <p
          className={`basis-full text-micro uppercase lg:basis-auto lg:mt-[2.47rem] ${
            tone === 'pale' ? 'text-mu-ink-dim' : 'text-mu-dim'
          }`}
        >
          {note.map((line, i) => (
            <span key={line}>
              {line}
              {i < note.length - 1 && <br />}
            </span>
          ))}
        </p>
      )}
    </div>
  );
}
