# Spec: more than one tom, more than one perc (multi-sound pads and more pads)

Status: **proposal**, nothing built yet. The user asked for this on 2026-10-12.

**Goal.** A kit should be able to hold several toms, several percussion sounds, several
cymbals, and so on. They should be playable from the keyboard, especially in EXPANDED,
without giving up what makes ANDOR Drums simple: one pad, one idea.

## What limits a kit today

- **Nine pads,** each with one category (`drums::maxPads = 9`). A pad can hold 16 sample
  layers, but layers are alternatives or a stack: round robin, velocity, stack. One hit plays
  one choice, so four toms on one pad are four takes of "the tom", not four drums.
- **EXPANDED transposes one sound.** Each pad owns a contiguous key range, and every key plays
  the pad's sound shifted by `key - root` semitones (`drums::resolve`). A tom zone of 12 keys
  gives one tom at 12 pitches, not a low, mid and high tom with their own recordings.
- **PAD mode** gives a pad exactly one note (`KeyMap::padNote`). General MIDI's six tom notes
  (41 43 45 47 48 50) and its percussion family (congas 62–64, bongos 60–61, cowbell 56,
  tambourine 54, shaker 82…) cannot each reach their own sound.

## Proposal in one line

A pad can become a **family**: a TOMS pad or a PERC pad whose sounds each have **their own
key**. Every sound can still have round robins and velocity layers. A second bank of pads
(16 in all) is a separate, later step, for kits that need more families.

Families are the step that matters. A drummer thinks "the toms", not "pad 6, pad 7, pad 8",
and the pad grid stays readable.

## Part A: family pads (key-split sounds)

### What a musician does

1. Drop four tom samples on the TOM pad. The Cmd-drop menu gains **"Spread across keys
   (one sound per key)"**, next to round robin, velocity, stack and replace. A plain drop
   (no menu) does it by default when the pad's category is a family category (tom,
   percussion, cymbal), or when the classifier (M7) finds clearly different pitches or sounds
   in the drop.
2. The pad now shows **TOMS · 4**. Its waveform strip shows the four sounds side by side,
   lowest first. Hitting the pad on screen plays the *lead* sound, which is the first by
   default. Option-click plays them all in turn.
3. **In PAD mode** each sound answers its own note:
   - **Toms:** 4 sounds take 41 45 48 50 (low floor, low, hi-mid, high); 6 sounds take all of
     41 43 45 47 48 50.
   - **Percussion:** each sound takes the GM note of its classified kind (conga, bongo,
     cowbell, tambourine, shaker…), or the next free note when its kind is unknown.
   - The KEY MAP editor shows a family as one lane with a chip per sound. Each chip can be
     dragged to another note and has MIDI Learn, as pads do today.
4. **In EXPANDED** the pad's zone is laid out by sound, in a choice of three shapes:
   - **ONE KEY EACH** (the default for families): `n` keys, one sound per key at its natural
     pitch. Four toms take four adjacent keys.
   - **SPLIT RANGE:** the zone is cut into `n` equal sub-ranges. Each sound plays chromatically
     around its own root, so you can play melodic toms, each recording near its own pitch.
     This is how a sampled instrument does key splits, and it sounds better than stretching
     one tom across an octave.
   - **ONE SOUND, TRANSPOSED:** today's behaviour, playing the lead sound only.
5. **Per sound,** the layer editor gains a SOUNDS level above LAYERS:
   - each sound has its name, level, pan offset, tune offset, START/END, and its own takes;
   - round robin or velocity layers work *within* a sound: "high tom" can hold three takes;
   - the pad's envelope, modulation, choke and voice mode apply to the whole family.

   Mute applies to the whole family; there is no per-sound mute in V1.
6. **Chokes inside a family:** a per-pad switch, **"Sounds choke each other"**. It is off for
   toms and percussion. It is on by default for a HATS family (closed, pedal, half-open,
   open), so one hats pad covers the whole hi-hat in PAD mode (GM 42/44/46).

### Data model (`src/drums`)

| Today | Proposed |
|---|---|
| `LayerPlan { mode, order, count, layer[16] }` | Adds `int soundCount` and, per layer, a `sound` index (0..soundCount-1). Layers with the same `sound` are that sound's takes, and `mode`/`order` apply within it. `soundCount = 1` is exactly today. |
| `LayerParams` | Adds `int sound = 0`. |
| *(new)* `SoundParams` | Per sound: `name`, `tuneSemitones`, `gainDb`, `pan`, `naturalNote` (its MIDI note at natural pitch, for SPLIT RANGE), `kind` (classifier subcategory: lowTom, conga…). Held in `LayerPlan::sound[maxSounds]`. |
| `KeyMap::padNote[9]` | Becomes `padNote[9][maxSounds]`. Sound 0 is the pad's note and keeps every existing map. |
| `Zone` | Adds `ZoneLayout layout` (oneKeyEach / splitRange / transposed). The default is `transposed`, today's behaviour. |
| `Hit { pad, semitones }` | Adds `int sound` (or -1: the lead / the picker). |

