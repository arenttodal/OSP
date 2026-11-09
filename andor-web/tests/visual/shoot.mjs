// Visual + functional QA for the hero concepts. Serves dist/, opens each page at the plan's seven
// widths, waits for the entrance to settle, and records: screenshot, horizontal overflow, console
// errors, cumulative layout shift, and whether the headline, primary CTA and product are inside
// the first viewport. Usage: node tests/visual/shoot.mjs [h1 h2 h3 index] [--scale 2]
import http from 'node:http';
import fs from 'node:fs';
import path from 'node:path';
import { createRequire } from 'node:module';
const require = createRequire(import.meta.url);
const { chromium } = require('playwright');

const root = path.resolve(path.dirname(new URL(import.meta.url).pathname), '../..');
const dist = path.join(root, 'dist');
const out = path.join(root, 'tests/visual/out');
fs.mkdirSync(out, { recursive: true });
const args = process.argv.slice(2);
const scale = args.includes('--scale') ? Number(args[args.indexOf('--scale') + 1]) : 1;
const pages = args.filter((a) => /^([hg]\d|index|osp)$/.test(a));
const PAGES = pages.length ? pages : ['h1', 'h2', 'h3'];
const WIDTHS = [[1440, 900], [1280, 800], [1024, 768], [768, 1024], [390, 844], [375, 667], [320, 568]];
const TYPES = { '.html': 'text/html', '.js': 'text/javascript', '.css': 'text/css', '.webp': 'image/webp', '.avif': 'image/avif', '.png': 'image/png', '.woff2': 'font/woff2', '.woff': 'font/woff', '.json': 'application/json' };

const server = http.createServer((req, res) => {
  const p = path.join(dist, decodeURIComponent(req.url.split('?')[0]).replace(/\/$/, '/index.html'));
  if (!p.startsWith(dist) || !fs.existsSync(p)) { res.writeHead(404); return res.end(); }
  res.writeHead(200, { 'content-type': TYPES[path.extname(p)] ?? 'application/octet-stream' });
  fs.createReadStream(p).pipe(res);
});
await new Promise((r) => server.listen(4818, r));

const browser = await chromium.launch();
const report = [];
for (const page of PAGES) {
  for (const [w, h] of WIDTHS) {
    const ctx = await browser.newContext({ viewport: { width: w, height: h }, deviceScaleFactor: scale, hasTouch: w < 800, isMobile: w < 800 });
    const tab = await ctx.newPage();
    const errors = [];
    tab.on('console', (m) => m.type() === 'error' && errors.push(m.text()));
    tab.on('pageerror', (e) => errors.push(String(e)));
    await tab.addInitScript(() => {
      window.__cls = 0;
      new PerformanceObserver((l) => { for (const e of l.getEntries()) if (!e.hadRecentInput) window.__cls += e.value; }).observe({ type: 'layout-shift', buffered: true });
    });
    await tab.goto(`http://localhost:4818/${page}.html?clean`, { waitUntil: 'networkidle' });
    if (page === 'h2' && (w === 1440 || w === 390)) {
      // The opening frame of the matched cut: the recording alone, before the frame pulls back.
      await tab.waitForTimeout(1250);
      await tab.screenshot({ path: path.join(out, `${page}-${w}x${h}-sound.png`) });
      await tab.waitForTimeout(3000);
    } else await tab.waitForTimeout(page === 'h2' ? 4200 : 3000);
    const m = await tab.evaluate(() => {
      const vis = (el) => { if (!el) return 0; const r = el.getBoundingClientRect(); const ih = Math.max(0, Math.min(r.bottom, innerHeight) - Math.max(r.top, 0)); const iw = Math.max(0, Math.min(r.right, innerWidth) - Math.max(r.left, 0)); return r.width * r.height ? +((ih * iw) / (r.width * r.height)).toFixed(2) : 0; };
      const prod = [...document.querySelectorAll('.h1-object--active, .h2-object, .h3-object, .g-object')][0];
      const cta = document.querySelector('.btn-pair');
      const h1 = document.querySelector('h1');
      const pr = prod?.getBoundingClientRect();
      return {
        overflowX: document.documentElement.scrollWidth > innerWidth + 1,
        cls: +window.__cls.toFixed(4),
        headlineInView: vis(h1),
        ctaInView: vis(cta),
        productInView: vis(prod),
        productPx: pr ? `${Math.round(pr.width)}x${Math.round(pr.height)}` : null,
        h1Size: h1 ? getComputedStyle(h1).fontSize : null,
        missingMedia: document.querySelectorAll('.pm--error').length,
      };
    });
    const file = `${page}-${w}x${h}.png`;
    await tab.screenshot({ path: path.join(out, file) });
    report.push({ page, viewport: `${w}x${h}`, ...m, errors });
    await ctx.close();
  }
}
await browser.close();
server.close();
fs.writeFileSync(path.join(out, 'report.json'), JSON.stringify(report, null, 2));
for (const r of report) console.log(`${r.page} ${r.viewport.padEnd(9)} overflow:${r.overflowX ? 'YES' : 'no '} cls:${r.cls} h1:${r.headlineInView} cta:${r.ctaInView} product:${r.productInView} (${r.productPx}) font:${r.h1Size} missing:${r.missingMedia} errors:${r.errors.length}`);
