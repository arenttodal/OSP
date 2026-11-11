// Composites advertising type over a product render (the render itself is never altered).
// Type is set in HTML with the site's own fonts and captured with Playwright, so the same render
// can carry any copy without re-rendering the 3D scene.
//
// Usage (from andor-web/):
//   node product-renders/blender/scripts/compose_ad.mjs <render.png> <out.png> [--guides]
//        [--headline "One sound.|An entire instrument."] [--size 1080x1920]
// --guides draws the Instagram Stories safe zones (top 250 px, bottom 340 px at 1080 x 1920) for review.
import fs from 'node:fs';
import path from 'node:path';
import { createRequire } from 'node:module';
const require = createRequire(import.meta.url);
const { chromium } = require('playwright');

const args = process.argv.slice(2);
const [input, output] = args;
if (!input || !output) { console.error('usage: compose_ad.mjs <render.png> <out.png> [--guides]'); process.exit(1); }
const opt = (name, dflt) => (args.includes(name) ? args[args.indexOf(name) + 1] : dflt);
const guides = args.includes('--guides');
const headline = opt('--headline', 'One sound.|An entire instrument.').split('|');
const [w, h] = opt('--size', '1080x1920').split('x').map(Number);

const root = path.resolve(path.dirname(new URL(import.meta.url).pathname), '../../..');
const font = (p) => 'data:font/woff2;base64,' + fs.readFileSync(path.join(root, 'node_modules', p)).toString('base64');
const img = 'data:image/png;base64,' + fs.readFileSync(path.resolve(input)).toString('base64');

const html = `<!doctype html><html><head><meta charset="utf-8"><style>
@font-face { font-family: 'Inter Tight'; src: url(${font('@fontsource-variable/inter-tight/files/inter-tight-latin-wght-normal.woff2')}) format('woff2'); font-weight: 100 900; }
@font-face { font-family: 'Geist Mono'; src: url(${font('@fontsource/geist-mono/files/geist-mono-latin-500-normal.woff2')}) format('woff2'); font-weight: 500; }
html, body { margin: 0; width: ${w}px; height: ${h}px; overflow: hidden; background: #0e0c0a; }
.bg { position: absolute; inset: 0; background: url(${img}) center / cover no-repeat; }
.type { position: absolute; left: 0; right: 0; top: ${Math.round(h * 0.135)}px; text-align: center; color: #f4f1e9; font-family: 'Inter Tight', sans-serif; }
.brand { font-family: 'Geist Mono', monospace; font-size: ${Math.round(w * 0.026)}px; letter-spacing: 0.32em; text-transform: uppercase; color: rgba(244, 241, 233, 0.78); margin: 0 0 ${Math.round(w * 0.04)}px; }
.brand b { color: #f4f1e9; font-weight: 500; }
h1 { margin: 0; font-weight: 520; font-size: ${Math.round(w * 0.098)}px; line-height: 1.02; letter-spacing: -0.035em; }
h1 span { display: block; }
h1 span + span { color: rgba(244, 241, 233, 0.62); }
.guide { position: absolute; left: 0; right: 0; background: rgba(255, 60, 60, 0.18); border: 1px dashed rgba(255, 90, 90, 0.8); color: #ffb3b3; font: 500 18px 'Geist Mono', monospace; display: flex; align-items: center; justify-content: center; }
</style></head><body>
<div class="bg"></div>
<div class="type"><p class="brand"><b>ANDOR</b> / OSP</p><h1>${headline.map((l) => `<span>${l}</span>`).join('')}</h1></div>
${guides ? `<div class="guide" style="top:0;height:${Math.round(h * 0.13)}px">Stories UI · keep clear</div><div class="guide" style="bottom:0;height:${Math.round(h * 0.177)}px">Stories UI · keep clear</div>` : ''}
</body></html>`;

const browser = await chromium.launch();
const page = await browser.newPage({ viewport: { width: w, height: h }, deviceScaleFactor: 1 });
await page.setContent(html, { waitUntil: 'load' });
await page.evaluate(() => document.fonts.ready);
await page.screenshot({ path: output });
await browser.close();
console.log('wrote', output);
