// Copies the capture pipeline's web assets and manifest into the site (the site never names
// a file by hand: every video, poster and step still comes from learn/assets/osp/manifest.json).
import { cpSync, mkdirSync, readFileSync, writeFileSync, rmSync, existsSync } from 'node:fs';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const here = dirname(fileURLToPath(import.meta.url));
const assets = resolve(here, '../../assets/osp');
const media = resolve(here, '../public/media/osp');
const manifest = JSON.parse(readFileSync(join(assets, 'manifest.json'), 'utf8'));
rmSync(media, { recursive: true, force: true });
mkdirSync(media, { recursive: true });
let copied = 0;
for (const e of manifest.scenes) {
  if (e.validation.status !== 'passed') continue;   // failed scenes never reach the site
  const slug = e.scenario.split('.').pop();
  cpSync(join(assets, slug), join(media, slug), { recursive: true, filter: (p) => !p.endsWith('entry.json') });
  copied++;
}
mkdirSync(resolve(here, '../src/data'), { recursive: true });
writeFileSync(resolve(here, '../src/data/manifest.json'), JSON.stringify(manifest, null, 2));
console.log(`media: ${copied} scenes copied; manifest for ${manifest.productCommit}`);
