# Content model

Types in `shared/types/content.ts`; each maps to a Framer CMS collection so a non-developer can
edit text, images and audio without touching React.

| Collection | Fields | Notes |
|---|---|---|
| **Audio Demos** (`AudioDemo`) | id, title, sourceInstrument, description, artwork, waveformPeaks, durationSeconds, bpm?, variants[{id, label, sources[{src,type}], loudnessLufs}], sourceBuild, approved | Variants must be pre-aligned and loudness-matched. `sources` empty → player shows "pending". Peaks precomputed (no client analysis). |
| **Reimagined Modes** (`ReimaginedMode`) | id, name, subtitle, description, screenshot (asset id), video?, audio (AudioDemo), fallbackImage, approved | Exactly the five real modes. |
| **Features** | id, name, tier (essential / deeper), headline, body, asset id, order | Tier drives editorial hierarchy, not equal cards. |
| **FAQ** | question, answer, order | |
| **Reviews** | quote, name, role, source URL, verified | Only genuine; empty at launch is fine. |
| **Release Notes** | version, date, notes | |
| **Product Assets** | = `generated.json` entries | In Framer, product images are uploaded as managed images; the manifest id is kept in the alt/notes field for traceability. |

Code components receive content as props (Framer property controls) or from CMS bindings. No
component hard-codes a demo or asset (`AudioComparison` takes `demo`, `ProductMedia` takes `asset`).
