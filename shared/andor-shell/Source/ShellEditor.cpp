#include "ShellEditor.h"

#include "Binding.h"

namespace andor::shell
{

using namespace osp::plugin;

namespace
{
    /** Every descendant of `root`, depth first. */
    void forEachDescendant (juce::Component& root, const std::function<void (juce::Component&)>& visit)
    {
        for (auto* child : root.getChildren())
        {
            visit (*child);
            forEachDescendant (*child, visit);
        }
    }
}

ShellEditor::ShellEditor (ShellProcessor& p)
    : AudioProcessorEditor (p),
      shell (p),
      modulationBound (binding::anyBound ({ "mod.route*" }, p.boundParameters())),
      pitchWheel ("PITCH", true, [&p] (float v) { p.setScreenPitchWheel (v); }),
      modWheel ("MOD", false, [&p] (float v) { p.setScreenModWheel (v); }),
      keyboard (p.keyboardState)
{
    setLookAndFeel (&lookAndFeel);

    // Header: the preset, the master volume and the menu. (Child order as ANDOR OSP's: the
    // components overlap the same way.)
    presetBar.onChange = [this] { timerCallback(); };
    content.addAndMakeVisible (presetBar);
    savePresetButton.onClick = [this] { choosePresetFile (true); };
    content.addAndMakeVisible (savePresetButton);
    content.addAndMakeVisible (volume);
    menuButton.setTooltip ("Sounds, presets, undo");
    menuButton.onClick = [this] { showMenu(); };
    content.addAndMakeVisible (menuButton);
    setWantsKeyboardFocus (true);

    // Sources: one card per layer slot, the drop zone, the add target.
    for (int layer = 0; layer < InstrumentHost::numLayers; ++layer)
    {
        auto& card = cards[static_cast<std::size_t> (layer)];
        card = std::make_unique<EngineCard> (shell, layer);
        card->onFocus = [this] (int l) {
            shell.setEditLayer (l);
            updateFocus();
        };
        card->onMenu = [this] (int l, juce::Component& target) { showLayerMenu (l, target); };
        card->onEqToggle = [this] (int l, bool open) {
            for (auto& c : cards)
                c->setEqOpen (open && c->layer() == l);
            modOverlay.toFront (false);
            applyBindings();   // the editor's fields are new controls
        };
        content.addChildComponent (*card);
    }
    dropZone.onBrowse = [this] { chooseFile (-1); };
    content.addChildComponent (dropZone);
    content.addChildComponent (addTarget);
    content.addChildComponent (headerMix);

    // The six macros (the product's): names and knobs as ANDOR OSP's.
    const bool macroPopovers = shell.product().macroPopover != nullptr;
    for (std::size_t i = 0; i < macros.size(); ++i)
    {
        frame::setUpMacro (macros[i], static_cast<int> (i), shell.product().macros[i], shell.parameters, content, macroPopovers);
        if (macroPopovers)
            macros[i].label->onClick = [this, i] {
                if (closedByLabelPress == static_cast<int> (i))
                {
                    closedByLabelPress = -1;
                    return;
                }
                openPopup (static_cast<int> (i));
            };
    }
    content.addAndMakeVisible (modPanel);

    content.addAndMakeVisible (pitchWheel);
    content.addAndMakeVisible (modWheel);
    frame::setUpKeyboard (keyboard);
    content.addAndMakeVisible (keyboard);
    frame::setUpStatusLabel (statusLabel);
    content.addAndMakeVisible (statusLabel);
    advancedButton.setTitle ("Advanced settings");
    advancedButton.setTooltip ("Tuning, bend range, voices, pitch character, dynamics (how touch changes the sound), MPE, variation seed");
    // ANDOR OSP's Advanced popover (shared): enabled when the engine reads any of it.
    binding::tag (advancedButton, "fineTune bendRange glide voiceMode pitchCharacter mpe seed dynamics dynamics.curve velocityRange dynamics.tone");
    advancedButton.onClick = [this] {
        if (closedByLabelPress == advancedPopup)
        {
            closedByLabelPress = -1;
            return;
        }
        openPopup (advancedPopup);
    };
    content.addAndMakeVisible (advancedButton);
    arpButton.onToggleEditor = [this] { setArpExpanded (! arpShown); };
    arpPanel.onCollapse = [this] { setArpExpanded (false); };
    content.addAndMakeVisible (arpButton);
    content.addChildComponent (arpPanel);
    arpShown = shell.arpEditorExpanded();
    arpButton.setExpanded (arpShown);
    arpPanel.setVisible (arpShown);

    // MODULATION: the panel's tabs are the sources; dragging one onto a control makes a
    // route, but only when the engine reads the routes (else nothing pretends to modulate).
    if (modulationBound)
    {
        modWheel.setAssignable (modui::sourceColour (4));
        modWheel.onSourceDrag = [this] (juce::Point<int> screen) { dragModulation (4, content.getLocalPoint (nullptr, screen).toFloat()); };
        modWheel.onSourceDrop = [this] (juce::Point<int> screen) { dropModulation (4, content.getLocalPoint (nullptr, screen).toFloat()); };
        modPanel.onDragMove = [this] (int source, juce::Point<int> screen) { dragModulation (source, content.getLocalPoint (nullptr, screen).toFloat()); };
        modPanel.onDragEnd = [this] (int source, juce::Point<int> screen) { dropModulation (source, content.getLocalPoint (nullptr, screen).toFloat()); };
        modPanel.onShowRoutes = [this] (int source) { openPopup (routesPopup, source); };
        modOverlay.onSelect = [this] (int source, int slot) { selectModulation (source, slot); };
        modOverlay.onShowRoutes = [this] (int source) { openPopup (routesPopup, source); };
    }
    modPanel.onExpandCurve = [this] (int source) { openPopup (curvePopup, source); };
    modPanel.onTabChanged = [this] (int tab) {
        shell.setModPanelTab (tab);
        selectModulation (modui::sourceOfTab (tab), -1);
        applyBindings();   // the tab's page is shown now
    };
    modPanel.showTab (shell.modPanelTab());
    content.addAndMakeVisible (modOverlay);   // last: above everything

    juce::Desktop::getInstance().addGlobalMouseListener (&outsideClicks);

    content.onPaint = [this] (juce::Graphics& g) { frame::paint (g, content, shell.product().identity, arpShown, macros); };
    addAndMakeVisible (content);
    layoutInstrument();
    setResizable (true, true);
    frame::applyWindowShape (*this, instrumentHeight(), false);
    constexpr float defaultScale = 1086.0f / design::width;
    setSize (juce::roundToInt (defaultScale * design::width), juce::roundToInt (defaultScale * instrumentHeight()));
    applyBindings();
    timerCallback();
    startTimerHz (30);
}

ShellEditor::~ShellEditor()
{
    stopTimer();
    juce::Desktop::getInstance().removeGlobalMouseListener (&outsideClicks);
    for (auto& card : cards)
        juce::Desktop::getInstance().getAnimator().cancelAnimation (card.get(), false);
    popup.reset();
    setLookAndFeel (nullptr);
}

void ShellEditor::paint (juce::Graphics& g)
{
    g.fillAll (design::draw::surround());
}

void ShellEditor::resized()
{
    frame::fit (*this, content, instrumentHeight());
    modOverlay.setBounds (content.getLocalBounds());
}

void ShellEditor::layoutInstrument()
{
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
    parts.status = &statusLabel;
    parts.arpControl = &arpButton;
    parts.advanced = &advancedButton;
    parts.headerMix = &headerMix;
    frame::place (parts, arpShown);
    modOverlay.setBounds (content.getLocalBounds());
    sourceArea = frame::sourceArea();
    shownCount = -1;
    layoutSources();
}

//==============================================================================
// Bindings: a control the engine does not read is shown disabled (ANDOR OSP's own disabled
// look: the look-and-feel's dimmed state, and the 0.4 alpha of a disabled control).

void ShellEditor::applyBindings()
{
    const auto& bound = shell.boundParameters();
    int count = 0;
    forEachDescendant (content, [&] (juce::Component& c) {
        const auto tags = binding::tagsOf (c);
        if (tags.isEmpty())
            return;
        const bool on = binding::anyBound (tags, bound);
        c.setEnabled (on);
        c.setAlpha (on ? 1.0f : 0.4f);
        count += on ? 0 : 1;
    });
    // The keyboard row: the wheels are read only by engines that say so.
    for (auto [wheel, id] : { std::pair<Wheel*, const char*> { &pitchWheel, "midi.pitchWheel" }, { &modWheel, "midi.modWheel" } })
    {
        const bool on = binding::anyBound ({ id }, bound);
        wheel->setEnabled (on);
        wheel->setAlpha (on ? 1.0f : 0.4f);
        count += on ? 0 : 1;
    }
    disabledControls = count;
}

//==============================================================================
// Sources

int ShellEditor::visibleCardCount() const
{
    int n = 0;
    for (const auto& card : cards)
        n += card->isVisible() ? 1 : 0;
    return n;
}

void ShellEditor::layoutSources()
{
    const int count = shell.slotCount();
    if (count == shownCount && ! dragging)
        return;
    shownCount = count;
    std::array<bool, InstrumentHost::numLayers> occupied {};
    for (int l = 0; l < InstrumentHost::numLayers; ++l)
        occupied[static_cast<std::size_t> (l)] = l < count;
    const bool showAdd = dragging && count >= 1 && count < InstrumentHost::numLayers;
    const int columns = count + (showAdd ? 1 : 0);
    const auto density = columns <= 1 ? EngineLayoutDensity::hero : (columns == 2 ? EngineLayoutDensity::dual : EngineLayoutDensity::triple);
    dropZone.setVisible (count == 0);
    dropZone.setBounds (sourceArea);
    int col = 0;
    for (int l = 0; l < InstrumentHost::numLayers; ++l)
    {
        auto& card = *cards[static_cast<std::size_t> (l)];
        if (! occupied[static_cast<std::size_t> (l)])
        {
            card.setVisible (false);
            continue;
        }
        card.setDensity (density);
        card.setBounds (frame::sourceColumn (sourceArea, columns, col++));
        card.setVisible (true);
    }
    addTarget.setVisible (showAdd);
    if (showAdd)
    {
        addTarget.setBounds (frame::sourceColumn (sourceArea, columns, col));
        addTarget.setLetter (InstrumentHost::layerName (count));
    }
    headerMix.setLayers (occupied);
    updateFocus();
    applyBindings();
    content.repaint();
}

void ShellEditor::updateFocus()
{
    int edit = shell.editLayer();
    if (shownCount > 0 && edit >= shownCount)
    {
        edit = 0;
        shell.setEditLayer (edit);
    }
    for (int l = 0; l < InstrumentHost::numLayers; ++l)
        cards[static_cast<std::size_t> (l)]->setFocused (shownCount <= 1 || l == edit);
}

void ShellEditor::refreshNow()
{
    timerCallback();
    if (popup != nullptr)
    {
        juce::Desktop::getInstance().getAnimator().cancelAnimation (popup.get(), true);
        popup->setAlpha (1.0f);
    }
}

int ShellEditor::dropLayerAt (juce::Point<int> editorPoint) const
{
    // On a card: that layer (its sound is replaced). Elsewhere: a new layer (-1).
    const auto at = content.getLocalPoint (this, editorPoint);
    for (int l = 0; l < InstrumentHost::numLayers; ++l)
        if (cards[static_cast<std::size_t> (l)]->isVisible() && cards[static_cast<std::size_t> (l)]->getBounds().contains (at))
            return l;
    return -1;
}

bool ShellEditor::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (const auto& f : files)
        if (juce::File (f).hasFileExtension ("wav;aif;aiff;flac"))
            return true;
    return false;
}

