/* The feature row: four equal cells divided by hairlines, each a line icon,
   a title and a line of copy. No heading — the row is the whole band, so it
   reads as a spec plate rather than a section. */

import { FEATURES } from '../content.tsx';
import { WRAP } from '../lib/layout.ts';

export function Features() {
  return (
    <section id="features" className="on-pale scroll-mt-4 bg-mu-pale py-12 text-mu-ink lg:py-16">
      <div className={WRAP}>
        <ul className="grid divide-y divide-mu-ink/15 lg:grid-cols-4 lg:divide-x lg:divide-y-0">
          {FEATURES.map((feature) => {
            const Icon = feature.icon;

            return (
              <li key={feature.title} className="flex flex-col items-center gap-4 px-6 py-8 text-center">
                <Icon className="w-9 shrink-0" />
                <h3 className="text-h4 uppercase">{feature.title}</h3>
                <p className="max-w-xs text-lede text-mu-ink-dim">{feature.body}</p>
              </li>
            );
          })}
        </ul>
      </div>
    </section>
  );
}
