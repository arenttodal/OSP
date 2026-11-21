// Browser tests of the built preview (astro preview): search, step URLs, reduced motion,
// phone width and basic accessibility. Uses the Playwright copy installed on this machine.
import { spawn } from 'node:child_process';
import { createRequire } from 'node:module';
const require = createRequire(import.meta.url);
let playwright;
try { playwright = require('playwright'); } catch { playwright = require(require.resolve('playwright', { paths: ['/usr/lib/node_modules', '/opt/node22/lib/node_modules', process.cwd()] })); }
const { chromium } = playwright;

const BASE = (process.env.LEARN_BASE ?? '/').replace(/\/?$/, '/');
const PORT = 4329, ROOT = `http://localhost:${PORT}${BASE}`;
const server = spawn('npx', ['astro', 'preview', '--port', String(PORT)], { stdio: 'pipe' });
await new Promise((ok, fail) => {
  const t = setTimeout(() => fail(new Error('preview did not start')), 30000);
  server.stdout.on('data', (d) => { if (String(d).includes(String(PORT))) { clearTimeout(t); ok(); } });
});
const results = [];
const check = (name, ok, detail = '') => { results.push({ name, ok, detail }); console.log(`${ok ? 'ok  ' : 'FAIL'} ${name}${detail ? ' — ' + detail : ''}`); };
const browser = await chromium.launch();
try {
  const page = await browser.newPage();
  // 1. search: each query's first three results include the actionable guide or step
  const queries = {
    'record': 'make-an-instrument/record-input/', 'live input': 'make-an-instrument/record-input/', 'grab last': 'make-an-instrument/grab-last/',
    'phrase transformer': 'make-an-instrument/phrase-map-notes/', 'map notes': 'make-an-instrument/phrase-map-notes/', 'strum': 'arpeggiator/strum/',
    'satellite': 'arpeggiator/satellite/', 'imprint': 'synth-imprint/imprint/', 'key dna': 'granular-key-dna/key-dna/', 'pedal loop': 'play-and-layer/pedal-loop/',
    'ping pong': 'play-and-layer/pedal-loop/', 'sustain pedal': 'play-and-layer/pedal-loop/', 'sub bass': 'arpeggiator/sub/', 'reverb': 'shape-and-finish/effects/',
  };
  for (const [q, want] of Object.entries(queries)) {
    await page.goto(`${ROOT}osp/search/?q=${encodeURIComponent(q)}`);
    await page.waitForSelector('[data-results] li a', { timeout: 8000 }).catch(() => {});
    const urls = await page.$$eval('[data-results] li a', (as) => as.slice(0, 3).map((a) => a.getAttribute('href')));
    check(`search "${q}"`, urls.some((u) => u && u.includes(want)), urls.join(' | ') || 'no results');
  }
  // 2. a step's own URL opens on that step; a fragment does the same on the guide page
  await page.goto(`${ROOT}osp/make-an-instrument/grab-last/grab-last/`);
  check('step URL selects its step', (await page.getAttribute('li[data-step="grab-last"]', 'aria-current')) === 'step');
  await page.goto(`${ROOT}osp/arpeggiator/strum/#direction`);
  await page.waitForTimeout(800);
  check('fragment selects its step', (await page.getAttribute('li[data-step="direction"]', 'aria-current')) === 'step');
  // 3. choosing a step plays its own segment
  await page.goto(`${ROOT}osp/arpeggiator/strum/`);
  await page.click('li[data-step="spread"] a');
  await page.waitForTimeout(1200);
  const seg = await page.$eval('li[data-step="spread"]', (li) => [parseFloat(li.dataset.start), parseFloat(li.dataset.end)]);
  const t = await page.$eval('video', (v) => v.currentTime);
  check('a step plays its own segment', t >= seg[0] - 0.05 && t <= seg[1] + 0.05, `t=${t.toFixed(2)} in [${seg[0]}, ${seg[1]}]`);
  // 4. reduced motion: nothing plays by itself
  const rm = await browser.newPage({ reducedMotion: 'reduce' });
  await rm.goto(`${ROOT}osp/make-an-instrument/record-input/`);
  await rm.waitForTimeout(1500);
  check('reduced motion: no autoplay', await rm.$eval('video', (v) => v.paused));
  await rm.close();
  // 5. phone width: no horizontal page scroll
  const phone = await browser.newPage({ viewport: { width: 390, height: 844 } });
  for (const p of ['osp/', 'osp/arpeggiator/per-source/', 'osp/reference/satellite/', 'osp/search/']) {
    await phone.goto(ROOT + p);
    const w = await phone.evaluate(() => document.documentElement.scrollWidth);
    check(`390 px: ${p} fits`, w <= 390, `scrollWidth ${w}`);
  }
  await phone.close();
  // 6. desktop: a guide fits one window (no page scroll), all its steps visible; the active step follows the clip
  for (const [w, h] of [[1440, 900], [1280, 800]]) {
    const desk = await browser.newPage({ viewport: { width: w, height: h } });
    for (const p of ['osp/arpeggiator/strum/', 'osp/play-and-layer/midi-learn/', 'osp/make-an-instrument/record-input/']) {
      await desk.goto(ROOT + p);
      const m = await desk.evaluate(() => ({
        scroll: document.documentElement.scrollHeight,
        stepsOut: [...document.querySelectorAll('li[data-step]')].filter((li) => li.getBoundingClientRect().bottom > innerHeight).length,
        video: document.querySelector('video').getBoundingClientRect().bottom <= innerHeight,
      }));
      check(`${w}×${h}: ${p} fits one window`, m.scroll <= h && m.stepsOut === 0 && m.video, JSON.stringify(m));
    }
    await desk.close();
  }
  {
    const desk = await browser.newPage({ viewport: { width: 1440, height: 900 } });
    await desk.goto(`${ROOT}osp/arpeggiator/strum/`);
    await desk.waitForTimeout(5000);
    const cur = await desk.$eval('li[aria-current="step"]', (li) => li.dataset.step).catch(() => '');
    const t = await desk.$eval('video', (v) => v.currentTime);
    check('the active step follows the clip', cur !== 'choose-chord' && t > 3.9, `t=${t.toFixed(2)}, step=${cur}`);
    await desk.close();
  }
  // 7. sound: clips start muted; the sound button unmutes and is remembered on the next guide
  {
    const desk = await browser.newPage({ viewport: { width: 1440, height: 900 } });
    await desk.goto(`${ROOT}osp/make-an-instrument/load-and-play/`);
    await desk.waitForTimeout(600);
    const before = await desk.$eval('video', (v) => v.muted);
    await desk.click('[data-sound]');
    const after = await desk.$eval('video', (v) => v.muted);
    const pressed = await desk.getAttribute('[data-sound]', 'aria-pressed');
    await desk.goto(`${ROOT}osp/play-and-layer/pedal-loop/`);
    await desk.waitForTimeout(600);
    const remembered = await desk.getAttribute('[data-sound]', 'aria-pressed');
    check('sound: starts muted and the button unmutes (the next page tries the remembered choice)', before === true && after === false && pressed === 'true' && remembered !== null,
      JSON.stringify({ before, after, pressed, remembered }));
    await desk.close();
  }
  // 8. basic accessibility on every kind of page
  for (const p of ['osp/', 'osp/play-and-layer/', 'osp/play-and-layer/pedal-loop/', 'osp/play-and-layer/pedal-loop/pedal-down/', 'osp/reference/pedal-loop/', 'osp/search/']) {
    await page.goto(ROOT + p);
    const a = await page.evaluate(() => ({
      lang: document.documentElement.lang,
      h1: document.querySelectorAll('h1').length,
      imgNoAlt: [...document.images].filter((i) => !i.hasAttribute('alt')).length,
      videoNoLabel: [...document.querySelectorAll('video')].filter((v) => !v.getAttribute('aria-label')).length,
      emptyLinks: [...document.querySelectorAll('a')].filter((x) => !x.textContent.trim() && !x.getAttribute('aria-label') && !x.querySelector('img[alt]:not([alt=""])') && !x.querySelector('h3')).length,
      unlabelledInputs: [...document.querySelectorAll('input')].filter((i) => !document.querySelector(`label[for="${i.id}"]`)).length,
    }));
    check(`a11y basics: ${p}`, a.lang === 'en' && a.h1 === 1 && a.imgNoAlt === 0 && a.videoNoLabel === 0 && a.emptyLinks === 0 && a.unlabelledInputs === 0, JSON.stringify(a));
  }
} finally {
  await browser.close();
  server.kill();
}
const failed = results.filter((r) => !r.ok);
console.log(`site tests: ${results.length - failed.length}/${results.length} passed`);
process.exit(failed.length ? 1 : 0);