void ShellEditor::fileDragEnter (const juce::StringArray&, int x, int y)
{
    dragging = true;
    dragLayer = dropLayerAt ({ x, y });
    shownCount = -1;
    layoutSources();
    dropZone.setHighlight (true);
}

void ShellEditor::fileDragMove (const juce::StringArray&, int x, int y)
{
    dragLayer = dropLayerAt ({ x, y });
    addTarget.setHighlight (dragLayer < 0);
}

void ShellEditor::fileDragExit (const juce::StringArray&)
{
    dragging = false;
    dropZone.setHighlight (false);
    shownCount = -1;
    layoutSources();
}

void ShellEditor::filesDropped (const juce::StringArray& files, int x, int y)
{
    const int layer = dropLayerAt ({ x, y });
    dragging = false;
    dropZone.setHighlight (false);
    juce::Array<juce::File> list;
    for (const auto& f : files)
        if (juce::File (f).hasFileExtension ("wav;aif;aiff;flac"))
            list.add (juce::File (f));
    if (layer >= 0 && ! list.isEmpty())
        shell.loadFile (list.getFirst(), layer);
    else
        shell.addLayers (list);
    shownCount = -1;
    timerCallback();
}

void ShellEditor::chooseFile (int layer)
{
    chooser = std::make_unique<juce::FileChooser> ("Choose a sound", juce::File(), "*.wav;*.aif;*.aiff;*.flac");
    juce::Component::SafePointer<ShellEditor> safe (this);
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [safe, layer] (const juce::FileChooser& fc) {
        if (safe == nullptr || fc.getResult() == juce::File())
            return;
        if (layer >= 0)
            safe->shell.loadFile (fc.getResult(), layer);
        else
            safe->shell.addLayers ({ fc.getResult() });
    });
}

