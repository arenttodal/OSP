# ANDOR Drums M7: smart classification and batch import

Spec Part 6 (batch import), §7.5–7.8 (classification and grouping for breaks) and
Milestone 7. Screenshot: `screens/15-import-preview.png`.

## What a musician does

1. **Drop several files.** Any of these opens **IMPORT SAMPLES**:
   - dropping several files on **BREAK**;
   - choosing **Sort into the kit (import preview)...** when dropping them on a pad (the
     ticked default on an empty kit);
   - the kit menu's **Import samples...**.

   Nothing changes until Import.
2. **The plan.**
   - **One card per group.** A group is the files one pad would play, as round robins.
   - **The destination:** a pad, or the tray. When that pad already plays something, the
     card says "replaces …".
   - **The category,** and every file with a confidence dot: green is sure (≥ 75 %), amber
     likely, grey unsure. Hover over a file for the reason: "the file name and the sound
     agree", "the file name (the sound suggests closedHat)", "unsure (closest: tom)".
3. **Correct.** Each of these is one undo step:
   - click a file to hear it (shift selects several);
   - right-click a file: **Own pad**, or **Move to group**;
   - **Group selected** joins files on one pad;
   - click a destination or category to change it;
   - **Reset suggestions** goes back to the classifier's plan.
4. **Import** puts every group that has a pad on it, with its category and that category's
   play modes (kick mono, hat chokes), as one undo step.
   - Groups left in the tray stay in the preview: more than nine groups are never dropped
     (spec 6.5).
   - Files that could not be loaded, and duplicates (the same content), are listed with the
     reason and never placed twice.

Breaks (M6) now use the same classifier: every slice gets a category and confidence, and is
flagged when it holds several instruments. The suggested plan groups alike slices onto the
pad of their category: kicks on the kick pad, hats on the hat pad. Slices of very different
strength on one pad become velocity layers rather than round robins, so a ghost note stays a
ghost note (spec 7.8).

## How it decides (`src/drums/DrumClassifier.*`, pure C++, deterministic)

1. **The file name first, when it is reliable** (spec 6.2).
   - **Words.** Kick, snare, clap, tom, rim, crash, ride, shaker, conga and the like. They
     also count as the start of a longer word ("kicks"), and camelCase and digits are split
     ("KickSoft02"). Confidence 0.9.
   - **Abbreviations count only as whole tokens.** BD, SD, CH, OH, LT, RS and so on;
     "abdomen" is not a kick. Confidence 0.7.
   - **Hats.** "Hat"/"HH" with "open" is an open hat; otherwise a closed hat. "Hi tom" is
     not a hat. "Snare rimshot" is a snare.
   - **A name naming two things** is not trusted alone (0.5).
2. **The sound.** Features of the first hit:
   - the share of energy in six bands (< 80 Hz, 80–200 Hz, 200–800 Hz, 0.8–3 kHz,
     3–8 kHz, > 8 kHz);
   - the spectral centroid of the first 80 ms;
   - flatness, measured per octave (noise against tone, independent of tilt);
   - the decay to −20 dB;
   - the attack time (5 ms average);
   - attack bursts in the first 50 ms (claps), on the pre-emphasised signal;
   - the strongest peak under 400 Hz and its prominence (toms, congas);
   - the centroid of the tail (cymbals stay bright).

   Each category scores a product of soft thresholds on these. The best score wins, with a
   confidence that falls when the runner-up is close.
3. **Together.**
   - The name and the sound agree: the higher confidence.
   - They disagree, and the sound is sure (≥ 0.75): the name wins at 0.55 confidence, with
     the sound's suggestion in the reason.
   - Below 0.45: **Unknown**. Nothing is claimed with low confidence.
4. **Layered hits.** Strong sub-bass together with strong highs (a kick and a hat at once)
   is flagged as layered. The confidence drops by 30 %, and the slice and tray show "?".
5. **Grouping.**
   - **Joining a group.** A file joins a group when the category matches and every member
     sounds alike. "Alike" is a feature distance mapped to 0–1: ≥ 0.35 with the same name
     stem ("Kick_01" and "Kick_03" are both "kick"), or ≥ 0.8 without one.
   - **Different sounds stay apart.** A long 808 and a short kick are not grouped, even
     when both are kicks (spec 6.4).
6. **Mapping.** Groups in kit order go first to a free pad of their own category, then to
   any free pad; the rest go to the tray. On a kit already in use, only empty pads are
   suggested.

