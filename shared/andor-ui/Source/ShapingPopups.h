#pragma once

#include "OspLookAndFeel.h"
#include "InstrumentHost.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>
#include <vector>

namespace osp::plugin
{

/**
    A macro's name above its knob. When the macro has settings, the name is the door to
    them (shaping system v1.0 §5): hover darkens it and underlines it, a click opens the
    popup. Whether the settings differ from the defaults is kept here (isCustomised) for
    the LED the editor draws under the knob.
*/
class MacroLabel final : public juce::Component, public juce::SettableTooltipClient, private juce::Timer
{
public:
    MacroLabel (juce::String text, bool opensPopup, bool twoWay = false);

    std::function<void()> onClick;
    /** A macro whose effect also has a rack (EffectRack.h), with no mark of its own on the
        row: a double-click opens or closes the rack (`rack`), a right-click offers both
        (`menu`), Enter opens the rack while the name has the keyboard's focus. A click then
        opens the popover only once a double-click can no longer follow (the system's
        double-click interval, a cancellable deferral), so a double-click never flashes the
        popover first. Without it a click acts at once, as before. */
    void setRackActions (std::function<void()> rack, std::function<void()> menu);
    /** The rack is open: the name keeps the open underline (as while its popover is open). */
    void setRackOpen (bool open);
    bool isRackOpen() const noexcept { return rackOpen; }
    /** Tests: a click waits for a possible double-click; act on it now as the timer would. */
    bool isClickPending() const noexcept { return clickPending; }
    void flushPendingClick() { timerCallback(); }
    /** The macro's identity colour (its small light and the open underline). */
    void setAccent (juce::Colour colour) { accent = colour; repaint(); }
    void setCustomised (bool customised);
    bool isCustomised() const noexcept { return customised; }
    void setOpen (bool open);
    /** The name's width as drawn (centred in the label): where a key beside it goes. */
    float textWidth() const;

    void paint (juce::Graphics&) override;
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }
    void mouseDown (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress&) override;
    void focusGained (FocusChangeType) override { repaint(); }
    void focusLost (FocusChangeType) override { repaint(); }

private:
    void timerCallback() override;
    juce::String text;
    bool opensPopup, twoWay;
    bool customised = false, open = false, rackOpen = false, clickPending = false, swallowUp = false;
    std::function<void()> onRack, onMenu;
    juce::Colour accent { 0xffe8692a };
};

/** A row of mutually exclusive choices bound to a choice parameter (the chosen one sits in, its text in the accent). */
class SegmentedControl final : public juce::Component, public juce::SettableTooltipClient
{
public:
    SegmentedControl (juce::RangedAudioParameter& parameter, juce::StringArray items);

    std::function<void (int)> onChange;
    int selected() const noexcept { return index; }
    /** A macro popover's identity: the chosen segment becomes a pale key of it, its text in it. */
    void setAccent (juce::Colour colour) { accent = colour; repaint(); }
    /** On a graphite display (an in-card editor): outlined graphite cells, the chosen one
        lit in the accent (the EQ / NOTE MAP pages' keys). Opt-in: off, exactly as before. */
    void setOnDark (bool dark) { onDark = dark; repaint(); }

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    void paintOnDark (juce::Graphics&);

    juce::StringArray items;
    bool onDark = false;
    juce::Colour accent;   ///< transparent: the instrument's coral key
    int index = 0;
    juce::ParameterAttachment attachment;
};

/** A small knob with its caption and a readable value (unit included). */
class MiniKnob final : public juce::Component
{
public:
    using Formatter = std::function<juce::String (double)>;
    MiniKnob (juce::AudioProcessorValueTreeState& state, const juce::String& parameterId, juce::String caption,
              Formatter formatter, bool horizontal = false);

    void setCaption (juce::String newCaption);
    const juce::String& captionText() const noexcept { return caption; }
    void setFormatter (Formatter newFormatter);
    /** Light captions for use over a graphite display. */
    void setOnDark (bool dark) { onDark = dark; repaint(); }
    /** Secondary controls: smaller type. */
    void setSmall (bool small) { compact = small; resized(); repaint(); }
    /** The effects rack's cells: the caption a step stronger, so it reads at plugin size
        (opt-in; the popovers' as ever). */
    void setStrongCaption (bool strong) { strongCaption = strong; repaint(); }
    /** The approved effects racks' cell (opt-in): caption 11 px in its 22 px line, a 59 px
        knob under it, the value 13 px under that, in the approved colours. */
    void setRackMetrics (bool rack);
    /** A stepped rate (opt-in): a detent mark per step around the knob, the chosen one long,
        in `accent` (the slider's range must be stepped: a choice parameter). */
    void setDetents (bool show, juce::Colour accent);
    /** A rack's secondary setting (opt-in): its caption and value a step quieter. */
    void setQuiet (bool q) { quiet = q; repaint(); }
    bool isQuiet() const noexcept { return quiet; }
    /** The popups' cell (SPACE's DECAY): caption, a ticked knob, the value in a box. */
    void setBoxed (bool shouldBeBoxed);
    /** The value arc (and a boxed knob's pointer) in this colour: its macro's identity. */
    void setArcColour (juce::Colour colour);
    /** A choice parameter walked in another order than its choices' (ARP RATE: slowest to
        fastest, while the parameter keeps its saved order). The knob's value is then the
        position in `order`, whose entries are the parameter's choice indices; the formatter
        receives the position. */
    void setChoiceOrder (juce::RangedAudioParameter& parameter, std::vector<int> order, juce::UndoManager* undo);
    /** With a choice order: the parameter's choice index the knob shows. */
    int choiceShown() const;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

