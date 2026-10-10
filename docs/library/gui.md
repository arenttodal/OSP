# The Library window (Stage 5)

`apps/plugin/Source/LibraryPanel`: one overlay over the instrument, from the housing's top to
just above the keyboard row (decision D-10). The keyboard stays visible and plays the
audition when the tray's Keys switch is on. The window keeps the instrument's size and
scales with it (it is laid out in the instrument's reference pixels). It uses the
instrument's own materials, colours and type roles (`design::`, `type::`).

## Opening and closing

- The preset name's menu: **Open Library…** (first item). The ⋮ menu: **Library…**.
  **Cmd/Ctrl+L** opens and closes it.
- The window's **×**, **Escape** (when no sheet is open and the search is empty) or
  Cmd/Ctrl+L close it. Closing stops the preview and gives the keyboard back to the
  instrument.
- The main interface is unchanged (mockup panel 1 not adopted: the user, 2026-10-10).

## Views

| Mockup | View | What it does |
|---|---|---|
| 2 | A Presets | Complete instruments. Sidebar scopes: All, Factory, User, Packs, Favorites, Recent, Trash. The user's collections (+ makes one; right-click renames or deletes). Categories (Keys, Pads, Basses, Plucks, Rhythmic, Textures, Evolving, FX, Experimental, plus any in use), each with its count. Search; sort (name, recently used, newest, rating, length). Table: Name, Category, Origin, Rating (click a star; the same star again clears it), Date. Detail strip: artwork (D-09), name, origin · category · size, description, heart, stars, **Load**, ⋯ |
| 3 | B Templates | Settings without sounds: the built-in starting states (Factory, read only), then the user's. Sources column: the source count it expects. Detail: "Ready for your own sounds", **Load Template** |
| 4 | C Sounds | Every sound the Library knows. Filters: origin, length (under 1 s, 1–5 s, over 5 s) and sort; Types in the sidebar. Rows: waveform overview (tinted by Type), Name, Type, Length, Origin, Date. A row can be dragged into the tray |
| 5 | D Sound inspector | Right of the results: name and ▶, the waveform with its playhead (click: play), Type, Root note (detected or set; the set root goes with it into the instrument), Collections (chips + add), Tags (chips, a cross removes; Add tag), Notes, Properties (length, channels, added, last used, rate, size, format, uses, where it is), **Load to A / B / C / First Empty**, heart |
| 4 | Audition tray | A, B, C slots (drop a sound, or click an empty slot to take the selected one; click: play it; the cross clears; right-click: replace or clear). ▶ plays all three together, ■ stops. **Keys play the audition** switches the keyboard to the preview: the tray's sounds, or the selected sound when the tray is empty. Preview level. **Load to Instrument** commits (docs/library/preview.md) |
| 10 | E Save | Name; Type (Preset with sounds / Template, no sounds); Category; Origin (User); Tags; Description; **Include audio as portable copy** (writes the `.ospinstrument` beside it). An existing name asks first; the old file goes to the trash |
| - | J Library settings | Preview level; storage (managed copies by what holds them); Empty Library trash… (asks first; docs/library/preview.md, Storage); show the sample store and the Library folder; catalog check |
| - | K Missing sounds | Every sound that cannot be found (not in the store, not where it was seen), with where it was last seen. **Locate…** checks the chosen file by its bytes and remembers the place; a sound can be sent to the trash |

| - | F Scan folder | **⋯ → Scan folder…**: the folders the user chose (Add folder…, Scan again, Stop scanning it), subfolders, "find likely one-shots", skip files over 60 s, Only names like / Leave out patterns; live counts (found, new or changed, unchanged, unreadable, analysed of, each class) with a bar; Pause/Resume, Cancel, Done. The scan runs in the background; the window shows Scanned when it finishes (docs/library/classification.md) |
| - | G Scanned / Set aside | Sidebar **Finding sounds**: Inbox, Scanned, Set aside, with counts. Rows: waveform, Name, Kind, Length, State (New, Not analysed, Cannot read, In Library, Set aside). Several can be selected. The **candidate inspector** replaces the sound inspector: ▶ and the waveform, the kind as a chip with its reason, **Kind** (as analysed or the user's; the user's wins), details and path, **Trim / Slice…**, **Set aside / Bring back**, **Import to A / B / C**, **Import, load to first empty**, **Import to Library** (with several selected: Import N) |
| - | H Inbox | The same list for the Inbox folders' arrivals, newest first, with their state (Arriving…, Ready, Importing…, In Library, Cannot read, Missing) and reason (docs/library/inbox.md). Inbox folders are chosen in Library settings |
| - | I Trim and slice | Waveform with zoom and scroll, cursor, slice regions with draggable edges and names; Suggest trim, Find slices (sensitivity), Split at cursor, Merge with next, Remove slice, Rename…, ▶ Slice, Reset; the slice list (include boxes); peak to −1 dB and zero-crossing options; **Save as New Sound / Save N Slices to Library** (docs/library/slicing.md) |

L (Mobile Capture) belongs to Stage 9 and is not in the window. There is no placeholder
for it ("no placeholder buttons, no disconnected screens").

## Actions

- **Single click** selects. **Double-click** or **Return** loads: a preset or template
  replaces the patch, recoverably. A sound goes into the first empty layer. When all three
  hold a sound, the window asks which layer to replace; A is never replaced silently.
- **Right-click** a row:
  - Presets and templates: Load, Duplicate, Export file…, Export as portable
    instrument… (for the loaded preset), Category, Describe…, Show in Finder.
  - Sounds: Preview, Audition in A/B/C, Load into A/B/C / first empty, Trim / Slice….
  - Candidates (Scanned, Inbox, Set aside): Preview, Import to Library, Import as…,
    Import, load into A/B/C, Trim / Slice…, Kind, Set aside / Bring back, Show in Finder.
    Return and double-click import; Delete sets a scanned candidate aside (never deletes).
  - Both: favourite, Rating, Rename…, Move to Trash.
  - In the Trash: Restore (also Return or the detail's button).
- **Delete** moves the selected item to the trash (recoverable).
- **Space** previews the selected sound. **Cmd/Ctrl+F** goes to the search.
  **Cmd/Ctrl+1/2/3** switch views. Up and down move through the results and the sidebar.
- After a load, the footer offers **Back to previous patch** (the processor's
  previous-state swap).
- Built-in starting states are read only. Editing one and saving makes the user's own copy.
- Preset favourites are the header heart's (`Favourites.txt`, by name). The Library sets
  both, and the catalog follows the file when the window opens (D-11).

## Threads

Every query and edit runs on the Library thread. `LibraryService::request` returns the
answer on the message thread at the next `deliver()` (the window's 30 Hz timer and the
processor's timer), and only the newest query's answer is applied. Previews decode on the
preview thread (`Audition`). Row waveform overviews (48 bins, the first 30 s) are made in the
background on first sight and kept in memory. Opening the window indexes preset and
template files that the catalog does not know yet (saved before the Library existed, or
copied in by hand).

## Accessibility and scaling

- Every control has an accessible title. List rows have names (name, category, origin).
- Selection is shown by a fill and an accent bar, never by colour alone. Hit targets are
  at least 26 reference px.
- Keyboard: focus moves with Tab; the shortcuts are listed above.
- Scaling: the window is part of the instrument's scaled layout. A test checks that no part
  leaves the window at 80, 100, 150 and 200 %.

## Tests

`[plugin][library][library-ui]` (the real editor):
- Every view, the sidebar counts, search, scopes and categories.
- Loading a preset (recoverable).
- Built-in templates.
- Selecting, previewing and auditioning sounds without changing the patch.
- The tray's commit.
- The asking when all layers are full; Escape.
- The trash and restore.
- Saving, and the replace question.
- The settings and missing-sounds sheets.
- Every scale.
- Cmd/Ctrl+L.

`[plugin][library][library-capture]`:
- Scanning a chosen folder from the window; the Scanned list and its counts.
- Kinds and a correction that sticks; set aside and bring back.
- Importing two at once with their evidence.
- An Inbox folder: an arrival becomes ready and classified; import and load into B.
- Slicing a scanned groove: the original imported first, eight new sounds, the file
  unchanged; a slice into C.
- Snapshots: `library-scan.png`, `library-scanned.png`, `library-inbox.png`,
  `library-trim.png`, `library-slices.png`, `library-settings-inbox.png`.

`OSP_SNAPSHOT_DIR=<dir>` writes `library-presets.png`, `library-templates.png`,
`library-sounds.png`, `library-choose-layer.png`, `library-save.png` and
`library-settings.png`.
