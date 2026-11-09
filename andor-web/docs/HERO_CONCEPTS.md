# Hero concepts — Gate H

## Round 3b (9 Nov 2026): E · open bubbles

Owner: a version where the deep dives don't need a click. They float next to the feature titles
as smaller bubbles, already open on the front page, and clicking one opens the full modal.

`g5.html`: the three-layer instrument in the centre, with six floating bubbles around it on one
grid. Left: 1 Your recording (the real drop area), 2 LIFE. Right: 3 SYNTH + IMPRINT (the Imprint
page), 4 REIMAGINED (MOSAIC). Beneath: 5 SPACE, 6 Keyboard + arpeggiator (the arp panel with the
keyboard), then the player. Each bubble is a soft rounded plate with the genuine popover or panel,
a number matching the marker on the instrument, a title and one line. The whole card is one
button (stretched hit area) that opens `FeatureModal` at that feature, as do the markers. The
bubbles open in sequence after the instrument settles. Tablet: two columns beneath the
instrument. Phones: a swipeable snap row in its own scroller (the page never scrolls sideways).
QA: 7 widths clean; E checks (six bubbles open, bubble and marker open the modal, focus returns,
phone row, bottom sheet) pass with the full suite (37/37).

## Round 3 (9 Nov 2026): Gallery C, chosen, in two versions

Owner: go with C, use B's header area and no subheading, show the app with three layers (not a
single oscillator/sound), and add a version where the features open rounded deep-dive modals,
with LIFE's macro controls, the REIMAGINED modes, SYNTH/IMPRINT, and the arpeggiator on the keyboard.

| Version | Page | What it is |
|---|---|---|
| **1 · Notes** | `g3.html` | B's panel header (pill navigation, centred ANDOR wordmark, pill waitlist), title with no subheading, then Explore OSP and Hear it. The real three-layer capture, five notes pinned to measured component bounds, and the facts strip with the player. |
| **2 · Deep dive** | `g4.html` | The same hero, with six numbered parts as buttons: Your recording, LIFE, SYNTH + IMPRINT, REIMAGINED, SPACE, Keyboard + arpeggiator. Each opens `FeatureModal`: a rounded sheet with the genuine popover or panel, tabs where there are several states (5 REIMAGINED modes; Analog / Imprint / Imprint + Motion; Before / Three layers), facts and prev/next. Desktop: popover beside text. Phones: a bottom sheet. |

Evidence: notes pin to `web-08-hero-3L-bounds.json`, the component bounds of the real editor.
Modal media are crops of new captures `web-06-*` (LIFE, SPACE and the five REIMAGINED popovers at
3x, cut to their card from recorded bounds) and `web-07-arp-3L` (the arpeggiator with the
keyboard). QA: 7 widths × 2 pages clean, with the CTA in view at every width; 13 deep-dive
interaction checks (marker, tabs, arrow keys, next/prev, Escape + focus return, backdrop, phone
legend, bottom sheet) plus reduced motion pass.

## Round 2 (9 Nov 2026): The Gallery in three editions

Owner feedback on round 1: prefers **The Gallery**, without the serif. Wanted to see it in three
editions: one with a font closer to the references (or Open Sans, Inter or similar), and one with
more context around the object instead of an empty left side.

