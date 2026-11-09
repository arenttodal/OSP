# Packs (Stage 10)

A pack brings sounds, presets and templates into the Library as one installable,
updatable, removable thing. The code is in `src/library/Packs` and `src/library/Archive`;
the window's side is `apps/plugin/Source/LibraryPacks.cpp`. Tests: `[unit][library][packs]`
(with `[security]`) and `[plugin][library-packs]`.

## The file

A `.osppack` is a zip archive (D-20) holding `manifest.json` and the files the manifest
lists:

| Entry | Holds |
|---|---|
| `manifest.json` | the manifest, below (at most 8 MiB) |
| `sounds/<sha256>.<ext>` | a sound (WAV, AIFF or FLAC), named by its content's SHA-256 |
| `presets/<name>.osppreset` | a preset (the instrument's state; its sounds are referenced by hash) |
| `templates/<name>.ospstate` | a template (settings without sounds) |

The manifest (`schemaVersion` 1):

```json
{
  "format": "osppack", "schemaVersion": 1,
  "id": "com.example.warm-keys", "name": "Warm Keys", "version": "1.2.0",
  "creator": "...", "description": "...", "license": "...",
  "requiresPlugin": "1.0.0", "factory": false,
  "requires": [ { "id": "com.example.core", "version": "1.0" } ],
  "items": [
    { "uid": "kick-01", "kind": "sound", "path": "sounds/<sha256>.wav", "sha256": "...", "bytes": 123456,
      "name": "Kick 01", "category": "Percussion", "tags": ["kick"], "rootMidi": 36 },
    { "uid": "warm-hymn", "kind": "preset", "path": "presets/Warm Hymn.osppreset", "sha256": "...", "bytes": 4096,
      "name": "Warm Hymn", "category": "Keys", "sounds": ["<sha256>", "..."] },
    { "uid": "two-layers", "kind": "template", "path": "templates/Two Layers.ospstate", "sha256": "...", "bytes": 2048 }
  ]
}
```

The fields:

- **`id`**: lower-case letters, digits, `.`, `_` and `-`, at most 100 characters.
- **`version`, `requiresPlugin`**: dotted numbers, compared number by number (`1.10` > `1.9`).
- **`uid`**: stable from one version of the pack to the next. It is how an update finds
  the same record.
- **`requires`**: other packs, and the minimum version of each.
- **`factory`**: true for packs that ship with the plugin.
- **`license`**: shown before installing.

## Installing: everything is checked before anything changes

`inspectPack` is what the confirmation sheet shows, and `installPack` repeats every check.
The archive and its manifest are untrusted.

**The archive** (`ArchiveReader`) is refused whole when:

- an entry has an absolute path, a drive letter, `..`, a backslash, a control character, a
  hidden part, or is nested too deep;
- an entry is a symbolic link;
- a name appears twice;
- one file claims more than 2 GiB, the whole more than 4 GiB, or there are more than 60,000
  entries.

**The manifest** is refused when:

- it is missing, not JSON, or newer than this build;
- its id or version is not valid;
- an item's path is outside its folder, or a sound's file name is not its hash;
- a uid or a path appears twice.

Its text (names, description, licence) is cleaned before it is shown or stored: valid
UTF-8 only, no control characters, capped lengths.

**The contents**, refused with nothing changed when:

- a file listed in the manifest is missing, or of another size;
- a file's checksum differs. Every file is unpacked into a staging folder and its SHA-256
  checked before anything is used.

**What it needs**, refused with nothing changed when:

- it needs a newer plugin;
- a pack it requires is not installed (or not that version);
- a preset needs a sound that is neither in the pack nor already in the store;
- a newer version of the same pack is installed (no downgrade).

Nothing in a pack is ever run: there is no script, no code and no plugin in the format.

Then the pack's files go into place:

- **Sounds** are imported into the store (verified copy, atomic rename) with origin Pack,
  or Factory for a factory pack. Their provenance is "licensed", under the pack's licence.
- **Presets and templates** go into the pack's own folder, `<Library>/Packs/<id>/Presets`
  and `Templates`; factory packs use `<Library>/Factory/<id>`. They never go into the
  user's folders.
- **Records** carry `pack_id` and their `uid` as `pack_key` (catalog 5).

Installing the same version again repairs a pack; it never duplicates anything.

## Updates keep the user's edits

A newer version finds each record by its uid:

- **Same identity.** The record keeps its id, so it is never a new record.
- **New audio and details.** When a sound's audio changed, the record takes the new
  content. Its name, category and preset files become the new version's.
- **The user's edits stay.** Favourite, rating, the user's tags, notes and collections are
  kept.
- **Items the new version dropped** are removed: sounds to the Library's trash, so a user
  preset that uses one still finds its audio; presets and templates are purged with their
  pack files.

The user's own content is never touched by an install, an update or a removal.

## Removing

**Packs… → Remove pack** (it asks first). Here is what happens to each part:

| What | Where it goes | Can it come back? |
|---|---|---|
| The pack's presets and templates | the computer's Trash, with the pack's folder | from the Trash |
| The pack's sounds | the Library's trash | yes, until the Library's trash is emptied |
| The audio | stays in the store while anything may use it (a user preset, a project) | — |

Emptying the Library's trash later frees the audio only when nothing holds it (Stage 4's
rule).

