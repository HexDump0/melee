import { defineConfig } from 'vite';
import react from '@vitejs/plugin-react';
import tailwindcss from '@tailwindcss/vite';

// The client build emits dist/; `npm run build` then renders the same App to
// static HTML and injects it (see prerender.mjs), so the shipped page is real
// markup that works with JS off. React hydrates it for GSAP and the scroll-spy.
export default defineConfig({
  plugins: [react(), tailwindcss()],
  build: { outDir: 'dist', emptyOutDir: true },
});
