# The ANDOR OSP landing page (`osp.html`)

Built after Gate H (hero E approved, 9 Nov 2026). Source: `prototype/osp/` (`main.tsx`, `Hero.tsx`,
`sections.tsx`, `content.ts`, `osp.css`). All copy lives in `content.ts` and follows
`docs/marketing/features-and-benefits.md`, including its §7 on what to say carefully.

## Sections

| # | Section | Anchor | Product imagery (genuine) | Interaction | Audio |
|---|---|---|---|---|---|
| 0 | Hero E | `#top` | three-layer capture; six bubbles (`bub-*`, `dd-*`) | markers + bubbles open `FeatureModal` | pending |
| 1 | Statement | — | — | — | — |
| 2 | First Note: Capture → Play → Transform | `#first-note` | `osp-empty`, `osp-hero-1l`, `osp-reimagined-mosaic` (same framing, crossfade) | desktop: sticky stage driven by the chapter at mid-screen; phones: scenes in order, no sticky | pending per chapter |
| 3 | Sounds | `#sounds` | none (typographic cards; no recordings exist) | deck of tilted dark cards, prev/next; phones: swipe row | players ready, pending |
| 4 | REIMAGINED | `#reimagined` | the five popovers `dd-reimagined-*` | mode list (tablist, arrows/Home/End) | pending |
| 5 | SYNTH + IMPRINT (dark) | `#synth` | `syn-card-sample/-analog/-imprint/-motion` (layer A card, SYNTH page) | 4-state selector | pending; "IMPRINT is new" note |
| 6 | Layers + Granular | `#layers` | `osp-hero-1l/2l/3l`, `gran-card` | 1/2/3 selector | pending |
| 7 | Instrument details | `#details` | `crop-knob-life`, `crop-macros`, `crop-teak`, `crop-display`, `det-mod`, `crop-keybed` | — | — |
| 8 | Deeper features | `#features` | `feat-notemap`, `feat-capture`, `feat-keydna`, then a text list | — | — |
| 9 | Technical | `#tech` | — | — | — |
| 10 | Waitlist | `#waitlist` | — | form; says nothing was sent (no provider yet) | — |
| 11 | FAQ | `#faq` | — | native `<details>` | — |
| 12 | Footer | — | — | in-page links; unbuilt pages marked "soon", not linked | — |

## Claims and their evidence

- Formats: "AU, VST3 and Standalone for macOS … at release". Builds exist; host validation is
  pending, so the copy says "to be validated in Logic Pro, Ableton Live and Reaper".
  **Windows: "not announced yet"** (a CI build exists but nothing is promised).
- Sustain: "without an obvious loop", which matches STATUS (60 s holds, mostly inaudible on non-vocal sources).
- IMPRINT: described by what it does. No sound-quality claim (its listening verdict is pending).
- Numbers: 3 layers, 5 REIMAGINED modes, 6 macros, 4 SPACE engines, 16 routes, 18 arp patterns,
  MPE ±48 semitones, 44.1–96 kHz, UI scale 80–200 %. All from the features doc.
- No prices, dates, testimonials or "first/only" claims.

## Open items (owner)
1. **Recordings (A-01).** Every player is pending; the captures show stand-in waveforms.
2. **Waitlist provider (Q-04).** The form is honest that nothing is sent. Choose a provider
   (MailerLite is connected to this workspace, unused) before going live.
3. **Support and legal pages**: Installation, Documentation, Contact, Privacy, Terms, Licence.
4. **Framer (Q-02).** Implementation route.

## Tests
`tests/visual/interaction.mjs` covers the landing page: every in-page link has a target,
REIMAGINED (click + arrow keys), SYNTH (click + End), layers, the sound deck, FAQ, the waitlist
message and a hero bubble opening its modal. `tests/visual/shoot.mjs osp` checks 7 widths
(no overflow, no errors, no missing media, CLS ≤ 0.001). `tests/visual/fullpage.mjs osp` takes
full-page screenshots at 1440 and 390. First load: 873 KB (desktop @2x) / 587 KB (phone @3x).
