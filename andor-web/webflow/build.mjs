#!/usr/bin/env node
// Builds the ANDOR OSP landing page for Webflow.
//
//   node webflow/build.mjs --media <base URL> [--waitlist <MailerLite form endpoint>] [--out webflow/dist] [--preview]
//
// <x-img name=… sizes=… alt=…> and <x-film id=… mode=… alt=…> in src/page.html are expanded from media.json
// into responsive <picture> markup; {{M}} becomes the media base URL (Cloudflare R2). Output:
//   andor.min.css, andor.min.js           upload next to the media (<base>/andor.min.css …)
//   webflow-head.html                     Page settings → Custom code → Inside <head>
//   webflow-embed.html                    a Code Embed element (the page itself)
//   webflow-footer.html                   Page settings → Custom code → Before </body>
//   webflow-single-embed.html             all three in one Code Embed (if the page allows only one)
//   preview.html                          the whole page as a standalone document, for local testing
import { readFileSync, writeFileSync, mkdirSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";
import { createRequire } from "node:module";

const HERE = dirname(fileURLToPath(import.meta.url));
const require = createRequire(join(HERE, "..", "package.json"));
const esbuild = require("esbuild");
const arg = (k, d) => { const i = process.argv.indexOf(k); return i > 0 ? process.argv[i + 1] : d; };
const MEDIA = arg("--media", "media").replace(/\/$/, "");
const WAITLIST = arg("--waitlist", "");
const OUT = arg("--out", join(HERE, "dist"));
const VERSION = "1";
const PREVIEW = process.argv.includes("--preview"); // a hosted preview: the Webflow size limit only warns
const manifest = JSON.parse(readFileSync(join(HERE, "media.json"), "utf8"));
const LIMIT = 50000; // Webflow's character limit for one Code Embed and for each custom-code field
mkdirSync(OUT, { recursive: true });

const attrs = (s) => Object.fromEntries([...s.matchAll(/([\w-]+)(?:="([^"]*)")?/g)].map((m) => [m[1], m[2] ?? ""]));
const esc = (s) => s.replace(/&/g, "&amp;").replace(/"/g, "&quot;");
const missing = [];

function picture(a) {
  const m = manifest[a.name];
  if (!m) { missing.push(a.name); return ""; }
  const ws = m.widths, big = ws[ws.length - 1], mid = ws.find((w) => w >= 1080) || big;
  const set = (ext) => ws.map((w) => `${MEDIA}/img/${a.name}-${w}.${ext} ${w}w`).join(", ");
  const h = Math.round(big * m.ratio);
  const extra = Object.entries(a).filter(([k]) => k.startsWith("data-")).map(([k, v]) => ` ${k}="${esc(v)}"`).join("");
  const cls = a.class ? ` class="${esc(a.class)}"` : "";
  const load = "eager" in a ? `fetchpriority="high"` : `loading="lazy"`;
  return `<picture${cls}${extra}><source type="image/avif" srcset="${set("avif")}" sizes="${esc(a.sizes || "100vw")}">` +
    `<img src="${MEDIA}/img/${a.name}-${mid}.webp" srcset="${set("webp")}" sizes="${esc(a.sizes || "100vw")}" alt="${esc(a.alt || "")}" width="${big}" height="${h}" ${load} decoding="async"></picture>`;
}
function film(a) {
  if (!manifest["film:" + a.id]) { missing.push("film:" + a.id); return ""; }
  const cls = ["a-film", a.class].filter(Boolean).join(" ");
  const p = `${MEDIA}/film/${a.id}_poster`;
  return `<figure class="${cls}" data-film="${a.id}" data-mode="${a.mode || "once"}"><picture><source type="image/webp" srcset="${p}.webp">` +
    `<img src="${p}.jpg" alt="${esc(a.alt || "")}" width="1920" height="1080" loading="lazy" decoding="async"></picture></figure>`;
}

let html = readFileSync(join(HERE, "src/page.html"), "utf8")
  .replace(/<x-img\s([^>]*)><\/x-img>/g, (_, s) => picture(attrs(s)))
  .replace(/<x-film\s([^>]*)><\/x-film>/g, (_, s) => film(attrs(s)))
  .replaceAll("{{M}}", MEDIA)
  .replace('<div class="andor" id="andor-top">', `<div class="andor" id="andor-top" data-media="${MEDIA}"${WAITLIST ? ` data-waitlist-endpoint="${esc(WAITLIST)}"` : ""}>`)
  .replace(/<!--[\s\S]*?-->/g, "").replace(/\n\s*\n/g, "\n").replace(/>\s+</g, (m) => (m.includes("\n") ? ">\n<" : m));
if (missing.length) { console.error("missing media:", [...new Set(missing)].join(", ")); process.exit(1); }

const css = esbuild.transformSync(readFileSync(join(HERE, "src/andor.css"), "utf8").replaceAll("{{M}}", MEDIA), { loader: "css", minify: true, target: ["chrome100", "safari15", "firefox100"] }).code;
const js = esbuild.transformSync(readFileSync(join(HERE, "src/andor.js"), "utf8"), { loader: "js", minify: true, target: ["es2020"] }).code;
writeFileSync(join(OUT, "andor.min.css"), css);
writeFileSync(join(OUT, "andor.min.js"), js);

const v = `?v=${VERSION}`;
const head = [
  `<!-- ANDOR OSP: Page settings > Custom code > Inside <head> -->`,
  `<link rel="preconnect" href="${new URL(MEDIA, "https://x.invalid").origin}" crossorigin>`,
  `<link rel="preload" href="${MEDIA}/font/inter-tight-var.woff2" as="font" type="font/woff2" crossorigin>`,
  `<link rel="preload" href="${MEDIA}/font/eb-garamond-400.woff2" as="font" type="font/woff2" crossorigin>`,
  `<link rel="preload" as="image" type="image/avif" imagesrcset="${[1280, 1920, 2560, 3840].map((w) => `${MEDIA}/img/hero-desktop-${w}.avif ${w}w`).join(", ")}" imagesizes="100vw" media="(min-aspect-ratio: 4/5)">`,
  `<link rel="stylesheet" href="${MEDIA}/andor.min.css${v}">`,
].join("\n");
const libs = ["gsap.min.js", "ScrollTrigger.min.js", "SplitText.min.js", "lenis.min.js"].map((f) => `<script src="${MEDIA}/lib/${f}" defer></script>`).join("\n");
const footer = `<!-- ANDOR OSP: Page settings > Custom code > Before </body> -->\n${libs}\n<script src="${MEDIA}/andor.min.js${v}" defer></script>`;
const embed = `<!-- ANDOR OSP: Code Embed (the page) -->\n${html}`;
const single = `${head.replace(/<!--.*?-->\n/, "")}\n${html}\n${footer.replace(/<!--.*?-->\n/, "")}`;
writeFileSync(join(OUT, "webflow-head.html"), head);
writeFileSync(join(OUT, "webflow-embed.html"), embed);
writeFileSync(join(OUT, "webflow-footer.html"), footer);
writeFileSync(join(OUT, "webflow-single-embed.html"), single);
// a stand-in for the Webflow page: its normalize-style defaults on body, so the scoping is exercised
writeFileSync(join(OUT, "preview.html"), `<!doctype html><html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1">
<title>ANDOR OSP</title><meta name="description" content="ANDOR OSP turns a single recording into an expressive, playable instrument.">
<style>html{height:100%}body{margin:0;min-height:100%;font-family:Arial,sans-serif;font-size:14px;line-height:20px;color:#333;background:#fff}</style>
${head}</head><body><div class="w-embed">${html}</div>${footer}</body></html>`);

const sizes = { "webflow-head.html": head.length, "webflow-embed.html": embed.length, "webflow-footer.html": footer.length, "webflow-single-embed.html": single.length };
let ok = true;
for (const [k, n] of Object.entries(sizes)) { const fits = n <= LIMIT; ok &&= fits || k === "webflow-single-embed.html"; console.log(`${k.padEnd(28)} ${String(n).padStart(6)} chars ${fits ? "OK" : "OVER " + LIMIT}`); }
console.log(`andor.min.css ${css.length} B, andor.min.js ${js.length} B, media ${MEDIA}`);
if (!ok && PREVIEW) console.warn("over the Webflow limit: fine for a preview; for Webflow, serve the media from a short custom domain");
else if (!ok) process.exit(1);
