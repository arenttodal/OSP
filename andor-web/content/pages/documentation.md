---
title: Documentation
kicker: Support
summary: How to get a sound in, play it, shape it and keep it. The complete guide comes with the release.
---

> This is the short guide. The complete user guide (every control, menu and setting) will be
> published here with the first release.

## Getting a sound in

- **Drop one file** onto the window (WAV, AIFF or FLAC; mono or stereo; 44.1–96 kHz), or use
  **Browse…** in the empty window. It is playable almost at once; the status line shows what is
  still being prepared in the background.
- The **root note** is found for you and shown beside the layer's letter. If it is wrong, or shows
  `?`, choose the right one in the card's **⋮ → Root**.
- Your files are copied into OSP's own sample library, so projects still open when the originals
  move.

## One, two or three sounds

OSP plays up to three sounds as one instrument (layers **A**, **B**, **C**).

- Drag another sound over the instrument and drop it on **ADD LAYER**. Dropping on a card
  replaces that layer's sound.
- Drop several files at once and each becomes its own layer. Drop a **folder** to make one
  multi-sample sound: pitches, velocity layers and round robins are sorted out for you.
- Every layer plays every note, through the same macros and envelope. Each layer has its own
  playback mode, **REIMAGINED**, EQ, **SYNTH** and arpeggiator lane.

## The six macros

| Macro | What it does |
|---|---|
| **LIFE** | How differently each note is performed. 0 % is the identical recording every time, around 50 % a believable player, 100 % a freer reinterpretation. |
| **DRIVE** | Saturation of the whole instrument: TUBE, TAPE or CRUNCH. |
| **CHARACTER** | The tonal shape: a filter that moves the sound's own body resonances. |
| **MOVEMENT** | How the sound changes over time: DRIFT, TAPE, CHORUS, PULSE or a SHAPER pattern. |
| **ECHO** | Repeats: TAPE, BBD, or ASCEND, where the repeats climb an interval. |
| **SPACE** | The room, from small rooms to huge, freezable spaces. |

Click a macro's name for its page: a picture of what it does, with its few controls. Escape, a
click elsewhere or the name again closes it.

## REIMAGINED

Every card has a **REIMAGINED** knob that takes the layer from the recording itself toward a
mode's far end. Click the card's REIMAGINED name to choose the mode and its settings.

| Mode | What it makes of the recording |
|---|---|
| **KALEIDOSCOPE** | Sympathetic resonance tuned to the sound, a fluid sustain, moving doubling and wandering formants. |
| **TAPE FRAME** | The sound on a finite piece of tape: low notes slower and darker, every pass a little more worn. |
| **TOYBOX** | A tiny digital memory whose head turns back and forth through the sound. |
| **MOSAIC** | The sound rebuilt from its harmonic fingerprint, at any pitch. |
| **MIRAGE** | Early-digital weight into a driven, resonant 4-pole filter. |

## Granular and KEY DNA

Switch a layer's playback mode to **Granular** and it plays a stream of short grains from the
recording, so notes sustain as long as you hold them.

- **POS** is where the grains come from, **SIZE** their length (20–400 ms), **DENS** how many
  per second (4–40), **TUNE** extra pitch (±12 semitones), and **SPREAD** how far around POS they
  may come from.
- **KEY DNA** gives each key its own grain behaviour: draw a curve across the keyboard for any
  grain setting.

## SYNTH and IMPRINT

Every layer has an analog oscillator. The **SYNTH** chip in the top-right of a card's display
sets how much of it you hear; click it to open the SYNTH page.

- **Analog:** saw, square, triangle or sine, with octave, detune and tone. It follows the keys,
  or the recording's own tuning and vibrato.
- **Imprint:** the recording's spectral body shapes the oscillator. **MOTION** lets that colour
  change through the note.

## Recording and capturing

Press **REC** on a card to **RECORD** a take of up to a minute, or **GRAB** the last 10, 20, 30 or
60 seconds after you played them. The take is trimmed, chopped into its notes, and each note is
placed on its key. In a DAW, send a track to OSP's sidechain input; in the standalone app, choose
your input in **Options**.

## Arpeggiator

Open **ARP** beside the keyboard: 18 patterns, rate, gate, octaves, swing and velocity, synced
to your host. **PLAYS A / B / C** chooses which layers it plays; turn off **LINK** to give each
layer its own pattern.

## The Library

Every sound you load is kept in the Library: searchable, playable, with collections, tags,
favourites and notes.

- **Inbox:** choose folders to watch (a Voice Memos export, Dropbox or iCloud Drive) in
  **⋯ → Inbox folders…**. New recordings appear under **Sounds → Inbox**.
- **Trim / Slice…:** right-click a sound to cut it before you play it.
- **Back up the Library…** writes everything (sounds, presets, tags, history) to one folder.
  **Restore a backup…** brings it to another computer, asking first what to do with anything in
  both places.

## Saving and sharing

- Projects remember everything and reopen exactly; bounces render the same every time.
- **Presets** are ordinary files in `Documents/OSP/Presets`. Make sub-folders to organise them.
- **Export instrument** makes one `.ospinstrument` file with the recordings, their analysis and
  your settings. It sounds the same on another computer. When a sound's rights are unknown, OSP
  asks you to confirm you may share it.

## When a sound is unusual

OSP never refuses a file it can read. With no clear pitch, the root shows `?`, so choose one. Very
short, noisy or decaying sounds play as they are. Files longer than ten minutes are shortened to
their first ten minutes.
