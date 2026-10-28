# 03 — Components

Every component of the ANDOR interface is real code in `shared/andor-ui/Source`. ANDOR OSP and
every product built from the ANDOR Instrument Shell compile the very same files. A product
**composes** these components; it never copies or restyles them.

Crops (at 2×) are in `img/components/`. They come from the hidden capture test
(`tests/plugin/DesignCaptureTests.inc`).

**Bound to** lists the parameter IDs a component reads. These IDs are part of the ANDOR
schema, AndorParameters.h. A component whose parameters the product's engine does not read
is shown disabled by the shell (Binding.h, `andorParams`).

## Frame (InstrumentFrame.h)

| Component | What it is | Bound to | Crop |
|---|---|---|---|
| `frame::paint` | Paints, in order: the housing and teak rails; the raised faces of the cards and ARP panel; the identity (mark, product, tagline); the keybed recess; the macro panel with its divider and six lenses; the ceramic keys on the housing. | — | the states in `img/osp/` |
| `frame::Content` | The 1448 × 1086 canvas, scaled as one piece (`frame::fit`). | — | — |
| `frame::MacroKnob` + `setUpMacro` | One of six macros: a name (`MacroLabel`), a 132 px knob with the ivory sprite cap, a thin identity-colour ring, the value bubble in whole percent, and a modulation drop target. | the product's macro ID (OSP: `life`, `drive`, `character`, `motion`, `space`, `echo`) | `MacroLabel-1@2x.png`, `Slider-1@2x.png` |
| `OspKeyboard` | 88 keys A0–C8, warm white keys and soft graphite black keys; a held key warms toward the accent. MIDI 60 = C4. | the keyboard state | `OspKeyboard-1@2x.png` |
| `DropZone` | The empty instrument: a dashed invitation, "Browse…" and "Load example". | — | `../img/osp/00-empty.png` |
| `AddLayerTarget` | While a sound is dragged in: where the new layer would go ("ADD LAYER B"). | — | — |
| `HeaderMenuButton` | The header's three dots. | — | `HeaderMenuButton-1@2x.png` |

## Header (MainSections.h)

| Component | What it is | Bound to | Crop |
|---|---|---|---|
| `PresetBar` | ‹ name ♥ › with the preset menu. | the host's preset list (`InstrumentHost`) | `PresetBar-1@2x.png` |
| `SavePresetButton` | The cassette key right of the bar. | — | `SavePresetButton-1@2x.png` |
| `HeaderMix` | MIX: the A/B line (two layers). The triangle for three is hidden by `design::showMixTriangle`. | `ab.blend`, `mix.x`, `mix.y` | `../img/osp/02-two-layers.png` |
| `VolumeSlider` | VOLUME, a thin utility slider with its value above. | `gain` | `VolumeSlider-1@2x.png` |

## Sources (EngineCard.h)

| Component | What it is | Bound to | Crop |
|---|---|---|---|
| `EngineCard` | One layer (A, B or C). Hero, Dual or Triple density changes sizes, never content. It holds: the badge with M/S, the root, EQ, the mode selector, the menu, the display, START TUNE PAN LEVEL REIMAGINED, and LINK REVERSE LOOP FOLLOW. | `layerX.*`, `reimagined` (A) | `EngineCard-1@2x.png` |
| `SourceDisplay` | The waveform on graphite: layered body, RMS and brightness colour, the read heads, the grains, the start and loop markers. | the loaded sound | `SourceDisplay-1@2x.png` |
| `LayerKnob` | START, TUNE, PAN, LEVEL, REIMAGINED (spectral arc). | `layerX.start/tune/pan/level`, `reimagined` / `layerX.reimagined` | `LayerKnob-1@2x.png` |
| `ModifierButton` | LINK, REVERSE, LOOP, FOLLOW: ceramic keys with thin icons, active in `accentSoft`. | `layerX.link/reverse/loop/follow` | `ModifierButton-1@2x.png` |
| `LayerStateButton` | M and S beside the badge. | `layerX.mute/solo` | `LayerStateButton-1@2x.png` |
| `ModeSelector` | One Shot / Granular. | `layerX.sourceMode` | `ModeSelector-1@2x.png` |
| `MiniKnob` (granular) | POS SIZE DENS TUNE SPREAD over the display (hover, Granular). | `layerX.granular.*` | `../img/osp/07-card-granular.png` |
| `EqButton` / `EqEditor` / `DampenEditor` | The layer's five-band EQ over its waveform, and DAMPEN (key-tracked) as its second page. | `layerX.eq.*`, `layerX.dampen.*` | `EqButton-1@2x.png`, `../img/osp/07-card-eq.png`, `07-card-dampen.png` |

## Macros, modulation, keyboard row

| Component | What it is | Bound to | Crop |
|---|---|---|---|
| `MacroLabel` | A macro's name. With settings behind it, hover darkens and underlines it and a click opens its popover. | — | `MacroLabel-1@2x.png` |
| `ModulationPanel` | Tabs AMP, ENV, LFO 1, LFO 2, VEL, NOTE (each tab is a drag socket), with their pages. | the AMP `EnvelopePanel` (`attack`, `decay`, `sustainLevel`, `release`); `mod.lfoN.*`, `mod.envN.*` | `ModulationPanel-1@2x.png`, `../img/osp/05-mod-tab-*.png` |
| `ModulationOverlay` | The halos on modulated controls, and drag and drop of sources. | `mod.routeN.*` | `../img/osp/06-popup-routes-lfo1.png` |
| `Wheel` | PITCH (springs back) and MOD (a drag socket). | the MIDI wheels | `Wheel-1@2x.png` |
| `ArpControl` / `ArpInlinePanel` | The arpeggiator's card by the keyboard, and its inline editor (the window grows by 187 px). | `arp.*` | `ArpControl-1@2x.png`, `../img/osp/09-arp-open.png` |
| `AdvancedCardButton` | "Advanced ›" under ARP. | — | `AdvancedCardButton-1@2x.png` |

## Popovers (ShapingPopups.h, ModulationPanel.h)

| Component | What it is | Bound to |
|---|---|---|
| `MiniPanel` | The popover frame: shadow margin, title and subtitle, close button, unit scaling. It is placed by `frame::placePopover` with `macroPopoverOrigin`, `advancedPopoverOrigin` or `modulationPopoverOrigin`. | — |
| `createAdvancedPopup` | ADVANCED (TUNING, VOICES & DYNAMICS): FINE, BEND, GLIDE, POLY/MONO, TAPE/NATURAL, the dynamics row, MPE, Reseed. | `fineTune`, `bendRange`, `glide`, `voiceMode`, `pitchCharacter`, `dynamics*`, `velocityRange`, `mpe`, `seed` |
| `ModRoutesPopup`, `ModCurvePopup` | A source's routes, and its larger curve. | `mod.*` |
| `SegmentedControl`, `MiniKnob` | The parts a product builds its own macro popovers from. | their parameter |

The macro popovers (LIFE … ECHO) and REIMAGINED stay ANDOR OSP's own
(`apps/plugin/Source/MacroPopups.cpp`, `OspPopups.cpp`). They describe OSP's engine. A
product provides its own through `ProductInfo::macroPopover`, built from `MiniPanel`,
`MiniKnob` and `SegmentedControl`.

## What is ANDOR OSP's alone (not shared)

These describe OSP's own features, so a product does not get them:
- the Library window (`LibraryPanel`);
- the Samples inspector;
- the macro and REIMAGINED popovers;
- OSP's preset/instrument/starting-state model;
- its engine (`src/engine`).
