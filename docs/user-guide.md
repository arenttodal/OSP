# OSP user guide

**Drop in a sound. Play an instrument.**

## Getting a sound in

- **Drop one file** (WAV, AIFF or FLAC; mono or stereo; 44.1–96 kHz) onto the window, or
  use **Load…**. It is playable almost immediately. The status line shows what is still
  being prepared in the background ("building sustain…", "preparing registers…").
- **Drop several files or a folder** to make one instrument from all of them. OSP sorts
  them into pitches, velocity layers (from words like *pp, mf, ff, soft, hard* in the
  names, or from clear loudness differences) and round robins. Open **Samples** to see
  the result and correct any file's role, layer or root note.
- **Load example** loads a synthetic vowel.
- The detected root is shown large. If it is wrong (or `?`), choose the right one in the
  **Root** menu. This is undoable (Cmd/Ctrl+Z).
- Your files are copied into OSP's own sample library, so projects still open when the
  originals move.

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
