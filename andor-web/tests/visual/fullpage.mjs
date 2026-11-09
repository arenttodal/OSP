// Full-page screenshots of a page at desktop and phone widths, scrolled through first so
// in-view behaviour has run. Usage: node tests/visual/fullpage.mjs osp
import { preview } from 'vite';
import { createRequire } from 'node:module';
import path from 'node:path';
const require = createRequire(import.meta.url);
const { chromium } = require('playwright');
const root = path.resolve(path.dirname(new URL(import.meta.url).pathname), '../..');
const page = process.argv[2] ?? 'osp';
const server = await preview({ root, configFile: path.join(root, 'vite.config.ts'), preview: { port: 4824, strictPort: true }, logLevel: 'error' });
const b = await chromium.launch();
for (const [w, h] of [[1440, 900], [390, 844]]) {
  const t = await b.newPage({ viewport: { width: w, height: h }, isMobile: w < 800, hasTouch: w < 800 });
  const errors = [];
  t.on('pageerror', (e) => errors.push(String(e)));
  await t.goto(`http://localhost:4824/${page}.html?clean`, { waitUntil: 'networkidle' });
  await t.waitForTimeout(2500);
  const H = await t.evaluate(() => document.documentElement.scrollHeight);
  for (let y = 0; y < H; y += h / 2) { await t.evaluate((yy) => window.scrollTo(0, yy), y); await t.waitForTimeout(120); }
  await t.evaluate(() => window.scrollTo(0, 0));
  await t.waitForTimeout(600);
  await t.screenshot({ path: path.join(root, `tests/visual/out/${page}-full-${w}.png`), fullPage: true });
  console.log(page, w, 'height', H, 'errors', errors.length, errors.join(' | '));
  await t.close();
}
await b.close(); server.httpServer.close();
