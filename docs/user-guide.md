# OSP user guide

**Drop in a sound. Play an instrument.**

## Getting a sound in

- **Drop one file** (WAV, AIFF or FLAC; mono or stereo; 44.1–96 kHz) onto the window, or
  use **☰ → Load sample into A…**. It is playable almost immediately. The status line
  shows what is still being prepared in the background ("building sustain…", "preparing
  registers…").
- **Drop several files or a folder** to make one instrument from all of them. OSP sorts
  them into pitches, velocity layers (from words like *pp, mf, ff, soft, hard* in the
  names, or from clear loudness differences) and round robins. **☰ → Samples** shows the
  result and lets you correct any file's role, layer or root note.
- **☰ → Load example** loads a synthetic vowel.
- The detected root is shown large. If it is wrong (or `?`), choose the right one in
  **☰ → Root**. This is undoable (Cmd/Ctrl+Z).
- Your files are copied into OSP's own sample library, so projects still open when the
  originals move.

## Two layers: A and B

- The small **A / B** tabs at the top left of the display choose which layer you are
  editing. Dropping or loading a sound goes into that layer; the other one is untouched.
  The display, the file name (bottom left), the root and **ONE SHOT / GRANULAR** (bottom
  right) all belong to the edited layer.
- While you play, an orange read head per note follows the recording in the display (in
  ONE SHOT); you see it jump back where the sustain continues and forward where the
  release joins the recording's ending.
- The slider at the top right blends them: all the way left only A, the middle both
  (equal power), all the way right only B. An empty layer is silent.
- Both layers go through the same LIFE, DYNAMICS, CHARACTER, MOVEMENT, SPACE and
  ORIGINAL ↔ REIMAGINED. **☰ → Clear layer** empties the edited layer.

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
| **CHARACTER** | The instrument's body. Up = smaller and brighter, down = larger and darker. It moves the recording's own resonances, not just an EQ tilt. |
| **MOTION** | How much the sound keeps moving while you hold it: drift in level, pitch, colour and stereo, and how often the endless sustain moves around the recording. Almost no effect on plucks. The mod wheel opens it further. |
| **SPACE** | Width and air: stereo width, spread for mono recordings, a soft ambience. |
| **ORIGINAL ↔ REIMAGINED** | How far the instrument moves away from the recording: sympathetic resonance tuned to your sound, more fluid sustain, more movement, and at the far end gentle harmonic saturation. |
| Starting state | Natural, Alive, Floating, Broken, Frozen, Dream, Wide: macro settings that keep your sound. |

The name of the last preset you opened or saved appears under the starting state.

**Advanced** (click *ADVANCED ▸* under the macros; it stays open with the project): Attack, Release (a release of 0.2 s or more lets the recording's own
ending play when you let go), Velocity range, Fine tune, Bend range, Output, **Pitch
character** (*Tape*: classic resampling, faster and brighter going up; *Natural*: keeps
the recording's speed and movement in other registers), **Sustain** (*Recording*: notes
end when the recording ends; *Endless*: hold as long as you like without an obvious
loop), **MPE**, and **Reseed** (a new variation pattern for LIFE and MOTION).

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
