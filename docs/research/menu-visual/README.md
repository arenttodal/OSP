# Shared menu visual refinement

Branch `claude/menu-visual` (on `claude/osp-menu-content`). One look for every menu in ANDOR OSP
and the shell products (ANDOR ANALOG and the sister instruments), built in `shared/andor-ui`.

## What was wrong (the "before" shots)

No menu set a look of its own, so JUCE opened each one as a separate desktop window that took
**JUCE's default look** (LookAndFeel_V4, dark slate), not the editor's. OspLookAndFeel's menu
drawing only ever ran for the few menus shown inside a component. In a DAW and in the
standalone app, every menu looked like `before-*.png`.

## What changed

**Every menu takes the look.** `SharedMenuLook` (OspLookAndFeel.h): each editor (OSP's and
the shell's) holds one; one shared OspLookAndFeel is JUCE's default look from the first open
editor to the last. That covers the menus, alert windows and tooltip windows of this plug-in
binary only (each binary has its own JUCE). The last editor to close dismisses open menus,
then restores JUCE's default.

**The ceramic card** (OspLookAndFeel):

| | |
|---|---|
| surface | `#F7F0E6`, 8 px corners, a faint warm rim |
| shadow | contact (offset 1, blur 3, 15 %) plus ambient (offset 3, blur 6, 10 %), drawn in an 8 px margin of a translucent window |
| rows | 28 px; separators 11 px; section labels in quiet small capitals, 28 px |
| hover | sage `#DDE3D4`, rounded (5 px), inset 4 px |
| icons | monochrome line drawings at 15 px (`menuicons`), in the row's ink; warm rust `#A4553A` for destructive actions (remove, clear) |
| ticks | in the left gutter of a plain menu; at the right in a menu with icons |

Where the platform cannot composite windows (`Desktop::canUseSemiTransparentWindows()` false),
the window falls back to the same card, square and opaque, with the system's drop shadow.

**Geometry.**
- JUCE's rows span the whole window (its border is vertical only), so the look insets each
  row by the shadow margin (`drawPopupMenuItemWithOptions`). Custom items do the same
  through `OspLookAndFeel::popupSideInset`.
- Menus that open on their own are moved in `preparePopupMenuWindow` so the card's left edge
  is the anchor's and its top sits 3 px under it.
- JUCE opens a submenu at its parent window's edge, which would leave the two cards two
  shadow margins apart. Each submenu is moved to overlap its parent by 3 px instead. It keeps
  JUCE's vertical placement, so its first row is level with the row that opened it.
  - The edge match allows 3 px of slack, because a scaled menu's edges are rounded in its own
    units. An exact match would miss at 125–200 % and in scaled host windows.
- OSP's main menu still right-aligns to its key, now measured on the card.
- JUCE scales a menu window as it scales the menu's anchor, so all of this holds at every
  interface size. The geometry test checks 100 %, 150 % and 200 %.

**Content.**
- **Global menu (OSP and shell):** every action has its icon. Undo, Redo and the Open Library
  shortcuts are unchanged. ANDOR Learn's items are given icons by `menuicons::decorate`, so
  `shared/andor-learn` is untouched.
- **Source menus:** an identity header replaces the "SOURCE A · file" section label. It shows
  the letter on a chip in the source's colour, the source's name and, beneath, its file. The
  shell's layer menus get the same header. It is a custom item: never highlighted, never
  chosen, and its accessible title is the old label.
- **Preset dropdown:**
  - icons for Open Library, Previous patch (renamed from "Back to (the) previous patch"),
    INIT, Reset settings, Starting States, Favorites, Recently Used, All Presets and Packs;
  - categories keep the icon column, so their labels stay aligned;
  - inside a category's flyout, a preset's leading `<Category> - ` (or its singular,
    `Pad - ` under Pads) is not shown. This is display only: names, files and the Library
    are unchanged.
- **Library row menu, USER macro menus (OSP and shell), FX routing gear, the modulation
  VEL/NOTE menu:** icons for their actions via `menuicons::decorate`. Remove and Trash are in
  the destructive ink.

Behaviour, item order, shortcuts, enabled states and layering are unchanged. The one tested
structural change: a source menu's first entry is now the identity item (a custom item)
instead of a section header. The menu architecture test reads it as the header it replaces.

## Screenshots

`OSP_MENU_VISUAL_SHOTS=<dir> OSP_MENU_PREFIX=before|after osp_plugin_tests "[menu-visual-shots]"`
under Xvfb. Each menu is opened by its own code path, as a host shows it (a desktop window),
and drawn over the editor where it stands. A bare X server composites nothing, so the test
forces the translucent card (`setTranslucentMenusForTests`).

| | before | after |
|---|---|---|
| 1 global menu (a row hovered) | before-01-global-menu.png | after-01-global-menu.png |
| 2 source A | before-02-source-a-menu.png | after-02-source-a-menu.png |
| 3 source B | before-03-source-b-menu.png | after-03-source-b-menu.png |
| 4 preset dropdown | before-04-preset-menu.png | after-04-preset-menu.png |
| 5 a category's flyout | before-05-category-submenu.png | after-05-category-submenu.png |
| 6 USER macro menu | before-06-user-macro-menu.png | after-06-user-macro-menu.png |
| 7 Library row menu | before-07-library-menu.png | after-07-library-menu.png |
| 8 FX routing gear | before-08-fx-routing-menu.png | after-08-fx-routing-menu.png |

## Tests

- `menus: the ANDOR look is every menu's while an editor is open, and only then`: the shared
  default look's lifetime across two editors.
- `menus: the card sits on its anchor and a submenu opens edge to edge, at every UI scale`
  (needs a display; Xvfb is enough). At 100 %, 150 % and 200 % it checks:
  - the window really takes the look and is translucent;
  - the card is aligned with its anchor;
  - a real submenu, opened from its row through JUCE's own submenu path (Down to the row,
    then Right, as the keyboard does it), overlaps its parent by 3 px with its first row level
    with the row that opened it (within JUCE's rounding of scaled units).
