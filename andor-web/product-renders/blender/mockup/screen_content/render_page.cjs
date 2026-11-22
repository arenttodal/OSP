// Renders a local page to a 3840x2160 PNG.  node render_page.cjs <page.html> <out.png> "<query>"
const { chromium } = require("playwright");
const path = require("path");
(async () => {
  const [page, out, query] = process.argv.slice(2);
  const b = await chromium.launch({ args: ["--allow-file-access-from-files"] });
  const p = await b.newPage({ viewport: { width: 1920, height: 1080 }, deviceScaleFactor: 2 });
  await p.goto("file://" + path.resolve(page) + "?" + (query || ""));
  await p.waitForFunction(() => document.title === "ready", null, { timeout: 15000 });
  await p.waitForTimeout(400);
  await p.screenshot({ path: out });
  await b.close();
  console.log("wrote", out);
})();
