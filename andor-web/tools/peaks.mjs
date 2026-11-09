// Precomputes waveform peaks for AudioComparison / SoundDeck (paste the output into the
// component's "Peaks (JSON)" control). Peaks are computed once, here, never on page visits.
// Usage: node tools/peaks.mjs <audio file> [columns=160]
// Needs ffmpeg on PATH. Output: a JSON array of numbers 0..1 (rounded to 3 decimals).
import { execFileSync } from 'node:child_process';

const [file, colsArg] = process.argv.slice(2);
if (!file) { console.error('usage: node tools/peaks.mjs <audio file> [columns=160]'); process.exit(2); }
const cols = Math.max(16, Math.min(1000, Number(colsArg) || 160));
// Decode to mono 32-bit float at 8 kHz: plenty for an overview, small to process.
const raw = execFileSync('ffmpeg', ['-v', 'error', '-i', file, '-ac', '1', '-ar', '8000', '-f', 'f32le', '-'], { maxBuffer: 1 << 30 });
const samples = new Float32Array(raw.buffer, raw.byteOffset, Math.floor(raw.byteLength / 4));
if (!samples.length) { console.error('no audio decoded'); process.exit(1); }
const per = samples.length / cols;
const peaks = [];
for (let c = 0; c < cols; c++) {
  let m = 0;
  for (let i = Math.floor(c * per), end = Math.floor((c + 1) * per); i < end; i++) m = Math.max(m, Math.abs(samples[i]));
  peaks.push(m);
}
const max = Math.max(...peaks) || 1;
console.log(JSON.stringify(peaks.map((p) => Math.round((p / max) * 1000) / 1000)));
