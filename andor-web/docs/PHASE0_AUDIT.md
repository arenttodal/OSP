# Phase 0 — Foundation and source audit

Gate G0: verified foundation and source inventory. Date: 9 Nov 2026.

## 1. Repositories and branches

| Item | Finding |
|---|---|
| Repository | `arenttodal/OSP` |
| `claude/synth-imprint` | Tip `02465c7` ("SYNTH chip spacing and Refined Core note colours"). Defines SYNTHESIZE S1 (analog oscillator), S2 (spectral IMPRINT + MOTION), the SYNTH chip, playback-mode symbols, and the marketing source doc `docs/marketing/features-and-benefits.md`. |
| `claude/charming-lovelace-n25fte` | Tip `c49d77f` = merge of `claude/synth-imprint` (both commits ahead are merges of it). **Contains everything in `synth-imprint`.** |
| Website work branch | `claude/friendly-pascal-0y48r0`, created at `c49d77f`. All captures are from this commit. |
| Product commit for every asset | **`c49d77f`** (`assets/manifests/assets.json` → `product.commit`). |
| `arenttodal/analog` (ANDOR ANALOG) | Separate repo using OSP's shared code. Out of scope for the first release; the site architecture leaves room for it (`docs/DESIGN_SYSTEM.md` §7). |

Where the website lives: `andor-web/` inside the OSP repository on the work branch, isolated from all
DSP/plugin code (nothing under `src/`, `apps/`, `shared/` is modified). Only one plugin-side file
was added: the hidden capture test `tests/plugin/WebCaptureTests.inc` (+1 include line). It
renders the real editor into PNGs, like the repo's other hidden screenshot tests. It changes no
product behaviour. **Decision D-01**: a separate `andor-web` repository is cleaner long term; this
session cannot create repositories, so moving it out is listed in the backlog.

## 2. Product capability inventory (at `c49d77f`)

Status sources: `docs/STATUS.md`, `docs/marketing/features-and-benefits.md` §7, and the real
editor captured in this phase. "Functional" means implemented and covered by automated tests.
Per the plan, a passing unit test is **not** treated as musical approval.

| Feature | Origin branch | Functional | Listening / host status | Capture now | Audio demo | Marketing readiness |
|---|---|---|---|---|---|---|
| Drop one file, root detection | main line | Yes | — | `web-00-empty`, `web-01-hero-1L` | **None** | Ready (needs recording) |
| LIFE (performance engine) | main line | Yes | Blind test beat identical repeats on non-vocal sources | `crop-knob-life` | None | Ready; say "designed to" for vocals |
| Continuation (loop-free sustain) | main line | Yes | 60 s holds rated 4–5/5 on non-vocal sources | (not visible in UI) | None | Needs audio; vocals weaker |
| Dynamics (velocity → timbre) | main line | Yes | **Mixed** verdict on sustained sources | — | None | Say carefully |
| Pitch characters (Tape/Natural) | main line | Yes | Chosen by blind listening | — | None | Ready |
| REIMAGINED: Kaleidoscope, Tape Frame, Toybox, Mosaic, Mirage | main line | Yes | Range rebuilt after a mixed verdict | `web-02-reimagined-1…5` | None | Captures ready; audio required |
| Granular + KEY DNA | main line | Yes | — | `web-04-granular-1L` (controls strip **did not stage**, see §5) | None | Re-capture needed |
| SYNTHESIZE (Analog) | `synth-imprint` | Yes | — | `web-03-synth-2-analog` | None | Captures ready |
| IMPRINT (+ MOTION) | `synth-imprint` | Yes | **Measured on stand-in references; real-recording verdict pending** | `web-03-synth-3-imprint`, `…-4-imprint-motion` | None | **Blocked on listening approval** |
| Three layers (A/B/C) | main line | Yes | — | `web-01-hero-2L`, `web-01-hero-3L` | None | Ready |
| Live Capture (REC / GRAB LAST) | main line | Yes | — | not yet captured (P2) | None | P2 |
| MAP NOTES / Note Atlas | main line | Yes | — | not yet captured (P2; `[refine-shots]` has states) | None | P2 |
| Macros: DRIVE, CHARACTER, MOVEMENT, ECHO (ASCEND), SPACE (4 engines) | main line | Yes | — | `web-05-space`; ECHO popover **did not stage** | None | P2 |
| Modulation (2 LFO, 2 ENV, 16 routes) | main line | Yes | — | partly visible in hero (AMP panel) | — | P2 |
| Arpeggiator (per card) | main line | Yes | — | not captured | — | P2 |
| Library, Inbox, packs | main line | Yes | — | not captured | — | P2 |
| Presets, `.ospinstrument`, exact recall | main line | Yes | — | — | — | Copy only |
| Platforms: AU/VST3/Standalone macOS | main line | Builds | **Host validation in Logic/Ableton/Reaper not done; unsigned, no installer** | — | — | **Do not claim** until tested |
| Windows VST3 | CI | Builds + tests in CI | Not distributed | — | — | Do not claim |
| Factory sounds | — | **None exist** | — | — | — | Blocker for demos |

## 3. Asset inventory

