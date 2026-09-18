/* The page.

   The first screen is the hero: the header and the hero share one
   `min-h-svh` block, so the name, the window and the bottom rule fill the
   viewport and the sections scroll in under them. The background is black
   throughout; the pale bands in the middle supply the rhythm. */

import { SECTION_IDS } from './content.tsx';
import { Footer } from './components/Footer.tsx';
import { Header } from './components/Header.tsx';
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
        className="sr-only focus:not-sr-only focus:fixed focus:top-4 focus:left-4 focus:z-50 focus:bg-mu-violet focus:px-4 focus:py-2 focus:text-micro focus:text-mu-black focus:uppercase"
      >
        Skip to content
      </a>

      {/* The comp's slim violet edge, full height, behind nothing. */}
      <div aria-hidden className="fixed inset-y-0 left-0 z-40 w-2 max-w-16 bg-mu-violet md:w-[3.3vw]" />

      <div id="top" className="flex min-h-svh flex-col">
        <Header active={active} />
        <Hero />
      </div>

      <main id="main">
        <Steps />
        <TwoWays />
        <Docs />
        <Faq />
      </main>
      <Footer />
    </ToastProvider>
  );
}
