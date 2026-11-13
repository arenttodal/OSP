# Automatic multi-sample mapping — MS0 audit and MS1 reproduction

_13 Nov 2026. Branch `claude/multisample-mapping`, from `0a418b9` (Pedal Loop 1.0)._

The goal: several one-shot recordings dropped together onto a source (A, B or C) become **one**
instrument in that source. Every MIDI key 0–127 plays the nearest recorded root (a tie goes to
the lower root). Takes of one pitch are real round robins that cycle in a fixed order. Octaves
stay apart. Explicit dynamics layers (pp/mf/ff) are kept. The chooser and the drop behave the
same. Old sessions keep their sound.

This document records what the code did **before** any change, measured by reproduction
tests. Those tests now run as the regression tests:

- `tests/unit/MultiSampleMappingTests.cpp`, tag `[multisample]`;
- `tests/plugin/MultiSampleDropTests.inc`, tag `[plugin][multisample]`.

## 1. Code map (where each decision is made)

| Step | Where | What it did at `0a418b9` |
| --- | --- | --- |
| Drop | `apps/plugin/Source/PluginEditor.cpp` `filesDropped` | **A folder** was one sound (`addLayerSet` / `replaceLayer`). **Loose files** were one sound each: on an empty or ADD target `addLayers` (one layer per file, up to three); on a card (REPLACE) the *first* file replaced the card and the rest became new layers while there was room. |
| Chooser | `PluginEditor.cpp` `chooseFile` | "Replace sound…" with several files: one set (`replaceLayer (files)`). "Add layer…" ("each becomes a layer"): `addLayers`, one layer per file. Already different from the drop. |
| Load | `PluginProcessor.cpp` `loadFiles` → `enqueueSetLoad` | One file: `loadFile`. Several: a `SetLoadRequest` in the order given (drop or file-system order, not sorted). |
| Build | `shared/andor-ui/Source/InstrumentLoader.cpp` `loadInstrumentSet` → `instrument::buildSet` → `inferSampleSet` | The analysis per file, then the set inference. |
| Pitch groups | `src/engine/SampleSetInference.cpp` | Roots within 0.5 semitone form one group, greedy over the sorted detected roots. **Octaves stay apart** (C3, C4 and C5 are three groups). |
| Layers | same | Dynamics words in the names (pp … ff, soft, hard, …) make velocity layers when every playable take of the group has one. **Otherwise a loudness gap of ≥ 4.5 dB split layers.** |
| Takes | same | Numbered within each (group, layer), in the group's member order (the detected roots' sort order, so effectively arbitrary). |
| Articulations | same | With ≥ 3 takes, a take more than 3× longer or shorter than the median is an articulation: kept, but not played by ordinary notes. |
| Key → group | `src/model/InstrumentSet.h` `groupFor` | Nearest root, ties to the lower one, no limit for file sets (`fillLimitSemitones` = ∞; the Note Atlas keeps its own FILL limits). |
| Take pick | `src/engine/InstrumentEngine.cpp` `memberFor` | A seeded random take (`Prng::deriveSeed (seed, eventIndex, note + 0x7272)`) that is not the previous take of that (source, group, layer). |
| Save / recall | `PluginProcessor.cpp` (`Set` tree) | Member files in member order and the user's corrections; recall re-infers from the cached analyses. |
| Filename pitch hints | — | **None.** The root always comes from the acoustic analysis (no "C4" parsing). |

## 2. Reproduction (MS1): measured on `0a418b9`

The fixtures are synthetic (`testsignals::vowel` / `testsignals::pluck` at known pitches, one
seed per take). The engine's own choice is read through `InstrumentEngine::memberFor`. No
recorded audio is used.

### Core (`osp_tests "[multisample]"`): 8 cases, 4 passed, 4 failed

| Case | Result | Classification |
| --- | --- | --- |
| One recording plays on every key 0–127 | pass | already works |
| Several pitches (48/55/62/69): nearest root, ties to the lower, edges reach 0 and 127; C#4 between C4 and D4 → C4 | pass | already works |
| C3, C4, C5 are three roots (and 0 → C3, 127 → C5, 53 → C3, 55 → C4) | pass | already works |
| Plucked recordings (G2 … E5) are rooted at their own pitch (within 0.5 semitone) | pass | already works |
| Three C4 takes: all round robins **and a strict cycle** (`names[i] == names[i-3]`) | **fail**: takes alternate randomly (never-repeat), not in a cycle | limitation |
| Each root keeps its own cycle: the C4 sequence alone equals the C4 sequence with G4 notes between them | **fail**: the random pick depends on the global note count, so G4 notes change which C4 take plays | fails |
| Eight takes of one pitch: all eight used in 16 notes | **fail**: a random never-repeat pick leaves some takes unused | limitation |
| Unlabeled performances about 10 dB apart (0.6 / 0.2 / 0.07): one layer, all round robins | **fail**: the 4.5 dB rule makes three velocity layers, so at one velocity only one take plays (the "machine gun") | fails |

The filled-key parts already passed (D4 plays all three C4 takes, E4 plays G4's, below and above
the roots extend). So did explicit pp/mf/ff names: three layers.

### Plugin (`osp_plugin_tests "[plugin][multisample]"`): 3 cases, 1 passed, 2 failed

| Case | Result | Classification |
| --- | --- | --- |
| Three loose files dropped on an empty instrument → one source with one 3-member, 2-root set | **fail**: `slotCount() == 3`, no set (three single-file layers) | fails |
| With A, B and C loaded, three loose files dropped on card A, B or C → only that card becomes the set; the others are untouched | **fail**: the card got only the first file (no set); the rest could not be added (three layers already) | fails |
| One file → one sound (no set), as before | pass | already works |

Also observed:

- **Order.** Member order was the drop order. Take numbers were the detected roots' sort order. The cycle could not be "the file order" because the code had no fixed order. Two drops of the same files in different orders gave different take numbers.
- **The chooser and the drop disagreed.** "Add layer…" with several files made separate layers. "Replace sound…" made one set.
- **Not implemented:** filename pitch hints. They were not needed: on every fixture, including plucks with a sharp attack, the acoustic root was within 0.5 semitone. Left out (spec: only what is proven absent or incorrect).
- A lone "F" between separators in a name reads as *forte* (`dynamicsRankFromName`). For a group whose takes all carry it, every take gets the same rank, so they stay one layer of round robins. Harmless; left as is.

## 3. What MS2–MS3 change (minimal; details in `01-report.md`)

1. **Drop and chooser**: everything dropped (or chosen) together is one collection. One file is one sound, as before. Several files make one set, in a new source (empty or ADD) or in the card dropped on (REPLACE, only that card).
2. **Order**: files imported together are sorted by natural file name (`C4_2` before `C4_10`) before loading, so the take order is the file order.
3. **Policy, stored on the set** (`InstrumentSet::cycleTakes`, `loudnessLayers`). Both default to the original rules. Files imported together get `cycleTakes = true` and `loudnessLayers = false`, saved on the session's `Set` tree. A `Set` without them (every session saved before) recalls with the original rules and sounds as it did.
4. **Engine**: with `cycleTakes`, the take after the previous one in take order, then the first again. Per (source, group, velocity layer), so no other note moves it, and filled keys share their root's cycle. The legacy random pick is unchanged (the bit-exact renders confirm it).
