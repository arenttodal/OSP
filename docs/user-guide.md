# ANDOR/OSP user guide

**ANDOR/OSP, One Shot Performer. Drop in a sound. Play an instrument.**

## Getting a sound in

- **Drop one file** (WAV, AIFF or FLAC; mono or stereo; 44.1–96 kHz) onto the window, or
  use **Browse…** in the empty window (or **⋮ → Load sound…**). It is playable almost
  immediately; the status line shows what is still being prepared in the background.
- One sound is a whole instrument: it gets one large card the width of the window.
- **⋮ → Load example** loads a synthetic vowel.
- Rest the pointer on (or click) the layer's letter **A / B / C** to see the sample's full file
  name, its length, sample rate, channels, bit depth and root.
- The detected root is shown next to the layer's letter. If it is wrong (or `?`), choose
  the right one in the card's **⋮ → Root** (undoable with Cmd/Ctrl+Z).
- Your files are copied into OSP's own sample library, so projects still open when the
  originals move.

## One, two or three sounds

OSP grows with what you give it, up to three sounds (layers **A**, **B**, **C**):

- **Drag another sound over the instrument**: the cards make room and an **ADD LAYER**
  place appears beside them; drop there to add it. Dropping onto a card says
  **REPLACE A** (or B, C) and replaces that layer's sound (its root goes back to
  automatic; its controls stay). With three layers you can only replace.
- **Drop several files at once**: each becomes its own layer (up to three; if you drop
  more, OSP says how many were left out). **Drop a folder** to make ONE multi-sample sound
  from all its files (pitches, velocity layers and round robins are sorted out for you;
  the card's **⋮ → Samples** shows and corrects the result).
- **⋮ → Add layer…** does the same from a file browser.
- **Remove** a layer from its card's **⋮** menu (it asks once more). The layers after it
  move up (A + C never stays: it becomes A + B). **⋮ → Restore removed layer** brings it
  back.
- Click a card to make it the **edited** layer (its letter is in colour); the menu acts on
  it. All layers stay visible and playable.

Every layer plays every note you play, through the same LIFE, DRIVE, CHARACTER,
MOVEMENT, ECHO, SPACE, dynamics and envelope: one instrument, not three. Only how far each source departs
from itself is its own: every card has its own **REIMAGINED**.

### A layer's card

| Control | What it does |
|---|---|
| **M / S** (beside the letter) | MUTE silences the layer; SOLO hears only the soloed layers (several can be soloed; Alt/Option-click S to solo this one alone). A layer that is not heard is veiled (MUTED / NOT SOLOED); its held notes wait and come back with it. Saved with the project; a new sound in a slot starts unmuted. |
| **ONE SHOT / GRANULAR** | Play the recording through, or as a cloud of grains (below). |
| **START** | Where notes begin: further into the sound (One Shot), or added to POS (Granular). The orange marker shows it. |
| **TUNE** | The layer's own transposition, ±24 semitones (whole steps; Alt/Option-drag for fine). |
| **PAN** | The layer's place left–right. |
| **LEVEL** | The layer's level, from silent to +6 dB. |
| **REIMAGINED** (spectral arc) | How far this source travels into its REIMAGINED mode (below). 0 % is the recording itself; double-click returns there. A can stay almost untouched while B is fully reimagined. Click the **REIMAGINED** name to choose the mode. |
| **LINK** (chain) | Linked layers move together: turning one linked layer's START, TUNE, PAN, LEVEL or REIMAGINED moves the others by the same amount, so their relationship stays. |
| **REVERSE** | Play backwards (Granular: every grain backwards). With LOOP on, a held note keeps going backwards the way a forward note sustains. Reimagined acts on a reversed layer exactly as on a forward one. |
| **LOOP** | One Shot: hold a note and it continues on the recording's own loops (off: it plays once and ends). Granular sustains by itself, so LOOP rests there. |
| **FOLLOW** | On: the recording's own loudness contour. Off: its fades are lifted (up to 24 dB) so a decaying sound plays on like a held tone; the envelope then shapes it. |

While you play, an orange read head per note follows the recording (One Shot); you see it
jump back where the sustain continues and forward where the release joins the ending.

### A layer's EQ

**EQ** in a card's header opens that layer's equaliser over its waveform (the card stays as it
is; **EQ** again, or ×, closes it). Each layer has its own, so one sound can lose its rumble
while another gets brighter. A small light on EQ means the layer's EQ is on.

At first the graph is almost empty: five quiet rings on the middle line, from left to right
**HP** (high pass: removes the lows below it), **LOW SHELF**, **BELL**, **HIGH SHELF** and **LP**
(low pass: removes the highs above it). Drag a ring and that band starts working (the first one
also switches the EQ on): across for its frequency, up or down for boost or cut (HP and LP only
move across). The mouse wheel over a band sets how wide it is (Q) — for HP and LP, how steep
(12 or 24 dB per octave). Double-click a band to put its boost back to 0 (HP and LP: their
frequency back). The values in the top row can be dragged, or double-clicked to type
(`3.2k`, `-4`, `2`); next to them, the band's own switch and a reset. The power symbol at the top
left switches the whole EQ off and on without losing the settings.

The curve is exactly what you hear. The EQ works the same for One Shot, Granular, reversed and
REIMAGINED sounds and for ARP notes, and comes after the layer's REIMAGINED, before its LEVEL and
PAN. Closing the editor leaves the EQ playing. Sounds from before have it off and sound exactly
as they did. An LFO (or the mod wheel, or an envelope) can be dragged onto the bell's frequency
or gain or a shelf's gain, or assigned there with a right-click.

### Mixing the layers

**MIX** sits in the header between the preset and VOLUME, and grows with the instrument:
nothing with one sound; with two a small line **A ─●─ B** (drag the node; the middle is
both at equal power). While you hover or drag, a small readout shows each layer's share;
double-click for equal shares. The mix keeps the total level steady wherever it is. With
three layers the A/B/C triangle is hidden for now (it is being redrawn); the three layers keep
the mix they have, and a sound saved with a triangle mix plays exactly as before. Use each
layer's LEVEL to balance three.

### REIMAGINED modes

Click a card's **REIMAGINED** name: a small popover opens above it with the layer's mode
at the top right (click it for the list), a picture of what the mode does to this sound,
and the mode's own two or three settings. Each layer has its own mode (A can be TAPE
FRAME while B is MOSAIC); every mode remembers its settings when you switch away and back.
The knob on the card says how far into the mode the layer goes.

