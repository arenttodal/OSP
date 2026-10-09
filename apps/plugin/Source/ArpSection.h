#pragma once

#include "Design.h"
#include "PluginProcessor.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace osp::plugin
{

/**
    The arpeggiator's control by the keyboard: a power light (switches arp.enabled, nothing
    else), its state as text ("ARP / UP · 1/8", subdued while it is off) and a chevron that
    shows or hides the inline editor (a view setting, nothing else). Two separate things on
    purpose: the editor can be open while the arpeggiator is off and the other way round.
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
    juce::String statusText() const { return status; }
    bool isOn() const noexcept { return enabled; }

private:
    OspAudioProcessor& ospProcessor;
    juce::ParameterAttachment enabledAttachment;
    bool enabled = false, expanded = false;
    juce::String status;
    int hover = 0;   ///< 1 the light, 2 the chevron
};

/** A small text link (ADVANCED ›): quiet until hovered or open. */
class SmallLinkButton final : public juce::Button
{
public:
    explicit SmallLinkButton (const juce::String& caption);
    void paintButton (juce::Graphics&, bool highlighted, bool down) override;
};

/**
    The arpeggiator's inline editor (between the macros and the keyboard when open): the title,
    a 16-step display of the pattern (the step sounding lit, the steps to come as they will
    play, from the audio thread's own scheduler), PATTERN (plain text: a click opens the
    menu), RATE (a selector), GATE and OCTAVES. One restrained amber; everything else is the
    instrument's own material. It has no power switch of its own: the light by the keyboard
    is the only one.
*/
class ArpInlinePanel final : public juce::Component
{
public:
    explicit ArpInlinePanel (OspAudioProcessor& processor);
    ~ArpInlinePanel() override;

    /** Pulls the scheduler's step display and the parameters (editor timer, ~30 Hz). */
    void refresh();

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;

    juce::PopupMenu patternMenu() const;
    juce::PopupMenu rateMenu() const;
    void showPatternMenu();
    void showRateMenu();
    /** Places of the parts (tests, snapshots). */
    juce::Rectangle<float> displayArea() const { return well; }
    juce::Rectangle<float> patternArea() const { return patternValue; }
    juce::Rectangle<float> rateArea() const { return rateValue; }
    juce::Rectangle<float> octaveCell (int octave) const;
    /** The column lit as sounding (-1: none), as last drawn. */
    int litColumn() const noexcept { return shown.current; }

private:
    struct GateSlider final : juce::Slider
    {
        GateSlider();
        void paint (juce::Graphics&) override;
        bool dimmed = false;
    };
    void paintDisplay (juce::Graphics&);
    void paintControls (juce::Graphics&);
    void updatePreview();

    OspAudioProcessor& ospProcessor;
    juce::ParameterAttachment enabledAttachment, patternAttachment, rateAttachment, octavesAttachment;
    GateSlider gate;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> gateAttachment;
    bool enabled = false;
    int pattern = 0, rate = 1, octaves = 1;
    OspAudioProcessor::ArpView shown;
    Arpeggiator::Display preview;   ///< the pattern's shape on a C major chord, while nothing is held
    juce::Rectangle<float> title, well, patternLabel, patternValue, rateLabel, rateValue, gateLabel, gateValue, octavesLabel, octavesRow;
    int hover = 0;   ///< 1 pattern, 2 rate, 10 + n octave n
};

} // namespace osp::plugin
