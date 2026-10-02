#pragma once

#include "OspLookAndFeel.h"
#include "PluginProcessor.h"
#include "ShapingPopups.h"

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace osp::plugin
{

/** Draws the loaded sample's overview in the dark display (time grid, note info), or the drop prompt. */
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
    Samples inspector (spec §48): the inferred structure of a multi-sample set, one row
    per file under its pitch group, with role and layer selectors. Changing a selector
    pins that file's place (a user assignment) and rebuilds the set.
*/
class SamplesPanel final : public juce::Component
{
public:
    explicit SamplesPanel (OspAudioProcessor& processor);
    void setInstrument (std::shared_ptr<const LoadedInstrument> instrument);
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct Row
    {
        juce::Label name, info;
        juce::ComboBox role, layer, root;
        bool isHeader = false;
    };
    OspAudioProcessor& ospProcessor;
    juce::Viewport viewport;
    juce::Component content;
    std::vector<std::unique_ptr<Row>> rows;
    std::uint64_t shownGeneration = 0;
};

/** The on-screen keyboard in the instrument's finish: ivory and ebony keys, labelled octaves. */
class OspKeyboard final : public juce::MidiKeyboardComponent
{
public:
    explicit OspKeyboard (juce::MidiKeyboardState& keyState) : MidiKeyboardComponent (keyState, horizontalKeyboard) {}

private:
    void drawWhiteNote (int note, juce::Graphics&, juce::Rectangle<float> area, bool isDown, bool isOver,
                        juce::Colour lineColour, juce::Colour textColour) override;
    void drawBlackNote (int note, juce::Graphics&, juce::Rectangle<float> area, bool isDown, bool isOver,
                        juce::Colour noteFillColour) override;
    juce::String getWhiteNoteText (int note) override;
};

/** The menu button: three short rules instead of a glyph. */
class MenuButton final : public juce::TextButton
{
public:
    void paintButton (juce::Graphics&, bool highlighted, bool down) override;
};

/**
    The instrument (UI redesign): a warm housing with the pitch, character and file
    details on top, the dark waveform display, the five macros and Original <->
    Reimagined, Advanced, the keyboard and a slim status row. A macro's name opens its
    popup (one at a time; Escape or a click elsewhere closes it).
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
    bool keyPressed (const juce::KeyPress& key) override;

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void fileDragEnter (const juce::StringArray&, int, int) override { waveform.setDragHighlight (true); }
    void fileDragExit (const juce::StringArray&) override { waveform.setDragHighlight (false); }
    void filesDropped (const juce::StringArray& files, int, int) override;

    /** Pulls the latest processor state into the UI now (normally done by a timer). */
    void refreshNow() { timerCallback(); }

    /** Popups: 0-4 the macros (LIFE..SPACE), 5 Advanced; -1 closes. Public for tests and snapshots. */
    static constexpr int advancedPopup = 5;
    void openPopup (int which);
    void closePopup();
    int openPopupIndex() const noexcept { return popupIndex; }

private:
    void timerCallback() override;
    void refreshInstrumentInfo();
    void chooseFile();
    void showMenu();
    void presetOpened();
    void choosePresetFile (bool save, bool instrument);
    void positionPopup();
    void updateCustomisedDots();

    struct Knob
    {
        juce::Slider slider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
        std::unique_ptr<MacroLabel> label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };

    /** Closes the open popup when the mouse goes down anywhere outside it. */
    struct OutsideClickWatcher final : juce::MouseListener
    {
        explicit OutsideClickWatcher (OspAudioProcessorEditor& e) : editor (e) {}
        void mouseDown (const juce::MouseEvent& e) override;
        OspAudioProcessorEditor& editor;
    };

    OspLookAndFeel lookAndFeel;
    OspAudioProcessor& ospProcessor;
    WaveformView waveform;
    juce::Label rootLabel, characterLabel, detailLabel, statusLabel;
    juce::ComboBox rootBox;
    juce::TextButton loadButton { "Load..." }, exampleButton { "Load example" }, samplesButton { "Samples" };
    SamplesPanel samplesPanel { ospProcessor };
    MenuButton menuButton;
    juce::ComboBox stateBox;
    std::array<Knob, 6> macros;      // Life, Dynamics, Character, Movement, Space, Original/Reimagined
    juce::TextButton advancedButton { "ADVANCED" };
    OspKeyboard keyboard;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::TooltipWindow tooltips { this, 700 };

    std::unique_ptr<MiniPanel> popup;
    int popupIndex = -1;
    int closedByLabelPress = -1;     // a press on an open popup's own label closes it (and must not reopen it)
    OutsideClickWatcher outsideClicks { *this };

    std::uint64_t shownGeneration = 0;
    OspAudioProcessor::LoadState shownState = OspAudioProcessor::LoadState::empty;
    juce::Image texture;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OspAudioProcessorEditor)
};

} // namespace osp::plugin
