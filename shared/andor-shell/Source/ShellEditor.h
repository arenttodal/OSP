#pragma once

#include "ArpSection.h"
#include "DropOwner.h"
#include "EngineCard.h"
#include "InstrumentFrame.h"
#include "EffectRack.h"
#include "KeyDnaEditor.h"
#include "MacroPopups.h"
#include "MainSections.h"
#include "ModulationPanel.h"
#include "OspLookAndFeel.h"
#include "ShellProcessor.h"
#include "UserMacroBank.h"

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
    /** A second, quieter action under the first (ProductInfo::hybridAction): its label and what
        it does. Empty: none. */
    void setSecondaryAction (const juce::String& label, std::function<void()> action);
    juce::Rectangle<int> secondaryActionBounds() const;
    void setHighlight (bool on);
    /** Three engines: the dock is the hybrid card's empty state, headed like a card (its badge
        letter and title). Empty letter: no header (the compact dock, as before). */
    void setHeader (const juce::String& letter, int shownLayer, const juce::String& title);
    /** Where a header leaves the dashed place (local coordinates). */
    juce::Rectangle<int> headerBounds() const;
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
    juce::String name { "SAMPLE" }, problem, secondaryLabel, headerLetter, headerTitle;
    int headerLayer = 0;
    std::function<void()> secondary;
    bool highlight = false, hover = false;
};

