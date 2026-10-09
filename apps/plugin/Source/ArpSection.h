#pragma once

#include "Design.h"
#include "PluginProcessor.h"
#include "ShapingPopups.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <vector>

namespace osp::plugin
{

/**
    The arpeggiator's card beside the keyboard (the approved mockup): a power light, ARP and a
    chevron on top, the pattern and rate ("UP · 1/8") in a small inset below. The light
    switches arp.enabled and nothing else; anywhere else on the card shows or hides the inline
    editor (a view setting) and nothing else. The editor can be open while the arpeggiator is
    off and the other way round.
*/
class ArpControl final : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit ArpControl (OspAudioProcessor& processor);

    std::function<void()> onToggleEditor;
    void setExpanded (bool open);
    /** What a click on the light does: arp.enabled on <-> off (one host gesture). */
    void toggleEnabled();
    /** Follows the parameters (pattern, rate): call from the editor's timer. */
    void refresh();

    void paint (juce::Graphics&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;

    juce::Rectangle<float> lightArea() const;
    juce::Rectangle<float> chevronArea() const;
    /** The inset's text, "UP · 1/8". */
    juce::String statusText() const { return status; }
    bool isOn() const noexcept { return enabled; }

private:
    OspAudioProcessor& ospProcessor;
    juce::ParameterAttachment enabledAttachment;
    bool enabled = false, expanded = false;
    juce::String status;
    int hover = 0;   ///< 1 the light, 2 the rest of the card
};

/** "Advanced" and "MOD ›" as raised half cards under the arpeggiator's, pressed in while
    open. The type shrinks a little to fit the half width (never below 12 px); the chevron
    goes when it does not fit. */
class AdvancedCardButton final : public juce::Button
{
public:
    explicit AdvancedCardButton (const juce::String& caption);
    /** The chevron's direction: 1 right, -1 left, 0 none. */
    void setChevron (int direction) { chevron = direction; repaint(); }
    /** A small light: the patch has active modulation (MOD), never a switch. */
    void setIndicator (bool on) { if (indicator != on) { indicator = on; repaint(); } }
    bool hasIndicator() const noexcept { return indicator; }
    void paintButton (juce::Graphics&, bool highlighted, bool down) override;

private:
    int chevron = 1;
    bool indicator = false;
};

/**
    The arpeggiator's inline editor, between the macros and the keyboard when open: ARPEGGIATOR
    and a collapse button; a 16-step display, numbered, each step a bar as high as its note (the
    step sounding lit and marked, from the audio thread's own scheduler; while nothing is held,
    the pattern on a C major chord). Then PATTERN as a small picture of the style (after Live's
    Arpeggiator: the notes of a four-note chord joined over twelve steps) with the list of every
    style beside it, RATE as a knob over the divisions from slowest to fastest, OCTAVES as four
    stacked keys, and GATE and SWING as the instrument's small knobs. No power switch: the light
    by the keyboard is the only one.
*/
class ArpInlinePanel final : public juce::Component
{
public:
    explicit ArpInlinePanel (OspAudioProcessor& processor);
    ~ArpInlinePanel() override;

    /** The collapse button (top right): hides the editor (a view setting). */
    std::function<void()> onCollapse;

    /** Pulls the scheduler's step display and the parameters (editor timer, ~30 Hz). */
    void refresh();

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    /** The styles in the list's order (musical families, not the parameter's saved order). */
    static const std::array<int, arp::patternCount>& listOrder();
    /** RATE's divisions from slowest to fastest, as parameter choice indices. */
    static const std::vector<int>& rateOrder();
    /** The pattern as the list shows it ("Up", "Pinky Up/Down", ...). */
    static juce::String patternLabel (int pattern);

    /** One host gesture each, as a click does. */
    void choosePattern (int pattern);
    void chooseOctaves (int octaves);

    /** Places of the parts (tests, snapshots). */
    juce::Rectangle<float> displayArea() const { return steps; }
    juce::Rectangle<float> patternFieldArea() const { return field; }
    juce::Rectangle<float> patternListArea() const { return list; }
    /** A style's row in the list, or empty while it is scrolled out of view. */
    juce::Rectangle<float> patternRowArea (int pattern) const;
    juce::Rectangle<float> octaveKeyArea (int octaves) const;
    juce::Rectangle<float> collapseArea() const { return collapse; }
    MiniKnob& rateKnob() noexcept { return rate; }
    int firstVisibleRow() const noexcept { return firstRow; }
    /** The style the picture shows: the one under the pointer in the list, else the chosen one. */
    int fieldPattern() const noexcept { return fieldShown; }
    /** The column lit as sounding (-1: none), as last drawn. */
    int litColumn() const noexcept { return shown.current; }

    static constexpr int visibleRows = 6;

private:
    void paintSteps (juce::Graphics&);
    void paintField (juce::Graphics&);
    void paintList (juce::Graphics&);
    void paintOctaves (juce::Graphics&);
    void updatePreview();
    void updateField();
    void scrollTo (int first);
    void revealSelected();
    int rowAt (juce::Point<float> p) const;

    OspAudioProcessor& ospProcessor;
    juce::ParameterAttachment enabledAttachment, patternAttachment, octavesAttachment;
    MiniKnob rate, gate, swing;
    bool enabled = false;
    int pattern = 0, octaveCount = 1;
    OspAudioProcessor::ArpView shown;
    Arpeggiator::Display preview;   ///< the pattern on a C major chord, while nothing is held
    Arpeggiator::Display fieldSteps; ///< the picture: the style on C E G B, one octave
    int fieldShown = -1;
    juce::Rectangle<float> title, collapse, numbers, steps, patternCaption, field, list, octaveCaption, octaveKeys;
    int firstRow = 0, hoverRow = -1;
    float wheelPending = 0.0f;
    int hover = 0;   ///< 3 collapse, 10 + n octave key n
};

} // namespace osp::plugin