The interface `drums::DrumClassifier` is what a licensed model would implement later. The
shipped `FeatureClassifier` is the transparent one above.

## Model research and licences (spec 7.7)

No pretrained model is shipped.

- **CLAUDE.md rule 5** forbids neural inference until a later phase calls for it.
- **No candidate is cleared.** None below is cleared for commercial redistribution without
  further steps.

What was checked:

| Candidate | Code / runtime | Weights | Use in a commercial plugin |
|---|---|---|---|
| Essentia models (MTG-UPF) | Essentia library: AGPL-3.0 | CC BY-NC-ND 4.0 for non-commercial use, proprietary licence on request ([licensing](https://essentia.upf.edu/licensing_information.html), [models LICENSE](https://essentia.upf.edu/models/LICENSE)). The LICENSE file names BY-NC-ND but links the BY-NC-SA legal code: to be confirmed with UPF. | Only with a proprietary licence from UPF |
| madmom (CP-JKU) | BSD | CC BY-NC-SA 4.0. Commercial users are asked to contact the authors ([PyPI](https://pypi.org/project/madmom), [paper](https://bpb-us-e1.wpmucdn.com/wp.nyu.edu/dist/2/2294/files/2016/08/b%C3%B6ck-madmom.pdf)) | No, without permission |
| YAMNet (Google) | Apache-2.0 (tensorflow/models) | Apache-2.0 for Google's release ([README](https://github.com/tensorflow/models/blob/master/research/audioset/yamnet/README.md)). Converted copies elsewhere carry other licences (e.g. [Qualcomm AI Hub](https://aihub.qualcomm.com/compute/models/yamnet) lists MIT) | Plausible from Google's own release. But it is a general AudioSet classifier, not a drum classifier, so it would need evaluation. The AudioSet training data's own terms need a legal review first |
| ONNX Runtime / TensorFlow Lite | MIT / Apache-2.0 | – | A permissive runtime does not make any model's weights usable (spec 7.7) |

**Not evaluated here:** accuracy on real one-shots and break slices, model size, CPU cost
and Apple Silicon performance. They need a licensed test set and the runtimes, neither of
which is in this environment. That evaluation is the step before any model is considered.

## Validation (`osp_drums_tests "[classify]"`, `andor_drums_plugin_tests "[import]"`)

The sounds are synthesised with seeded variations, 12 per category. Results with sound
alone (file named "sample.wav"):

| True \ guessed | kick | snare | clap | closed | open | tom | rim | cymbal | perc | unknown |
|---|---|---|---|---|---|---|---|---|---|---|
| kick | 12 | | | | | | | | | |
| snare | | 12 | | | | | | | | |
| clap | | | 9 | | | | | | 3 | |
| closed hat | | | | 12 | | | | | | |
| open hat | | | | | 12 | | | | | |
| tom | | | | | | 12 | | | | |
| rim | | | | | | | 11 | | | 1 |
| cymbal | | | | | | | | 12 | | |
| percussion | | | | | | 6 | | | 6 | |

- Overall 107 of 108 are confident, and 9 are wrong (8 %).
- **Congas** (tonal, 250–450 Hz) read as toms. **Some claps** read as percussion.
- **Names fix both** in practice: "conga" and "clap" are reliable words.
- **Real recordings are not measured.** These numbers are on synthetic sounds; no licensed
  real-recording test set exists in the repository. That is the first thing to measure with
  a user's library.

Other tests:
- **Names:** words, abbreviations as whole tokens only, open versus closed hats, "hi tom",
  ambiguous names.
- **Name and sound together:** agreement raises confidence; disagreement lowers it with a
  reason; no evidence gives Unknown.
- **Layered:** a kick plus a hat is flagged and less confident; a plain kick is not.
- **Grouping:** three kick takes in one group, the 808 apart; snares together; kit order.
- **Mapping:** a same-category pad first; never two groups on one pad; 12 groups for 9 pads
  leave 3 in the tray; a kit in use gets only its empty pads.
- **Scenario 6, end to end:**
  - 15 samples plus a broken file and a duplicate, onto an empty kit;
  - grouping checked;
  - the 808 merged into the kicks;
  - a group's category corrected;
  - imported: 15 files on 9 pads;
  - the kick pad plays;
  - one undo empties the kit again.
- **Import view:** split, move, group the selection; a destination taken from another
  group sends that one to the tray; every correction undoes.
- **Breaks:** the M6 test break's kicks, snares and hats are classified and planned onto
  the kick, snare and hat pads.
