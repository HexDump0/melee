/* The FAQ.

   Built on native <details name="faq">, so it is an exclusive accordion with
   no JavaScript at all — it works on the prerendered page before the bundle
   lands, and keeps working if the bundle never does. The only motion is the
   `.faq-answer` keyframe in styles.css, which runs on open. */

import { FAQS, HEADINGS } from '../content.tsx';
import type { ReactNode } from 'react';
import { PlusIcon } from '../components/icons.tsx';
import { SectionHeading } from '../components/SectionHeading.tsx';

export function Faq() {
  const { eyebrow, title, note } = HEADINGS.faq;

  return (
    <section id="faq" className="band scroll-mt-4 bg-mu-black">
      <div className="wrap">
        <SectionHeading eyebrow={eyebrow} title={title} note={note} tone="dark" />

        <div className="mt-block space-y-2">
          {FAQS.map((faq) => (
            <FaqRow key={faq.q} question={faq.q} answer={faq.a} />
          ))}
        </div>
      </div>
    </section>
  );
}

function FaqRow({ question, answer }: { question: string; answer: ReactNode }) {
  return (
    <details name="faq" className="group rounded-card bg-mu-card px-6 lg:px-7">
      <summary className="flex cursor-pointer items-center justify-between gap-6 py-4 text-lede font-medium">
        {question}
        <PlusIcon className="w-5 shrink-0 text-mu-violet transition-transform duration-300 group-open:rotate-45" />
      </summary>
      <p className="faq-answer max-w-2xl pb-5 text-lede text-mu-dim">{answer}</p>
    </details>
  );
}