void ShellEditor::showLayerMenu (int layer, juce::Component& target)
{
    juce::PopupMenu menu;
    menu.addSectionHeader ("Layer " + InstrumentHost::layerName (layer));
    menu.addItem (juce::String::fromUTF8 ("Replace sound\xe2\x80\xa6"), [this, layer] { chooseFile (layer); });
    menu.addItem ("Remove layer", [this, layer] { shell.removeLayer (layer); timerCallback(); });
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&target));
}

//==============================================================================
// Menu and presets

void ShellEditor::showMenu()
{
    juce::PopupMenu menu;
    menu.addSectionHeader (shell.product().name);
    menu.addItem (juce::String::fromUTF8 ("Add sound\xe2\x80\xa6"), shell.slotCount() < InstrumentHost::numLayers, false, [this] { chooseFile (-1); });
    menu.addSeparator();
    menu.addItem (juce::String::fromUTF8 ("Open preset\xe2\x80\xa6"), [this] { choosePresetFile (false); });
    menu.addItem (juce::String::fromUTF8 ("Save preset\xe2\x80\xa6"), [this] { choosePresetFile (true); });
    menu.addItem ("Init", [this] { shell.initPatch(); timerCallback(); });
    menu.addItem ("Reset settings", [this] { shell.resetSettings(); timerCallback(); });
    menu.addSeparator();
    menu.addItem ("Undo", shell.undoManager.canUndo(), false, [this] { shell.undoManager.undo(); });
    menu.addItem ("Redo", shell.undoManager.canRedo(), false, [this] { shell.undoManager.redo(); });
    menu.addSeparator();
    const auto engine = shell.engineDescription();
    menu.addItem (juce::String ("Engine: ") + engine.name + (engine.diagnostic ? " (diagnostic)" : ""), false, false, [] {});
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&menuButton));
}

