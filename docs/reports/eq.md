# Per-engine EQ

Each source layer (A, B, C) has its own five-band EQ, edited over its waveform (the spec's
"Option 3"). This report covers the audit, the insertion point, the DSP, the editor, the
compatibility guarantees, the tests and the open points.

The Option 3 reference image was not attached to the request. The editor was designed from
the spec's text in the instrument's existing graphite-well style. Refine it against the image
when it is available.

## Audit and insertion point

`InstrumentEngine::renderBlock` already renders every layer apart, with no restructuring
needed:

1. The layer's voices are summed into `slot.buffer` (One Shot or Granular, reverse, loops,
   ARP notes; these are all just voices).
2. With per-layer REIMAGINED routing, the layer's own REIMAGINED stage runs on that buffer.
3. The buffer is multiplied by the mix weight × LEVEL × PAN (gain-ramped) and added to the
   output.
4. The shared post chain (DRIVE, CHARACTER's shared part, MOVEMENT, ECHO, SPACE) follows.

The EQ sits between steps 2 and 3. It is one processor per layer on the layer's summed
signal, not per voice. That is the spec's preferred point: after the source's generation and
its own REIMAGINED, before the layer enters the mix.

- **Legacy global REIMAGINED (older sessions).** That routing has no per-layer stage; its
  shared resonance stage runs after the mix. The EQ takes the same place, after the layer's
  voices and before LEVEL and PAN, which is the nearest per-layer point. The shared stage
  therefore hears the equalised layers. Old sessions never have the EQ on, so they are
  unaffected. No preset is converted.
- **A layer that stops sounding.** The engine normally skips a silent layer. With its EQ on,
  it keeps running the filters until their tails have rung out (`Processor::ringing()`).
- **A muted layer.** The EQ's tail is dropped, and it fades in again when the layer returns.

## DSP (`src/engine/LayerEq`)

| Band | Type | Frequency | Other |
|---|---|---|---|
| 1 | High pass | 20 Hz – 2 kHz (80) | 12 or 24 dB/oct |
| 2 | Low shelf | 20 Hz – 1 kHz (200) | ±18 dB, Q 0.3–2 (0.707) |
| 3 | Bell | 20 Hz – 20 kHz (1 k) | ±18 dB, Q 0.2–12 (1.0) |
| 4 | High shelf | 1 kHz – 20 kHz (5 k) | ±18 dB, Q 0.3–2 (0.707) |
| 5 | Low pass | 500 Hz – 20 kHz (12 k) | 12 or 24 dB/oct |

- **Filter structure.** Every band is a trapezoidal state-variable filter (Simper / Cytomic,
  "linear trapezoidal integrated SVF"). It has the same responses as the RBJ cookbook bell and
  Q-form shelves. HP and LP are Butterworth: one section at 12 dB/oct, two sections
  (Q 0.541 and 1.307) at 24 dB/oct. Frequencies are kept below 0.45 × the sample rate.
  The filters are minimum phase with zero latency and no lookahead.
- **Why not plain biquads.** The first version used direct-form biquads redesigned every
  16 samples. The click test caught a kink 8× the signal's own curvature when the gain glided
  from +12 to −18 dB. A direct-form biquad's state does not carry over when its coefficients
  move; the SVF's integrator states do. With the SVF the same test stays within the signal's
  own curvature.
- **Smoothing.** Frequency (in octaves), gain and Q glide with about a 20 ms time constant.
  Coefficients are recomputed from the glided values every 16 samples; each set is a valid,
  stable design and is never interpolated. Switching a band, or the whole EQ, crossfades
  that band's output with its input over 10 ms.
- **Exact bypass.** When the EQ is off, or on with every band off, it is not in the signal
  path at all once faded. The layer's samples are untouched, so sessions without it are
  bit-identical.
- **State.** The state is kept in double precision. Tails below 1e-25 are flushed to zero, so
  there are no denormals.
- **Stereo.** The same coefficients run on both channels, and identical channels stay
  identical (tested). A mono layer is filtered once.
- **The display is exact.** `eq::responseDb()` evaluates the very sections the processor
  runs. The SVF is the bilinear transform of H(s) = m0 + (m1 s + m2) / (s² + k s + 1), so the
  digital response is H at s = j·tan(ω/2)/g. The tests measure steady-state sine gains
  through the processor and match the drawn curve within 0.05 dB, for every band type, at
  44.1, 48, 88.2 and 96 kHz.

## Parameters and state (version hint 18)

Each layer has 19 parameters, `layerA.eq.*` (likewise B and C):

- `enabled`
- `hp.enabled`, `hp.frequency`, `hp.slope`
- `lowShelf.enabled`, `.frequency`, `.gain`, `.q`
- `bell.enabled`, `.frequency`, `.gain`, `.q`
- `highShelf.enabled`, `.frequency`, `.gain`, `.q`
- `lp.enabled`, `lp.frequency`, `lp.slope`

Frequencies and Q have true logarithmic ranges. All of them are automatable, saved with
presets and sessions, and undoable.

- **Defaults.** Everything is off.
- **Older sessions.** They have none of these values and open with the EQ off. The test
  strips all 57 values from a saved state to check this.
- **The open editor.** Which editor is open is a view state only, never a parameter. One
  editor is open at a time; it is not saved.

## Editor

- **EQ key.** A small "EQ" key sits in each card's header, left of One Shot / Granular.
  - The name area gives it its room: about 54 px, or 46 px with three layers.
  - It shows a small light in the layer's colour while the layer's EQ is heard.
  - It looks pressed while the editor is open.
  - Opening never switches the EQ on, and closing never switches it off.
- **Overlay.** The editor occupies exactly the waveform's rectangle (checked by the tests).
  - It is the same graphite at 86 %, so the recording stays faintly visible.
  - The card, its knobs, the other cards, the macros, ARP and MOD do not move.
  - The granular hover controls stay hidden while it is open.
- **Graph.**
  - Log frequency 20 Hz – 20 kHz, labelled 20, 100, 500, 1k, 5k, 10k, 20k (labels never
    crowd).
  - ±18 dB, with the 0 dB line stronger.
  - The response curve is a warm neutral line with a soft fill.
  - The selected band's own shape is shown faintly in its colour.
  - When the EQ is switched off, its shape is shown dashed and the title says OFF.
- **Progressive activation.** Bands that are off are small, quiet rings on the 0 dB line. On
  hover a ring brightens and names itself ("BELL · drag to use"). Pressing or dragging it
  switches the band on and moves it at once. The first band used also switches the EQ on;
  after that, the EQ's switch belongs to the musician alone.
- **Node colours:** HP muted blue, LOW SHELF amber, BELL sage, HIGH SHELF soft rust, LP
  desaturated violet.
- **Node behaviour.**
  - Dragging moves the frequency across. For the bell and shelves it also moves the gain up
    and down; HP and LP move across only, with the node sitting on its own curve at the
    cutoff. Shift gives fine control.
  - The mouse wheel sets Q (bell, shelves) or the slope (HP, LP).
  - A double-click resets the band's main value: gain to 0 dB, or the cutoff to its default.
  - A right-click offers "Turn off".
- **Top row.**
  - Left: the EQ's switch (a power glyph) and "EQ A".
  - Right: the selected band's name, its values (frequency, gain, Q, or the 12/24 slope),
    its own on/off, reset and close.
  - The values are drag fields; a double-click types a value ("3.2k", "-4", "2").
- **Modulation.**
  - The bell's frequency and gain and both shelves' gains are modulation destinations
    (`EQ A BELL FREQ` and so on). Their spans are ±2 octaves and ±12 dB.
  - They are layer stages, so only global LFOs may reach them. A per-note envelope is
    refused.
  - The value fields are drop targets. A modulated field shows its range as a thin underline.
  - On the graph, a modulated node shows a line to where the band is now, and the curve
    shows what is heard now.
  - Resting a dragged source on a card's EQ key opens that EQ.
  - "+ ADD" lists the four EQ destinations under each layer.

## Tests

| Test | Covers |
|---|---|
| `[unit][eq]` (5 cases) | Drawn equals heard: every band type and all bands at once, 4 sample rates, 7 frequencies, within 0.05 dB. Reference points: bell gain at its centre, a shelf at half gain at its corner, −3 dB at a pass band's cutoff, 24 dB/oct steepness. Off, and on with every band off, are untouched. Random extremes every 64 samples (±18 dB, Q 0.2 and 12, both slopes, frequencies near Nyquist) stay finite and bounded, with stereo coherent. Switching and moving are click-free (second difference within twice the signal's own). Engine: layer isolation, null when off, a global LFO on the bell moves it, a per-voice source is refused. |
| `[plugin][eq]` | Defaults off; IDs. Three layers: on with no band is bit-identical; B soloed is untouched by A's and C's EQ and changed by its own. Recall. An older session (all 57 values stripped) opens off. Rapid automation of every band at 44.1–96 kHz stays finite. |
| `[ui][eq-ui]` (CI, xvfb) | The key; the editor in the waveform's place, with nothing moving. Bell drag (on, the EQ on, frequency and gain); HP to 90 Hz across only. Typed 3.2 kHz / −4 dB / Q 2, and the curve reads −4 dB at 3.2 kHz. The EQ's switch keeps the bands. One editor at a time over three layers; ARP and MOD open with an EQ. LFO 1 dropped onto C's bell frequency; ENV 1 refused. Hover-to-open while dragging. Screenshots `eq-01` … `eq-09`. |
| `[.][eq-cpu]` | Cost (below). |
| `[arp-baseline]`, `[drive-baseline]` | Sessions from the builds before ARP and before DRIVE still render bit-identically (EQ and modulation off). |

## CPU and latency

The setup was three layers and an 8-note chord at 48 kHz in 256-sample blocks: 811 µs per
block with the EQs off, and 801 µs with every band of every layer's EQ on (24 dB slopes).
The difference is within the measurement noise. The same loaded build machine was used as for
the modulation figures. At most seven SVF sections per layer cost far less than one granular
or REIMAGINED voice, and there is no FFT and no allocation.

Latency is zero: there is no lookahead and no oversampling, and the plugin reports no
additional latency.

## Open points

1. **The Option 3 reference.** Compare the proportions, the key's place and the graph's
   look against the image when it is supplied.
2. **No spectrum analyser.** The spec makes one optional, so none was added.
3. **No global envelope for EQ destinations.** As with the macros, only global LFOs reach
   them until a gate-aggregation rule is defined.
4. **Host checks.** Automation lanes for the 57 new parameters, and bounce comparisons, are
   still to be checked in DAWs.
