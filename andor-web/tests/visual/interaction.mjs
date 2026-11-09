// Interaction checks on the built prototype (dist/ via vite preview): H1 state switcher (pointer +
// keyboard), mobile menu (open, Escape, focus return), primary CTA scrolls to #explore, and
// reduced motion shows the settled state immediately. Usage: node tests/visual/interaction.mjs
import { preview } from 'vite';
import { createRequire } from 'node:module';
import path from 'node:path';
const require = createRequire(import.meta.url);
const { chromium } = require('playwright');
const root = path.resolve(path.dirname(new URL(import.meta.url).pathname), '../..');
const server = await preview({ root, configFile: path.join(root, 'vite.config.ts'), preview: { port: 4820, strictPort: true }, logLevel: 'error' });
const browser = await chromium.launch();
let failed = 0;
const check = (n, ok, d = '') => { console.log(`${ok ? 'PASS' : 'FAIL'}  ${n}${d ? '  ' + d : ''}`); if (!ok) failed++; };
const url = (p) => `http://localhost:4820/${p}?clean`;

// H1 states
let page = await browser.newPage({ viewport: { width: 1440, height: 900 } });
await page.goto(url('h1.html'));
await page.waitForTimeout(1500);
const active = () => page.$eval('.h1-object--active', (el) => el.getAttribute('data-asset'));
check('H1 starts on one layer', (await active()) === 'object-1l');
await page.click('.h1-state:nth-child(3)');
await page.waitForTimeout(600);
check('H1 click IMPRINT shows the IMPRINT render', (await active()) === 'object-imprint');
await page.focus('.h1-state[aria-checked="true"]');
await page.keyboard.press('ArrowDown');
check('H1 arrow key moves to next state (wraps)', (await active()) === 'object-1l');
const visible = await page.$$eval('.h1-object', (els) => els.filter((e) => getComputedStyle(e).opacity !== '0').length);
await page.waitForTimeout(700);
check('H1 only one state visible after crossfade', (await page.$$eval('.h1-object', (els) => els.filter((e) => getComputedStyle(e).opacity === '1').length)) === 1, `mid=${visible}`);
await page.click('.h1 .btn-pair');
await page.waitForTimeout(800);
check('Primary CTA reaches #explore', await page.evaluate(() => { const r = document.getElementById('explore').getBoundingClientRect(); return r.top < innerHeight && r.bottom > 0; }));
await page.close();

// Mobile menu
page = await browser.newPage({ viewport: { width: 390, height: 844 }, isMobile: true, hasTouch: true });
await page.goto(url('h2.html'));
await page.waitForTimeout(800);
await page.tap('.menu-button');
check('Menu opens as a modal dialog', (await page.$('[role="dialog"][aria-modal="true"]')) !== null);
check('Focus moves into the menu', await page.evaluate(() => document.activeElement?.getAttribute('aria-label') === 'Close menu'));
await page.keyboard.press('Escape');
await page.waitForTimeout(100);
check('Escape closes the menu', (await page.$('[role="dialog"]')) === null);
check('Focus returns to the menu button', await page.evaluate(() => document.activeElement?.classList.contains('menu-button')));
await page.close();

// Reduced motion: settled immediately
for (const p of ['h1.html', 'h2.html', 'h3.html', 'g1.html', 'g2.html', 'g3.html']) {
  const ctx = await browser.newContext({ viewport: { width: 1440, height: 900 }, reducedMotion: 'reduce' });
  const t = await ctx.newPage();
  await t.goto(url(p));
  await t.waitForTimeout(150);
  const o = await t.evaluate(() => { const el = document.querySelector('.h1-object--active, .h2-object, .h3-object, .g-object'); const s = getComputedStyle(el); return { o: s.opacity, tr: s.transform }; });
  check(`Reduced motion ${p}: product settled at once`, o.o === '1' && (o.tr === 'none' || o.tr === 'matrix(1, 0, 0, 1, 0, 0)'), JSON.stringify(o));
  await ctx.close();
}
await browser.close();
server.httpServer.close();
process.exit(failed ? 1 : 0);
