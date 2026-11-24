#include "PluginEditor.h"
#include "SourceOctaveRow.h"
#include "MenuIcons.h"
#include "UserMacroEditor.h"
#include "MacroPopups.h"
#include "WheelPopups.h"
#include "ActionEvents.h"
#include "LearnController.h"

#include "core/PitchMath.h"
#include "io/AudioFileIO.h"

#if JucePlugin_Build_Standalone
 #include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>
#endif

#include <map>
#include "core/PathText.h"

namespace osp::plugin
{

namespace
{
    const juce::String dot = juce::String::fromUTF8 ("  \xc2\xb7  ");

    bool isAudioFile (const juce::File& file)
    {
        return io::isSupportedAudioExtension (osp::pathFromUtf8 (file.getFullPathName().toStdString()));
    }
}

//==============================================================================
SamplesPanel::SamplesPanel (OspAudioProcessor& p) : ospProcessor (p)
{
    viewport.setViewedComponent (&content, false);
    viewport.setScrollBarsShown (true, false);
    addAndMakeVisible (viewport);
}

void SamplesPanel::paint (juce::Graphics& g)
{
    g.setColour (palette::display);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 7.0f);
    g.setColour (juce::Colours::black.withAlpha (0.55f));
    g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), 7.0f, 1.0f);
}

void SamplesPanel::setInstrument (std::shared_ptr<const LoadedInstrument> instrument, int)
{
    if (instrument == nullptr || instrument->set == nullptr)
    {
        rows.clear();
        content.removeAllChildren();
        shownGeneration = 0;
        return;
    }
    if (instrument->generation == shownGeneration)
        return;
    shownGeneration = instrument->generation;
    rows.clear();
    content.removeAllChildren();
    const auto& set = *instrument->set;
    for (const auto& group : set.groups)
    {
        auto header = std::make_unique<Row>();
        header->isHeader = true;
        header->name.setText (juce::String (midiNoteName (static_cast<int> (std::lround (group.rootMidi))))
                                  + (group.layers > 1 ? "   " + juce::String (group.layers) + " velocity layers" : juce::String()),
                              juce::dontSendNotification);
        header->name.setFont (fonts::make (14.0f, fonts::Weight::medium));
        header->name.setColour (juce::Label::textColourId, palette::housingLight);
        content.addAndMakeVisible (header->name);
        rows.push_back (std::move (header));
        for (int id : group.members)
        {
            const auto& member = set.members[static_cast<std::size_t> (id)];
            auto row = std::make_unique<Row>();
            row->name.setText (juce::String::fromUTF8 (member.filename.c_str()), juce::dontSendNotification);
            row->name.setColour (juce::Label::textColourId, palette::housingLight);
            row->info.setText (member.userAssigned ? juce::String ("set by you")
                                                   : "auto " + juce::String (static_cast<int> (std::lround (member.confidence * 100))) + "%",
                               juce::dontSendNotification);
            row->info.setColour (juce::Label::textColourId, palette::displayText);
            row->role.addItemList ({ "Pitch", "Velocity layer", "Round robin", "Articulation" }, 1);
            row->role.setSelectedId (static_cast<int> (member.role) + 1, juce::dontSendNotification);
            for (int l = 1; l <= 4; ++l)
                row->layer.addItem ("Layer " + juce::String (l), l);
            row->layer.setSelectedId (std::clamp (member.layer + 1, 1, 4), juce::dontSendNotification);
            // Root: the detected pitch, or a correction (octave errors on unusual sources).
            row->root.addItem ("root auto", 1);
            for (int note = 24; note <= 108; ++note)
                row->root.addItem ("root " + juce::String (midiNoteName (note)), note + 2);
            int pinnedRoot = 1;
            for (const auto& a : instrument->assignments)
                if (a.filename == member.filename && a.rootMidi)
                    pinnedRoot = static_cast<int> (std::lround (*a.rootMidi)) + 2;
            row->root.setSelectedId (pinnedRoot, juce::dontSendNotification);
            const auto filename = member.filename;
            auto* rolePtr = &row->role;
            auto* layerPtr = &row->layer;
            auto* rootPtr = &row->root;
            auto apply = [this, filename, rolePtr, layerPtr, rootPtr] {
                std::optional<double> root;
                if (rootPtr->getSelectedId() >= 2)
                    root = static_cast<double> (rootPtr->getSelectedId() - 2);
                ospProcessor.reassignSample (filename, static_cast<SampleRole> (rolePtr->getSelectedId() - 1), layerPtr->getSelectedId() - 1, root);
            };
            // (A combo box's change: only the musician's choice; the rows are filled silently.)
            row->role.onChange = [this, apply] { apply(); if (onUserEdit != nullptr) onUserEdit ("role"); };
            row->layer.onChange = [this, apply] { apply(); if (onUserEdit != nullptr) onUserEdit ("layer"); };
            row->root.onChange = [this, apply] { apply(); if (onUserEdit != nullptr) onUserEdit ("root"); };
            for (juce::Component* c : { static_cast<juce::Component*> (&row->name), static_cast<juce::Component*> (&row->info),
                                        static_cast<juce::Component*> (&row->role), static_cast<juce::Component*> (&row->layer),
                                        static_cast<juce::Component*> (&row->root) })
                content.addAndMakeVisible (c);
            rows.push_back (std::move (row));
        }
    }
    resized();
}

void SamplesPanel::resized()
{
    viewport.setBounds (getLocalBounds().reduced (8));
    const int width = viewport.getWidth() - viewport.getScrollBarThickness();
    int y = 0;
    for (auto& row : rows)
    {
        if (row->isHeader)
        {
            row->name.setBounds (0, y + 6, width, 22);
            y += 30;
            continue;
        }
        auto line = juce::Rectangle<int> (0, y, width, 26);
        row->layer.setBounds (line.removeFromRight (90));
        line.removeFromRight (6);
        row->root.setBounds (line.removeFromRight (100));
        line.removeFromRight (6);
        row->role.setBounds (line.removeFromRight (130));
        line.removeFromRight (6);
        row->info.setBounds (line.removeFromRight (90));
        row->name.setBounds (line.withTrimmedLeft (12));
        y += 30;
    }
    content.setSize (width, std::max (y, 10));
}

//==============================================================================
void OspAudioProcessorEditor::OutsideClickWatcher::mouseDown (const juce::MouseEvent& e)
{
    editor.mouseDownAnywhere (e.eventComponent);
}

void OspAudioProcessorEditor::mouseDownAnywhere (juce::Component* c)
{
    if (userMacroController != nullptr && (c == this || isParentOf (c)))
        userMacroController->mouseDownAnywhere (c);   // (a click elsewhere: the halos back to their quiet arcs)
    if (popup == nullptr)
    {
        closedByLabelPress = -1;
        return;
    }
    if (c == nullptr || c == popup.get() || popup->isParentOf (c))
        return;
    // A USER macro's lens (dragged onto a control) or the picking overlay: the popover stays, so
    // its own knobs (SUB's LEVEL...) can be assigned.
    if (userMacroController != nullptr && userMacroController->keepsPopoverOpen (c))
        return;
    // The shared modulation overlay lies above everything, popovers included: a right-click on a
    // popover's knob (its menu: Assign to Macro, modulation) or a press on its halo's range is
    // taken by the overlay, so the pressed component is the overlay - but the press is the
    // popover's own. It stays open; a press the overlay took for a control elsewhere is an
    // outside click as ever.
    if ((c == &modOverlay || modOverlay.isParentOf (c)) && modOverlay.pressBelongsTo (*popup))
        return;
    // Only clicks inside this editor close the popup. The global listener also hears clicks in
    // other JUCE windows - above all the popup's own menus (PATTERN, RATE): closing the popup
    // there would delete the control whose menu item is being chosen, before its choice arrives.
    if (c != this && ! isParentOf (c))
        return;
    if (popupIndex == anchoredPopup && popupAnchor != nullptr && (c == popupAnchor.getComponent() || popupAnchor->isParentOf (c)))
        return;   // (its anchor: it opened the panel in this very press - JUCE tells this watcher after the
                  // key - or closes it itself; closing here too shut SATELLITE's panel as it opened)
    if (c == &arpButton && popupIndex == subPopup
        && arpButton.secondRowArea().contains (arpButton.getLocalPoint (nullptr, juce::Desktop::getMousePosition().toFloat())))
        return;   // (SUB's own row: it opened the panel, or switches SUB with it open)
    int pressed = -1;
    for (int i = 0; i < static_cast<int> (macros.size()); ++i)
        if (c == macros[static_cast<std::size_t> (i)].label.get())
            pressed = popupOfMacro (i);
    if (c == &advancedSettingsButton())
        pressed = advancedPopup;
    if (headerMix.isMixOpener (c))
        pressed = mixPopup;
    for (int l = 0; l < OspAudioProcessor::numLayers; ++l)
        if (cards[static_cast<std::size_t> (l)]->isReimaginedLabel (c))
        {
            const auto& card = *cards[static_cast<std::size_t> (l)];
            const auto at = card.getLocalPoint (nullptr, juce::Desktop::getMousePosition());
            if (card.reimaginedLabelBounds().contains (at))
                pressed = reimaginedPopup + l;
        }
    for (auto [wheel, which] : { std::pair<Wheel*, int> { &pitchWheel, pitchWheelPopup }, { &modWheel, modWheelPopup } })
        if (c == wheel && wheel->captionArea().contains (wheel->getLocalPoint (nullptr, juce::Desktop::getMousePosition().toFloat())))
            pressed = which;
    closedByLabelPress = pressed == popupIndex ? pressed : -1;
    closePopup();
}

