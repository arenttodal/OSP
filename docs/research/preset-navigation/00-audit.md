# Scalable preset navigation — Gate 0 audit and design

17 November 2026. Queued after the menu/content work; on `claude/osp-menu-content`.

## What exists

| Piece | Where | Today |
|---|---|---|
| Quick preset menu | `PresetBar` (`shared/andor-ui/Source/MainSections.cpp`, `Part::name`) | Open Library…, Back to the previous patch, **Starting states**: INIT, Reset settings, the built-ins, the user's states, then Save starting state…; then **Presets**: one flat list. JUCE splits a long list into several columns. |
| The list behind it | `InstrumentHost::presetList()`, OSP: `OspAudioProcessor::presetList()` | Built-ins, then `Documents/OSP/Starting States/*.ospstate`, then `Documents/OSP/Presets/**/*.osppreset`. **Factory and pack presets are missing:** they live in `<Library>/Factory` and `<Library>/Packs`. |
| ‹ › arrows | `stepPresetList (delta)` | The whole flat list, starting states included, wrapping around. |
| Preset metadata | The catalog (`src/library/Catalog.h`, `Asset`, `PresetInfo`) | Stable `id`, `name`, `category` (one string), tags (separate), `packId`, `origin` (user / factory / pack), `favourite`, `rating`, `type` (preset / templateState), file, layers, history (`recent`). The author is the pack's `creator` for pack and factory content, the user otherwise. |
| Save dialog | Library `SaveSheet` | Name, type, **Category** (`presetCategories`: Keys, Pads, Basses, Plucks, Rhythmic, Textures, Evolving, FX, Experimental), tags, description. |
| Library browsing | Presets view: sidebar (All, Factory, User, Packs, Favorites, Recent, Trash, collections, categories), search, sort, ratings | Its categories come from `categoriesFor (View)`. |

**Reuse:** the category, origin, pack, favourite, rating and type attributes all exist and are
independent of each other. No second preset database is needed, and no file moves.

## Design

1. **One category list.** `library::presetCategory` (src/library) holds:
   - the 14 names: Arpeggios, Bass, Bells & Mallets, Keys, Leads, Pads, Percussion, Plucks,
     Strings, Synths, Textures, Voices, FX, Uncategorized;
   - `canonicalCategory (stored, name)`: the stored category through an alias table (Basses →
     Bass, Textured → Textures, …), else a name prefix ("Arp – " → Arpeggios, "Keys – " → Keys,
     "Lead – " → Leads, "Pad – " → Pads, "Pluck – " → Plucks, "Strings – " → Strings, "Synth – "
     → Synths), else Uncategorized.

   It is derived when read, so the migration is non-destructive and repeatable: no file or
   stored value changes until the user picks a category. The quick menu, the Save sheet and the
   Library's filter all use it.
2. **The quick menu, built from the catalog** (the user's, factory and pack presets) through a
   snapshot the menu reads when it opens. Single column:
   - Open Library…, Back to previous patch, INIT, Reset settings;
   - **COLLECTIONS:**
     - Starting States ▸ (built-ins, the user's, Save Starting State…);
     - Favorites ▸;
     - Recently Used ▸;
     - All Presets ▸;
   - **CATEGORIES:** each category that is not empty ▸;
   - **PACKS:** Packs ▸ (one ▸ per pack).

   Each list is alphabetical, with a tick on the loaded preset. It shows at most 20 rows, then
   **Browse all … in the Library**, which opens the Library on that filter. One column always,
   with no hover loading.
3. **Browsing context:** the collection the loaded preset was chosen from is remembered (All, a
   category, Favorites, Recent, a pack, Starting States, or a Library scope).
   - ‹ › walk that collection, recomputed at each step, so deleted or renamed items simply drop
     out.
   - Starting States are only stepped through when they are the context.
4. **Save:** the Save sheet's CATEGORY uses the 14 names and defaults to the loaded preset's
   category. Tags stay optional.
5. **Library:** the Presets view's category filter uses the same list. Opening "Browse all Keys"
   selects Keys, and search, favourites and packs combine as before.
6. **Scale:** 50, 500 and 5,000 presets.
   - The quick menu's top level stays the same size, and no submenu exceeds 21 rows.
   - Build time is measured.

Gate 1 starts once the full suite of the menu/content work is green.

## Gates 1–5: what was built

| Item | Where |
|---|---|
| One category list (14 names, aliases, name prefixes, Uncategorized) | `src/library/PresetCategory.h` (pure C++, no JUCE); `tests/library/PresetCategoryTests.cpp` |
| Preset metadata for the menu (category, pack, favourite, last used, the context a preset came from) | `PresetEntry` in `shared/andor-ui/Source/InstrumentHost.h`; `InstrumentHost::groupsPresets()` (default false, so sister products keep the flat menu) |
| The grouped quick menu | `PresetBar::presetMenu()` in `shared/andor-ui/Source/MainSections.cpp`: global items, COLLECTIONS, CATEGORIES (non-empty, fixed order), PACKS; single column; each flyout at most 20 rows plus "Browse all N in the Library…" |
| The list behind it | `OspAudioProcessor::presetList()`: built-ins, folder scans and catalog presets (user, factory, pack), the trashed left out; a one-second cache; one catalog read (`runAndWait`) |
| Browsing context and arrows | `presetsIn (context)`, `setBrowseContext`, `browseContext()`, `stepPresetList`. The Library's `loadRow` sets the context too, so ‹ › walk what was chosen from there. Recently Used keeps the order it had when chosen, so stepping does not reshuffle it. |
| Library agreement | The Presets view's `category:X` filter uses `canonicalCategory`; the sidebar lists all 14 with counts as listed. "Browse all" opens that filter. |
| Save | The Save sheet's CATEGORY offers the 14 names and defaults to the loaded preset's category. |
| Packs | No new format: a pack preset keeps the category its manifest carries, and is listed under its pack through `packId`. Factory content is read-only as before. |

**Migration.** Nothing is rewritten. A stored "Basses" reads as Bass; an unset category with a
name like "Keys – Felt Upright" reads as Keys. Picking a category in the Save sheet or the
Library is the only thing that stores one.

**Not in the quick menu, on purpose:** hover never loads anything; tags stay in the Library
only; empty categories are hidden in the menu (the Library still lists them, with 0).

### Results

Tests (`[preset-nav]`, `[preset-categories]`):

- quick menu structure (global items, sections, empty categories hidden, one column, tick);
- arrows walk the chosen collection; Starting States only when chosen from them; a trashed
  preset drops out;
- Browse all → Library on that category, the same presets in both;
- scale: 50 / 500 / 5,000 generated presets in an isolated library.

| Presets | Top-level rows | Widest flyout | Menu built in |
|---|---|---|---|
| 50 | 16 | 21 | 1.2 ms |
| 500 | 16 | 21 | 15 ms |
| 5,000 | 16 | 21 | ≈ 210 ms |

At 5,000 the catalog read dominates; the menu is only built on click, and the one-second cache
means the arrows and a reopened menu do not read it again.

Screenshots: `before-01-quick-menu.png` (flat list), `after-01-quick-menu.png` (grouped),
`after-02-keys-flyout.png` (the Keys flyout, loaded preset ticked). Regenerate with
`OSP_PRESET_NAV_SHOTS=docs/research/preset-navigation osp_plugin_tests "[preset-nav-shots]"`
under a display (Xvfb).
