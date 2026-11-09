# Asset manifest

Machine-readable: `assets/manifests/assets.json` (what each asset is) and
`assets/manifests/generated.json` (what was produced: sizes, files, srcset data). This page is
the human summary and the rules.

## Rules
1. Every product image originates from the real OSP editor at a recorded commit (`product.commit`).
2. Each asset records: stable id, master file, crop (if any), intended use, alt text, approval
   status, and treatment (for renders).
3. Missing master → the build fails loudly; the site shows a "missing media" state. Never a
   CSS recreation.
4. Audio: every file records source recording, owner/licence, render settings, OSP commit,
   alignment offset and integrated loudness (LUFS). **No audio exists yet (A-01).**
5. Production hosting: images through Framer's image delivery; audio/video through a managed CDN.
   No raw GitHub URLs, no temporary paths.

## Pipeline
```
OSP build (c49d77f) ──[web-capture] hidden test──▶ assets/source/captures/*.png  (1x/2x/3x, not committed)
                     └─ tools/render-object.py ──▶ assets/source/renders/*.png   (studio renders)
assets.json ──tools/build-assets.mjs──▶ assets/optimized/<id>-<width>.{avif,webp} + generated.json
```
Regenerate everything: `andor-web/tools/capture.sh` (see the script header), then
`python tools/render-object.py …` per render, then `node tools/build-assets.mjs`.

## Current assets (all `approval: concept`, source audio = stand-in test signals)

| Id | From | Use |
|---|---|---|
| `object-1l`, `object-3l`, `object-imprint` | studio renders of `web-01-hero-1L@3x`, `web-01-hero-3L@3x`, `web-03-synth-3-imprint@2x` | H1 hero object + its states |
| `osp-hero-1l` | `web-01-hero-1L@3x` | H2, H3 hero |
| `osp-hero-2l`, `osp-hero-3l` | captures | Layering |
| `osp-empty` | `web-00-empty@2x` | First Note: before |
| `osp-analog`, `osp-imprint` | SYNTH page captures | SYNTH + IMPRINT |
| `osp-reimagined-mosaic` | REIMAGINED popover capture | REIMAGINED |
| `crop-display`, `crop-macros`, `crop-knob-life`, `crop-keybed`, `crop-teak`, `crop-imprint-display`, `crop-mosaic-popover` | crops of the above | details, H3 prints, H2 matched cut |

Ownership: all GUI imagery is the owner's product. Fonts: Inter Tight, EB Garamond and Geist Mono
are all SIL Open Font License.

## Round 3 additions
Captures `web-06-life`, `web-06-space`, `web-06-reimagined-1…5` (3x, each with a `.json` of the
popup's editor bounds), `web-07-arp-3L` (three layers, arp on, panel open, editor grown) and
`web-08-hero-3L-bounds` (every component's bounds). Assets `dd-*` are crops of these for the
deep-dive modals: `dd-recording-empty/-loaded`, `dd-life`, `dd-space`, `dd-synth-analog/-imprint/-motion`,
`dd-reimagined-<mode>` ×5, `dd-arp`.

## Landing-page additions
Captures `web-09-granular-1L` (granular controls shown), `web-09-echo` (ASCEND popover),
`web-09-modulation` (LFO 1), `web-09-keydna`, `web-09-notemap`, `web-09-capture` (recording) and
`web-09-library`, each with bounds JSON. The library capture is unused: a fresh test profile has
no presets. Assets: `bub-recording`, `bub-synth`, `syn-card-*` ×4, `gran-card`, `feat-notemap`,
`feat-capture`, `feat-keydna`, `det-mod`.
