#pragma once

#include "ArpSection.h"
#include "Design.h"
#include "EngineCard.h"
#include "InstrumentFrame.h"
#include "KeyDnaEditor.h"
#include "KeyboardUtilityActions.h"
#include "LibraryPanel.h"
#include "MainSections.h"
#include "ModulationPanel.h"
#include "OspLookAndFeel.h"
#include "PluginProcessor.h"
#include "ShapingPopups.h"

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace andor::learn
{
class LearnController;
}

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
    /** A wheel no knob took scrolls the instrument while the window is shorter than it. */
    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override
    {
        if (! overflow.scrollBy (wheel))
            AudioProcessorEditor::mouseWheelMove (e, wheel);
    }
    /** Tests: the window shorter than the instrument - it scrolls, at the same scale. */
    frame::Overflow& instrumentOverflow() noexcept { return overflow; }

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void fileDragEnter (const juce::StringArray& files, int x, int y) override;
    void fileDragMove (const juce::StringArray& files, int x, int y) override;
    void fileDragExit (const juce::StringArray&) override;
    void filesDropped (const juce::StringArray& files, int x, int y) override;

    /** Pulls the latest processor state into the UI now (normally done by a timer) and
        finishes any layout transition at once (tests, snapshots). */
    void refreshNow();

    /** Popups: 0-4 the macros (LIFE..SPACE), 5 Advanced, 10 ECHO; -1 closes. Public for tests and snapshots. */
    static constexpr int advancedPopup = 5;
    static constexpr int mixPopup = 6;   ///< the large three-layer mix
    static constexpr int reimaginedPopup = 7;   ///< 7, 8, 9: layer A's, B's, C's REIMAGINED modes
    static constexpr int echoPopup = 10;   ///< the sixth macro (it came after the others' numbers)
    static constexpr int routesPopup = 11; ///< a modulation source's routes (the panel's route count)
    static constexpr int curvePopup = 12;  ///< the larger curve editor (CUSTOM LFO, ONE SHOT envelope)
    static constexpr int pitchWheelPopup = 13;   ///< PITCH BEND (a click on the PITCH caption)
    static constexpr int modWheelPopup = 14;     ///< MOD WHEEL's VIBRATO (a click on the MOD caption)
    static constexpr int lastPopup = modWheelPopup;
    /** The macro (index into the macro row's data order: LIFE..SPACE, ECHO) a popup belongs to, or -1. */
    static int macroOfPopup (int popup) noexcept { return popup >= 0 && popup < 5 ? popup : (popup == echoPopup ? 5 : -1); }
    static int popupOfMacro (int macro) noexcept { return macro < 5 ? macro : echoPopup; }
    void openPopup (int which);
    /** The routes or curve popover of a modulation source (0 LFO 1 .. 3 ENV 2). */
    void openModulationPopup (int which, int source);
    MiniPanel* openPopupPanel() noexcept { return popup.get(); }
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

    /** Shows or hides the arpeggiator's inline editor: the keyboard moves down by the
        editor's height and the window grows by as much (its width stays). A view setting
        only (saved with the session), never a parameter, never heard. */
    void setArpExpanded (bool open);
    bool isArpExpanded() const noexcept { return arpShown; }
    /** The instrument's height in reference pixels (taller while the arpeggiator's editor shows). */
    float instrumentHeight() const noexcept { return frame::instrumentHeight (arpShown); }
    ArpInlinePanel& arpInlinePanel() noexcept { return arpPanel; }
    ArpControl& arpControl() noexcept { return arpButton; }

    /** The envelope panel (AMP, ENV 1, ENV 2, LFO 1, LFO 2) and the halos. */
    ModulationPanel& modulationPanel() noexcept { return modPanel; }
    ModulationOverlay& modulationOverlay() noexcept { return modOverlay; }
    /** The one modulation selection (the panel's source, the chosen route; -1: none): the
        halos, the panel and the routes popover follow it. */
    void selectModulation (int source, int slot);
    int selectedModulationSource() const noexcept { return modSelSource; }
    int selectedModulationRoute() const noexcept { return modSelSlot; }
    Wheel& modulationWheel() noexcept { return modWheel; }
    Wheel& pitchBendWheel() noexcept { return pitchWheel; }
    /** The keyboard's SETTINGS key (it opens Advanced). */
    juce::Button& advancedSettingsButton() noexcept { return *utilityKeys.button (KeyboardUtilityActions::Action::settings); }
    /** The keyboard's action keys (SETTINGS, INFO). */
    KeyboardUtilityActions& keyboardActions() noexcept { return utilityKeys; }
    /** ANDOR Learn (OSP's Info and tutorials), or null where it cannot run. */
    andor::learn::LearnController* learnController() noexcept { return learn.get(); }
    /** Tests: the main menu (showMenu) is handed here instead of being shown. */
    std::function<void (juce::PopupMenu&)> showMenuForTest;
    void openMainMenu() { showMenu(); }
    HeaderMix& headerMixControl() noexcept { return headerMix; }
    EngineCard& engineCard (int layer) noexcept { return *cards[static_cast<std::size_t> (juce::jlimit (0, 2, layer))]; }
    /** A source (0..3 a tab, 4 the mod wheel) dragged to / released at a point (instrument
        coordinates): what the tabs' and the wheel's drag does (tests drive it directly). Returns the route's slot on release, or -1. */
    void dragModulation (int source, juce::Point<float> where);
    int dropModulation (int source, juce::Point<float> where);

    /** The Library window over the instrument's upper part (the keyboard stays below it and
        plays the audition). Opened from the preset menu, the main menu or Cmd/Ctrl+L. */
    void openLibrary (LibraryPanel::View view = LibraryPanel::View::presets);
    void closeLibrary();
    LibraryPanel* libraryWindow() noexcept { return libraryPanel.get(); }

    /** A layer's KEY DNA over the instrument's upper part (the Library's place), the keyboard
        live below and its keys under the profile. From the Granular strip's KEY DNA key. */
    void openKeyDna (int layer);
    void closeKeyDna();
    KeyDnaEditor* keyDnaWindow() noexcept { return keyDnaPanel.get(); }
    OspKeyboard& keyboardComponent() noexcept { return keyboard; }
    const std::array<OspKeyboard::KeyTint, 128>& keyboardTints() const noexcept { return keyTints; }

