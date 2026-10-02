# Morning checklist

Everything I need from you: listening tests, checks on your Mac, and decisions. The
summary of the night is in `docs/reports/overnight-phases-2-8.md`.

## 1. Listening tests

**Round 1 is done**; the results and what changed are in `docs/reports/lab-1.md`.

**Round 2** is on the same page (https://claude.ai/artifact/SBtjwht3czzVXQbzpmeWUR),
about 20 minutes. Each tab re-tests one change:

| Tab | What changed since round 1 | Listen for |
|---|---|---|
| **Reimagined 2** | the far end now does much more: a slowly moving doubling, stronger resonance, a halo a fifth above | is there now a useful range from 0 to 100 %, and is the source still recognisable at 100 %? |
| **Sustain 2** | movement follows how much the recording moves by itself | multi-loop with and without movement: more alive, or an unwanted wobble? |
| **Dynamics 2** | the dynamic model's strength follows the kind of sound | level only vs the new default: which crescendo sounds like playing harder? |
| **Multi-sample 2** | sets are levelled by how notes start (the plucks set jumped up to 19 dB) | one file vs the whole set |

## 2. Decisions I made overnight (please confirm or overrule)

1. **Pitch bake-off.** A (resampling) stays the default, C (formant compensation) is
   dropped, and B became **Pitch Character: Natural**.
   *Question:* should Natural become the automatic default for organ-like sustained
   sources? You picked B in all four organ groups.
2. **Release.** When the recording has a natural ending, a Release of 0.2 s or more
   lets that ending play instead of a fade. Shorter settings fade as before.
3. **Default macros.** LIFE 50 % (raised after round 1), DYNAMICS 50 %, CHARACTER 50 %, MOTION 35 %,
   SPACE 20 %, Reimagined 20 %. Sustain defaults to *Endless*. Too much, or too little,
   out of the box?
4. **Old sessions.** Projects saved with the Phase 1 plugin open with neutral engine
   settings, so they keep sounding like the plain sampler.
5. **MIDI mapping.**
   - Mod wheel opens MOTION.
   - Pressure adds level and brightness.
   - CC74 sets brightness.
   - CC 20–25 control the six macros.
   - MPE bend range is ±48 semitones.
6. **Transient preservation is now off by default.** It lost in round 1 on the pluck,
   bowed and violin. It stays in the code as an option.
7. **Recordings over a minute skip Natural pitch.** Natural keeps four extra copies of
   the sound, so with long recordings it plays as Tape, and the status line says so.
   This halves memory for long files.
8. **Presets and instruments live in `Documents/OSP/Presets` and
   `Documents/OSP/Instruments`.** They are ordinary files, so you can share them or
   sync them with Dropbox.

## 3. Checks on your Mac

- [ ] Install the test build: on the latest CI run, download the **OSP-macOS-test**
  artifact, then unzip it and the `OSP-macOS-test.zip` inside it. Double-click
  `install.command` (if macOS refuses, right-click → Open). It is a universal build, for
  Apple Silicon and Intel, and is not signed yet.
- [ ] **Logic, Ableton, Reaper:**
  - Drop a sample, play, save, reopen: it should sound identical.
  - Bounce twice: the two bounces should be identical.
  - Automate the macros: there should be no clicks.
- [ ] Drop **several files or a folder** (for example `research/corpus/plucks`). Open
  **Samples**: do the groups, layers and round robins make sense? Correct one, save,
  reopen.
- [ ] **Pitch Character** Tape vs Natural on the organ and the vocal, ±1–2 octaves.
- [ ] **Sustain** Recording vs Endless: hold a chord for a minute.
- [ ] **Starting states:** step through all seven while holding a chord.
- [ ] **Advanced panel:** it is now closed by default (spec §13: the macros are the
  instrument). Open it with *ADVANCED ▸* under the macros. Is that the right default for
  you?
- [ ] **☰ menu:**
  - export an instrument and import it in a fresh project (or on a second machine);
  - Undo/Redo after loading a second sample;
  - Interface size 80–200 %;
  - **Presets**: save two or three, make a sub-folder in `Documents/OSP/Presets`, then
    browse them with ☰ → Presets and Previous/Next preset.
- [ ] If you have one, an **MPE controller** (Advanced → MPE): per-note bend, pressure
  and slide.
- [ ] **CPU:** 16 held voices at 48 kHz / 128 in your DAW's meter. CI's Apple Silicon
  runner now measures 4.9 % on average (the target is under 25 %).

## 4. Questions

1. **Factory examples (spec §100).** These need legally owned sounds (vocal, bowed,
   synth, pluck, a strange texture). May I use files from your corpus for the
   *Load example* button, and which ones? Right now it loads a synthetic vowel.
2. **Your organ recordings mix different stops** (some flute-like, some reedy). Should
   a dropped set be split by timbre as well as pitch (into separate "instruments" or
   articulations), or is one set per stop what you would do anyway? Today the louder
   stops become "velocity layers", so velocity also moves between stops (and the new
   multi-velocity learning smooths the steps between them). That is wrong if a stop is
   not a dynamic.
3. **Names and identity for packaging.** The plugin is currently "OSP" by "OSP"
   (codes `Ospx`/`Osp1`, bundle ids `com.osp.*`). What company or developer name and
   identifiers should the signed installer use, and do you have a Developer ID?
4. **Windows.** It builds and passes all tests in CI. Ship it with v1, or launch
   Mac-first?
5. **Next listening round.** After you have rated the lab, should I tune toward your
   preferences straight away, or first run the next experiments (a second pitch round
   with more sources per family, a quieter or louder default for LIFE and SPACE)?
