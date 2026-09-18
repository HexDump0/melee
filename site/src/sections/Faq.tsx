/* The FAQ.

   Built on native <details name="faq">, so it is an exclusive accordion with
   no JavaScript at all — it works on the prerendered page before the bundle
   lands, and keeps working if the bundle never does. The only motion is the
   `animate-faq-answer` token in styles.css, which runs on open, and the
   chevron turning over. */

import { FAQS, HEADINGS } from '../content.tsx';
import type { ReactNode } from 'react';
import { WRAP } from '../lib/layout.ts';
import { ChevronIcon } from '../components/icons.tsx';
import { SectionHeading } from '../components/SectionHeading.tsx';

export function Faq() {
  const { eyebrow, title } = HEADINGS.faq;

  return (
    <section id="faq" className="scroll-mt-4 bg-mu-black py-12 lg:py-16">
      <div className={WRAP}>
        <SectionHeading eyebrow={eyebrow} title={title} tone="dark" />

        <div className="mt-8 space-y-2 lg:mt-10">
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
    <details
      name="faq"
      className="group rounded-card bg-mu-card px-6 transition-colors duration-200 hover:bg-mu-card-hi lg:px-7"
    >
      <summary className="flex cursor-pointer items-center justify-between gap-6 py-5 text-lede font-medium">
        {question}
        <ChevronIcon className="w-5 shrink-0 text-mu-dim transition-transform duration-300 group-open:rotate-180 group-open:text-mu-violet" />
      </summary>
      <p className="animate-faq-answer max-w-3xl pb-5 text-lede text-mu-dim motion-reduce:animate-none">
        {answer}
      </p>
    </details>
  );
}