    /** The caption as a door (RATE: Sync or Hz): a click on it calls this; it then shows a
        small chevron and a pointer, and brightens on hover. */
    std::function<void()> onCaptionClick;
    juce::Rectangle<float> captionArea() const;

    juce::Slider slider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox };

private:
    bool captionHover = false;
    juce::String caption;
    Formatter formatter;
    bool horizontal;
    bool onDark = false, compact = false, boxed = false, strongCaption = false, rackMetrics = false, detents = false, quiet = false;
    juce::Colour detentAccent;
    juce::Rectangle<float> valueBox() const;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    std::vector<int> choiceOrder;
    std::unique_ptr<juce::ParameterAttachment> orderedAttachment;
    bool orderedGesture = false;
};

/** A stepped choice shown as its value (PATTERN, RATE): click for the list, arrows or the
    mouse wheel to step. Bound to a choice parameter. */
class ValueSelector final : public juce::Component, public juce::SettableTooltipClient
{
public:
    ValueSelector (juce::RangedAudioParameter& parameter, juce::String caption);
    /** Text only (a popover's mode beside its title): no key, quiet type, a small chevron. */
    void setPlain (bool shouldBePlain) { plain = shouldBePlain; repaint(); }
    /** A macro popover's identity: the plain mode's text leans to it. */
    void setAccent (juce::Colour colour) { accent = colour; hasAccent = true; repaint(); }
    /** On a graphite display (an in-card editor): a graphite key, cream text. Opt-in. */
    void setOnDark (bool dark) { onDark = dark; repaint(); }
    /** The effects rack's MODE: its value a little larger and darker (opt-in; off as ever). */
    void setProminent (bool shouldBeProminent) { prominent = shouldBeProminent; repaint(); }
    /** The approved effects racks' fields (opt-in): `mode` the MODE dropdown (165 x 27, 11 px),
        `option` a cell's compact list (PATTERN, INTERVAL: 25 px, 10 px). */
    enum class RackStyle
    {
        none,
        mode,
        option
    };
    void setRackStyle (RackStyle style) { rackStyle = style; repaint(); }
    int selected() const noexcept { return index; }
    std::function<void (int)> onChange;
    /** Shown instead of the value while it returns text (SHAPER: CUSTOM); no item is ticked then. */
    std::function<juce::String()> textOverride;
    /** Appended to the list (SHAPER: CUSTOM and the saved patterns). */
    std::function<void (juce::PopupMenu&)> extraItems;
    /** Every pick from the list or the arrows, even of the current value (SHAPER leaves CUSTOM). */
    std::function<void (int)> onPick;
    void paint (juce::Graphics&) override;
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }
    void focusGained (FocusChangeType) override { repaint(); }
    void focusLost (FocusChangeType) override { repaint(); }
    void mouseUp (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    void step (int delta);
    void choose (int i);
    juce::RangedAudioParameter& parameter;
    juce::String caption;
    int index = 0;
    bool plain = false, hasAccent = false, onDark = false, prominent = false;
    RackStyle rackStyle = RackStyle::none;
    juce::Colour accent;
    juce::ParameterAttachment attachment;
};

/**
    The popup shell (macro popups and Advanced): a raised card that floats over the
    instrument with its title, a short subtitle and a close button, then the panel's own
    content (mode, visualisation, compact controls). Changes apply immediately. The
    component includes a margin for its shadow; only the card takes clicks.
*/
class MiniPanel : public juce::Component
{
public:
    MiniPanel (juce::String title, juce::String subtitle = {});

    static constexpr int shadowMargin = 44;
    /** Preferred size of the card (without the shadow margin), in the popup's own units. */
    virtual juce::Point<int> cardSize() const = 0;
    /** A macro popover: a small raised card unfolding above its macro, no close button
        (a click elsewhere, Escape or the macro's name closes it). */
    virtual bool compact() const { return false; }
    /** The popup's drawing unit: 1 draws the shell at the reference's size (the macro
        popups); smaller panels laid out at a smaller unit are scaled up by the editor
        (1 / unit) so every popup's shell and type read alike. */
    virtual float unit() const { return 1.0f; }
    juce::Rectangle<int> card() const { return getLocalBounds().reduced (shadowMargin); }
    /** Height of the title block inside the card (where the content starts). */
    int headerHeight() const noexcept { return juce::roundToInt (91.0f * unit()); }

    void paint (juce::Graphics&) override;
    void resized() override;
    bool hitTest (int x, int y) override { return card().contains (x, y); }
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override { repaint (closeButton().getSmallestIntegerContainer().expanded (2)); }

    /** Follows the processor (the editor's timer, while open). */
    virtual void refreshContent() {}
    /** Brings what it draws up to date at once, without its own timer (tests, snapshots). */
    virtual void settleNow() {}

    /** Called when the card's preferred size changes (e.g. another MOVEMENT mode). */
    std::function<void()> onSizeChanged;
    /** The close button (the owner closes the popup, asynchronously). */
    std::function<void()> onClose;

protected:
    virtual void layoutContent (juce::Rectangle<int> area) = 0;
    juce::Rectangle<float> closeButton() const;
    juce::String title, subtitle;
};

namespace format
{
    juce::String percent (double v);
    juce::String bipolar (double v);
    juce::String milliseconds (double ms);
    juce::String hertz (double hz);
}

/** Advanced (spec §13): tuning, bend, voices, pitch character, dynamics, MPE, reseed. */
std::unique_ptr<MiniPanel> createAdvancedPopup (InstrumentHost& processor);

} // namespace osp::plugin