| Edition | Page | Typeface | What changes |
|---|---|---|---|
| **A · Quiet** | `g1.html` | Inter Display (Inter's optical size axis, variable, OFL) | Round 1's museum composition in sans with a two-tone title. The left placard now carries content: kicker, lede, three short points and the CTA. |
| **B · Panel** | `g2.html` | **Geist** (variable, OFL): the closest free neo-grotesk to the Teenage Engineering references | A framed, softly lit panel on a warm grey ground. Pill navigation, the wordmark centred, a large centred title with a two-tone first word, a mono statement lower left, actions and the player lower right. |
| **C · Context** | `g3.html` | Inter Tight (the system face) | The annotated object. Five notes are pinned to real controls in the capture (the recording, LIFE, SYNTH + IMPRINT, REIMAGINED, the keyboard), each with a hairline to the exact point. A strip of real numbers sits beneath (1 recording, 3 layers, 5 REIMAGINED modes, 6 macros, 4 SPACE engines, 16 modulation routes), with the player beside it. On phones: numbered markers on the image plus a legend. Screen readers get the legend at every width. |

All three share `prototype/gallery/gallery.css`. QA at 7 widths each: no overflow, no errors, no
missing media, CLS ≤ 0.0006. The CTA is visible in the first viewport everywhere except B at
375×667 / 320×568 and A at 768×1024. Reduced motion settles at once.

Annotation accuracy: note points are 1x editor coordinates of the controls in the capture. Lines
are measured against the untransformed frame, and the notes fade in after the object settles.

Round 1 follows for reference.

Three distinct compositions with one shared design system. Each one is a working page
(`h1.html`, `h2.html`, `h3.html`) with art-directed mobile layouts. All product imagery is
genuine: captures of the editor at `c49d77f`, or a studio render with that capture as its face.

## H1 — The Instrument (recommended)
- **Idea.** The instrument as a manufactured object. A studio render stands to the right with
  real depth: the face is the exact capture, and the sides carry the interface's own teak. The
  type sits to the left in two tones. A contact sheet beside the object shows its real states:
  one layer, three layers, IMPRINT.
- **Desktop.** Mono eyebrow with the two-disc mark → "A single sound." (ink) / "Somewhere entirely
  new." (quiet) → lede → ButtonPair → compact player. The object is sized by viewport height and
  anchored by its own right edge, so its shadow runs under the copy.
- **Mobile.** Type and CTA first. Then the object at 150 % width, centred on the instrument
  itself, then the state thumbnails in a row, then the player.
- **Motion.** Masked line rise → the object rises and settles (1.4 s, soft ease) → states and
  player arrive. The states crossfade in 520 ms.
- **Risk.** Needs decision D-04 (accept presentation renders). Without it, the concept loses its depth.

## H2 — The Sound
- **Idea.** Sound first. The page opens on a close-up of the recording's waveform, alone in the
  dark. After 1.5 s the frame pulls back on the same capture (a matched cut, 1.7 s), and the
  instrument is around it. Time-based and played once; no scroll-jacking.
- **Desktop.** Copy left: Inter Tight line plus Garamond italic line, "Hear it" beside the
  ButtonPair. The stage on the right bleeds off the edge, with a mono caption stating the real
  state captured.
- **Mobile.** The same cut, edge to edge, under the copy.
- **Risk.** The settled frame is a flat capture, so the still image is less distinctive than the
  motion. LCP lands later (~0.9 s locally) because the image fades in.

## H3 — The Gallery
- **Idea.** Museum restraint. A centred serif title and one object lit like a print. Placards to
  either side: the lede and CTA on the left; the object label and player on the right. Three real
  details hang below (LIFE knob, teak logotype, MOSAIC).
- **Risk.** At this restraint the object is ~735 px wide on desktop: elegant but less immediate.
  On short phones (≤ 667 px tall) the CTA falls just below the fold.

## Comparative evaluation (plan §31, 1–10)

| Criterion | H1 | H2 | H3 |
|---|---|---|---|
| Distinctiveness | **9** | 8 | 7 |
| Art direction | **8.5** | 8 | 7.5 |
| Typography | 8 | **8.5** | 8.5 |
| Product presentation | **9** | 7.5 | 7 |
| Motion | 7.5 | **9** | 7 |
| Musical storytelling | 6 | **7** | 6 |
| Responsiveness | **8.5** | 8 | 8 |
| Accessibility | 9 | 9 | 9 |
| Performance | 8.5 | 8 | **9** |
| Brand cohesion | **9** | 8 | 8 |

Musical storytelling is capped for all three until real recordings exist. No concept reaches the
launch bar (≥ 9 on art direction **and** product presentation) yet. For H1 the remaining gap is:
(1) real recordings, re-captured, (2) a final art-direction pass on the eyebrow/mark and the player
placement, (3) a lighting pass on the render (a slightly warmer key, an EP-133-like rim on the teak).

## Measured (prototype, Chromium)
- 7 widths × 3 concepts: no horizontal overflow, no console errors, no missing media; CLS
  ≤ 0.029; headline and product in the first viewport everywhere; CTA in view everywhere except
  H3 at 375×667 and 320×568.
- First load: H1 550 KB / 482 KB (desktop @2x / mobile @3x), H2 518 KB, H3 427 / 385 KB.
- Interactions: state switcher (pointer + arrows), menu (modal, Escape, focus return), CTA anchor,
  and reduced motion (settled immediately) all pass. Audio component: 13/13 functional checks.
- Contrast: 25/25 token pairings pass AA.

## Recommendation
**H1, The Instrument.** It makes the product desirable within the opening viewport, which is the
hero's first goal. It is the most distinctive, and it answers your references directly. It also
gives the rest of the site a vocabulary: the object, its states, and the tactile depth. If you
don't accept studio renders (D-04), choose **H2**. Its opening frame is pure genuine capture and
its motion carries the story.
