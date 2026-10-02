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

## Checks in a DAW (need your Mac)

- Load the AU/VST3 in Logic, Ableton and Reaper. Drop a corpus sample, play, save the
  project, reopen it, and confirm it sounds identical.

## Questions
