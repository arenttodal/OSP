// Renders daw.html to a 3840x2160 PNG (screen content for screen_mockup.py).
//   node render_content.cjs <out.png> "<query string>"     e.g. "plugin=file:///…/web-01-hero-3L@2x.png&layout=window"
const { chromium } = require("playwright");
const path = require("path");
(async () => {
  const [out, query] = process.argv.slice(2);
  const b = await chromium.launch({ args: ["--allow-file-access-from-files"] });
  const p = await b.newPage({ viewport: { width: 1920, height: 1080 }, deviceScaleFactor: 2 });
  await p.goto("file://" + path.join(__dirname, "daw.html") + "?" + (query || ""));
  await p.waitForFunction(() => document.title === "ready", null, { timeout: 15000 });
  await p.waitForTimeout(300);
  await p.screenshot({ path: out });
  await b.close();
  console.log("wrote", out);
})();
