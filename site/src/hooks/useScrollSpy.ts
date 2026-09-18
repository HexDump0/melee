/* Which section is the reader actually looking at?

   Answers with the id of the section crossing the middle of the viewport, so
   the rail can mark its matching link. This is navigation state, not
   decoration, so it stays on when the visitor has asked for reduced motion.

   An IntersectionObserver with a single midline rootMargin does the whole
   job: the top and bottom margins collapse the viewport to a one-pixel band
   at 55% down, and a section intersects that band exactly while it is the
   one under the reader. */

import { useEffect, useState } from 'react';

/** How far down the viewport the midline sits. */
const MIDLINE = 55;

export function useScrollSpy<Id extends string>(ids: readonly Id[]): Id | null {
  const [active, setActive] = useState<Id | null>(null);

  useEffect(() => {
    const sections = ids
      .map((id) => document.getElementById(id))
      .filter((el): el is HTMLElement => el !== null);
    if (!sections.length || typeof IntersectionObserver === 'undefined') return;

    const observer = new IntersectionObserver(
      (entries) => {
        for (const entry of entries) {
          if (entry.isIntersecting) setActive(entry.target.id as Id);
        }
      },
      { rootMargin: `-${MIDLINE}% 0px -${100 - MIDLINE}% 0px`, threshold: 0 },
    );

    for (const section of sections) observer.observe(section);
    return () => observer.disconnect();
  }, [ids]);

  return active;
}
