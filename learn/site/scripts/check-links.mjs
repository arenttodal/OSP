// Every internal link, video, poster and image in the built site resolves to a file (base: LEARN_BASE, default /).
import { readFileSync, readdirSync, statSync, existsSync } from 'node:fs';
import { join, resolve, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';
const base = (process.env.LEARN_BASE ?? '/').replace(/\/?$/, '/');
const dist = resolve(dirname(fileURLToPath(import.meta.url)), '../dist');
const pages = [];
const walk = (d) => { for (const f of readdirSync(d)) { const p = join(d, f); statSync(p).isDirectory() ? walk(p) : p.endsWith('.html') && pages.push(p); } };
walk(dist);
const bad = [];
let checked = 0;
for (const page of pages) {
  const html = readFileSync(page, 'utf8');
  for (const [, url] of html.matchAll(/(?:href|src|poster)="([^"#?]+)/g)) {
    if (!url.startsWith(base) || url.startsWith('//')) continue;
    checked++;
    let p = join(dist, decodeURI(url.slice(base.length)));
    if (url.endsWith('/')) p = join(p, 'index.html');
    if (!existsSync(p)) bad.push(`${page.slice(dist.length)} → ${url}`);
  }
}
if (bad.length) { console.error(`broken links (${bad.length}):\n` + bad.slice(0, 40).join('\n')); process.exit(1); }
console.log(`links: ${checked} internal links in ${pages.length} pages, none broken`);
