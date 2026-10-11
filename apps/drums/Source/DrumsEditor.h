#pragma once

#include "DrumsProcessor.h"
#include "OspLookAndFeel.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <memory>

namespace osp::drumsplugin
{

class DrumsEditor;

/** One pad on the grid: shows the pad, auditions it, selects it, takes dropped files. */
class PadView final : public juce::Component, public juce::FileDragAndDropTarget
{
public:
    PadView (DrumsEditor& editor, int pad);
    int pad() const noexcept { return padIndex; }

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int x, int y) override;
    void fileDragEnter (const juce::StringArray&, int, int) override { dragOver = true; repaint(); }
    void fileDragExit (const juce::StringArray&) override { dragOver = false; repaint(); }

    /** Called at the display rate: repaints only when something it shows changed. */
    void refresh();

private:
    DrumsEditor& owner;
    int padIndex;
    bool dragOver = false;
    std::uint32_t lastHits = 0;
    float flash = 0.0f;
    int lastVoices = 0;
};

/**
    ANDOR Drums' functional editor (Milestone 1): the pad grid (2x4 or 3x3), the selected
    pad's sound and controls, the kit's shared macros and the PAD / EXPANDED switch. It uses
    OSP's palette, fonts and knobs (Design.h, OspLookAndFeel); the final pad artwork waits for
    the mockup review (docs/drums/plan.md).
*/
class DrumsEditor final : public juce::AudioProcessorEditor, private juce::ChangeListener, private juce::Timer
{
public:
    explicit DrumsEditor (DrumsProcessor&);
    ~DrumsEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    DrumsProcessor& processor() noexcept { return proc; }
    void selectPad (int pad);
    static juce::Colour categoryColour (drums::DrumCategory category);
    juce::String noteText (int pad) const;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;
    void bindSelectedPad();
    void refreshSelectedInfo();
    void layoutPads();
    juce::Rectangle<int> gridArea() const;

    DrumsProcessor& proc;
    plugin::OspLookAndFeel lookAndFeel;

    juce::TextButton padMode { "PAD" }, expandedMode { "EXPANDED" };
    juce::TextButton layout8 { "2 x 4" }, layout9 { "3 x 3" };
    juce::TextButton ninthPadChip { "9" };
    std::array<std::unique_ptr<PadView>, DrumsProcessor::numPads> padViews;

    // Selected pad
    juce::Label padName, padStatus;
    juce::Slider gain, pan, tune;
    juce::Label gainLabel, panLabel, tuneLabel;
    juce::TextButton mute { "MUTE" }, solo { "SOLO" }, loadButton { "LOAD" }, clearButton { "CLEAR" };
    juce::ComboBox category, choke, voiceMode, trigger;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> gainAttachment, panAttachment, tuneAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> muteAttachment, soloAttachment;
    juce::Rectangle<int> waveformArea, padPanel, kitPanel;

    // Kit macros (shared)
    struct KitKnob
    {
        juce::Slider slider;
        juce::Label label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };
    std::array<KitKnob, 8> kitKnobs;
    std::unique_ptr<juce::FileChooser> chooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DrumsEditor)
};

} // namespace osp::drumsplugin
