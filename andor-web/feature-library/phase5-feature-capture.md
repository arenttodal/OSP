# Website feature capture (Phase 5): workflow and shot lists

**Purpose:** six flagship features, captured from the real editor. The workflow is ready now. The
musical material comes from the owner.

**Not flagship material:** the synthetic tutorial examples. Every frame made from them is named
`…-workflow-test` and is only a check of the workflow. Those frames stay out of the site.

**Three kinds of footage, never mixed up:**

| Kind | What it is | Named / stored |
|---|---|---|
| **Screenshot** | A still of the real editor drawn headless by the plugin's own code, with audio running through the instrument | `F*/NN-…@2x-{owner,workflow-test}.png` |
| **Headless sequence** | Frame-accurate editor frames from a scripted performance (MIDI and automation through `processBlock`) | `seq-…/NNNN.png` and an MP4 marked "headless" |
| **DAW recording** | A screen recording of the standalone or a DAW session on a Mac, with sound | Recorded by the owner (below); never generated |

## 1. The capture code (local, not committed)

- **File:** `tests/plugin/LocalFeatureCapture.inc`, tag `[feature-capture]`.
- **Build:** the D2 line plus the pilot packs.
- **Run:**

```bash
OSP_FEATURE_CAPTURE=<out> \
OSP_FEATURE_PHRASE=<owner phrase.wav> \
OSP_FEATURE_LIVE=<owner live take.wav> \
OSP_FEATURE_SOUND=<owner single note.wav> \
  osp_plugin_tests "[feature-capture]"
```

- **Without the owner's files:** the run uses the synthetic phrase and the tutorial examples, and tags every file `-workflow-test`.
- **Log:** `capture-log.txt` records what each state actually contains: notes found, takes per key, the gap key, and whether GRAB LAST kept the take.

