# The state the plugin saves today

Everything here must keep loading (spec 2.1, 2.5). The Library adds to it; it never changes
what an existing field means.

## Host state (DAW project) = presets = starting states

One XML document, the APVTS tree `OSP`:

| Part | Content |
|---|---|
| Root attributes | `stateVersion` (10), `uiScale`, `advancedOpen`, `arpExpanded`, `modTab`, `program`, `editLayer`, `keptSlots`, `reimaginedRouting`, `shaperCustom`, `modCurves` |
| `PARAM {id, value}` x ~1,000 | every parameter, by stable ID (IDs carry JUCE version hints) |
| `ModLinks` | meta-modulation route IDs (optional, schemaVersion 1) |
| `Instrument` / `InstrumentB` / `InstrumentC` | per layer: `contentHash` ("sha256:<hex>"), `filename`, `originalPath`, `playbackRootMidi`, `rootOrigin`, `startSeconds`, `playbackGainDb`, optional `rootOverride`, optional `Set {Member*, Assignment*}` |

- **Audio is not embedded** in host state. A layer refers to its sound by content hash.
- **Recall order**: the sample store by hash, then the original path. A file found elsewhere
  is never substituted by name.
- The saved playback model (root, start, gain) is restored exactly, so a project sounds as
  saved even if the analyser has changed since.
- Older states migrate in `applyStateXml`: v1 neutral engine, legacy shared REIMAGINED (< 8),
  REIMAGINED modes (< 9), MOVEMENT v2, and parameters missing from the state reset to their
  defaults.

## Files on disk

| File | Where | What |
|---|---|---|
| `<sha256>.<ext>` | `~/Library/Application Support/OSP/Samples/` (`OSP_SAMPLE_STORE` overrides) | managed copy of every sound ever loaded |
| `<sha256>.analysis.json` | same | versioned analysis cache |
| `*.osppreset` | `~/Documents/OSP/Presets/` | the state XML (sounds by hash) |
| `*.ospstate` | `~/Documents/OSP/Starting States/` | the state XML without layer trees, window state or program (`startingState="1"`) |
| `*.ospinstrument` | anywhere | zip: `manifest.json` {schemaVersion 1, format, engineVersion, stateVersion, sources[] {contentHash, filename, stored}}, `source/`, `analysis/`, `preset.xml` |
| `*.ospshaper` | `~/Documents/OSP/Shaper Patterns/` | SHAPER CUSTOM patterns (JSON, schemaVersion 1) |

## Tradeoff kept

Self-contained project state (audio inside the DAW project) would make projects large; OSP
chose content-hash references plus a managed store, with `.ospinstrument` for moving an
instrument with its sounds. The Library keeps this (decision D-02): the catalog never owns
the audio a project needs. Deleting the Library's database must leave every project loadable
from the store.

## Undo

`juce::UndoManager` on the APVTS: parameter changes, sample loads (`InstrumentChangeAction`),
root changes, curve and pattern edits, route changes (one step each). Loading a preset or a
starting state clears the history today (`replaceState` does). The Library adds a
recoverable "previous state" snapshot for those (Stage 3).
