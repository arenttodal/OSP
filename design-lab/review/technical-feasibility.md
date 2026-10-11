# Technical feasibility (estimates only, nothing integrated)

Nothing here has been implemented or profiled. The memory figures below are
**arithmetic** (width × height × 4 bytes, ARGB, decoded) for the proposed asset shapes. They
are not measurements. The approved knob's measured figures, from its own phase-1 work, are
quoted only as a reference point.

## Principles

1. **Offline only.** Blender renders each asset once, and the plugin draws bitmaps and vectors. No runtime 3D.
2. **Small reusable pieces, not screenshots.** Surfaces become 9-slice frames (corners,
   edges, centre) plus, where a texture is visible, a small seamless tile. Light gradients
   stay vector. A full-window bitmap is never needed: one 2× frame of the whole GUI is
   2896 × 2172 × 4 = 25 MB decoded.
3. **Icons, text, values, halos and fills stay vector.** The lab already keeps them separate.
4. **High DPI.** Masters at 4×. Ship 1× and 2×, and resample other scales by area at load
   time, as `SpriteKnobRenderer` does (exact-size, premultiplied).

## Per family

| Family | Runtime size (design px) | States | Proposed asset | Decoded memory (arithmetic, 1× + 2×) | Vector alternative |
|---|---|---|---|---|---|
| **Buttons A1/A2/A3/A4** | Modifier 98×45 (×12), mode 128×41 (×3), EQ 38×33 (×3), M/S 27×21 (×6), Advanced 105×31, ARP 105×64, preset bar 446×48 | normal, hover, pressed, on; disabled in software (wash + ink alpha) | One 9-slice frame per state per family: 2× source ≈ 48×48 px with 14 px corners. A2 needs the groove frame plus the face frame, and the on state's glow frame. | 4 states × (48² + 24²) × 4 B ≈ **46 KB per family**. All four families: < 0.2 MB. | A4 is almost vector: a 1-step edge plus a contact shadow is reproducible with gradients. A2 and A3 need the frames for the groove and the machined edge. |
| **Tabs B1** | strip 450×24 | selected, hover, focus, disabled | None (vector). | 0 | **Yes, recommended:** a crisper underline is a drawing change. |
| **Tabs B2/B3/B4** | strip 370×22; tab ≈ 44–68×22 | as above | B2: strip 9-slice plus a pad 9-slice. B3: strip 9-slice plus a tiled grain. B4: key 9-slice ×2 states. | < 30 KB each | B2's recess and pad are close to vector. B3's grain needs a tile. |
| **Fader cap C1/C2/C3** | cap 13–14 × 21–22 | normal, hover, drag | One cap sprite per state (2× = 28×44 px). The track and its value fill stay vector. | 3 × (14×22 + 28×44) × 4 ≈ **19 KB per cap** | The track is vector. T2's slot is a 3-slice (2 lip edges + floor) at ≈ 2 KB, or vector. |
| **Macro panel D1–D6** | 1378×216 (D5 inset 892×202) | normal | A 9-slice frame for the bevel or lip (2× corners ≈ 32 px), a vector light gradient, and for D2/D4 a 256×256 seamless grain tile (brushed). | frame ≈ (64² × 4 + edges) ≈ 40 KB; tile 256 KB. **< 0.3 MB.** The full bitmap alternative is 2756×432×4 = 4.8 MB at 2×. | D1, D3 and D6 are mostly vector with a rendered edge strip. D2 and D4 need the tile to show grain at all. |
| **Enclosure E1–E4** | housing 1398×1053; cards 451×525 (×3) | normal | Housing 9-slice (≈ 64 px corners) plus a powder or brushed tile; one card 9-slice shared by the three cards; display bezels as a 9-slice ring. | < 0.5 MB in total | E1 is achievable with today's vector code plus better edges and shadows. E2 and E3 need tiles. |
| **LED lens (F)** | 9 px (×6), ARP 11 px | off, lit | One neutral lens sprite (off and lit) tinted per macro colour at draw time, plus a glow sprite. 4× masters. | < 10 KB | The current bead is vector. A lens sprite is cheap. |
| **Divider groove, seam (F)** | 1.5 × 188; seam ring | — | Vector (a 1 px shade plus a 1 px light line). | 0 | Yes. |
| **Knobs (approved)** | 93 px body | 129 frames | Unchanged (`SpriteKnobRenderer`). Reference, measured in its phase-1 work: six macros 796 KB decoded at 1×, worst case 37 MB at 2× of the design across all cached sizes. | — | — |

## Texture sharing

- The three engine cards, the ARP card and the Advanced key share one card or key frame per family.
- Every macro LED shares one lens tinted per colour.
- D2, E3 and B3 share one brushed-ivory tile, and D4, A3, C2 and T3 share one brushed-metal tile. The grain is the same at every size because it is a tile.

## Risks

- **Brushed tiles at 1×.** The grain is felt rather than seen at 100 %. Its value is mostly at 2× (Retina). A tile scaled by a non-integer factor (150 %) must be resampled once, not on every frame.
- **Light direction.** The rendered edges assume the studio light from above. Every frame must come from the same rig, which the lab already enforces.
- **The A2 lit groove** is a bitmap state. If the accent colour ever changes, re-render it or tint a neutral glow frame (preferred).
- **Graphite (D5)** requires light variants of the macro labels, the "MACROS" title and the knob tick dots. The dots are drawn dark today and nearly vanish on graphite (visible in Direction 05).
- **Implementation effort.** Ranking, lowest first:
  - B1 and E1 refinements (code only);
  - fader cap and LEDs (sprites);
  - A2 buttons (frames in every key);
  - D panels;
  - E2/E3 tiles across the window.
