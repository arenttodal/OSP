# Trimming and slicing (Stage 8)

Turning a long recording into usable one-shots without touching the original. Code:
`src/library/Slicer` (pure C++), `library::saveDerivedSound` (`SoundImport`), the editor
`LibraryPanel::TrimSheet` (`apps/plugin/Source/LibraryCapture.cpp`). Tests:
`[unit][library][slicer]` and `[plugin][library-capture]`.

## The algorithms (Sample Forge port, D-04)

Sample Forge's `slicer.js` is the user's own tested code. It was reviewed in Stage 0
(01-sample-forge-audit.md) and ported to C++ with its tests, not linked: it is
JavaScript, and the algorithm is small.

- **Envelope**: log energy, RMS of 512 samples every 128, on the mono mix.
- **Noise floor**: the 10th percentile of the frame energies.
- **Onsets**: a frame that passes two tests, at least 50 ms after the last onset. With
  `s` the sensitivity (0–1):
  - it is above the gate, floor + max(6 dB, (peak − floor) × (0.45 − 0.35 `s`));
  - it rose by at least 14 − 10 `s` dB within about 10 ms.
- **Where a slice starts**: each onset moves back to where the attack crosses 10 % of its
  peak, minus 2 ms of pre-roll. It never cuts into an attack: the tests allow < 1 ms late
  and < 10 ms early, at 48, 44.1 and 22.05 kHz.
- **Slice end**: the next onset, or where the sound has died away (three 10 ms windows below
  max(floor + 4 dB, peak − 50 dB)), or the longest slice.

**OSP's change**: the longest slice is 20 s, not Sample Forge's 1.5 s. A sustained note is a
valid one-shot, and a 5 s held note stays one slice (tested). Inside the classifier the
limit is 600 s.

**Trim suggestion** (`suggestTrim`): the first and last 5 ms windows above −60 dB relative to
the peak (and above floor + 6 dB). The start gets 10 ms of pre-roll, the end 50 ms of tail.
It is conservative on purpose. A breathy 400 ms attack keeps everything louder than 55 dB
under the peak (tested), and the release stays. A silent recording is left as it is.

**Rendering** (`renderSlice`): the original's samples, unchanged, between the two marks.

- **Anti-click fades**: 0.5 ms in, 5 ms out.
- **Zero crossing** (optional): the start snaps to the nearest zero crossing within 1 ms.
- **Gain** (optional): "Bring each peak to −1 dB" is off by default. Nothing is normalised
  unless asked, and the gain is capped at +24 dB.
- **Stereo** stays stereo, channels aligned frame for frame (tested).

**Reversed**: the fades make both ends silent, so a slice played by OSP's REVERSE starts
and ends cleanly (tested).

## The editor (view I)

**Right-click → Trim / Slice…** opens it, on any sound, on a scanned candidate or on an
Inbox item. It is also in the candidate inspector.

**The waveform.**

- Zoom with + / − or the wheel, and scroll.
- A click places the cursor. Dragging a slice's edge moves it; a slice is at least 10 ms.
- The playhead shows while a slice plays.

**Buttons.**

- **Suggest trim**: one region, start and end as above.
- **Find slices**: the slicer, at the sensitivity on the slider.
- **Split at cursor**, **Merge with next**, **Remove slice** (only from the proposal),
  **Rename…** (inline).
- **▶ Slice**: hears the selected slice as it will be saved, fades and gain included.
- **Reset**: back to the whole recording.

**The slice list.** The box in front of each slice includes or leaves it out. Its name and
length are shown.

**Options.** "Bring each peak to −1 dB", and "Start each slice at a zero crossing".

**Save as New Sound** (or **Save N Slices to Library**) renders every included slice and
saves each one as a **new Sound** in the background, then closes. The original's record,
file and store copy are never changed.

## Provenance (spec 8.5)

Each slice is an ordinary sound record. Its `sounds` row holds:

- `parent_asset`: the original's id;
- `slice_start` / `slice_end`: in seconds;
- `operations`: JSON with the fades, gain, zero crossing, `"slicer": "sampleforge-1"`, the
  analysis version, and the source path when cut from a candidate (docs/library/schema.md).

Its origin is the original's (Captured for an Inbox recording). Its Type is the original's
Type or the analysis' suggestion. It is analysed when saved, so it shows its kind like any
sound. `Catalog::derivedFrom (id)` lists an original's slices in time order.

**Cutting from a candidate.** A scanned file or Inbox item is not a Library sound yet. So
saving its slices first imports the whole recording, unchanged and with its evidence, so
every slice can name its original. The status line says so.

**Identical audio.** A slice whose bytes are already in the Library returns that sound; its
provenance is kept, not overwritten. The status line counts these apart: "had exactly the
audio of a sound already in the Library".

**Atomic saves.** Each slice is written as a float32 temporary file in the store
(`.derived-…`, never taken for a sound). Then it goes through the import transaction (hash,
verified copy, record) and the temporary file is removed. An interrupted save leaves no
half sound; startup clears such leftovers.

## Tests

The unit tests (`[unit][library][slicer]`) cover:

- every hit of a kit, at three rates, including quiet hits;
- room noise alone gives no hit, even at high sensitivity;
- close hits, and empty and tiny input;
- a 5 s sustained note is one slice;
- the trim keeps a breathy attack and the release;
- fades, the zero crossing, stereo alignment, the gain and its cap;
- the source is unchanged;
- a very quiet take (−41 dBFS peaks) over room noise;
- slices play backwards cleanly;
- saved slices are new sounds with their provenance, in the store, analysed, with no
  leftovers;
- identical audio keeps the existing record;
- an empty slice is refused.

`[plugin][library-capture]` uses the real window. A groove of eight hits is sliced from the
Scanned list:

- the original is imported first;
- eight new sounds are saved;
- the original's file is byte-for-byte unchanged;
- `derivedFrom` lists eight;
- a slice loads into C.
