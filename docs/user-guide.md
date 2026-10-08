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
MOVEMENT, SPACE and envelope: one instrument, not three. Only how far each source departs
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

### Mixing the layers

**MIX** sits in the header between the preset and VOLUME, and grows with the instrument:
nothing with one sound; with two a small line **A ─●─ B** (drag the node; the middle is
both at equal power); with three the line unfolds upwards into a triangle with **C** on
top (A bottom left, B bottom right). Adding C makes it heard at once - it takes a third of
the mix while A and B keep the balance you had (a centred pair puts all three in the middle);
removing a layer keeps the other two's balance. While
you hover or drag, a small readout shows each layer's share; double-click for equal shares.
With three layers, click **MIX** for the large mix. The mix keeps the total level steady
wherever it is.

### REIMAGINED modes

Click a card's **REIMAGINED** name: a small popover opens above it with the layer's mode
at the top right (click it for the list), a picture of what the mode does to this sound,
and the mode's own two or three settings. Each layer has its own mode (A can be TAPE
FRAME while B is MOSAIC); every mode remembers its settings when you switch away and back.
The knob on the card says how far into the mode the layer goes.

| Mode | What it makes of the recording | Settings |
|---|---|---|
| **KALEIDOSCOPE** | Refraction: richer versions of the sound itself - sympathetic resonance tuned to it, a more fluid sustain, a moving doubling, wandering formants, at the far end grains and gentle saturation. The original Reimagined. | **FOCUS** recognisable ↔ abstract; **SPREAD** width and variation (50 % / 50 % is the sound every earlier patch had) |
| **TAPE FRAME** | Mechanical memory: the sound laid onto a finite piece of tape and played by tape speed - low notes slower, darker and longer, high notes quicker and brighter; each note a slightly different pass; at high amounts faint ghost passes and the tape running out at its end (lower amounts rewind it, quieter each time). | **AGE** bandwidth, saturation, wear; **STABILITY** wow, flutter, pass-to-pass variation; **FRAME** SHORT / CLASSIC / LONG (about 3.6 / 7.6 / 11.4 s of tape at the root) |
| **TOYBOX** | Primitive digital mutation: a small, clever digital memory (low rate, few bits) whose head can turn back and forth through the sound, modulated by its own envelope, with short memory fragments. | **MOTION** turning and self-modulation; **DIGITAL** rate, resolution, bandwidth; **PLAY** FWD / TURN / CHAOS |
| **MOSAIC** | Harmonic reconstruction: the sound rebuilt from its harmonic fingerprint (partials plus its breath and noise), at any pitch, travelling through the recording's own evolution. | **DETAIL** how many partials and how much fine detail; **MOTION** a stable snapshot ↔ the recording's movement; **MODEL** PURE / TEXTURED |
| **MIRAGE** | Early-digital weight: the sample clock follows the note (low notes grainy and dark, high notes sharp), into a driven resonant 4-pole low-pass. | **CLOCK** old-digital intensity; **FILTER** the filter's character, resonance and drive; **TONE** DARK / OPEN |

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
a fresh set. DYNAMICS and the player's slow drift still apply on top, as they would to a
real round-robin set.

## The controls

| Control | What it does |
|---|---|
| **LIFE** | How differently each note is performed. 0 = the identical recording every time; around 50 % = a believable player (calibrated on real repeated takes); 100 % = freer, related reinterpretations. Repeated fast notes alternate naturally. Popup: NATURAL / LOOSE / FRAY; CHARACTER (AUTO reads the sample; PLUCK, SYNTH, DRUM use round-robin models); TAKES (see below); PITCH, TONE, ATTACK. |
| **DYNAMICS** | What velocity does besides volume. 0 = volume only. Higher = soft notes darker and gentler, hard notes brighter with more bite and a slightly sharp start. |
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
performances, the touch response) with its few controls. Escape, a click elsewhere or the
name again closes it.

**Advanced** (bottom right; it stays open with the project): Fine tune, Bend range,
**Voices** (*Poly*, or *Mono* for basses and leads: one note at a time, the newest key
wins, a key played while another is held changes the note's pitch without restarting it
(legato) and releasing it returns to the key still held; **GLIDE** sets how long the pitch
slides between notes, also from the last note into a fresh one; 0 = instant),
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
| Note on/off, velocity | play (velocity → level and, with DYNAMICS, performance) |
| Sustain pedal (CC 64) | hold |
| Pitch bend | ± Bend range |
| Mod wheel (CC 1) | opens MOTION |
| Channel pressure / aftertouch | intensity: louder and brighter while pressing |
| CC 74 | brightness (MPE "slide") |
| CC 20–26 | LIFE, DYNAMICS, CHARACTER, MOTION, SPACE, A's REIMAGINED, ECHO |
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