`maxSounds = 8` per pad. Layers stay 16 per pad in total: 8 sounds × 2 takes, 4 × 4, or
1 × 16 as today.

**Real time.** `resolve` stays allocation-free: one more small lookup. `LayerPicker::pick`
gains a sound filter: it picks among the layers of the hit's sound. The engine's
`startHit` already receives the pad and its semitones. It now receives a sound too, and
plays only that sound's layers (the `onlyLayer` path generalises to a layer mask).

**Memory and voices.** A family pad is still one `InstrumentEngine` with 48 voice slots.
Four toms rolling at once are covered: per-pad polyphony is 12 by default, up to 24.

### State and files

- **Session state 5 and kit schema 2.** The families are added inside `<Layers>`
  (`<Sound>` children with their parameters; `sound="n"` on each layer). PAD notes per sound
  go in `<KeyMap>`. The zone layout is an attribute.
- **Migration.** A state without these attributes is a pad with one sound, exactly as today.
  An older plugin refuses a kit of schema 2 ("made with a newer ANDOR Drums"), as for every
  schema bump, rather than misread it.
- **Break slicing (M6).** The suggested plan can put several tom slices (or perc slices) as
  sounds of one family pad, instead of round robins, when their pitches differ by more than a
  semitone. Today such slices are grouped as alternatives.
- **Import (M7).** The same rule applies: groups of the same category with distinct pitches
  or kinds become one family pad. Today they go to separate pads, or to the tray when the
  pads run out. This alone frees pads in most real imports.

### UI

- **Pad cell:** the name with a count ("TOMS · 4"). A thin strip of `n` ticks, each lit as its
  sound plays, so you see which tom you hit.
- **Selected-pad editor:** a SOUNDS row above the layer list, with chips per sound that can
  be dragged to reorder. Selecting a chip shows its layers (takes) below, as today.
- **Keyboard range strip:** a family zone is drawn in the pad's colour, with fine dividers per
  sound and each sound's root marked.
- **KEY MAP editor:** a family lane has one chip per sound. Collision handling (swap, move
  others, layer, cancel) works per chip as it does per pad.

### Tests to write

- `resolve`: each layout (one key each, split range, transposed), with overlaps on and off,
  in PAD and EXPANDED.
- The picker within a sound: round robin and velocity per sound, reproducible from the seed.
- A family's sounds at their natural pitches; SPLIT RANGE transposes around each sound's root.
- Choke within a family (hats): closed chokes open; toms never choke each other.
- **States 1–4 load unchanged.** A family kit round-trips bit-identically. Portable kits and
  relinking cover every sound.
- Import and break plans: distinct-pitch toms become one family pad.
- Zero allocation in `processBlock` with family pads (the M8 guard).

## Part B: more pads (16, in two banks)

For kits that need more than nine ideas: two kicks, layered claps, FX, vocal chops.

- `maxPads` goes from 9 to 16, shown as **BANK A / BANK B** with 8 pads each. The layouts
  are 1x8 and 2x4, plus **4x4** for both banks at once. 3x3 stays: bank A plus the PAD 9
  button, as today.
- **Parameters.** Per-pad parameters are appended for pads 10–16: 7 × 11 = 77 new IDs. Hosts
  keep their automation of pads 1–9, because IDs are only ever appended.
- **Cost.**
  - Seven more pad engines is about 7 × 1.5 MB of voice slots when idle. That is acceptable
    after M8's 48-slot cut.
  - The callback cost grows with voices sounding, not with pads (M8 measurement).
  - Empty pads cost nothing per block: there is an early-out to add for unloaded engines.
- **State 5 or 6.** Old kits load into bank A; bank B starts empty.
- **GM default notes** for bank B: 44 pedal hat, 47 low-mid tom, 50 high tom, 51 ride,
  56 cowbell, 54 tambourine, 57 crash 2.

**Recommendation.** Build Part A first. It answers "more toms, more percs" without more
pads, and keeps the grid at nine ideas. Build Part B only if real kits still run out of
pads with family pads.

## Part C: EXPANDED becomes a playable kit surface (small additions)

- **Auto Map "Playable kit":** one key per sound for every family, toms placed low to high,
  hats and cymbals grouped, with the gaps closed. One click, previewed like the existing Auto
  Map presets.
- **Velocity to pitch per family** comes from per-pad modulation (just built): toms rise a
  little when hit harder.
- **Round robin per key** is kept: repeated presses of one key cycle that sound's takes,
  never another sound's.

## Open questions for the user

1. Hats as one family pad (closed/pedal/open with choke inside), or keep two hat pads as
   today? The proposal allows both; the default is the question.
2. Should the on-screen pad, when clicked, play the lead sound only, or cycle through the
   sounds? The proposal says lead sound; cycling is option-click.
3. 16 pads (Part B): wanted now, or only after trying families?
