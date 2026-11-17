# Menu architecture and content authoring — Gate 0 audit

17 November 2026. Branch `claude/osp-menu-content`, on top of `claude/osp-final-polish`.

## 1. What exists today

### 1.1 The main menu (header ⋮, `OspAudioProcessorEditor::showMenu`)

| # | Label | Action | Scope | Elsewhere? | Enabled when |
|---|---|---|---|---|---|
| 1 | Load sound… / Add layer… | `chooseFile (-1, true)`: the first free slot | global (adds a source) | ADD drop target, the drop zone | a slot is free |
| 2 | Load example | `resetLayerControls (0)` + `loadExample (0)` | **Layer A only** | — | no sound loaded |
| 3 | Record… / Record into a new layer… | `openNewLayerCapture()`: the first free slot | global | drop zone REC, each card's REC key | a slot is free and live capture exists |
| 4 | Restore removed layer | `restoreRemovedLayer()` | global (undo of a removal) | — | a removed layer can be restored |
| 5 | **Layer X section** (header "Layer A · file") | `addLayerSection (editLayer)`: Replace sound…, Load example, Root ▸, Map notes…, Notes, Detect notes again ▸, Play as one sample, Samples, Remove layer ▸ | **source-specific** (the edited layer, A by default) | **the card's ⋯ menu has the same items** | a slot exists |
| 6 | Clear all samples ▸ (confirm) | `clearAllSamples()` | global | — | a sound is loaded |
| 7 | Library… | `openLibrary()` | global | the preset name's menu, LIBRARY key, Cmd/Ctrl+L | always |
| 8 | Presets ▸ (folder tree, Show folder) | load a `.osppreset` | global | the preset name's list, the Library | always |
| 9 | Open preset file... | file chooser → `loadPreset` | global | — | always |
| 10 | Instruments ▸ (folder tree) | `importInstrument` | global | the Library (portable rows) | always |
| 11 | Export instrument... | `exportInstrument` (`.ospinstrument`: settings + copies of the recordings) | global | Library row menu "Export as portable instrument…" | a sound is loaded |
| 12 | Open instrument file... | file chooser → `importInstrument` | global | — | always |
| 13 | Undo / Redo <what> | the processor's `UndoManager` | global | Cmd/Ctrl+Z, Shift+Cmd/Ctrl+Z | can undo / redo |
| 14 | Interface size ▸ 80–200 % | `setUiScale` | preference | — | always |
| 15 | Learn ▸ Tutorials…, Restore Previous Sound, Reset Tutorials… | `LearnController::addMenuItems` | support | INFO note's See tutorials | Learn usable |

The tooltip still reads "Sounds, presets, instruments, undo, size".

### 1.2 The source card's ⋯ menu (`showLayerMenu` → `addLayerSection (layer, false)`)

Every card uses the same function, addressed by its own index: the card's menu already acts on
the right source.

| Label | Action | Shown when |
|---|---|---|
| Replace sound… | `chooseFile (layer, false)` | always |
| Load example | `loadExample (layer)` | always |
| Root ▸ Auto, Octave 0–8 ▸ notes | `changeRootOverride` (undoable) | a sound is loaded |
| Map notes… | `mapNotes (layer)` | a single recording (no atlas, no set) |
| Notes | toggles the card's NOTES page | an atlas |
| Detect notes again ▸ fewer / normal / more | `mapNotes (layer, d)` | an atlas with analysis |
| Play as one sample | `playAsOneSample` | an atlas |
| Samples | the multi-sample panel | a set (else disabled) |
| Remove layer X ▸ Remove <file> | `removeLayer` (restorable) | occupied or kept-empty, not loading |

**There is no header on the card's menu.** The source's identity is only implicit. The card
itself carries MODE, EQ, REC, NOTES and MAP NOTES.

### 1.3 SAVE (header key, `savePresetButton`)

It always opens a file chooser ("Save preset") into `Documents/OSP/Presets`. It never updates
the preset that is open (`currentPresetFile()`), and never guards factory presets: a factory
preset's folder is outside the chooser's default, but nothing prevents choosing it.

### 1.4 The preset name's menu (`PresetBar`, `MainSections.cpp`)

- Open Library…;
- Back to the patch before …;
- **Starting states:** INIT, Reset settings, the built-in states and the user's own;
- **Save starting state…:** a file chooser into `Documents/OSP/Starting States`;
- **Presets:** the user's presets.

### 1.5 The Library