OspAudioProcessorEditor::OspAudioProcessorEditor (OspAudioProcessor& p)
    : AudioProcessorEditor (p),
      ospProcessor (p),
      pitchWheel ("PITCH", true, [&p] (float v) { p.setScreenPitchWheel (v); }),
      modWheel ("MOD", false, [&p] (float v) { p.setScreenModWheel (v); }),
      keyboard (p.keyboardState)
{
    setLookAndFeel (&lookAndFeel);

   #if JucePlugin_Build_Standalone
    // The standalone app mutes its input against feedback; OSP never plays its input back
    // (live capture only keeps it), so the input is let through.
    if (p.wrapperType == juce::AudioProcessor::wrapperType_Standalone)
        if (auto* holder = juce::StandalonePluginHolder::getInstance())
            holder->getMuteInputValue().setValue (false);
   #endif

    // Header: the preset, the master volume and the menu (utilities live there).
    presetBar.onChange = [this] { updateStatus(); timerCallback(); };
    presetBar.onSaveStartingState = [this] { saveStartingState(); };
    presetBar.onOpenLibrary = [this] { openLibrary(); };
    presetBar.onBrowseLibrary = [this] (const juce::String& scope, bool startingStates) {
        // Browse all ...: the Library on that collection (Keys selects Keys).
        openLibrary (startingStates ? LibraryPanel::View::templates : LibraryPanel::View::presets);
        if (libraryPanel != nullptr)
            libraryPanel->selectScope (scope);
    };
    content.addAndMakeVisible (presetBar);
    // SAVE: a click saves (in place when it can); a right-click is Save As.
    savePresetButton.onSaveAs = [this] { choosePresetFile (true, false); };
    savePresetButton.onClick = [this] {
        if (savePresetButton.isSecondaryClick())
            savePresetButton.onSaveAs();
        else
            saveInstrument();
    };
    savePresetButton.setTooltip ("Save (right-click: Save as" + juce::String::fromUTF8 ("\xe2\x80\xa6") + ")");
    content.addAndMakeVisible (savePresetButton);
    content.addAndMakeVisible (volume);
    menuButton.setTooltip ("Instrument, sources, content, preferences, support");
    menuButton.onClick = [this] { showMenu(); };
    content.addAndMakeVisible (menuButton);
    setWantsKeyboardFocus (true);

    // Sources: one card per layer slot (only occupied ones show), the drop zone, the add target.
    for (int layer = 0; layer < OspAudioProcessor::numLayers; ++layer)
    {
        auto& card = cards[static_cast<std::size_t> (layer)];
        card = std::make_unique<EngineCard> (ospProcessor, layer);
        card->onFocus = [this] (int l) {
            if (ospProcessor.editLayer() != l)
            {
                ospProcessor.setEditLayer (l);
                updateFocus();
            }
        };
        card->onMenu = [this] (int l, juce::Component& target) { showLayerMenu (l, target); };
        card->onKeyDna = [this] (int l) { openKeyDna (l); };
        // One EQ editor open at a time (opening B's closes A's editor; A's EQ keeps playing).
        card->onEqToggle = [this] (int l, bool open) {
            for (auto& c : cards)
            {
                if (open)
                    c->setNotesOpen (false);   // one page over the displays at a time
                c->setEqOpen (open && c->layer() == l);
            }
            modOverlay.toFront (false);
        };
        // NOTE MAP (a Note Atlas layer): one open at a time, as the EQ; it edits that layer.
        card->onNotesToggle = [this] (int l, bool open) {
            for (auto& c : cards)
            {
                if (open)
                    c->setEqOpen (false);
                c->setNotesOpen (open && c->layer() == l);
            }
            if (open && ospProcessor.editLayer() != l)
            {
                ospProcessor.setEditLayer (l);
                updateFocus();
            }
            modOverlay.toFront (false);
        };
        card->onReimagined = [this] (int l) {
            if (closedByLabelPress == reimaginedPopup + l)
            {
                closedByLabelPress = -1;   // that press closed it
                return;
            }
            openPopup (reimaginedPopup + l);
        };
        // The modifiers as a compact row of keys in the display's top-left (ANDOR ANALOG's).
        card->setModifiersInWell (true);
        card->setFootTrimmed (false);
        content.addChildComponent (*card);
    }
    dropZone.onBrowse = [this] { chooseFile (0, true); };
    dropZone.setRecordShown (ospProcessor.liveCapture());
    dropZone.onRecord = [this] { openNewLayerCapture(); };
    dropZone.onExample = [this] {
        ospProcessor.resetLayerControls (0);
        ospProcessor.loadExample (0);
    };
    content.addChildComponent (dropZone);
    content.addChildComponent (addTarget);
    content.addChildComponent (samplesPanel);

    headerMix.onOpenMix = [this] {
        if (! design::showMixTriangle)
            return;   // the triangle is hidden for now (design::showMixTriangle)
        if (closedByLabelPress == mixPopup)
        {
            closedByLabelPress = -1;   // that press closed it
            return;
        }
        openPopup (mixPopup);
    };
    headerMix.onModulationMenu = [this] {
        modOverlay.contextMenu (modui::mixTargetId).showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&headerMix));
    };
    content.addChildComponent (headerMix);

    // The six macros: a name with settings behind it (click: its popup) over its knob.
    const auto macroSpecs = frame::ospMacroSpecs();   // (FILTER: the `character` parameter)
    for (std::size_t i = 0; i < macros.size(); ++i)
    {
        auto& knob = macros[i];
        frame::setUpMacro (knob, static_cast<int> (i), macroSpecs[i], ospProcessor.parameters, content);
        const int index = popupOfMacro (static_cast<int> (i));
        knob.label->onClick = [this, index] {
            if (closedByLabelPress == index)
            {
                closedByLabelPress = -1;
                return;
            }
            openPopup (index);
        };
        // DRIVE, CHARACTER, MOVEMENT, ECHO and SPACE (as ANDOR ANALOG's macros): no mark on the
        // row - the name opens the full rack on a double-click, a right-click offers the popover
        // or the rack, Enter opens it. A single click still opens the popover.
        if (const auto kind = macroRackKind (macroSpecs[i].parameterId); kind.has_value() && knob.label != nullptr)
        {
            rackOfMacro[i] = *kind;
            const auto k = *kind;
            juce::Component::SafePointer<OspAudioProcessorEditor> safe (this);
            knob.label->setRackActions ([this, k] { toggleRackFromLabel (k); }, [safe, index, k, i] {
                if (safe == nullptr)
                    return;
                juce::PopupMenu menu;
                menu.addItem ("Quick Settings", [safe, index] {
                    if (safe != nullptr)
                    {
                        safe->closedByLabelPress = -1;
                        safe->openPopup (index);
                    }
                });
                menu.addItem (safe->isMacroRackOpen (k) ? "Close Rack" : "Open Rack", [safe, k] {
                    if (safe != nullptr)
                        safe->toggleRackFromLabel (k);
                });
                menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (safe->macros[i].label.get()));
            });
        }
    }
    content.addAndMakeVisible (modPanel);

    content.addAndMakeVisible (pitchWheel);
    content.addAndMakeVisible (modWheel);
    // The wheels' captions are doors to their settings (as a macro's name): PITCH to the bend's
    // RANGE, MOD to its VIBRATO. A press that just closed the popover does not reopen it.
    pitchWheel.setCaptionAccent (juce::Colour (0xffcd5a1e));
    modWheel.setCaptionAccent (modui::sourceColour (4));
    for (auto [wheel, which] : { std::pair<Wheel*, int> { &pitchWheel, pitchWheelPopup }, { &modWheel, modWheelPopup } })
        wheel->onCaptionClick = [this, which] {
            if (closedByLabelPress == which)
            {
                closedByLabelPress = -1;
                return;
            }
            if (popupIndex == which)
                closePopup();
            else
                openPopup (which);
        };
    frame::setUpKeyboard (keyboard);
    keyboard.tintOf = [this] (int note) { return keyTints[static_cast<std::size_t> (std::clamp (note, 0, 127))]; };
    content.addAndMakeVisible (keyboard);

    shownStatusMessage = ospProcessor.statusMessage();   // (an older message is not news to a new editor)
    // SETTINGS and INFO beneath ARP (the large Advanced key's place).
    utilityKeys.onClick = [this] (KeyboardUtilityActions::Action a) { utilityKeyClicked (a); };
    content.addAndMakeVisible (utilityKeys);
    arpButton.onToggleEditor = [this] { setArpExpanded (! arpShown); };
    // SUB: the housing's second row (its light switches SUB, the row opens its panel above).
    arpButton.setSecondRow ("SUB", OspAudioProcessor::subFieldId ("enabled"));
    arpButton.onSecondRow = [this] {
        if (popupIndex == subPopup)
            closePopup();
        else
            openPopup (subPopup);
    };
    arpPanel.onCollapse = [this] { setArpExpanded (false); };
    arpPanel.onOctavesMenu = [this] {
        modOverlay.contextMenu ("arp.octaves").showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&arpPanel));
    };
    content.addAndMakeVisible (arpButton);
    content.addChildComponent (arpPanel);
    arpShown = ospProcessor.arpEditorExpanded();
    arpButton.setExpanded (arpShown);
    arpPanel.setVisible (arpShown);

    // MODULATION: the envelope panel's tabs (AMP, ENV 1, ENV 2, LFO 1, LFO 2) hold the
    // sources' editors; a source's tab dragged onto a control makes a route; the halos on the
    // modulated controls show and edit them. (The MOD button that brought the panel forward is
    // gone: the tabs are always there.) The mod wheel is a source too: its MOD caption is a
    // drag socket.
    modWheel.setAssignable (modui::sourceColour (4));
    modWheel.onSourceDrag = [this] (juce::Point<int> screen) { dragModulation (4, content.getLocalPoint (nullptr, screen).toFloat()); };
    modWheel.onSourceDrop = [this] (juce::Point<int> screen) { dropModulation (4, content.getLocalPoint (nullptr, screen).toFloat()); };
    modPanel.onDragMove = [this] (int source, juce::Point<int> screen) { dragModulation (source, content.getLocalPoint (nullptr, screen).toFloat()); };
    modPanel.onDragEnd = [this] (int source, juce::Point<int> screen) { dropModulation (source, content.getLocalPoint (nullptr, screen).toFloat()); };
    modPanel.onShowRoutes = [this] (int source) { openModulationPopup (routesPopup, source); };
    modPanel.onExpandCurve = [this] (int source) { openModulationPopup (curvePopup, source); };
    modPanel.onTabChanged = [this] (int tab) {
        ospProcessor.setModPanelTab (tab);
        const int source = modui::sourceOfTab (tab);
        // The panel's source is the selection's source (its route stays chosen if it is that source's).
        int slot = -1;
        for (const auto& r : ospProcessor.modulationRoutes())
            if (r.slot == modSelSlot && mod::sourceIndex (r.route.source) == source)
                slot = modSelSlot;
        selectModulation (source, slot);
    };
    modOverlay.onSelect = [this] (int source, int slot) { selectModulation (source, slot); };
    modPanel.ampEnvelope().livePosition = [this] (const juce::String& id) { return modOverlay.livePosition (id); };
    modOverlay.onShowRoutes = [this] (int source) { openModulationPopup (routesPopup, source); };
    modPanel.showTab (ospProcessor.modPanelTab());
    content.addAndMakeVisible (modOverlay);   // last: above everything (popups bring it back up)

    // USER MACROS: six assignable macros in CORE's places, behind the CORE / USER tabs.
    {
        UserMacroController::Editor host;
        host.host = &ospProcessor;
        host.parameters = &ospProcessor.parameters;
        host.content = &content;
        host.coreRow = &macros;
        host.modOverlay = &modOverlay;
        juce::Component::SafePointer<OspAudioProcessorEditor> safe (this);
        host.closePopup = [safe] { if (safe != nullptr) safe->closePopup(); };
        host.openPanel = [safe] (std::unique_ptr<MiniPanel> panel, juce::Component& anchor) {
            if (safe == nullptr)
                return false;
            safe->openAnchoredPopover (std::move (panel), anchor);
            if (safe->popupIndex != anchoredPopup)
                return false;
            safe->anchorAbove = true;   // (over the macro's name, as CORE's popovers)
            safe->positionPopup();
            return true;
        };
        host.learnAction = [safe] (const juce::String& action, const juce::String& target, const juce::String& parameterId) {
            if (safe != nullptr)
                safe->learnAction (action, target, parameterId);
        };
        host.savedBank = [safe] { return safe != nullptr ? safe->ospProcessor.macroBank() : 0; };
        host.saveBank = [safe] (int bank) { if (safe != nullptr) safe->ospProcessor.setMacroBank (bank); };
        host.macroPanelArea = [safe] { return safe != nullptr ? design::layout::macroPanel.translated (0.0f, -safe->sourceTrim()) : juce::Rectangle<float>(); };
        host.isShowing = [safe] { return safe != nullptr && safe->isShowing(); };
        auto descriptor = usermacros::descriptor();
        auto makePanel = descriptor.panel;
        descriptor.panel = [safe, makePanel] (InstrumentHost& h, int macro, const juce::String& select) -> std::unique_ptr<MiniPanel> {
            auto panel = makePanel (h, macro, select);
            if (auto* p = dynamic_cast<UserMacroPanel*> (panel.get()))
                p->onAddAssignment = [safe] (int m) { if (safe != nullptr && safe->userMacroController != nullptr) safe->userMacroController->beginPick (m); };
            return panel;
        };
        userMacroController = std::make_unique<UserMacroController> (std::move (host), std::move (descriptor));
        userMacroController->setUp();
        userMacroController->place();
    }

    juce::Desktop::getInstance().addGlobalMouseListener (&outsideClicks);

    // The instrument is laid out once at the reference size (design::width x height) and
    // scaled as a whole: proportions never drift between sizes.
    content.onPaint = [this] (juce::Graphics& g) { paintInstrument (g); };
    // The FX library's rack: hidden until the routing view opens an effect.
    content.addChildComponent (racks, content.getIndexOfChildComponent (&modOverlay));   // (under the overlay: the halos of its knobs show)
    racks.setFullBleed (true);
    racks.onLayoutChanged = [this] { rackChanged(); };
    // FX ROUTING: its key beside SAVE (as ANDOR ANALOG's), its view in the cards' place.
    routingKey.setTooltip ("FX Routing");
    routingKey.setTitle ("FX Routing");
    routingKey.setClickingTogglesState (false);
    routingKey.onClick = [this] { setSourceViewShown (! routingOpen); };
    content.addAndMakeVisible (routingKey);
    addAndMakeVisible (content);
    overflow.onScroll = [this] { resized(); };
    layoutInstrument();
    setResizable (true, true);
    applyWindowShape (false);
    // The default size (1086 wide), not a scale measured before the window had any size.
    constexpr float defaultScale = 1086.0f / design::width;
    setSize (juce::roundToInt (defaultScale * design::width), juce::roundToInt (defaultScale * instrumentHeight()));
    setScaleFactor (ospProcessor.uiScale());

    if (ospProcessor.advancedOpen())
        openPopup (advancedPopup);
    createLearn();   // (nothing is shown until the musician asks)
    timerCallback();
    startTimerHz (30);   // grains and read heads move at the display rate
}

