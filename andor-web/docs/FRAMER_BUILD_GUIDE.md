# Framer build guide — ANDOR OSP landing page

Builds the approved page (`osp.html`, hero E) in the Framer project
**Andor** (`framer.com/projects/Andor--wzk8dDluBfoaTaPRcn4R`). The prototype is the reference for
every pixel: open it beside Framer while you work. Do not touch the existing The Library project.

**What you need**
- `andor-web/framer/*.tsx`: eight code files (§4).
- `framer-upload/`: 45 images named by section (the zip sent with this guide, or run
  `node tools/framer-pack.mjs` in `andor-web/`).
- `docs/FRAMER_CONTENT.md`: every text and value to type into the controls, generated from the
  prototype's content.

**Ground rules**
- Native Framer for layout, text, the header, the hero bubbles, the First Note scroll, the
  waitlist form and motion. Code only where the guide says so.
- Every product image comes from `framer-upload/`. If one is missing, leave the slot empty and
  flag it. Never redraw the interface.
- If Framer asks for a plan upgrade or any payment at any step, stop and ask the owner. No
  expenses are committed by this build.
- Publish to the Framer staging subdomain only (§11). Not to a production domain.

---

## 1. Project settings
1. **Site settings → General:** title "ANDOR OSP — One Shot Performer"; description "ANDOR OSP
   turns one recording into a playable, expressive instrument. In development for macOS."
2. **Site settings → SEO:** turn search indexing **off** until launch (the prototype uses `noindex`).
3. **Fonts:** add **Inter Tight** (variable), **Geist Mono** (400, 500) and **Inter** (optional,
   for future editions) from Framer's Google Fonts. The code components ask for "Inter Tight" and
   "Geist Mono" by name, so they must be installed in the project.

## 2. Styles
### Colour styles (Assets → Colors). Create with exactly these names
| Name | Value | Use |
|---|---|---|
| ANDOR/bg | `#F7F6F2` | page |
| ANDOR/bg-2 | `#EDEBE5` | details section, chips |
| ANDOR/bg-elev | `#FCFBF8` | bubbles, modal, tiles |
| ANDOR/text | `#242723` | text, ink buttons |
| ANDOR/text-2 | `#5C635A` | secondary text |
| ANDOR/text-muted | `#656C63` | labels |
| ANDOR/text-quiet | `#878C83` | second tone of headlines, **large type only** |
| ANDOR/accent | `#BD653D` | marks and rules only (never body text) |
| ANDOR/accent-strong | `#A4532E` | accent text, focus rings |
| ANDOR/border | `#D8D9D1` | hairlines |
| ANDOR/border-strong | `#858C82` | control outlines |
| ANDOR/dark-bg | `#222924` | dark sections |
| ANDOR/dark-elev | `#303A32` | |
| ANDOR/dark-text | `#F4F1E9` | |
| ANDOR/dark-text-2 | `#C4CEC1` | |
| ANDOR/dark-muted | `#9AA597` | |
| ANDOR/dark-quiet | `#7F897F` | |
| ANDOR/dark-accent | `#D08A63` | |
| ANDOR/pill | `#E8E6E0` | header pills (hover `#DFDDD6`) |

All pairings are measured in `docs/DESIGN_SYSTEM.md` §1. A section is light or dark as a whole;
never put light text on a light surface.

### Text styles (Assets → Text). Set each size per breakpoint
| Style | Font | Desktop 1440 | Tablet 768 | Phone 390 | Line / tracking |
|---|---|---|---|---|---|
| Hero | Inter Tight 520 | 74 | 56 | 48 | 0.98 / −4.2 % |
| Section | Inter Tight 500 | 64 | 46 | 36 | 1.04 / −4 % |
| Statement | Inter Tight 500 | 64 | 44 | 34 | 1.06 / −4 % |
| Feature | Inter Tight 500 | 40 | 32 | 28 | 1.08 / −3 % |
| Lede | Inter Tight 400 | 21 | 19 | 17 | 1.45 / −1 % |
| Body | Inter Tight 400 | 17 | 16 | 15 | 1.55 / 0 |
| Small | Inter Tight 400 | 14 | 14 | 14 | 1.45 / 0 |
| Mono label | Geist Mono 500, UPPERCASE | 12 | 12 | 12 | 1.3 / +4 % |
| Wordmark | Inter Tight 760 | 20 | 20 | 18 | 1 / +2 % |

