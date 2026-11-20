// Content checks before a build: every guide points at a captured, validated scene; its steps
// are exactly that scene's marks (so each step plays its own segment); IDs are unique; links
// between guides and reference articles resolve; nothing was verified against another commit.
import { readFileSync, readdirSync } from 'node:fs';
import { join, resolve, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';

const here = dirname(fileURLToPath(import.meta.url));
const manifest = JSON.parse(readFileSync(resolve(here, '../../assets/osp/manifest.json'), 'utf8'));
const scenes = new Map(manifest.scenes.map((e) => [e.scenario, e]));
const front = (file) => {
  const t = readFileSync(file, 'utf8');
  const m = t.match(/^---\n([\s\S]*?)\n---/);
  const get = (k) => (m[1].match(new RegExp(`^${k}: (.*)$`, 'm')) || [])[1];
  const steps = [...m[1].matchAll(/^  - \{ id: ([a-z0-9-]+),/gm)].map((x) => x[1]);
  const list = (k) => { const v = get(k); return v ? JSON.parse(v) : []; };
  return { id: get('id'), scene: get('scene'), reference: get('reference'), related: list('related'), guides: list('guides'), steps, commit: (m[1].match(/commit: "([^"]+)"/) || [])[1] };
};
const dir = (d) => readdirSync(resolve(here, d)).filter((f) => f.endsWith('.mdx')).map((f) => front(join(resolve(here, d), f)));
const guides = dir('../src/content/guides'), refs = dir('../src/content/reference');
const errors = [];
const ids = new Set();
for (const x of [...guides, ...refs]) { if (ids.has(x.id)) errors.push(`duplicate id ${x.id}`); ids.add(x.id); }
for (const g of guides) {
  const e = scenes.get(g.scene);
  if (!e) { errors.push(`${g.id}: scene ${g.scene} not captured`); continue; }
  if (e.validation.status !== 'passed') errors.push(`${g.id}: scene ${g.scene} failed validation`);
  const marks = Object.keys(e.marks);
  const extra = g.steps.filter((s) => !marks.includes(s)), missing = marks.filter((m) => !g.steps.includes(m));
  if (extra.length || missing.length) errors.push(`${g.id}: steps ${JSON.stringify(g.steps)} differ from the scene's marks ${JSON.stringify(marks)}`);
  if (g.commit !== e.productCommit) errors.push(`${g.id}: verified at ${g.commit}, captured at ${e.productCommit} (stale)`);
  if (g.reference && !ids.has(g.reference)) errors.push(`${g.id}: reference ${g.reference} missing`);
  for (const r of g.related) if (!ids.has(r)) errors.push(`${g.id}: related ${r} missing`);
}
for (const r of refs) for (const gid of r.guides) if (!ids.has(gid)) errors.push(`${r.id}: guide ${gid} missing`);
if (errors.length) { console.error(errors.join('\n')); process.exit(1); }
console.log(`content: ${guides.length} guides, ${refs.length} reference articles, ${guides.reduce((n, g) => n + g.steps.length, 0)} steps — all match captured, validated scenes`);
