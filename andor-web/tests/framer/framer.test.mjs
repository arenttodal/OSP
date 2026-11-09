// Functional test for the Framer code components (framer/*.tsx), rendered through the stand-in
// "framer" module in real Chromium. Usage: node tests/framer/framer.test.mjs
import { createServer } from 'vite';
import { createRequire } from 'node:module';
import path from 'node:path';
const require = createRequire(import.meta.url);
const { chromium } = require('playwright');

const here = path.dirname(new URL(import.meta.url).pathname);
const server = await createServer({ configFile: path.join(here, 'vite.config.ts'), logLevel: 'error' });
await server.listen();
const browser = await chromium.launch({ args: ['--autoplay-policy=no-user-gesture-required'] });
let failed = 0;
const check = (n, ok, d = '') => { console.log(`${ok ? 'PASS' : 'FAIL'}  ${n}${d ? '  ' + d : ''}`); if (!ok) failed++; };

const page = await browser.newPage({ viewport: { width: 1280, height: 900 } });
const errors = [];
page.on('console', (m) => (m.type() === 'error' || m.type() === 'warning') && errors.push(m.text()));
page.on('pageerror', (e) => errors.push(String(e)));
await page.goto('http://localhost:4830/tests/framer/harness.html');
await page.waitForSelector('[data-c="ac-live"] section');
await page.waitForTimeout(500);

// Property controls registered for every component
const controls = await page.evaluate(() => Object.fromEntries(Object.entries(window.__framerControls).map(([k, v]) => [k, Object.keys(v)])));
for (const name of ['AudioComparison', 'SoundDeck', 'ViewSwitcher', 'DeepDive', 'AnnotatedInstrument', 'FAQ'])
  check(`${name}: property controls registered`, (controls[name] ?? []).length > 0, (controls[name] ?? []).join(', '));

// AudioComparison: pending, play, switch keeps position, exclusive with SoundDeck (shared controller)
const st = (sel) => page.$eval(`${sel} section`, (el) => el.dataset.status);
const pos = (sel) => page.$eval(`${sel} [role="slider"]`, (el) => Number(el.getAttribute('aria-valuenow')));
check('AudioComparison without files is honestly pending', (await st('[data-c="ac-pending"]')) === 'pending' && (await page.$eval('[data-c="ac-pending"] button', (b) => b.disabled)));
await page.click('[data-c="ac-live"] button[aria-label^="Play"]');
await page.waitForFunction(() => document.querySelector('[data-c="ac-live"] section')?.dataset.status === 'playing', null, { timeout: 10000 });
await page.waitForTimeout(2100);
const p1 = await pos('[data-c="ac-live"]');
await page.click('[data-c="ac-live"] [role="radio"]:nth-child(3)');
await page.waitForTimeout(1100);
const p2 = await pos('[data-c="ac-live"]');
check('AudioComparison plays and a switch keeps the playhead', p1 >= 1 && p2 >= p1, `${p1}s → ${p2}s`);
await page.click('[data-c="deck"] article[aria-label^="2 of 3"] button[aria-label^="Play"]');
await page.waitForFunction(() => document.querySelector('[data-c="deck"] section')?.dataset.status === 'playing', null, { timeout: 10000 });
check('SoundDeck playing pauses the other player (one controller)', (await st('[data-c="ac-live"]')) !== 'playing');
await page.click('[data-c="deck"] button[aria-label^="Pause"]');

// SoundDeck navigation
await page.click('[data-c="deck"] button[aria-label="Next example"]');
check('SoundDeck next brings Voice forward', /3 of 3: Voice/.test(await page.$eval('[data-c="deck"] article:has(section)', (a) => a.getAttribute('aria-label'))));

// ViewSwitcher: list + keyboard, dark pills, stacked
const shown = (sel) => page.$eval(`${sel} img[aria-hidden="false"]`, (i) => i.getAttribute('alt'));
check('ViewSwitcher starts at the given view', (await shown('[data-c="reim"]')) === 'REIMAGINED mosaic');
await page.click('[data-c="reim"] [role="tab"]:nth-child(2)');
check('ViewSwitcher click switches the picture', (await shown('[data-c="reim"]')) === 'REIMAGINED tape-frame');
await page.keyboard.press('End');
check('ViewSwitcher End key reaches the last view', (await shown('[data-c="reim"]')) === 'REIMAGINED mirage');
check('ViewSwitcher renders the intro slot', (await page.$eval('[data-c="reim"] h2', (h) => h.textContent)) === 'Five instruments');
await page.click('[data-c="synth"] [role="tab"]:nth-child(2)');
check('ViewSwitcher (dark, pills) shows the Analog card', (await shown('[data-c="synth"]')) === 'SYNTH analog');
check('ViewSwitcher (stacked) starts on three layers', (await shown('[data-c="layers"]')) === '3 layers');

// AnnotatedInstrument → overlay with DeepDive
check('AnnotatedInstrument: six markers at default positions', (await page.$$('[data-c="hero"] .ai-dot')).length === 6);
await page.click('[data-c="hero"] button[aria-label^="2."]');
await page.waitForSelector('[data-c="overlay"]');
check('Marker 2 fires its event (opens the LIFE overlay)', (await page.$eval('[data-c="overlay"] h2', (h) => h.textContent)) === 'LIFE');
check('DeepDive moves focus to Close', await page.evaluate(() => document.activeElement?.getAttribute('aria-label') === 'Close'));
await page.keyboard.press('Shift+Tab');
check('DeepDive keeps focus inside (Shift+Tab wraps)', await page.evaluate(() => !!document.activeElement?.closest('[role="dialog"]')));
await page.click('[data-c="overlay"] footer button:last-child');
check('DeepDive next shows REIMAGINED with five tabs', (await page.$$('[data-c="overlay"] [role="tab"]')).length === 5);
await page.click('[data-c="overlay"] [role="tab"]:nth-child(4)');
check('DeepDive tab shows MOSAIC', (await page.$eval('[data-c="overlay"] img[aria-hidden="false"]', (i) => i.alt)) === 'mosaic');
await page.keyboard.press('ArrowRight');
check('DeepDive arrow moves to MIRAGE', (await page.$eval('[data-c="overlay"] img[aria-hidden="false"]', (i) => i.alt)) === 'mirage');
await page.keyboard.press('Escape');
await page.waitForTimeout(100);
check('Escape calls On Close', (await page.$('[data-c="overlay"]')) === null);

// FAQ
await page.click('[data-c="faq"] summary');
check('FAQ opens an answer', await page.$eval('[data-c="faq"] details', (d) => d.open));

await page.screenshot({ path: path.join(here, '../visual/out/framer-harness.png'), fullPage: true });
check('No console errors or React warnings', errors.length === 0, errors.slice(0, 3).join(' | '));

// Phones: SoundDeck becomes a row with every card visible
const phone = await browser.newPage({ viewport: { width: 390, height: 844 }, isMobile: true });
await phone.goto('http://localhost:4830/tests/framer/harness.html');
await phone.waitForSelector('[data-c="deck"] article');
check('SoundDeck (phone): no hidden cards, row scrolls', (await phone.$$('[data-c="deck"] article[aria-hidden="true"]')).length === 0);
check('Harness (phone): no horizontal page overflow', await phone.evaluate(() => document.documentElement.scrollWidth <= innerWidth + 1));

await browser.close();
await server.close();
process.exit(failed ? 1 : 0);
