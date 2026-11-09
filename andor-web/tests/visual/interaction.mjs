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


// Gallery deep dive: markers open the modal; tabs, next/prev, Escape and focus return
page = await browser.newPage({ viewport: { width: 1440, height: 900 } });
await page.goto(url('g4.html'));
await page.waitForTimeout(2600);
await page.click('.ga-dot:nth-of-type(2)');
await page.waitForTimeout(400);
const title = () => page.$eval('dialog.fm[open] #fm-title', (el) => el.textContent).catch(() => null);
check('Deep dive: marker 2 opens LIFE', (await title()) === 'LIFE');
check('Deep dive: LIFE shows its real popover', (await page.$eval('dialog.fm .fm__media.is-current', (el) => el.dataset.asset)) === 'dd-life');
await page.click('.fm__step:last-of-type');
await page.click('.fm__step:last-of-type');
check('Deep dive: next/next reaches REIMAGINED', (await title()) === 'REIMAGINED');
check('Deep dive: REIMAGINED has five mode tabs', (await page.$$eval('.fm__tabs [role="tab"]', (t) => t.length)) === 5);
await page.click('.fm__tabs [role="tab"]:nth-child(4)');
check('Deep dive: MOSAIC tab shows the MOSAIC capture', (await page.$eval('.fm__media.is-current', (el) => el.dataset.asset)) === 'dd-reimagined-mosaic');
await page.keyboard.press('ArrowRight');
check('Deep dive: arrow key moves to MIRAGE', (await page.$eval('.fm__media.is-current', (el) => el.dataset.asset)) === 'dd-reimagined-mirage');
await page.keyboard.press('Escape');
await page.waitForTimeout(200);
check('Deep dive: Escape closes the modal', (await page.$('dialog.fm[open]')) === null);
check('Deep dive: focus returns to marker 2', await page.evaluate(() => document.activeElement?.classList.contains('ga-dot')));
await page.click('.ga-note__btn >> text=Keyboard');
await page.waitForTimeout(300);
check('Deep dive: keyboard note opens the arpeggiator', (await page.$eval('.fm__media.is-current', (el) => el.dataset.asset)) === 'dd-arp');
await page.mouse.click(10, 10);
await page.waitForTimeout(200);
check('Deep dive: backdrop click closes', (await page.$('dialog.fm[open]')) === null);
await page.close();
page = await browser.newPage({ viewport: { width: 390, height: 844 }, isMobile: true, hasTouch: true });
await page.goto(url('g4.html'));
await page.waitForTimeout(2600);
await page.tap('.ga-legend--buttons li:nth-child(3) button');
await page.waitForTimeout(400);
check('Deep dive (phone): legend opens SYNTH + IMPRINT', (await title()) === 'SYNTH + IMPRINT');
const sheet = await page.$eval('.fm__sheet', (el) => { const r = el.getBoundingClientRect(); return { w: Math.round(r.width), bottom: Math.round(r.bottom), vw: innerWidth, vh: innerHeight }; });
check('Deep dive (phone): bottom sheet, full width', sheet.w === sheet.vw && sheet.bottom === sheet.vh, JSON.stringify(sheet));
check('Deep dive (phone): no horizontal overflow', await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth));
await page.close();


// Version E: open bubbles; a click opens the full modal
page = await browser.newPage({ viewport: { width: 1440, height: 900 } });
await page.goto(url('g5.html'));
await page.waitForTimeout(3000);
check('E: six bubbles already open on the page', (await page.$$eval('.bub', (b) => b.filter((x) => getComputedStyle(x).opacity === '1').length)) === 6);
{ const b = await (await page.$('.bub:has(.bub__btn:text("LIFE")) .bub__plate')).boundingBox(); await page.mouse.click(b.x + b.width / 2, b.y + b.height / 2); } // click the image, as a user would
await page.waitForTimeout(400);
check('E: clicking the LIFE bubble opens the LIFE modal', (await title()) === 'LIFE');
await page.keyboard.press('Escape');
await page.waitForTimeout(200);
check('E: focus returns to the LIFE bubble', await page.evaluate(() => document.activeElement?.textContent?.startsWith('LIFE')));
await page.click('.ga-dot:nth-of-type(4)');
await page.waitForTimeout(400);
check('E: marker 4 opens REIMAGINED', (await title()) === 'REIMAGINED');
await page.keyboard.press('Escape');
await page.close();
page = await browser.newPage({ viewport: { width: 390, height: 844 }, isMobile: true, hasTouch: true });
await page.goto(url('g5.html'));
await page.waitForTimeout(3000);
const row = await page.$eval('.e-bubbles', (el) => ({ scroll: el.scrollWidth > el.clientWidth, overflow: getComputedStyle(el).overflowX }));
check('E (phone): bubbles form a swipeable row', row.scroll && row.overflow === 'auto', JSON.stringify(row));
check('E (phone): the page itself does not scroll sideways', await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth));
{ const b = await (await page.$('.bub:has(.bub__btn:text("Your recording")) .bub__plate')).boundingBox(); await page.touchscreen.tap(b.x + b.width / 2, b.y + b.height / 2); }
await page.waitForTimeout(400);
check('E (phone): tapping a bubble opens the bottom sheet', (await title()) === 'Your recording');
await page.close();

// Reduced motion: settled immediately
for (const p of ['h1.html', 'h2.html', 'h3.html', 'g1.html', 'g2.html', 'g3.html', 'g4.html', 'g5.html']) {
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
