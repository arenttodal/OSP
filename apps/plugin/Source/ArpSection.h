#pragma once

#include "Design.h"
#include "PluginProcessor.h"
#include "ShapingPopups.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

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
    The arpeggiator's inline editor, between the macros and the keyboard when open (the
    approved mockup): ARPEGGIATOR and a collapse button; a 16-step display, numbered, each
    step a bar as high as its note (the step sounding lit and marked, from the audio thread's
    own scheduler; while nothing is held, the pattern's shape on a C major chord); then
    PATTERN and RATE as drop-down boxes and GATE, OCTAVES and SWING as the instrument's small
    knobs. No power switch: the light by the keyboard is the only one.
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

    juce::PopupMenu patternMenu() const;
    juce::PopupMenu rateMenu() const;
    void showPatternMenu();
    void showRateMenu();
    /** Places of the parts (tests, snapshots). */
    juce::Rectangle<float> displayArea() const { return steps; }
    juce::Rectangle<float> patternArea() const { return patternBox; }
    juce::Rectangle<float> rateArea() const { return rateBox; }
    juce::Rectangle<float> collapseArea() const { return collapse; }
    /** The column lit as sounding (-1: none), as last drawn. */
    int litColumn() const noexcept { return shown.current; }

    /** The pattern as the drop-down shows it ("Up", "Up/Down", ...). */
    static juce::String patternLabel (int pattern);

private:
    void paintSteps (juce::Graphics&);
    void paintBox (juce::Graphics&, juce::Rectangle<float> box, const juce::String& text, bool hot);
    void updatePreview();

    OspAudioProcessor& ospProcessor;
    juce::ParameterAttachment enabledAttachment, patternAttachment, rateAttachment;
    MiniKnob gate, octaves, swing;
    bool enabled = false;
    int pattern = 0, rate = 1, octaveCount = 1;
    OspAudioProcessor::ArpView shown;
    Arpeggiator::Display preview;   ///< the pattern's shape on a C major chord, while nothing is held
    juce::Rectangle<float> title, collapse, numbers, steps, patternLabelArea, patternBox, rateLabelArea, rateBox;
    int hover = 0;   ///< 1 pattern, 2 rate, 3 collapse
};

} // namespace osp::plugin
