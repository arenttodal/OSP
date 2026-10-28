# 01 — The ANDOR identity

ANDOR OSP's interface is the definitive source of truth. This guide describes it; it does not
redesign it. Where words and pixels disagree, the pixels (the code, and its screenshots in
`img/`) win.

## What an ANDOR instrument is, visually

- **One physical object.** A warm ceramic housing on a quiet surround, with teak rails left
  and right (rendered, not gradients). The whole instrument is laid out once at
  1448 × 1086 and scales as one piece: proportions never change with the window.
- **Raised and recessed.** Parts stand up from the housing:
  - the source cards;
  - the macro panel;
  - the keys (ceramic keys in four states);
  - the ARP card.
  Displays are cut into it: dark graphite wells for waveforms, the envelope and the popovers' pictures.
- **Quiet colour.**
  - Neutrals carry the instrument.
  - The one live accent (`accent`, a warm orange) marks what is active.
  - The six macro identities are a mineral family at equal lightness: rust, ochre, moss,
    mineral blue, mauve and verdigris. They appear only on a macro's ring, light and popover.
  - Layers A/B/C have their own warm, cool and sage identities.
- **Type that whispers.**
  - Inter, with three tones of text (`text`, `textSecondary`, `textMicro`).
  - Hierarchy comes from size and tracking, almost never weight.
  - Outfit is for the identity only: **ANDOR** in ExtraBold, **/PRODUCT** in ExtraLight, a sentence-case tagline under it.
- **Rendered where it matters.**
  - The macro knobs wear an offline-rendered ivory cap (Blender sprite sheets).
  - The housing, cards, bezels, keys, lenses and keybed are rendered ceramic surfaces.
  - The code draws over them, never instead of them.
- **Honest controls.** Every control does what it shows. A control the product does not
  use is shown disabled, in ANDOR OSP's own disabled look. The instrument never pretends.

## The layout, top to bottom (reference px)

1. **Header** (y 23–111):
   - the identity (x 51);
   - the preset bar (501–895) and the save key;
   - MIX for two layers (966–1130);
   - VOLUME (1150–1344);
   - the menu (1366).
2. **Sources** (y 127–652): one hero card, two dual cards or three triple cards, 12 px apart.
   With none, the drop zone. Each card holds:
   - the badge with M/S, the root, EQ, the mode and the menu;
   - the display;
   - five knobs: START TUNE PAN LEVEL REIMAGINED;
   - four modifier keys: LINK REVERSE LOOP FOLLOW.
3. **Macro panel** (y 682–898):
   - six macros (x 128–858) with lenses under them;
   - a divider at x 945;
   - the modulation panel (x 955–1405) with its six tabs.
4. **Keyboard row** (y 913–1014): PITCH and MOD wheels, 88 keys (148–1298), the ARP card and
   Advanced (1308–1413). When ARP's editor opens, a 187 px band takes this row's place and
   everything below moves down.
5. **Footer**: the status line (x 48, y 1038).

## What a product may make its own

- Its identity, as the **word after "ANDOR/"** (up to 8 capitals) and the **tagline**.
- The **names and parameters of the six macros**, keeping the six positions and identity
  colours, and their **popovers**, built from the shared popover parts.
- Its **engine** (sound), and which controls it binds.
- Its **presets** (namespace, folder, extension) and saved state.

Everything else (colour, type, geometry, materials, rendered assets, components, interaction)
is ANDOR OSP's, used unchanged. See HANDOFF.md, "ABSOLUTE DESIGN RULES FOR NEW ANDOR PRODUCTS".
