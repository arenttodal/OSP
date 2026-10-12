# ANDOR Drums M6: break slicing

Spec Part 7 and Milestone 6. Drop a drum break and turn it into a playable kit.
Screenshot: `screens/14-break-slicing.png`.

## Workflow

1. **Open a break.** Three ways:
   - **BREAK** in the top bar: click to choose a file, or drop one on it (the dedicated
     target);
   - drop a long recording (over 1.5 s) on a pad and choose **Slice as a break...**;
   - the kit menu's **Slice a break...**.
2. **SLICE BREAK** opens over the pad editor, as KEY MAP does. The kit is not touched until
   Apply. The break loads and is sliced in the background.
3. **Correct.**
   - SENSITIVITY: higher finds softer hits and ghost notes.
   - MIN GAP: two slices are never closer than this.
   - Drag a marker to move a boundary.
   - Double-click to add one. It snaps back to an attack within 15 ms.
   - Select slices and press Delete to join them with the previous slice.
   - Click a slice or a chip to hear it (shift adds to the selection).
4. **Plan.**
   - Every slice is a chip in the tray. A break can have many more slices than nine pads;
     unplanned slices stay in the tray.
   - Drag chips (one or a selection) onto a pad in the grid.
   - Or use **Assign to...**, **Category...**, **To tray** and **Suggest plan**. The
     suggested plan puts the slices in order onto the empty pads.
   - Several slices on one pad become round robins.
5. **Apply...**
   - **New kit from this break.** The kit is named after the file.
   - **Fill the empty pads only.** Slices planned onto used pads stay in the tray, and this
     is reported.
   - **Merge into this kit.** Used pads get the slices as extra round-robin layers.

   Apply is one undo step, and so is every edit before it.
6. **MIDI.**
   - In the slice view, drag **MIDI** into the DAW, or click it to save the groove as a
     `.mid` file. Each hit lands on its planned pad's note, with a velocity from its
     strength, at the estimated tempo.
   - After Apply, the groove is kept with the kit (spec 7.13). The kit menu's **Save groove
     as MIDI...** writes it at any time.

## How it works

- **Non-destructive regions** (spec 7.4).
  - The break is loaded once as one sound.
  - Every slice on a pad is a sample layer that points at that sound, with its own START
    and END: the M3 layer engine, through `InstrumentEngine::NoteOverride` and the voice's
    END fade. No audio is copied per slice.
  - OSP levels the break as a whole, so the slices keep their relative dynamics. Ghost notes
    stay quiet.
- **Fades.** The voice softens a non-zero START over 3 ms (the fade-in). END fades over at
  least 1.5 ms (the fade-out). The 2 ms pre-roll keeps the attack whole.
- **Each slice keeps** the source (by content hash), start, end, onset (the original
  timing), strength, category, confidence, destination pad, and whether the user edited
  it.

### Onset detection (`src/drums/BreakSlicer.*`, pure C++, deterministic)

Multi-band spectral flux, in the spirit of SuperFlux (Böck & Widmer, 2013):

1. **Analysis frames.** Hann windows of about 21 ms, hop a quarter of that.
2. **Log magnitude, four bands.**
   - 30–150 Hz (kick);
   - 150–800 Hz (body);
   - 0.8–4 kHz (snare and clap crack);
   - 4–18 kHz (hats).
3. **Positive change at lag 2** against a three-bin maximum of the earlier frame. Vibrato
   and slow movement are not attacks, and a hit entering the window is seen whole.
4. **Per band:** subtract the band's own running median (±0.1 s), so a cymbal's wash or a
   noise floor does not count. Then normalise by the band's strong hits. A quiet hat over a
   loud kick still stands out.
5. **Combine:** 0.7 × the strongest band (any one instrument is enough) + 0.3 × the mean
   (several at once is stronger evidence).
6. **Peaks** above an adaptive median threshold (±0.12 s) and set by sensitivity, at least
   MIN GAP apart.
