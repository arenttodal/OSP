# Custom ARP Patterns — CP0 audit and baseline

18 November 2026. Research only: no production code changed in CP0 or CP1. The spec is
"ANDOR — CUSTOM ARP PATTERNS V1"; it needs explicit approval before CP2 (integration).

## 1. What exists

| Piece | Where | Relevant facts |
|---|---|---|
| The arpeggiator | `src/engine/Arpeggiator.{h,cpp}` | One class, pure C++, fixed capacity: 64 held notes, `maxSequence` 1024, 2048 events per block. The 18 styles are walks over a **pool** (held notes ascending, or as played, × OCTAVES, nothing above 127), built in `ensureSequence`. `stepNotes (patternStep, …)` picks a step's notes; `atCycleStart` uses the walk's length; `fireStep` schedules a note-off for every note-on, ends a still-sounding pitch before retriggering it, and accepts a step with **no notes** (`count == 0`: nothing plays, the display shows a gap). |
| Time | `HostTiming.h`, `Arpeggiator.cpp` | Host grid (PPQ multiples of the step), sample-placed; free running at the last tempo; loops and jumps resync without catch-up; ARP RATE modulation through a phase accumulator; SWING delays odd steps. |
| Lanes A/B/C | `shared/andor-ui/Source/CardArpLanes.*` (OSP); **a separate copy in the shell** (`ShellProcessor.h:524-546`, `ShellProcessor.cpp:720`) | Lane A is the main arpeggiator, B and C own their own `Arpeggiator`. LINK, PLAYS, EDIT. Output channel = 1 + lane mask. MPE and MONO fall back to the single arpeggiator. The shell's AUX follower copies a lane's `Arpeggiator` object. |
| Parameters | `shared/andor-ui/Source/AndorParameters.{h,cpp}` | `arp.pattern` (Choice, 18, **append only**), `arp.rate`, `arp.gate`, `arp.octaves` (hint 15), `arp.swing` (16), `arp.velocity` (19); per lane `arp.cardB|C.pattern/rate/octaves`, `arp.link`, `arp.cardA/B/C` (27). Pattern names are listed in four places: `Arpeggiator.cpp`, `InstrumentHost::arpPatternNames`, `ArpInlinePanel::patternLabel` / `listOrder` (with a `static_assert`) and `research/RenderConfig.cpp` (`static_assert`). |
| Telemetry | `PluginProcessor.cpp:1909-1923`, `InstrumentHost::ArpView` | The engine's `Display` (16 steps low/high, current, active) as relaxed atomics, read at 30 Hz. It already pages: it shows the 16-step page around the sounding step. The panel's picture is drawn by a **private `Arpeggiator`** on the message thread over C E G B (`ArpSection.cpp:585-622`). |
| MIDI capture | — | **None.** Live capture records audio only. There is no note recorder. |
| SMF | `src/midi/MidiFileIO` | `readMidiFile` merges all tracks into seconds (tempo map applied) and keeps note on/off, CC64 and CC123 only. There are no tracks, channels, PPQ or bars. |
| Exchange | `src/model/ModelExchange.h`; SHAPER CUSTOM (`PluginProcessor.cpp:1314-1352`) | Immutable models are published to the audio thread without locks. SHAPER CUSTOM is the closest precedent: a user-drawn pattern published with a generation counter and undoable. |
| State | `createStateXml` / `applyStateXml` (`PluginProcessor.cpp:3580-3779`), `.osppreset`, `.ospinstrument` (preset.xml inside), shell `stateXml` | Optional children (`UserMacros`, `FxRouting`) and encoded properties (`shaperCustom`) beside the parameters. A pattern embedded there travels in presets, songs and portable instruments. |
| SATELLITE | `src/engine/SatelliteMotion.*` | Stereo motion only. It shares the ARP panel's title row and is independent of pattern selection. Nothing to change. |
| Tests | `tests/unit/ArpeggiatorTests.cpp` (26), `tests/plugin` `[arp]` (7), `CardArpTests.inc` (5, incl. no-allocation), shell `[arp]` | There was **no golden of the styles' output**. The hidden `[arp-baseline]` is the off-is-untouched null test. |

## 2. Baseline frozen (CP0)

`tests/regression/ArpStyleBaselineTests.cpp` (`[regression][arp-style-baseline]`, run by ctest)
renders every one of the 18 styles through four scenes and compares every generated event with
`tests/regression/golden/arp/arp-styles.txt` (schemaVersion 1):

1. free running, a triad, 1/16, 2 octaves;
2. a looping host at 97 BPM, a seventh, 1/8T, 3 octaves, swing 40 %, gate 120 %, a chord change
   and a jump back two beats;
3. the modulated clock (ARP RATE 1.5×, then 0.75×), one note over 4 octaves, velocity 80 %;
4. the sustain pedal keeping two released keys of a five-note chord.

