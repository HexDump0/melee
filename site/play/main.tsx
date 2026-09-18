/* Client entry for /play. The page is client-rendered on purpose: it is a
   canvas and an emscripten module, so there is nothing to prerender. */

import { StrictMode } from 'react';
import { createRoot } from 'react-dom/client';
import { PlayPage } from './PlayPage.tsx';
import '../src/styles.css';

const root = document.getElementById('root');
if (root) {
  createRoot(root).render(
    <StrictMode>
      <PlayPage />
    </StrictMode>,
  );
}