Two-tone headlines: one text layer, with the second phrase coloured **ANDOR/text-quiet** (dark
sections: **ANDOR/dark-quiet**).

## 3. Breakpoints, grid, spacing
- Breakpoints: **Desktop 1440** (primary), **Tablet 768**, **Phone 390**. Check 1280, 1024, 375 and
  320 in Preview before publishing.
- Content max width **1360**, centred. Side padding: Desktop **80**, Tablet **43**, Phone **22**.
- Desktop columns: 12, gap 24. Section padding (top and bottom): Desktop **160**, Tablet **112**,
  Phone **88**.
- Corner radii: bubbles and plates 28, the modal 30, tiles 24, pills 999, images 7–12.
- Shadows: "Object" (`0 28 60 -22 rgba(46,38,28,.28)` + `0 70 120 -50 rgba(46,38,28,.30)`) for
  captures; "Plate" (`0 18 40 -26 rgba(46,38,28,.35)`) for bubbles.

## 4. Install the code components
In **Assets → Code → + → New code file**, create each file below with **exactly** this name, then
replace its contents with the file from `andor-web/framer/`. Create them **in this order** (later
files import earlier ones):

| # | Framer file name | From | What it is |
|---|---|---|---|
| 1 | `ANDOR_Tokens` | `framer/ANDOR_Tokens.tsx` | Colours, fonts and motion (not a component) |
| 2 | `ANDOR_AudioController` | `framer/ANDOR_AudioController.tsx` | One audio player at a time, site-wide (not a component) |
| 3 | `AudioComparison` | `framer/AudioComparison.tsx` | The player (hero, anywhere) |
| 4 | `SoundDeck` | `framer/SoundDeck.tsx` | Sounds section |
| 5 | `ViewSwitcher` | `framer/ViewSwitcher.tsx` | REIMAGINED, SYNTH + IMPRINT, Layers |
| 6 | `DeepDive` | `framer/DeepDive.tsx` | Content of each feature overlay |
| 7 | `AnnotatedInstrument` | `framer/AnnotatedInstrument.tsx` | The hero instrument with its markers |
| 8 | `FAQ` | `framer/FAQ.tsx` | Questions |

Each file begins with `andor-web framer bundle v1.0.0`. If Framer shows an error, check the
file name (imports use `./ANDOR_Tokens.tsx` and `./ANDOR_AudioController.tsx`).

## 5. Images
Upload all of `framer-upload/` to the project (drag into the canvas or Assets). File names start
with the section they belong to. Paste each image's **alt text** from `FRAMER_CONTENT.md` →
"Images to upload". Framer makes the responsive sizes itself, so upload these largest files only.

## 6. Shared components (native)
### Header (component "Site Header", variants Desktop / Phone)
- Desktop: a horizontal stack, height 72, padding 0 80, three regions.
  - Left: three pills ("Instrument" → `#first-note`, "Sounds" → `#sounds`, "Questions" → `#faq`):
    fill ANDOR/pill, radius 999, padding 8 16, Small text, gap 6.
  - Centre: wordmark "ANDOR", absolutely centred.
  - Right: pill "Join the waitlist" → `#waitlist`.
- Phone: wordmark left, a 44×44 round menu button right. It opens an Overlay (full-screen,
  ANDOR/bg) with the three links at 32 px and a "Join the waitlist" ink button.
- Hover: pill fill `#DFDDD6`. Focus: 2 px ANDOR/accent-strong outline.

### Button pair "Explore OSP"
- An outline pill (height 56; phone 52), padding 0 30, 1 px ANDOR/text border, Body 16/520,
  beside a 56 px ink circle with a ↗ arrow (6 px gap).