OspAudioProcessorEditor::~OspAudioProcessorEditor()
{
    stopTimer();
    learnGestures.reset();
    learn.reset();   // (before the components it points at)
    juce::Desktop::getInstance().removeGlobalMouseListener (&outsideClicks);
    for (auto& card : cards)
        juce::Desktop::getInstance().getAnimator().cancelAnimation (card.get(), false);
    popup.reset();
    libraryPanel.reset();
    keyDnaPanel.reset();
    setLookAndFeel (nullptr);
}

void OspAudioProcessorEditor::openLibrary (LibraryPanel::View view)
{
    closePopup();
    closeKeyDna();
    if (libraryPanel == nullptr)
    {
        libraryPanel = std::make_unique<LibraryPanel> (ospProcessor);
        juce::Component::SafePointer<OspAudioProcessorEditor> safe (this);
        // Closed after the click or key that asked for it has finished with the window.
        libraryPanel->onClose = [safe] { juce::MessageManager::callAsync ([safe] { if (safe != nullptr) safe->closeLibrary(); }); };
        libraryPanel->onPatchChanged = [safe] {
            if (safe != nullptr)
            {
                safe->presetBar.refresh();
                safe->updateStatus();
                safe->timerCallback();
            }
        };
        libraryPanel->getProperties().set ("modCover", true);   // the halos stay under it
        content.addAndMakeVisible (*libraryPanel);
    }
    placeLibrary();
    libraryPanel->setView (view);
    libraryPanel->toFront (true);
}

void OspAudioProcessorEditor::closeLibrary()
{
    if (libraryPanel == nullptr)
        return;
    libraryPanel.reset();
    presetBar.refresh();
    grabKeyboardFocus();
}

void OspAudioProcessorEditor::openKeyDna (int layer)
{
    closePopup();
    closeLibrary();
    if (keyDnaPanel != nullptr && keyDnaPanel->layer() != layer)
        keyDnaPanel.reset();
    if (keyDnaPanel == nullptr)
    {
        keyDnaPanel = std::make_unique<KeyDnaEditor> (ospProcessor, layer);
        juce::Component::SafePointer<OspAudioProcessorEditor> safe (this);
        keyDnaPanel->onClose = [safe] {
            juce::MessageManager::callAsync ([safe] {
                if (safe != nullptr)
                    safe->closeKeyDna();
            });
        };
        // The profile sits over the real keys below it.
        keyDnaPanel->keyBounds = [safe] (int note) -> juce::Rectangle<float> {
            if (safe == nullptr || safe->keyDnaPanel == nullptr)
                return {};
            auto& kb = safe->keyboard;
            if (note < kb.getRangeStart() || note > kb.getRangeEnd())
                return {};
            const auto r = kb.getRectangleForKey (note);
            return r.translated (static_cast<float> (kb.getX() - safe->keyDnaPanel->getX()), static_cast<float> (kb.getY() - safe->keyDnaPanel->getY()));
        };
        keyDnaPanel->isKeyDown = [safe] (int note) { return safe != nullptr && safe->ospProcessor.keyboardState.isNoteOnForChannels (0xffff, note); };
        keyDnaPanel->getProperties().set ("modCover", true);   // the halos stay under it
        content.addAndMakeVisible (*keyDnaPanel);
    }
    if (ospProcessor.editLayer() != layer)
    {
        ospProcessor.setEditLayer (layer);
        updateFocus();
    }
    placeLibrary();
    keyDnaPanel->toFront (true);
}

void OspAudioProcessorEditor::closeKeyDna()
{
    if (keyDnaPanel == nullptr)
        return;
    keyDnaPanel.reset();
    grabKeyboardFocus();
}

void OspAudioProcessorEditor::placeLibrary()
{
    if (keyDnaPanel != nullptr)
    {
        const float top = design::layout::keyboard.getY() + (arpShown ? design::layout::arpShift : 0.0f);
        keyDnaPanel->setBounds (juce::Rectangle<float> (31.0f, 23.0f, 1386.0f, top - 9.0f - 23.0f).getSmallestIntegerContainer());
    }
    if (libraryPanel == nullptr)
        return;
    // Over everything from the housing's top to just above the keyboard row, which stays
    // visible and playable (it auditions while the tray's Keys switch is on).
    const float keyboardTop = design::layout::keyboard.getY() + (arpShown ? design::layout::arpShift : 0.0f);
    libraryPanel->setBounds (juce::Rectangle<float> (31.0f, 23.0f, 1386.0f, keyboardTop - 9.0f - 23.0f).getSmallestIntegerContainer());
}

//==============================================================================
// Popups

void OspAudioProcessorEditor::openPopup (int which)
{
    const bool keepAdvanced = which == advancedPopup;
    closePopup();
    if (which == subPopup)
    {
        popup = ospProcessor.createSubPanel();
        presentPopup (subPopup);
        arpButton.setSecondRowOpen (true);
        return;
    }
    const bool wheelPopup = which == pitchWheelPopup || which == modWheelPopup;
    if (which < 0 || (which > lastPopup && ! wheelPopup) || (which >= reimaginedPopup + OspAudioProcessor::numLayers && which < echoPopup))
        return;
    const bool reimagined = which >= reimaginedPopup && which < reimaginedPopup + OspAudioProcessor::numLayers;
    if (reimagined && ! cards[static_cast<std::size_t> (which - reimaginedPopup)]->isVisible())
        return;
    if (reimagined)
        popup = createReimaginedPopup (ospProcessor, which - reimaginedPopup);
    else if (which == mixPopup)
        popup = createMixPopup (ospProcessor);
    else if (which == advancedPopup)
        popup = createAdvancedPopup (ospProcessor);
    else if (which == routesPopup)
    {
        auto routes = std::make_unique<ModRoutesPopup> (ospProcessor, popupSource);
        routes->onSelectRoute = [this] (int slot) { selectModulation (popupSource, slot); };
        routes->setSelectedSlot (modSelSlot);
        popup = std::move (routes);
    }
    else if (which == curvePopup)
        popup = std::make_unique<ModCurvePopup> (ospProcessor, popupSource);
    else if (which == pitchWheelPopup)
        popup = createPitchBendPopup (ospProcessor);
    else if (which == modWheelPopup)
        popup = createModWheelPopup (ospProcessor, defaultVibratoSources(), [this] { return ospProcessor.modWheelPosition(); });
    else
        popup = createMacroPopup (static_cast<MacroPopup> (macroOfPopup (which)), ospProcessor);
    presentPopup (which);
    if (const int m = macroOfPopup (which); m >= 0)
        macros[static_cast<std::size_t> (m)].label->setOpen (true);
    if (reimagined)
        cards[static_cast<std::size_t> (which - reimaginedPopup)]->setReimaginedOpen (true);
    if (which == pitchWheelPopup)
        pitchWheel.setCaptionOpen (true);
    if (which == modWheelPopup)
        modWheel.setCaptionOpen (true);
    if (keepAdvanced)
    {
        ospProcessor.setAdvancedOpen (true);
        utilityKeys.setActive (KeyboardUtilityActions::Action::settings, true);
    }
}

void OspAudioProcessorEditor::presentPopup (int which)
{
    popupIndex = which;
    popup->onSizeChanged = [this] { positionPopup(); };
    juce::Component::SafePointer<OspAudioProcessorEditor> safe (this);
    // The close button lives on the popup: close it after the click has been handled.
    popup->onClose = [safe] { juce::MessageManager::callAsync ([safe] { if (safe != nullptr) safe->closePopup(); }); };
    content.addAndMakeVisible (*popup);
    popup->toFront (false);
    modOverlay.toFront (false);
    positionPopup();
    if (userMacroController != nullptr)
        userMacroController->popupOpened();   // (mid-assignment: its knobs become targets)
    frame::animatePopoverOpening (*popup);
    if (isShowing())
        grabKeyboardFocus(); // Escape closes it
}

void OspAudioProcessorEditor::closePopup()
{
    if (popup == nullptr)
        return;
    if (const int m = macroOfPopup (popupIndex); m >= 0)
        macros[static_cast<std::size_t> (m)].label->setOpen (false);
    if (popupIndex >= reimaginedPopup && popupIndex < reimaginedPopup + OspAudioProcessor::numLayers)
        cards[static_cast<std::size_t> (popupIndex - reimaginedPopup)]->setReimaginedOpen (false);
    if (popupIndex == pitchWheelPopup)
        pitchWheel.setCaptionOpen (false);
    if (popupIndex == modWheelPopup)
        modWheel.setCaptionOpen (false);
    if (popupIndex == advancedPopup)
    {
        ospProcessor.setAdvancedOpen (false);
        utilityKeys.setActive (KeyboardUtilityActions::Action::settings, false);
    }
    if (popupIndex == subPopup)
        arpButton.setSecondRowOpen (false);
    const bool anchored = popupIndex == anchoredPopup;
    if (anchored)
    {
        content.removeChildComponent (&popupBackdrop);
        popupAnchor = nullptr;
        anchorAbove = false;
    }
    juce::Desktop::getInstance().getAnimator().cancelAnimation (popup.get(), false);
    popup.reset();
    popupIndex = -1;
    if (anchored && userMacroController != nullptr)
        userMacroController->popupClosed();   // (a USER macro's panel: its name no longer open)
}

void OspAudioProcessorEditor::requestClosePopup()
{
    if (popup != nullptr && ! popup->dismissRequested())
        return;
    closePopup();
}

void OspAudioProcessorEditor::openAnchoredPopover (std::unique_ptr<MiniPanel> panel, juce::Component& anchor)
{
    closePopup();
    if (panel == nullptr || ! isParentOf (&anchor))
        return;
    popup = std::move (panel);
    popupAnchor = &anchor;
    // The backdrop: the click that closes the panel goes nowhere else.
    juce::Component::SafePointer<OspAudioProcessorEditor> safe (this);
    popupBackdrop.onPress = [safe] { juce::MessageManager::callAsync ([safe] { if (safe != nullptr) safe->requestClosePopup(); }); };
    popupBackdrop.setBounds (content.getLocalBounds());
    content.addAndMakeVisible (popupBackdrop);
    presentPopup (anchoredPopup);
}

bool OspAudioProcessorEditor::anchoredPopoverOpenFrom (const juce::Component& anchor) const
{
    return popup != nullptr && popupIndex == anchoredPopup && popupAnchor.getComponent() == &anchor;
}

void OspAudioProcessorEditor::closeAnchoredPopover()
{
    if (popupIndex == anchoredPopup)
        requestClosePopup();   // (its anchor again: as the musician asked)
}

