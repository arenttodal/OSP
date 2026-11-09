# andor-web

Custom components, asset pipeline, tests and documentation for the ANDOR OSP website. Framer
holds the page layout (see `docs/FRAMER.md`). This folder is isolated from the plugin's DSP code.

**Current stage:** hero E approved at Gate H; the full landing page is built (`osp.html`,
`docs/PAGE.md`). Waiting on recordings, the waitlist provider and the Framer route.

## Run
```sh
npm install
npm run dev            # http://localhost:5173 (board), /osp.html (landing page), /g5.html (hero E), /h1–h3, /g1–g4
npm run build          # static prototype in dist/
npm test               # contrast + audio functional test + visual QA at 7 widths
```

## Layout
```
docs/          MASTER_SPEC, PHASE0_AUDIT, DESIGN_SYSTEM, HERO_CONCEPTS, ASSET_MANIFEST,
               CONTENT_MODEL, QA_CHECKLIST, FRAMER, DECISIONS
components/    audio/ (AudioComparison, GlobalAudioController) · media/ (ProductMedia) ·
               interactive/ (SiteHeader)
shared/        tokens/ (tokens.css, base.css) · types/ · content/ · utilities/
assets/        manifests/ (assets.json → generated.json) · optimized/ (committed) · source/ (git-ignored masters)
tools/         capture.sh (real editor captures) · render-object.py (studio renders) · build-assets.mjs
prototype/     hero concept pages and the concept board
tests/         accessibility/ · audio/ (harness + generated tone fixtures, test-only) · visual/
```

## Product imagery
Every image comes from the real ANDOR OSP editor at commit `c49d77f`, captured headlessly by the
hidden test `tests/plugin/WebCaptureTests.inc` in the OSP repo. Renders put that exact capture on
a studio slab (`tools/render-object.py`). The waveforms currently show deterministic stand-in test
signals; re-capture with approved recordings before launch (`OSP_WEB_SOURCES`).
