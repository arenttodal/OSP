#pragma once

#include "PluginProcessor.h"

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace osp::plugin
{

/** Draws the loaded sample's overview, or the drop prompt. */
class WaveformView final : public juce::Component
{
public:
    void setInstrument (std::shared_ptr<const LoadedInstrument> newInstrument);
    void setLoading (bool isLoading);
    void setDragHighlight (bool on);
    void paint (juce::Graphics&) override;

private:
    std::shared_ptr<const LoadedInstrument> instrument;
    bool loading = false;
    bool dragHighlight = false;
};

/**
    Working interface: drop zone + waveform, detected root (editable), the five macros
    and Original <-> Reimagined, a compact Advanced row, an on-screen keyboard and
    status. Functional rather than final; the designed instrument UI comes later.
*/
class OspAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                      public juce::FileDragAndDropTarget,
                                      private juce::Timer
{
public:
    explicit OspAudioProcessorEditor (OspAudioProcessor&);
    ~OspAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void fileDragEnter (const juce::StringArray&, int, int) override { waveform.setDragHighlight (true); }
    void fileDragExit (const juce::StringArray&) override { waveform.setDragHighlight (false); }
    void filesDropped (const juce::StringArray& files, int, int) override;

    /** Pulls the latest processor state into the UI now (normally done by a timer). */
    void refreshNow() { timerCallback(); }

private:
    void timerCallback() override;
    void refreshInstrumentInfo();
    void chooseFile();

    struct Knob
    {
        juce::Slider slider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
        juce::Label label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };

    OspAudioProcessor& ospProcessor;
    WaveformView waveform;
    juce::Label rootLabel, characterLabel, detailLabel, statusLabel;
    juce::ComboBox rootBox;
    juce::TextButton loadButton { "Load..." }, exampleButton { "Load example" }, reseedButton { "Reseed" };
    std::array<Knob, 6> macros;      // Life, Dynamics, Character, Motion, Space, Original/Reimagined
    std::array<Knob, 6> knobs;       // Advanced: attack, release, velocity, fine, bend, output
    juce::ComboBox pitchCharacterBox, sustainBox;
    juce::Label pitchCharacterLabel, sustainLabel, advancedLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> pitchCharacterAttachment, sustainAttachment;
    juce::MidiKeyboardComponent keyboard;
    std::unique_ptr<juce::FileChooser> chooser;

    std::uint64_t shownGeneration = 0;
    OspAudioProcessor::LoadState shownState = OspAudioProcessor::LoadState::empty;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OspAudioProcessorEditor)
};

} // namespace osp::plugin