| Mode | What it makes of the recording | Settings |
|---|---|---|
| **KALEIDOSCOPE** | Refraction: richer versions of the sound itself - sympathetic resonance tuned to it, a more fluid sustain, a moving doubling, wandering formants, at the far end grains and gentle saturation. The original Reimagined. | **FOCUS** recognisable ↔ abstract; **SPREAD** width and variation (50 % / 50 % is the sound every earlier patch had) |
| **TAPE FRAME** | Mechanical memory: the sound laid onto a finite piece of tape and played by tape speed - low notes slower, darker and longer, high notes quicker and brighter; each note a slightly different pass; at high amounts faint ghost passes. With LOOP on a held note rewinds the tape for another pass (a little more worn each time, never gone); with LOOP off it plays once, to the recording's end. | **AGE** bandwidth, saturation, wear; **STABILITY** wow, flutter, pass-to-pass variation; **FRAME** SHORT / CLASSIC / LONG (about 3.6 / 7.6 / 11.4 s of tape at the root) |
| **TOYBOX** | Primitive digital mutation: a small, clever digital memory (low rate, few bits) whose head can turn back and forth through the sound, modulated by its own envelope, with short memory fragments. | **MOTION** turning and self-modulation; **DIGITAL** rate, resolution, bandwidth; **PLAY** FWD / TURN / CHAOS |
| **MOSAIC** | Harmonic reconstruction: the sound rebuilt from its harmonic fingerprint (partials plus its breath and noise), at any pitch, travelling through the recording's own evolution. | **DETAIL** how many partials and how much fine detail; **MOTION** a stable snapshot ↔ the recording's movement; **MODEL** PURE / TEXTURED |
| **MIRAGE** | Early-digital weight: the sample clock follows the note (low notes grainy and dark, high notes sharp), into a driven resonant 4-pole low-pass. | **CLOCK** old-digital intensity; **FILTER** the filter's character, resonance and drive; **TONE** DARK / OPEN |

