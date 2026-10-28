#pragma once

#include "ArpSection.h"
#include "EngineCard.h"
#include "InstrumentFrame.h"
#include "MainSections.h"
#include "ModulationPanel.h"
#include "OspLookAndFeel.h"
#include "ShellProcessor.h"

#include <juce_audio_utils/juce_audio_utils.h>

namespace andor::shell
{

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
    static constexpr int advancedPopup = 6, reimaginedPopup = 7, routesPopup = 11, curvePopup = 12;
    void openPopup (int which, int source = 0);
    void closePopup();
    static constexpr bool isReimaginedPopup (int which) noexcept { return which >= reimaginedPopup && which < reimaginedPopup + osp::plugin::InstrumentHost::numLayers; }
    int openPopupIndex() const noexcept { return popupIndex; }
    osp::plugin::MiniPanel* openPopupPanel() noexcept { return popup.get(); }
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
    int shownCount = -1;
    bool dragging = false;
    int dragLayer = -1;

    // Macros, modulation, keyboard row
    osp::plugin::frame::MacroRow macros;
    osp::plugin::ModulationPanel modPanel { shell };
    osp::plugin::Wheel pitchWheel, modWheel;
    float shownWheelPosition = -1.0f;
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
    int popupIndex = -1, popupSource = 0;
    int closedByLabelPress = -1;   ///< a press on an open popover's own door closes it (and must not reopen it)
    OutsideClickWatcher outsideClicks { *this };
    std::unique_ptr<juce::FileChooser> chooser;
    juce::TooltipWindow tooltips { this, 700 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ShellEditor)
};

} // namespace andor::shell
