// Screenshots of the deep-dive modals (desktop and phone) for review and the concept board.
import { preview } from 'vite';
import { createRequire } from 'node:module';
import path from 'node:path';
const require = createRequire(import.meta.url);
const { chromium } = require('playwright');
const root = path.resolve(path.dirname(new URL(import.meta.url).pathname), '../..');
const out = path.join(root, 'tests/visual/out');
const server = await preview({ root, configFile: path.join(root, 'vite.config.ts'), preview: { port: 4822, strictPort: true }, logLevel: 'error' });
const browser = await chromium.launch();
const desk = await browser.newPage({ viewport: { width: 1440, height: 900 } });
await desk.goto('http://localhost:4822/g4.html?clean');
await desk.waitForTimeout(2600);
for (const [n, tab, name] of [[2, 0, 'life'], [4, 4, 'reimagined-mosaic'], [3, 2, 'synth'], [6, 0, 'arp'], [5, 0, 'space'], [1, 1, 'recording']]) {
  await desk.click(`.ga-dot:nth-of-type(${n})`);
  await desk.waitForTimeout(450);
  if (tab) { await desk.click(`.fm__tabs [role="tab"]:nth-child(${tab})`); await desk.waitForTimeout(600); }
  await desk.screenshot({ path: path.join(out, `g4-modal-${name}-1440.png`) });
  await desk.keyboard.press('Escape');
  await desk.waitForTimeout(250);
}
const phone = await browser.newPage({ viewport: { width: 390, height: 844 }, isMobile: true, hasTouch: true, deviceScaleFactor: 1 });
await phone.goto('http://localhost:4822/g4.html?clean');
await phone.waitForTimeout(2600);
await phone.tap('.ga-legend--buttons li:nth-child(4) button');
await phone.waitForTimeout(500);
await phone.screenshot({ path: path.join(out, 'g4-modal-reimagined-390.png') });
await browser.close();
server.httpServer.close();