| | Feature | What the code does (real APIs, no simulated behaviour) | Files |
|---|---|---|---|
| F1 | **Phrase Transformer / Note Atlas** | `analyseRecording` (as a drop does) → mapped keys → NOTES open → the most-repeated key pressed three times (each press lights its round-robin take) → a key with no note of its own (FILL lights the neighbour's take) → a held chord with LOOP on (sustain) | `01-phrase-mapped-keys`, `02-note-map-open`, `03-round-robin-press1..3`, `04-gap-filled-<key>`, `05-sustained-chord-2s` |
| F2 | **Retrospective live capture** | Empty instrument → **Record…** → the input connected as a host sidechain → the take played in → **GRAB LAST** (`grabLast`) → the grabbed take played | `01-empty-instrument-record`, `02-listening-after-playing`, `03-grabbed-as-instrument`, `04-grabbed-played` |
| F3 | **One sound becomes an instrument** | Empty → one sound loaded → five notes across five octaves | `01..03` |
| F4 | **Three independent arpeggiators** | LINK off. A plays nothing (held chord: the pad). B plays CHORD at 1/8 (pulse). C plays UP & DOWN at 1/16 over 3 octaves (runs). ARP panel open | `01-A-pad-B-pulse-C-runs` |
| F5 | **Granular KEY DNA** | Granular. KEY DNA on: SIZE long → short and DENS sparse → dense over the keys, POS variation 0.5, PAN ramp, individuality 0.6. Low, mid and high keys held | `01-key-dna-size-dens-pos-pan`, `02-granular-display-low-mid-high` |
| F6 | **SYNTH + IMPRINT** | Four states on one sound: original (SYNTHESIZE 0); pure oscillator (100, Analog); blended (50, Analog); imprinted (100, Imprint, imprint 80) | `01-original` … `04-imprinted` (+ `-card` crops) |

**Custom ARP patterns** are not shown: none is verified in this build. Only the 18 shipping patterns
appear.

## 2. Shot lists for the owner's recordings

**General:**
- Standalone OSP on a Retina Mac at the default window, the D2 build, Learn closed.
- Record the screen at 60 fps. Record the plugin output separately (24-bit, 48 kHz), and slate each take.
- Save the session, MIDI and automation for every take (playbook §5), and note the build commit.

### F1 Phrase → instrument (30–45 s)

1. **Source (5 s):** the dry monophonic phrase, heard alone. It should have at least one repeated
   note and a gap of a tone somewhere.
2. **Drop (5 s):** drag the WAV onto card A, then answer **Map notes? → Yes**. Hold on the keys
   lighting up.
3. **Atlas (8 s):** open **NOTES**. Play the repeated note three times slowly: a different region lights
   each time (round robin).
4. **Fill (5 s):** play a key that has no note. A neighbour's region lights, and it sounds in tune.
5. **Perform (10–15 s):** a slow phrase with sustained notes (LOOP on). Wide shot, then the
   keyboard.

### F2 Retrospective capture (20–30 s)

1. **Setup:** an audio interface input routed to OSP's sidechain (DAW) or the standalone input.
2. **Play first (8 s):** play the instrument freely with **nothing armed**. The CAPTURE page shows the level picture scrolling.
3. **Grab (3 s):** click **GRAB LAST**. Hold on the card turning into the new instrument.
4. **Play it (10 s):** the just-played phrase is now playable on the keys.
5. **Alternative:** the same with **RECORD → STOP** for a deliberate take.

### F3 One sound → instrument (15–20 s)

1. **The dry note (3 s).**
2. **Drop it (2 s),** then play immediately (no setup cut).
3. **Played across the range (10 s):** low drones to high melody, with macros untouched.

### F4 Three arpeggiators (20–30 s)

1. **Three layers:** a pad source, a percussive source and a bright plucked source.
2. **Card A alone (5 s):** the chord held, ARP off for A.
3. **B joins (5 s):** PLAYS B on, CHORD 1/8. A rhythmic pulse under the pad.
4. **C joins (8 s):** PLAYS C on, UP & DOWN 1/16 × 3. Runs over both.
5. **Close-up (5 s):** the ARP panel's EDIT A | B | C and LINK.

### F5 KEY DNA (20 s)

1. **A granular sound (5 s)** played as one chord, KEY DNA off.
2. **KEY DNA on (5 s):** the editor open with the SIZE / DENS curves drawn.
3. **Low, mid and high keys (10 s),** one at a time. Each register has its own grain character, audible and
   visible in the display.

### F6 SYNTH + IMPRINT (20–25 s, level-matched)

1. **Original (5 s).**
2. **Pure oscillator (5 s):** SYNTHESIZE 100, Analog.
3. **Blend (5 s):** 50, Analog.
4. **Imprint (5–10 s):** mode Imprint, imprint 80. The oscillator takes the source's spectral character.
5. **The same short phrase for all four,** so the comparison is honest.

## 3. Rules

- No synthetic tutorial example as a flagship sound demonstration.
- No custom ARP pattern unless verified in the shipping build.
- No development switches or research tools: the knob-skin toggle, SYNTH's blend-law audition,
  the polyphonic lab and Memo Lab.
- Screenshots and headless sequences are labelled as such. They never stand in for a DAW recording with sound.

## 4. Workflow test run (18 Nov 2026): the synthetic signals, labelled `-workflow-test`

The capture code ran end to end on the trial build (D2 line plus the pilot packs). This proves the workflow. None of these files is flagship material.

| | What the log recorded (`capture-log.txt`) |
|---|---|
| F1 | 8 notes found; keys A3 ×1, C4 ×2, E4 ×1, G4 ×2, A4 ×1, C5 ×1. The repeated C4 lit a different take on each press. The gap key A♯3 played a neighbour's take (FILL) |
| F2 | The CAPTURE page with the input level and the scrolling take. **GRAB LAST 10 s** (the page's own key) kept the take: 7 notes mapped, then played on the keys |
| F3 | DROP A SOUND → one sound → five notes five octaves apart |
| F4 | LINK off. A plays nothing (the pad), B CHORD 1/8, C UP & DOWN 1/16 ×3. ARP panel open with EDIT A / B / C |
| F5 | Granular. KEY DNA: SIZE long → short, DENS sparse → dense, POS variation 0.5, PAN ramp, individuality 0.6 |
| F6 | SYNTHESIZE 0 / 100 Analog / 50 Analog / 100 Imprint (imprint 80) |

**F6 is audible, not visible:** the four full-editor screenshots look almost the same, because the display shows the source waveform in all four. On screen, the difference shows only in the SYNTH chip and its value. **F6 therefore needs the owner's level-matched DAW recording** (shot list above). Screenshots alone cannot carry it.

**The files** (30 PNG at 2×) are in the Package E zip, under `feature-capture-workflow-test/`.
