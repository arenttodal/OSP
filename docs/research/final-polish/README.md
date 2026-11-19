# Final interface polish (17 November 2026)

## 1. INFO: See tutorials
- Every Info note now has a neutral **See tutorials** key just left of **Close**: the same height
  and centre line, 8 px apart. Close keeps its styling: primary when the topic has no lesson.
- The key turns Info off, closes the note and opens the existing tutorial list. It starts no lesson.
- A note with four keys (Open Controls, See tutorials, Close, Start Lesson) wraps its first keys
  onto a row above, instead of running off the card. Notes that fit on one row are unchanged.
- Code: `shared/andor-learn/juce/LearnController.cpp`, `LearnOverlay.{h,cpp}`. Shared, so
  ANDOR ANALOG gets it as well.

## 2. Preset detail card
- The heart sits 10 px after the measured name, on the name's centre line, lowered 1 px to look
  centred.
- A long name truncates with an ellipsis before the heart. Before, the heart sat at the far right
  of the text column.
- Load and ⋯ share one height (38 px) and one centre line: the card's own vertical centre.
- Code: `apps/plugin/Source/LibraryPanel.cpp` (`Detail::layout()`).

## 3. ROOT NOTE and TYPE (sound inspector)
**ROOT NOTE** (`apps/plugin/Source/RootNoteSelector.{h,cpp}`):
- It replaces the 73-row list with a field in ANDOR's dropdown look. A click opens a small picker:
  - Detected, with the analysis's note when known;
  - the 12 pitch classes as an octave of keys;
  - an octave stepper (−1 to 9; C4 = 60, as the Library names notes);
  - the resulting root.
- Every MIDI root 0–127 can be chosen. Before, only 24–96 were listed.
- Keys and octave steps that would leave 0–127 are disabled, never clamped. For example, G#9–B9
  are disabled, and from A8 the octave can't go up.
- Each choice is the same catalog edit as before (`setRootMidi`), so persistence is unchanged.
  The Library has no undo for sound metadata; it had none before either.
- Escape, or a press outside, closes the picker. The arrow keys move the note and the octave.

**TYPE**:
- It is now `AndorDropdown`, a ComboBox drawn as the racks' MODE field: light ceramic face,
  centred capitals, small chevron, rack focus ring.
- The choices, the list and the catalog field are unchanged.
- `drawDropdownField` (`shared/andor-ui/Source/OspLookAndFeel.{h,cpp}`) is shared, and nothing
  else changes look.

## Tests
`osp_plugin_tests "[final-polish]"`:
- **INFO:**
  - the key's label, placement, height and centre line;
  - Close's styling;
  - a real click opens the list with Info off and no lesson;
  - four keys stay inside the card.
- **Preset card:**
  - the heart is 8–12 px after the name, on its centre line, and drawn there;
  - a long name truncates before the heart;
  - Load and ⋯ share a centre line and a height, at the card's centre.
- **ROOT NOTE:**
  - every MIDI root, and the edges at octaves −1, 8 and 9;
  - in the inspector: A3, then the octave down to A2, written to the catalog, recalled after
    reselecting, Escape, and Detected again.
- **TYPE:** the dropdown's look, the same choices, and a choice written to the catalog.

## Screenshots (this folder)
- `before-*` / `after-*`: `OSP_FINAL_POLISH_SHOTS=<dir> OSP_FINAL_POLISH_PREFIX=before|after
  osp_plugin_tests "[final-polish-compare-shots]"`. The `before-*` set was made with the previous
  Learn and Library sources.
- `after-04-root-picker-open.png`: `"[final-polish-shots]"`.
