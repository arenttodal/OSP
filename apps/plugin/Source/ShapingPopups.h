#pragma once

#include "OspLookAndFeel.h"
#include "PluginProcessor.h"

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
    popup, and a dot after the name says the settings differ from the defaults.
*/
class MacroLabel final : public juce::Component, public juce::SettableTooltipClient
{
public:
    MacroLabel (juce::String text, bool opensPopup, bool twoWay = false);

    std::function<void()> onClick;
    void setCustomised (bool customised);
    void setOpen (bool open);

    void paint (juce::Graphics&) override;
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }
    void mouseUp (const juce::MouseEvent&) override;

private:
    juce::String text;
    bool opensPopup, twoWay;
    bool customised = false, open = false;
};

/** A row of mutually exclusive choices bound to a choice parameter (the chosen one sits in, its text in the accent). */
class SegmentedControl final : public juce::Component, public juce::SettableTooltipClient
{
public:
    SegmentedControl (juce::RangedAudioParameter& parameter, juce::StringArray items);

    std::function<void (int)> onChange;
    int selected() const noexcept { return index; }

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    juce::StringArray items;
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
    void setFormatter (Formatter newFormatter);
    /** Light captions for use over a graphite display. */
    void setOnDark (bool dark) { onDark = dark; repaint(); }
    /** Secondary controls: smaller type. */
    void setSmall (bool small) { compact = small; resized(); repaint(); }

    void paint (juce::Graphics&) override;
    void resized() override;

    juce::Slider slider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox };

private:
    juce::String caption;
    Formatter formatter;
    bool horizontal;
    bool onDark = false, compact = false;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
};

/** A stepped choice shown as its value (PATTERN, RATE): click for the list, arrows or the
    mouse wheel to step. Bound to a choice parameter. */
class ValueSelector final : public juce::Component
{
public:
    ValueSelector (juce::RangedAudioParameter& parameter, juce::String caption);
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

    static constexpr int shadowMargin = 14;
    /** Preferred size of the card (without the shadow margin). */
    virtual juce::Point<int> cardSize() const = 0;
    juce::Rectangle<int> card() const { return getLocalBounds().reduced (shadowMargin); }
    /** Height of the title block inside the card. */
    int headerHeight() const noexcept { return subtitle.isEmpty() ? 26 : 40; }

    void paint (juce::Graphics&) override;
    void resized() override;
    bool hitTest (int x, int y) override { return card().contains (x, y); }
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override { repaint (closeButton().getSmallestIntegerContainer().expanded (2)); }

    /** Called when the card's preferred size changes (e.g. another MOVEMENT mode). */
    std::function<void()> onSizeChanged;
    /** The close button (the owner closes the popup, asynchronously). */
    std::function<void()> onClose;

protected:
    virtual void layoutContent (juce::Rectangle<int> area) = 0;
    juce::Rectangle<float> closeButton() const;
    juce::String title, subtitle;
};

/** Which macro a popup belongs to (the order of the macro row). */
enum class MacroPopup { life, dynamics, character, movement, space };

/** The parameters behind a macro's popup (the "customised" dot compares them to their defaults). */
const juce::StringArray& popupParameterIds (MacroPopup macro);
/** The macro popups (MacroPopups.cpp): each with its own visualisation of what it does. */
std::unique_ptr<MiniPanel> createMacroPopup (MacroPopup macro, OspAudioProcessor& processor);
/** Advanced (spec §13): tuning, bend, pitch character, MPE, reseed. */
std::unique_ptr<MiniPanel> createAdvancedPopup (OspAudioProcessor& processor);

namespace format
{
    juce::String percent (double v);
    juce::String bipolar (double v);
    juce::String milliseconds (double ms);
    juce::String hertz (double hz);
}

} // namespace osp::plugin