void ShellEditor::choosePresetFile (bool save)
{
    const auto folder = shell.presetFolder();
    folder.createDirectory();
    const auto pattern = "*" + shell.product().presetExtension;
    chooser = std::make_unique<juce::FileChooser> (save ? "Save preset" : "Open preset", folder, pattern);
    juce::Component::SafePointer<ShellEditor> safe (this);
    const auto flags = save ? juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting
                            : juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;
    chooser->launchAsync (flags, [safe, save] (const juce::FileChooser& fc) {
        if (safe == nullptr || fc.getResult() == juce::File())
            return;
        if (save)
            safe->shell.savePreset (fc.getResult());
        else
            safe->shell.loadPreset (fc.getResult());
        safe->presetBar.refresh();
        safe->timerCallback();
    });
}

bool ShellEditor::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress::escapeKey && popup != nullptr)
    {
        closePopup();
        return true;
    }
    if (key.getModifiers().isCommandDown() && key.getKeyCode() == 'Z')
    {
        if (key.getModifiers().isShiftDown())
            shell.undoManager.redo();
        else
            shell.undoManager.undo();
        return true;
    }
    return false;
}

//==============================================================================
// Modulation

void ShellEditor::selectModulation (int source, int slot)
{
    modSelSource = source;
    modSelSlot = slot;
    modOverlay.setSelection (modSelSource, modSelSlot);
    if (auto* routes = dynamic_cast<ModRoutesPopup*> (popup.get()))
        routes->setSelectedSlot (modSelSlot);
}

