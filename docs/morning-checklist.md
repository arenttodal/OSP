# Morning checklist

These are the things I need from you: listening tests, checks in a DAW, and decisions.
Items are grouped by phase. The newest items are at the bottom of each section.

## Decisions I made overnight (please confirm or overrule)

1. **Pitch bake-off.** A (resampling) stays the default and C (formant compensation) is
   dropped. B is added as a second option, **Pitch Character: Natural**, built from
   offline octave anchors. See `docs/reports/pitch-bakeoff-1.md`. Question: should
   *Natural* become the automatic default for organ-like sustained sources? You chose B
   on all four organ groups.

## Listening tests

All in one page, **the listening lab**: https://claude.ai/artifact/SBtjwht3czzVXQbzpmeWUR.
There is one tab per experiment. Ratings and notes save automatically, and I read them
from the page. If a tab is new, the takes are blind and in a random order.

1. **Sustain** (Phase 3 exit test): 5 sources each held for 60 s and released. The
   four takes are: a single naive loop, the best single loop, multi-loop, and
   multi-loop with movement. Main question: *can you hear the loop?* The measurements
   predict that multi-loop removes audible repetition. Single loops score 0.7–0.9 on my
   repetition measure, multi-loop 0.1–0.2. No clicks were detected in any take.
2. **Repetition** (Phase 4 exit test): 4 sources (Tagel, pluck, violin, vocal), each as
   steady repeats (8 × 0.5 s) and fast repeats (12 × 0.16 s). The four takes are:
   identical retrigger, baseline B (independent random pitch and level), the
   performance engine at LIFE 50 %, and at LIFE 80 %. Main question: *does it stop
   sounding like the same retrigger without sounding random?* Measured per-note spread
   on the Tagel at LIFE 50 %: 2.1 dB level, 1.0 semitone brightness, 3 cents. Real
   repeated takes spread 3.5 dB, 1.4 semitones and 3.7 cents, so LIFE 80 % is about
   the real spread.
3. **Dynamics** (Phase 5 exit test): 5 expressive sources (violin, sax, vocal,
   nyckelharpa, pluck), each played at velocities 20 → 127. The four takes are: gain
   only, gain plus velocity filter, the dynamic model at DYNAMICS 50 %, and at 100 %.
   Main question: *does the crescendo sound like playing harder?* Measured: the model
   moves brightness by 3.5–6 semitones on harmonic-rich sources and changes the
   attack-to-body ratio by 8–16 dB. The vocal and pluck are nearly pure tones, so
   there velocity changes mostly the attack.
4. **Original ↔ Reimagined** (Phase 6 exit test): 4 sources each play an arpeggio and a
   held chord at Reimagined 0, 25, 50, 75 and 100 %. Rate each take on its own. Main
   question: *is there a useful sound across the whole range, not only at the ends?*
   Toward Reimagined the instrument adds sympathetic resonance tuned to the source's own
   partials and body, more granular continuation, more drift, and gentle harmonic
   saturation above 50 %.
5. **Multi-sample** (Phase 7 exit test): the plucks set (16 files) and the Tagel set
   (14 files) each play a phrase across their range plus fast repeats. The three takes
   are: one file with the plain sampler, one file with the OSP engine, and the whole
   set combined automatically. Main question: *does dropping the set make a better
   instrument with no mapping?*

## Checks in a DAW (need your Mac)

- Load the AU/VST3 in Logic, Ableton and Reaper. Drop a corpus sample, play, save the
  project, reopen it, and confirm it sounds identical.

## Plugin checks for the new features (need your Mac)

- Drop several files, or a folder, onto the plugin. Open **Samples** and check that the
  pitch groups, velocity layers and round robins make sense, then correct one (role,
  layer or root). Save, reopen, and it should sound the same.
- Move the five macros and Original ↔ Reimagined while holding a chord. There should be
  no clicks, and every position should be usable.
- Pitch Character Tape vs Natural on an organ or vocal, ±1–2 octaves.
- Sustain Recording vs Endless, holding notes for a minute.

## Questions