Every mode follows the layer's **LOOP** and **REVERSE**: with LOOP on a held note
sustains (also reversed: it never runs back into the attack), with LOOP off it plays the
recording once and ends - TOYBOX's turning head walks through the sound and stops at its
end. A sound with nothing stable to hold (a decaying pluck or bell) cannot sustain in any
mode, as in the original.

A mode change applies to the notes you play next; notes already sounding finish the way
they started. MOSAIC needs a pitched sound: on noise or unpitched material it plays the
recording (the popover says so). Right after loading, TAPE FRAME and MOSAIC may show
**ANALYZING…** for a moment and play the recording until their data is ready.

## Patches from before per-layer REIMAGINED

Projects, presets and starting states made with an earlier version open and sound exactly
as they did: each card's REIMAGINED shows the amount the patch had, and the patch keeps
the way it was processed then (one shared Reimagined stage after the mix). Saving it again
keeps that. The first time you turn a layer's REIMAGINED, the patch switches to
independent per-layer processing (the knobs do not move; from then on each layer's amount
shapes only that layer). Host automation of the old Reimagined parameter keeps working: it
is A's REIMAGINED. Patches from before the REIMAGINED modes open as KALEIDOSCOPE, exactly
as they sounded; choosing a mode or changing a mode setting also switches a patch to
per-layer processing.

## Granular mode

Switch a layer to **GRANULAR** and it plays a stream of short grains taken from the
recording instead of playing it through, so notes sustain for as long as you hold them,
at the pitch you play. Five small controls appear over the waveform. The orange line is
POS, the band is how far SPREAD reaches, and every grain playing right now is a dot that
glides along the waveform where it reads, swelling and fading with its window:

| Control | What it does |
|---|---|
| **POS** | Where in the recording grains are taken (starts in the recording's steady part). |
| **SIZE** | Grain length, 20–400 ms: short is buzzy and smeared, long is smooth and close to the source. |
| **DENS** | Grains per second, 4–40: low is sparse and pulsing, high is a continuous texture. |
| **TUNE** | Extra pitch for the grains, ±12 semitones, on top of the note you play. |
| **SPREAD** | How far around POS grains may come from: low stays on one spot, 100 % reaches anywhere in the recording. |

After you let go no new grains start; the ones playing finish (under the release time).

REIMAGINED works on grains too, each mode in its own way: KALEIDOSCOPE doubles the cloud
(a second stream of grains a little behind POS, a few cents apart, to one side) and, further
up, scatters the grains wider and denser; TAPE FRAME moves POS like a tape head through a
FRAME of the recording, rewinding and passing again while you hold the key (with its wow,
flutter, age and ghosts on the grains); TOYBOX's heads carry POS around (FORWARD loops a
short leg, TURN turns at its ends, CHAOS jumps), with its digital character and echoes;
MOSAIC and MIRAGE rebuild the grains' spectrum.

LIFE works here too: every note takes its grains from its own spot near POS, with its own
grain size, density, spread and a few cents of pitch, so repeated notes sound like
different clouds of the same sound (none at 0 %, clearly different at 50 %). In the LIFE
popup, PITCH sets the pitch differences, TONE the spread and density differences, ATTACK
the size differences; LOOSE varies more and FRAY sometimes jumps far across the recording.

### Round robins (LIFE → TAKES)

