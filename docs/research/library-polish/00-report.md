# ANDOR OSP — Library & Preset Browser polish

17 November 2026. Branch `claude/osp-library-polish`, on top of `claude/osp-user-macros`.

## Gate 0 — audit

| Part | Where | Found |
|---|---|---|
| Library glyph | `shared/andor-ui/Source/KeyboardUtilityActions.cpp` (`Action::library`) | Two upright spines and a third rotated 0.36 rad (~21°) about its bottom-left corner. Only OSP's footer shows it. |
| Preset browser | `apps/plugin/Source/LibraryPanel.cpp` (Presets / Templates views) | The seven built-in starting states (Natural, Alive, Floating, Broken, Frozen, Dream, Wide) sat as plain rows of the Templates list, among the user's saved templates. They are already identified by registry: `LibraryRow::program` (`"program:<n>"`), never by name. |
| Sounds tab | `LibraryPanel::Inspector`, `LibraryPanel::Tray` | The inspector ended with LOAD TO A / B / C and Load to First Empty. The A/B/C tray (the only drop targets while the Library covers the cards) only held auditions. Loading needed **Load to Instrument**; **Keys play the instrument** switched the keyboard to the tray. |
| Preview transport | `LibraryPanel::previewSelected`, `Audition`, `src/library/PreviewEngine` | See below. |

**The Space bug, exactly.**
- `LibraryPanel::keyPressed` called `previewSelected()` on every Space.
- `previewSelected()` always called `Audition::play()` (when the sound was prepared) or `Audition::preview()`.
- Both start the sound from 0. `PreviewEngine::play` replaces what is playing.
- There was no paused state anywhere, so the second press restarted instead of pausing.
- JUCE's key auto-repeat called it again for as long as Space was held.

## The changes

1. **Gate 1 — glyph:**
   - the leaning book is now 5° from vertical;
   - it pivots on its outer bottom corner, so its foot rests on the shelf line beside the
     upright spines;
   - stroke, rounding, uprights, button geometry, material and neighbours are unchanged.
2. **Gate 2 — removed controls:**
   - LOAD TO A / B / C, Load to First Empty, **Keys play the instrument** and **Load to
     Instrument** are gone, together with their layout space;
   - the row menu's "Audition in A/B/C" is gone too.
   - The keys and MIDI always play the instrument (`setPreviewKeys (false)`), as before by
     default.
3. **SOURCES A B C** (the old tray, same place and look):
   - a sound dragged onto A, B or C is in that source at once, through the existing
     `Audition::loadIntoLayer`: one recoverable step, undone by **Back to previous patch**;
     an occupied source is replaced;
   - a click on an empty well takes the selected sound;
   - an empty well shows what the source holds now, or the one hint "Drag onto a source";
   - ▶ still plays the wells together, dry; the preview level stays;
   - Return and double-click still load into the first empty source.

   **Decision to confirm:** the spec asks for "drag onto A, B or C, with no extra load key". The
   Library covers the instrument's cards, so the wells are the sources' drop targets, and a drop
   loads instead of only auditioning.
4. **Gate 3 — transport:**
   - **Engine** (`PreviewEngine`, the smallest extension):
     - `playFrom(mask, seconds)` resumes at a position;
     - `pause(mask)` fades out only the browser's one-shot. Tray voices and keyboard notes keep
       sounding.
     - Both use a 3 ms ramp (`resumeFadeSeconds`). The commands go through the existing
       lock-free queue: no allocation, no lock.
   - **`Audition`:** one explicit transport per browser preview, `Transport {stopped, playing,
     paused}`, and never two flags.
     - `toggleBrowser` goes from stopped to playing from 0, from playing to paused (position
       kept), and from paused to playing at that position.
     - A preview that ran to its end, failed, or was never rendered reads as stopped again.
     - A press for another sound than the paused one starts that sound fresh.
     - `stopBrowser()` runs on a selection change, a tab change, and in the destructor
       (`stop()`).
   - **Space** is handled once per physical press: `keyStateChanged` re-arms it, so
     auto-repeat is eaten.
     - The search field and every text field keep their spaces, because they consume the key
       first.
     - The inspector's ▶ / ❚❚ key calls the same toggle.
     - The waveform keeps its playhead and shows "position / length" while playing or paused.
5. **Gate 4 — STARTING STATES:**
   - one compact row of understated tiles above the list, in **Presets** and **Templates**;
   - each tile shows the Library's existing generated artwork (D-09) as a small swatch, the
     name, hover and focus, and an accent bar on the state in use
     (`OspAudioProcessor::activeBuiltInState`, by program index);
   - a click applies the state: settings only, the sounds stay, recoverable;
   - below the tiles, an understated hairline and the list's own heading: PRESETS or
     TEMPLATES.
   - Built-ins are no longer list rows. Identity is `program`, so a user preset called
     *Natural* stays a preset.
   - The search narrows both sections in place; the user's own scopes (User, Favorites, Recent,
     Trash, collections, categories) show no built-in tiles.
   - Nothing about how states are stored changed.
6. **Gate 5 — Sounds rows:**
   - the name is primary;
   - Root, Type and Length are quieter;
   - Origin and Date left the rows: the sidebar and the sort already carry them;
   - a long name truncates, with its full name as the row's tooltip.

   **The inspector** reads in this order:
   1. the name, with length · channels · rate · format under it;
   2. the waveform with playhead and time;
   3. Type and Root;
   4. collections, tags, notes;
   5. Added, Last used, Size, Uses and the path.

   Nothing filled the recovered space.
7. **Docs and Learn:**
   - `docs/user-guide.md` (The Library) now says "Select a sound. Press Space to play or pause.
     Drag it onto a source.", describes the STARTING STATES and SOURCES, and gives Space's
     focus rule;
   - the LIBRARY key's Info text says the same.
   - No Learn lesson referred to the removed keys.

## Tests

`osp_plugin_tests "[library-polish]"`:
- the engine pauses and resumes with no click (largest sample step under the sine's own);
- Space plays, pauses (position kept, silence), resumes there, the end restarts, held Space
  toggles once, another sound starts fresh, a tab change and closing stop it;
- no LOAD TO, Keys play or Load to Instrument; drops on A, B and C load into those sources,
  replace an occupied one, and Back to previous patch undoes;
- STARTING STATES in both views by identity: the user's *Natural* stays a preset, search,
  scopes, a tile loads and marks its state.

The existing `[library-ui]` test was adjusted: the built-ins are tiles now, not Templates rows.
Screenshots come from the hidden test `[library-polish-shots]`, in this folder.

## Known limitations

- Space reaches the preview only while the Library has keyboard focus. A host that keeps
  Space for its own transport, with the plugin window unfocused, is respected. This was not
  checked in Logic or Ableton here.
- The engine's 3 ms ramp was verified headless (sample-step test), not by listening in a host.