void OspAudioProcessorEditor::positionPopup()
{
    if (popup == nullptr)
        return;
    juce::Desktop::getInstance().getAnimator().cancelAnimation (popup.get(), false);
    popup->setAlpha (1.0f);
    const auto size = frame::popoverSize (*popup);
    float x = 0.0f, y = 0.0f;
    if (const int macro = macroOfPopup (popupIndex); macro >= 0)
    {
        const auto at = frame::macroPopoverOrigin (macros[static_cast<std::size_t> (macro)].label->getBounds().toFloat(), size);
        x = at.x;
        y = at.y;
    }
    else if (popupIndex >= reimaginedPopup && popupIndex < reimaginedPopup + OspAudioProcessor::numLayers)
    {
        // A layer's REIMAGINED popover unfolds above that card's REIMAGINED name.
        const auto& card = *cards[static_cast<std::size_t> (popupIndex - reimaginedPopup)];
        const auto anchor = content.getLocalArea (&card, card.reimaginedLabelBounds()).toFloat();
        x = anchor.getCentreX() - 0.5f * size.x;
        y = anchor.getY() - 10.0f - size.y;
    }
    else if (popupIndex == routesPopup || popupIndex == curvePopup)
    {
        const auto at = frame::modulationPopoverOrigin (modPanel.getBounds().toFloat(), size);
        x = at.x;
        y = at.y;
    }
    else if (popupIndex == anchoredPopup && popupAnchor != nullptr && anchorAbove)
    {
        // A USER macro's assignments unfold directly above its name, as CORE's popovers do.
        const auto at = frame::macroPopoverOrigin (content.getLocalArea (popupAnchor.getComponent(), popupAnchor->getLocalBounds()).toFloat(), size);
        x = at.x;
        y = at.y;
    }
    else if (popupIndex == anchoredPopup && popupAnchor != nullptr)
    {
        // Under its anchor, right edges aligned; above it where there is no room below (the
        // arpeggiator's title row sits low in the window). placePopover keeps it inside.
        const auto anchor = content.getLocalArea (popupAnchor.getComponent(), popupAnchor->getLocalBounds()).toFloat();
        x = anchor.getRight() + 14.0f - size.x;
        y = anchor.getBottom() + 8.0f;
        const float visibleBottom = content.getLocalArea (this, getLocalBounds()).toFloat().getBottom();
        if (y + size.y > visibleBottom - 8.0f)
            y = anchor.getY() - 8.0f - size.y;
    }
    else if (popupIndex == subPopup)
    {
        // Above the arpeggiator's housing, its left edge with the housing's (placePopover keeps
        // it inside), as in ANDOR ANALOG.
        const auto anchor = arpButton.getBounds().toFloat();
        x = anchor.getX();
        y = anchor.getY() - 10.0f - size.y;
    }
    else if (popupIndex == pitchWheelPopup || popupIndex == modWheelPopup)
    {
        auto& wheel = popupIndex == pitchWheelPopup ? pitchWheel : modWheel;
        const auto at = frame::wheelPopoverOrigin (wheel.getBounds().toFloat(), size);
        x = at.x;
        y = at.y;
    }
    else if (popupIndex == mixPopup)
    {
        // The large MIX unfolds under the header's MIX.
        const auto anchor = headerMix.getBounds().toFloat();
        x = anchor.getCentreX() - 0.5f * size.x;
        y = anchor.getBottom() + 4.0f;
    }
    else
    {
        const auto at = frame::advancedPopoverOrigin (content.getLocalArea (&utilityKeys, advancedSettingsButton().getBounds()).toFloat(), size,
                                                      arpButton.getBounds().toFloat());
        x = at.x;
        y = at.y;
    }
    frame::placePopover (*popup, content, { x, y });
}

void OspAudioProcessorEditor::updateCustomisedDots()
{
    for (int i = 0; i < static_cast<int> (macros.size()); ++i)
    {
        bool customised = false;
        for (const auto& id : popupParameterIds (static_cast<MacroPopup> (i)))
            if (auto* param = ospProcessor.parameters.getParameter (id))
                customised = customised || std::abs (param->getValue() - param->getDefaultValue()) > 1.0e-4f;
        macros[static_cast<std::size_t> (i)].label->setCustomised (customised);
    }
}

//==============================================================================
// Adaptive source area

int OspAudioProcessorEditor::visibleCardCount() const
{
    int n = 0;
    for (const auto& card : cards)
        n += card->isVisible() ? 1 : 0;
    return n;
}

void OspAudioProcessorEditor::setSourceViewShown (bool shown)
{
    if (shown == routingOpen)
        return;
    if (shown && routingView == nullptr)
    {
        routingView = createFxRoutingView (ospProcessor);
        content.addChildComponent (*routingView);
    }
    if (shown && routingView != nullptr)
    {
        // An alternative view of the source area, in the cards' layer - never a top-level
        // panel: the popovers (and their backdrop), the modulation overlay with the popovers'
        // halos, the Library and Learn stay above it. Placed once, as it opens (not at every
        // layout, which once pulled it over an open popover at the next timer tick).
        juce::Component* lowest = &modOverlay;
        auto below = [this, &lowest] (juce::Component* c) {
            if (c != nullptr && c->getParentComponent() == &content && content.getIndexOfChildComponent (c) < content.getIndexOfChildComponent (lowest))
                lowest = c;
        };
        below (popup.get());
        below (&popupBackdrop);
        routingView->toBehind (lowest);
    }
    routingOpen = shown;
    routingKey.setToggleState (shown, juce::dontSendNotification);
    routingKey.repaint();
    shownCount = -1;
    layoutSources (false);
    if (shown && routingView != nullptr)
        routingView->grabKeyboardFocus();
}

void OspAudioProcessorEditor::layoutSources (bool animate)
{
    // Three cards: the row the modifiers had under the knobs is gone (they are in the
    // displays), so the cards are that much shorter and the window with them, as the ARP
    // editor resizes it. One or two: nothing to trim (the keys sat beside the knobs).
    if (const float trim = ospProcessor.slotCount() >= 3 ? EngineCard::footTrim : 0.0f; trim != cardTrim)
    {
        cardTrim = trim;
        for (auto& card : cards)
            card->setFootTrimmed (trim > 0.0f);
        shownCount = -1;
        layoutInstrument();   // (lays the sources out again, at the new trim)
        applyWindowShape (true);
        positionPopup();
        placeLibrary();
        content.repaint();
        return;
    }
    if (routingView != nullptr)
        routingView->setVisible (routingOpen);
    if (routingOpen && routingView != nullptr)
    {
        // The routing view in the cards' place (the sound is the same either way).
        for (auto& card : cards)
            card->setVisible (false);
        dropZone.setVisible (false);
        addTarget.setVisible (false);
        routingView->setBounds (sourceArea);
        shownCount = -1;   // (the cards are laid out afresh when it closes)
        return;
    }
    std::array<bool, OspAudioProcessor::numLayers> occupied {};
    int count = 0;
    // A card per slot: the layers with sounds and the empty slots kept for new ones.
    const int slots = ospProcessor.slotCount();
    for (int l = 0; l < OspAudioProcessor::numLayers; ++l)
    {
        occupied[static_cast<std::size_t> (l)] = l < slots;
        count += occupied[static_cast<std::size_t> (l)] ? 1 : 0;
    }
    const bool countChanged = count != shownCount;
    shownOccupied = occupied;
    shownCount = count;

    // While a sound is dragged over an instrument with room, the next layout is previewed:
    // the cards make space and the new layer's place says ADD LAYER.
    const bool showAdd = dragging && count >= 1 && count < OspAudioProcessor::numLayers;
    const int columns = count + (showAdd ? 1 : 0);
    const auto density = columns <= 1 ? EngineLayoutDensity::hero : (columns == 2 ? EngineLayoutDensity::dual : EngineLayoutDensity::triple);
    dropZone.setVisible (count == 0);
    dropZone.setBounds (sourceArea);
    if (newLayerCapture != nullptr)
    {
        // A take for a new layer: its CAPTURE page over the sources until the take arrives.
        if (newLayerCaptureTarget < OspAudioProcessor::numLayers && ospProcessor.currentInstrument (newLayerCaptureTarget) != nullptr)
            newLayerCapture.reset();
        else
        {
            newLayerCapture->setBounds (sourceArea);
            newLayerCapture->toFront (false);
        }
    }
    auto column = [&] (int i) { return frame::sourceColumn (sourceArea, columns, i); };

    auto& animator = juce::Desktop::getInstance().getAnimator();
    int col = 0;
    for (int l = 0; l < OspAudioProcessor::numLayers; ++l)
    {
        auto& card = *cards[static_cast<std::size_t> (l)];
        if (! occupied[static_cast<std::size_t> (l)])
        {
            animator.cancelAnimation (&card, false);
            card.setVisible (false);
            continue;
        }
        const auto bounds = column (col++);
        card.setDensity (density);
        if (! card.isVisible())
        {
            card.setBounds (bounds);
            card.setVisible (true);
            if (animate && ! countChanged)
                continue;
            if (animate)
            {
                // A new layer settles in (layout only; nothing happens to the sound).
                card.setAlpha (0.0f);
                animator.animateComponent (&card, bounds, 1.0f, 200, false, 1.0, 0.0);
            }
        }
        else if (animate)
            animator.animateComponent (&card, bounds, 1.0f, 180, false, 1.0, 0.0);
        else
        {
            animator.cancelAnimation (&card, false);
            card.setAlpha (1.0f);
            card.setBounds (bounds);
        }
    }
    addTarget.setVisible (showAdd);
    if (showAdd)
    {
        addTarget.setBounds (column (col));
        addTarget.setLetter (OspAudioProcessor::layerName (count));
    }
    headerMix.setLayers (occupied);
    if (popupIndex == mixPopup && headerMix.layerCount() < 3)
        closePopup();   // the large mix belongs to three layers
    if (popupIndex >= reimaginedPopup && ! cards[static_cast<std::size_t> (popupIndex - reimaginedPopup)]->isVisible())
        closePopup();   // its layer is gone
    else if (popupIndex >= reimaginedPopup)
        positionPopup();   // its card moved
    updateFocus();
}

void OspAudioProcessorEditor::refreshNow()
{
    timerCallback();
    if (popup != nullptr)
    {
        juce::Desktop::getInstance().getAnimator().cancelAnimation (popup.get(), true);
        popup->setAlpha (1.0f);
    }
    for (auto& card : cards)
    {
        juce::Desktop::getInstance().getAnimator().cancelAnimation (card.get(), true);
        card->setAlpha (1.0f);
    }
}

void OspAudioProcessorEditor::updateFocus()
{
    int edit = ospProcessor.editLayer();
    if (shownCount > 0 && ! shownOccupied[static_cast<std::size_t> (edit)])
    {
        // The edited layer must be one that is there.
        for (int l = 0; l < OspAudioProcessor::numLayers; ++l)
            if (shownOccupied[static_cast<std::size_t> (l)])
            {
                edit = l;
                break;
            }
        ospProcessor.setEditLayer (edit);
    }
    for (int l = 0; l < OspAudioProcessor::numLayers; ++l)
        cards[static_cast<std::size_t> (l)]->setFocused (shownCount <= 1 || l == edit);
    if (samplesShown)
    {
        const auto& card = *cards[static_cast<std::size_t> (edit)];
        samplesPanel.setBounds (card.getBounds().reduced (12).withTrimmedTop (36).withTrimmedBottom (100));
        samplesPanel.toFront (false);
    }
}

OspAudioProcessorEditor::DropTarget OspAudioProcessorEditor::targetAt (juce::Point<int> where) const
{
    DropTarget target;
    if (shownCount <= 0)
    {
        target.kind = DropTarget::Kind::empty;
        target.layer = 0;
        return target;
    }
    if (addTarget.isVisible() && addTarget.getBounds().contains (where))
    {
        target.kind = DropTarget::Kind::add;
        target.layer = shownCount;   // a new slot after the ones shown
        return target;
    }
    for (int l = 0; l < OspAudioProcessor::numLayers; ++l)
    {
        const auto& card = *cards[static_cast<std::size_t> (l)];
        if (card.isVisible() && card.getBounds().contains (where))
        {
            target.kind = DropTarget::Kind::replace;
            target.layer = l;
            return target;
        }
    }
    return target;
}

juce::String OspAudioProcessorEditor::dropTargetAt (juce::Point<int> where) const
{
    const auto target = targetAt (toInstrument (where));
    switch (target.kind)
    {
        case DropTarget::Kind::empty: return "drop";
        case DropTarget::Kind::replace: return "replace " + OspAudioProcessor::layerName (target.layer);
        case DropTarget::Kind::add: return "add " + OspAudioProcessor::layerName (target.layer);
        case DropTarget::Kind::none: break;
    }
    return {};
}

