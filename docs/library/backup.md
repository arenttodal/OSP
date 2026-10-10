# Library backup, restore and storage cleanup (Stage 10)

The code is in `src/library/Backup` and `Catalog::snapshotTo` / `mergeFrom`; the window's
side is `apps/plugin/Source/LibraryPacks.cpp`. Tests: `[unit][library][backup]` and
`[plugin][library-packs]`.

## A backup

**⋯ → Back up the Library…** asks for a name and whether to include external files, then
writes a folder named `<name>.ospbackup` (D-19). Its contents:

| Entry | What |
|---|---|
| `catalog.db` | the whole catalog: records, tags, collections, favourites, ratings, notes, history, rights, analyses, packs |
| `store/<sha256>.<ext>` | every managed sound a record or a preset needs (trashed records too, so they can be restored), and the copies from before the Library (old projects may use them) |
| `presets/…` | the user's presets (`.osppreset`) |
| `templates/…` | the user's templates (`.ospstate`) |
| `packs/<id>/…` | installed third-party packs' files |
| `settings.json` | the Library settings |
| `favourites.txt` | the header heart's favourites |
| `manifest.json` | written last: what the backup holds, every file with its size and SHA-256, the folders it came from, the plugin and catalog versions |

How a backup is made safe:

- **One consistent catalog.** The catalog is copied with SQLite's `VACUUM INTO`: a single
  consistent snapshot, taken while other plugin instances keep writing. The live database
  file is never copied (a test writes from another connection throughout).
- **Checked copies.** Every file is copied and then checked against its hash.
- **All or nothing.** The backup is written as `<name>.ospbackup.partial`. It is renamed
  only when complete. A cancel, or any error, removes it. An existing backup is never
  written over.

Factory packs are not in a backup: they come with the plugin. Scan folders, the Inbox and
the preview cache are this machine's: their state is in the catalog snapshot, but a restore
leaves it out.

### External references (10.6)

A sound whose bytes are not in the store is **not included unless asked**. It may be
indexed in place, or only at its original place (a moved store copy). The backup lists
each one under "Not included", with where it was last found.

**Include external files** copies them into the backup's `store/`, so after a restore they
are ordinary managed sounds. Files a scan found but the user never imported are not Library
sounds and are never part of a backup.

## Restoring

**⋯ → Restore a backup…** reads the backup and shows what it holds. Then it asks what
happens when a record is in both: **Keep mine** (the default) or **Newer wins**.

Every check comes first. A backup is untrusted, and it is refused with nothing changed when:

- it has no manifest (an incomplete backup), or it is newer than this build;
- a listed path is unsafe (`..`, absolute, hidden, too deep, outside the backup's folders);
- a file is a link, missing, or not its listed size;
- a stored sound's name is not its hash;
- its catalog is unreadable or damaged.

A stored sound whose bytes no longer match its hash is reported and never stored. The rest
is restored.

Then it **merges** into the Library that is there, which may be empty (a new machine). It
does not replace it (D-21):

- **Records keep their identities.** A record that is only in the backup is added with its
  id.
- **Records in both.** With Keep mine the one here stays as it is. With Newer wins the
  backup's is taken where it was modified later.
- **One copy each.** Tags merge by name, collections and their items by id, and history
  without repeats. Analyses, the user's classifications and pack records are added where
  missing. Store copies are never doubled.
- **Another machine's folders.** Presets, templates and pack files are moved from the
  backup's folders to this machine's. Their records follow.
- **Files**:
  - a missing file is written;
  - one that is the same is left alone;
  - one that differs is either kept (Keep mine: its record decides), sent to the system
    Trash first and replaced (Newer wins, when the backup's record is newer), or written
    beside it as "Name (restored)" with its record following;
  - nothing is overwritten without a way back.
- **Settings and favourites.** `settings.json` is restored only where there is none.
  Favourites are joined.
- **Search** finds the restored records: their search entries are rebuilt.

Restoring the same backup twice changes nothing the second time (tested).

## Storage cleanup (10.7)

**⋯ → Library settings** shows the storage:

- **Managed copies**, by what holds them: Library records, only the Library's trash, or
  nothing (from before the Library).
- **The analysis cache**: how many analyses, their size, and how many no longer refer to
  anything.
- **The preview cache**: the waveform overviews in memory. Nothing is cached on disk.

The cleanup actions:

| Action | What it does | Why it is safe |
|---|---|---|
| **Empty Library trash…** (Stage 4) | removes trashed records; moves copies nothing else holds to the computer's Trash | recoverable from that Trash; never a copy a preset, another record or an open layer uses |
| **Forget unused analysis** | deletes analyses of content no record, preset, scan or Inbox item refers to | they can always be made again |
| **Clear preview cache** | drops the overviews in memory | they are made again when seen |

Copies from before the Library are never offered for deletion: the Library cannot know
whether a saved project still uses one (spec 10.7, "never delete audio still required by
saved presets or projects").

## Tests

The unit tests (`[unit][library][backup]`) cover:

- **A whole Library**, backed up and restored into another with different folders:
  - every identity, favourite, rating, trashed state, history, tag and collection kept;
  - preset and template files moved to the new folders, byte-identical;
  - their sounds resolved from the store;
  - favourites, settings and search all work;
  - restoring it again doubles nothing.
- **A Library in use**: Keep mine keeps a later rename and a local file; Newer wins takes a
  newer record; a different file at the same place is written beside it, never over it.
- **A busy, odd Library**:
  - a consistent snapshot while another connection writes;
  - external files left out unless asked, and once included, restored as managed sounds;
  - a cancelled backup leaves nothing.
- **Damaged and hostile backups** (no manifest, an unsafe path, a newer version, a link in
  the store, a garbage catalog) are refused with nothing changed. A damaged sound is
  reported and not stored.
- **The analysis cache** and what can safely be forgotten.

The plugin test (`[plugin][library-packs]`) backs up machine A through the window and
restores into machine B with the confirmation. The restored preset opens and finds its
sound in B's store.
