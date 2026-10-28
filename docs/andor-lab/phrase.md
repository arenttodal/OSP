# ANDOR Phrase — performance to instrument

ANDOR Phrase turns a recorded monophonic performance (voice, violin, nyckelharpa, flute, a
lead synth line) into a polyphonic instrument: it finds the notes, puts each on its key,
groups repeats as variations and makes the result playable at once. Corrections are fast
and never required before playing.

| | |
| --- | --- |
| Plug-in | `apps/phrase` (`ANDOR_Phrase`: VST3, AU on macOS, Standalone) |
| Engine | `src/andor/phrase/PhraseEngine` (real-time), `PhraseEdits` (corrections) |
| Analysis | `docs/andor-lab/phrase-analysis.md` |
| Identity | ANDOR Phrase · `Ospx` / `Aphr` · `com.osp.andorphrase` · state tag `ANDOR_PHRASE_STATE` |

## Workflow

1. **Import**: drag a WAV / AIFF onto the window, or IMPORT (file picker), or PRESET ▾ → Load
   example phrase. Decoding, hashing, copying into the managed store and analysis run on a
   worker thread with a progress bar and CANCEL; the editor stays responsive. Up to 300 s
   load; 10–30 s phrases are the normal case.
2. **See** the detected notes over the waveform: colour by pitch class, label `E4 +12`,
   solid outline when confident, dashed when not, the sustain body as a band, read heads of
   the sounding voices. The keyboard map shows recorded keys (amber, with the number of
   variations), filled keys (pale) and keys that cannot play (hatched).
3. **Play** at once: chromatically, in chords (a monophonic recording plays polyphonically).
   If notes are cut too finely or too coarsely, try **DETECTION** (FEWER / NORMAL / MORE: how
   readily a held sound is cut into notes) and **MIN NOTE** (50–500 ms, default 250 ms: shorter
   pieces without a pitch of their own — a fragment of the same note, a slide, an octave slip —
   join the neighbouring note; short notes with their own pitch stay). Either change
   re-analyses in the background once the control rests (~0.35 s), keeps every corrected note
   and is one undo step: play the result, and UNDO puts the notes *and* the control back. Each
   setting's analysis is cached, so switching back is instant.
4. **Correct** what is wrong (all undoable): select a note (click it, or a key on the map),
   drag its markers (attack start, sustain start / end, end), PITCH − / + and OCT − / + (an
   octave or note-name correction), PLAYS ON (map the recording to another key as it is),
   INCLUDED / EXCLUDED, SPLIT (at the dashed cursor set by clicking inside the selected note,
   else in the middle), MERGE > (with the next note), RE-ANALYSE NOTE, UNDO / REDO.
5. **Save**: the host session (or a preset `.andorphrase`) stores the parameters, the source
   reference and the full Note Atlas with every correction. Reopening plays the same
   instrument sample for sample without analysing again.

## Mapping

- **Exact notes**: a note detected at C4 plays on C4. Several C4 takes form the C4 variation
  group.
- **Variations** (VARIATIONS): IN TURN (sequential), RANDOM, SHUFFLE (never the take that
  played last on that key). Random choices come from `Prng::deriveSeed (seed, event, key)`
  and restart when the host transport starts: offline bounces are deterministic.
- **Fill Keyboard** (FILL KEYBOARD): a key without a recording takes the nearest recorded key
  within an explicit limit and transposes it: OFF 0, NATURAL 2, BALANCED 5 (default), WIDE
  12, EXTENDED 24 semitones. Beyond the limit the key stays silent — a slice is never
  stretched several octaves silently. Ties between a key below and above go to the more
  confident take, then the lower key (deterministic). WIDE and EXTENDED show "large
  transpositions change the timbre": playback is resampling, not formant-preserving.
- **Tuning**: IN TUNE plays every note exactly on its key (the detected cents are corrected);
  AS PLAYED keeps the performance's deviation (only the rounding to the key is applied).