void ShellEditor::dragModulation (int source, juce::Point<float> where)
{
    modOverlay.setDrag (source, where);
}

void ShellEditor::dropModulation (int source, juce::Point<float> where)
{
    modOverlay.setDrag (source, where);
    if (modOverlay.drop (source, where) >= 0)
        modPanel.refresh();
    modOverlay.setDrag (-1, {});
}

void ShellEditor::openPopup (int which, int source)
{
    closePopup();
    if (which == routesPopup || which == curvePopup)
    {
        if (source > 3)
            return;   // the wheel's routes are on its controls
        popupSource = juce::jlimit (0, 3, source);
        if (which == routesPopup)
        {
            auto routes = std::make_unique<ModRoutesPopup> (shell, popupSource);
            routes->onSelectRoute = [this] (int slot) { selectModulation (popupSource, slot); };
            routes->setSelectedSlot (modSelSlot);
            popup = std::move (routes);
        }
        else
            popup = std::make_unique<ModCurvePopup> (shell, popupSource);
    }
    else if (which == advancedPopup)
        popup = createAdvancedPopup (shell);
    else if (which >= 0 && which < frame::macroCount && shell.product().macroPopover != nullptr)
        popup = shell.product().macroPopover (which, shell);
    if (popup == nullptr)
        return;
    popupIndex = which;
    juce::Component::SafePointer<ShellEditor> safe (this);
    popup->onClose = [safe] { juce::MessageManager::callAsync ([safe] { if (safe != nullptr) safe->closePopup(); }); };
    popup->onSizeChanged = [this] { positionPopup(); };
    content.addAndMakeVisible (*popup);
    popup->toFront (false);
    modOverlay.toFront (false);
    positionPopup();
    frame::animatePopoverOpening (*popup);
    if (which >= 0 && which < frame::macroCount)
        macros[static_cast<std::size_t> (which)].label->setOpen (true);
    if (which == advancedPopup)
        advancedButton.setToggleState (true, juce::dontSendNotification);
    applyBindings();   // the popover's controls follow the engine's bindings too
    if (isShowing())
        grabKeyboardFocus();
}

void ShellEditor::positionPopup()
{
    if (popup == nullptr)
        return;
    juce::Desktop::getInstance().getAnimator().cancelAnimation (popup.get(), false);
    popup->setAlpha (1.0f);
    const auto size = frame::popoverSize (*popup);
    juce::Point<float> at;
    if (popupIndex >= 0 && popupIndex < frame::macroCount)
        at = frame::macroPopoverOrigin (macros[static_cast<std::size_t> (popupIndex)].label->getBounds().toFloat(), size);
    else if (popupIndex == advancedPopup)
        at = frame::advancedPopoverOrigin (advancedButton.getBounds().toFloat(), size);
    else
        at = frame::modulationPopoverOrigin (modPanel.getBounds().toFloat(), size);
    frame::placePopover (*popup, content, at);
}

