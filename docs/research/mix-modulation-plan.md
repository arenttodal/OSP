# Modulating the MIX — investigation and plan

Request: assign the LFOs (and other modulation sources) to the MIX slider in the header, so
the balance between A, B (and C) can move by itself.

Status: plan only, nothing implemented. To be decided before work starts.

## What exists today

- **Engine.**
  - The mix is three numbers in `EngineSettings`:
    - `blend` (A↔B with two layers);
    - `mixX` and `mixY` (the A/B/C triangle with three).
  - `InstrumentEngine::render` glides them over about 20 ms per block. `mixWeights` turns them
    into the per-layer gains, equal power. That is a shared stage after every voice.
- **Modulation.**
  - `mod::Dest` is the registry of destinations. Each has an owner: `global` for a shared
    stage (global sources only), `voice` for a per-note one.
  - `applyGlobalModulation` adds the routed offsets for the shared stages once per control
    block: macros, the EQ, REIMAGINED's shared part, the arpeggiator.
  - The mix is not a destination yet.
- **Host parameters.**
  - Each route is `mod.routeN.source/dest/depth/enabled`. `dest` is an `AudioParameterChoice`
    whose list is frozen at `hostDestCount`. Adding a choice moves every saved normalised
    value, and with it the automation.
  - The destinations after the frozen list (KEY DNA AMOUNT) are therefore reachable only from
    the engine and the research renderer. This is the blocker Phase 5 already reported for KEY
    DNA AMOUNT.
- **Interface.**
  - `HeaderMix` (shared/andor-ui) draws the A–B line or the triangle and attaches to `blend`,
    `mix.x` and `mix.y`.
  - Knobs show routes as halo rings, and sources are assigned by dragging or by right-click,
    through `InstrumentHost::assignModulation`. The MIX takes no part in that today.

## Proposal

### 1. Engine: two global destinations (small)

Append to `mod::Dest`:

- `mixPosition`: unit domain, global, continuous.
  - Two layers: it adds to `blend` (A↔B).
  - Three layers: it moves the node along the triangle's A→B axis (`mixX`).
- `mixTowardsC`: unit domain, global, continuous. It moves the node towards C (`mixY`);
  it does nothing with two layers.

In `applyGlobalModulation`, store the two offsets. In the render loop, the glide targets become
`clamp (config.blend + offset)` and so on. The stored parameters never change, so the knob
shows the base and the halo the movement.

- **Smoothing.** The 20 ms glide would smear an LFO above about 10 Hz. A modulated mix glides
  per block (≈ 1–5 ms) instead: still click-free, because the gains already ramp sample by
  sample.
- **Sources.** Global LFOs, the global ENV, the mod wheel, macros as sources, and
  meta-modulation of these routes' depth (already generic).
- **Per-voice sources.** VEL, NOTE and poly LFOs do not fit a shared stage. See option 4.

Cost: a few additions per control block. No allocation. Deterministic: the LFOs already derive
from the seed.

### 2. Host parameters: unfreeze the destination list once, with a version (the real decision)

Choose one of:

- **(a) Recommended. A second destination parameter per route,** `mod.routeN.destExt`. Its
  choice list covers the destinations after the frozen ones: KEY DNA AMOUNT A/B/C, MIX, MIX C,
  and later additions, appended only.
  - `dest` keeps its list and its automation.
  - A route uses `destExt` when `dest` is "Extended".
  - Old sessions load unchanged: `destExt` defaults to none.
  - This also unblocks KEY DNA AMOUNT in the modulation bay.
- **(b) Destination out of the host parameters,** into the session state (not automatable).
  - Simpler.
  - But a route's target could no longer be automated or seen by the host, unlike today.
- **(c) A new parameter generation** (`mod.routeN.dest2` replacing `dest`) with a migration of
  the saved index.
  - Clean list.
  - But more migration code, and old automation lanes on `dest` stop driving it.

### 3. Interface (shared/andor-ui, OSP pixel-identical when nothing is routed)

- **Drop target.**
  - Dragging a source onto the header MIX assigns it (the same path as the knobs).
  - Right-click MIX: "Modulate MIX with… / Remove".
  - Three layers: "MIX (A↔B)" and "MIX towards C".
- **Two layers.**
  - A thin range bar along the A–B line in the source's colour, from base − depth to
    base + depth (the halo's equivalent).
  - A second, hollow node that moves live with the modulation. The solid node stays the base
    the musician sets.
- **Three layers.**
  - The live point traces its path inside the triangle, with a short fading trail.
  - The large mix popover shows the same with room to edit the depth.
- **Routes popover and modulation bay.** MIX appears as a destination, with depth and polarity
  like any other route.
- **Readout.** While modulated, the hover readout shows the live shares (A 62 % · B 38 %).

### 4. Optional later: per-note MIX (expressive, larger)

Each voice belongs to one layer, so a per-voice MIX can be done as a per-voice gain: the ratio
of the layer's weight at (base + the voice's offset) to its weight at the base. That makes
**VEL → MIX** possible, where hard notes lean towards B and soft notes towards A. The same
works for NOTE → MIX (low keys A, high keys B) and for poly LFOs.

It costs a weight evaluation per voice per control block. It is worth doing only if option 1
proves musical.

## Tests

- **Engine unit tests:**
  - a route moves the weights, equal power at every point;
  - an unrouted mix is bit-exact with today;
  - a 20 Hz LFO on MIX makes no click (the block-to-block gain step is bounded);
  - same seed → identical render.
- **Plugin tests:**
  - assign by drag or menu;
  - undo and redo;
  - session save and load (`destExt`);
  - an old session loads unchanged;
  - host automation of `dest` is unaffected.
- **Visual:** design capture and shell regression at 0 px with nothing routed; screenshots of
  the range bar and the live node.
- **Goldens:** unchanged (nothing routes the mix in the fixtures). Add one research-renderer
  scene: "LFO on MIX".

## Effort (rough)

| Step | Size |
|---|---|
| 1 Engine destinations + smoothing + unit tests | small |
| 2 `destExt` parameter, migration rules, routes popover, bay | medium (the decision first) |
| 3 Header MIX drop target, range bar, live node, triangle trail | medium |
| 4 Per-note MIX (optional) | medium |

## Questions before starting

1. Option 2(a) (a `destExt` parameter, which also brings KEY DNA AMOUNT to the modulation bay)?
2. With three layers: one MIX destination that moves along A↔B only, or both axes
   ("MIX" and "MIX towards C")?
3. Per-note MIX (VEL → MIX) now, or after the global one has been played?
