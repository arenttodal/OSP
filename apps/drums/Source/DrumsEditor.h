#pragma once

#include "DrumsProcessor.h"
#include "KeyMapEditor.h"
#include "OspLookAndFeel.h"
#include "PadGrid.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <memory>

namespace osp::drumsplugin
{

using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

/** A knob with its label and value, in OSP's style. */
struct LabelledKnob
{
    juce::Slider slider;
    juce::Label label;
    std::unique_ptr<SliderAttachment> attachment;
    void setup (juce::Component& parent, const juce::String& text, juce::Colour arc, bool bipolar = false);
    void attach (juce::AudioProcessorValueTreeState& state, const juce::String& id);
    void setBounds (juce::Rectangle<int> r);
};

/** The selected pad's sound: waveform, START / TUNE / GAIN / PAN / REVERSE, play modes. */
class SamplePanel final : public juce::Component
{
public:
    explicit SamplePanel (DrumsProcessor&);
    void paint (juce::Graphics&) override;
    void resized() override;
    void bind();   ///< follow the selected pad
    std::function<void (int)> onSelect;
    std::function<void()> onLoad;

private:
    juce::Rectangle<int> waveArea() const;
    DrumsProcessor& proc;
    juce::Label name;
    juce::TextButton previous { "<" }, next { ">" }, load { "LOAD" }, clear { "CLEAR" };
    LabelledKnob start, tune, gain, pan;
    juce::ToggleButton reverse { "REVERSE" };
    std::unique_ptr<ButtonAttachment> reverseAttachment;
    juce::ComboBox trigger, voice, choke, category;
    juce::Label triggerLabel, voiceLabel, chokeLabel, categoryLabel;
};

/** The selected pad's amplitude envelope (AMP), drawn from its own parameters. */
class EnvelopePanel final : public juce::Component
{
public:
    explicit EnvelopePanel (DrumsProcessor&);
    void paint (juce::Graphics&) override;
    void resized() override;
    void bind();

private:
    DrumsProcessor& proc;
    LabelledKnob attack, decay, sustain, release;
};

/** Everything below the top bar, laid out once at the base size and scaled as a whole. */
class DrumsContent final : public juce::Component, private juce::Timer, private juce::ChangeListener
{
public:
    static constexpr int baseWidth = 1280;
    static constexpr int baseHeight = 900;

    explicit DrumsContent (DrumsProcessor&);
    ~DrumsContent() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;

    void selectPad (int pad);
    void setKeyMapOpen (bool open);
    bool isKeyMapOpen() const noexcept { return keyMap.isVisible(); }
    PadGrid& padGrid() noexcept { return grid; }
    KeyMapEditor& keyMapEditor() noexcept { return keyMap; }
    void openLoadDialog (int pad);
    /** Brings the mode bar, undo state and labels in line with the processor now. */
    void refreshModeBar();

private:
    void timerCallback() override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;

    DrumsProcessor& proc;
    PadGrid grid;
    juce::TextButton padMode { "PAD" }, expandedMode { "EXPANDED" }, keyMapButton { "KEY MAP" };
    juce::TextButton layout8 { "2 x 4" }, layout9 { "3 x 3" }, ninthPad { "PAD 9" };
    juce::TextButton undoButton { "UNDO" }, redoButton { "REDO" };
    juce::Slider volume;
    std::unique_ptr<SliderAttachment> volumeAttachment;
    SamplePanel sample;
    EnvelopePanel envelope;
    std::array<LabelledKnob, 4> effects;   // DRIVE, MOVEMENT, ECHO, SPACE
    std::array<LabelledKnob, 3> macros;    // LIFE, DYNAMICS, CHARACTER
    KeyMapEditor keyMap;
    std::unique_ptr<juce::FileChooser> chooser;
    bool lastExpanded = false;
};

/**
    ANDOR Drums' editor (V1.1): pads on top (2 x 4 or 3 x 3, equal cells), PAD / EXPANDED
    and KEY MAP under them, then the selected pad's sound, its envelope, the kit's effects and
    macros. KEY MAP replaces the lower part while open (the pads stay visible and playable).
    Resizable with a fixed aspect ratio: the content is laid out once and scaled, so every
    proportion (and every pad) stays exactly equal at any size.
*/
class DrumsEditor final : public juce::AudioProcessorEditor
{
public:
    explicit DrumsEditor (DrumsProcessor&);
    ~DrumsEditor() override;
    void resized() override;
    void paint (juce::Graphics&) override;
    DrumsContent& content() noexcept { return body; }

    static constexpr double minScale = 0.7, defaultScale = 0.9, maxScale = 1.5;

private:
    plugin::OspLookAndFeel lookAndFeel;
    DrumsContent body;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DrumsEditor)
};

} // namespace osp::drumsplugin
