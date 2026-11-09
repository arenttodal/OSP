// Measures WCAG 2.x contrast for every text/background pairing the design system allows.
// Fails (exit 1) if a pairing drops below its required ratio. Usage: node tests/accessibility/contrast.mjs
import fs from 'node:fs';
import path from 'node:path';

const css = fs.readFileSync(path.resolve(path.dirname(new URL(import.meta.url).pathname), '../../shared/tokens/tokens.css'), 'utf8');
const tok = Object.fromEntries([...css.matchAll(/--([a-z0-9-]+):\s*(#[0-9a-f]{6})/gi)].map((m) => [m[1], m[2]]));
const lum = (hex) => {
  const c = [1, 3, 5].map((i) => parseInt(hex.slice(i, i + 2), 16) / 255).map((x) => (x <= 0.03928 ? x / 12.92 : ((x + 0.055) / 1.055) ** 2.4));
  return 0.2126 * c[0] + 0.7152 * c[1] + 0.0722 * c[2];
};
const ratio = (a, b) => { const [x, y] = [lum(a), lum(b)].sort((p, q) => q - p); return (x + 0.05) / (y + 0.05); };

// [foreground, background, minimum, purpose]
const pairs = [
  ['text', 'bg', 4.5, 'body'], ['text', 'bg-2', 4.5, 'body'], ['text', 'bg-elev', 4.5, 'body'],
  ['text-2', 'bg', 4.5, 'secondary'], ['text-2', 'bg-2', 4.5, 'secondary'], ['text-2', 'bg-elev', 4.5, 'secondary'],
  ['text-muted', 'bg', 4.5, 'muted'], ['text-muted', 'bg-2', 4.5, 'muted'], ['text-muted', 'bg-elev', 4.5, 'muted'],
  ['accent-strong', 'bg', 4.5, 'accent text'], ['accent-strong', 'bg-elev', 4.5, 'accent text'],
  ['text-quiet', 'bg', 3.0, 'two-tone headline (large type only)'], ['dark-quiet', 'dark-bg', 3.0, 'two-tone headline on dark'],
  ['accent', 'bg', 3.0, 'accent graphics / large type'],
  ['border-strong', 'bg', 3.0, 'control outline'], ['border-strong', 'bg-elev', 3.0, 'control outline'],
  ['dark-text', 'dark-bg', 4.5, 'dark body'], ['dark-text', 'dark-elev', 4.5, 'dark body'],
  ['dark-text-2', 'dark-bg', 4.5, 'dark secondary'], ['dark-text-2', 'dark-elev', 4.5, 'dark secondary'],
  ['dark-muted', 'dark-bg', 4.5, 'dark muted'], ['dark-muted', 'dark-elev', 4.5, 'dark muted'],
  ['dark-accent', 'dark-bg', 4.5, 'dark accent text'],
  ['bg', 'text', 4.5, 'primary button (paper on ink)'], ['dark-bg', 'dark-text', 4.5, 'primary button on dark'],
];
let fail = 0;
for (const [fg, bg, min, why] of pairs) {
  const r = ratio(tok[fg], tok[bg]);
  const ok = r >= min;
  if (!ok) fail++;
  console.log(`${ok ? 'PASS' : 'FAIL'}  ${r.toFixed(2).padStart(5)} : 1  (min ${min})  ${fg} on ${bg}  — ${why}`);
}
console.log(`white on accent-strong: ${ratio('#ffffff', tok['accent-strong']).toFixed(2)}`);
process.exit(fail ? 1 : 0);
