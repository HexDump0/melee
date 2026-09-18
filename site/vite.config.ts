import { defineConfig, type Connect, type Plugin } from 'vite';
import react from '@vitejs/plugin-react';
import tailwindcss from '@tailwindcss/vite';

// Two entries: the marketing page (index.html, prerendered by prerender.mjs)
// and the browser port (/play/, a static page that loads the wasm build from
// public/play/). The client build emits both into dist/.
/* Dev and preview only: serve `/play` — not just `/play/index.html` — so the
   local URL matches what a static host will do with dist/play/index.html. */
function playIndex(): Plugin {
  const rewrite: Connect.NextHandleFunction = (req, _res, next) => {
    if (req.url === '/play' || req.url === '/play/') {
      req.url = '/play/index.html';
    }
    next();
  };
  return {
    name: 'melee-play-index',
    configureServer(server) {
      server.middlewares.use(rewrite);
    },
    configurePreviewServer(server) {
      server.middlewares.use(rewrite);
    },
  };
}

export default defineConfig({
  plugins: [react(), tailwindcss(), playIndex()],
  build: {
    outDir: 'dist',
    emptyOutDir: true,
    rollupOptions: {
      input: {
        main: 'index.html',
        play: 'play/index.html',
      },
    },
  },
});
