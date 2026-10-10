# The capture Inbox (Stage 7)

Audio saved into folders the user chose appears in the Library's **Inbox**, ready to hear,
name, trim, slice, import or load. Sources can be:

- Finder
- a Voice Memos export
- a Dropbox or iCloud Drive folder
- a phone app that saves into one of these

There is no account, no server and no cloud of OSP's own; a sync client the user already
has does the moving. Code: `src/library/Inbox`. Tests: `[unit][library][inbox]` and
`[plugin][library-capture]`.

## Folders

**⋯ → Inbox folders…** (also in Library settings): **Add folder…** and **Stop watching**.
The user picks each folder; nothing is chosen for them. The list lives in
`<Library>/settings.json` (`inboxFolders`). Files in an Inbox folder are never moved,
changed or deleted. Stopping watching a folder keeps everything that came from it in the
Library.

**Sandboxed builds.** The current plugin and standalone are not sandboxed, so a path the
user chose stays readable. A sandboxed build (a Mac App Store app, or a future iOS
companion) would lose access after relaunch. It would need a **security-scoped bookmark**
for each chosen folder: `NSURL bookmarkDataWithOptions:
NSURLBookmarkCreationWithSecurityScope`, stored beside the path, and
`startAccessingSecurityScopedResource` before every pass. Inside a host the sandbox is the
host's, so a sandboxed host (Logic, GarageBand) runs AU plug-ins out of process with their
own entitlements. This is investigated and documented here; it is not built, because no
shipped build is sandboxed. It belongs with the signing work (Stage 12).

## Reconciliation, not notifications

Every pass looks at the folders as they are now; nothing depends on having heard every
change. A pass runs:

- once when the plugin starts (in the background, so files that arrived while the DAW was
  closed are waiting);
- as soon as the Library window opens;
- every 3 s while the window is open;
- not while a scan has the background thread.

This is a decision (D-16). Filesystem notifications (FSEvents on macOS) would only make a
change show up sooner. They would still need this reconciliation, which the spec requires
("never assume notifications are complete"). JUCE has no portable watcher. A pass over a
few hundred files costs a few milliseconds. FSEvents can be added later as a "check now"
trigger without changing the states.

## States

| State | Meaning | Next |
|---|---|---|
| detected | Just seen | waiting, or ready once it has stood still |
| waiting | Still arriving (size or time changed within 2 s), an iCloud placeholder not downloaded yet, or unreadable so far | ready when stable and readable; failed after 6 stable, unreadable tries |
| ready | Stable, the header reads, hashed, classified | importing (the user's import) |
| importing | Being copied into the Library | imported or failed |
| imported | In the Library (or its bytes already were: a duplicate) | (gone from the list when the file goes) |
| failed | Unreadable after every try, or the import failed; the reason is shown | ready again if the file changes; the user can retry |
| missing | The file went (moved or deleted), or its folder is offline | back to where it was when the file or the folder returns |

**The rules.**

- **A partial download is never called damaged.** An unreadable file waits and is tried
  again. It is called failed only after six tries on a file that stood still.
- **Temporary download names are ignored**: `.part`, `.partial`, `.download`,
  `.crdownload`, `.tmp`, `.dropbox`, `.sb-…`, `~$…`. Hidden files are ignored too.
- **iCloud placeholders** (`.Name.wav.icloud`) mean the file is in iCloud and not on this
  Mac. They show as waiting, "download it, or keep it downloaded".
- **Duplicates**: content already in the Library is marked imported, pointing at the
  existing sound. It is never copied twice.
- **Renames**: a renamed file is recognised by its bytes.
- **Changed after import**: a file that changes after import becomes a new arrival.
- **Crashes**: an item left "importing" (a crash, a closed host) goes back to ready, and the
  import checks the copy again.
- **Offline folders**: a disconnected drive, or a sync client that is not running. Its items
  become missing, not failed, and come back with the folder.

## Workflow

**Sounds → Inbox** lists what has arrived, newest first.

- **Selecting** an item cues it: ▶, Space or a click on the waveform plays it.
- **The inspector** shows its kind (Stage 6), length, rate and channels, its state and
  reason, and its path.
- **Kind**: correct it there or with right-click → Kind.
- **Import to Library** brings it in as **Captured**, provenance "original" (the user's own
  recording). It keeps its name's and folders' tags and Type, and a root from its name when
  the audio agrees.
- **Import as…** (right-click) gives it a name on the way in.
- **Tags** are edited in the inspector once it is in the Library.
- **Import to A / B / C** and **Import, load to first empty** do both in one step
  (recoverable: Back to previous patch).
- **Trim / Slice…** opens the editor (docs/library/slicing.md). Saving slices of an item not
  imported yet imports the whole recording first, unchanged, so every slice names its
  original.
- **Several items** can be selected (Shift/Cmd-click) and imported at once.

## Optional background helper (investigated, not built)

A small macOS login item could watch the folders while no DAW is open, using FSEvents
and the same `reconcileInbox` in its own process on the shared catalog (SQLite WAL is
already safe across processes, S2-07).

Why it is not built:

- The startup and window-open passes already catch everything that arrived while the DAW
  was closed.
- A helper adds a signed, notarised second binary and a login item the user must allow.
  Those depend on signing (Stage 12).
- It would only spread the hashing and classifying over the day.

The spec makes it optional, and nothing in the Library depends on it.

## Tests

`[unit][library][inbox]`:

- a slow copy waits;
- a placeholder waits, then is ready when the file lands;
- duplicates;
- import as captured, the file untouched;
- a renamed file;
- a deleted file goes missing, and comes back;
- an offline folder;
- a crash mid-import;
- an unreadable file waits, then fails after its tries;
- temporary names ignored;
- a ready item is classified, and its import carries its name's and folders' evidence.

`[plugin][library-capture]` uses the real window:

- an Inbox folder in the settings;
- a file arrives, becomes ready and is classified;
- import and load into B in one step;
- it lists under Captured.
