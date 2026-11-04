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
    an effect's whole editor (its controls in the open and its picture at the size of the
    modulation editors), expanded from a small key beside its macro's name. Opt-in: a
    product that asks for no modules shows no keys and no rack (ANDOR OSP: as before).

    The rack is a view: expanding or collapsing a module never changes a setting. The host
    shows one module at a time by default; it can stack several (setStacking) without
    changing how a module is built.
*/
enum class Kind
{
    movement,
    echo,
    space
};
static constexpr int kindCount = 3;
juce::String name (Kind kind);
/** The module a macro's parameter opens ("motion" MOVEMENT, "echo", "space"), if any. */
std::optional<Kind> kindForMacro (const juce::String& parameterId);

/** The small key beside a macro's name: expands (and collapses) its module. A button, so
    it takes keyboard focus and speaks to accessibility clients. */
class ExpandButton final : public juce::Button
{
public:
    explicit ExpandButton (Kind kind);
    Kind kind() const noexcept { return which; }
    void setExpanded (bool expanded);
    bool isExpanded() const noexcept { return open; }
    void setAccent (juce::Colour colour) { accent = colour; repaint(); }
    void paintButton (juce::Graphics&, bool highlighted, bool down) override;

private:
    Kind which;
    bool open = false;
    juce::Colour accent { 0xff5b7c99 };
};

/** One module's body (the host draws the panel, its title and the collapse key). */
class Module : public juce::Component
{
public:
    virtual juce::String title() const = 0;
    virtual juce::Colour accent() const = 0;
    /** The body's height under the title row, in reference pixels. */
    virtual int bodyHeight() const = 0;
    /** Components of the module's own placed in the title row, right of the title (lanes,
        presets): `row` is what is free between the title and the collapse key. */
    virtual void layoutTitleRow (juce::Rectangle<int> row) { juce::ignoreUnused (row); }
    /** The editor's timer (~30 Hz): pull what the parameters and the engine show. */
    virtual void refresh() {}
    /** Up to date at once (a snapshot without a message loop). */
    virtual void settle() {}
    /** The module's height changed (a mode with more controls): the host lays out again. */
    std::function<void()> onHeightChanged;
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

    static constexpr int titleHeight = 36, bottomMargin = 12, divider = 14;

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
    bool stacking = false;
};

} // namespace osp::plugin::rack
