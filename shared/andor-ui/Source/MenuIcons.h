#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>

namespace osp::plugin::menuicons
{

/**
    The menus' monochrome action icons (docs/research/menu-visual): line drawings at about 15 px,
    one stroke weight, in the item's own ink (graphite, warm rust for a destructive action,
    subdued when disabled). The source cards' modifier icons' convention (icons::draw: drawn
    paths, not glyphs). Only actions get one; presets and categories stay plain text.
*/
enum class Kind
{
    blank,        ///< no picture, but the icon's column kept (labels stay aligned in an icon menu)
    newDocument,
    save,
    saveAs,
    open,
    library,
    record,
    add,
    mapNotes,
    startingState,
    package,
    favourite,
    recent,
    settings,
    tutorials,
    remove,
    previous,
    init,
    reset,
    list,
    exportOut,
    importIn,
    info,
    rename,
    replace,
    undo,
    redo,
    reveal,
    waveform,
    play,
    trim
};

/** Draws `kind` in `area` (square) in `colour`. */
void draw (juce::Graphics&, Kind, juce::Rectangle<float> area, juce::Colour colour);

/** A menu item's picture: the look recognises it and draws the glyph in the item's ink (any
    other Drawable is drawn as itself). */
class Glyph final : public juce::Drawable
{
public:
    explicit Glyph (Kind k) : kind (k) { setBounds (0, 0, 16, 16); }
    Kind glyphKind() const noexcept { return kind; }
    std::unique_ptr<juce::Drawable> createCopy() const override { return std::make_unique<Glyph> (kind); }
    juce::Rectangle<float> getDrawableBounds() const override { return { 0.0f, 0.0f, 16.0f, 16.0f }; }
    juce::Path getOutlineAsPath() const override { return {}; }
    void paint (juce::Graphics& g) override { menuicons::draw (g, kind, getDrawableBounds(), juce::Colours::black); }

private:
    Kind kind;
};

/** For PopupMenu::Item::setImage / addItem (..., image). */
inline std::unique_ptr<juce::Drawable> make (Kind kind) { return std::make_unique<Glyph> (kind); }

/** Adds an action with its icon (and a destructive action in warm rust). */
void addItem (juce::PopupMenu&, Kind, const juce::String& text, bool enabled, std::function<void()> action, bool destructive = false,
              const juce::String& shortcut = {});

/** An item with its icon (for setTicked, setEnabled ... before adding it). */
juce::PopupMenu::Item item (Kind, const juce::String& text, std::function<void()> action, bool enabled = true, bool ticked = false);

/** A submenu with its icon (Kind::blank keeps the column in an icon menu). */
void addSubMenu (juce::PopupMenu&, Kind, const juce::String& text, juce::PopupMenu sub, bool enabled = true);

/** The identity at the top of a source's menu: its letter on a chip in the source's colour,
    the source's name and, quieter beneath, its file (or "Empty"). Not an action: never
    highlighted, never chosen. */
void addSourceHeader (juce::PopupMenu&, const juce::String& title, const juce::String& letter, juce::Colour chip, const juce::String& name,
                      const juce::String& detail);

/** Gives icons to a menu's items by their text (an ellipsis or not; a trailing '*' matches the
    start, "Load into*"), in this menu only (not its submenus): for menus built elsewhere (ANDOR
    Learn's items) or by many hands. Items not named keep the column blank; Kind::remove items
    take the destructive ink. */
void decorate (juce::PopupMenu&, std::initializer_list<std::pair<const char*, Kind>> icons);

/** The warm rust of destructive actions (remove, clear). */
juce::Colour destructiveInk();

} // namespace osp::plugin::menuicons
