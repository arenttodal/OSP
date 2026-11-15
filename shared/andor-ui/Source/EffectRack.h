#pragma once

#include "CeramicSkin.h"
#include "Design.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>
#include <optional>
#include <vector>

namespace osp::plugin::rack
{

/**
    The effects rack: full-width modules between the macro row and the arpeggiator, each
    an effect's whole editor (its controls in the open and its picture in the modulation
    window's column). No mark on the macro row: a double-click on the macro's name opens or
    closes its module, a right-click offers the popover or the module, Enter opens it
    (MacroLabel::setRackActions); a click still opens the popover. Opt-in: a product that
    asks for no modules has none (ANDOR OSP: as before). Future modules follow the same.

    The rack is a view: expanding or collapsing a module never changes a setting. The host
    shows one module at a time by default; it can stack several (setStacking) without
    changing how a module is built.
*/
enum class Kind
{
    movement,
    echo,
    space,
    filter,   ///< a product's own (ShellProduct::productRacks): ANDOR ANALOG's FILTER
    morph,    ///< a product's own: ANDOR ANALOG's MORPH
    fx,       ///< a product's own, opened from its view rather than a macro: ANDOR ANALOG's FX library
    aux       ///< a product's own, opened from its AUX strip (ShellProduct::auxStrip): ANDOR ANALOG's D
};
static constexpr int kindCount = 7;
juce::String name (Kind kind);
/** The module a macro's parameter opens ("motion" MOVEMENT, "echo", "space"), if any. */
std::optional<Kind> kindForMacro (const juce::String& parameterId);

/** One module's body (the host draws the panel, its title and the collapse key). */
class Module : public juce::Component
{
public:
    virtual juce::String title() const = 0;
    virtual juce::Colour accent() const = 0;
    /** The body's height under the title row, in reference pixels. */
    virtual int bodyHeight() const = 0;
    /** Components of the module's own placed in the title row, right of the title (lanes,
        presets): `row` (in the module's coordinates) is what is free between the title and
        the collapse key. The module's bounds include its title row (the host draws the title
        under it): its body starts at titleOffset(). */
    virtual void layoutTitleRow (juce::Rectangle<int> row) { juce::ignoreUnused (row); }
    int titleOffset() const noexcept { return titleTop; }
    void setTitleOffset (int top) noexcept { titleTop = top; }
    /** The editor's timer (~30 Hz): pull what the parameters and the engine show. */
    virtual void refresh() {}
    /** Up to date at once (a snapshot without a message loop). */
    virtual void settle() {}
    /** The module's height changed (a mode with more controls): the host lays out again. */
    std::function<void()> onHeightChanged;

private:
    int titleTop = 0;
};

/** The rack: the expanded modules, stacked in one ceramic panel. */
class Host final : public juce::Component, public skin::SurfaceFace
{
public:
    using Factory = std::function<std::unique_ptr<Module> (Kind)>;
    explicit Host (Factory factory);
    ~Host() override;

    float surfaceRadius() const override { return design::layout::panelRadius; }
    bool surfaceIsCard() const override { return false; }

    /** Several modules at once (stacked in the order they were expanded), or one: expanding
        a module collapses the other (the default). */
    void setStacking (bool allowMultiple) { stacking = allowMultiple; }
    /** V3 (opt-in; ANDOR ANALOG's): the module is the rack's whole face - no panel, title or
        divider of the host's around it (only its shadow), the module given the host's full
        height (the same 214 px), the collapse key a borderless X on the rail's centre line.
        Off (ANDOR OSP): as before, pixel for pixel. */
    void setFullBleed (bool bleed);
    bool fullBleed() const noexcept { return bleedFace; }
    bool surfaceShadowOnly() const override { return bleedFace; }

    void toggle (Kind kind) { setShown (kind, ! isShown (kind)); }
    void setShown (Kind kind, bool shown);
    bool isShown (Kind kind) const noexcept;
    std::vector<Kind> shownKinds() const;
    /** The shown module of `kind` (tests, snapshots), or nullptr. */
    Module* module (Kind kind) const noexcept;

    /** The rack's height in reference pixels (0 when nothing shows). */
    float height() const noexcept;

    /** A module was expanded or collapsed (or changed its height): the editor lays out and
        resizes the window. */
    std::function<void()> onLayoutChanged;

    void refresh();
    void settle();

    void paint (juce::Graphics&) override;
    void resized() override;

    /** A module is never taller than the macro panel above it (216 px): title 32 + body 172 + 10. */
    static constexpr int titleHeight = 32, bottomMargin = 10, divider = 14;

private:
    class CollapseButton;
    struct Slot
    {
        Kind kind;
        std::unique_ptr<Module> module;
        std::unique_ptr<CollapseButton> collapse;
        juce::Rectangle<int> titleArea;
    };
    void changed();

    Factory factory;
    std::vector<Slot> slots;
    bool stacking = false, bleedFace = false;
};

} // namespace osp::plugin::rack