- Hover: the pill fills ink with paper text, and the arrow rotates 45°.
- Link: `#first-note`. The text link "▶ Hear it" sits beside it → `#sounds`.

### Bubble (component "Feature Bubble"). Used six times in the hero
- Vertical stack, padding 16 16 18, radius 28.
  - Fill: linear gradient `#F3F2ED` → `#ECEAE4`; shadow "Plate".
  - Head row: number badge (18 px ink circle, Geist Mono 10 paper), title (Mono label), and at
    the right a 20 px outlined "+" circle.
  - A line of Small text-2, then the plate: radius 18, fill radial `#FBFAF7` → `#F0EEE8`, padding
    12, with the image inside (radius 12, soft shadow).
- Hover variant: y −3, deeper shadow, "+" filled ink.
- Interaction: **Tap → Open Overlay** for its feature (§7.0). Content per bubble:
  `FRAMER_CONTENT.md` → "Hero · bubbles".

## 7. The page, section by section
Page name **OSP**, path `/`. Fill ANDOR/bg. Section IDs (for anchors) as named below.

### 7.0 Hero (`#top`)
- **Title**: centred Hero style, "A single sound." then a line break and "Somewhere entirely new."
  in text-quiet. Top padding 32 below the header.
- **Buttons**: button pair + "Hear it", centred, 24 below the title. No subheading.
- **Stage**: a grid of 3 columns `1fr 2.4fr 1fr` (gap 36 × 24).
  - Rows 1–2 left: Bubbles 1 (Your recording) and 2 (LIFE).
  - Centre (spanning 2 rows): **AnnotatedInstrument**.
  - Rows 1–2 right: Bubbles 3 (SYNTH + IMPRINT) and 4 (REIMAGINED).
  - Row 3: Bubble 5 (SPACE) left; Bubble 6 (Keyboard + arpeggiator) centre; the
    **AudioComparison** (theme Light, layout Row, no files) right, aligned to the bottom.
- **AnnotatedInstrument** settings:
  - Capture `01-hero__osp-hero-3l.webp`, Editor W/H 1448/1086, Markers after 1500 ms.
  - The six markers are prefilled with the measured positions; leave them.
  - Wire **Marker 1…6 → Open Overlay** "DD Your recording", "DD LIFE", "DD SYNTH + IMPRINT",
    "DD REIMAGINED", "DD SPACE", "DD Keyboard + arpeggiator".
- **Six overlays** (one per feature). Each is an Overlay containing one **DeepDive** instance,
  width 1040 (Phone: 100 %, anchored to the bottom):
  - Overlay settings: backdrop ANDOR/dark-bg at 38 % with background blur 6; close on click
    outside; transition opacity + y 14 → 0 over 520 ms, ease `0.22, 1, 0.36, 1`.
  - DeepDive: fill the Features list once (`FRAMER_CONTENT.md` → "DeepDive · Features"), then
    copy that instance into the other five overlays. Only **Start at (id)** differs:
    `recording`, `life`, `synth`, `reimagined`, `space`, `keyboard`.
  - Wire **On Close → Close Overlay**.
- **Tablet**: the instrument spans the full width; the bubbles below in 2 columns (1|3, 2|4, 6
  across, then 5 | player).
- **Phone**: the instrument at full width, then the bubbles as a horizontal scroll stack
  (each 80 vw, max 320, snap to start, gap 12, bleeding to the screen edges), then the player.
  Bubbles keep their own heights; do not stretch them.
- **Entrance** (Appear effects, once):
  - Header pills/wordmark fade + y 14, 520 ms, stagger 90 ms.
  - Title lines slide up from a mask, 900 ms, delays 90 and 180 ms.
  - Buttons 520 ms at 360 ms.
  - Instrument opacity 0 → 1 + y 18 → 0 + scale 0.985 → 1, 1400 ms, ease `0.33, 0, 0.15, 1`,
    delay 280 ms.
  - Bubbles opacity + y 10 + scale 0.97, 520 ms, delays 1100 + n × 110 ms.
  - Nothing loops afterwards.