- **PLAYS ON** vs **PITCH**: PITCH says *what note the recording is* (detection fixes; its
  cents are kept). PLAYS ON *moves* the recording to another key where it sounds as recorded
  (like setting a sample's root key) — the same meaning in ANDOR Grain.

## Playback modes

| Mode | Behaviour |
| --- | --- |
| ONE SHOT | plays the note's region from its attack to its end; note-off is ignored |
| GATE | plays while held; note-off releases with the amp envelope; the region's end ends it |
| SUSTAIN (default) | plays the attack, then loops the sustain body for as long as the key is held; note-off cross-fades (20 ms) into the recording's own release and the amp envelope releases |

Loop points (`buildPhraseModel`): a whole number of pitch periods covering ~60 % of the
sustain, its start aligned where the cross-fade windows correlate best (normalised
cross-correlation, ±1 period); equal-power cross-fade when the ends correlate poorly
(< 0.7), linear otherwise. **No loop is shorter than 120 ms** (`minLoopSeconds`, the loop of a
200 ms sustain, the shortest that sounded like the note in the first user tests): shorter loops
repeat fast enough to buzz. Regions whose sustain cannot hold one, or noisy notes, do not loop:
in SUSTAIN they play to their stable end and then hold with a soft granular cloud of their
body, its grains (80 ms down to 20 ms) fitted inside the note so they never read into a
neighbour and the note's own pitch is heard (a graceful fallback, not a pretend loop).

**SUSTAIN CHARACTER** blends the clean loop (0) with a granular bloom of the sustain (1):
80 ms Hann grains at 20 ms intervals, seeded per note.

Listening is still required to judge the loops (see `test-instructions.md`). The automated
test only proves the absence of steps larger than the waveform's own (no clicks) and a
steady level over 6 s holds.

## Controls (parameters, stable IDs)

| Group | Parameters |
| --- | --- |
| Play | `playMode`, `roundRobin` (VARIATIONS), `fill`, `tuning`, `polyphony` (4–28, default 16), `seed` |
| Character | `attackPreserve` (KEEP ATTACK: 100 % starts at the recorded attack, 0 % at the sustain with an 8 ms fade), `sustainCharacter`, `synthetic` (Natural ↔ Synthetic: a band-limited tone at the played pitch that follows the recording's own loudness), `tone` (low-pass 300 Hz–20 kHz; open = bypassed) |
| Envelope | `attack`, `decay`, `sustain`, `release` |
| Level | `gain` (MASTER), `velocity` (0 = ignored, 1 = 40 dB range), `levelMatch` (0 the recording's dynamics, 1 every note at one level) |
| Pitch | `pitch` (±24 st), `fine` (±100 c), `bendRange` |
| Effects | `space` (OSP's hall reverb as a send), `echo` (OSP's tape delay as a send) |
| Analysis | `detection` (FEWER / NORMAL / MORE), `minNote` (0.05–0.5 s): saved with the session, **not automatable** (each change re-analyses) |
| Other | `startOffset` |

MIDI: note on/off (velocity), sustain pedal, pitch bend, All Notes Off (CC 123), All Sound
Off (CC 120, immediate). Voices: a pool of 32 with the polyphony limit; stealing takes the
oldest released voice, then the oldest, with a 4 ms fade; a repeated key releases its
previous voice (no pile-up).

## Formants

OSP's pitch-character work (Signalsmith Stretch "Natural" anchors, formant compensation) was
inspected but not reused: it is built per sample for one root, the formant compensation was
rejected in OSP's own listening bake-off, and it is offline. Phrase therefore plays by
high-quality resampling (Kaiser-windowed sinc, `SincInterpolator`) and warns that large
transpositions change the timbre. Formant-preserving transposition is not claimed.

## State and presets

- Host state: XML tree `ANDOR_PHRASE_STATE` (version 1) with the APVTS parameters, the source
  (`hash`, `name`, `originalPath`) and the atlas JSON. Recall finds the audio in the managed
  store by hash, then at the original path. A missing source keeps the atlas, shows "Missing:
  … (menu > Relink)" and plays silence; RELINK checks the chosen file (same fingerprint =
  exact; same length/rate/channels = accepted with a new fingerprint; otherwise refused).
- Presets: the same tree in `Documents/ANDOR Lab/Phrase Presets/*.andorphrase`.
- Another product's state (Grain, OSP, garbage) is ignored without changing anything.
- Re-analyse (menu) keeps every note the user touched and only replaces untouched ones;
  undoable. DETECTION / MIN NOTE changes do the same; their undo step restores the controls too.
- Recall plays the saved notes as they were, whatever analysis version made them: reopening
  never re-analyses (the controls are taken as what the saved notes were analysed with).
- Export Note Atlas (menu) writes `.andoratlas` for ANDOR Grain (`docs/andor-lab/note-atlas.md`).

## Real-time safety

The audio thread reads only the immutable `PhraseModel` (published with `ModelExchange`,
freed on the message thread after the audio thread reports it unused), the parameters'
atomics and a lock-free audition queue (the editor's keys; `juce::MidiKeyboardState` is not
used because it locks). `processBlock` and every engine call are tested allocation-free.
