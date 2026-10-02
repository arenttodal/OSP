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
    them (shaping system v1.0 §5): hover brightens it and underlines it, a click opens
    the popup, and a dot after the name says the settings differ from the defaults.
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

/** A row of mutually exclusive choices bound to a choice parameter (the active one is orange). */
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
    /** Light captions for use over the dark display. */
    void setOnDark (bool dark) { onDark = dark; repaint(); }

    void paint (juce::Graphics&) override;
    void resized() override;

    juce::Slider slider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox };

private:
    juce::String caption;
    Formatter formatter;
    bool horizontal;
    bool onDark = false;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
};

/**
    The anchored mini-panel (FX-01): a raised card that floats over the instrument, with
    a title and a few controls. Changes apply immediately; there is no Apply button.
    The component includes a margin for its shadow; only the card takes clicks.
*/
class MiniPanel : public juce::Component
{
public:
    explicit MiniPanel (juce::String title);

    static constexpr int shadowMargin = 12;
    /** Preferred size of the card (without the shadow margin). */
    virtual juce::Point<int> cardSize() const = 0;
    juce::Rectangle<int> card() const { return getLocalBounds().reduced (shadowMargin); }

    void paint (juce::Graphics&) override;
    void resized() override;
    bool hitTest (int x, int y) override { return card().contains (x, y); }

protected:
    virtual void layoutContent (juce::Rectangle<int> area) = 0;
    juce::String title;
};

/** Which macro a popup belongs to (the order of the macro row). */
enum class MacroPopup { life, dynamics, character, movement, space };

/** The parameters behind a macro's popup (the "customised" dot compares them to their defaults). */
const juce::StringArray& popupParameterIds (MacroPopup macro);
std::unique_ptr<MiniPanel> createMacroPopup (MacroPopup macro, OspAudioProcessor& processor);
/** Advanced (spec §13): velocity range, tuning, bend, output, pitch character, sustain, MPE, reseed. */
std::unique_ptr<MiniPanel> createAdvancedPopup (OspAudioProcessor& processor);

namespace format
{
    juce::String percent (double v);
    juce::String bipolar (double v);
    juce::String milliseconds (double ms);
    juce::String hertz (double hz);
}

} // namespace osp::plugin
