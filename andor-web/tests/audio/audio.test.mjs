// Functional test for AudioComparison in real Chromium, using generated tone fixtures.
// Checks: lazy load on first play, playing state, position advances, variant switch keeps the
// position (no restart), keyboard seek, exclusivity between players, error state for a broken
// source, pending state on the hero demo. Usage: node tests/audio/audio.test.mjs
import { createServer } from 'vite';
import { createRequire } from 'node:module';
import path from 'node:path';
const require = createRequire(import.meta.url);
const { chromium } = require('playwright');

const root = path.resolve(path.dirname(new URL(import.meta.url).pathname), '../..');
const server = await createServer({ root, configFile: path.join(root, 'vite.config.ts'), server: { port: 4819, strictPort: true }, logLevel: 'error' });
await server.listen();
const browser = await chromium.launch({ args: ['--autoplay-policy=no-user-gesture-required'] });
const page = await browser.newPage();
const errors = [];
page.on('pageerror', (e) => errors.push(String(e)));
let failed = 0;
const check = (name, ok, detail = '') => { console.log(`${ok ? 'PASS' : 'FAIL'}  ${name}${detail ? '  ' + detail : ''}`); if (!ok) failed++; };

await page.goto('http://localhost:4819/tests/audio/harness.html');
await page.waitForSelector('[data-player="a"] .ac__play');
const pos = (p) => page.$eval(`[data-player="${p}"] .ac__track`, (el) => Number(el.getAttribute('aria-valuenow')));
const status = (p) => page.$eval(`[data-player="${p}"] .ac`, (el) => el.dataset.status);
const requests = [];
page.on('request', (r) => r.url().includes('/fixtures/') && requests.push(r.url()));

check('no audio requested before play', requests.length === 0);
await page.click('[data-player="a"] .ac__play');
await page.waitForFunction(() => document.querySelector('[data-player="a"] .ac')?.dataset.status === 'playing', null, { timeout: 10000 });
check('player A playing after first press', (await status('a')) === 'playing');
check('three variants fetched on demand', new Set(requests.filter((u) => !u.includes('missing'))).size === 3, `${requests.length} requests`);
await page.waitForTimeout(2200);
const p1 = await pos('a');
check('position advances', p1 >= 1, `t=${p1}s`);
await page.click('[data-player="a"] [role="radio"]:nth-child(3)');
await page.waitForTimeout(1100);
const p2 = await pos('a');
check('variant switch keeps the playhead (no restart)', p2 >= p1, `before=${p1}s after=${p2}s`);
check('selected variant reported', (await page.$eval('[data-player="a"] [role="radio"][aria-checked="true"]', (el) => el.textContent)) === 'Reimagined');
await page.focus('[data-player="a"] .ac__track');
await page.keyboard.press('Home');
await page.waitForTimeout(250);
check('keyboard Home seeks to start', (await pos('a')) <= 1);
await page.keyboard.press('ArrowRight');
await page.waitForTimeout(250);
const p3 = await pos('a');
check('keyboard seek forward (+5 s) while playing', p3 >= 5 && (await status('a')) === 'playing', `t=${p3}s`);
await page.click('[data-player="b"] .ac__play');
await page.waitForFunction(() => document.querySelector('[data-player="b"] .ac')?.dataset.status === 'playing', null, { timeout: 10000 });
check('starting B pauses A (exclusive playback)', (await status('a')) !== 'playing', `A=${await status('a')}`);
await page.click('[data-player="b"] .ac__play');
check('pause B', (await status('b')) !== 'playing');
await page.click('[data-player="broken"] .ac__play');
await page.waitForFunction(() => document.querySelector('[data-player="broken"] .ac')?.dataset.status === 'error', null, { timeout: 10000 }).catch(() => {});
check('broken source shows error state', (await status('broken')) === 'error');
check('no page errors', errors.length === 0, errors.join(' | '));

await page.goto('http://localhost:4819/h1.html');
await page.waitForSelector('.ac');
check('hero player is honestly pending (no audio yet)', (await page.$eval('.ac', (el) => el.dataset.status)) === 'pending' && (await page.$eval('.ac__play', (b) => b.disabled)));

await browser.close();
await server.close();
process.exit(failed ? 1 : 0);