- **Tabs:** Sounds, Presets and Templates. The Templates tab lists the user's starting states
  (`AssetType::templateState`, `.ospstate`). Since the polish, the built-in states are tiles
  above Presets and Templates.
- **Save to Library sheet** (`SaveSheet`): Preset or **Template (no sounds)**, name, category,
  tags, notes (the description) and Portable. This is the existing starting-state authoring
  dialog, with a description.
- **Row menus:**
  - load, duplicate, Export file…, Export as portable instrument…;
  - Category ▸, Describe…, Show in Finder, favourite, Rating ▸, Rename…, Move to Trash;
  - for sounds: Play/Pause, Load into A/B/C, Trim/Slice….
- **Library ⋯ menu:**
  - Library settings…, Missing sounds…;
  - Scan folder…, Inbox folders…;
  - Install pack…, Packs…;
  - Back up the Library…, Restore a backup…;
  - Show presets folder.
- **Collections:** "Publish as pack (id, version, licence)…" opens `PublishSheet`, which has
  the **Factory pack** checkbox.

### 1.6 Content infrastructure that already exists

| Piece | Where | State |
|---|---|---|
| Preset `.osppreset` | `savePreset` / `loadPreset` | whole state; sounds by hash, sample store |
| Portable instrument `.ospinstrument` | `exportInstrument` / `importInstrument` | settings plus copies of every recording (sets, atlases) |
| Starting state `.ospstate` | `saveStartingState` / `loadStartingState` | `startingState="1"` attribute, explicit identity; no instrument trees (no audio, root overrides, atlas edits); `keptSlots` |
| Built-in starting states | `apps/plugin/Resources/StartingStates/*.ospstate` (embedded, this branch's base) | whole states |
| Packs `.osppack` | `src/library/Packs.*`, `LibraryPacks.cpp` | manifest v1: sounds, presets, templates; checksums, safety, dependencies, update by stable uid, factory flag, install, remove |
| Factory install | `factoryPackSource()` at startup → `installFactoryPacks` | bundle `Contents/Resources/Factory`; CI fetch (`packs.lock`) |
| Pack export | `exportCollection` (a collection → `.osppack`) | from a Library collection; resolves the sounds a preset needs |

## 2. Findings

1. **The main menu carries a whole source section** (5), defaulting to the edited layer:
   historically A. Each card's menu already has every one of those items. This duplicates them,
   and gives one source special treatment.
2. **Load example** (2) always loads into A.
3. **Browsing is triplicated:**
   - presets: main menu ▸ Presets, the preset name's list, and the Library;
   - instruments: main menu ▸ Instruments, and the Library.
4. **Opening files is split:** Open preset file… and Open instrument file… are two items for
   one gesture.
5. **SAVE is always Save As:** it never updates the open user preset, and nothing protects
   factory presets.
6. **Starting-state authoring exists twice:**
   - the preset name's "Save starting state…" is a bare file chooser: no description;
   - the Library's Save sheet has Template, a description and a category.

   It is also named three ways: "starting state" in the header, "Template" in the Library, and
   "STARTING STATES" on the tiles.
7. **Pack authoring exists, but is hidden:** a collection's context menu → Publish as pack.
   - Anyone can tick **Factory pack** (spec §9.7).
   - There are no drafts: the publish fields are remembered per collection, which is close to
     one.
   - There is no validation report before export.
8. **No About, and no in-app user guide link.**
9. **Shortcuts:**
   - Cmd/Ctrl+L: Library;
   - Cmd/Ctrl+Z and Shift+Cmd/Ctrl+Z: undo / redo;
   - Cmd/Ctrl+Alt+G and Cmd/Ctrl+Alt+K: development looks (never in a customer menu).
10. **Learn refers to the main menu in 4 steps:**
    - Add layer… (`osp.layers` lessons);
    - a third layer;
    - Export instrument…;
    - Open instrument file…;
    - the target is `osp.header.menu`.

    The user guide refers to it in about 6 places.

## 3. Migration table

| Today | Proposed place | Note |
|---|---|---|
| Load sound… / Add layer… | Main ▸ SOURCES · **Add source…** | global: a new source |
| Record… / Record into a new layer… | Main ▸ SOURCES · **Record new source…** | |
| Restore removed layer | Main ▸ SOURCES · **Restore removed source** | |
| Clear all samples ▸ | Main ▸ SOURCES · **Clear all sources ▸** (confirm) | settings kept |
| Load example (A) | **Source menu ▸ Load example** (each source); main menu: gone | was A-only; already on every card |
| Layer X section (Replace, Root, Map notes, Notes, Detect again, Play as one sample, Samples, Remove) | **Source menus only** | duplicates removed from the main menu |
| — | Main ▸ INSTRUMENT · **New instrument** | `initPatch` (INIT): already in the preset list |
| SAVE key | Main ▸ INSTRUMENT · **Save instrument** (Cmd/Ctrl+S) + the SAVE key | updates a writable user preset; otherwise Save As |
| — | Main ▸ INSTRUMENT · **Save as…** (Shift+Cmd/Ctrl+S) | always a new user preset |
| Open preset file… + Open instrument file… | Main ▸ INSTRUMENT · **Open instrument…** (Cmd/Ctrl+O) | one chooser, both formats, by extension |
| Library… | Main ▸ INSTRUMENT · **Open Library** (Cmd/Ctrl+L) | |
| Presets ▸ / Instruments ▸ | **removed** (duplicates) | the preset name's list and the Library browse them |
| Undo / Redo | Main ▸ INSTRUMENT (bottom) | kept: global, with their shortcuts shown |
| Preset name ▸ Save starting state… | Main ▸ CONTENT · **Save as Starting State…** | opens the existing Save sheet as a Starting State (name, description); the preset list keeps its entry |
| Export instrument... | Main ▸ CONTENT · **Export portable instrument…** | |
| Collection ▸ Publish as pack… | Main ▸ CONTENT · **Create content pack…** (Gate 5 builder); the collection item stays | |
| Library ⋯ Install pack… | Main ▸ CONTENT · **Import pack…** (and stays in the Library ⋯) | |
| Library ⋯ Packs… | Main ▸ CONTENT · **Manage packs…** (and stays) | |
| Interface size ▸ | Main ▸ PREFERENCES · Interface size ▸ | |
| Library settings… | Main ▸ PREFERENCES · **Library settings…** | the preferences OSP has (preview level, caches) |
| Learn ▸ | Main ▸ SUPPORT · **Tutorials…**, Reset tutorials…, Restore Previous Sound | one level fewer |
| — | Main ▸ SUPPORT · **About ANDOR OSP** | version and build |

**Source menu (per card, with a "SOURCE A · file" header):**
- **SOURCE:** Load audio… (Replace sound… when loaded), Load example, Record audio… (the card's
  capture), Save source to Library… (the recording into the Library);
- **AUDIO:** Root ▸, Map notes… / Notes / Detect notes again ▸ / Play as one sample, Samples,
  Reveal source file;
- **MANAGEMENT:** Reset source settings (`resetLayerControls`), Remove source ▸.

Actions with no source, or no matching state, are not shown.

**Not built, because nothing exists to reuse and the spec asks not to invent:**
- Duplicate to…: a source copied into another slot;
- Reanalyse for a plain recording: Detect notes again covers atlases.

These are listed as limitations.

## Gate 1: done (`claude/osp-menu-content`)

**Main menu (⋮), global actions only, five groups with quiet labels and hairlines:**
- **INSTRUMENT:** New instrument, Save instrument, Save as…, Open instrument… (presets and
  portable instruments), Open Library ⌘L, Undo ⌘Z, Redo ⇧⌘Z;
- **SOURCES:** Add source…, Record new source…, Restore removed source, Clear all sources ▸;
- **CONTENT:** Save as Starting State… (the Library's Save sheet, as a Starting State), Export
  portable instrument…, Import pack…, Manage packs…;
- **PREFERENCES:** Interface size ▸, Library settings…;
- **SUPPORT:** Tutorials…, Restore Previous Sound (while possible), Reset Tutorials…, About
  ANDOR OSP.

The menu opens right-aligned under its key, so it stays inside the plugin.

**Removed duplicates:**
- the whole Layer section (now only on each card);
- Load example aimed at A;
- Presets ▸ and Instruments ▸ (the preset name's list and the Library);
- the separate Open preset file… / Open instrument file… (now one Open instrument…);
- the Learn ▸ level.

**Source menus (one function, `sourceMenu (layer)`):**
- a header names the source ("SOURCE B · Glass Pluck E4.wav");
- **SOURCE:** Load/Replace audio…, Load example, Record audio…;
- **AUDIO:** Root ▸, Map notes… / Notes / Detect notes again ▸ / Play as one sample, Samples,
  Reveal source file;
- **MANAGEMENT:** Reset source settings, Remove source ▸.

Every action takes that source's index, never the edited layer's.

**SAVE:**
- `canSaveInPlace()` is true only for a user `.osppreset` this computer can write. It is false
  for a starting state, and for anything inside the Library's Factory or Packs folders.
- In place, the catalog record (tags, favourite, collections) is kept, through
  `Catalog::savePreset`'s upsert by file.
- Otherwise SAVE opens the existing Save As chooser.
- No new keyboard shortcuts: a plugin that took Cmd/Ctrl+S or Cmd/Ctrl+O would take them from
  the host.

**Popup look (`OspLookAndFeel`, every menu):**
- 24 px rows, a 24 px gutter for ticks, the text aligned under the section labels;
- section labels in 10.5 px tracked small capitals, in the dim ink;
- a rounded recessed highlight, a drawn chevron, shortcuts right-aligned in the dim ink.

**Learn and the user guide:** the moved items are renamed: Add source…, Export portable
instrument…, Open instrument…, and the menu's help text.

**Tests:** `osp_plugin_tests "[menus]"`:
- the main menu's groups and items, nothing of a source's, no development commands, shortcuts
  shown;
- each source's menu names its source and acts on it (B's Reset leaves A), and the card's key
  opens it; an empty source offers only what fills it;
- SAVE in place, through the real key, and never for a starting state, a factory preset or a
  pack preset.

The utility-keys test now expects SUPPORT in place of Learn ▸.

**Screenshots (this folder):** `before-01-main-menu.png`, `after-01-main-menu.png`,
`after-02/03/04-source-A/B/C-menu.png`. Made under Xvfb: JUCE needs a display to place a menu.

**Limitations:**
- Duplicate to… and Reanalyse (for a plain recording) are not built: there is nothing to reuse,
  and the spec asks not to invent.
- There is no in-app user guide link: OSP has no published guide URL. About shows the version
  and the build date.
- Three hidden UI tests (`[.][ui]`: mod-ui, andor-ui, meta-ui) fail when selected by hand. They
  check the modulation panel, which this change does not touch, and ctest does not run them.
  Whether they already failed before this change was not checked.

## Gate 3: done, Starting State authoring

**Behaviour that already existed, kept (§8.4):**
- A Starting State applies onto the sounds already loaded. It keeps them, and keeps an empty
  slot for each one it was saved with (`keptSlots`).
- The built-in states and the user's own load the same way.

**One dialog for every way in:**
- **⋮ → Save as Starting State…** and the preset name's **Save starting state…** both open the
  Library's Save sheet in Starting State mode.
- Its fields are a name, the type ("Starting State (no audio): every setting, macros, effects
  and modulation; no recordings"), a category, tags and a description.
- The bare file chooser is gone.

**Selective inclusion (§8.2) is not offered.** A Starting State is always the complete musical
configuration without the source audio:
- every parameter;
- the USER macros (names and assignments);
- the FX route and its effects;
- modulation and performance;
- the slots.

Partial recall (for example, effects only) would leave unaddressed parameters at whatever the
instrument had: contradictory, and outside the existing contract. Audio is never included. To
keep recordings, save an instrument (a preset, or a portable instrument).

**Audio independence (§8.3):**
- The source trees are not written, so nothing tied to a recording is: file names, hashes,
  stored or original paths, analysis, root overrides, Note Atlas regions and edits, multi-sample
  maps.
- A test checks the saved file for each of these.
- Applied onto a new recording, that recording's own analysis and root stand.

**Explicit identity:**
- `startingState="1"` in the file, and `AssetType::templateState` in the catalog. Never the name
  or the folder.
- A preset and a Starting State with the same name are two different items.

**The description travels in the file** (`description` attribute) as well as in the catalog. It
survives a rename.

**The instrument is unaffected:** saving a Starting State no longer renames the open instrument
or drops its preset identity. Before, the header took the state's name and SAVE lost its target.

**Library:**
- the tab reads **Starting States** (it read Templates);
- the list's heading reads YOUR STARTING STATES, "All Starting States", and **Load State**;
- the built-in tiles above the list are unchanged;
- rename, duplicate and trash work through the existing row menu.

**Tests:** `osp_plugin_tests "[starting-state-authoring]"`:
- saved without audio or anything tied to it;
- the description is in the file;
- the instrument's name, preset and SAVE target are unchanged;
- applied onto another recording, it keeps that recording, its own root, the macros, the USER
  macros, the FX route and the kept empty slot;
- a rename keeps the identity and the description;
- a preset and a state of the same name stay apart in the Library.