### 7.1 Statement
- Left-aligned Statement style, max width 26 ch: "Every recording contains something worth
  discovering."
- Below it, in Lede-size text-quiet (max 44 ch): "A sung vowel, a bowed note, an old synth, a
  phone memo. OSP turns it into an instrument you want to play, and it still sounds like your sound."
- Padding: top 180, bottom 80.

### 7.2 First Note (`#first-note`)
- Kicker "THE FIRST NOTE", title "From one sound / to an instrument." (second phrase quiet).
- **Desktop**: two columns, 5 : 7.
  - Left: three chapter blocks, each min-height 72 vh, content centred vertically: Mono kicker
    in accent-strong ("01 · CAPTURE"), Feature title, Lede body, and the pending line
    "○ SOUND EXAMPLE IN PREPARATION".
  - Right: a **sticky** frame (top 12 vh) with a component "Chapter Picture" with three
    variants (Capture / Play / Transform), each showing its image in the "Object" style. Below
    it, a step row "01 Capture · 02 Play · 03 Transform" (the active one in text).
  - On each chapter block, add **Scroll → While in view (centre) → set Chapter Picture variant**,
    with a 520 ms crossfade. Inactive chapter titles are text-quiet, the active one is text.
    Don't fade the text with opacity; it would fail contrast.
- **Tablet and Phone**: no sticky. Each chapter is followed by its own image at full width.
- Content: `FRAMER_CONTENT.md` → "First Note · chapters".

### 7.3 Sounds (`#sounds`, dark)
- Section fill ANDOR/dark-bg, overflow clipped.
- Kicker "SOUNDS", title "Hear it." with "One recording, played three ways: the original, OSP and
  REIMAGINED." under it (half size, dark-quiet). Then a Small line: "These examples arrive with
  the first approved recordings. Each player is ready and will play the moment they do."
- **SoundDeck** at full width: items from `FRAMER_CONTENT.md`, Start at 2, Row below 640. Leave
  the files empty until the recordings exist (each card shows its honest pending state).

### 7.4 REIMAGINED (`#reimagined`)
- One **ViewSwitcher** (settings in `FRAMER_CONTENT.md`).
- Into its **Intro (slot)**, connect a native frame holding:
  - kicker "REIMAGINED";
  - title "Five instruments / hidden in every sample." (second phrase quiet);
  - Lede "One knob takes each layer from the recording itself toward its mode's far end, and the
    sound stays recognisably yours."

### 7.5 SYNTH + IMPRINT (`#synth`, dark)
- Section fill ANDOR/dark-bg with a soft radial glow `rgba(208,138,99,.12)` at 75 % / 50 %.
- **ViewSwitcher** (settings in `FRAMER_CONTENT.md`). Its intro slot holds:
  - kicker "SYNTH + IMPRINT";
  - title "Give a synthesizer / the soul of your recording.";
  - Lede "Every layer has an analog oscillator of its own. In IMPRINT mode it takes on the body of
    your recording, so you can add weight and sustain under an acoustic sound and keep its
    identity." (dark-text-2).
- Column ratio note: the component uses 5 : 7. That's fine; the prototype's 4 : 8 is only a
  little wider.

### 7.6 Layers + Granular (`#layers`)
- Header row, two columns:
  - left: kicker "LAYERS" and the title "One, two, three sounds. / One instrument.";
  - right: the **ViewSwitcher** (Stacked, Pills).
- Below it, Granular, two columns 7 : 5:
  - left: a plate (radius 28) holding `06-layers__gran-card.webp`;
  - right: kicker "GRANULAR", Feature title "Or turn any layer into a cloud.", body "Any layer
    can play as grains: position, size, density, tune, spread, scatter and pan, with three window
    shapes. With KEY DNA, each key gets its own grain behaviour.", and the pending line.

