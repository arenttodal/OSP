#pragma once

#include "ArpSection.h"
#include "EngineCard.h"
#include "InstrumentFrame.h"
#include "KeyDnaEditor.h"
#include "MainSections.h"
#include "ModulationPanel.h"
#include "OspLookAndFeel.h"
#include "ShellProcessor.h"

#include <juce_audio_utils/juce_audio_utils.h>

#include <array>

namespace andor::shell
{

/**
    A hybrid product's empty recording (ProductInfo::hybridSampleDock): a compact raised card
    beside the generator's, its one action ADD SAMPLE (a click browses; a file dropped
    anywhere on the instrument loads it, the dock lit while one is dragged). After a failed
    load it says so and offers another try. Not a third source: the recording's own card
    replaces it as soon as one loads.
*/
class SampleDock final : public juce::Component, public osp::plugin::skin::SurfaceFace
{
public:
    SampleDock();
    std::function<void()> onBrowse;
    void setHighlight (bool on);
    /** The name it shows (the product's: "ORGANIC") and, after a failed load, the reason. */
    void setText (const juce::String& sourceName, const juce::String& problem);
    float surfaceRadius() const override { return osp::plugin::design::layout::cardRadius; }
    bool surfaceIsCard() const override { return true; }
    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseEnter (const juce::MouseEvent&) override { setHover (true); }
    void mouseExit (const juce::MouseEvent&) override { setHover (false); }
    bool keyPressed (const juce::KeyPress&) override;
    bool isHighlighted() const noexcept { return highlight; }
    const juce::String& problemText() const noexcept { return problem; }

private:
    void setHover (bool on);
    juce::String name { "SAMPLE" }, problem;
    bool highlight = false, hover = false;
};

/**
    The ANDOR Instrument Shell's window: ANDOR OSP's layout and look, built from the very same
    shared components and frame (shared/andor-ui), with the product's identity in the header.
    The source cards, the macro row, the modulation panel, the keyboard and the arpeggiator
    are all there; every control whose parameter the product's engine does not read is shown
    disabled (Engine::boundParameters), and a diagnostic engine says what it is in the footer.
*/
class ShellEditor final : public juce::AudioProcessorEditor, public juce::FileDragAndDropTarget, private juce::Timer
{
public:
    explicit ShellEditor (ShellProcessor&);
    ~ShellEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress& key) override;

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void fileDragEnter (const juce::StringArray& files, int x, int y) override;
    void fileDragMove (const juce::StringArray& files, int x, int y) override;
    void fileDragExit (const juce::StringArray&) override;
    void filesDropped (const juce::StringArray& files, int x, int y) override;

    /** Pulls the processor's state into the UI now and finishes layout transitions (tests, snapshots). */
    void refreshNow();
    int visibleCardCount() const;
    osp::plugin::EngineCard& engineCard (int layer) noexcept { return *cards[static_cast<std::size_t> (juce::jlimit (0, 2, layer))]; }
    osp::plugin::ModulationPanel& modulationPanel() noexcept { return modPanel; }
    void setArpExpanded (bool open);
    float instrumentHeight() const noexcept { return osp::plugin::frame::instrumentHeight (arpShown); }
    /** The popovers: a macro's (0..5, when the product has one), Advanced, a layer's
        REIMAGINED (7 + layer, when the engine reads it), a modulation source's routes or
        larger curve (source 0 LFO 1 .. 3 ENV 2). */
    static constexpr int advancedPopup = 6, reimaginedPopup = 7, routesPopup = 11, curvePopup = 12, startControlPopup = 13, generatorHeaderPopup = 14;
    void openPopup (int which, int source = 0);
    void closePopup();
    static constexpr bool isReimaginedPopup (int which) noexcept { return which >= reimaginedPopup && which < reimaginedPopup + osp::plugin::InstrumentHost::numLayers; }
    int openPopupIndex() const noexcept { return popupIndex; }
    osp::plugin::MiniPanel* openPopupPanel() noexcept { return popup.get(); }
    /** For tests: a mouse press on `clicked` as the instrument's outside-click watch sees it
        (a press outside the open popover closes it; on its own door it marks the toggle). */
    void pressAnywhere (juce::Component* clicked) { mouseDownAnywhere (clicked); }
    /** The generator card's in-card editor (ProductInfo::generatorWellEditor), in the
        waveform's place: shown or not (the heading's toggle unpins when it hides it). */
    bool hasWellEditor() const;
    void showWellEditor (bool show);
    void toggleWellEditor();
    bool isWellEditorShown() const;
    /** KEY DNA (an engine that has it): ANDOR OSP's editor over the instrument, above the
        keyboard (its profile drawn over the real keys), opened by a card's KEY DNA key. */
    void openKeyDna (int layer);
    void closeKeyDna();
    osp::plugin::KeyDnaEditor* keyDnaWindow() noexcept { return keyDnaPanel.get(); }
    /** For tests: the hybrid recording's dock (ProductInfo::hybridSampleDock). */
    SampleDock& recordingDock() noexcept { return sampleDock; }
    /** For tests: the controls shown disabled because the engine does not read their parameters. */
    int disabledControlCount() const noexcept { return disabledControls; }

private:
    void timerCallback() override;
    void layoutInstrument();
    void layoutSources();
    void updateFocus();
    void updateStatus();
    void applyBindings();
    void showMenu();
    void showLayerMenu (int layer, juce::Component& target);
    void chooseFile (int layer);
    void choosePresetFile (bool save);
    void selectModulation (int source, int slot);
    void dragModulation (int source, juce::Point<float> where);
    void dropModulation (int source, juce::Point<float> where);
    void positionPopup();
    int dropLayerAt (juce::Point<int> editorPoint) const;
    void mouseDownAnywhere (juce::Component* clicked);