With TAKES at ∞ every note is a new performance. Set TAKES to 2–16 and each note keeps
that many fixed takes instead, like a sampled round-robin set: a repeated note steps to
its next take (ORDER: CYCLE) or to any other one (RANDOM), never the same take twice in
a row. The takes are spread evenly and average out to your recording, so none drifts
off-pitch. LIFE sets how far apart they are, CHARACTER how they differ (a PLUCK's harder
takes are louder, brighter and start a touch sharp; a DRUM's have no pitch). **NEW** rolls
a fresh set. Dynamics (Advanced) and the player's slow drift still apply on top, as they would to a
real round-robin set.

## The controls

| Control | What it does |
|---|---|
| **LIFE** | How differently each note is performed. 0 = the identical recording every time; around 50 % = a believable player (calibrated on real repeated takes); 100 % = freer, related reinterpretations. Repeated fast notes alternate naturally. Popup: NATURAL / LOOSE / FRAY; CHARACTER (AUTO reads the sample; PLUCK, SYNTH, DRUM use round-robin models); TAKES (see below); PITCH, TONE, ATTACK. |
| **DRIVE** | Saturation of the whole instrument (all layers together, before ECHO and SPACE): 0 = clean (off), 10-25 % a little harmonic richness, 25-50 % warmth and density, 50-75 % clear saturation with gentle compression, 75-100 % crunchy, still playable breakup. Popup: TUBE / TAPE / CRUNCH, TONE, BODY (see below). |
| **CHARACTER** | The tonal shape: where the filter sits between its MIN and MAX (popup: type, resonance, drive, envelope). |
| **MOVEMENT** | How the sound changes over time (popup: DRIFT, TAPE, CHORUS, PULSE, SHAPER). The mod wheel opens it further. |
| **ECHO** | Repeats of the sound, in parallel with SPACE: how loud they are (0 = off). Popup: TAPE or BBD, FREE / SYNC, MONO / PING-PONG / WIDE, TIME, FEEDBACK, TONE, AGE (see below). |
| **SPACE** | The room: how much of it you hear. Popup: ROOM, HALL, PLATE, SPRING; PRE-DELAY, SIZE, DECAY, DAMP, MOD, WIDTH and the room's EQ (LOW CUT, HIGH CUT; drag the two handles on the picture). |
| **AMP ENVELOPE** (A D S R) | The instrument's one envelope, for every layer: a short attack and decay with low sustain makes a pluck, a slow attack a swell, full sustain a held tone. Drag the points or turn the knobs. A release of 0.2 s or more lets the recording's own ending play when you let go. |
| **VOLUME** (top right) | The instrument's output level: drag the thin slider, double-click for 0 dB. |
| Preset (top) | ‹ › step through the starting states (Natural, Alive, Floating, Broken, Frozen, Dream, Wide, and your own: settings that keep your sounds) and your presets; click the name for the list, ♡ marks a favourite. At the top of the list: **INIT** (an empty patch: no sounds, every setting at its default), **Reset settings** (defaults, the sounds stay) and **Save starting state…** (every setting and the number of slots, no audio; it appears under Starting states and applies to whatever sounds are loaded, keeping empty slots for the rest). |
| Clear all samples (⋮ menu) | Removes every sound but keeps the A/B/C slots and every setting: drop new sounds into the empty cards and they play with the same layer controls, blend or mix and macros. |

Click a macro's name for its popup: each one shows what it does (the reverb's tail, the
filter's curve over your sound's spectrum, the movement over time, a cloud of possible
performances, the drive's curve) with its few controls. Escape, a click elsewhere or the
name again closes it.

**Advanced** (the card under ARP, right of the keyboard; it stays open with the project): Fine tune, Bend range,
**Voices** (*Poly*, or *Mono* for basses and leads: one note at a time, the newest key
wins, a key played while another is held changes the note's pitch without restarting it
(legato) and releasing it returns to the key still held; **GLIDE** sets how long the pitch
slides between notes, also from the last note into a fresh one; 0 = instant),
**Pitch character** (*Tape*: classic resampling, faster and brighter going up; *Natural*:
keeps the recording's speed and movement in other registers), **Dynamics**, **MPE**, and
**Reseed** (a new variation pattern for LIFE and MOVEMENT). The old Sustain switch is each
layer's **LOOP**; Output is **VOLUME**.

**Dynamics** (in Advanced since DRIVE took its place on the panel; it works exactly as
before, and sessions keep their settings): **AMOUNT** is what velocity does besides volume
(0 = volume only; higher = soft notes darker and gentler, hard notes brighter with more bite
and a slightly sharp start); the curve **SOFT / LINEAR / HARD** (soft reaches loud easily,
hard needs a firm touch); **RANGE**, the velocity's level range in dB; **TONE**, how much
velocity moves CHARACTER's filter.

The pitch and mod wheels beside the keyboard act like a controller's.

## MOVEMENT modes

The big knob is how much movement; click MOVEMENT for what kind. Every mode keeps its own
settings, so you can switch back and forth.

| Mode | Settings | What you hear |
|---|---|---|
| **DRIFT** | SPEED, PITCH, TONE | Slow, organic instability of pitch and brightness (the default) |
| **TAPE** | WOW, FLUTTER, WEAR | The whole instrument through an imperfect tape transport |
| **CHORUS** | RATE, WIDTH, STEREO | Vintage bucket-brigade dimension |
| **PULSE** | RATE, SHAPE, STEREO | Free-running tremolo / auto-pan, sine to rounded square |
| **SHAPER** | PATTERN, RATE, TARGET, DEPTH, SMOOTH | A held chord becomes rhythm, locked to your DAW's tempo |

**SHAPER**: pick a pattern, a note value (1/4 to 1/32, with triplets), what it shapes - VOL
(volume), FILTER (a soft low-pass) or BOTH (darker and a little quieter on the closed steps,
rather than off) - DEPTH and SMOOTH (crisp edges to flowing). DEPTH is the MOVEMENT knob
itself (turning either turns both): how far the level travels. At 100 % every pattern's
lowest point closes completely - on VOL that is silence, a gate - and at 10 % the level only
dips by a tenth, an almost sustained rhythm. The strip shows the pattern faintly, what DEPTH
makes of it in front, and where it is. It follows your DAW's bars exactly (any start position, loops,
bounces); with the transport stopped it starts with the first note you play.

| Pattern | Character |
|---|---|
| PULSE | A decaying articulation on every step, beats a little stronger |
| OFFBEAT | Low on the beat, opening on the "and" |
| BREATH | Two slow, rounded swells per bar |
| THREE | An accent every three steps against the bar (3 over 4) |
| FIVE | Groups of five against the bar |
| EUCLID 3 | Three events spread evenly over the bar, quiet between |
| EUCLID 5 | Five events spread over the bar |
| CASCADE | Each beat a little lower than the last |
| RISE | Pulses that grow through the bar, then drop |
| BROKEN | Deliberate, uneven syncopation |
| SCATTER | Mostly open, with a few sparse dips |
| MACHINE | A precise on/off gate on sixteenths (x.xx.xx.x.xx.x.x) |

## DRIVE

The big knob is how hard the instrument is driven; click DRIVE for the circuit. The picture
is the circuit's curve at your settings: input across, output up, the straight dashed line
the clean sound. The more the curve bends, the more the peaks are rounded into harmonics.

| Circuit | What it is |
|---|---|
| **TUBE** (default) | Two tube-like stages: warmth and even harmonics first, rounded breakup as you push it. The low end has its own gentle stage, so a bass note never muddies the chord above it. |
| **TAPE** | Smooth, dense, compressed: a tape's saturation, highs softened first and transients rounded a little more than the sustain (the dashed curve shows a transient being driven harder). No hiss, no wow: that is MOVEMENT's TAPE. |
| **CRUNCH** | An old console or preamp overloaded: tighter bass, forward mids, rawer and more articulate; for old synths, electric pianos, organs and plucked strings. |

**TONE** darkens (softer, warmer harmonics) or opens (clearer, more present) the saturation
without filtering the clean sound. **BODY** goes from lean (clearer attack, a little of the
clean sound kept, less compression) to dense (thicker, rounder, more sustain). DRIVE is
level-matched: turning it up adds density and harmonics, about +1 dB at 100 %, not a jump in
volume. It reacts to how hot it is fed, like real hardware: a quiet single note is driven
less than a full chord.

## ARPEGGIATOR

To the right of the keyboard: the ARP card (a light, ARP, an arrow, and the pattern and rate
below) and Advanced under it.

- **The light** switches the arpeggiator on and off (orange when on).
- **Anywhere else on the card** shows or hides its settings, which open between the macros and
  the keyboard; the round button at the panel's top right hides them too. The window grows by
  that much and shrinks back when you hide them. Showing the settings does not switch it on,
  and switching it on does not show them.

Hold a chord: it plays as a stepped pattern, each note with the velocity you played it with,
through every layer and effect exactly as if you were playing the notes yourself. The display
shows sixteen steps: the one sounding lit, the ones to come as they will play. With nothing held,
it shows the pattern's shape.

| Setting | What it does |
|---|---|
| **PATTERN** (pick from the list; the picture beside it shows the style, and pointing at a name previews it) | UP, DOWN, UP/DOWN and DOWN/UP (the top and bottom once), UP & DOWN and DOWN & UP (the top and bottom twice), CONVERGE (from the outside in), DIVERGE (from the inside out), CON & DIVERGE, PINKY UP and PINKY UP/DOWN (the top note between the others), THUMB UP and THUMB UP/DOWN (the bottom note between the others), PLAYED (the order you pressed the keys), CHORD (the whole chord on every step), RANDOM (never the same note twice in a row), RANDOM OTHER (every note once, then a new order), RANDOM ONCE (one random order, repeated). The random styles are the same every time the song plays from its start |
| **RATE** | a knob from the slowest step to the fastest: 1/4D, 1/4, 1/8D, 1/4T, 1/8, 1/16D, 1/8T, 1/16, 1/16T, 1/32 (D dotted, T triplet) |
| **GATE** | how long each note sounds, 10–150 % of a step; above 100 % notes overlap (legato); double-click for 75 % |
| **OCTAVES** (the four stacked keys) | 1–4: the pattern spans that many octaves (CHORD climbs an octave per step) |
| **SWING** | 0–100 %: every second step plays later, up to half a step (100 % feels dotted); 0 % is straight |

With the host playing, the steps sit on its beat grid. A chord played between steps starts on
the next one, and loops and jumps stay on the grid. With the host stopped (or in the standalone
app), the arpeggiator runs at the host's last tempo (120 BPM if it never said), and the first
note sounds at once.

The **sustain pedal** keeps released keys in the pattern. **All Notes Off** stops everything.
Pitch bend, the mod wheel and pressure work as usual. The settings are automatable and saved
with the project. Projects from before the arpeggiator open with it off and sound exactly as
they did. **Advanced** is the card under ARP (the whole width: the MOD button there is gone,
the modulation tabs are always in the envelope panel).

## MODULATION

Modulation lives in the envelope panel beside the macros. Its small tabs are **AMP** (the
volume envelope), **ENV 1**, **ENV 2**, **LFO 1** and **LFO 2**; one is shown at a time.
After each source's name sits a small **socket**: grab it (or the name) and drag it onto any
knob to modulate that knob. Its centre fills while the source is in use.

There are five sources:

- **LFO 1, LFO 2**: a repeating movement. SHAPE (sine, triangle, ramp up, ramp down, pulse,
  random, or CUSTOM: your own curve), MODE (FREE keeps running, RETRIG starts over when you
  play, ONE SHOT runs one cycle and holds), RATE, PHASE, and VOICES (GLOBAL: one movement for
  the whole instrument; POLY: each note its own). Click the word **RATE** to choose **Sync**
  (1/32 to 8 bars, dotted and triplet, following the song) or **Hz**. The tag in the graph's
  corner (± BIPOLAR / + UNIPOLAR) says whether it swings both ways around the setting or only
  adds; click it, or right-click the graph or the tab, to change it.
- **ENV 1, ENV 2**: a shape every note plays: ATTACK, DECAY, SUSTAIN, RELEASE and CURVE, or a
  ONE SHOT curve over LENGTH. Each note (also every ARP note) has its own. On the macros and
  the EQ (which are shared by all notes) the envelope runs once for the whole instrument:
  every new note restarts it from where it is, and it releases when the last key (and the
  sustain pedal) lets go.
- **MOD WHEEL**: your controller's wheel (MIDI CC 1) or the on-screen one. Drag **MOD**
  (with its socket, under the wheel) onto a knob. Moving the wheel itself still just moves it.
  Until the wheel is routed it opens MOVEMENT up, as it always did. Once you route it, it
  does only what its routes say.

To use one, **drag it onto a knob** and let go. The controls it can reach light up while you
drag. Rest on CHARACTER for a moment and its settings open, so you can drop onto RES. Rest on
a Granular layer's picture and its POS, SIZE, DENS and SPREAD come up for the drop.

Or **right-click any knob** (control-click on a Mac) for its MODULATION menu:
- **Assign MOD WHEEL / LFO 1 / LFO 2 / ENV 1 / ENV 2**: only the sources that can reach that
  knob are offered. One already assigned reads **Edit** and is selected rather than added
  twice.
- The knob's routes, each with Select, Bypass, Depth to 0 and Remove.
- **Remove All Assignments**.

If a route cannot be made (all 16 are in use, or the rules below), a short note says why.

What can move what:
- **START** (each layer's): where a new note begins in the recording, taken as the note
  starts. A sounding note never jumps. A free-running LFO gives each note a different start.
  The envelopes cannot choose START, because they have no value yet when a note begins.
- **ENV 1 / ENV 2's ATTACK, DECAY, SUSTAIN, RELEASE**: the LFOs and the wheel can move them.
  - ATTACK and DECAY are taken as the note starts, and RELEASE as it is released.
  - SUSTAIN follows continuously.
  - An envelope cannot move an envelope.
- A POLY LFO belongs to single notes, so it cannot move the macros or the EQ. Dropped on
  CHARACTER, the envelopes and POLY LFOs move each note's filter. The right-click menu also
  offers an envelope on the whole instrument's CHARACTER.

A modulated knob gets a **halo**, an arc just outside it in the source's colour. It shows the
range the knob moves through and a dot where it is now. Point at it to see the route, its
depth and the range. **Drag the halo** (or the knob with **Option**) up or down for more or
less; below zero turns the movement around, and the knob's own setting never changes. When
several sources move one knob, the selected one is drawn bold and the others as thin arcs
outside.

**Modulating a modulation.** A halo is a target too. Drop a source **on the halo** (the ring)
instead of the knob and it moves that route's **depth**: drop LFO 1 on the halo of ENV 1 →
CHARACTER and ENV 1 still moves CHARACTER, but how much now rises and falls with LFO 1. Drop
it **on the knob** and it moves the knob itself, as before. While you drag, a note under the
control says which it will be ("LFO 1 → ENV 1 / CHARACTER CUTOFF DEPTH" or "LFO 1 →
CHARACTER"). When a knob has several routes, the inner ring is the selected route and the thin
outer arcs the others; where several lie together, a small menu asks which one you mean.
- **Right-click a halo** for that route's depth: **MODULATE THIS DEPTH** offers the sources that
  can do it, **EXISTING DEPTH MODULATION** lists those already on it (edit, bypass, remove), and
  **Remove Depth Modulation** clears them. The knob's own menu is under **This Control**.
- A small dot in the depth source's colour rides just outside the halo: it shows where the
  depth is right now, while the halo itself stays on the depth you set. Point at the halo for
  the base depth, the amount and the range the depth can move through.
- Just after you make one (or pick **Edit Amount**), dragging the halo changes the depth
  source's amount; **Edit Base Depth** (or clicking the route's source) goes back to the route's
  own depth. Your knob setting and the route's depth are never changed by the movement.
- One level only: a depth's amount cannot itself be modulated, and a source cannot move the
  depth of its own route. Removing a route removes what moved its depth (one undo).

**N ROUTES** opens the source's routes: each with its depth (drag the bar; double-click for 0),
a light to bypass it and × to remove it. The corner mark beside it opens a large curve editor.
For CUSTOM and ONE SHOT, drag the points, double-click to add or remove one, drag a line to
bend it; RESET starts over.

Everything is saved with the preset and the project, and can be automated. Sounds from before
have no modulation and sound exactly as they did.

## SPACE and ECHO

| SPACE type | Character |
|---|---|
| **ROOM** | A real small room: its early reflections are a furnished room's walls, then a short, dense tail |
| **HALL** | A concert hall after the Berlin halls for orchestra: a gap, strong reflections from the sides, then a long, warm tail that keeps moving |
| **PLATE** | A steel plate: dense at once, bright, wide, the most motion |
| **SPRING** | A spring tank: its chirp and drip, narrow |

PRE-DELAY is the gap before the room answers, SIZE the room's dimensions, DECAY how long it
rings, DAMP how much sooner the highs die than the mids (0 = bright, 100 % = dark), MOD how
much the tail moves (0 = still, 100 % = lush), WIDTH mono to the room's full width. The EQ
filters what goes into the room (LOW CUT keeps the bass dry and clear, HIGH CUT darkens the
room): drag its corners on the picture, or use the two small knobs; double-click a corner to
reset it.

**ECHO** runs in parallel with SPACE (each hears the dry sound). **TAPE** is a tape echo: each
repeat a little darker and warmer, AGE adds wow and flutter, high FEEDBACK runs away into warm
saturation; moving TIME bends the repeats' pitch like changing tape speed. **BBD** is a
bucket-brigade pedal: longer times are darker, the repeats grainier, AGE adds its slow chorus.
SYNC sets TIME as a note value (1/16 to a bar, dotted and triplet) on your DAW's tempo; FREE in
ms. PING-PONG alternates the repeats left and right, WIDE plays two heads a little apart.

## SHAPER: your own pattern

Choose **CUSTOM** at the end of PATTERN's list (or just click on the pattern: editing any
pattern makes it CUSTOM, starting from it). Click or drag on the display to set each step;
right-click a step to change its shape. The **magnifier** (top right of the display) opens the
large editor: **STEP** sets each step's level with the chosen brush (HOLD, FALL, RISE, PULSE,
DIP, SOFT), **DRAW** paints a free line; **COPY ▾** starts from any pattern, **CLEAR** empties it,
**SAVE…** keeps it under Documents/OSP/Shaper Patterns and **LOAD ▾** (or PATTERN → SAVED) brings
it back. Every edit is one undo step; the pattern is saved with the project and presets.

