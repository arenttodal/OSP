// QA for the support and legal pages: each page at the seven widths (overflow, console errors,
// layout shift), every link on every page (target file exists, #anchor exists), the landing
// page footer (no "soon" left for these pages), and full-page shots at 1440 and 390.
// Usage: node tests/visual/pages.mjs   (after `vite build`)
import http from 'node:http';
import fs from 'node:fs';
import path from 'node:path';
import { createRequire } from 'node:module';
const require = createRequire(import.meta.url);
const { chromium } = require('playwright');

const root = path.resolve(path.dirname(new URL(import.meta.url).pathname), '../..');
const dist = path.join(root, 'dist');
const out = path.join(root, 'tests/visual/out/pages');
fs.mkdirSync(out, { recursive: true });
const PAGES = ['installation', 'documentation', 'contact', 'privacy', 'terms', 'licence'];
const WIDTHS = [[1440, 900], [1280, 800], [1024, 768], [768, 1024], [390, 844], [375, 667], [320, 568]];
const TYPES = { '.html': 'text/html', '.js': 'text/javascript', '.css': 'text/css', '.webp': 'image/webp', '.avif': 'image/avif', '.png': 'image/png', '.woff2': 'font/woff2', '.woff': 'font/woff', '.json': 'application/json' };
const server = http.createServer((req, res) => {
  const p = path.join(dist, decodeURIComponent(req.url.split('?')[0]).replace(/\/$/, '/index.html'));
  if (!p.startsWith(dist) || !fs.existsSync(p)) { res.writeHead(404); return res.end(); }
  res.writeHead(200, { 'content-type': TYPES[path.extname(p)] ?? 'application/octet-stream' });
  fs.createReadStream(p).pipe(res);
});
await new Promise((r) => server.listen(4819, r));
const base = 'http://localhost:4819/';
const browser = await chromium.launch();
let failures = 0;
const fail = (msg) => { failures++; console.log(`FAIL ${msg}`); };

// Anchors present on each page, for checking `page.html#id` links.
const anchors = {};
const idsOf = async (page) => {
  if (anchors[page]) return anchors[page];
  const ctx = await browser.newContext({ viewport: { width: 1280, height: 800 } });
  const tab = await ctx.newPage();
  await tab.goto(`${base}${page}?clean`, { waitUntil: 'networkidle' });
  anchors[page] = new Set(await tab.evaluate(() => [...document.querySelectorAll('[id]')].map((e) => e.id)));
  await ctx.close();
  return anchors[page];
};

for (const page of PAGES) {
  for (const [w, h] of WIDTHS) {
    const ctx = await browser.newContext({ viewport: { width: w, height: h }, hasTouch: w < 800, isMobile: w < 800 });
    const tab = await ctx.newPage();
    const errors = [];
    tab.on('console', (m) => m.type() === 'error' && errors.push(m.text()));
    tab.on('pageerror', (e) => errors.push(String(e)));
    await tab.addInitScript(() => {
      window.__cls = 0;
      new PerformanceObserver((l) => { for (const e of l.getEntries()) if (!e.hadRecentInput) window.__cls += e.value; }).observe({ type: 'layout-shift', buffered: true });
    });
    await tab.goto(`${base}${page}.html?clean`, { waitUntil: 'networkidle' });
    await tab.waitForTimeout(400);
    const m = await tab.evaluate(() => ({
      // scrollWidth misses content cut off by overflow-x: clip, so also check the reading column.
      overflow: document.documentElement.scrollWidth > innerWidth + 1 || [...document.querySelectorAll('.doc-body > *, .doc-hero > *')].some((e) => e.getBoundingClientRect().right > document.documentElement.clientWidth + 0.5),
      cls: +window.__cls.toFixed(4),
      h1: document.querySelectorAll('h1').length,
      title: document.title,
      current: document.querySelector('.doc-pages [aria-current="page"]')?.textContent,
      tocOpen: document.querySelector('.doc-toc')?.open ?? null,
      links: [...document.querySelectorAll('a[href]')].map((a) => a.getAttribute('href')),
      unrendered: /\[\[|\]\]|\*\*/.test(document.querySelector('.doc-body').textContent),
    }));
    const tag = `${page} ${w}x${h}`;
    if (m.overflow) fail(`${tag} horizontal overflow`);
    if (m.cls > 0.01) fail(`${tag} CLS ${m.cls}`);
    if (m.h1 !== 1) fail(`${tag} has ${m.h1} h1`);
    if (errors.length) fail(`${tag} console: ${errors.join(' | ')}`);
    if (m.unrendered) fail(`${tag} raw Markdown or placeholder brackets in the text`);
    if (!m.current) fail(`${tag} no current page in the side navigation`);
    if (w === 1440) {
      console.log(`${page.padEnd(13)} "${m.title}" current:${m.current} toc:${m.tocOpen} links:${m.links.length}`);
      for (const href of new Set(m.links)) {
        if (/^(https?:|mailto:)/.test(href)) continue;
        const [file, hash] = href.split('#');
        const target = file || `${page}.html`;
        const real = target === './' ? 'index.html' : target;
        if (!fs.existsSync(path.join(dist, real))) { fail(`${page}: link ${href} → missing ${real}`); continue; }
        if (hash && !(await idsOf(real)).has(hash)) fail(`${page}: link ${href} → no #${hash}`);
      }
    }
    if (w === 390 && m.tocOpen) fail(`${tag} section list should start folded on phones`);
    if (w === 1440 || w === 390) await tab.screenshot({ path: path.join(out, `${page}-${w}.png`), fullPage: true });
    await ctx.close();
  }
}

// The landing page footer now links every support and legal page.
{
  const ctx = await browser.newContext({ viewport: { width: 1280, height: 800 } });
  const tab = await ctx.newPage();
  await tab.goto(`${base}osp.html?clean`, { waitUntil: 'networkidle' });
  const foot = await tab.evaluate(() => ({ links: [...document.querySelectorAll('.foot a')].map((a) => a.getAttribute('href')), soon: [...document.querySelectorAll('.foot em')].map((e) => e.parentElement.textContent) }));
  for (const p of PAGES) if (!foot.links.includes(`${p}.html`)) fail(`osp footer lacks ${p}.html`);
  if (foot.soon.some((t) => !/ANALOG/.test(t))) fail(`osp footer still says soon: ${foot.soon}`);
  console.log(`osp footer: ${foot.links.length} links, soon: ${foot.soon.join(', ')}`);
  // Following a footer link lands on the page.
  await tab.click('.foot a[href="privacy.html"]');
  await tab.waitForLoadState('networkidle');
  if (!/privacy\.html/.test(tab.url())) fail('footer link did not navigate');
  await ctx.close();
}
await browser.close();
server.close();
console.log(failures ? `\n${failures} failure(s)` : '\nall page checks passed');
process.exit(failures ? 1 : 0);