private:
    void timerCallback() override;
    /** Places everything in reference coordinates (once: the whole layout scales). */
    void layoutInstrument();
    void paintInstrument (juce::Graphics&);
    /** Editor coordinates (drag and drop, tests) to the instrument's reference coordinates. */
    juce::Point<int> toInstrument (juce::Point<int> editorPoint) const;
    void layoutSources (bool animate);
    void updateFocus();
    void showMenu();
    void showLayerMenu (int layer, juce::Component& target);
    void addLayerSection (juce::PopupMenu& menu, int layer, bool header);
    void chooseFile (int layer, bool addAsNewLayer);
    void choosePresetFile (bool save, bool instrument);
    void saveStartingState();
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

    /** Closes the open popup when the mouse goes down anywhere outside it. */
    struct OutsideClickWatcher final : juce::MouseListener
    {
        explicit OutsideClickWatcher (OspAudioProcessorEditor& e) : editor (e) {}
        void mouseDown (const juce::MouseEvent& e) override;
        OspAudioProcessorEditor& editor;
    };

    OspLookAndFeel lookAndFeel;
    OspAudioProcessor& ospProcessor;

    /** The instrument at the reference size, scaled to the window as one piece. Declared
        before every child so it outlives them. */
    frame::Content content;
    /** A window shorter than the instrument: a vertical scroll bar, never a smaller instrument. */
    frame::Overflow overflow { *this };

    // Header
    PresetBar presetBar { ospProcessor };
    VolumeSlider volume { ospProcessor.parameters };
    HeaderMenuButton menuButton;
    SavePresetButton savePresetButton;
    juce::Rectangle<int> headerArea, logoArea, volumeCaption;

    // Sources
    juce::Rectangle<int> sourceArea;
    std::array<std::unique_ptr<EngineCard>, OspAudioProcessor::numLayers> cards;
    DropZone dropZone;
    AddLayerTarget addTarget;
    SamplesPanel samplesPanel { ospProcessor };
    bool samplesShown = false;
    std::array<bool, OspAudioProcessor::numLayers> shownOccupied {};
    std::array<float, 6> shownValues {};   ///< mix x/y and ADSR last drawn
    int shownCount = -1;
    bool dragging = false;
    DropTarget dragTarget;

    // Mix, macros, envelope
    HeaderMix headerMix { ospProcessor };   ///< MIX in the header (two or three layers)
    juce::Rectangle<int> lowerPanel;
    frame::MacroRow macros;      // LIFE, DYNAMICS, CHARACTER, MOVEMENT, SPACE, ECHO (ECHO sits before SPACE on screen)
    ModulationPanel modPanel { ospProcessor };   ///< the AMP envelope's place, with the modulation editors

    // Keyboard row
    Wheel pitchWheel, modWheel;
    float shownWheelPosition = -1.0f;   ///< the processor's wheel position as last shown
    OspKeyboard keyboard;
    /** The edited layer's note map on the keys (a Note Atlas layer; none otherwise). */
    std::array<OspKeyboard::KeyTint, 128> keyTints {};
    std::shared_ptr<const atlas::NoteAtlas> tintedAtlas;
    const InstrumentSet* tintedSet = nullptr;
    bool tintedLoop = true;
    int tintedLayer = -1;
    void updateKeyTints();
    juce::Label statusLabel;
    /** SETTINGS (Advanced) and INFO (ANDOR Learn) beneath ARP: OSP has no AUX D, so no D. */
    KeyboardUtilityActions utilityKeys { { KeyboardUtilityActions::Action::settings, KeyboardUtilityActions::Action::info } };
    void utilityKeyClicked (KeyboardUtilityActions::Action);
    void refreshUtilityKeys();
    std::unique_ptr<andor::learn::LearnController> learn;
    void createLearn();
    void learnAction (const juce::String& actionId, const juce::String& targetId, const juce::String& parameterId = {});
    ArpControl arpButton { ospProcessor };
    ArpInlinePanel arpPanel { ospProcessor };
    bool arpShown = false;
    ModulationOverlay modOverlay { ospProcessor, content };
    int modSelSource = -1, modSelSlot = -1;
    int popupSource = 0;                 ///< the modulation popovers' source
    int hoverMacro = -1;                 ///< a dragged source resting on a macro (opens its popover)
    juce::uint32 hoverSince = 0;
    void applyWindowShape (bool resizeWindow);

    std::unique_ptr<juce::FileChooser> chooser;
    juce::TooltipWindow tooltips { this, 700 };

    std::unique_ptr<MiniPanel> popup;
    std::unique_ptr<LibraryPanel> libraryPanel;
    std::unique_ptr<KeyDnaEditor> keyDnaPanel;
    void placeLibrary();
    int popupIndex = -1;
    int closedByLabelPress = -1;     // a press on an open popup's own label closes it (and must not reopen it)
    OutsideClickWatcher outsideClicks { *this };
    frame::ScreenFit screenFit { *this, [this] { return instrumentHeight(); } };   ///< (first shown: fitted to its screen)

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OspAudioProcessorEditor)
};

} // namespace osp::plugin
