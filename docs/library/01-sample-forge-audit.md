# Sample Forge audit

Repository `arenttodal/sampleforge` (private, the user's own), commit `d07ded6`
(2026-10-08), read-only clone. It is **not** the Round Robin Generator (that is
`arenttodal/roundrobin-generator`, whose Ableton exporter Sample Forge ports).

## What it is

A JavaScript web app (React 18, Vite, a PWA) that records sounds on a phone, chops recordings
into drum hits, tags them (kick, snare, clap, hat, tom, perc), puts them on 9 pads and exports
WAVs or Ableton Live Drum Racks. A Cloudflare Worker (D1 + R2) syncs a library between
paired devices. Tests: Vitest (unit) and Playwright (browser, including emulated phones).

There is **no C++** and no JUCE code. Nothing can be linked into the plugin as is; the useful
parts are small, well-separated algorithms that can be ported to C++.

## Capability by capability

| Capability | Status | Source | Tests | Reuse for OSP |
|---|---|---|---|---|
| Recording | Browser `MediaRecorder` / Web Audio in the UI component | `src/SampleForge.jsx` | e2e (emulated phone) | No (iOS app: AVFoundation, Stage 9) |
| Trimming (manual) | None as a tool; slices have start/end | - | - | No |
| Silence removal | Implicit: a slice ends when the hit has died into the noise floor | `src/audio/slicer.js` `sliceEnd` | unit | **Port the rule** (end = 3 x 10 ms below max(floor + 4 dB, peak - 50 dB)) |
| Transient / onset detection | Log-energy envelope (512 frame, 128 hop), gate above the 10th-percentile noise floor, a dB rise within 10 ms, 50 ms minimum gap; start moved back to 10 % of the peak minus 2 ms pre-roll | `slicer.js` `detectOnsets`, `refineStart` | unit: every synthetic hit found, never cuts into the attack (< 1 ms late), < 10 ms early; ignores room noise; close hits | **Port** (Stage 8): proven on synthetic drums; OSP's own `src/analysis` onset detector is tuned for tonal one-shots, the two complement each other |
| Automatic chopping / multi-event segmentation | Onsets, then slice end at the next onset, the noise floor or 1.5 s | `slicer.js` `sliceHits` | unit | **Port** with OSP's longer limit (sustained notes are valid one-shots) |
| Crossfades / fades | Only at export: 0.5 ms fade-in, 5 ms fade-out, DC removal | `src/export/...` | unit (export) | Port the fade values as Stage 8 defaults |
| Normalisation | Export only: one kit-wide gain so the loudest peaks at -1 dBFS, capped at +24 dB | export | unit | Idea only (Stage 8: optional gain, never by default) |
| Pitch detection | None | - | - | No (OSP has its own) |
| Classification | Fuzzy scores from band shares, centroid, flatness, decay, bursts in the first 40 ms | `src/audio/classify.js` | unit (synthetic kit, resampled) | Partly: drum tags as audio-derived tags (Stage 6); one-shot vs loop is not covered |
| Sample metadata | IndexedDB records (name, tag, source id) | `src/storage/db.js` | unit | Concepts only |
| Batch export | WAV and Ableton Drum Rack | `src/export/ableton/` | unit + validator | Not needed for OSP |
| Sync | Pairing by QR, Worker API (D1 + R2), no accounts | `src/sync`, `worker/` | unit + e2e | **No** (the Library must not need a cloud service; Inbox uses user folders, Stage 7) |

## Accuracy and performance

Only on synthetic fixtures (generated kicks, snares, claps, hats, toms in sequence, at
48/44.1/22.05 kHz). The handoff notes it is not tuned on real recordings and misses very
quiet hats under loud noise at the default sensitivity. No performance figures exist. Our port
will be measured on the Stage 6 labelled set.

## Licensing

The user's own private repository; the Ableton templates under `src/export/ableton/templates`
carry their own LICENSE (from roundrobin-generator) and are not needed here. Porting the
algorithms is within the user's rights; recorded as D-04.

## Decision

Port `detectOnsets` / `sliceEnd` / `sliceHits` (and the export fade values) to pure C++ in
`src/library/analysis` for Stage 8, with Sample Forge's tests translated as the first
fixtures. Do not port the UI, storage, sync or classifier wholesale.