## MIDI

| Message | Effect |
|---|---|
| Note on/off, velocity | play (velocity → level and, with Dynamics, performance) |
| Sustain pedal (CC 64) | hold |
| Pitch bend | ± Bend range |
| Mod wheel (CC 1) | opens MOTION |
| Channel pressure / aftertouch | intensity: louder and brighter while pressing |
| CC 74 | brightness (MPE "slide") |
| CC 20–27 | LIFE, Dynamics (amount), CHARACTER, MOTION, SPACE, A's REIMAGINED, ECHO, DRIVE |
| MPE (lower zone, toggle in Advanced) | per-note bend (±48 semitones), pressure and slide |

## Saving and sharing

- Projects remember everything: the sample(s), corrections, settings and the exact
  playback. They reopen identically and render bounces identically.
- **☰ → Presets**: settings plus which sample(s) (`.osppreset`). Presets live in
  `Documents/OSP/Presets` as ordinary files: make sub-folders there to organise them,
  and they appear as sub-menus. **Previous / Next preset** step through them.
- **☰ → Export instrument**: one `.ospinstrument` file with the recordings, their
  analysis and the settings, saved in `Documents/OSP/Instruments` by default. Import it
  on another computer and it sounds the same. **☰ → Instruments** lists that folder.
- **☰ → Interface size**: 80–200 %.

## When a sound is unusual

Recordings longer than a minute play with Tape pitch even when Natural is selected (Natural keeps four extra copies of the sound); the status line says so. Files longer than ten minutes are shortened to their first ten minutes.

OSP never refuses a file it can read. With no clear pitch, the root shows `?`; choose
one. Very short, noisy or decaying sounds play as they are (plucks ring out naturally
instead of being held artificially).
