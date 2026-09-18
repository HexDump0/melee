/* The page.

   Layout is one grid: `[rail | content]`. The rail column is violet the full
   height of the document, which is what makes the footer's pale band start
   at the rail's right edge rather than at the window edge. */

import { SECTION_IDS } from './content.tsx';
import { Footer } from './components/Footer.tsx';
import { Rail } from './components/Rail.tsx';
import { ToastProvider } from './components/Toast.tsx';
import { useScrollSpy } from './hooks/useScrollSpy.ts';
import { Docs } from './sections/Docs.tsx';
import { Faq } from './sections/Faq.tsx';
import { Hero } from './sections/Hero.tsx';
import { Steps } from './sections/Steps.tsx';
import { TwoWays } from './sections/TwoWays.tsx';

export function App() {
  const active = useScrollSpy(SECTION_IDS);

  return (
    <ToastProvider>
      <a
        href="#main"
        className="sr-only focus:not-sr-only focus:fixed focus:top-4 focus:left-4 focus:z-50 focus:bg-mu-violet focus:px-4 focus:py-2 focus:text-micro focus:uppercase focus:text-mu-black"
      >
        Skip to content
      </a>

      <div className="grid min-h-screen grid-cols-1 lg:grid-cols-[var(--spacing-rail)_minmax(0,1fr)]">
        <Rail active={active} />

        <div id="top" className="min-w-0">
          <main id="main">
            <Hero />
            <Steps />
            <TwoWays />
            <Docs />
            <Faq />
          </main>
          <Footer />
        </div>
      </div>
    </ToastProvider>
  );
}
