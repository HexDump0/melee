/* Client entry. The markup is already in the document (see prerender.mjs),
   so this hydrates it rather than rendering it — the page never blanks and
   never reflows on load. */

import { StrictMode } from 'react';
import { hydrateRoot } from 'react-dom/client';
import { App } from './App.tsx';
import './styles.css';

const root = document.getElementById('root');
if (root) {
  hydrateRoot(
    root,
    <StrictMode>
      <App />
    </StrictMode>,
  );
}