void ShellEditor::closePopup()
{
    if (popup == nullptr)
        return;
    if (popupIndex >= 0 && popupIndex < frame::macroCount)
        macros[static_cast<std::size_t> (popupIndex)].label->setOpen (false);
    if (popupIndex == advancedPopup)
        advancedButton.setToggleState (false, juce::dontSendNotification);
    juce::Desktop::getInstance().getAnimator().cancelAnimation (popup.get(), false);
    popup.reset();
    popupIndex = -1;
}

void ShellEditor::mouseDownAnywhere (juce::Component* c)
{
    if (popup == nullptr)
    {
        closedByLabelPress = -1;
        return;
    }
    if (c == nullptr || c == popup.get() || popup->isParentOf (c))
        return;
    if (c != this && ! isParentOf (c))
        return;   // a click in the popover's own menus
    int pressed = -1;
    for (int i = 0; i < frame::macroCount; ++i)
        if (c == macros[static_cast<std::size_t> (i)].label.get())
            pressed = i;
    if (c == &advancedButton)
        pressed = advancedPopup;
    closedByLabelPress = pressed == popupIndex ? pressed : -1;
    closePopup();
}

void ShellEditor::setArpExpanded (bool open)
{
    shell.setArpEditorExpanded (open);
    if (open == arpShown)
        return;
    arpShown = open;
    arpButton.setExpanded (open);
    arpPanel.setVisible (open);
    if (open)
        arpPanel.refresh();
    layoutInstrument();
    frame::applyWindowShape (*this, instrumentHeight(), true);
    content.repaint();
}

//==============================================================================

void ShellEditor::updateStatus()
{
    juce::String status;
    switch (shell.loadState (shell.editLayer()))
    {
        case InstrumentHost::LoadState::empty: status = shownCount > 0 ? "" : "No sound loaded"; break;
        case InstrumentHost::LoadState::loading: status = juce::String::fromUTF8 ("Understanding the sound\xe2\x80\xa6"); break;
        case InstrumentHost::LoadState::ready: status = "Ready"; break;
        case InstrumentHost::LoadState::failed: status = "Could not load that file"; break;
    }
    const auto dot = juce::String::fromUTF8 (" \xc2\xb7 ");
    if (const auto message = shell.statusMessage(); message.isNotEmpty())
        status << (status.isNotEmpty() ? dot : juce::String()) << message;
    // A diagnostic engine always says what it is.
    if (const auto& engine = shell.engineDescription(); engine.diagnostic && engine.statusLine.isNotEmpty())
        status << (status.isNotEmpty() ? dot : juce::String()) << engine.statusLine;
    if (statusLabel.getText() != status)
        statusLabel.setText (status, juce::dontSendNotification);
}

void ShellEditor::timerCallback()
{
    layoutSources();
    for (auto& card : cards)
        if (card->isVisible())
            card->refresh();
    updateFocus();
    presetBar.refresh();
    if (const float wheel = shell.modWheelPosition(); ! juce::exactlyEqual (wheel, shownWheelPosition))
    {
        shownWheelPosition = wheel;
        modWheel.setValue (wheel);
    }
    updateStatus();
    if (shell.arpEditorExpanded() != arpShown)
        setArpExpanded (shell.arpEditorExpanded());
    arpButton.refresh();
    if (arpShown)
        arpPanel.refresh();
    if (shell.modPanelTab() != modPanel.tab())
        modPanel.showTab (shell.modPanelTab());
    modPanel.refresh();
    modOverlay.refresh();
    if (popup != nullptr)
        popup->refreshContent();
}

} // namespace andor::shell