## Factory packs

Factory packs are packs with `"factory": true`, shipped in the plugin bundle's
`Resources/Factory` (`OSP_FACTORY_PACKS_DIR` overrides it). At startup the processor
installs any that are new or newer, in the background (`installFactoryPacks`). Their records
are origin Factory, and their files live in `<Library>/Factory`, apart from user data. Their
updates follow the rules above, so they never damage user content. A non-factory pack in
that folder is ignored.

The build puts them there: every `*.osppack` in `OSP_FACTORY_PACKS_DIR` (CMake; default
`packaging/factory-packs/`, not committed) is copied into each plugin bundle's
`Contents/Resources/Factory` (`cmake/CopyFactoryPacks.cmake`). With no pack in that folder
the bundles are exactly as before. No factory pack ships yet: the owner chooses the Core
set (50-100 presets, `docs/release/SOUND_PACKS_SPEC.md`).

## Making a pack

Right-click a collection in the sidebar → **Export as pack…**. The pack holds:

- the collection's sounds, presets and templates;
- every sound those presets need.

It gets an id of the user's own (`user.<name>.<8 hex>`) and version 1.0.0. Sounds whose
distribution rights are unknown (found on a drive) are named, and the export stops until
the user confirms they may share them. The confirmation is recorded, the same rule as a
portable instrument meant for sharing (Stage 3). Sounds that came with another pack are
noted: their licence decides whether they may be shared.

## Publishing a pack (expansions and the Core pack)

Right-click a collection → **Publish as pack (id, version, licence)…** makes a pack whose id
and version the publisher chooses, so later versions update it in place:

- **Pack id** (`com.evenant.osp.dusty-keys`): kept for every version.
- **Version:** the sheet suggests the next one after the last published.
- **Creator, licence, description:** shown before installing.
- **Factory pack:** for the Core pack built into the installer only
  (`PackExport::factory`; off by default, so every other export is unchanged).

The fields are remembered per collection in `<Library>/Publishing/<collection>.json`.
Publish every version from the same collection in the same Library: item uids come from
its records, and they are what keeps a customer's edits through an update. The same rights
question as an ordinary export applies. How packs are sold: `docs/release/SOUND_PACKS_SPEC.md`.

## Tests

The unit tests (`[unit][library][packs]`) cover:

- **Names**: names, text, ids and versions from outside are checked.
- **A collection exported, then installed in another Library**:
  - the rights question;
  - records, store, the pack folder;
  - a repair does not duplicate;
  - an update (new audio for one sound, a template dropped) keeps the same identity, the
    favourite, rating and tag;
  - a downgrade and a too-new pack are refused;
  - removal leaves the user's sound alone.
- **Hostile archives**, made byte by byte in the test (traversal, links, duplicates, lying
  sizes, missing or newer or invalid manifests, wrong checksums and sizes, missing
  dependencies, oversized text). Each is refused, with nothing in the catalog, the store or
  the packs folder.
- **Factory packs**: installed and updated from a folder; non-factory ones are ignored;
  user content is unchanged.
- **Publishing**: an export without the factory mark is never installed from the plugin's
  folder; with it, it installs into `<Library>/Factory`, updates in place under the same id,
  and leaves the user's presets and sounds alone.

The plugin test (`[plugin][library-packs]`), through the real window:

- export a collection (answering the rights question);
- install it in a second Library with confirmation;
- the Packs scope and sheet;
- a factory pack installed at plugin startup into `<Library>/Factory`.
- a collection published as a factory pack (the sheet refuses an invalid id; the publisher's
  id and version), installed at startup beside a musician's own preset, which stays
  byte-identical.
