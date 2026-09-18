import { defineConfig } from "vite";
import react from "@vitejs/plugin-react";
import tailwind from "@tailwindcss/vite";
import { fileURLToPath } from "node:url";

/* The launcher imports the site's design tokens directly (src/styles.css),
   so the two cannot drift. That file lives outside this project root, which
   Vite blocks by default -- hence the explicit allow. */
const repoRoot = fileURLToPath(new URL("..", import.meta.url));

export default defineConfig({
  plugins: [react(), tailwind()],
  clearScreen: false,
  server: {
    port: 5183,
    strictPort: true,
    fs: { allow: [repoRoot] },
  },
  build: { outDir: "dist", emptyOutDir: true, target: "esnext" },
});
