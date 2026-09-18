/* The pale documentation band: one link row per destination. A plain rule-
   separated list reads calmer than cards here, and it scales to four
   destinations without a second row. */

import { DOC_CARDS, HEADINGS, type DocCard } from '../content.tsx';
import { useDestination } from '../hooks/useDestination.ts';
import { WRAP } from '../lib/layout.ts';
import { ArrowIcon } from '../components/icons.tsx';
import { SectionHeading } from '../components/SectionHeading.tsx';

export function Docs() {
  const { eyebrow, title } = HEADINGS.docs;

  return (
    <section id="docs" className="on-pale scroll-mt-4 bg-mu-pale py-12 text-mu-ink lg:py-16">
      <div className={WRAP}>
        <SectionHeading eyebrow={eyebrow} title={title} tone="pale" />

        <ul className="mt-8 border-t border-mu-ink/10 lg:mt-10">
          {DOC_CARDS.map((card) => (
            <li key={card.title}>
              <DocRow card={card} />
            </li>
          ))}
        </ul>
      </div>
    </section>
  );
}

function DocRow({ card }: { card: DocCard }) {
  const destination = useDestination(card.to);
  const Icon = card.icon;

  return (
    <a {...destination} className="group flex items-center gap-6 border-b border-mu-ink/10 py-6">
      <Icon className="w-6 shrink-0 text-mu-violet-deep" />

      <span className="min-w-0 flex-1 text-h4 uppercase">{card.title}</span>

      <ArrowIcon className="w-5 shrink-0 text-mu-violet-deep transition-transform duration-300 group-hover:translate-x-1" />
    </a>
  );
}
