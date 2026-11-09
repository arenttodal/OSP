# ANDOR design system (v1, Phase 1)

Source of truth in code: `shared/tokens/tokens.css` (tokens) and `shared/tokens/base.css` (roles,
buttons, header, motion). In Framer, every token below becomes a Color/Text Style with the same
name (see `docs/FRAMER.md`).

## 1. Colour

Measured with `node tests/accessibility/contrast.mjs` (WCAG 2.x). All 25 required pairings pass.

| Token | Value | Use | Measured |
|---|---|---|---|
| `--bg` | `#F7F6F2` | Background / Primary | — |
| `--bg-2` | `#EDEBE5` | Background / Secondary | — |
| `--bg-elev` | `#FCFBF8` | Elevated surfaces | — |
| `--text` | `#242723` | Primary text, ink buttons | 14.0:1 on bg |
| `--text-2` | `#5C635A` | Secondary text (**refined** from `#626960`) | 5.7:1 bg, 5.2:1 bg-2 |
| `--text-muted` | `#656C63` | Muted text (**refined**: `#737A70` failed at 4.1:1) | 5.0:1 bg, 4.5:1 bg-2 |
| `--text-quiet` | `#878C83` | **New.** Second tone of two-tone headlines, large type only (≥ 24 px) | 3.2:1 (AA large) |
| `--accent` | `#BD653D` | Terracotta: graphics, rules, marks, large type only | 3.8:1 (fails as body text) |
| `--accent-strong` | `#A4532E` | **New.** Terracotta for text and fills under white type | 5.0:1 bg; white on it 5.5:1 |
| `--border` | `#D8D9D1` | Decorative hairlines | — |
| `--border-strong` | `#858C82` | **New.** Control outlines (non-text 3:1) | 3.1:1 |
| `--dark-bg` / `--dark-elev` | `#222924` / `#303A32` | Dark sections | — |
| `--dark-text` | `#F4F1E9` | | 13.2:1 |
| `--dark-text-2` | `#C4CEC1` | | 9.2:1 |
| `--dark-muted` | `#9AA597` | **New.** Muted on dark | 5.8:1 / 4.6:1 elev |
| `--dark-quiet` | `#7F897F` | **New.** Two-tone on dark | 4.1:1 |
| `--dark-accent` | `#D08A63` | **New.** Terracotta lifted for dark surfaces | 5.3:1 |

Rules, as in the plan: a whole component switches theme (`.theme-dark` redefines every token, so
a component never mixes light text on light). The accent stays rare: a mark, an active state, a
rule. No gradients beyond warm light washes. Product colours (`--osp-*`) are sampled from the
interface and never used for site text.

## 2. Typography

| Role | Family | Size (375 → 1440 px, fluid) | Notes |
|---|---|---|---|
| Hero | Inter Tight (variable, self-hosted) | 46 → 88–96 px | weight 480–500, tracking −0.038em, leading 1.02 |
| Hero editorial line | EB Garamond 400 italic | same | used once per page at most |
| Section | Inter Tight | 34 → 64 px | |
| Feature | Inter Tight | 27 → 40 px | |
| Lede | Inter Tight | 17 → 21 px | `--text-2` |
| Body | Inter Tight | 15 → 17 px | leading 1.55 |
| Technical label | **Geist Mono** 400/500 | 12–13 px | **New** (owner's references). Uppercase, +0.02em. Replaces most tiny uppercase sans labels |
| Label | Inter Tight 560 | 12 px | sparingly |

Inter Tight renders well at all sizes in Chromium. Its tight default spacing suits the hero sizes
without manual kerning. Söhne remains the premium alternative (licence cost: **owner decision**,
not committed). All fonts are self-hosted through `@fontsource` (no Google Fonts requests).

## 3. Layout

- Content max 1360 px; gutter `clamp(20px, 5.6vw, 80px)` (20 at 360 px, 43 at 768, 80 at ≥ 1430).
- 12 columns, gap `clamp(12px, 1.7vw, 24px)`; mobile collapses to art-directed single columns.
- Section spacing `clamp(88px, 10vw, 160px)`, varied per composition, never uniform heights.
- Touch targets ≥ 44 px (CTA pair 52–56 px on mobile).

## 4. Buttons and links

| Component | Spec |
|---|---|
| **ButtonPair** (primary CTA) | Outline pill (56 px, 1 px ink) + ink circular "orb" with an arrow. Hover: the pill fills with ink and the orb turns 45°. Focus ring on both. |
| PrimaryButton | Ink fill, paper text, pill, 48 px |
| SecondaryButton | Transparent, `--border-strong` outline |
| TextLink | Underline at 40 % of currentColor, full on hover |
| Header CTA | Small secondary pill "Join the waitlist" |

## 5. Motion tokens

| Token | Value | Use |
|---|---|---|
| `--ease-out` | `cubic-bezier(.22,1,.36,1)` | arrivals, reveals |
| `--ease-in-out` | `cubic-bezier(.65,0,.35,1)` | state changes (variant switch, crossfades) |
| `--ease-soft` | `cubic-bezier(.33,0,.15,1)` | product emergence, matched cut |
| `--dur-1…5` | 160 / 280 / 520 / 900 / 1400 ms | hover / control / text / product / settle |
| `--stagger` | 90 ms | sequence step |

Hero sequence (all concepts): nav (step 0–1) → eyebrow → headline lines (masked rise, 900 ms) →
lede → CTA → product emergence (900–1400 ms, delayed 280–380 ms) → audio control activates → settle.
The sequence waits for fonts and the hero image to decode, so text never reflows mid-animation.
Only transform/opacity animate; measured CLS ≤ 0.033. `prefers-reduced-motion` shows the settled
state with no movement. Nothing moves perpetually.

## 6. Media presentation (`ProductMedia`)

Variants: `capture` (straight-on, hairline + warm shadow), `studio` (soft overhead light wash +
contact shadow), `detail` (print-like crop), `object` (studio render with a baked shadow). Every
instance names an asset id. A missing or failed asset renders an explicit "Product media missing"
state, never a recreation. Dimensions are reserved from the manifest (`width`/`height`). AVIF and
WebP `srcset` are generated per asset. Only the hero image is `fetchpriority="high"`.

## 7. Multi-product readiness

The header takes a `wordmark` variant (`brand` = ANDOR, `product` = ANDOR/OSP, matching the
plugin's own logotype). Tokens are product-neutral. A future ANDOR ANALOG page reuses every
component with its own asset ids and content records.
