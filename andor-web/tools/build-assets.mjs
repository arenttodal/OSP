// Builds the website's optimized images from genuine OSP captures.
// Reads assets/manifests/assets.json, cuts crops from the masters (1x editor coordinates scaled to
// the master's resolution), writes AVIF + WebP at each width to assets/optimized/, and records the
// result in assets/manifests/generated.json (dimensions for layout reservation, srcset lists).
// Usage: node tools/build-assets.mjs
import fs from 'node:fs';
import path from 'node:path';
import sharp from 'sharp';

const root = path.resolve(path.dirname(new URL(import.meta.url).pathname), '..');
const manifest = JSON.parse(fs.readFileSync(path.join(root, 'assets/manifests/assets.json'), 'utf8'));
const srcDir = path.join(root, 'assets/source/captures');
const outDir = path.join(root, 'assets/optimized');
fs.mkdirSync(outDir, { recursive: true });

const EDITOR_W = 1448; // the canonical editor width every capture is taken at
const generated = { schemaVersion: 1, product: manifest.product, assets: {} };

const only = process.argv.slice(2);
const prev = fs.existsSync(path.join(root, 'assets/manifests/generated.json')) ? JSON.parse(fs.readFileSync(path.join(root, 'assets/manifests/generated.json'), 'utf8')).assets : {};
for (const a of manifest.assets) {
  if (only.length && !only.includes(a.id)) { if (prev[a.id]) generated.assets[a.id] = prev[a.id]; continue; }
  const masterPath = path.join(root, 'assets/source', a.masterDir ?? 'captures', a.master);
  if (!fs.existsSync(masterPath)) {
    // Never substitute: a missing master is an error the manifest must show.
    console.error(`MISSING master for ${a.id}: ${a.master}`);
    process.exitCode = 1;
    continue;
  }
  let input = masterPath;
  if (a.trim) {
    // Renders: cut the transparent margin but keep the soft shadow (alpha > ~1 %).
    const t = await sharp(masterPath).trim({ background: { r: 0, g: 0, b: 0, alpha: 0 }, threshold: 3 }).toBuffer();
    const tm = await sharp(t).metadata();
    const pad = Math.round(tm.width * 0.02);
    // The shadow catcher ends at the render frame: feather alpha over the outer 7 % so the soft
    // shadow never shows a hard edge on the page.
    const { data, info } = await sharp(t).ensureAlpha().raw().toBuffer({ resolveWithObject: true });
    const fx = info.width * 0.07, fy = info.height * 0.07;
    const ease = (v) => v * v * (3 - 2 * v);
    for (let y = 0; y < info.height; y++) {
      const ky = ease(Math.min(1, (info.height - 1 - y) / fy)); // bottom only: the top edge is the object itself
      for (let x = 0; x < info.width; x++) {
        const k = ky * ease(Math.min(1, Math.min(x, info.width - 1 - x) / fx));
        if (k < 1) data[(y * info.width + x) * 4 + 3] = Math.round(data[(y * info.width + x) * 4 + 3] * k);
      }
    }
    const feathered = await sharp(data, { raw: info }).png().toBuffer();
    input = await sharp(feathered).extend({ top: pad, bottom: pad, left: pad, right: pad, background: { r: 0, g: 0, b: 0, alpha: 0 } }).png().toBuffer();
  }
  const meta = await sharp(input).metadata();
  const scale = meta.width / EDITOR_W;
  let base = sharp(input);
  let w = meta.width, h = meta.height;
  if (a.crop) {
    const c = { left: Math.round(a.crop.x * scale), top: Math.round(a.crop.y * scale), width: Math.round(a.crop.w * scale), height: Math.round(a.crop.h * scale) };
    base = base.extract(c);
    w = c.width; h = c.height;
  }
  const buf = await base.png().toBuffer();
  const widths = a.widths.filter((x) => x <= w);
  if (!widths.includes(w) && widths.length < a.widths.length) widths.push(w); // largest = native
  const files = { avif: [], webp: [] };
  for (const width of widths) {
    const img = sharp(buf).resize({ width, withoutEnlargement: true });
    const height = Math.round((h * width) / w);
    const stem = `${a.id}-${width}`;
    // Interface captures hold fine text: keep quality high, sharp chroma.
    await img.clone().avif({ quality: 62, effort: 4, chromaSubsampling: '4:4:4' }).toFile(path.join(outDir, `${stem}.avif`));
    await img.clone().webp({ quality: 86, smartSubsample: true }).toFile(path.join(outDir, `${stem}.webp`));
    files.avif.push({ width, height, file: `${stem}.avif` });
    files.webp.push({ width, height, file: `${stem}.webp` });
  }
  generated.assets[a.id] = { id: a.id, alt: a.alt, use: a.use, width: w, height: h, aspect: +(w / h).toFixed(5), priority: !!a.priority, approval: a.approval, master: a.master, treatment: a.treatment ?? null, crop: a.crop ?? null, files };
  console.log(`${a.id}: ${w}x${h} -> ${widths.join(', ')}`);
}
fs.writeFileSync(path.join(root, 'assets/manifests/generated.json'), JSON.stringify(generated, null, 2));
