# Text inputs and overlay layers: the system-wide audit (17 November 2026)

## Text inputs

**Confirmed cause.**
- JUCE's `TextEditor` gives its text the colour that applies at the moment the text is inserted.
- The Library's Rename sheet, and every field given its text in a constructor, got that text
  before it joined the editor's look-and-feel (`setLookAndFeel` is set on the editor only). It
  kept JUCE's default white, on the ceramic background drawn later.
- Characters typed afterwards were dark, which is why only the pre-filled name was unreadable.
  The test measured 0 dark pixels for the initial name and 68 for three typed characters.
- A second defect: the EQ's typed-value field (graphite) selects everything when it opens, and
  selected text took the look-and-feel's dark highlighted-text colour, so it was dark on dark.

**One shared mechanism:** `styleTextField (field, TextSurface::light | TextSurface::dark)` in
`shared/andor-ui/Source/OspLookAndFeel`.
- It resolves every colour role from the palette: background, text, caret, selection (muted
  sage), selected text, outline, focus.
- It recolours text already inserted.
- The foreground always follows the field's own surface:
  - **light ceramic:** graphite text (`palette::text`) on `palette::raised`;
  - **graphite:** warm light text on the display colour.
- Placeholders keep their muted colour.

**Inventory.** Every editable text input in OSP and the shared interface, and what was done:

| Where | Workflow | Surface | Done |
|---|---|---|---|
| Library `PromptSheet` | Rename preset / template / sound / instrument, Duplicate, New and Rename collection, Import as… | light | styled (the reported defect) |
| Library `SaveSheet` (name, tags, description) | Save, Save As (preset or template) | light | styled |
| Library search | Search (all tabs) | light | styled |
| Library inspector (tag, notes) | Sound metadata | light | styled |
| Library Publish sheet (6 fields) | Publish as pack | light | styled |
| Library Scan sheet (include / exclude) | Scan filters | light | styled |
| Library Trim sheet, slice name | Rename a slice inline | light | styled |
| USER macro rename (OSP's controller, the shell's editor) | Inline rename | light (ivory) | styled; focus in the macro's colour kept |
| FX ROUTING library search | Search effects | light | styled; its own background kept |
| `AlertWindow` name fields: FX preset save, FX chain save, SHAPER / MOVEMENT pattern save (×2), MOVEMENT preset save | Save, Save As | light field in JUCE's dialog | styled |
| EQ value field | Direct numeric entry | dark | styled dark (selection fixed) |
| Save starting state…, Save preset…, Export / Import instrument | File names | system file chooser | macOS native dialog (not ours to style); on Linux JUCE's browser in its own default look (readable) |

Searched and not found: editable `juce::Label`s and editable slider text boxes. OSP's knobs show
values in bubbles, not text entry.

**Tests** (`[contrast]`):
- The Rename sheet: pre-filled name, selected, typed.
- The search field, the Save sheet's name, a USER macro renamed in place.
- The mechanism on both surfaces: pre-filled, selected, typed, placeholder only, disabled.

## Overlay layers

**Confirmed cause** of "the popover behind the Mixer" and "the Mixer above the Library":
- `layoutSources` raised the routing view (`toFront`) and set `shownCount = -1`, so the editor's
  timer re-ran it at every tick.
- Whatever had opened above the view (a popover, the Library, the modulation overlay) was covered
  again within a tick.
- With that line restored, the layering test fails.

**The policy** (applied when a view's state changes, never from a timer):
1. The instrument.
2. The source cards, or the routing view in the same layer: placed as it opens, beneath the lowest
   floating layer.
3. Controls and racks.
4. The Library and other foreground views: raised when they open.
5. Popovers: raised when they open.
6. The modulation overlay, with the popovers' halos: raised with each popover. It leaves out
   covered controls (`findCovers`: popovers and anything marked `modCover`, the Library).
7. Menus and dialogs: their own windows, or the editor's top.

The popovers' interaction ownership (right-click, Assign to Macro, halo drags) is the overlay's
`pressBelongsTo`: `docs/research/popover-macro-interaction.md`.

**Tests:**
- `[routing]`: popovers above the view through timer ticks and resizes at three widths (LIFE,
  DRIVE, FILTER, MOVEMENT, SPACE, ECHO, ADVANCED, PITCH, MOD), gestures inside them, the USER
  inspector.
- `[library][routing]`: the Library above the view, its presses its own, a sheet, closing back to
  the view, and the view opened while the Library is open.
- Real window events on Linux / Xvfb, as described in the popover note.
- Not performed: a DAW host (Logic, Ableton) and the macOS standalone with a real mouse.

**Dragging while the Mixer shows.** The Library's drag targets are its own SOURCES A B C wells,
inside the Library, so a sound reaches A, B or C whether the cards or the Mixer are behind it. The
Mixer stays open and its view state is kept.
