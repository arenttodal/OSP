# Framer integration plan

Framer owns layout, responsive breakpoints, CMS, navigation, motion and publishing. This repo owns
the custom code components, the asset pipeline, tests and docs (plan §17–20).

## Status
The empty project exists (`Andor--wzk8dDluBfoaTaPRcn4R`). This cloud session cannot reach
framer.com, and editing needs the owner's signed-in browser. Nothing has been changed in Framer.
The existing The Library project is untouched.

## After hero approval (Gate H)
1. **Styles.** Create Color Styles and Text Styles named as in `docs/DESIGN_SYSTEM.md` §1–2 (values
   from `shared/tokens/tokens.css`). Upload Inter Tight (variable), EB Garamond 400/400i and
   Geist Mono 400/500 as custom fonts (OFL), or use Framer's Google-hosted Inter Tight.
2. **Breakpoints.** Desktop 1440 (primary), 1280, Tablet 768–1023, Phone 390 (check 375/320).
3. **Components (native Framer).** SiteHeader (light/dark variants + mobile menu), ButtonPair,
   PrimaryButton, SecondaryButton, TextLink, SectionHeading, MediaCaption.
4. **Code components (from this repo).** `AudioComparison` (+ `GlobalAudioController`) and
   `ProductMedia` where art direction needs srcset control. Each file is self-contained React. To
   port: copy the `.tsx` and its CSS (inlined as a style object or a `<style>` tag) into a Framer
   code file. Expose props as property controls: demo fields (title, labels, file URLs, peaks),
   theme, layout.
5. **CMS.** Collections per `docs/CONTENT_MODEL.md`.
6. **Media.** Upload `assets/optimized/*-2200.webp` / `-2400.webp` (Framer generates its own
   responsive sizes). Audio goes to a managed CDN, never to raw GitHub URLs.
7. **Drift control.** Each code component carries a header comment with the repo commit. A
   change starts here, passes `tests/`, then gets pasted into Framer with the commit noted in the
   Framer file.

## Fallback if Framer cannot meet the benchmark
The prototype pages are production-grade static React. If a Framer limitation blocks a hero
detail (e.g. the H2 matched cut timing), the code component can carry the whole hero. Flag first
(plan §44).