Kinds, notes, velocities, channels and order must match exactly; times may differ by one sample
(floating-point contraction differs between compilers). It must stay green through every later
gate: the acceptance criterion "existing ARP styles remain unchanged".

## 3. Smallest extension (proposal for CP2, not built)

1. **Engine.** Add one walk to the pool rather than a second arpeggiator.
   - `ArpPattern::custom = 18` is appended to the engine's enum.
   - A `CompiledArpPattern` is a value type held inside `Arpeggiator`, of fixed size:
     - up to 128 steps;
     - each step a rank, or a rest for Stage B;
     - R, length, mapping, and a generation.
   - `ensureSequence` keeps building the pool. `stepNotes` maps `ranks[step % length]` onto it,
     `atCycleStart` uses the pattern's length, and a rest returns 0 notes (already handled).
   - The gate, the retrigger safeguard, swing, rate modulation, octave changes at cycle start,
     sync, loop and seek are all unchanged.
   - A value (not a pointer) means copying an `Arpeggiator`, as the AUX follower does, copies the
     pattern too.
2. **Publishing.**
   - The message thread compiles the pattern and publishes it per lane through `ModelExchange`.
   - The audio thread takes the pending one at the block start and copies it in (about 260
     bytes), so there is no allocation, lock or string lookup.
   - It is replaced at the next step. The phase (step count) is kept, and a note already
     sounding ends at its own gate.
3. **Parameter: decision needed.**
   - Appending a 19th choice to `arp.pattern` keeps every stored index, but it moves the
     normalised value of every choice. The last time choices were appended (6 → 18), host
     automation recorded on `arp.pattern` changed meaning; here an automated RANDOM ONCE (1.0)
     would become CUSTOM.
   - **Recommendation:**
     - keep `arp.pattern` as it is;
     - add one bool per lane, `arp.custom` / `arp.cardB.custom` / `arp.cardC.custom` (next
       version hint). When it is on, the engine uses `ArpPattern::custom`.
   - The panel's list shows CUSTOM as one more row, so the musician sees one selector.
   - Which pattern each lane plays (its id and its full data) lives in the state XML, not in a
     parameter.
4. **Both instruments.**
   - CLAUDE.md requires ARP features in OSP and ANALOG.
   - The engine change and the panel are shared, but the shell's lanes are a separate copy, so
     the per-lane assignment must be added there too (or the shell moved onto `CardArpLanes`
     first: a refactor to decide at CP4).
5. **Capture.**
   - A fixed-capacity FIFO of the **pre-arp** input (the `midi` buffer before
     `runCardArpeggiators`, screen keyboard already merged) feeds the editor while armed. The
     arpeggiator's output can never be captured, so there is no feedback.
   - Monitoring while recording: the recorded lane hears the keys directly (the existing
     on/off hand-over). The arp state is restored on Stop and on Cancel.
6. **Import.**
   - A new JUCE-side reader beside `readMidiFile` keeps tracks, channels, PPQ and tempo.
   - It runs on a background thread, and the musician picks a track when there are several.
7. **Display.**
   - The engine's 16-step page already pages long patterns (steps 17–32 are page 2), and it
     shows rests as gaps.
   - The panel's picture comes from its own `Arpeggiator` given the same compiled pattern, so the
     picture is the engine's real output, never a separate simulation.

## 4. Capacity

| Item | Limit | Why |
|---|---|---|
| Steps | 1–128 | The spec. `maxSequence` (1024) is untouched; the pattern is its own array. |
| Ranks | ≤ 128 | A rank per distinct captured pitch; a phrase has at most 128 pitches. |
| Captured events | 1024 note-ons per take | The FIFO size. Beyond it the take stops and says so; **nothing is silently truncated**. A phrase with more than 128 steps asks for a region instead of cutting. |
| Pool | ≤ 256 notes | 64 held × 4 octaves, as today. |
| Events per block | 2048 | Unchanged. A custom pattern emits at most one note-on and one note-off per step, as UP does. |

## 5. Reference gestures (fixtures)

The lab writes these original phrases as `fixtures/*.mid`
(`src/research/ArpPatternLab.cpp`, `referenceGestures()`):

| Gesture | What it tests |
|---|---|
| five-up | a five-note ascending motif (odd length) |
| bass-pedal | a recurring anchor (pedal note) |
| thumb-leaps | an anchor under irregular leaps |
| two-pitch | a repeated two-pitch rhythm |
| arch | ascending then descending |
| wide-jumps | two-octave register jumps |
| rests | a rhythm with rests (for Stage B) |
| short-three | a three-note cell |
| long-32 | a 32-step line |
| single | one pitch |
| legato | overlapping notes (still one step each) |
| chord-attack | a chord inside the phrase (the policy picks one note) |

CP1's results are in `01-lab.md`.