| Class | Status |
|---|---|
| GUI captures | **35 genuine captures** at `c49d77f`, ceramic look, 1x/2x (hero also 3x), produced by `[web-capture]`. Masters are reproducible (`tools/capture.sh`) and are not committed. |
| Detail crops | 7 crops cut from the masters by manifest coordinates (display, macros, LIFE knob, keybed, teak/logotype, IMPRINT display, MOSAIC popover). |
| Studio renders | 3 renders (`object-1l`, `object-3l`, `object-imprint`) from `tools/render-object.py`. The genuine capture is the face (emission, exact colours). The slab's sides are textured from the capture's own teak cheek. No controls, ports or hardware were added. **Decision D-04 needs owner approval** (§ DECISIONS). |
| Older repo screenshots (`andor-design-system/guide/img/osp`, 30 Oct) | **Rejected for marketing**: they predate SYNTH/IMPRINT and the mode symbols. |
| Waveform content in captures | **Stand-in synthetic test signals**, not recordings (vowel C4, pluck A3, vowel C3). The display looks authentic because it is the real waveform renderer, but the sound is a test signal. All captures must be re-shot with the approved recording before launch (`OSP_WEB_SOURCES`). |
| Video | None. Interaction recordings need a real-time standalone session (macOS) or a scripted headless frame sequence. Backlog. |
| Audio | **None.** See §4. |

## 4. Recordings (blocker)

No audio file exists in either repository. The research corpus (~80 WAV/AIFF, including the
bowed nyckelharpa) is git-ignored and local-only by design (`docs/corpus.md`: rights and size).
The marketing doc states: *"Factory sounds: none yet. Example content needs legally owned recordings."*

Consequences, per the stop conditions in plan §44:
- The hero player is fully built. It shows an honest **pending** state ("Recordings in
  preparation") and plays nothing. No placeholder audio, synthetic tones or borrowed recordings
  appear on any page.
- Rendering the OSP and REIMAGINED versions is mechanical once a source exists. The capture test
  takes real files via `OSP_WEB_SOURCES`, and audio rendering goes through the plugin or
  `research-renderer`.

**Needed from the owner (A-01):** 1–3 short source recordings you own, ideally the nyckelharpa
note named in the plan, plus a cello, voice or analog synth note. WAV/AIFF, 5–20 s, with a line
on ownership/licensing.

## 5. Capture issues found (for the next capture pass)

- `web-04-granular-1L`: `showGranularControls (true, false)` did not show the controls strip in
  the captured state. Investigate the card API (`DesignCaptureTests.inc` line ~165 does it differently).
- `web-05-echo-ascend`: `openPopup (echoPopup)` produced no visible popover. Likely needs the macro
  row's ECHO slot visible first (the `[space-shots]` path works for SPACE).
- Playheads and held-key tint are genuine (the processor was run for 0.6 s with a held chord).

## 6. Reference analysis

### thelibrary.fm (primary benchmark; rendered here at 1440 and 390 px)
- **Structure.** Centred hero: a pill eyebrow, a 64 px headline with a rotating last word, an
  18 px lede, and two CTAs (filled light, ghost). The real product screenshot follows in
  perspective, fading into black. Then logo proof, and a statement section in larger centred type.
- **Rhythm.** Alternating copy/screenshot rows. Each screenshot sits on a vivid gradient plate.
  A scroll-revealed statement is lit word by word. A grid of feature cards, the FAQ, the
  waitlist, and the footer.
- **Type.** Inter Display / Inter (body 18 px at 80 % white), H2 48 px with −1.92 px tracking,
  amber eyebrow labels at 14 px.
- **What to take.** Real product imagery as the design material. One confident statement per
  section. Generous dark space. Product reveals with perspective. Restraint in UI chrome.
- **What not to take.** The generic SaaS gradient plates, the uniform card grids, and the
  centred-everything rhythm. They read as a template, and the plan forbids template layouts and
  undifferentiated card grids.

### Owner's additional references (Teenage Engineering EP-133 / TP-7, dark cards)
- **Depth.** The product reads as a manufactured object with real thickness, in controlled
  isometric perspective → the studio-render pipeline (`object-*`) and H1.
- **Type.** A tight neo-grotesk, two-tone headlines (ink + grey), and monospace technical labels →
  `--text-quiet`, `--font-mono` (Geist Mono), `.t-mono`.
- **Elements.** Outline pill + ink circular arrow → `.btn-pair`. Thumbnails of product views
  beside the object → H1's state column. Light neutral field with a framed hero.
- **Dark tilted cards.** Kept for the Sounds/AudioGallery and REIMAGINED sections (later phases).

## 7. Initial production backlog

| # | Item | Priority | Blocked by |
|---|---|---|---|
| B1 | Approved source recording(s) → re-capture all GUI states with `OSP_WEB_SOURCES` | P0 | **A-01 owner** |
| B2 | Render ORIGINAL / OSP / REIMAGINED, align, loudness-match (LUFS), encode Opus + AAC, precompute peaks | P0 | B1 |
| B3 | Fix granular and ECHO capture staging; capture Live Capture, Note Atlas, Arp, Library, Modulation | P1/P2 | — |
| B4 | Hero motion: final storyboard and timing once the direction is chosen | P0 | Gate H |
| B5 | Framer project setup: styles, components, code components (`docs/FRAMER.md`) | P1 | Framer access (see DECISIONS) |
| B6 | Interaction video (real-time GUI recording) | P1 | macOS session or frame-sequence tooling |
| B7 | Move `andor-web` to its own repository | P2 | Owner creates the repo |
| B8 | Verify platform claims (host validation) before any compatibility copy | P1 | Product team |
| B9 | Waitlist provider choice (no expense committed) | P2 | Owner |
