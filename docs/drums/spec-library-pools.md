# Spec: a drum library, saved pools, and pads that draw from pools

Status: **proposal**, nothing built yet. The user asked for this on 2026-10-12.

**Goal.** Every drum sound and break a musician imports is kept in a library. Favourite sets
can be saved as **pools**: "my kicks", "dusty snares", "perc for house". A pad can play
from a pool, **cycling** through it or **randomising**, so a groove never repeats the same
kick unless asked to.

## Starting point: what exists already

- **Store.** OSP's Library (`src/library`, `docs/library/*.md`) is a SQLite catalog shared by
  every OSP instance (`<app data>/OSP/Library/catalog.db`). Its audio sits in the flat sample
  store `<app data>/OSP/Samples/<sha256>.<ext>`. **ANDOR Drums already uses that same store**
  (`plugin::SampleStore`, by SHA-256), and its build already links `osp_library` and the
  importers. Nothing in Drums talks to the catalog yet.
- **The catalog already has:**
  - records keyed by content hash, with duplicates found by hash;
  - tags with their source and confidence, where a removed tag stays rejected;
  - a category ("Type"), favourites, ratings, notes, use counts, recently used;
  - **collections** (`collections`, `collection_items`);
  - **derived sounds** (slices of a parent, with `slice_start`/`slice_end`);
  - full-text search (`Catalog::search(SearchQuery)`);
  - watched Inbox folders, folder scans, `.osppack` packs and backups;
  - a real-time-safe `PreviewEngine`.
- **Drums already has:**
  - its own drum classifier (M7: kick/snare/hat/… with a confidence and a reason);
  - break slicing (M6);
  - layers with round robin / random / shuffle (M3);
  - kits as `.andorkit` files.

So the library is mostly **wiring and drum vocabulary**, not new storage. That keeps CLAUDE.md
rule 6 (no unnecessary framework invention), and OSP and Drums share one library.

## 1. The drum library

### What a musician does

- **LIBRARY** opens beside BREAK in the top bar, as a panel over the pad editor (like SLICE
  BREAK). Its parts:
  - a **category column**: All, Kicks, Snares, Claps, Hats (closed/open), Toms, Percussion,
    Cymbals, Rims, FX, **Breaks**, **Kits**, **Pools**;
  - **search** (name, tags, folder, pack) with filters: favourites, rating, length, source
    pack, "used in this kit";
  - a **list** of sounds, with the category dot, confidence, length, tags and a mini
    waveform. Clicking auditions through the existing preview path, at its natural pitch and
    level-matched. Arrow keys step through the list while auditioning: the fast "flick
    through kicks" workflow.
  - **dragging** a sound onto a pad loads it (with the M3 drop choices). Dragging several
    makes layers or a family (see the multi-sound spec). Dragging onto **BREAK** slices it.