7. **Alignment.** Each onset moves to where its attack begins in the waveform. It uses 1 ms
   energy, both plain and pre-emphasised (the one with the clearer rise):
   - the steepest 3 ms rise;
   - its valley before it;
   - the first block 30 % of the way up.

   The envelope is floored at the recording's own noise floor, so the quantisation steps of
   a bit-crushed file are never attacks.
8. **Significance.** A peak is kept only if that rise is real: at least 2.5 + 3 × (1 −
   sensitivity) dB. Steady noise produces no slices (tested).
9. **Slices** run from each onset minus the pre-roll to the next slice's start: the groove's
   own timing. The last slice runs until its sound has died away.
10. **Strength** is the attack level against the loudest hit: −36 dB maps to 0.05, the
    loudest to 1. It becomes the groove's velocity.
11. **Tempo.** The 8th-note grid that best fits the onsets (70–180 BPM), favouring a whole
    number of bars.

### Validation (spec 7.14): `osp_drums_tests "[breaks]"`

The test collection is synthetic, so its true onsets are known exactly. No third-party
audio is in the repository.

- The kick is a pitch-swept sine with a click.
- The snare is noise plus a 190 Hz body.
- The hats, open hat and crash are bright noise with different decays.

Hits within 30 ms count as one, and a hit is found if it is within 20 ms. The start column
is where the slice starts against the true attack (negative is before it).

| Break | Hits | Missed | Extra | F | Start |
|---|---|---|---|---|---|
| Clean electronic | 16 | 0 | 0 | 1.000 | −5.2 … 0.0 ms |
| Acoustic (room tail, −54 dB noise) | 20 | 0 | 2 | 0.952 | −3.9 … 0.0 ms |
| Funk 16ths | 32 | 0 | 0 | 1.000 | −5.3 … 0.0 ms |
| Lo-fi (8-bit, dull, −42 dB noise) | 16 | 0 | 1 | 0.970 | −3.9 … 0.0 ms |
| Ghost notes (−18 dB) | 18 | 0 | 0 | 1.000 | −3.7 … 0.0 ms |
| Kick and hat together | 16 | 0 | 0 | 1.000 | −4.0 … 0.0 ms |
| Fast hats (32nds at 110) | 31 | 1 | 0 | 0.984 | −3.9 … 0.0 ms |
| Swing (2:1) | 16 | 0 | 0 | 1.000 | −3.9 … 0.0 ms |
| Quiet transients (−30 dB) | 16 | 0 | 0 | 1.000 | −3.9 … 0.0 ms |
| Long cymbal tails | 12 | 4 | 0 | 0.857 | −3.9 … 0.0 ms |

- **No slice ever starts after its attack.** The transient is never cut.
- **Known limitation: hats under a fresh crash.** The 4 misses are hats in the first second
  under a crash as loud as they are; the crash's own wash masks them. Raising SENSITIVITY,
  or adding a boundary by double-click, recovers them. A local normalisation that found them
  doubled the false slices elsewhere, so it was not kept.
- **Not compared against reference implementations.** The spec asks for a comparison, but
  none (aubio, madmom, librosa) is available in this build environment. The method follows
  the published SuperFlux design. The comparison is listed for a session with those tools.

Other tests:
- `andor_drums_plugin_tests "[breaks]"`:
  - import, plan, undo of slice edits, preview;
  - the three apply modes, with undo;
  - the groove as MIDI, and recall;
  - the view: drops on pads, Delete, category.
- Slicer unit tests: sensitivity, minimum gap, manual add/delete/move, tempo, and silence,
  noise and tiny files.

## State

`<Groove filename hash bpm><Hit seconds pad strength/>…</Groove>` keeps the last applied
break's timing. This is an additive element, so the state schema stays 3. The slices
themselves live on as pad layers, saved as in M3.

## Not done here

- **Classification** (spec 7.5, 7.6). Categories are the user's here. Automatic kick, snare
  and hat suggestions, grouping similar slices into round robins, and the layered/uncertain
  flag are Milestone 7. The data model already carries `category`, `confidence` and
  `layered`.
- **The sample browser's context menu** (spec 7.2). Drums has no browser yet.
