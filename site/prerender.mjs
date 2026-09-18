/* Turn the SPA shell into a real HTML page.
 *
 * `npm run build` runs, in order:
 *   1. vite build                   -> dist/           (client bundle + css)
 *   2. vite build --ssr             -> .ssr/           (the same App, for node)
 *   3. node prerender.mjs           -> rewrites dist/index.html
 *
 * Step 3 renders the App to a string and drops it into `<div id="root">`, so
 * the shipped page is full markup: crawlers and JS-off visitors get the whole
 * thing, and the native <details> FAQ still works. React then hydrates it.
 */

import { readFile, writeFile, rm } from 'node:fs/promises';

const SHELL = 'dist/index.html';
const PLACEHOLDER = '<div id="root"></div>';

const { render } = await import('./.ssr/entry-server.js');

const shell = await readFile(SHELL, 'utf8');
if (!shell.includes(PLACEHOLDER)) {
  throw new Error(`prerender: ${SHELL} no longer contains ${PLACEHOLDER}`);
}

const html = shell.replace(PLACEHOLDER, `<div id="root">${render()}</div>`);
await writeFile(SHELL, html);
await rm('.ssr', { recursive: true, force: true });

console.log(`prerendered ${SHELL} (${(html.length / 1024).toFixed(1)} KB)`);