### 7.7 Details (`#details`)
- Section fill ANDOR/bg-2. Title "Made like an instrument, / down to the knobs."
- A 12-column bento of tiles (fill bg-elev, radius 24, padding 22), each an image plus a caption
  (Mono title, then Small text):
  - knob: span 3, 2 rows;
  - macros: span 9;
  - teak: 4;
  - display: 5;
  - mod: 5;
  - keybed: 7.
- Phone: every tile full width; the knob image max 260 centred.
- Content: `FRAMER_CONTENT.md` → "Details".

### 7.8 Deeper features (`#features`)
- Title "Simple to start. / Deep when you want it."
- **Lead**: Note Atlas, image plate 7 : text 5.
- **Pair**: Live Capture | KEY DNA, side by side, each image plate over text.
- Then the "deeper" list: 2 columns of rows, each with a hairline on top, the title (Body 560)
  and a Small text-2 description.
- Phone: one column throughout.

### 7.9 Technical (`#tech`)
- Two columns, 4 : 8: the title "The details.", then a list of rows (Mono label in text-muted,
  value in Body), each with a top hairline. Values: `FRAMER_CONTENT.md` → "Technical".
- Check every value against `docs/marketing/features-and-benefits.md` §7 before publishing.

### 7.10 Waitlist (`#waitlist`)
- A dark panel: radius 32, fill dark-bg with a radial glow, padding 72 (phone 28).
- Content: title "Be first to play it." and the Lede "Leave your email and we will write when ANDOR
  OSP is ready, with the first sound examples. Pricing and the release date will be announced
  together."
- Use a **native Framer Form**: an email field (pill, 52 high, translucent fill) and the button
  "Join the waitlist" (paper fill, ink text). Success message: "Thank you. We will write when
  it's ready."
- **Owner decision (Q-04):** where submissions go. Framer's own form submissions work with no
  third-party provider. MailerLite is connected to the workspace but unused. Until decided, the
  form must not be on the live domain.

### 7.11 FAQ (`#faq`)
- Two columns, 4 : 8: the title "Asked often.", then the **FAQ** component with the questions from
  `FRAMER_CONTENT.md`.

### 7.12 Footer (dark)
- Brand on the left: "ANDOR" and "Instruments made from the sounds you love."
- Four link columns:
  - Instruments: ANDOR OSP → `/`; ANDOR ANALOG **soon** (plain text, not a link).
  - On this page: Sounds, REIMAGINED, SYNTH + IMPRINT, Questions (`/#sounds` etc. so they work
    from every page).
  - Support: Installation → `/installation`, Documentation → `/documentation`, Contact → `/contact`.
  - Legal: Privacy → `/privacy`, Terms → `/terms`, Licence → `/licence`.
- Make the footer a native component so the six support and legal pages (§13) share it.
- Fine print: "© 2026 ANDOR. ANDOR OSP is in development; pictures show the current development build."

## 8. Motion
Set these as Framer transition presets:
- **Out**: `0.22, 1, 0.36, 1`
- **In-Out**: `0.65, 0, 0.35, 1`
- **Soft**: `0.33, 0, 0.15, 1`

Durations: 160 (hover), 280 (controls), 520 (text, crossfades), 900 (masked lines), 1400 (the
instrument). Only opacity and transform animate. Turn on **Reduce motion** handling (Framer
respects the OS setting for Appear effects; the code components do too). Nothing moves forever.

## 9. Keeping Framer and the repo in step
1. Change code **only in the repo** (`andor-web/framer/`). Bump `ANDOR_VERSION` in
   `ANDOR_Tokens.tsx` and the `bundle vX` header of every changed file.
2. Run `npm run framer:check`: it verifies the headers and that the tokens equal `tokens.css`, then
   writes `framer/MANIFEST.json`. Then run `npm run framer:test`.
3. Paste the changed files into Framer, replacing their contents.
4. To confirm Framer matches the repo, copy a file's contents out of Framer into a temp file, run
   `node tools/framer-check.mjs --hash <file>`, and compare with `framer/MANIFEST.json`.
5. If someone edits code inside Framer, copy it back into the repo first, then follow steps 1–3.

