/* Turn a LinkKey into props you can spread onto an <a>.

   A key with a URL becomes a real external link. A key without one still
   renders as a link, but clicking it says so — see the note in lib/links.ts. */

import type { AnchorHTMLAttributes, MouseEvent } from 'react';
import { LINKS, LINK_LABELS, type LinkKey } from '../lib/links.ts';
import { useToast } from '../components/Toast.tsx';

export function useDestination(key: LinkKey): AnchorHTMLAttributes<HTMLAnchorElement> {
  const toast = useToast();
  const href = LINKS[key];

  if (href) return { href, target: '_blank', rel: 'noopener' };

  return {
    href: '#',
    onClick: (event: MouseEvent<HTMLAnchorElement>) => {
      event.preventDefault();
      toast(`${LINK_LABELS[key]} link is not set yet.`);
    },
  };
}