- **Getting sounds in:**
  - everything loaded on a pad, imported with IMPORT SAMPLES, or sliced as a break is
    **added automatically**. Nothing is copied twice: it is the same store;
  - **Add folder…** scans a sample folder. It uses `FolderScan`'s fast and deep stages in
    the background, with the drum classifier on top;
  - a watched **Inbox** folder (OSP's) picks up new downloads.
- **Breaks** are records too, with tempo, slice count and their slices as derived sounds. A
  break's slices can be searched like one-shots ("all kicks from my breaks").
- **Kits** become library entries as well: a kit record points at its `.andorkit` file and
  the content hashes it needs, the pattern of OSP's `presets` + `preset_sounds`. The library
  can then show "kits that use this snare", and a missing-sample relink can look in the
  catalog first.

### Data (catalog schema 6, additive)

| Need | How |
|---|---|
| Drum categories | Use `assets.category` with the drum vocabulary (kick, snare, clap, closedHat, openHat, tom, rim, cymbal, percussion, fx, break). OSP's own Types are not touched: OSP's browser keeps "Percussion" as the parent of all of them. |
| The drum classifier's result | `audio_analysis` rows tagged with Drums' classifier and version (it is cached by hash), plus tags with source `audio` and their confidence. A user's correction goes in `classification_overrides` and always wins. |
| Subkind for families | A tag such as `kind:lowTom` or `kind:conga`, for the multi-sound spec's GM placement. |
| Breaks | Category `break`, with BPM and slice count in a small `breaks` table keyed by asset. Slices are derived sounds (`parent_asset`, `slice_start`/`slice_end`). |
| Kits | A new `AssetType::kit` (schema 6). Its file is the `.andorkit`, and a `kit_sounds` table holds the hashes it needs, as `preset_sounds` does. |
| Which plugin added it | `history` records "ANDOR Drums" as the user, so OSP can show what came from Drums. |

**Threads.** All catalog work runs on a background thread with its own connection, as in
OSP (`docs/library/architecture.md`). The audio thread never touches it. Audition reuses the
preview path Drums already has, or OSP's `PreviewEngine` if one is added.

**Versioning.**
- Catalog `schemaVersion` 5 → 6, with a migration in `Catalog::migrate`. OSP and Drums ship
  the same `osp_library`, so both understand schema 6.
- An older OSP refuses a schema-6 catalog without touching it, as it already does for any
  newer schema. The user must therefore update both plugins together; we say so in the
  release notes.

## 2. Pools

**A pool is a named, saved set of sounds of one role.** Examples: "Kicks: tight", "Snares:
vinyl", "Perc: shakers". It is an OSP **collection** with a role attached.

### What a musician does

- **Making a pool:**
  - select sounds in the library and choose **Save as pool…**;
  - or drag sounds onto a pool in the Pools list;
  - or, from a pad, **"Save this pad's layers as a pool"**.
- **Smart pools** (optional, phase 2) are a saved search: "every kick rated ≥ 4 from the
  Vinyl pack". They refresh as the library grows.
- **Kept with the library:** pools are kept with the library, not the kit, so every kit and
  every session can use them. A kit that uses a pool **also stores the hashes it played**
  (see section 3), so the kit still opens identically on another computer without the pool.

### Data

- `collections` gains `role` (the drum category or none) and `kind` (`manual` | `smart`). A
  smart pool keeps its `SearchQuery` as JSON.
- Members stay in `collection_items`, ordered by `position` (for the cycle order).
- **Export:** a pool exports as an `.osppack` via the existing `exportPack` from a
  collection. Packs already handle sharing pools with other people.

## 3. Pads that draw from a pool (cycle and randomise)

This is the instrument side. It builds on M3 layers, which already have **sequential**
(cycle), **random without repeats** and **shuffle bag** orders, deterministic from the kit's
seed (rule 2).

### What a musician does

On a pad's SAMPLE page, the layer list gains a **SOURCE** switch:
- **LAYERS:** as today, the pad's own samples.
- **POOL:** pick a pool. The pad then plays from it. Up to 16 members are loaded at once:
  the pad's layer limit and its memory budget.
  - **Cycle:** every hit plays the next member (the sequential order).
  - **Random:** a random member, never the same twice in a row (the random order).
  - **Shuffle:** every member once before any repeats (the shuffle bag).
  - **Per bar** (new): the member changes only on a new bar of the host's transport, at a
    chosen rate (every bar, every 2 or 4 bars). The groove keeps one kick per phrase, and the
    next phrase gets another. This uses the host timing the engine already receives.
  - **Hold:** the pad keeps the current member, and a **dice** button rolls a new one. This is
    for auditioning a pool against the groove, then keeping the one you like ("Freeze to
    layer" turns it into an ordinary layer).
- **Variation within a pool** keeps each sound's character: every member plays at its
  natural pitch, matched in loudness to the pool's median (an optional **Match level**, on
  by default) so a random kick is never suddenly 6 dB louder.
- **Pools larger than 16.** When a pool has more than 16 members, the pad loads a window of
  16:
  - the window advances by **ROTATE** (manual), or every N bars (automatic);
  - it is loaded in the background, never on the audio thread (rule 1);
  - each new set reaches the audio thread through the pad's `ModelExchange`, as layers do
    today.
- **Kit-level RANDOMISE**, a button in the kit menu:
  - **"Roll the kit":** every pad set to a pool takes a new member, as one undo step;
  - **"Roll kicks only"** and so on per role;
  - **"Lock"** a pad to keep it out of rolls.

  This answers "randomise different kicks" for the whole kit. It is reproducible: a roll
  draws from `Prng::deriveSeed (kitSeed, rollCounter, pad)`, so undo/redo and bounces repeat
  exactly.

### Determinism and recall (rule 2, rule 7)

- **What a session saves:**
  - the pool's id and name;
  - the members' content hashes **in the order loaded**;
  - the picker state: the order and the hit counter, as M3 already does.
- **Reopening** plays exactly the same sequence, even if the pool changed since: new members
  are not used until the musician presses **Refresh pool**.
- **On another computer,** a kit with a pool falls back to the stored hashes, through the
  existing relink and portable-kit path. The pool itself is optional.
- **DAW bounces:** cycle and shuffle advance per hit and are seeded, and per-bar follows the
  song position. An offline bounce therefore matches the real-time playback.

### Data model (Drums)

| Today | Proposed |
|---|---|
| `PadLayers { mode, order, layers[] }` | Adds `PadSource { kind: layers / pool; poolId; poolName; memberHashes[]; windowStart; matchLevel; changeEvery: hit / bar / 2 bars / 4 bars / hold }`. |
| `LayerPlan` (audio) | Unchanged in shape: a pool pad is a layer plan of its loaded window. Per-bar change is a picker mode using the host's bar index. Level matching is each layer's `gainDb`, computed in the background at load. |
| State 3 / kit schema 1 | Session state 5 / kit schema 2 (together with the multi-sound spec): a `<Pool>` element inside `<Layers>`. Older states load as plain layers. |

## 4. Where it lives in the UI

- Top bar: **LIBRARY** beside **BREAK**.
- Pad editor, SAMPLE page:
  - the **SOURCE: LAYERS | POOL** switch above the layer list;
  - for a pool: its name, the member count, the order, the change rate, **dice** and
    **Rotate**.
- Pad cell: a small **pool mark** and the member now playing. It flashes on change, so a
  musician sees the kick change.
- Kit menu: **Roll the kit**, **Roll [role]**, **Lock pads…**.

## 5. Phases

1. **Library browser on the existing catalog.**
   - What's in it: drum categories, the classifier writing to the catalog, auto-add on
     load/import/break, audition, drag to pad, Add folder, search.
   - Tests: the catalog in a temporary directory (`OSP_LIBRARY_DIR`), the import and
     classify path, the OSP library suites unchanged.
2. **Pools.**
   - What's in it: collections with a role, Save as pool, a pool pad with cycle, random and
     shuffle (via M3), level matching, recall by hashes.
   - Tests: determinism across a save/load, a missing pool falls back to the hashes, zero
     allocation while rolling.
3. **Per-bar change, hold and dice, Roll the kit and lock.**
4. **Smart pools, windows over large pools, kits as library entries, pool packs.**

## Open questions for the user

1. Should the library be one shared library with OSP (one catalog, drums shown in Drums, and
   OSP's browser also seeing them as Percussion), or a separate Drums catalog? This spec
   assumes **one shared**, because the store is already shared.
2. On a pool pad, should the default change be **per hit** (more alive) or **hold with dice**
   (a stable groove until you roll)?
3. Should **Roll the kit** also re-roll breaks' slices, or only one-shot pools?
