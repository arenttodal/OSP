# Source manifest (pass 2)

## Interface sources (genuine)

| Source | What it gives | Where |
|---|---|---|
| `web-10-hero-3L-nocaps@4x.png` | The real ANDOR OSP editor, three layers, 5792 × 4344 (4× of 1448 × 1086), with every knob's ticks and arcs but **no cap**. It's identical to `web-01-hero-3L` everywhere else (a re-take of 01 is byte-identical). It uses a test-only switch, `design::draw::captureWithoutCaps`, in the OSP repo (default off, never set by the plugin) | `assets/source/captures/` (regenerate with `tools/capture.sh`, OSP test `[web-capture]`) |
| `web-10-hero-3L-nocaps.json` | Each knob's centre, cap radius and pointer angle, computed exactly as `drawRotarySlider` does (macro r 46.48 px, layer 26.06, envelope 20.0) | same |
| `web-08-hero-3L-bounds.json` | Every component's bounds: cards, buttons, displays, keyboard | same |
| `geometry.json` | All of the above, plus measured frame edges and the 36 black keys found in the capture | `blender/textures/` (written by `prep_textures.py`) |

## The layers

| Layer | Parts | Made from |
|---|---|---|
| **A housing** | teak front fascia and back rail (full width, grain along x), side rails between them (grain along y), butt joints with real 0.7 mm bevels; a ceramic tray inside; a dark 2 mm plinth inset 3 mm | frame edges measured from the capture; teak = the editor's own cheek pixels |
| **B ceramic** | the panel (seated 1 mm below the timber), the three layer cards and the macro/envelope panel (raised 1.2 mm), real bevels | component bounds |
| **C displays** | four wells, 0.85 mm deep, cut into the cards (Boolean); dark bezel walls; the screen on the well floor | `SourceDisplay` bounds + the measured envelope screen |
| **D controls** | 25 knobs (collar for base clearance, cylindrical skirt, knurled on the macros, rounded rim, groove on layer/envelope knobs, shallow dome, inlaid charcoal indicator at the editor's pointer angle); 31 buttons, the preset bar, ARP and Advanced raised 0.8–1.3 mm; key bed + 36 black keys | knob JSON; bounds; detected keys |
| **E artwork** | everything else: type, scales, ticks, arcs, waveforms, button icons | the capture, projected down the face normal onto the tops of B, C and D |

All the knobs are modelled. None of the editor's drawn caps or drawn knob shadows remain, so there's no
double shading. The knobs cast their own shadows.

## Approximations

- **Scale:** 400 × 300 mm face, 18 mm tall (16 mm timber plus the plinth). This is a visualisation scale.
- **Continuous timber frame (owner's direction):** the editor draws teak only on the left and right, with the
  ceramic tray at the top and bottom. Pass 2 frames all four sides in teak, as briefed, and keeps the side
  rails' drawn width (25 px). The front and back rails take the drawn tray margin.
- **Not in any screenshot:** knob sides and the collar, the housing walls, the plinth and the end grain. Colours
  are sampled from the capture or are neutral darks.
- **Buttons** are raised slabs with their own artwork on top. The editor's 1–2 px drawn contact shadow stays
  around them (too faint to read as a second shadow).
- **Waveforms** are deterministic stand-in signals; re-capture with approved recordings before public use.

## Reference

The Limo Series "Il Granello" Instagram Story ad (owner-supplied). It is not stored in the repository.
Only its qualities were studied: dimensional enclosure, raised knobs, dark glass, layered surfaces, soft
light on bevels, an elevated three-quarter camera and a restrained dark set. No controls, text, logo or
layout were taken.
