# OSP user guide

**Drop in a sound. Play an instrument.**

## Getting a sound in

- **Drop one file** (WAV, AIFF or FLAC; mono or stereo; 44.1–96 kHz) onto the window, or
  use **Browse…** in the empty window (or **⋮ → Load sound…**). It is playable almost
  immediately; the status line shows what is still being prepared in the background.
- One sound is a whole instrument: it gets one large card the width of the window.
- **⋮ → Load example** loads a synthetic vowel.
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

Every layer plays every note you play, through the same LIFE, DYNAMICS, CHARACTER,
MOVEMENT, SPACE, ORIGINAL ↔ REIMAGINED and envelope: one instrument, not three.

### A layer's card

| Control | What it does |
|---|---|
| **ONE SHOT / GRANULAR** | Play the recording through, or as a cloud of grains (below). |
| **START** | Where notes begin: further into the sound (One Shot), or added to POS (Granular). The orange marker shows it. |
| **TUNE** | The layer's own transposition, ±24 semitones (whole steps; Alt/Option-drag for fine). |
| **PAN** | The layer's place left–right. |
| **LEVEL** | The layer's level, from silent to +6 dB. |
| **LINK** (chain) | Linked layers move together: turning one linked layer's START, TUNE, PAN or LEVEL moves the others by the same amount, so their relationship stays. |
| **REVERSE** | Play backwards (Granular: every grain backwards). With LOOP on, a held note keeps going backwards the way a forward note sustains. Reimagined acts on a reversed layer exactly as on a forward one. |
| **LOOP** | One Shot: hold a note and it continues on the recording's own loops (off: it plays once and ends). Granular sustains by itself, so LOOP rests there. |
| **FOLLOW** | On: the recording's own loudness contour. Off: its fades are lifted (up to 24 dB) so a decaying sound plays on like a held tone; the envelope then shapes it. |

While you play, an orange read head per note follows the recording (One Shot); you see it
jump back where the sustain continues and forward where the release joins the ending.

### Mixing the layers

The band under the cards: **ORIGINAL ↔ REIMAGINED** always; with two layers the **A / B
blend** above it (left only A, middle both at equal power, right only B); with three a small
**triangle**: drag the dot towards A, B or C (double-click: all three equal). The numbers
beside it say how much of each you hear. The mix keeps the total level steady wherever it is.

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

LIFE works here too: every note takes its grains from its own spot near POS, with its own
grain size, density, spread and a few cents of pitch, so repeated notes sound like
different clouds of the same sound (none at 0 %, clearly different at 50 %). In the LIFE
popup, PITCH sets the pitch differences, TONE the spread and density differences, ATTACK
the size differences; LOOSE varies more and FRAY sometimes jumps far across the recording.

## The controls

| Control | What it does |
|---|---|
| **LIFE** | How differently each note is performed. 0 = the identical recording every time; around 50 % = a believable player (calibrated on real repeated takes); 100 % = freer, related reinterpretations. Repeated fast notes alternate naturally. |
| **DYNAMICS** | What velocity does besides volume. 0 = volume only. Higher = soft notes darker and gentler, hard notes brighter with more bite and a slightly sharp start. |
| **CHARACTER** | The tonal shape: where the filter sits between its MIN and MAX (popup: type, resonance, drive, envelope). |
| **MOVEMENT** | How the sound changes over time (popup: DRIFT, TAPE, CHORUS, PULSE, SHAPER). The mod wheel opens it further. |
| **SPACE** | The room: how much of it you hear (popup: ROOM, CHAMBER, PLATE, SPRING and DECAY). |
| **ORIGINAL ↔ REIMAGINED** | How far the instrument moves away from the recording: sympathetic resonance tuned to your sound, more fluid sustain, more movement, and at the far end gentle harmonic saturation. |
| **AMP ENVELOPE** (A D S R) | The instrument's one envelope, for every layer: a short attack and decay with low sustain makes a pluck, a slow attack a swell, full sustain a held tone. Drag the points or turn the knobs. A release of 0.2 s or more lets the recording's own ending play when you let go. |
| **VOLUME** (top right) | The instrument's output level. |
| Preset (top) | ‹ › step through the starting states (Natural, Alive, Floating, Broken, Frozen, Dream, Wide: settings that keep your sounds) and your presets; click the name for the list, ♡ marks a favourite. |

Click a macro's name for its popup: each one shows what it does (the reverb's tail, the
filter's curve over your sound's spectrum, the movement over time, a cloud of possible
performances, the touch response) with its few controls. Escape, a click elsewhere or the
name again closes it.

**Advanced** (bottom right; it stays open with the project): Fine tune, Bend range,
**Pitch character** (*Tape*: classic resampling, faster and brighter going up; *Natural*:
keeps the recording's speed and movement in other registers), **MPE**, and **Reseed** (a
new variation pattern for LIFE and MOVEMENT). Velocity range is DYNAMICS' **RANGE**; the
old Sustain switch is each layer's **LOOP**; Output is **VOLUME**.

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
| **SHAPER** | PATTERN, RATE, TARGET, SMOOTH | A held chord becomes rhythm, locked to your DAW's tempo |

**SHAPER**: pick a pattern, a note value (1/4 to 1/32, with triplets), what it shapes - VOL
(volume), FILTER (a soft low-pass) or BOTH (darker and a little quieter on the closed steps,
rather than off) - and SMOOTH (crisp edges to flowing). MOVEMENT is the depth: 25 % is a
subtle articulation, 50 % clearly rhythmic, 100 % the full pattern. The strip shows the
pattern and where it is. It follows your DAW's bars exactly (any start position, loops,
bounces); with the transport stopped it starts with the first note you play.

| Pattern | Character |
|---|---|
| PULSE | A decaying articulation on every step, beats a little stronger |
| OFFBEAT | Low on the beat, opening on the "and" |
| BREATH | Two slow, rounded swells per bar; never closes far |
| THREE | An accent every three steps against the bar (3 over 4) |
| FIVE | Groups of five against the bar |
| EUCLID 3 | Three events spread evenly over the bar, quiet between |
| EUCLID 5 | Five events spread over the bar |
| CASCADE | Each beat a little lower than the last |
| RISE | Pulses that grow through the bar, then drop |
| BROKEN | Deliberate, uneven syncopation |
| SCATTER | Mostly open, with a few sparse dips |
| MACHINE | Fast and precise, down to silence |

## MIDI

| Message | Effect |
|---|---|
| Note on/off, velocity | play (velocity → level and, with DYNAMICS, performance) |
| Sustain pedal (CC 64) | hold |
| Pitch bend | ± Bend range |
| Mod wheel (CC 1) | opens MOTION |
| Channel pressure / aftertouch | intensity: louder and brighter while pressing |
| CC 74 | brightness (MPE "slide") |
| CC 20–25 | LIFE, DYNAMICS, CHARACTER, MOTION, SPACE, REIMAGINED |
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
