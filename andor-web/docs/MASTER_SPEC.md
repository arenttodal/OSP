# ANDOR — Website Master Build Plan (condensed working copy)

The owner's "ANDOR — Website Master Build Plan v1.0" is authoritative. This page is the working
digest the implementation is checked against. If the two differ, the owner's document wins.

**Mission.** A bespoke launch experience for ANDOR OSP. The instrument is the hero; the sound
tells the story. Benchmark: thelibrary.fm (translated into ANDOR, not copied). Platform: Framer
plus a minimum of custom React.

**Positioning.** ANDOR OSP, One Shot Performer: one recording becomes a playable, expressive
instrument. Emotional line: *Every recording contains something worth discovering.* Write about
creative motivation, not DSP.

**Site map (v1).** OSP landing: Hero → First Note Story → Audio Demonstrations → REIMAGINED →
SYNTH + IMPRINT → Layering & Granular → Instrument Details → Technical → Purchase/Waitlist → FAQ.
Plus Sounds, Support (Installation, Docs, Contact), Legal. Room for ANALOG later.

**Design system.** Tokens §6 (refined in `DESIGN_SYSTEM.md`). Inter Tight + EB Garamond sparingly.
Hero 76–104 / 46–58 px; section 48–70 / 34–44; feature 30–46 / 27–34; body 16–18 / 15–17; labels
11–12. Max content 1360; gutters 64–80 / 32–48 / 20–24; 12 columns; sections 110–160 px.

**Product presentation.** Genuine interface, accurate content, correct proportions, warm light,
contact shadows, controlled perspective. No fake hardware or invented controls. A shared
ProductMedia component (capture, studio, detail, video, fallback). Every scene has an explicit
source, and a missing one is never silently replaced.

**Hero.** Headline "A single sound. / Somewhere entirely new." Lede "Turn the smallest musical
moment into an instrument that feels alive." CTAs: Explore OSP, Hear it. Sequence: nav → headline
→ plugin emerges → audio active → settle; nothing perpetual. Variants H1 Instrument / H2 Sound /
H3 Gallery; recommend one, no compromise. Acceptance: product visible at desktop and mobile;
proposition without scrolling; readable text; no layout shift; working CTA; no hover dependence
on mobile; audio controls play real recordings. **Gate H before anything else.**

**Audio.** AudioComparison: one timeline, synchronized variants, switching keeps position, no
overlap, seek, play/pause, touch, keyboard, loading/error, cleanup, format fallback, no autoplay.
Web Audio for synchronized switching. Precomputed peaks. Loudness-matched (measured). A global
controller makes playback exclusive.

**Asset rules.** Captures from the verified current build (`claude/synth-imprint` features);
manifest with id, source, commit, approval, use, format, fallback, licence. Reject wrong states,
DAW chrome, tiny UI, wrong samples, fake labels, distortion.

**Responsive.** 1440, 1280, 1024, 768, 390, 375, 320. Mobile individually art-directed; no hover
actions; no horizontal overflow; ≥ 44 px targets; no scroll traps.

**QA targets.** WCAG AA; keyboard; focus; reduced motion; Lighthouse Perf ≥ 90, A11y ≥ 95; LCP ≤
2.5 s, CLS ≤ 0.1, INP ≤ 200 ms. Review scores 1–10; anything below 9 on art direction or product
presentation is revised before launch.

**Agent limits.** May not change DSP, invent features, change brand fundamentals, publish to
production, commit expenses, activate checkout, invent testimonials/pricing, or fabricate
screenshots. Stop and flag when evidence, recordings or commercial facts are missing.

**Phases.** 0 Foundation (G0) → 1 Direction + design system (G1) → 2 Hero assets (G2) → 3 Framer
hero (G3) → 4 Audio system (G4) → 5 Narrative sections (G5) → 6 Supporting content (G6) → 7
Commerce + brand (G7) → 8 Polish + launch (G8).