void OspAudioProcessorEditor::showDropTarget (const DropTarget& target)
{
    dragTarget = target;
    for (int l = 0; l < OspAudioProcessor::numLayers; ++l)
        cards[static_cast<std::size_t> (l)]->display().setDropLabel (target.kind == DropTarget::Kind::replace && target.layer == l
                                                                         ? (ospProcessor.isKeptEmptySlot (l) ? "DROP INTO " : "REPLACE ") + OspAudioProcessor::layerName (l)
                                                                         : juce::String());
    addTarget.setHighlight (target.kind == DropTarget::Kind::add);
    dropZone.setHighlight (dragging && target.kind == DropTarget::Kind::empty);
}

void OspAudioProcessorEditor::previewDrag (bool on, juce::Point<int> where)
{
    dragging = on;
    layoutSources (false);
    showDropTarget (on ? targetAt (toInstrument (where)) : DropTarget {});
}

bool OspAudioProcessorEditor::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (const auto& f : files)
        if (juce::File (f).isDirectory() || isAudioFile (juce::File (f)))
            return true;
    return false;
}

void OspAudioProcessorEditor::fileDragEnter (const juce::StringArray&, int x, int y)
{
    dragging = true;
    layoutSources (true);
    showDropTarget (targetAt (toInstrument ({ x, y })));
}

void OspAudioProcessorEditor::fileDragMove (const juce::StringArray&, int x, int y)
{
    const auto target = targetAt (toInstrument ({ x, y }));
    if (target.kind != dragTarget.kind || target.layer != dragTarget.layer)
        showDropTarget (target);
}

void OspAudioProcessorEditor::fileDragExit (const juce::StringArray&)
{
    dragging = false;
    showDropTarget ({});
    layoutSources (true);
}

void OspAudioProcessorEditor::filesDropped (const juce::StringArray& files, int x, int y)
{
    const auto target = targetAt (toInstrument ({ x, y }));
    dragging = false;
    showDropTarget ({});

    // Everything dropped together - loose files, a folder's files, or both - is ONE sound in
    // the source it lands on: one file plays alone, several become one multi-sample instrument
    // mapped by their detected roots (docs/research/multisample-mapping).
    juce::Array<juce::File> dropped;
    for (const auto& f : files)
    {
        const juce::File file (f);
        if (file.isDirectory())
        {
            for (const auto& entry : juce::RangedDirectoryIterator (file, false, "*", juce::File::findFiles))
                if (isAudioFile (entry.getFile()))
                    dropped.add (entry.getFile());
        }
        else if (isAudioFile (file))
            dropped.add (file);
    }
    if (! dropped.isEmpty() && target.kind != DropTarget::Kind::none)
        learnAction ("source.load.requested", "osp.source.dropZone");   // (the result is checked once it has loaded)
    switch (target.kind)
    {
        case DropTarget::Kind::empty:
        case DropTarget::Kind::add:
            if (dropped.size() == 1)
            {
                const int first = target.kind == DropTarget::Kind::add ? target.layer : ospProcessor.firstFreeLayer();
                if (ospProcessor.addLayers (dropped, first) > 0)
                    ospProcessor.setEditLayer (std::max (0, first));
            }
            else if (! dropped.isEmpty())
                ospProcessor.setEditLayer (std::max (0, ospProcessor.addLayerSet (dropped)));
            break;
        case DropTarget::Kind::replace:
            if (! dropped.isEmpty())
                ospProcessor.replaceLayer (dropped, target.layer);
            ospProcessor.setEditLayer (target.layer);
            break;
        case DropTarget::Kind::none:
            ospProcessor.showMessage ("Drop a sound on a layer to replace it, or beside the layers to add one");
            break;
    }
    layoutSources (true);
    timerCallback();
}

//==============================================================================
// Menus

namespace
{
    juce::String ellipsis() { return juce::String::fromUTF8 ("\xe2\x80\xa6"); }

    /** A menu item showing its shortcut at the right (only shortcuts OSP really handles). */
    juce::PopupMenu::Item itemWithShortcut (const juce::String& text, bool enabled, juce::KeyPress key, std::function<void()> action)
    {
        juce::PopupMenu::Item item (text);
        item.setEnabled (enabled);
        item.setAction (std::move (action));
        item.shortcutKeyDescription = key.getTextDescriptionWithIcons();
        return item;
    }
}

juce::PopupMenu OspAudioProcessorEditor::sourceMenu (int layer)
{
    // One menu for every source, addressed by its own letter (never A by default): SOURCE,
    // AUDIO and MANAGEMENT, each showing only what this source's state makes meaningful.
    const auto letter = OspAudioProcessor::layerName (layer);
    const auto loaded = ospProcessor.currentInstrument (layer);
    const bool busy = ospProcessor.loadState (layer) == OspAudioProcessor::LoadState::loading;
    juce::Component::SafePointer<OspAudioProcessorEditor> safe (this);
    juce::PopupMenu menu;
    // The identity first: the source's letter on its colour, its name and its file.
    using menuicons::Kind;
    {
        const auto file = loaded != nullptr ? juce::String::fromUTF8 (loaded->filename.c_str()) : juce::String();
        menuicons::addSourceHeader (menu, "SOURCE " + letter + (loaded != nullptr ? dot + file : juce::String (dot + "empty")), letter,
                                    palette::layer (InstrumentHost::shownLayer (layer)), loaded != nullptr ? juce::File::createFileWithoutCheckingPath (file).getFileNameWithoutExtension() : juce::String ("Empty"),
                                    loaded != nullptr ? file : juce::String ("Drop audio here, or load a file"));
    }

    // SOURCE: what this slot holds.
    menu.addItem (menuicons::item (loaded != nullptr ? Kind::replace : Kind::open, (loaded != nullptr ? "Replace audio" : "Load audio") + ellipsis(),
                                   [safe, layer] { if (safe != nullptr) safe->chooseFile (layer, false); }));
    menu.addItem (menuicons::item (Kind::waveform, "Load example", [safe, layer] { if (safe != nullptr) safe->ospProcessor.replaceLayer ({}, layer), safe->ospProcessor.loadExample (layer); }, ! busy));
    if (ospProcessor.liveCapture())
        menu.addItem (menuicons::item (Kind::record, "Record audio" + ellipsis(), [safe, layer] {
            if (safe == nullptr)
                return;
            auto& card = *safe->cards[static_cast<std::size_t> (layer)];
            card.setCaptureOpen (! card.isCaptureOpen());
        }, ! busy, cards[static_cast<std::size_t> (layer)]->isCaptureOpen()));

    // AUDIO: the recording's analysis and mapping (a loaded sound only).
    if (loaded != nullptr)
    {
        menu.addSeparator();
        menu.addSectionHeader ("AUDIO");
        // OCTAVE: the detected note in another octave (the root, not a transposition).
        if (loaded->atlas == nullptr && loaded->analysis.pitch.detected)
        {
            const double detected = loaded->analysisRootMidi;
            menu.addCustomItem (SourceOctaveRow::itemId,
                                std::make_unique<SourceOctaveRow> (SourceOctaveRow::octaveOf (ospProcessor.rootOverride (layer), detected),
                                                                   palette::layer (InstrumentHost::shownLayer (layer)),
                                                                   [safe, layer, detected] (int octave) {
                                                                       if (safe != nullptr)
                                                                           safe->ospProcessor.changeRootOverride (SourceOctaveRow::overrideFor (octave, detected), layer);
                                                                   }),
                                nullptr, "OCTAVE");
        }
        {
            juce::PopupMenu root;
            const auto overrideMidi = ospProcessor.rootOverride (layer);
            root.addItem ("Auto (detected)", true, ! overrideMidi.has_value(), [safe, layer] { if (safe != nullptr) safe->ospProcessor.changeRootOverride (std::nullopt, layer); });
            for (int octave = 0; octave <= 8; ++octave)
            {
                juce::PopupMenu notes;
                for (int note = 12 * (octave + 1); note < 12 * (octave + 2) && note < 128; ++note)
                    notes.addItem (juce::String (midiNoteName (note)), true, overrideMidi && std::lround (*overrideMidi) == note,
                                   [safe, layer, note] { if (safe != nullptr) safe->ospProcessor.changeRootOverride (static_cast<double> (note), layer); });
                root.addSubMenu ("Octave " + juce::String (octave), notes);
            }
            menuicons::addSubMenu (menu, Kind::blank, "Root" + (overrideMidi ? ": " + juce::String (midiNoteName (static_cast<int> (std::lround (*overrideMidi)))) : juce::String (": auto")), root);
        }
        // A recording becomes an instrument of its notes (MAP NOTES), and back.
        if (loaded->atlas == nullptr && loaded->set == nullptr)
            menu.addItem (menuicons::item (Kind::mapNotes, "Map notes" + ellipsis(), [safe, layer] {
                if (safe == nullptr)
                    return;
                safe->ospProcessor.mapNotes (layer, phrase::Detection::normal);
                safe->timerCallback();
                safe->learnAction ("notes.mapped", "osp.source.mapNotes");
            }, ospProcessor.canMapNotes (layer)));
        if (loaded->atlas != nullptr)
        {
            const auto* card = cards[static_cast<std::size_t> (layer)].get();
            menu.addItem (menuicons::item (Kind::mapNotes, "Notes", [safe, layer] {
                if (safe == nullptr)
                    return;
                auto& c = *safe->cards[static_cast<std::size_t> (layer)];
                c.onNotesToggle (layer, ! c.isNotesOpen());
            }, true, card != nullptr && card->isNotesOpen()));
            juce::PopupMenu detect;
            const auto current = InstrumentHost::detectionOf (*loaded->atlas);
            const bool analysing = ospProcessor.atlasActivity (layer) != InstrumentHost::AtlasActivity::none;
            for (const auto d : { phrase::Detection::fewer, phrase::Detection::normal, phrase::Detection::more })
                detect.addItem (juce::String (phrase::toString (d)).toUpperCase() + " notes", ! analysing, d == current, [safe, layer, d] {
                    if (safe != nullptr)
                        safe->ospProcessor.mapNotes (layer, d), safe->timerCallback();
                });
            menuicons::addSubMenu (menu, Kind::reset, "Detect notes again", detect, loaded->atlas->analysis.version > 0);
            menu.addItem (menuicons::item (Kind::waveform, "Play as one sample", [safe, layer] {
                if (safe != nullptr)
                    safe->ospProcessor.playAsOneSample (layer), safe->timerCallback();
            }));
        }
        if (loaded->set != nullptr)
            menu.addItem (menuicons::item (Kind::list, "Samples", [safe, layer] {
                if (safe == nullptr)
                    return;
                safe->ospProcessor.setEditLayer (layer);
                safe->samplesShown = ! safe->samplesShown;
                safe->samplesPanel.setVisible (safe->samplesShown);
                safe->samplesPanel.setInstrument (safe->ospProcessor.currentInstrument (layer), layer);
                safe->updateFocus();
                if (safe->samplesShown)
                    safe->learnAction ("samples.opened", "osp.samples");
            }, true, samplesShown && ospProcessor.editLayer() == layer));
        // The recording's file: where it was dropped from, else its copy in the sample store.
        const juce::File original (juce::String::fromUTF8 (loaded->originalPath.c_str())), stored (juce::String::fromUTF8 (loaded->storedPath.c_str()));
        const auto file = original.existsAsFile() ? original : stored;
        if (loaded->set == nullptr && loaded->atlasSources.size() <= 1 && file.existsAsFile())
            menu.addItem (menuicons::item (Kind::reveal, "Reveal source file", [file] { file.revealToUser(); }));
    }

    // MANAGEMENT
    menu.addSeparator();
    menu.addSectionHeader ("MANAGEMENT");
    menu.addItem (menuicons::item (Kind::reset, "Reset source settings", [safe, layer] { if (safe != nullptr) safe->ospProcessor.resetLayerControls (layer), safe->timerCallback(); },
                                   ospProcessor.isLayerOccupied (layer) || ospProcessor.isKeptEmptySlot (layer)));
    // Removes at once (the owner's choice: a confirming sub-menu read as "Remove does nothing");
    // the message says it can be restored from the main menu, which it can, once.
    menuicons::addItem (menu, Kind::remove, "Remove source " + letter,
                        (ospProcessor.isLayerOccupied (layer) || ospProcessor.isKeptEmptySlot (layer)) && ! busy,
                        [safe, layer] { if (safe != nullptr) safe->ospProcessor.removeLayer (layer), safe->timerCallback(); }, true);
    return menu;
}

