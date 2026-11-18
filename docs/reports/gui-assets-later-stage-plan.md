# GUI assets · Later-stage skin components: development plan (Phase 4)

**For review only after** the rotary families (macro D2, `layer-ivory` B, the minis) and the button pilot are established. Nothing here is integrated, and no layout changes.

## Principles (shared with the rotary and button work)

- **Reference:** the approved Blender master is the physical reference. Components render with fixed orthographic cameras, the GUI light (`studio-sculpt`, upper left), transparent backgrounds and calibrated values. Cropped marketing photographs are never used.
- **Reusable, not unique:** physical backgrounds are reusable. Text, icons, values and state stay live vector. There is never one image per label.
- **Pipeline:** the existing ceramic skin pack (`CeramicSkin`: nine-slice pieces with state variants) is the delivery format, with no new runtime system. Blender renders replace pieces in the same slots.
- **Memory:** each family is measured with every family visible before it ships. The resampled-piece cache budget (see the ceramic-skin memory investigation) applies.

## Components, in order

| # | Component | What it is | Approach | Risk |
|---|---|---|---|---|
| 1 | **Ceramic card surfaces** | The three source cards, the macro card and the modulation card | Nine-slice: rendered corner and edge pieces of a 2–3 mm raised ceramic slab with a fine bevel and a soft contact shadow. Flat interior from the pack's colour, so the cards scale with no stretched texture | Low. It is the existing skin's slot. Corner radius and shadow must match today's vector card exactly, or the layout appears to move |
| 2 | **Main enclosure material** | The editor background and the panel around the cards | A large tileable ivory surface with a very fine powder grain, below 2 % luminance variation. A two-level tile (1× and 2×) | Low. The grain must not shimmer when the window resizes, so it tiles at integer scale only |
| 3 | **Recessed display bezels** | The three waveform displays, the envelope and modulation displays | Nine-slice inner bevel: an ivory lip falling to charcoal glass, with a faint inner shadow at the top. The display content stays the live vector drawing | Medium. The displays are drawn at many sizes. The bevel must stay crisp at 869 px windows |
| 4 | **Keyboard keycaps** | White and black keys, normal and pressed, plus the key-colour states (MAP NOTES, NOTES, ARP) | Per key type: a rendered key top and front face (white natural, white sharp-adjacent, black), stretched only in length. Pressed state from the same scene with the key 1.2 mm down. Note colours stay as a live overlay | Medium-high. The keyboard repaints every note-on, so images must be pre-scaled per window size (cache) and never decoded on the audio thread. The 88-key layout and hit areas don't change |
| 5 | **Small parts** | Mute/solo keys, LED lenses, layer badges, the fader cap | Covered by the button pilot (Workstream B §7); listed here only so the families stay consistent | — |

## Order of work and gates

1. **Card surface study:** three bevel and shadow strengths in the real editor at 869, 1448 and 1810 px, at 1× and 2×. **Gate:** pick one.
2. **Display bezel:** the same procedure as step 1, then the enclosure grain.
3. **Keyboard:** last. A performance study first: repaint cost with 10 held notes and an arpeggio at 1/32, against the current vector keys.
4. **Each step:** `[design-capture]` and the visual regression are updated intentionally, with before and after images; the memory figures are measured; and the CPU when idle is checked.

## Explicitly out of scope

- Layout, sizes, spacing and typography.
- The marketing model's geometry, which is approved and is the reference only.
- Animated effects (glows, light sweeps).
