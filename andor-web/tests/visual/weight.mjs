import { preview } from 'vite';
import { createRequire } from 'node:module';
const require = createRequire(import.meta.url);
const { chromium } = require('playwright');
import path from 'node:path';
const root = path.resolve(path.dirname(new URL(import.meta.url).pathname), '../..');
const server = await preview({ root, configFile: root + '/vite.config.ts', preview: { port: 4821 }, logLevel: 'error' });
const b = await chromium.launch();
for (const p of ['h1', 'h2', 'h3']) for (const [w, h] of [[1440, 900], [390, 844]]) {
  const ctx = await b.newContext({ viewport: { width: w, height: h }, deviceScaleFactor: w < 800 ? 3 : 2 });
  const t = await ctx.newPage();
  let bytes = 0, img = 0;
  t.on('response', async (r) => { try { const n = (await r.body()).length; bytes += n; if (/\.(avif|webp)$/.test(r.url())) img += n; } catch {} });
  await t.addInitScript(() => { window.__lcp = 0; new PerformanceObserver((l) => { for (const e of l.getEntries()) window.__lcp = e.startTime; }).observe({ type: 'largest-contentful-paint', buffered: true }); });
  await t.goto(`http://localhost:4821/${p}.html?clean`, { waitUntil: 'networkidle' });
  await t.waitForTimeout(500);
  const lcp = await t.evaluate(() => Math.round(window.__lcp));
  console.log(`${p} ${w}px @${w < 800 ? 3 : 2}x  total ${(bytes / 1024).toFixed(0)} KB  images ${(img / 1024).toFixed(0)} KB  LCP(local) ${lcp} ms`);
  await ctx.close();
}
await b.close(); server.httpServer.close();
