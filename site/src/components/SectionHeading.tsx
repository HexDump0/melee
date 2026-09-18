/* The heading block every band below the hero opens with: a ruled eyebrow
   and a display title, plus an optional line under it (the "same game"
   tagline the browser-or-desktop band uses).

   `tone` picks the muted colour for the eyebrow, which is the only thing
   that differs between the black and pale bands. */

import type { ReactNode } from 'react';

type Props = {
  eyebrow: string;
  /** One entry per line of the title. */
  title: readonly string[];
  tone: 'dark' | 'pale';
  /** Rendered under the title. */
  children?: ReactNode;
};

export function SectionHeading({ eyebrow, title, tone, children }: Props) {
  return (
    <div className="min-w-0">
      <p
        className={`flex items-center gap-4 text-eyebrow uppercase ${
          tone === 'pale' ? 'text-mu-ink-dim' : 'text-mu-dim'
        }`}
      >
        <span aria-hidden className="block h-0.5 w-9 flex-none bg-current" />
        {eyebrow}
      </p>

      <h2 className="mt-5 text-h2 uppercase">
        {title.map((line) => (
          <span key={line} className="block">
            {line}
          </span>
        ))}
      </h2>

      {children}
    </div>
  );
}