Content (texts, images, audio files) lives in Framer's controls and may be edited there freely.
Mirror lasting copy changes into `prototype/osp/content.ts` and re-run `node tools/framer-pack.mjs`
so the appendix stays true.

## 10. Audio, when the recordings arrive
For each example:
1. Align the three versions sample-accurately.
2. Loudness-match them (measure LUFS; record the values).
3. Encode each as AAC `.m4a` (all browsers) plus Opus `.webm` as the fallback.
4. Host the files on a managed CDN, not raw GitHub URLs.
5. In Framer, set Version 1–3 file and fallback on the AudioComparison / SoundDeck item.
6. Paste peaks from `npm run peaks -- <file>` (the OSP version).

## 11. Publish to staging and QA
1. Publish to the **framer.app staging subdomain** only. Keep indexing off.
2. Check on the published URL, not the canvas, at 1440, 1280, 1024, 768, 390, 375 and 320:
   - no sideways scroll;
   - every link lands on a section;
   - markers and bubbles open the right overlays;
   - Escape and clicking outside close them;
   - tabs work with the arrow keys;
   - the FAQ opens;
   - the form shows its success state;
   - reduced motion is respected.
3. Lighthouse on the published URL: Performance ≥ 90, Accessibility ≥ 95, CLS ≤ 0.1. Record the
   results in `docs/QA_CHECKLIST.md`.
4. Product accuracy: every image is from `framer-upload/` (commit `c49d77f`); no stand-in waveform
   goes to the production domain (re-capture with the recordings first: `tools/capture.sh`).

## 12. Known differences from the prototype
- **Overlays.** The prototype uses a native `<dialog>`; in Framer the overlay is native, and
  DeepDive adds its own focus trap and Escape. Framer may not return focus to the marker after
  closing. Acceptable, but check it.
- **First Note.** Scroll behaviour is Framer's "while in view" variant switching, not the
  prototype's observer. Check that the switch happens near the middle of the screen at
  1024–1440 heights 700–1000.
- **Header.** The prototype's header shares React code with the page. In Framer it is a native
  component, so keep its text and links in step by hand.

## 13. Support and legal pages
Six native pages, one template. The text lives in the repo, in `content/pages/*.md` (Markdown with a
small frontmatter: `title`, `kicker`, `summary`, and `draft` on the legal pages). The prototype
renders the same files (`installation.html` … `licence.html`).

| Page | Framer path | Group |
|---|---|---|
| Installation | `/installation` | Support |
| Documentation | `/documentation` | Support |
| Contact | `/contact` | Support |
| Privacy | `/privacy` | Legal |
| Terms | `/terms` | Legal |
| Licence | `/licence` | Legal |

**Template** (make the first page, then duplicate it):
- Site Header (the Support pill current on all six) and the footer component.
- Hero: kicker (Mono Label), title (Section), summary (Lede, max 46ch). Top padding 120/80/48.
- Two columns, 3 : 9, gap 80 (one column below 900 px):
  - **Side** (sticky, top 24): the group's three pages as a link list, the current one on an
    Elevated fill; under it "On this page" with one link per `##` heading (anchor = the heading
    in lower case with dashes, e.g. `#who-is-responsible`). On phones: the three pages as pills,
    "On this page" as a folded accordion.
- **Body**: Framer's Rich Text, max width 70ch, body size, line height 1.65.
  - `##` → H2 at 22–28 px with a hairline above.
  - Tables become a Rich Text table, or a stack of two-line rows on phones.
  - The licence texts are code blocks: Mono, 13 px, Secondary fill, wrap on.
- Paste each page's Markdown into Rich Text (Framer converts headings, lists, links, tables and
  code when pasted as Markdown; check tables by eye). Links in the files are written as Framer paths
  (`/contact`, `/installation#uninstall`).

**Before publishing anything but staging:** every `[[…]]` is a placeholder the owner must fill
(the prototype highlights them and counts them per page), and the three legal pages carry
"Draft for legal review. Not in force." They need a lawyer's review before they go live. Remove the
draft note only then.