/**
    The ANDOR Instrument Shell's window: ANDOR OSP's layout and look, built from the very same
    shared components and frame (shared/andor-ui), with the product's identity in the header.
    The source cards, the macro row, the modulation panel, the keyboard and the arpeggiator
    are all there; every control whose parameter the product's engine does not read is shown
    disabled (Engine::boundParameters), and a diagnostic engine says what it is in the footer.
*/
class ShellEditor final : public juce::AudioProcessorEditor, public juce::FileDragAndDropTarget, public osp::plugin::AnchoredPopoverHost, private juce::Timer
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
    /** Tests: a modulation source dragged over the editor (content coordinates), and dropped. */
    void dragModulationForTest (int source, juce::Point<float> where) { dragModulation (source, where); }
    void dropModulationForTest (int source, juce::Point<float> where) { dropModulation (source, where); }
    juce::Component& contentForTest() noexcept { return content; }
    int visibleCardCount() const;
    osp::plugin::EngineCard& engineCard (int layer) noexcept { return *cards[static_cast<std::size_t> (juce::jlimit (0, 2, layer))]; }
    osp::plugin::ModulationPanel& modulationPanel() noexcept { return modPanel; }
    void setArpExpanded (bool open);
    void applyAuxStrip();
    /** The product's other view of the sources' area (ShellProduct::sourceAreaView): shown in
        the cards' place, or the cards again. Interface only. */
    void setSourceViewShown (bool shown);
    bool sourceViewShown() const noexcept { return sourceViewOpen; }
    juce::Component* sourceViewComponent() const noexcept { return sourceView.get(); }
    juce::Button& sourceViewToggle() noexcept { return sourceViewButton; }
    float instrumentHeight() const noexcept { return osp::plugin::frame::instrumentHeight (arpShown, rackShift(), sourceTrim()); }
    /** The source cards' trim (their modifiers in the well: nothing kept under the knobs). */
    float cardTrim() const noexcept { return shell.modifiersInWell() ? osp::plugin::EngineCard::footTrim : 0.0f; }
    /** How far everything under the cards moves up: the cards' trim, less the AUX strip's room
        while it is shown (negative: down). */
    float sourceTrim() const noexcept { return cardTrim() - auxShift(); }
    /** The product's AUX strip (ShellProduct::auxStrip): shown or hidden (a view change only;
        the window grows or shrinks by its room). */
    void setAuxStripShown (bool shown);
    bool auxStripShown() const noexcept { return auxShownApplied; }
    juce::Component* auxStripComponent() const noexcept { return auxStrip.get(); }
    /** A card's dark well (its source display) in the instrument's coordinates (the AUX strip's
        content aligns to it); empty: none shown. */
    juce::Rectangle<int> cardDisplayBounds (int layer);
    /** The room the AUX strip takes while shown: its height and the gap above it. */
    float auxShift() const noexcept { return auxShownApplied ? shell.product().auxStrip->height + auxGap : 0.0f; }
    static constexpr float auxGap = 18.0f;   ///< (as far below the cards as the macros' tabs were)
    /** The effects rack (the product's rackModules; tests and snapshots). */
    osp::plugin::rack::Host& effectsRack() noexcept { return rack; }
    /** The macro (0..5) whose name opens a rack module (a double-click), or -1. */
    int rackMacro (osp::plugin::rack::Kind kind) const noexcept;
    /** The rack module a macro (its parameter ID) opens: the product's own, or a shared one the
        product lists (rackModules), if any. */
    std::optional<osp::plugin::rack::Kind> rackKindForMacro (const juce::String& parameterId) const;
    bool hasRack (osp::plugin::rack::Kind kind) const noexcept;
    /** A macro's name (tests: its clicks, its context menu). */
    osp::plugin::MacroLabel* macroLabel (int macro) noexcept { return macro >= 0 && macro < osp::plugin::frame::macroCount ? macros[static_cast<std::size_t> (macro)].label.get() : nullptr; }
    void setRackExpanded (osp::plugin::rack::Kind kind, bool open);
    /** A double-click on the macro's name: its rack opens (another one closes) or closes, and
        a popover in the way closes. */
    void toggleRackFromLabel (osp::plugin::rack::Kind kind);
    /** A right-click on the name (or the keyboard's menu key): Quick Settings, Open / Close Rack. */
    juce::PopupMenu rackLabelMenu (int macro, osp::plugin::rack::Kind kind);
    /** The popovers: a macro's (0..5, when the product has one), Advanced, a layer's
        REIMAGINED (7 + layer, when the engine reads it), a modulation source's routes or
        larger curve (source 0 LFO 1 .. 3 ENV 2). */
    static constexpr int advancedPopup = 6, reimaginedPopup = 7, routesPopup = 11, curvePopup = 12, startControlPopup = 13, generatorHeaderPopup = 14,
                         hybridHeaderPopup = 15, arpCompanionPopup = 16, anchoredPopup = 17;
    // A product's panel anchored to a component (SHAPER DNA from its rack key): placed beside it,
    // over a transparent backdrop that takes the click which closes it (that click reaches no
    // control). Escape closes it too.
    void openAnchoredPopover (std::unique_ptr<osp::plugin::MiniPanel> panel, juce::Component& anchor) override;
    bool anchoredPopoverOpenFrom (const juce::Component& anchor) const override;
    void closeAnchoredPopover() override;
    void openPopup (int which, int source = 0);
    void closePopup();
    static constexpr bool isReimaginedPopup (int which) noexcept { return which >= reimaginedPopup && which < reimaginedPopup + osp::plugin::InstrumentHost::numLayers; }
    int openPopupIndex() const noexcept { return popupIndex; }
    osp::plugin::MiniPanel* openPopupPanel() noexcept { return popup.get(); }
    /** For tests: the arpeggiator's housing (and its second row, ProductInfo::arpCompanion). */
    osp::plugin::ArpControl& arpHousing() noexcept { return arpButton; }
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
    /** A macro's knob (tests: its look, e.g. the arc a macro switch dims), or nullptr. */
    juce::Slider* macroSlider (int macro) noexcept { return macro >= 0 && macro < osp::plugin::frame::macroCount ? &macros[static_cast<std::size_t> (macro)].slider : nullptr; }
    // USER MACROS V1.1 (opt-in: ProductInfo::userMacros). The bank shown is presentation only:
    // both banks always sound.
    int macroBankShown() const noexcept { return bankShown; }
    void showMacroBank (int bank);
    osp::plugin::MacroBankTab* macroBankTab() noexcept { return bankTab.get(); }
    osp::plugin::frame::MacroKnob* userMacroKnob (int macro) noexcept;
    osp::plugin::MacroLens* userMacroLens (int macro) noexcept;
    osp::plugin::MacroAssignOverlay& macroAssignOverlay() noexcept { return assignOverlay; }
    /** A USER macro's assignments' panel, anchored over its name (`select`: the row shown first). */
    void openUserMacroPanel (int macro, const juce::String& select = {});
    /** The panel's + Add Assignment: the next click on a control the macro may move assigns it. */
    void beginUserMacroPick (int macro);
    void cancelUserMacroAssign();
    /** Renaming in place (a double-click on the name): its field, or null. */
    void startUserMacroRename (int macro);
    juce::TextEditor* userMacroRenameField() noexcept { return renameField.get(); }
    /** Names, lenses and arcs brought up to date (an edit, undo, a preset). */
    void refreshUserMacros();
    /** Tests: the lens drag as the gesture does it (screen points), and a control's right-click menu. */
    void dragUserMacroLens (int macro, juce::Point<int> from, juce::Point<int> to, bool cancel = false);
    juce::PopupMenu controlContextMenu (const juce::String& parameterId) { return modOverlay.contextMenu (parameterId); }
    /** The halos (tests: macro ranges, handles, readouts). */
    osp::plugin::ModulationOverlay& modulationOverlay() noexcept { return modOverlay; }
    /** A USER macro chosen from a control (right-click "Assign to Macro", "Edit Macro Range") or
        by touching its knob: its controls' rings stay shown until another is chosen or a click
        lands elsewhere (-1: none). */
    void setChosenUserMacro (int macro);
    int chosenUserMacro() const noexcept { return chosenMacro; }
    /** A macro's on / off lamp (ProductInfo::macroSwitches), or nullptr. */
    osp::plugin::MacroSwitchLamp* macroSwitch (int macro) noexcept;

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
    int dragHoverCard = -1;               ///< the Granular card a source being dragged rests on
    juce::uint32 dragHoverSince = 0;
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
    /** The product's other view of the sources' area (ShellProduct::sourceAreaView) and its icon. */
    struct SourceViewButton final : juce::Button, osp::plugin::skin::KeyFace
    {
        SourceViewButton() : juce::Button ("FX Routing") {}
        void paintButton (juce::Graphics&, bool over, bool down) override;
        void buttonStateChanged() override { osp::plugin::skin::repaintKey (*this); }
        // (a header key, as SAVE: the same face, size and centre line)
        osp::plugin::skin::KeyRole keyRole() const override { return osp::plugin::skin::KeyRole::preset; }
        osp::plugin::skin::KeyState keyState() const override
        {
            return isDown() ? osp::plugin::skin::KeyState::pressed : isOver() ? osp::plugin::skin::KeyState::hover : osp::plugin::skin::KeyState::normal;
        }
    };
    SourceViewButton sourceViewButton;
    std::unique_ptr<juce::Component> sourceView;
    std::unique_ptr<juce::Component> auxStrip;   ///< (ShellProduct::auxStrip; made when first shown)
    bool auxShownApplied = false;
    bool sourceViewOpen = false;

    // Sources
    juce::Rectangle<int> sourceArea;
    std::array<std::unique_ptr<osp::plugin::EngineCard>, osp::plugin::InstrumentHost::numLayers> cards;
    osp::plugin::DropZone dropZone;
    osp::plugin::AddLayerTarget addTarget;
    SampleDock sampleDock;
    /** The hybrid recording's dock is shown (opt-in, the recording absent). */
    bool sampleDockShown() const;
    int shownCount = -1;
    bool shownHybrid = false;   ///< three engines: the hybrid's card (or its empty dock) as last laid out
    // Three engines / product card extensions: what each card was last given.
    std::array<juce::String, 3> appliedKnobs, appliedOverlayKeys;
    std::unique_ptr<osp::plugin::WellOverlay> dockOverlay;
    juce::String dockOverlayKey;
    void refreshCardExtensions();
    bool threeEngines() const;
    bool dragging = false;
    int dragLayer = -1;
    /** The drop owner (osp::plugin::DropOwner) under `editorPoint`, and the point in its own
        coordinates; null: none there (the editor's own rules). */
    osp::plugin::DropOwner* dropOwnerAt (juce::Point<int> editorPoint, juce::Point<int>& local);
    void setDropHover (osp::plugin::DropOwner* owner, const juce::StringArray& files);
    juce::Component::SafePointer<juce::Component> hoverOwner;   ///< (the owner showing a drop target)

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
    // The effects rack (opt-in: the product's rackModules), opened from the macros' names.
    osp::plugin::rack::Host rack { [this] (osp::plugin::rack::Kind kind) { return createRackModule (kind); } };
    std::unique_ptr<osp::plugin::rack::Module> createRackModule (osp::plugin::rack::Kind kind);
    std::array<std::optional<osp::plugin::rack::Kind>, osp::plugin::frame::macroCount> rackOfMacro {};
    float rackShift() const noexcept { return rack.height() > 0.0f ? rack.height() + osp::plugin::design::layout::rackGap : 0.0f; }
    int rackMask() const;
    void rackChanged();
    std::vector<std::pair<int, std::unique_ptr<osp::plugin::MacroSwitchLamp>>> macroLamps;   ///< (macro, lamp)
    // USER MACROS
    void setUpUserMacros();
    void placeUserMacros();
    void collectAssignTargets();
    void updateUserMacroArcs();
    void userMacroLabelMenu (int macro);
    juce::Colour userMacroColour (int macro) const;
    std::unique_ptr<osp::plugin::MacroBankTab> bankTab;
    osp::plugin::frame::MacroRow userRow;
    std::array<std::unique_ptr<osp::plugin::MacroLens>, 6> lenses;
    osp::plugin::MacroAssignOverlay assignOverlay;
    std::unique_ptr<juce::TextEditor> renameField;
    int bankShown = 0, renaming = -1, dragMacro = -1, pickMacro = -1, panelMacro = -1, arcsMacro = -1;
    bool anchorAbove = false;   ///< the anchored popover opens above its anchor (a USER macro's name)
    std::uint64_t userMacroRevision = 0;
    int chosenMacro = -1;
    juce::String userMacroMenuName (int macro) const;
    juce::String userMacroTitle (int macro) const;
    void addUserMacroItems (juce::PopupMenu& menu, const juce::String& parameterId);
    void placeMacroLamps();
    void dimMacroArc (int macro, bool on);
    osp::plugin::ModulationOverlay modOverlay { shell, content };
    int modSelSource = -1, modSelSlot = -1;
    int disabledControls = 0;

    std::unique_ptr<osp::plugin::MiniPanel> popup;
    std::unique_ptr<osp::plugin::KeyDnaEditor> keyDnaPanel;
    void placeKeyDna();
    /** The recording card's heading opens ProductInfo::hybridHeaderPopover while it shows the
        engine's own source. */
    void updateHybridDoor();
    bool hybridDoorSet = false;
    int popupIndex = -1, popupSource = 0;
    void presentPopup (int which);
    juce::Component::SafePointer<juce::Component> popupAnchor;   ///< anchoredPopup's anchor
    struct PopupBackdrop final : juce::Component
    {
        std::function<void()> onPress;
        void mouseDown (const juce::MouseEvent&) override
        {
            if (onPress != nullptr)
                onPress();
        }
    } popupBackdrop;
    int closedByLabelPress = -1;   ///< a press on an open popover's own door closes it (and must not reopen it)
    OutsideClickWatcher outsideClicks { *this };
    std::unique_ptr<juce::FileChooser> chooser;
    juce::TooltipWindow tooltips { this, 700 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ShellEditor)
};

} // namespace andor::shell
