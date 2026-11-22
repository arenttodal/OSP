// QA for the Webflow page: loads it at desktop and phone sizes, scrolls through it in steps and saves viewport
// screenshots, and reports console errors, failed requests, horizontal overflow and the layout shift.
//   node webflow/tools/qa.mjs <url> <out dir> [--reduce]
import { createRequire } from "node:module";
const { chromium } = createRequire(import.meta.url)("playwright");  // the machine-wide Playwright (CommonJS)
import { mkdirSync, writeFileSync } from "node:fs";

const [url, out] = process.argv.slice(2);
const reduce = process.argv.includes("--reduce");
mkdirSync(out, { recursive: true });
const sizes = [
  { name: "desktop", viewport: { width: 1440, height: 900 } },
  { name: "laptop", viewport: { width: 1280, height: 720 } },
  { name: "tablet", viewport: { width: 834, height: 1112 } },
  { name: "phone", viewport: { width: 390, height: 844 }, isMobile: true, hasTouch: true, deviceScaleFactor: 2 },
];
const report = {};
const b = await chromium.launch();
for (const s of sizes) {
  const ctx = await b.newContext({ viewport: s.viewport, isMobile: s.isMobile, hasTouch: s.hasTouch, deviceScaleFactor: s.deviceScaleFactor || 1, reducedMotion: reduce ? "reduce" : "no-preference" });
  const p = await ctx.newPage();
  const errors = [], failed = [];
  p.on("console", (m) => m.type() === "error" && errors.push(m.text()));
  p.on("pageerror", (e) => errors.push(String(e)));
  p.on("requestfailed", (r) => failed.push(r.url().split("/").slice(-2).join("/") + " " + (r.failure()?.errorText || "")));
  p.on("response", (r) => r.status() >= 400 && failed.push(`${r.status()} ${r.url().split("/").slice(-2).join("/")}`));
  await p.addInitScript(() => { window.__cls = 0; new PerformanceObserver((l) => { for (const e of l.getEntries()) if (!e.hadRecentInput) window.__cls += e.value; }).observe({ type: "layout-shift", buffered: true }); });
  await p.goto(url, { waitUntil: "networkidle" });
  await p.waitForTimeout(2600);
  const H = await p.evaluate(() => document.documentElement.scrollHeight);
  const vh = s.viewport.height;
  const shots = [];
  let i = 0;
  for (let y = 0; y < H - vh * 0.5; y += Math.round(vh * 0.9)) {
    await p.evaluate((yy) => window.scrollTo(0, yy), y);
    await p.waitForTimeout(900);
    const f = `${out}/${s.name}-${String(i).padStart(2, "0")}.jpg`;
    await p.screenshot({ path: f, type: "jpeg", quality: 70 });
    shots.push(f); i++;
  }
  const overflow = await p.evaluate(() => document.documentElement.scrollWidth - document.documentElement.clientWidth);
  const cls = await p.evaluate(() => window.__cls);
  const films = await p.evaluate(() => [...document.querySelectorAll(".a-film video")].map((v) => ({ film: v.closest(".a-film").dataset.film, src: (v.currentSrc || "").split("/").pop(), ready: v.readyState })));
  report[s.name] = { height: H, shots: shots.length, overflow_px: overflow, cls: +cls.toFixed(4), errors, failed: [...new Set(failed)], films };
  await ctx.close();
}
await b.close();
writeFileSync(`${out}/report.json`, JSON.stringify(report, null, 1));
for (const [k, r] of Object.entries(report)) console.log(k, `h=${r.height}`, `shots=${r.shots}`, `overflow=${r.overflow_px}`, `cls=${r.cls}`, `errors=${r.errors.length}`, `failed=${r.failed.length}`, r.errors.slice(0, 3), r.failed.slice(0, 5));