    struct OutsideClickWatcher final : juce::MouseListener
    {
        explicit OutsideClickWatcher (ShellEditor& e) : editor (e) {}
        void mouseDown (const juce::MouseEvent& e) override { editor.mouseDownAnywhere (e.eventComponent); }
        ShellEditor& editor;
    };

    osp::plugin::OspLookAndFeel lookAndFeel;
    ShellProcessor& shell;
    const bool modulationBound;

    osp::plugin::frame::Content content;   // before every child: it outlives them

    // Header
    osp::plugin::PresetBar presetBar { shell };
    osp::plugin::VolumeSlider volume { shell.parameters };
    osp::plugin::HeaderMenuButton menuButton;
    osp::plugin::SavePresetButton savePresetButton;
    osp::plugin::HeaderMix headerMix { shell };

    // Sources
    juce::Rectangle<int> sourceArea;
    std::array<std::unique_ptr<osp::plugin::EngineCard>, osp::plugin::InstrumentHost::numLayers> cards;
    osp::plugin::DropZone dropZone;
    osp::plugin::AddLayerTarget addTarget;
    SampleDock sampleDock;
    /** The hybrid recording's dock is shown (opt-in, the recording absent). */
    bool sampleDockShown() const;
    int shownCount = -1;
    bool dragging = false;
    int dragLayer = -1;

    // Macros, modulation, keyboard row
    osp::plugin::frame::MacroRow macros;
    osp::plugin::ModulationPanel modPanel { shell };
    osp::plugin::Wheel pitchWheel, modWheel;
    float shownWheelPosition = -1.0f;
    std::array<float, 4> shownEnvelope {};   ///< the AMP envelope's values as last drawn
    osp::plugin::OspKeyboard keyboard;
    juce::Label statusLabel;
    osp::plugin::AdvancedCardButton advancedButton { "Advanced" };
    osp::plugin::ArpControl arpButton { shell };
    osp::plugin::ArpInlinePanel arpPanel { shell };
    bool arpShown = false;
    osp::plugin::ModulationOverlay modOverlay { shell, content };
    int modSelSource = -1, modSelSlot = -1;
    int disabledControls = 0;

    std::unique_ptr<osp::plugin::MiniPanel> popup;
    std::unique_ptr<osp::plugin::KeyDnaEditor> keyDnaPanel;
    void placeKeyDna();
    int popupIndex = -1, popupSource = 0;
    int closedByLabelPress = -1;   ///< a press on an open popover's own door closes it (and must not reopen it)
    OutsideClickWatcher outsideClicks { *this };
    std::unique_ptr<juce::FileChooser> chooser;
    juce::TooltipWindow tooltips { this, 700 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ShellEditor)
};

} // namespace andor::shell
