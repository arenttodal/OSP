# QA checklist

Automated now:
- `node tests/accessibility/contrast.mjs`: every token pairing against WCAG AA (25 checks).
- `node tests/visual/shoot.mjs [h1 h2 h3]`: each page at 1440×900, 1280×800, 1024×768, 768×1024,
  390×844, 375×667, 320×568. It records a screenshot, horizontal overflow, console errors, CLS,
  headline/CTA/product visibility in the first viewport, and missing-media count
  (`tests/visual/out/report.json`).

Per release (manual or to automate):
- [ ] Navigation, menu open/close (Escape, focus return), every button destination
- [ ] Audio: play/pause, variant switch keeps position, seek (pointer, keyboard), exclusivity
      across players, loading/error, unmount cleanup; Safari (desktop + iOS), Chrome, Firefox
- [ ] Keyboard-only pass; visible focus everywhere
- [ ] Screen reader pass (VoiceOver): headings, image alts, player labels
- [ ] Reduced motion: settled states, nothing essential hidden
- [ ] Images sharp at 1x/2x/3x DPR; no clipped UI text in crops
- [ ] Lighthouse (published URL): Performance ≥ 90, Accessibility ≥ 95; LCP ≤ 2.5 s, CLS ≤ 0.1, INP ≤ 200 ms
- [ ] Product accuracy: every capture matches the current build; no stand-in waveforms at launch
- [ ] Claims: platforms, pricing, listening statements checked against `features-and-benefits.md` §7
