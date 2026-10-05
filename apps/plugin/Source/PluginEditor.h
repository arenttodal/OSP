#pragma once

#include "EngineCard.h"
#include "MainSections.h"
#include "OspLookAndFeel.h"
#include "PluginProcessor.h"
#include "ShapingPopups.h"

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace osp::plugin
{

/**
    Samples inspector (spec §48): the inferred structure of a multi-sample set, one row
    per file under its pitch group, with role and layer selectors. Changing a selector
    pins that file's place (a user assignment) and rebuilds the set.
*/
class SamplesPanel final : public juce::Component
{
public:
    explicit SamplesPanel (OspAudioProcessor& processor);
    void setInstrument (std::shared_ptr<const LoadedInstrument> instrument, int layer);
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

/** The on-screen keyboard: warm white keys, soft graphite black keys, a quiet coral for held notes. */
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

/** The empty instrument: one full-width invitation to drop a sound (no empty A/B halves). */
class DropZone final : public juce::Component
{
public:
    DropZone();
    std::function<void()> onBrowse, onExample;
    void setHighlight (bool on);
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    juce::TextButton browse { juce::String::fromUTF8 ("Browse\xe2\x80\xa6") }, example { "Load example" };
    bool highlight = false;
};

/** While a sound is dragged over the instrument (fewer than three layers): where a new layer would go. */
class AddLayerTarget final : public juce::Component
{
public:
    void setLetter (const juce::String& letter);
    void setHighlight (bool on);
    void paint (juce::Graphics&) override;

private:
    juce::String letter = "B";
    bool highlight = false;
};

/**
    The instrument (adaptive 1-3 layer redesign). From the top: header (identity, preset,
    volume, menu); the source area, which is the only part that changes with the number of
    sounds (none: a drop zone; one: a full-width card; two or three: equal cards); the mix
    band (ORIGINAL <-> REIMAGINED, plus the A/B blend or the mix triangle); the five macros
    and the amplitude envelope; the keyboard with its wheels; Advanced at the bottom right.
    A macro's name opens its popup (one at a time; Escape, a click elsewhere or its name
    again closes it).
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
    void fileDragEnter (const juce::StringArray& files, int x, int y) override;
    void fileDragMove (const juce::StringArray& files, int x, int y) override;
    void fileDragExit (const juce::StringArray&) override;
    void filesDropped (const juce::StringArray& files, int x, int y) override;

    /** Pulls the latest processor state into the UI now (normally done by a timer) and
        finishes any layout transition at once (tests, snapshots). */
    void refreshNow();

    /** Popups: 0-4 the macros (LIFE..SPACE), 5 Advanced; -1 closes. Public for tests and snapshots. */
    static constexpr int advancedPopup = 5;
    void openPopup (int which);
    void closePopup();
    int openPopupIndex() const noexcept { return popupIndex; }
    /** A mouse press anywhere on the desktop (from the global listener): a press elsewhere in
        this editor closes the open popup. Public for tests. */
    void mouseDownAnywhere (juce::Component* clicked);

    /** For tests and snapshots: how many layer cards the source area shows, and what a drop
        at (x, y) would do ("drop", "replace A", "add B", or empty). */
    int visibleCardCount() const;
    juce::String dropTargetAt (juce::Point<int> where) const;
    /** For tests: the source area's cards and drop-time layout (as during a drag). */
    void previewDrag (bool dragging, juce::Point<int> where = {});

private:
    void timerCallback() override;
    void layoutSources (bool animate);
    void updateFocus();
    void showMenu();
    void showLayerMenu (int layer, juce::Component& target);
    void addLayerSection (juce::PopupMenu& menu, int layer, bool header);
    void chooseFile (int layer, bool addAsNewLayer);
    void choosePresetFile (bool save, bool instrument);
    void positionPopup();
    void updateCustomisedDots();
    void updateStatus();

    struct DropTarget
    {
        enum class Kind { none, empty, replace, add } kind = Kind::none;
        int layer = -1;
    };
    DropTarget targetAt (juce::Point<int> where) const;
    void showDropTarget (const DropTarget& target);

    struct Knob
    {
        juce::Slider slider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox };
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

    struct HeaderMenuButton final : juce::Button
    {
        HeaderMenuButton() : juce::Button ("Menu") {}
        void paintButton (juce::Graphics&, bool highlighted, bool down) override;
    };

    OspLookAndFeel lookAndFeel;
    OspAudioProcessor& ospProcessor;

    // Header
    PresetBar presetBar { ospProcessor };
    juce::Slider volume { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox };
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> volumeAttachment;
    HeaderMenuButton menuButton;
    juce::Rectangle<int> headerArea, logoArea, volumeCaption;

    // Sources
    juce::Rectangle<int> sourceArea;
    std::array<std::unique_ptr<EngineCard>, OspAudioProcessor::numLayers> cards;
    DropZone dropZone;
    AddLayerTarget addTarget;
    SamplesPanel samplesPanel { ospProcessor };
    bool samplesShown = false;
    std::array<bool, OspAudioProcessor::numLayers> shownOccupied {};
    int shownCount = -1;
    bool dragging = false;
    DropTarget dragTarget;

    // Mix, macros, envelope
    MixSection mixSection { ospProcessor };
    juce::Rectangle<int> lowerPanel;
    std::array<Knob, 5> macros;      // LIFE, DYNAMICS, CHARACTER, MOVEMENT, SPACE
    EnvelopePanel envelope { ospProcessor.parameters };

    // Keyboard row
    Wheel pitchWheel, modWheel;
    OspKeyboard keyboard;
    juce::Label statusLabel;
    juce::TextButton advancedButton { juce::String::fromUTF8 ("Advanced  \xe2\x80\xba") };

    std::unique_ptr<juce::FileChooser> chooser;
    juce::TooltipWindow tooltips { this, 700 };

    std::unique_ptr<MiniPanel> popup;
    int popupIndex = -1;
    int closedByLabelPress = -1;     // a press on an open popup's own label closes it (and must not reopen it)
    OutsideClickWatcher outsideClicks { *this };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OspAudioProcessorEditor)
};

} // namespace osp::plugin