void OspAudioProcessorEditor::showLayerMenu (int layer, juce::Component& target)
{
    ospProcessor.setEditLayer (layer);
    updateFocus();
    auto menu = sourceMenu (layer);
    if (showMenuForTest != nullptr)
    {
        showMenuForTest (menu);
        return;
    }
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (target).withDeletionCheck (*this));
}

void OspAudioProcessorEditor::openNewLayerCapture()
{
    // The first layer without a sound.
    int target = 0;
    while (target < OspAudioProcessor::numLayers && ospProcessor.currentInstrument (target) != nullptr)
        ++target;
    if (target >= OspAudioProcessor::numLayers)
        return;
    newLayerCaptureTarget = target;
    newLayerCapture = std::make_unique<CaptureEditor> (ospProcessor, target);
    juce::Component::SafePointer<OspAudioProcessorEditor> safe (this);
    newLayerCapture->onClose = [safe] {
        juce::MessageManager::callAsync ([safe] {
            if (safe != nullptr)
            {
                safe->newLayerCapture.reset();
                safe->layoutSources (false);
            }
        });
    };
    content.addAndMakeVisible (*newLayerCapture);
    layoutSources (false);
}

juce::PopupMenu OspAudioProcessorEditor::mainMenu()
{
    // Global actions only (each source's own are on its card): INSTRUMENT, SOURCES, CONTENT,
    // PREFERENCES and SUPPORT. No development command appears here.
    juce::PopupMenu menu;
    juce::Component::SafePointer<OspAudioProcessorEditor> safe (this);
    const int count = ospProcessor.occupiedLayerCount();
    const auto command = juce::ModifierKeys::commandModifier;

    // Every action carries its icon (menuicons; submenus of settings keep the column blank).
    using menuicons::Kind;
    menu.addSectionHeader ("INSTRUMENT");
    menuicons::addItem (menu, Kind::newDocument, "New instrument", true, [safe] {
        if (safe == nullptr)
            return;
        safe->ospProcessor.initPatch();
        safe->presetBar.refresh();
        safe->timerCallback();
    });
    menuicons::addItem (menu, Kind::save, "Save instrument", true, [safe] { if (safe != nullptr) safe->saveInstrument(); });
    menuicons::addItem (menu, Kind::saveAs, "Save as" + ellipsis(), true, [safe] { if (safe != nullptr) safe->choosePresetFile (true, false); });
    menuicons::addItem (menu, Kind::open, "Open instrument" + ellipsis(), true, [safe] { if (safe != nullptr) safe->openInstrumentFile(); });
    menu.addItem (itemWithShortcut ("Open Library", true, juce::KeyPress ('l', command, 0), [safe] {
        if (safe != nullptr)
        {
            safe->openLibrary();
            safe->learnAction ("library.opened", "osp.library");
        }
    }).setImage (menuicons::make (Kind::library)));
    menu.addItem (itemWithShortcut ("Undo " + ospProcessor.undoManager.getUndoDescription(), ospProcessor.undoManager.canUndo(), juce::KeyPress ('z', command, 0),
                                    [safe] { if (safe != nullptr) safe->ospProcessor.undoManager.undo(); }).setImage (menuicons::make (Kind::undo)));
    menu.addItem (itemWithShortcut ("Redo " + ospProcessor.undoManager.getRedoDescription(), ospProcessor.undoManager.canRedo(),
                                    juce::KeyPress ('z', command | juce::ModifierKeys::shiftModifier, 0),
                                    [safe] { if (safe != nullptr) safe->ospProcessor.undoManager.redo(); }).setImage (menuicons::make (Kind::redo)));

    // SOURCES: the instrument's set of sources (one source's own actions are on its card).
    menu.addSeparator();
    menu.addSectionHeader ("SOURCES");
    menuicons::addItem (menu, Kind::add, "Add source" + ellipsis(), count < OspAudioProcessor::numLayers, [safe] { if (safe != nullptr) safe->chooseFile (-1, true); });
    if (ospProcessor.liveCapture())
        menuicons::addItem (menu, Kind::record, "Record new source" + ellipsis(), count < OspAudioProcessor::numLayers, [safe] { if (safe != nullptr) safe->openNewLayerCapture(); });
    if (ospProcessor.canRestoreRemovedLayer())
        menuicons::addItem (menu, Kind::previous, "Restore removed source", true, [safe] { if (safe != nullptr) safe->ospProcessor.restoreRemovedLayer(), safe->timerCallback(); });
    {
        // Every sound out, every setting and slot kept (a sub-menu asks once more).
        juce::PopupMenu confirm;
        menuicons::addItem (confirm, Kind::remove, "Clear all sources (settings are kept)", true, [safe] {
            if (safe != nullptr)
                safe->ospProcessor.clearAllSamples(), safe->timerCallback();
        }, true);
        juce::PopupMenu::Item clear ("Clear all sources");
        clear.setEnabled (count > 0).setImage (menuicons::make (Kind::remove)).setColour (menuicons::destructiveInk());
        clear.subMenu = std::make_unique<juce::PopupMenu> (std::move (confirm));
        menu.addItem (std::move (clear));
    }

    // CONTENT: reusable states, portable instruments and packs.
    menu.addSeparator();
    menu.addSectionHeader ("CONTENT");
    menuicons::addItem (menu, Kind::startingState, "Save as Starting State" + ellipsis(), true, [safe] { if (safe != nullptr) safe->saveStartingState(); });
    menuicons::addItem (menu, Kind::exportOut, "Export portable instrument" + ellipsis(), count > 0, [safe] { if (safe != nullptr) safe->choosePresetFile (true, true); });
    menuicons::addItem (menu, Kind::package, "Create content pack" + ellipsis(), true, [safe] {
        if (safe == nullptr)
            return;
        safe->openLibrary();
        if (auto* library = safe->libraryWindow())
            library->createPackDraft();
    });
    menuicons::addItem (menu, Kind::importIn, "Import pack" + ellipsis(), true, [safe] {
        if (safe == nullptr)
            return;
        safe->openLibrary();
        if (auto* library = safe->libraryWindow())
            library->chooseAndInstallPack();
    });
    menuicons::addItem (menu, Kind::list, "Manage packs" + ellipsis(), true, [safe] {
        if (safe == nullptr)
            return;
        safe->openLibrary();
        if (auto* library = safe->libraryWindow())
            library->openPacksSheet();
    });

    // PREFERENCES
    menu.addSeparator();
    menu.addSectionHeader ("PREFERENCES");
    juce::PopupMenu size;
    for (int percent : { 80, 100, 125, 150, 200 })
        size.addItem (juce::String (percent) + " %", true, std::abs (ospProcessor.uiScale() * 100.0f - static_cast<float> (percent)) < 1.0f,
                      [safe, percent] {
                          if (safe == nullptr)
                              return;
                          safe->ospProcessor.setUiScale (static_cast<float> (percent) / 100.0f);
                          safe->setScaleFactor (safe->ospProcessor.uiScale());
                      });
    menuicons::addSubMenu (menu, Kind::blank, "Interface size", size);
    menuicons::addItem (menu, Kind::settings, "Library settings" + ellipsis(), true, [safe] {
        if (safe == nullptr)
            return;
        safe->openLibrary();
        if (auto* library = safe->libraryWindow())
            library->openSettingsSheet();
    });

    // SUPPORT
    menu.addSeparator();
    menu.addSectionHeader ("SUPPORT");
    if (learn != nullptr)
        learn->addSupportItems (menu);   // Tutorials..., Reset Tutorials...
    menuicons::decorate (menu, { { "Tutorials", menuicons::Kind::tutorials }, { "Restore Previous Sound", menuicons::Kind::previous },
                                 { "Reset Tutorials", menuicons::Kind::reset } });
    menuicons::addItem (menu, menuicons::Kind::info, "About ANDOR OSP", true, [safe] { if (safe != nullptr) safe->showAbout(); });
    return menu;
}

void OspAudioProcessorEditor::showMenu()
{
    auto menu = mainMenu();
    if (showMenuForTest != nullptr)
    {
        showMenuForTest (menu);
        return;
    }
    // Under its key, its card's right edge on the key's (the key is at the window's right: a
    // menu that opened rightwards from it would leave the plugin). The window is the rows' width
    // plus JUCE's border on each side; the look then moves it a shadow margin left.
    const int width = popupMenuWidth (menu);
    const auto key = menuButton.getScreenBounds();
    const int margin = lookAndFeel.getPopupMenuBorderSize() - OspLookAndFeel::popupPadding;
    const int window = width + 2 * lookAndFeel.getPopupMenuBorderSize();
    menu.showMenuAsync (juce::PopupMenu::Options()
                            .withTargetScreenArea (juce::Rectangle<int> (key.getRight() - window + 2 * margin, key.getY(), width, key.getHeight()))
                            .withMinimumWidth (width)
                            .withDeletionCheck (*this));
}

int OspAudioProcessorEditor::popupMenuWidth (const juce::PopupMenu& menu)
{
    // The width the look gives this menu's widest row (its text and shortcut), as JUCE measures it.
    int widest = 120;
    for (juce::PopupMenu::MenuItemIterator it (menu); it.next();)
    {
        const auto& item = it.getItem();
        if (item.isSeparator)
            continue;
        int w = 0, h = 0;
        const auto measured = item.shortcutKeyDescription.isNotEmpty() ? item.text + "      " + item.shortcutKeyDescription : item.text;
        lookAndFeel.getIdealPopupMenuItemSize (measured, false, -1, w, h);
        if (item.isSectionHeader)
            w += w / 4;
        widest = std::max (widest, w);
    }
    return widest;
}

void OspAudioProcessorEditor::saveInstrument()
{
    // SAVE: the open user preset is updated in place (its Library record, tags, favourite and
    // collections stay). A starting state, a factory or pack preset, or a new patch: Save As.
    if (ospProcessor.canSaveInPlace())
    {
        const auto file = ospProcessor.currentPresetFile();
        const bool ok = ospProcessor.savePreset (file);
        ospProcessor.showMessage (ok ? "Saved " + file.getFileNameWithoutExtension() : "Could not save " + file.getFileName());
        if (ok)
            learnAction ("preset.saved", "osp.header.save");
        presetBar.refresh();
        timerCallback();
        return;
    }
    choosePresetFile (true, false);
}

void OspAudioProcessorEditor::openInstrumentFile()
{
    // One chooser for a preset and a portable instrument: the file's type decides.
    auto folder = OspAudioProcessor::presetFolder();
    folder.createDirectory();
    chooser = std::make_unique<juce::FileChooser> ("Open instrument", folder,
                                                   juce::String ("*") + OspAudioProcessor::presetExtension + ";*" + OspAudioProcessor::instrumentExtension);
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this] (const juce::FileChooser& fc) {
        const auto file = fc.getResult();
        if (file == juce::File())
            return;
        const bool portable = file.hasFileExtension (OspAudioProcessor::instrumentExtension);
        juce::String error;
        const bool ok = portable ? ospProcessor.importInstrument (file, error) : ospProcessor.loadPreset (file);
        ospProcessor.showMessage (ok ? "Opened " + file.getFileNameWithoutExtension() : (error.isEmpty() ? "Could not open " + file.getFileName() : error));
        if (ok)
            learnAction (portable ? "instrument.opened" : "preset.opened", "osp.header.menu");
        presetBar.refresh();
        timerCallback();
    });
}

