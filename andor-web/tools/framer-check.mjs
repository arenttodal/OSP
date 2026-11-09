// Keeps the Framer code components and the repository in step (docs/FRAMER_BUILD_GUIDE.md §9).
//   node tools/framer-check.mjs           check versions + tokens, then write framer/MANIFEST.json
//   node tools/framer-check.mjs --verify  check that framer/*.tsx still match the manifest
//   node tools/framer-check.mjs --hash <file pasted back from Framer>   print its hash to compare
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import { execSync } from 'node:child_process';

const root = path.resolve(path.dirname(new URL(import.meta.url).pathname), '..');
const dir = path.join(root, 'framer');
const files = fs.readdirSync(dir).filter((f) => f.endsWith('.tsx')).sort();
// Hash with normalised line endings so a copy through Framer's editor still compares equal.
const hash = (text) => crypto.createHash('sha256').update(text.replace(/\r\n/g, '\n').trimEnd() + '\n').digest('hex').slice(0, 16);
const args = process.argv.slice(2);

if (args[0] === '--hash') {
  console.log(hash(fs.readFileSync(args[1], 'utf8')));
  process.exit(0);
}

let problems = 0;
const fail = (m) => { console.error('FAIL ' + m); problems++; };
const tokensSrc = fs.readFileSync(path.join(dir, 'ANDOR_Tokens.tsx'), 'utf8');
const version = /ANDOR_VERSION = "([\d.]+)"/.exec(tokensSrc)?.[1];
if (!version) fail('ANDOR_VERSION not found in ANDOR_Tokens.tsx');

// 1. Every file carries the bundle version in its header.
for (const f of files) {
  const src = fs.readFileSync(path.join(dir, f), 'utf8');
  const m = /andor-web framer bundle v([\d.]+)/.exec(src);
  if (!m) fail(`${f}: no "andor-web framer bundle vX" header`);
  else if (m[1] !== version) fail(`${f}: header v${m[1]} ≠ ANDOR_VERSION ${version}`);
}

// 2. Framer tokens equal the site tokens (tokens.css is the source of truth).
const css = fs.readFileSync(path.join(root, 'shared/tokens/tokens.css'), 'utf8');
const cssTok = Object.fromEntries([...css.matchAll(/--([a-z0-9-]+):\s*(#[0-9a-f]{6})/gi)].map((m) => [m[1], m[2].toLowerCase()]));
const map = { bg: 'bg', bg2: 'bg-2', bgElev: 'bg-elev', text: 'text', text2: 'text-2', textMuted: 'text-muted', textQuiet: 'text-quiet', accent: 'accent', accentStrong: 'accent-strong',
  border: 'border', borderStrong: 'border-strong', darkBg: 'dark-bg', darkElev: 'dark-elev', darkText: 'dark-text', darkText2: 'dark-text-2', darkMuted: 'dark-muted', darkAccent: 'dark-accent', darkBorder: 'dark-border' };
for (const [k, cssName] of Object.entries(map)) {
  const v = new RegExp(`\\b${k}: "(#[0-9A-Fa-f]{6})"`).exec(tokensSrc)?.[1]?.toLowerCase();
  if (!v) fail(`token ${k} missing in ANDOR_Tokens.tsx`);
  else if (v !== cssTok[cssName]) fail(`token ${k} ${v} ≠ --${cssName} ${cssTok[cssName]}`);
}

const manifestPath = path.join(dir, 'MANIFEST.json');
if (args[0] === '--verify') {
  const manifest = JSON.parse(fs.readFileSync(manifestPath, 'utf8'));
  for (const f of files) {
    const h = hash(fs.readFileSync(path.join(dir, f), 'utf8'));
    if (manifest.files[f] !== h) fail(`${f} changed since the manifest (${manifest.files[f]} → ${h}): bump the version and re-run without --verify`);
  }
  for (const f of Object.keys(manifest.files)) if (!files.includes(f)) fail(`${f} is in the manifest but missing`);
} else if (!problems) {
  let commit = 'unknown';
  try { commit = execSync('git rev-parse --short HEAD', { cwd: root }).toString().trim(); } catch {}
  const manifest = { bundle: `andor-web framer bundle v${version}`, builtFrom: commit, files: Object.fromEntries(files.map((f) => [f, hash(fs.readFileSync(path.join(dir, f), 'utf8'))])) };
  fs.writeFileSync(manifestPath, JSON.stringify(manifest, null, 2) + '\n');
  console.log(`wrote framer/MANIFEST.json (v${version}, ${files.length} files, from ${commit})`);
}
if (problems) process.exit(1);
console.log(`OK: ${files.length} files at v${version}; tokens match tokens.css`);
