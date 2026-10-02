# Morning checklist

Everything I need from you: listening tests, checks on your Mac, and decisions. The
summary of the night is in `docs/reports/overnight-phases-2-8.md`.

## 1. Listening tests (about 45–60 minutes in total)

All of them are on one page, **the listening lab**:
https://claude.ai/artifact/SBtjwht3czzVXQbzpmeWUR.

There is one tab per experiment. The takes in each group are blind and in a random
order. Rate each take (1–5 on three scales), pick a best and, if you like, write a note.
Everything saves as you go and I read it from the page. Headphones help.

| Tab | Phase / exit test | What to listen for | What the measurements predict |
|---|---|---|---|
| **Sustain** | 3: 60 s holds without an obvious loop | repeats, bumps, a vibrato cycling identically, the moment the recording ends | single loops repeat strongly (score 0.7–0.9) and multi-loop barely does (0.1–0.2); no clicks found |
| **Repetition** | 4: repeated notes preferred over identical retriggers | machine-gun attacks vs variation that sounds random | LIFE 50 % ≈ ⅔ of a real player's spread, 80 % ≈ all of it; baseline B varies pitch and level but never tone |
| **Dynamics** | 5: a crescendo is more than gain | do soft notes sound softly played, loud notes hard? | +3.5–6 semitones of brightness on harmonic sources; on pure-tone sources mostly the attack changes |
| **Original ↔ Reimagined** | 6: useful sounds across the whole range | rate each of the 5 settings on its own | — |
| **Multi-sample** | 7: a dropped set is automatically better | low and high notes, repeated notes | the register model is closer to real recordings for plucks and organ |
| **Transients** | §19: a pluck moved an octave keeps its real pick | the start of each note: real snap, or a thud (down) / chirp (up)? Any doubled or detached attack? | clear on synthetic picks; subtle on most of your plucks, which have little broadband attack. The bowed groups check that nothing gets worse |

## 2. Decisions I made overnight (please confirm or overrule)

1. **Pitch bake-off.** A (resampling) stays the default, C (formant compensation) is
   dropped, and B became **Pitch Character: Natural**.
   *Question:* should Natural become the automatic default for organ-like sustained
   sources? You picked B in all four organ groups.
2. **Release.** When the recording has a natural ending, a Release of 0.2 s or more
   lets that ending play instead of a fade. Shorter settings fade as before.
3. **Default macros.** LIFE 35 %, DYNAMICS 50 %, CHARACTER 50 %, MOTION 35 %,
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

## 3. Checks on your Mac

- [ ] Install the AU/VST3 from CI (Actions → latest run → `osp-macos-14` artifact), or
  build with `cmake -B build-plugin -G Ninja -DOSP_BUILD_PLUGIN=ON && cmake --build build-plugin`.
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
- [ ] **☰ menu:**
  - export an instrument and import it in a fresh project (or on a second machine);
  - Undo/Redo after loading a second sample;
  - Interface size 80–200 %;
  - **Presets**: save two or three, make a sub-folder in `Documents/OSP/Presets`, then
    browse them with ☰ → Presets and Previous/Next preset.
- [ ] If you have one, an **MPE controller** (Advanced → MPE): per-note bend, pressure
  and slide.
- [ ] **CPU:** 16 held voices at 48 kHz / 128 in your DAW's meter. CI's Apple Silicon
  runner measured 17 % before the latest two optimisations, which halved the cost
  again on the cloud machine.

## 4. Questions

1. **Factory examples (spec §100).** These need legally owned sounds (vocal, bowed,
   synth, pluck, a strange texture). May I use files from your corpus for the
   *Load example* button, and which ones? Right now it loads a synthetic vowel.
2. **Your organ recordings mix different stops** (some flute-like, some reedy). Should
   a dropped set be split by timbre as well as pitch (into separate "instruments" or
   articulations), or is one set per stop what you would do anyway?
3. **Names and identity for packaging.** The plugin is currently "OSP" by "OSP"
   (codes `Ospx`/`Osp1`, bundle ids `com.osp.*`). What company or developer name and
   identifiers should the signed installer use, and do you have a Developer ID?
4. **Windows.** It builds and passes all tests in CI. Ship it with v1, or launch
   Mac-first?
5. **Next listening round.** After you have rated the lab, should I tune toward your
   preferences straight away, or first run the next experiments (transient/body
   separation, a second pitch round with more sources per family)?
