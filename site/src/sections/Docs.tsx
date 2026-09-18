/* The pale documentation band: two link cards, nothing else. */

import { DOC_CARDS, HEADINGS, type DocCard } from '../content.tsx';
import { useDestination } from '../hooks/useDestination.ts';
import { ArrowIcon } from '../components/icons.tsx';
import { SectionHeading } from '../components/SectionHeading.tsx';

export function Docs() {
  const { eyebrow, title, note } = HEADINGS.docs;

  return (
    <section id="docs" className="band on-pale scroll-mt-4 bg-mu-pale text-mu-ink">
      <div className="wrap">
        <SectionHeading eyebrow={eyebrow} title={title} note={note} tone="pale" />

        <div className="mt-block grid gap-grid lg:grid-cols-2">
          {DOC_CARDS.map((card) => (
            <DocLink key={card.title} card={card} />
          ))}
        </div>
      </div>
    </section>
  );
}

function DocLink({ card }: { card: DocCard }) {
  const destination = useDestination(card.to);
  const Icon = card.icon;

  return (
    <a
      {...destination}
      className="group flex items-center gap-6 rounded-card bg-mu-white p-card transition-transform duration-300 hover:-translate-y-1"
    >
      <Icon className="w-11 shrink-0" />

      <span className="min-w-0 flex-1">
        <strong className="block text-h4 uppercase">{card.title}</strong>
        <span className="mt-2 block text-lede text-mu-ink-dim">{card.sub}</span>
      </span>

      <ArrowIcon className="w-6 shrink-0 text-mu-violet-deep transition-transform duration-300 group-hover:translate-x-1" />
    </a>
  );
}