void OspAudioProcessorEditor::showAbout()
{
    juce::String text;
   #ifndef OSP_RELEASE_DISPLAY_VERSION
    #define OSP_RELEASE_DISPLAY_VERSION JucePlugin_VersionString
    #define OSP_RELEASE_BUILD_NUMBER "0"
    #define OSP_RELEASE_GIT_SHA "unknown"
   #endif
    text << "Version " << OSP_RELEASE_DISPLAY_VERSION << " (build " << OSP_RELEASE_BUILD_NUMBER << ", " << OSP_RELEASE_GIT_SHA << ")\n"
         << "Built " << __DATE__ << "\n\n"
         << "One tonal recording, an expressive instrument.";
    juce::AlertWindow::showAsync (juce::MessageBoxOptions()
                                      .withIconType (juce::MessageBoxIconType::NoIcon)
                                      .withTitle ("ANDOR OSP")
                                      .withMessage (text)
                                      .withButton ("Close")
                                      .withAssociatedComponent (this),
                                  nullptr);
}

void OspAudioProcessorEditor::saveStartingState()
{
    // One dialog for every way in (the main menu, the preset name's list): the Library's Save
    // sheet as a Starting State - a name, a description, its category and tags.
    openLibrary (LibraryPanel::View::templates);
    if (libraryPanel != nullptr)
        libraryPanel->openSaveSheet (true);
}

void OspAudioProcessorEditor::choosePresetFile (bool save, bool instrument)
{
    const juce::String pattern = juce::String ("*") + (instrument ? OspAudioProcessor::instrumentExtension : OspAudioProcessor::presetExtension);
    auto folder = instrument ? OspAudioProcessor::instrumentFolder() : OspAudioProcessor::presetFolder();
    folder.createDirectory();
    chooser = std::make_unique<juce::FileChooser> (save ? (instrument ? "Export instrument" : "Save preset") : (instrument ? "Import instrument" : "Load preset"),
                                                   folder, pattern);
    const auto browserFlags = (save ? juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting
                                    : juce::FileBrowserComponent::openMode)
                              | juce::FileBrowserComponent::canSelectFiles;
    chooser->launchAsync (browserFlags, [this, save, instrument] (const juce::FileChooser& fc) {
        auto file = fc.getResult();
        if (file == juce::File())
            return;
        const auto extension = instrument ? OspAudioProcessor::instrumentExtension : OspAudioProcessor::presetExtension;
        if (save && file.getFileExtension() != extension)
            file = file.withFileExtension (extension);
        juce::String error;
        bool ok = false;
        if (instrument)
            ok = save ? ospProcessor.exportInstrument (file, error) : ospProcessor.importInstrument (file, error);
        else
            ok = save ? ospProcessor.savePreset (file) : ospProcessor.loadPreset (file);
        ospProcessor.showMessage (ok ? (save ? "Saved " : "Opened ") + file.getFileName() : (error.isEmpty() ? "Could not open " + file.getFileName() : error));
        if (ok)
            learnAction (instrument ? (save ? "instrument.exported" : "instrument.opened") : (save ? "preset.saved" : "preset.opened"), "osp.header.menu");
        timerCallback();
    });
}

void OspAudioProcessorEditor::chooseFile (int layer, bool addAsNewLayer)
{
    chooser = std::make_unique<juce::FileChooser> (addAsNewLayer ? "Choose sounds" : "Choose a sound", juce::File(),
                                                   "*.wav;*.wave;*.aif;*.aiff;*.aifc;*.flac");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles
                              | juce::FileBrowserComponent::canSelectMultipleItems,
                          [this, layer, addAsNewLayer] (const juce::FileChooser& fc) {
                              juce::Array<juce::File> files;
                              for (const auto& f : fc.getResults())
                                  if (f.existsAsFile())
                                      files.add (f);
                              if (files.isEmpty())
                                  return;
                              learnAction ("source.load.requested", "osp.source.dropZone");
                              // As a drop: one file is one sound, several are one multi-sample sound.
                              if (addAsNewLayer && files.size() > 1)
                                  ospProcessor.setEditLayer (std::max (0, ospProcessor.addLayerSet (files)));
                              else if (addAsNewLayer)
                              {
                                  const int first = ospProcessor.firstFreeLayer();
                                  if (ospProcessor.addLayers (files) > 0)
                                      ospProcessor.setEditLayer (std::max (0, first));
                              }
                              else
                                  ospProcessor.replaceLayer (files, layer);   // several files: one multi-sample sound
                              timerCallback();
                          });
}

bool OspAudioProcessorEditor::keyPressed (const juce::KeyPress& key)
{
    // Escape closes Learn's note, browser or Info first; then the popover, as before.
    if (learn != nullptr && learn->keyPressed (key))
        return true;
    if (userMacroController != nullptr && userMacroController->keyPressed (key))
        return true;   // (Escape: an assignment being dragged or picked is cancelled)
    if (key == juce::KeyPress::escapeKey && popup != nullptr)
    {
        // Closed asynchronously: the key may have come through one of the popup's children.
        juce::Component::SafePointer<OspAudioProcessorEditor> safe (this);
        juce::MessageManager::callAsync ([safe] { if (safe != nullptr) safe->requestClosePopup(); });
        return true;
    }
    const bool command = key.getModifiers().isCommandDown();
    if (command && (key.getKeyCode() == 'L' || key.getKeyCode() == 'l'))
    {
        if (libraryPanel != nullptr)
            closeLibrary();
        else
        {
            openLibrary();
            learnAction ("library.opened", "osp.library");
        }
        return true;
    }
    if (command && key.getModifiers().isAltDown() && (key.getKeyCode() == 'G' || key.getKeyCode() == 'g'))
    {
        // Ceramic / classic look (comparison). Looks only.
        skin::setLook (skin::look() == skin::Look::ceramic ? skin::Look::classic : skin::Look::ceramic);
        repaint();
        content.repaint();
        for (auto* c : content.getChildren())
            c->repaint();
        return true;
    }
    if (command && key.getModifiers().isAltDown() && (key.getKeyCode() == 'K' || key.getKeyCode() == 'k'))
    {
        // Development: original knob / rendered pilot / all macros rendered. Looks only.
        setKnobSkin (nextKnobSkin (knobSkin()));
        content.repaint();
        return true;
    }
    if (command && key.getKeyCode() == 'Z')
    {
        if (key.getModifiers().isShiftDown())
            ospProcessor.undoManager.redo();
        else
            ospProcessor.undoManager.undo();
        return true;
    }
    return false;
}

//==============================================================================
// Layout and drawing

void OspAudioProcessorEditor::paint (juce::Graphics& g)
{
    // Around the instrument when the window's shape differs from the reference's.
    g.fillAll (design::draw::surround());
}

void OspAudioProcessorEditor::paintInstrument (juce::Graphics& g)
{
    // The shared ANDOR frame with OSP's identity (ANDOR/OSP, One Shot Performer).
    // (The housing keeps its height; what the arpeggiator and the racks add is room under it, as
    // the shell's editor draws it.)
    // (the macros shown: CORE's or the USER bank's - their lights follow the knobs on screen)
    const auto& shownRow = userMacroController != nullptr ? userMacroController->paintedRow() : macros;
    frame::paint (g, content, frame::Identity {}, arpShown, shownRow, rackShift(), sourceTrim(), design::height - sourceTrim());
}

void OspAudioProcessorEditor::resized()
{
    frame::fit (*this, content, instrumentHeight(), &overflow);
    modOverlay.setBounds (content.getLocalBounds());
    if (userMacroController != nullptr)
        userMacroController->assignOverlay().setBounds (content.getLocalBounds());   // (over the whole instrument, as the halos)
}

juce::Point<int> OspAudioProcessorEditor::toInstrument (juce::Point<int> editorPoint) const
{
    return content.getLocalPoint (this, editorPoint);
}

void OspAudioProcessorEditor::setArpExpanded (bool open)
{
    ospProcessor.setArpEditorExpanded (open);
    if (open == arpShown)
        return;
    arpShown = open;
    arpButton.setExpanded (open);
    arpPanel.setVisible (open);
    if (open)
        arpPanel.refresh();
    // At once: a host-driven window resize animated frame by frame flickers or lags in
    // several hosts, so the window takes its new height in one step and the layout follows.
    layoutInstrument();
    applyWindowShape (true);
    positionPopup();
    placeLibrary();
    content.repaint();
}

void OspAudioProcessorEditor::setRackExpanded (rack::Kind kind, bool open)
{
    if (open && ! hasRack (kind))
        return;
    racks.setShown (kind, open);   // (rackChanged follows)
}

std::optional<rack::Kind> OspAudioProcessorEditor::macroRackKind (const juce::String& parameterId)
{
    // MOVEMENT, ECHO and SPACE: the shared racks; CHARACTER: its own (the voice filter's face,
    // as ANDOR ANALOG's FILTER); DRIVE: the FX rack's DRIVE (as the routing view opens it).
    if (parameterId == "character")
        return rack::Kind::filter;
    if (parameterId == "drive")
        return rack::Kind::fx;
    return rack::kindForMacro (parameterId);
}

bool OspAudioProcessorEditor::isMacroRackOpen (rack::Kind kind) const
{
    if (kind == rack::Kind::fx)   // (DRIVE's: the FX rack showing DRIVE, not another effect)
        return racks.isShown (kind) && fxRackShown().type == fx::Type::drive;
    return racks.isShown (kind);
}

void OspAudioProcessorEditor::toggleRackFromLabel (rack::Kind kind)
{
    // (A popover in the way closes; a press that closed it must not count later.)
    closedByLabelPress = -1;
    closePopup();
    if (kind == rack::Kind::fx)
    {
        if (isMacroRackOpen (kind))
            setRackExpanded (kind, false);
        else
            openFxRack (*this, { fx::Type::drive, 0 });   // (another effect's rack swaps to DRIVE)
        return;
    }
    setRackExpanded (kind, ! racks.isShown (kind));
}

void OspAudioProcessorEditor::rackChanged()
{
    // A view change only: the window grows or shrinks by the rack, nothing sounds different.
    for (std::size_t i = 0; i < macros.size(); ++i)
        if (rackOfMacro[i].has_value() && macros[i].label != nullptr)
            macros[i].label->setRackOpen (isMacroRackOpen (*rackOfMacro[i]));
    layoutInstrument();
    applyWindowShape (true);
    positionPopup();
    placeLibrary();
    content.repaint();
    racks.settle();
}

void OspAudioProcessorEditor::applyWindowShape (bool resizeWindow)
{
    frame::applyWindowShape (*this, instrumentHeight(), resizeWindow);
}

void OspAudioProcessorEditor::selectModulation (int source, int slot)
{
    // The one selection: the panel shows the source (when it shows a source at all), the
    // halos emphasise its routes (the chosen one most), an open routes popover marks the row.
    modSelSource = source;
    modSelSlot = slot;
    if (modui::tabOfSource (source) > 0 && modPanel.tab() != 0 && modPanel.selectedSource() != source)   // the wheel, ENV 2, the controllers: no tab
        modPanel.showTab (modui::tabOfSource (source));
    modOverlay.setSelection (modSelSource, modSelSlot);
    if (auto* routes = dynamic_cast<ModRoutesPopup*> (popup.get()))
        routes->setSelectedSlot (modSelSlot);
}

void OspAudioProcessorEditor::openModulationPopup (int which, int source)
{
    // LFO 1 .. ENV 2, VEL and NOTE have pages (their curves, their routes); the wheel's
    // routes are on its controls.
    if (source < 0 || (source > 3 && modui::keyCurveIndex (source) < 0))
        return;
    popupSource = source;
    openPopup (which);
}

