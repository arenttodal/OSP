#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <vector>

namespace osp::plugin
{

/**
    USER MACROS V1.1's interface pieces (a product's opt-in: ProductInfo::userMacros; ANDOR
    ANALOG's). Generic: they know nothing of what a macro moves.

    MacroBankTab: CORE and USER as two small folder tabs rising from the macro panel's top-left
    edge (the owner's folder-tab mockup, 11 November; it replaces V1.1 section 3.2's flat
    micro-tab). The chosen tab is cut from the panel itself - its surface, joined without a
    line, flared into the edge, a thin muted-sage underline; the other sits behind, recessed.
    Each tab is its own target (larger than drawn), keyboard-focusable, with an accessible
    name. Choosing a bank only changes what the six knobs show.
*/
class MacroBankTab final : public juce::Component
{
public:
    // The geometry, in the instrument's units.
    static constexpr float tabWidth = 40.0f, overlap = 2.0f, rise = 14.0f, radius = 8.0f, flare = 5.0f;
    static constexpr float targetAbove = 8.0f, targetSide = 4.0f, intoPanel = 3.0f;
    /** The component's size and where it goes: its tabs start `tabsLeft` from the panel's
        left edge; it reaches `intoPanel` into the panel. */
    static constexpr float width() { return 2.0f * (targetSide + flare) + 2.0f * tabWidth - overlap; }
    static constexpr float height() { return targetAbove + rise + intoPanel; }
    static constexpr float tabsLeft = 22.0f;
    static juce::Rectangle<float> boundsFor (juce::Rectangle<float> panel)
    {
        return { panel.getX() + tabsLeft - targetSide - flare, panel.getY() - rise - targetAbove, width(), height() };
    }

    MacroBankTab();
    /** 0 CORE, 1 USER. */
    void setBank (int bank);
    int bank() const noexcept { return current; }
    std::function<void (int bank)> onChange;
    /** The panel (in the parent's coordinates), whose surface the chosen tab is cut from. */
    void setPanel (juce::Rectangle<float> panelInParent, float panelRadius);
    /** The two tabs as drawn (their targets are larger). */
    juce::Rectangle<float> drawnArea() const;
    juce::Rectangle<float> tabArea (int bank) const;
    juce::Component& half (int bank) noexcept { return bank == 0 ? core : user; }

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct Half final : public juce::Component
    {
        Half (MacroBankTab& owner, int bank, const juce::String& title);
        void mouseUp (const juce::MouseEvent&) override;
        bool keyPressed (const juce::KeyPress&) override;
        void focusGained (FocusChangeType) override { getParentComponent()->repaint(); }
        void focusLost (FocusChangeType) override { getParentComponent()->repaint(); }
        void mouseEnter (const juce::MouseEvent&) override { getParentComponent()->repaint(); }
        void mouseExit (const juce::MouseEvent&) override { getParentComponent()->repaint(); }
        MacroBankTab& owner;
        int bank;
    };
    juce::Path tabShape (int bank, bool chosen) const;
    float panelTopY() const noexcept { return targetAbove + rise; }
    void choose (int bank);
    Half core { *this, 0, "Core macros" }, user { *this, 1, "User macros" };
    int current = 0;
    juce::Rectangle<float> panel;   ///< in parent coordinates
    float panelCorner = 13.0f;
};

/**
    The USER bank's grab handle: the small lens under a macro (drawn by the frame, as on CORE)
    becomes a source to drag onto a control. Transparent: it only takes the gestures. A click
    (no drag) asks for the macro's assignments; it never turns the macro.
*/
class MacroLens final : public juce::Component, public juce::SettableTooltipClient
{
public:
    MacroLens();
    std::function<void()> onClick;
    /** The drag (screen coordinates): moving, then ended (cancelled: Escape, or released nowhere). */
    std::function<void (juce::Point<int> screen)> onDragMove;
    std::function<void (juce::Point<int> screen, bool cancelled)> onDragEnd;
    bool isDragging() const noexcept { return dragging; }
    void cancelDrag();

    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    bool dragging = false, pressed = false;
};

/**
    Over the instrument while a USER macro is being assigned or shown: a faint ring on every
    control it can move, a stronger one with the control's name on the one under the pointer
    (dragging a lens, or picking a target), a thin arc on the controls it moves (shown), and
    the pick banner ("Assign to BLOOM · Click a control · Esc"). Draws nothing and takes no
    click when idle; while picking it takes clicks only on the controls it rings and on its
    banner, so everything else - a rack's name, a card's tab - still works.
*/
class MacroAssignOverlay final : public juce::Component
{
public:
    struct Control
    {
        juce::String parameterId, name;
        juce::Rectangle<float> area;   ///< in the overlay's coordinates
    };
    enum class Mode { idle, drag, pick, show };

    void setControls (std::vector<Control> eligible) { controls = std::move (eligible); repaint(); }
    const std::vector<Control>& eligibleControls() const noexcept { return controls; }
    void beginDrag (juce::Colour colour, const juce::String& macroName);
    void beginPick (juce::Colour colour, const juce::String& macroName, juce::Rectangle<float> bannerAnchor);
    /** The mapped controls of the shown macro (thin arcs); empty: none. */
    void show (std::vector<juce::String> parameterIds, juce::Colour colour);
    void stop();
    Mode mode() const noexcept { return current; }
    /** The pointer (overlay coordinates): the control under it is the pending one. */
    void pointAt (juce::Point<float> where);
    /** The eligible control under a point, or empty. */
    juce::String controlAt (juce::Point<float> where) const;
    juce::String pendingTarget() const noexcept { return pending; }
    juce::String bannerText() const;
    juce::Rectangle<float> bannerArea() const noexcept { return banner; }
    /** A control picked (pick mode), Escape or the banner's cancel. */
    std::function<void (const juce::String& parameterId)> onPicked;
    std::function<void()> onCancel;

    void paint (juce::Graphics&) override;
    bool hitTest (int x, int y) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    std::vector<Control> controls;
    std::vector<juce::String> mapped;
    Mode current = Mode::idle;
    juce::Colour accent;
    juce::String macro, pending;
    juce::Rectangle<float> banner;
};

} // namespace osp::plugin