void OspAudioProcessorEditor::dragModulation (int source, juce::Point<float> where)
{
    modOverlay.setDrag (source, where);
    headerMix.setModulationDrag (source, headerMix.acceptsModulationDrop (where));
    // Resting on a macro whose popover holds destinations (CHARACTER: RES) opens it, and on
    // a card's EQ key its EQ, so the drag can continue onto them.
    int over = -1;
    for (std::size_t i = 0; i < macros.size(); ++i)
    {
        const auto area = macros[i].slider.getBounds().getUnion (macros[i].label->getBounds()).toFloat();
        if (! area.contains (where))
            continue;
        for (const auto& id : popupParameterIds (static_cast<MacroPopup> (i)))
            if (! modui::destinationsShownBy (id).empty())
                over = static_cast<int> (i);
    }
    for (auto& card : cards)
    {
        if (! card->isVisible())
            continue;
        if (content.getLocalArea (card.get(), card->eqToggle().getBounds()).toFloat().contains (where))
            over = 100 + card->layer();
        // A Granular layer's display: its POS SIZE DENS TUNE SPREAD come up (the pointer's own
        // hover never reaches the card while a source is being dragged).
        else if (card->isGranular() && ! card->granularControlsShown() && ! card->isEqOpen()
                 && content.getLocalArea (card.get(), card->display().getBounds()).toFloat().contains (where))
            over = 200 + card->layer();
    }
    if (over != hoverMacro)
    {
        hoverMacro = over;
        hoverSince = juce::Time::getMillisecondCounter();
    }
    else if (over >= 200 && juce::Time::getMillisecondCounter() - hoverSince > 380)
    {
        // A short rest (not a pass across it) opens the granular controls; the drag goes on.
        cards[static_cast<std::size_t> (over - 200)]->showGranularControls (true, true);
        hoverMacro = -1;
        modOverlay.setDrag (-1, where);   // collect the knobs now shown
        modOverlay.setDrag (source, where);
    }
    else if (over >= 0 && over < 200 && juce::Time::getMillisecondCounter() - hoverSince > 550)
    {
        if (over >= 100)
        {
            auto& card = *cards[static_cast<std::size_t> (over - 100)];
            if (! card.isEqOpen())
            {
                card.onEqToggle (card.layer(), true);
                modOverlay.setDrag (-1, where);   // collect the editor's fields
                modOverlay.setDrag (source, where);
            }
        }
        else if (popupIndex != popupOfMacro (over))
        {
            openPopup (popupOfMacro (over));
            modOverlay.setDrag (source, where);
        }
    }
}

int OspAudioProcessorEditor::dropModulation (int source, juce::Point<float> where)
{
    int slot = -1;
    modOverlay.setDrag (source, where);   // the targets as they are now (a popover or EQ may have opened)
    // The one assignment path (as the right-click menu's): on a halo the route's depth, on
    // the control its value; an existing pairing is selected as it is, a refused one says why.
    // The header MIX has no knob: a drop on it is MIX.
    const bool onMix = headerMix.acceptsModulationDrop (where);
    headerMix.setModulationDrag (-1, false);
    slot = onMix ? modOverlay.dropOn (source, modui::mixTargetId, headerMix.getBounds().toFloat()) : modOverlay.drop (source, where);
    if (slot >= 0)
        modPanel.refresh();
    hoverMacro = -1;
    modOverlay.setDrag (-1, {});
    return slot;
}

void OspAudioProcessorEditor::layoutInstrument()
{
    // Everything at the reference's places (the shared ANDOR frame), then OSP's own parts.
    frame::Parts parts;
    parts.presetBar = &presetBar;
    parts.savePreset = &savePresetButton;
    parts.volume = &volume;
    parts.menu = &menuButton;
    parts.macros = &macros;
    parts.modulationPanel = &modPanel;
    parts.arpPanel = &arpPanel;
    parts.pitchWheel = &pitchWheel;
    parts.modWheel = &modWheel;
    parts.keyboard = &keyboard;
    parts.arpControl = &arpButton;
    parts.utilityActions = &utilityKeys;
    parts.headerMix = &headerMix;
    parts.rack = &racks;
    parts.rackShift = rackShift();
    parts.sourceTrim = sourceTrim();
    frame::place (parts, arpShown);
    if (userMacroController != nullptr)
        userMacroController->place();   // (USER in CORE's places, the tabs on the panel's edge)
    {
        // FX ROUTING's key as ANDOR ANALOG's: SAVE's size, 8 px after it on its centre line (one
        // group of keys); MIX moved right by as much, narrower by as much (it centres itself).
        const auto save = savePresetButton.getBounds();
        routingKey.setBounds (save.withX (save.getRight() + 8));
        const auto mix = headerMix.getBounds();
        if (const int shift = routingKey.getRight() + 15 - mix.getX(); shift > 0)
            headerMix.setBounds (mix.withTrimmedLeft (shift));
    }

    modOverlay.setBounds (content.getLocalBounds());
    sourceArea = frame::sourceArea (sourceTrim());
    layoutSources (false);
}

void OspAudioProcessorEditor::updateStatus()
{
    // No footer line (the compact bottom edge): "Ready", "No sound loaded" and the notes held
    // need no words, and the cards show their own loading. What matters - a file that could not
    // load, a preset opened or saved, a warning about the sound - is the overlay's brief notice,
    // once, when it happens.
    const int layer = ospProcessor.editLayer();
    const bool failed = ospProcessor.loadState (layer) == OspAudioProcessor::LoadState::failed;
    if (failed && ! shownLoadFailed)
        modOverlay.showStatusNotice ("Could not load that file");
    shownLoadFailed = failed;
    if (const auto message = ospProcessor.statusMessage(); message != shownStatusMessage)
    {
        shownStatusMessage = message;
        if (message.isNotEmpty())
            modOverlay.showStatusNotice (message);
    }
    // ("Ready · Natural pitch needs ...": the part after "Ready" is a warning worth reading)
    const auto stage = ospProcessor.stageMessage();
    const auto warning = stage.startsWith ("Ready") ? stage.fromFirstOccurrenceOf (juce::String::fromUTF8 ("\xc2\xb7 "), false, false).trim() : juce::String();
    if (warning != shownSoundWarning)
    {
        shownSoundWarning = warning;
        if (warning.isNotEmpty())
            modOverlay.showStatusNotice (warning);
    }
}

void OspAudioProcessorEditor::updateKeyTints()
{
    // Only the edited layer's map (three maps cannot share one keyboard); an ordinary layer
    // leaves the keys as they always were.
    const int l = ospProcessor.editLayer();
    const auto atlas = ospProcessor.layerAtlas (l);
    // The set as built (Stage B knows which notes loop) and the layer's LOOP: with LOOP on,
    // notes that cannot loop are not played, so their keys show what plays instead.
    const auto instrument = ospProcessor.currentInstrument (l);
    const InstrumentSet* set = instrument != nullptr && instrument->atlas != nullptr ? instrument->set.get() : nullptr;
    const bool loop = ospProcessor.parameterValue (InstrumentHost::layerParameterId (l, "loop")) >= 0.5f;
    if (atlas == tintedAtlas && l == tintedLayer && set == tintedSet && loop == tintedLoop)
        return;
    tintedAtlas = atlas;
    tintedSet = set;
    tintedLoop = loop;
    tintedLayer = l;
    const auto before = keyTints;
    if (atlas != nullptr)
        keyTints = NoteMapEditor::keyMap (*atlas, set, loop);
    else
        keyTints = {};
    if (keyTints != before)
        keyboard.repaint();
}

void OspAudioProcessorEditor::utilityKeyClicked (KeyboardUtilityActions::Action a)
{
    using Action = KeyboardUtilityActions::Action;
    if (a == Action::settings)
    {
        if (closedByLabelPress == advancedPopup)
            closedByLabelPress = -1;   // (that press closed it)
        else if (popup != nullptr && popupIndex == advancedPopup)
            closePopup();
        else
        {
            openPopup (advancedPopup);
            learnAction (andor::learn::actions::settingsOpened, "keyboard.settings");
        }
    }
    else if (a == Action::info && learn != nullptr)
    {
        learn->setLearnMode (! learn->learnMode());
        learnAction (andor::learn::actions::infoToggled, "keyboard.info");
    }
    else if (a == Action::library)
    {
        // The existing Library (as the menu's Library... and Cmd+L): opens it, or closes it.
        if (libraryPanel != nullptr)
            closeLibrary();
        else
        {
            openLibrary();
            learnAction ("library.opened", "osp.library");
        }
    }
    refreshUtilityKeys();
}

void OspAudioProcessorEditor::refreshUtilityKeys()
{
    using Action = KeyboardUtilityActions::Action;
    utilityKeys.setActive (Action::settings, popup != nullptr && popupIndex == advancedPopup);
    utilityKeys.setTooltip (Action::settings, "Advanced Settings");
    utilityKeys.setActive (Action::library, libraryPanel != nullptr);
    utilityKeys.setTooltip (Action::library, libraryPanel != nullptr ? "Close Library" : "Library");
    const bool on = learn != nullptr && learn->learnMode();
    utilityKeys.setActive (Action::info, on);
    utilityKeys.setTooltip (Action::info, on ? "Hide Info" : "Show Info");
}

void OspAudioProcessorEditor::timerCallback()
{
    refreshUtilityKeys();
    if (userMacroController != nullptr)
        userMacroController->tick();
    bool changed = false;
    int count = 0;
    for (int l = 0; l < OspAudioProcessor::numLayers; ++l)
    {
        const bool occupied = l < ospProcessor.slotCount();
        changed = changed || occupied != shownOccupied[static_cast<std::size_t> (l)];
        count += occupied ? 1 : 0;
    }
    if (changed || shownCount < 0)
    {
        const bool animate = shownCount >= 1 && count >= 1;
        layoutSources (animate);
        content.repaint (juce::Rectangle<int> (40, 30, 420, 80));
    }
    for (auto& card : cards)
        if (card->isVisible())
            card->refresh();
    updateFocus();
    updateKeyTints();
    headerMix.refreshModulation();
    if (keyDnaPanel != nullptr)
        keyDnaPanel->refresh();
    presetBar.refresh();
    // The controller's wheel moves it too: taken when it changes (a screen drag keeps its
    // place even while the host is not processing).
    if (const float wheel = ospProcessor.modWheelPosition(); ! juce::exactlyEqual (wheel, shownWheelPosition))
    {
        shownWheelPosition = wheel;
        modWheel.setValue (wheel);
    }
    if (samplesShown)
    {
        const auto instrument = ospProcessor.currentInstrument (ospProcessor.editLayer());
        if (instrument == nullptr || instrument->set == nullptr)
        {
            samplesShown = false;
            samplesPanel.setVisible (false);
        }
        else
            samplesPanel.setInstrument (instrument, ospProcessor.editLayer());
    }
    // Pictures that follow parameters repaint only when those moved.
    {
        const std::array<float, 6> now { ospProcessor.parameterValue ("mix.x"), ospProcessor.parameterValue ("mix.y"),
                                         ospProcessor.parameterValue ("attack"), ospProcessor.parameterValue ("decay"),
                                         ospProcessor.parameterValue ("sustainLevel"), ospProcessor.parameterValue ("release") };
        auto moved = [&] (std::size_t from, std::size_t to) {
            for (auto i = from; i < to; ++i)
                if (std::abs (now[i] - shownValues[i]) > 1.0e-6f)
                    return true;
            return false;
        };
        if (moved (2, 6))
            modPanel.ampEnvelope().repaint (modPanel.ampEnvelope().graphBounds());
        shownValues = now;
    }
    updateCustomisedDots();
    updateStatus();
    // The arpeggiator: its state by the keyboard, its step display, and the editor shown or
    // hidden when a recalled session says so.
    if (ospProcessor.arpEditorExpanded() != arpShown)
        setArpExpanded (ospProcessor.arpEditorExpanded());
    // The open racks follow the sound (CHARACTER's graph its voices' filter, the others what
    // they draw live), as the shell's editor does on every tick.
    racks.refresh();
    arpButton.refresh();
    if (arpShown)
        arpPanel.refresh();
    // Modulation: the bay (a recalled session may open or close it), the rings, MOD's light.
    if (ospProcessor.modPanelTab() != modPanel.tab())
        modPanel.showTab (ospProcessor.modPanelTab());   // a recalled session
    modPanel.refresh();
    modOverlay.refresh();
    modPanel.ampEnvelope().refreshLive();   // (its picture follows A D S R's modulation)
    if (popup != nullptr)
        popup->refreshContent();
    bool wheelInUse = false;
    for (const auto& route : ospProcessor.modulationRoutes())
        wheelInUse = wheelInUse || (route.route.source == mod::Source::modWheel && route.state == mod::RouteState::active);
    modWheel.setInUse (wheelInUse);
    tickLearn();
}

} // namespace osp::plugin
