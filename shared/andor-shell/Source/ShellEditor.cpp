#include "ShellEditor.h"

#include "Binding.h"
#include "MacroPopups.h"

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

//==============================================================================
SampleDock::SampleDock()
{
    setTitle ("Add sample");
    setDescription ("Load a recording: click to browse, or drop an audio file on the instrument");
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
    setWantsKeyboardFocus (true);
}

void SampleDock::setHighlight (bool on)
{
    if (on != highlight)
    {
        highlight = on;
        repaint();
    }
}

void SampleDock::setHover (bool on)
{
    if (on != hover)
    {
        hover = on;
        repaint();
    }
}

void SampleDock::setText (const juce::String& sourceName, const juce::String& why)
{
    if (sourceName == name && why == problem)
        return;
    name = sourceName;
    problem = why;
    repaint();
}

void SampleDock::setSecondaryAction (const juce::String& label, std::function<void()> action)
{
    secondaryLabel = label;
    secondary = std::move (action);
    repaint();
}

juce::Rectangle<int> SampleDock::secondaryActionBounds() const
{
    if (secondaryLabel.isEmpty())
        return {};
    return getLocalBounds().reduced (14).removeFromBottom (44).withSizeKeepingCentre (getWidth() - 40, 28);
}

void SampleDock::mouseUp (const juce::MouseEvent& e)
{
    if (! getLocalBounds().contains (e.getPosition()))
        return;
    if (secondary != nullptr && secondaryActionBounds().contains (e.getPosition()))
        secondary();
    else if (onBrowse != nullptr)
        onBrowse();
}

bool SampleDock::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress::returnKey || key == juce::KeyPress::spaceKey)
    {
        if (onBrowse != nullptr)
            onBrowse();
        return true;
    }
    return false;
}

void SampleDock::setHeader (const juce::String& letter, int shownLayer, const juce::String& title)
{
    headerLetter = letter;
    headerLayer = shownLayer;
    headerTitle = title;
    repaint();
}

juce::Rectangle<int> SampleDock::headerBounds() const
{
    return headerLetter.isEmpty() ? juce::Rectangle<int>() : getLocalBounds().reduced (16, 0).withTop (14).withHeight (38);
}

void SampleDock::paint (juce::Graphics& g)
{
    using namespace osp::plugin;
    using namespace osp::plugin::design;
    const auto bounds = getLocalBounds().toFloat();
    if (! skin::drawnBySurface())   // ceramic: the housing draws the card (its shadow falls there)
        draw::raised (g, bounds, layout::cardRadius, colour::cardTop, colour::cardBottom);
    // Three engines: headed as a card (its badge and title), the dashed place below.
    auto area = bounds;
    if (headerLetter.isNotEmpty())
    {
        const auto header = headerBounds().toFloat();
        const auto badge = header.withWidth (38.0f);
        const auto& id = colour::identity (headerLayer);
        g.setGradientFill (juce::ColourGradient (id.badgeTop, 0.0f, badge.getY(), id.badgeBottom, 0.0f, badge.getBottom(), false));
        g.fillRoundedRectangle (badge, 6.0f);
        g.setColour (juce::Colour (0xfffff6ec));
        g.setFont (fonts::make (29.0f, fonts::Weight::regular));
        g.drawText (headerLetter, badge.translated (0.0f, 0.5f), juce::Justification::centred, false);
        g.setColour (palette::textDim);
        g.setFont (type::sourceFilename());
        g.drawText (headerTitle, header.withTrimmedLeft (50.0f), juce::Justification::centredLeft, true);
        area.removeFromTop (header.getBottom() - 4.0f);
    }
    // The recessed, dashed place a recording goes (the shell's add target, at card scale).
    const auto lit = highlight || hover || hasKeyboardFocus (false);
    const auto r = area.reduced (14.0f);
    g.setColour (highlight ? palette::recessed.interpolatedWith (palette::accent, 0.08f) : palette::recessed.interpolatedWith (palette::housing, hover ? 0.35f : 0.5f));
    g.fillRoundedRectangle (r, 10.0f);
    juce::Path outline;
    outline.addRoundedRectangle (r, 10.0f);
    juce::Path dashed;
    const float dashes[] = { 6.0f, 5.0f };
    juce::PathStrokeType (1.2f).createDashedStroke (dashed, outline, dashes, 2);
    g.setColour (lit ? palette::accent : palette::hairline.darker (0.15f));
    g.fillPath (dashed);

    auto centre = r.withSizeKeepingCentre (r.getWidth() - 16.0f, 104.0f);
    icons::draw (g, problem.isEmpty() ? icons::Kind::plus : icons::Kind::loop, centre.removeFromTop (30.0f).withSizeKeepingCentre (28.0f, 28.0f),
                 lit ? palette::accent : palette::textDim, 1.8f);
    centre.removeFromTop (10.0f);
    g.setColour (lit ? palette::text : palette::text.withAlpha (0.78f));
    g.setFont (fonts::make (14.0f, fonts::Weight::medium, 0.1f));
    g.drawText (problem.isEmpty() ? juce::String ("ADD SAMPLE") : juce::String ("TRY ANOTHER FILE"), centre.removeFromTop (20.0f), juce::Justification::centred, false);
    g.setColour (palette::textDim);
    g.setFont (type::popupLabel (11.0f));
    g.drawFittedText (problem.isEmpty() ? name + juce::String::fromUTF8 (" \xc2\xb7 DROP OR BROWSE") : problem, centre.removeFromTop (34.0f).toNearestInt(),
                      juce::Justification::centredTop, 2, 1.0f);
    if (secondaryLabel.isNotEmpty())
    {
        // The second way in, quieter: a text key at the foot of the dock.
        const auto key = secondaryActionBounds().toFloat();
        const bool over = isMouseOver (false) && key.contains (getMouseXYRelative().toFloat());
        g.setColour (palette::text.withAlpha (over ? 0.9f : 0.55f));
        g.setFont (type::popupLabel (10.5f));
        g.drawText (secondaryLabel, key, juce::Justification::centred, false);
        g.setColour (palette::hairline.withAlpha (over ? 0.9f : 0.5f));
        g.fillRect (juce::Rectangle<float> (key.getCentreX() - 0.5f * juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), secondaryLabel), key.getBottom() - 4.0f,
                                            juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), secondaryLabel), 1.0f));
    }
}

//==============================================================================
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
        // REIMAGINED's name opens ANDOR OSP's mode popover when the engine reads the amount.
        if (binding::anyBound ({ InstrumentHost::reimaginedParameterId (layer) }, p.boundParameters()))
            card->onReimagined = [this] (int l) {
                if (closedByLabelPress == reimaginedPopup + l)
                {
                    closedByLabelPress = -1;   // that press closed it
                    return;
                }
                openPopup (reimaginedPopup + l);
            };
        card->onEqToggle = [this] (int l, bool open) {
            for (auto& c : cards)
                c->setEqOpen (open && c->layer() == l);
            // The generator's in-card editor gives the well to its EQ (the card hides it) and
            // takes it back when the EQ closes, if it is pinned.
            if (l == 0 && hasWellEditor())
            {
                if (open)
                    cards[0]->setHeadingOpen (false);
                else if (shell.wellViewState().pinned)
                    showWellEditor (true);
            }
            modOverlay.toFront (false);
            applyBindings();   // the editor's fields are new controls
        };
        if (p.hasKeyDna())
            card->onKeyDna = [this] (int l) { openKeyDna (l); };
        content.addChildComponent (*card);
    }
    // A generator's card may show one of the product's controls in START's place (its name
    // opening the product's popover). The parity render keeps ANDOR OSP's card.
    if (const auto& control = shell.product().generatorStartControl; control.parameterId.isNotEmpty() && shell.isGenerator())
        cards[0]->replaceStartKnob (control.parameterId, control.caption, control.popover == nullptr ? std::function<void()>() : [this] {
            if (closedByLabelPress == startControlPopup)
            {
                closedByLabelPress = -1;   // that press closed it
                return;
            }
            openPopup (startControlPopup);
        });
    // A generator product may make its card's heading (the name beside M and S) the door to
    // its settings: its in-card editor, in the waveform's place, or a popover. Opt-in: every
    // other card's name stays plain text.
    if (hasWellEditor())
        cards[0]->setHeadingDoor ([this] { toggleWellEditor(); });
    else if (shell.product().generatorHeaderPopover != nullptr && shell.isGenerator())
        cards[0]->setHeadingDoor ([this] {
            if (closedByLabelPress == generatorHeaderPopup || popupIndex == generatorHeaderPopup)
            {
                closedByLabelPress = -1;   // that press closed it (or closes it: keyboard)
                closePopup();
                return;
            }
            openPopup (generatorHeaderPopup);
        });
    if (const auto& name = shell.product().generatorModeName; name.isNotEmpty() && shell.isGenerator())
        for (int layer = 0; layer < ShellProcessor::numLayers; ++layer)
            if (shell.isGeneratorLayer (layer))
            {
                // Each oscillator card's selector: the product's name, only as wide as it needs
                // (or, opt-in, a GRANULAR on / off key: the oscillator plays as it is unless lit).
                cards[static_cast<std::size_t> (layer)]->setFirstModeName (name);
                if (shell.generatorGranularToggle())
                    cards[static_cast<std::size_t> (layer)]->setModeToggle (true);
                else
                    cards[static_cast<std::size_t> (layer)]->setModeFitsName (true);
            }
    dropZone.onBrowse = [this] { chooseFile (-1); };
    content.addChildComponent (dropZone);
    content.addChildComponent (addTarget);
    if ((shell.product().hybridSampleDock || threeEngines()) && shell.hasHybridLayer())
    {
        if (threeEngines())
            sampleDock.setHeader (InstrumentHost::shownLayerName (ShellProcessor::hybridLayer), InstrumentHost::shownLayer (ShellProcessor::hybridLayer),
                                  shell.product().hybridSampleName);
        sampleDock.onBrowse = [this] { chooseFile (ShellProcessor::hybridLayer); };
        if (const auto& action = shell.product().hybridAction; action.label.isNotEmpty() && action.run != nullptr)
            sampleDock.setSecondaryAction (action.label.toUpperCase(), [this] { shell.product().hybridAction.run (shell); });
        sampleDock.setText (shell.product().hybridSampleName, {});
        content.addChildComponent (sampleDock);
    }
    headerMix.onModulationMenu = [this] {
        modOverlay.contextMenu (modui::mixTargetId).showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&headerMix));
    };
    content.addChildComponent (headerMix);
    // The product's other view of the sources' area (opt-in): its icon beside SAVE.
    if (shell.showsSourceView())
    {
        sourceViewButton.setTooltip (shell.product().sourceAreaView.tooltip);
        sourceViewButton.setTitle (shell.product().sourceAreaView.tooltip);
        sourceViewButton.setClickingTogglesState (false);
        sourceViewButton.onClick = [this] { setSourceViewShown (! sourceViewOpen); };
        content.addAndMakeVisible (sourceViewButton);
    }

    // The six macros (the product's): names and knobs as ANDOR OSP's.
    const bool macroPopovers = shell.product().macroPopover != nullptr;
    for (std::size_t i = 0; i < macros.size(); ++i)
    {
        frame::setUpMacro (macros[i], static_cast<int> (i), shell.macroSpecs()[i], shell.parameters, content, macroPopovers);
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
    // The effects rack (opt-in): no mark on the row - the macro's name opens its module on a
    // double-click, a right-click offers the popover or the module, Enter opens it.
    for (std::size_t i = 0; i < macros.size(); ++i)
        if (const auto kind = rackKindForMacro (shell.macroSpecs()[i].parameterId); kind.has_value() && shell.showsRack() && macros[i].label != nullptr)
        {
            rackOfMacro[i] = *kind;
            const auto k = *kind;
            const int macro = static_cast<int> (i);
            juce::Component::SafePointer<ShellEditor> safe (this);
            macros[i].label->setRackActions ([this, k] { toggleRackFromLabel (k); }, [safe, macro, k] {
                if (safe != nullptr)
                    safe->rackLabelMenu (macro, k).showMenuAsync (juce::PopupMenu::Options().withTargetComponent (safe->macros[static_cast<std::size_t> (macro)].label.get()));
            });
        }
    // A macro's own on / off (opt-in): a tiny lamp beside its name; off dims the knob's arc.
    if (shell.showsMacroSwitches())
        for (const auto& sw : shell.product().macroSwitches)
            if (auto* parameter = shell.parameters.getParameter (sw.parameterId); parameter != nullptr && sw.macro >= 0 && sw.macro < frame::macroCount)
            {
                auto lamp = std::make_unique<MacroSwitchLamp> (*parameter, design::colour::macro (sw.macro), &shell.undoManager);
                lamp->setTooltip (sw.tooltip);
                const int macro = sw.macro;
                lamp->onStateChange = [this, macro] (bool on) { dimMacroArc (macro, on); };
                dimMacroArc (macro, lamp->isOn());
                content.addAndMakeVisible (*lamp);
                macroLamps.emplace_back (macro, std::move (lamp));
            }
    setUpUserMacros();
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
    if (shell.arpCompanionShown())
    {
        arpButton.setSecondRow (shell.product().arpCompanion.name, shell.product().arpCompanion.enabledParameterId);
        arpButton.onSecondRow = [this] {
            if (popupIndex == arpCompanionPopup)
                closePopup();
            else
                openPopup (arpCompanionPopup);
        };
    }
    arpPanel.onCollapse = [this] { setArpExpanded (false); };
    arpPanel.setCrossClose (shell.crossCloseKeys());
    content.addAndMakeVisible (arpButton);
    content.addChildComponent (arpPanel);
    content.addChildComponent (rack);
    rack.setFullBleed (shell.fullBleedRacks());
    for (int k = 0; k < rack::kindCount; ++k)
        if ((shell.rackExpanded() & (1 << k)) != 0 && hasRack (static_cast<rack::Kind> (k)))
            rack.setShown (static_cast<rack::Kind> (k), true);
    for (std::size_t i = 0; i < macros.size(); ++i)
        if (rackOfMacro[i].has_value())
            macros[i].label->setRackOpen (rack.isShown (*rackOfMacro[i]));
    rack.onLayoutChanged = [this] { rackChanged(); };   // (after: the window is laid out below)
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
    modPanel.setStableTabs (shell.stableModulationTabs());
    modPanel.onTabChanged = [this] (int tab) {
        shell.setModPanelTab (tab);
        selectModulation (modui::sourceOfTab (tab), -1);
        applyBindings();   // the tab's page is shown now
    };
    modPanel.showTab (shell.modPanelTab());
    content.addAndMakeVisible (modOverlay);   // last: above everything

    juce::Desktop::getInstance().addGlobalMouseListener (&outsideClicks);

    content.onPaint = [this] (juce::Graphics& g) { frame::paint (g, content, shell.product().identity, arpShown, bankShown == 1 ? userRow : macros, rackShift(), sourceTrim()); };
    addAndMakeVisible (content);
    layoutInstrument();
    setResizable (true, true);
    frame::applyWindowShape (*this, instrumentHeight(), false);
    constexpr float defaultScale = 1086.0f / design::width;
    setSize (juce::roundToInt (defaultScale * design::width), juce::roundToInt (defaultScale * instrumentHeight()));
    applyBindings();
    timerCallback();
    // A pinned in-card editor is open when the window opens.
    if (hasWellEditor() && shell.wellViewState().pinned)
        showWellEditor (true);
    startTimerHz (30);
}

ShellEditor::~ShellEditor()
{
    stopTimer();
    juce::Desktop::getInstance().removeGlobalMouseListener (&outsideClicks);
    for (auto& card : cards)
        juce::Desktop::getInstance().getAnimator().cancelAnimation (card.get(), false);
    popup.reset();
    keyDnaPanel.reset();
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
    if (bankTab != nullptr)
        assignOverlay.setBounds (content.getLocalBounds());   // (USER's rings and banner: the whole instrument)
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
    if (shell.showsRack())
    {
        parts.rack = &rack;
        parts.rackShift = rackShift();
    }
    parts.sourceTrim = sourceTrim();
    frame::place (parts, arpShown);
    if (shell.showsSourceView())
    {
        // The icon beside SAVE, on its centre line (12 px from it), and MIX moved right by as
        // much, narrower by as much (it centres itself), clear of VOLUME.
        auto save = savePresetButton.getBounds();
        if (! shell.headerMixShown())
        {
            // No MIX: [ preset bar ][ SAVE ][ ROUTING ] as one group of keys on one centre
            // line, 8 px apart, the bar taking MIX's room up to VOLUME (28 px clear of it).
            const int gap = 8, key = save.getWidth();
            const int right = volume.getX() - 28;
            sourceViewButton.setBounds (save.withX (right - key));
            save = save.withX (right - key - gap - key);
            savePresetButton.setBounds (save);
            presetBar.setBounds (presetBar.getBounds().withRight (save.getX() - gap));
        }
        else
        {
            sourceViewButton.setBounds (juce::Rectangle<int> (30, 30).withCentre ({ save.getRight() + 12 + 15, save.getCentreY() }));
            const auto mix = headerMix.getBounds();
            const int shift = sourceViewButton.getRight() + 15 - mix.getX();
            if (shift > 0)
                headerMix.setBounds (mix.withTrimmedLeft (shift));
        }
    }
    placeMacroLamps();
    placeUserMacros();
    modOverlay.setBounds (content.getLocalBounds());
    sourceArea = frame::sourceArea (sourceTrim());
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

bool ShellEditor::threeEngines() const
{
    return shell.product().threeEngines.enabled && shell.hasHybridLayer();
}

void ShellEditor::setSourceViewShown (bool shown)
{
    if (! shell.showsSourceView() || shown == sourceViewOpen)
        return;
    if (shown && sourceView == nullptr)
    {
        sourceView = shell.product().sourceAreaView.create (shell);
        if (sourceView == nullptr)
            return;
        content.addChildComponent (*sourceView);
    }
    sourceViewOpen = shown;
    sourceViewButton.setToggleState (shown, juce::dontSendNotification);
    sourceViewButton.repaint();
    shownCount = -1;
    layoutSources();
    if (shown && sourceView != nullptr)
        sourceView->grabKeyboardFocus();
}

void ShellEditor::SourceViewButton::paintButton (juce::Graphics& g, bool over, bool down)
{
    // A header key as SAVE (its face, its icon size and ink): a branching path, two inputs
    // joining; the path green while its view is shown.
    using namespace osp::plugin::design;
    const auto r = getLocalBounds().toFloat();
    const bool on = getToggleState();
    const float k = r.getHeight() / 48.0f;
    if (! osp::plugin::skin::drawnBySurface())   // ceramic: the housing draws the key
        draw::raised (g, r.reduced (0.5f), 9.0f * k, colour::cardTop, colour::cardBottom, 0.9f);
    if (on)
    {
        g.setColour (juce::Colour (0x24b4cab5));
        g.fillRoundedRectangle (r.reduced (5.0f * k), 6.0f * k);
    }
    const float s = 27.0f * k / 24.0f;
    const auto o = r.getCentre() - juce::Point<float> (12.0f * s, 12.0f * s) + juce::Point<float> (0.0f, down ? 0.5f : 0.0f);
    auto at = [&] (float x, float y) { return o + juce::Point<float> (x * s, y * s); };
    juce::Path p;
    for (const auto c : { at (5, 5), at (19, 12), at (5, 19) })
        p.addEllipse (juce::Rectangle<float> (4.0f * s, 4.0f * s).withCentre (c));
    p.startNewSubPath (at (7, 5));
    p.lineTo (at (11, 5));
    p.cubicTo (at (13, 5), at (13, 12), at (17, 12));
    p.startNewSubPath (at (7, 19));
    p.lineTo (at (11, 19));
    p.cubicTo (at (13, 19), at (13, 12), at (17, 12));
    g.setColour (on ? juce::Colour (0xff477767) : colour::text.withAlpha (over || down ? 0.85f : 0.6f));
    g.strokePath (p, juce::PathStrokeType (1.5f * k, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    if (hasKeyboardFocus (false))
    {
        g.setColour (juce::Colour (0xff88a8af));
        g.drawRoundedRectangle (r.reduced (1.5f), 9.0f * k, 1.5f);
    }
}

void ShellEditor::layoutSources()
{
    if (sourceView != nullptr)
        sourceView->setVisible (sourceViewOpen);
    if (sourceViewOpen && sourceView != nullptr)
    {
        // The product's view in the cards' place (the sound is the same either way).
        for (auto& card : cards)
            card->setVisible (false);
        sampleDock.setVisible (false);
        dropZone.setVisible (false);
        addTarget.setVisible (false);
        sourceView->setBounds (sourceArea);
        shownCount = -1;   // (the cards are laid out afresh when it closes)
        return;
    }
    if (threeEngines())
    {
        // Three engines: every card, always, at equal widths, in the product's order; the
        // hybrid, while empty, is its ADD SAMPLE invitation in its own column.
        const bool hybrid = shell.isLayerOccupied (ShellProcessor::hybridLayer);
        if (hybrid == shownHybrid && shownCount == 3)
            return;
        shownCount = 3;
        shownHybrid = hybrid;
        dropZone.setVisible (false);
        addTarget.setVisible (false);
        const auto& order = shell.product().threeEngines.order;
        for (int shown = 0; shown < 3; ++shown)
        {
            const int l = order[static_cast<std::size_t> (shown)];
            auto& card = *cards[static_cast<std::size_t> (l)];
            const auto column = frame::sourceColumn (sourceArea, 3, shown);
            if (l == ShellProcessor::hybridLayer && ! hybrid)
            {
                card.setVisible (false);
                sampleDock.setBounds (column);
                sampleDock.setVisible (true);
                continue;
            }
            if (l == ShellProcessor::hybridLayer)
                sampleDock.setVisible (false);
            card.setDensity (EngineLayoutDensity::triple);
            card.setBounds (column);
            card.setVisible (true);
        }
        if (dockOverlay != nullptr)
            dockOverlay->setBounds (sampleDock.getLocalBounds().reduced (14).withTrimmedTop (sampleDock.headerBounds().getBottom()));
        headerMix.setLayers ({ true, hybrid && shell.headerMixShown(), false });   // the analog side against the hybrid (none: no MIX)
        headerMix.setEndLabels (shell.product().mixEndLabels[0], shell.product().mixEndLabels[1]);
        updateFocus();
        applyBindings();
        content.repaint();
        return;
    }
    const int count = shell.slotCount();
    if (count == shownCount && ! dragging)
        return;
    shownCount = count;
    std::array<bool, InstrumentHost::numLayers> occupied {};
    for (int l = 0; l < InstrumentHost::numLayers; ++l)
        occupied[static_cast<std::size_t> (l)] = l < count;
    // A hybrid product's dock (opt-in): the generator's card and, beside it, the empty
    // recording's compact dock (about a fifth of the area); it is the drop target itself.
    const bool dock = sampleDockShown();
    const bool showAdd = dragging && count >= 1 && count < InstrumentHost::numLayers && ! dock;
    const int columns = count + (showAdd ? 1 : 0);
    const auto density = columns <= 1 ? EngineLayoutDensity::hero : (columns == 2 ? EngineLayoutDensity::dual : EngineLayoutDensity::triple);
    dropZone.setVisible (count == 0);
    dropZone.setBounds (sourceArea);
    auto cardArea = sourceArea;
    if (dock)
    {
        constexpr int gap = 12;
        const int dockWidth = juce::roundToInt (0.2f * static_cast<float> (sourceArea.getWidth()));
        sampleDock.setBounds (cardArea.removeFromRight (dockWidth));
        cardArea.removeFromRight (gap);
    }
    sampleDock.setVisible (dock);
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
        card.setBounds (frame::sourceColumn (cardArea, columns, col++));
        card.setVisible (true);
    }
    addTarget.setVisible (showAdd);
    if (showAdd)
    {
        addTarget.setBounds (frame::sourceColumn (sourceArea, columns, col));
        addTarget.setLetter (InstrumentHost::layerName (count));
    }
    if (! shell.headerMixShown())
        occupied = { true, false, false };   // (no MIX: nothing to balance in the header)
    headerMix.setLayers (occupied);
    if (isReimaginedPopup (popupIndex))
    {
        if (! cards[static_cast<std::size_t> (popupIndex - reimaginedPopup)]->isVisible())
            closePopup();     // its layer is gone
        else
            positionPopup();  // its card moved
    }
    updateFocus();
    applyBindings();
    content.repaint();
}

bool ShellEditor::sampleDockShown() const
{
    if (threeEngines())
        return ! shell.isLayerOccupied (ShellProcessor::hybridLayer);
    return shell.product().hybridSampleDock && shell.hasHybridLayer() && ! shell.isLayerOccupied (ShellProcessor::hybridLayer);
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
        popup->settleNow();
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
    if (shell.isGenerator() && ! shell.hasHybridLayer())
        return false;   // it makes its own sound: there is nowhere to drop a recording
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
    sampleDock.setHighlight (true);   // (a hybrid product: any drop loads the recording)
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
    sampleDock.setHighlight (false);
    shownCount = -1;
    layoutSources();
}

void ShellEditor::filesDropped (const juce::StringArray& files, int x, int y)
{
    const int layer = dropLayerAt ({ x, y });
    dragging = false;
    dropZone.setHighlight (false);
    sampleDock.setHighlight (false);
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
    if (shell.hasHybridLayer() && layer == ShellProcessor::hybridLayer)
    {
        // The generator's recording: replaced (or relinked, when its file went missing) or removed.
        menu.addSectionHeader (shell.product().hybridSampleName);
        menu.addItem (juce::String::fromUTF8 ("Replace or relink recording\xe2\x80\xa6"), [this] { chooseFile (ShellProcessor::hybridLayer); });
        menu.addItem ("Remove recording", [this] { shell.removeLayer (ShellProcessor::hybridLayer); timerCallback(); });
        if (const auto& action = shell.product().hybridAction; action.label.isNotEmpty() && action.run != nullptr)
            menu.addItem (action.label + juce::String::fromUTF8 ("\xe2\x80\xa6"), [this] { shell.product().hybridAction.run (shell); });
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&target));
        return;
    }
    if (shell.isGenerator())
    {
        // The oscillators are the instrument: nothing to replace or remove.
        menu.addSectionHeader (shell.generatedSourceName());
        menu.addItem ("Init", [this] { shell.initPatch(); timerCallback(); });
        menu.addItem ("Reset settings", [this] { shell.resetSettings(); timerCallback(); });
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&target));
        return;
    }
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
    if (! shell.isGenerator())
    {
        menu.addItem (juce::String::fromUTF8 ("Add sound\xe2\x80\xa6"), shell.slotCount() < InstrumentHost::numLayers, false, [this] { chooseFile (-1); });
        menu.addSeparator();
    }
    else if (shell.hasHybridLayer())
    {
        menu.addItem (shell.product().hybridSampleName + juce::String::fromUTF8 (": load a recording\xe2\x80\xa6"), [this] { chooseFile (ShellProcessor::hybridLayer); });
        menu.addSeparator();
    }
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
    if (key == juce::KeyPress::escapeKey && (pickMacro >= 0 || dragMacro >= 0))
    {
        cancelUserMacroAssign();
        return true;
    }
    if (key == juce::KeyPress::escapeKey && popup != nullptr)
    {
        closePopup();
        return true;
    }
    if (key == juce::KeyPress::escapeKey && isWellEditorShown())
    {
        toggleWellEditor();   // (as its heading: back to the waveform)
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
    headerMix.setModulationDrag (source, headerMix.acceptsModulationDrop (where));
    // As ANDOR OSP's editor: a Granular card's display, rested on (not passed across), brings
    // up its POS SIZE DENS TUNE SPREAD so the drag can land on them (the pointer's own hover
    // never reaches the card while a source is being dragged).
    int over = -1;
    for (int l = 0; l < ShellProcessor::numLayers; ++l)
    {
        auto& card = *cards[static_cast<std::size_t> (l)];
        if (card.isVisible() && card.isGranular() && ! card.granularControlsShown() && ! card.isEqOpen()
            && content.getLocalArea (&card, card.display().getBounds()).toFloat().contains (where))
            over = l;
    }
    if (over != dragHoverCard)
    {
        dragHoverCard = over;
        dragHoverSince = juce::Time::getMillisecondCounter();
    }
    else if (over >= 0 && juce::Time::getMillisecondCounter() - dragHoverSince > 380)
    {
        cards[static_cast<std::size_t> (over)]->showGranularControls (true, true);
        dragHoverCard = -1;
        modOverlay.setDrag (-1, where);   // collect the knobs now shown
        modOverlay.setDrag (source, where);
    }
}

void ShellEditor::dropModulation (int source, juce::Point<float> where)
{
    // A control the engine does not read (shown disabled) is no destination.
    if (auto* control = modOverlay.controlAt (where); control != nullptr && ! control->isEnabled())
    {
        headerMix.setModulationDrag (-1, false);
        modOverlay.setDrag (-1, {});
        modOverlay.showNotice ("This instrument does not use that control");
        return;
    }
    modOverlay.setDrag (source, where);
    const bool onMix = headerMix.acceptsModulationDrop (where);
    headerMix.setModulationDrag (-1, false);
    if ((onMix ? modOverlay.dropOn (source, modui::mixTargetId, headerMix.getBounds().toFloat()) : modOverlay.drop (source, where)) >= 0)
        modPanel.refresh();
    dragHoverCard = -1;
    modOverlay.setDrag (-1, {});
}

void ShellEditor::openPopup (int which, int source)
{
    closePopup();
    if (which == routesPopup || which == curvePopup)
    {
        // LFO 1 .. ENV 2, VEL and NOTE (their curves, their routes); the wheel's routes are on
        // its controls.
        if (source < 0 || (source > 3 && osp::plugin::modui::keyCurveIndex (source) < 0))
            return;
        popupSource = source;
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
    else if (which >= reimaginedPopup && which < reimaginedPopup + InstrumentHost::numLayers)
    {
        if (! cards[static_cast<std::size_t> (which - reimaginedPopup)]->isVisible())
            return;
        popup = createReimaginedPopup (shell, which - reimaginedPopup);
    }
    else if (which >= 0 && which < frame::macroCount && shell.product().macroPopover != nullptr)
        popup = shell.product().macroPopover (which, shell);
    else if (which == startControlPopup && shell.product().generatorStartControl.popover != nullptr)
        popup = shell.product().generatorStartControl.popover (shell);
    else if (which == generatorHeaderPopup && shell.product().generatorHeaderPopover != nullptr && cards[0]->hasHeadingDoor())
        popup = shell.product().generatorHeaderPopover (shell);
    else if (which == hybridHeaderPopup && shell.product().hybridHeaderPopover != nullptr && cards[ShellProcessor::hybridLayer]->hasHeadingDoor())
        popup = shell.product().hybridHeaderPopover (shell);
    else if (which == arpCompanionPopup && arpButton.hasSecondRow() && shell.product().arpCompanion.popover != nullptr)
        popup = shell.product().arpCompanion.popover (shell);
    if (popup == nullptr)
        return;
    presentPopup (which);
}

void ShellEditor::presentPopup (int which)
{
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
    if (isReimaginedPopup (which))
        cards[static_cast<std::size_t> (which - reimaginedPopup)]->setReimaginedOpen (true);
    if (which == startControlPopup)
        cards[0]->setStartOpen (true);
    if (which == generatorHeaderPopup)
        cards[0]->setHeadingOpen (true);
    if (which == hybridHeaderPopup)
        cards[ShellProcessor::hybridLayer]->setHeadingOpen (true);
    if (which == arpCompanionPopup)
        arpButton.setSecondRowOpen (true);
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
    else if (isReimaginedPopup (popupIndex))
    {
        // A layer's REIMAGINED popover unfolds above that card's REIMAGINED name (as in ANDOR OSP).
        const auto& card = *cards[static_cast<std::size_t> (popupIndex - reimaginedPopup)];
        const auto anchor = content.getLocalArea (&card, card.reimaginedLabelBounds()).toFloat();
        at = { anchor.getCentreX() - 0.5f * size.x, anchor.getY() - 10.0f - size.y };
    }
    else if (popupIndex == startControlPopup)
    {
        // Above the card knob's name, as REIMAGINED's popover above its own.
        const auto anchor = content.getLocalArea (cards[0].get(), cards[0]->startLabelBounds()).toFloat();
        at = { anchor.getCentreX() - 0.5f * size.x, anchor.getY() - 10.0f - size.y };
    }
    else if (popupIndex == generatorHeaderPopup || popupIndex == hybridHeaderPopup)
    {
        // Below the card's heading, starting at its first letter (placePopover keeps it inside).
        const auto& card = *cards[popupIndex == hybridHeaderPopup ? static_cast<std::size_t> (ShellProcessor::hybridLayer) : 0u];
        const auto anchor = content.getLocalArea (&card, card.headingBounds()).toFloat();
        at = { anchor.getX() - 14.0f, anchor.getBottom() + 8.0f };
    }
    else if (popupIndex == anchoredPopup && popupAnchor != nullptr)
    {
        // Under its anchor, right edges aligned (placePopover keeps it inside the instrument); a
        // USER macro's panel above its name, its left edge with the name's (as the macros' own).
        const auto anchor = content.getLocalArea (popupAnchor.getComponent(), popupAnchor->getLocalBounds()).toFloat();
        at = anchorAbove ? juce::Point<float> { anchor.getX() - 14.0f, anchor.getY() - 10.0f - size.y } : juce::Point<float> { anchor.getRight() + 14.0f - size.x, anchor.getBottom() + 8.0f };
    }
    else if (popupIndex == arpCompanionPopup)
    {
        // Above the housing, its left edge with the housing's (placePopover keeps it inside).
        const auto anchor = arpButton.getBounds().toFloat();
        at = { anchor.getX(), anchor.getY() - 10.0f - size.y };
    }
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
    if (isReimaginedPopup (popupIndex))
        cards[static_cast<std::size_t> (popupIndex - reimaginedPopup)]->setReimaginedOpen (false);
    if (popupIndex == startControlPopup)
        cards[0]->setStartOpen (false);
    if (popupIndex == generatorHeaderPopup)
        cards[0]->setHeadingOpen (false);
    if (popupIndex == hybridHeaderPopup)
        cards[ShellProcessor::hybridLayer]->setHeadingOpen (false);
    if (popupIndex == arpCompanionPopup)
        arpButton.setSecondRowOpen (false);
    if (popupIndex == anchoredPopup)
    {
        content.removeChildComponent (&popupBackdrop);
        popupAnchor = nullptr;
        anchorAbove = false;
        panelMacro = -1;
    }
    juce::Desktop::getInstance().getAnimator().cancelAnimation (popup.get(), false);
    popup.reset();
    popupIndex = -1;
}

void ShellEditor::openAnchoredPopover (std::unique_ptr<osp::plugin::MiniPanel> panel, juce::Component& anchor)
{
    closePopup();
    if (panel == nullptr || ! isParentOf (&anchor))
        return;
    popup = std::move (panel);
    popupAnchor = &anchor;
    // The backdrop: the click that closes the panel goes nowhere else.
    juce::Component::SafePointer<ShellEditor> safe (this);
    popupBackdrop.onPress = [safe] { juce::MessageManager::callAsync ([safe] { if (safe != nullptr) safe->closePopup(); }); };
    popupBackdrop.setBounds (content.getLocalBounds());
    content.addAndMakeVisible (popupBackdrop);
    presentPopup (anchoredPopup);
}

bool ShellEditor::anchoredPopoverOpenFrom (const juce::Component& anchor) const
{
    return popup != nullptr && popupIndex == anchoredPopup && popupAnchor.getComponent() == &anchor;
}

void ShellEditor::closeAnchoredPopover()
{
    if (popupIndex == anchoredPopup)
        closePopup();
}

//==============================================================================
// The generator's in-card editor (ProductInfo::generatorWellEditor)

bool ShellEditor::hasWellEditor() const
{
    return shell.product().generatorWellEditor != nullptr && shell.isGenerator();
}

bool ShellEditor::isWellEditorShown() const
{
    return hasWellEditor() && cards[0]->isWellEditorShown();
}

void ShellEditor::showWellEditor (bool show)
{
    if (! hasWellEditor())
        return;
    auto& card = *cards[0];
    if (show && card.wellEditorComponent() == nullptr)
    {
        // Made the first time it is asked for (never at start-up unless pinned), then kept:
        // closing it only hides it.
        auto editor = shell.product().generatorWellEditor (shell);
        if (editor == nullptr)
            return;
        editor->setViewState (shell.wellViewState());
        juce::Component::SafePointer<ShellEditor> safe (this);
        editor->onShowWaveform = [safe] {
            if (safe != nullptr)
                safe->toggleWellEditor();
        };
        auto* raw = editor.get();
        editor->onViewStateChanged = [safe, raw] {
            if (safe != nullptr)
                safe->shell.setWellViewState (raw->viewState());
        };
        card.setWellEditor (std::move (editor));
    }
    if (show && card.wellEditorComponent() != nullptr)
        card.wellEditorComponent()->setViewState (shell.wellViewState());
    card.setWellEditorShown (show);
    card.setHeadingOpen (show && card.isWellEditorShown());
    modOverlay.toFront (false);
    applyBindings();   // its controls, enabled as the engine reads them
}

void ShellEditor::toggleWellEditor()
{
    if (! isWellEditorShown())
    {
        showWellEditor (true);
        return;
    }
    // The musician's own "back to the waveform" outranks the pin: it unpins.
    auto state = shell.wellViewState();
    state.pinned = false;
    shell.setWellViewState (state);
    if (auto* editor = cards[0]->wellEditorComponent())
        editor->setViewState (state);
    showWellEditor (false);
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
    if (popupIndex == anchoredPopup && popupAnchor != nullptr && (c == popupAnchor.getComponent() || popupAnchor->isParentOf (c)))
        return;   // (its anchor: it opened the panel in this very press - JUCE tells this watcher after the
                  // key - or closes it itself; closing here too shut every anchored panel as it opened)
    if (c == &arpButton && popupIndex == arpCompanionPopup
        && arpButton.secondRowArea().contains (arpButton.getLocalPoint (nullptr, juce::Desktop::getMousePosition().toFloat())))
        return;   // (its own row: the row opened it, or switches its instrument with it open)
    int pressed = -1;
    for (int i = 0; i < frame::macroCount; ++i)
        if (c == macros[static_cast<std::size_t> (i)].label.get())
            pressed = i;
    if (c == &advancedButton)
        pressed = advancedPopup;
    for (int l = 0; l < InstrumentHost::numLayers; ++l)
        if (const auto& card = *cards[static_cast<std::size_t> (l)]; card.isReimaginedLabel (c)
            && card.reimaginedLabelBounds().contains (card.getLocalPoint (nullptr, juce::Desktop::getMousePosition())))
            pressed = reimaginedPopup + l;
    if (const auto& card = *cards[0]; card.isStartLabel (c) && card.startLabelBounds().contains (card.getLocalPoint (nullptr, juce::Desktop::getMousePosition())))
        pressed = startControlPopup;
    if (cards[0]->isHeading (c))
        pressed = generatorHeaderPopup;
    if (cards[ShellProcessor::hybridLayer]->isHeading (c) && hybridDoorSet)
        pressed = hybridHeaderPopup;
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
    placeKeyDna();
    content.repaint();
}

std::optional<rack::Kind> ShellEditor::rackKindForMacro (const juce::String& parameterId) const
{
    for (const auto& r : shell.product().productRacks)
        if (r.macroParameterId == parameterId)
            return r.kind;
    if (const auto kind = rack::kindForMacro (parameterId);
        kind.has_value() && std::find (shell.product().rackModules.begin(), shell.product().rackModules.end(), *kind) != shell.product().rackModules.end())
        return kind;
    return std::nullopt;
}

std::unique_ptr<rack::Module> ShellEditor::createRackModule (rack::Kind kind)
{
    for (const auto& r : shell.product().productRacks)
        if (r.kind == kind && r.create != nullptr)
            return r.create (shell);
    return osp::plugin::createRackModule (kind, shell);
}

bool ShellEditor::hasRack (rack::Kind kind) const noexcept
{
    if (rackMacro (kind) >= 0)
        return true;
    // A product's rack that no macro opens (ShellProduct::ProductRack with no macro: its own view
    // opens it, as ANDOR ANALOG's FX ROUTING opens its effects' racks).
    if (! shell.showsRack())
        return false;
    for (const auto& r : shell.product().productRacks)
        if (r.kind == kind && r.macroParameterId.isEmpty())
            return true;
    return false;
}

int ShellEditor::rackMacro (rack::Kind kind) const noexcept
{
    for (std::size_t i = 0; i < rackOfMacro.size(); ++i)
        if (rackOfMacro[i] == kind)
            return static_cast<int> (i);
    return -1;
}

void ShellEditor::toggleRackFromLabel (rack::Kind kind)
{
    // (A popover in the way closes; a press that closed it must not count later.)
    closedByLabelPress = -1;
    closePopup();
    setRackExpanded (kind, ! rack.isShown (kind));
}

juce::PopupMenu ShellEditor::rackLabelMenu (int macro, rack::Kind kind)
{
    juce::PopupMenu menu;
    juce::Component::SafePointer<ShellEditor> safe (this);
    if (shell.product().macroPopover != nullptr)
        menu.addItem ("Quick Settings", [safe, macro] {
            if (safe != nullptr)
            {
                safe->closedByLabelPress = -1;
                safe->openPopup (macro);
            }
        });
    menu.addItem (rack.isShown (kind) ? "Close Rack" : "Open Rack", [safe, kind] { if (safe != nullptr) safe->toggleRackFromLabel (kind); });
    return menu;
}

int ShellEditor::rackMask() const
{
    int mask = 0;
    for (const auto kind : rack.shownKinds())
        mask |= 1 << static_cast<int> (kind);
    return mask;
}

void ShellEditor::setRackExpanded (rack::Kind kind, bool open)
{
    if (open && ! hasRack (kind))
        return;
    rack.setShown (kind, open);   // (rackChanged follows)
}

void ShellEditor::rackChanged()
{
    // A view change only: the window grows or shrinks by the rack, nothing sounds different.
    shell.setRackExpanded (rackMask());
    for (std::size_t i = 0; i < macros.size(); ++i)
        if (rackOfMacro[i].has_value())
            macros[i].label->setRackOpen (rack.isShown (*rackOfMacro[i]));
    rack.settle();
    layoutInstrument();
    frame::applyWindowShape (*this, instrumentHeight(), true);
    placeKeyDna();
    content.repaint();
}

void ShellEditor::openKeyDna (int layer)
{
    // As ANDOR OSP's editor opens it.
    closePopup();
    if (keyDnaPanel != nullptr && keyDnaPanel->layer() != layer)
        keyDnaPanel.reset();
    if (keyDnaPanel == nullptr)
    {
        keyDnaPanel = std::make_unique<osp::plugin::KeyDnaEditor> (shell, layer);
        juce::Component::SafePointer<ShellEditor> safe (this);
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
        keyDnaPanel->isKeyDown = [safe] (int note) { return safe != nullptr && safe->shell.keyboardState.isNoteOnForChannels (0xffff, note); };
        keyDnaPanel->getProperties().set ("modCover", true);   // the halos stay under it
        content.addAndMakeVisible (*keyDnaPanel);
    }
    if (shell.editLayer() != layer)
    {
        shell.setEditLayer (layer);
        updateFocus();
    }
    placeKeyDna();
    keyDnaPanel->toFront (true);
    modOverlay.toFront (false);
}

void ShellEditor::closeKeyDna()
{
    if (keyDnaPanel == nullptr)
        return;
    keyDnaPanel.reset();
    grabKeyboardFocus();
}

void ShellEditor::placeKeyDna()
{
    // ANDOR OSP's place: the instrument's width, from the housing's top to just above the
    // keyboard row (which stays visible and playable).
    if (keyDnaPanel != nullptr)
        keyDnaPanel->setBounds (juce::Rectangle<float> (31.0f, 23.0f, 1386.0f, static_cast<float> (keyboard.getY()) - 9.0f - 23.0f).getSmallestIntegerContainer());
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

void ShellEditor::updateHybridDoor()
{
    if (shell.product().hybridHeaderPopover == nullptr || ! shell.hasHybridLayer())
        return;
    InstrumentHost::GeneratedSource source;
    const bool engineSource = shell.generatedSource (ShellProcessor::hybridLayer, source);
    if (engineSource == hybridDoorSet)
        return;
    hybridDoorSet = engineSource;
    if (! engineSource)
    {
        if (popupIndex == hybridHeaderPopup)
            closePopup();
        cards[ShellProcessor::hybridLayer]->setHeadingDoor (nullptr);
        return;
    }
    cards[ShellProcessor::hybridLayer]->setHeadingDoor ([this] {
        if (closedByLabelPress == hybridHeaderPopup || popupIndex == hybridHeaderPopup)
        {
            closedByLabelPress = -1;
            closePopup();
            return;
        }
        openPopup (hybridHeaderPopup);
    });
}

void ShellEditor::refreshCardExtensions()
{
    // A generator product's card extensions (the parity render's test engine keeps ANDOR OSP's cards).
    if (! shell.isGenerator())
        return;
    const auto& product = shell.product();
    bool changed = false;
    for (int l = 0; l < InstrumentHost::numLayers; ++l)
    {
        auto& card = *cards[static_cast<std::size_t> (l)];
        if (product.cardControls != nullptr)
        {
            const auto knobs = product.cardControls (l, shell);
            juce::String key;
            for (const auto& k : knobs)
                key << (k.hidden ? juce::String ("-") : k.parameterId) << "|" << k.caption << ";";
            if (key != appliedKnobs[static_cast<std::size_t> (l)])
            {
                appliedKnobs[static_cast<std::size_t> (l)] = key;
                for (int i = 0; i < 5; ++i)
                {
                    const auto& k = knobs[static_cast<std::size_t> (i)];
                    if (k.hidden)
                        card.replaceKnob (i, {}, {});
                    else if (k.parameterId.isEmpty())
                        card.restoreKnob (i);
                    else
                        card.replaceKnob (i, k.parameterId, k.caption);
                }
                changed = true;
            }
        }
        if (product.cardModifiersShown != nullptr)
            card.setModifiersShown (product.cardModifiersShown (l, shell));
        card.setModifiersInWell (shell.modifiersInWell());
        if (shell.modifiersInWell() && product.cardModifiersApplicable != nullptr)
            card.setModifiersApplicable (product.cardModifiersApplicable (l, shell));
        if (product.wellOverlay != nullptr)
        {
            const auto key = product.wellOverlayKey != nullptr ? product.wellOverlayKey (l, shell) : juce::String ("-");
            if (key != appliedOverlayKeys[static_cast<std::size_t> (l)])
            {
                appliedOverlayKeys[static_cast<std::size_t> (l)] = key;
                card.setWellOverlay (product.wellOverlay (l, shell));
                changed = true;
            }
        }
    }
    // The empty hybrid's dock carries its overlay too (its source type, say).
    if (product.wellOverlay != nullptr && threeEngines())
    {
        const auto key = (product.wellOverlayKey != nullptr ? product.wellOverlayKey (ShellProcessor::hybridLayer, shell) : juce::String ("-")) + "/dock";
        if (key != dockOverlayKey)
        {
            dockOverlayKey = key;
            dockOverlay = product.wellOverlay (ShellProcessor::hybridLayer, shell);
            if (dockOverlay != nullptr)
            {
                sampleDock.addAndMakeVisible (*dockOverlay);
                dockOverlay->setBounds (sampleDock.getLocalBounds().reduced (14).withTrimmedTop (sampleDock.headerBounds().getBottom()));
            }
            changed = true;
        }
    }
    if (changed)
    {
        modOverlay.toFront (false);
        applyBindings();
    }
}

void ShellEditor::timerCallback()
{
    updateUserMacroArcs();
    refreshCardExtensions();
    layoutSources();
    updateHybridDoor();
    if (sampleDock.isVisible())
    {
        // After a failed load: what went wrong, and that another file can be tried (the
        // sound as it was is untouched).
        const bool failed = shell.loadState (ShellProcessor::hybridLayer) == InstrumentHost::LoadState::failed;
        sampleDock.setText (shell.product().hybridSampleName, failed ? juce::String ("COULD NOT LOAD THAT FILE\nCLICK TO CHOOSE ANOTHER") : juce::String());
    }
    for (auto& card : cards)
        if (card->isVisible())
            card->refresh();
    updateFocus();
    if (keyDnaPanel != nullptr)
        keyDnaPanel->refresh();
    headerMix.refreshModulation();
    presetBar.refresh();
    if (const float wheel = shell.modWheelPosition(); ! juce::exactlyEqual (wheel, shownWheelPosition))
    {
        shownWheelPosition = wheel;
        modWheel.setValue (wheel);
    }
    {
        // The AMP envelope's picture follows its knobs (and automation), as in ANDOR OSP: it
        // repaints when one of them moved.
        const std::array<float, 4> now { shell.parameterValue ("attack"), shell.parameterValue ("decay"),
                                         shell.parameterValue ("sustainLevel"), shell.parameterValue ("release") };
        if (now != shownEnvelope)
            modPanel.ampEnvelope().repaint (modPanel.ampEnvelope().graphBounds());
        shownEnvelope = now;
        if (shell.product().ampEnvelopeNote != nullptr)
            modPanel.ampEnvelope().setNote (shell.product().ampEnvelopeNote (shell));
    }
    updateStatus();
    if (shell.arpEditorExpanded() != arpShown)
        setArpExpanded (shell.arpEditorExpanded());
    if (shell.showsRack())
    {
        // A session recalled with another rack open (the view setting it saved).
        if (const int wanted = shell.rackExpanded(); wanted != rackMask())
            for (int k = 0; k < rack::kindCount; ++k)
                if (hasRack (static_cast<rack::Kind> (k)))
                    rack.setShown (static_cast<rack::Kind> (k), (wanted & (1 << k)) != 0);
        rack.refresh();
    }
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

//==============================================================================
// The macros' own on / off lamps (ProductInfo::macroSwitches)

MacroSwitchLamp* ShellEditor::macroSwitch (int macro) noexcept
{
    for (auto& [m, lamp] : macroLamps)
        if (m == macro)
            return lamp.get();
    return nullptr;
}

void ShellEditor::placeMacroLamps()
{
    // Immediately left of the name, on its centre line: the name and knob stay exactly where
    // the macro row puts them (the lamp takes no room of theirs).
    for (auto& [macro, lamp] : macroLamps)
    {
        auto* label = macros[static_cast<std::size_t> (macro)].label.get();
        if (label == nullptr)
            continue;
        const auto r = label->getBounds().toFloat();
        constexpr float target = 26.0f, gap = 4.0f;
        const float centreX = r.getCentreX() - 0.5f * label->textWidth() - gap - 7.0f;
        lamp->setBounds (juce::Rectangle<float> (target, target).withCentre ({ centreX, r.getCentreY() }).toNearestInt());
    }
}

void ShellEditor::dimMacroArc (int macro, bool on)
{
    // Off: the knob's colour arc fades (its value and the knob itself as they are).
    auto& slider = macros[static_cast<std::size_t> (macro)].slider;
    const auto colour = design::colour::macro (macro);
    const auto shown = on ? colour : colour.interpolatedWith (design::colour::hairline, 0.62f);
    slider.getProperties().set ("arc", static_cast<juce::int64> (shown.getARGB()));
    slider.repaint();
}

//==============================================================================
// USER MACROS V1.1 (ProductInfo::userMacros)

juce::Colour ShellEditor::userMacroColour (int macro) const
{
    return design::colour::macro (std::clamp (macro, 0, frame::macroCount - 1));
}

void ShellEditor::setUpUserMacros()
{
    const auto* um = shell.userMacros();
    if (um == nullptr)
        return;
    // The second bank: six knobs in the very places of the first (the same knob, colours and
    // light), hidden until USER is chosen. Their names open their assignments; a double-click
    // renames; a right-click offers both. Their lenses are grab handles.
    for (std::size_t i = 0; i < userRow.size(); ++i)
    {
        const int macro = static_cast<int> (i);
        frame::MacroSpec spec { um->parameterIds[i], um->name != nullptr ? um->name (shell, macro) : "MACRO " + juce::String (macro + 1),
                                "User macro " + juce::String (macro + 1) + ": click for its assignments, double-click to rename" };
        frame::setUpMacro (userRow[i], macro, spec, shell.parameters, content, true);
        userRow[i].slider.setTitle ("User Macro " + juce::String (macro + 1));
        userRow[i].label->onClick = [this, macro] {
            if (closedByLabelPress == 100 + macro)
            {
                closedByLabelPress = -1;
                return;
            }
            openUserMacroPanel (macro);
        };
        juce::Component::SafePointer<ShellEditor> safe (this);
        userRow[i].label->setRackActions ([safe, macro] { if (safe != nullptr) safe->startUserMacroRename (macro); },
                                          [safe, macro] { if (safe != nullptr) safe->userMacroLabelMenu (macro); });
        auto lens = std::make_unique<MacroLens>();
        lens->onClick = [this, macro] { openUserMacroPanel (macro); };
        lens->onDragMove = [this, macro] (juce::Point<int> screen) {
            if (dragMacro != macro)
            {
                closePopup();
                dragMacro = macro;
                collectAssignTargets();
                assignOverlay.beginDrag (userMacroColour (macro), shell.userMacros()->name (shell, macro));
            }
            assignOverlay.pointAt (content.getLocalPoint (nullptr, screen).toFloat());
        };
        lens->onDragEnd = [this, macro] (juce::Point<int> screen, bool cancelled) {
            const auto id = cancelled ? juce::String() : assignOverlay.controlAt (content.getLocalPoint (nullptr, screen).toFloat());
            dragMacro = -1;
            assignOverlay.stop();
            if (id.isNotEmpty() && shell.userMacros()->assign (shell, macro, id))
                openUserMacroPanel (macro, id);   // (made neutral; its range in the panel)
            refreshUserMacros();
        };
        content.addChildComponent (*lens);
        lenses[i] = std::move (lens);
    }
    bankTab = std::make_unique<MacroBankTab>();
    bankTab->onChange = [this] (int bank) { showMacroBank (bank); };
    content.addAndMakeVisible (*bankTab);
    // Over everything while assigning (it takes clicks only when picking, only on a target).
    assignOverlay.onPicked = [this] (const juce::String& id) {
        const int macro = pickMacro;
        pickMacro = -1;
        assignOverlay.stop();
        if (macro >= 0 && shell.userMacros()->assign (shell, macro, id))
            openUserMacroPanel (macro, id);
        refreshUserMacros();
    };
    assignOverlay.onCancel = [this] { cancelUserMacroAssign(); };
    content.addAndMakeVisible (assignOverlay);
    // A control's right-click menu: Assign to USER (its existing items unchanged, first).
    modOverlay.extendContextMenu = [this] (juce::PopupMenu& menu, const juce::String& id) {
        const auto* u = shell.userMacros();
        if (u == nullptr || u->eligible == nullptr || ! u->eligible (shell, id))
            return;
        juce::PopupMenu sub;
        for (int m = 0; m < 6; ++m)
        {
            const bool has = u->targets != nullptr && u->targets (shell, m).contains (id);
            sub.addItem (u->name (shell, m), true, has, [this, m, id] {
                showMacroBank (1);   // (the macro it now has: shown)
                if (shell.userMacros()->assign (shell, m, id))
                    openUserMacroPanel (m, id);
                refreshUserMacros();
            });
        }
        menu.addSeparator();
        menu.addSubMenu ("Assign to USER", sub);
    };
    showMacroBank (shell.macroBank());
    refreshUserMacros();
}

void ShellEditor::placeUserMacros()
{
    if (bankTab == nullptr)
        return;
    // USER 1..6 read left to right: each takes the place of the CORE knob in its column (CORE's
    // data order is not its visual order - ECHO and SPACE sit swapped).
    std::array<std::size_t, frame::macroCount> column {};
    for (std::size_t i = 0; i < column.size(); ++i)
        column[i] = i;
    std::sort (column.begin(), column.end(), [this] (std::size_t a, std::size_t b) {
        return macros[a].slider.getX() < macros[b].slider.getX();
    });
    for (std::size_t i = 0; i < userRow.size(); ++i)
    {
        const auto& place = macros[column[i]];
        if (userRow[i].label != nullptr && place.label != nullptr)
            userRow[i].label->setBounds (place.label->getBounds());
        userRow[i].slider.setBounds (place.slider.getBounds());
        // The lens: where the frame draws the light under the knob (its centre 65 below the
        // knob's), a 22-unit target.
        const auto knob = place.slider.getBounds().toFloat();
        lenses[i]->setBounds (juce::Rectangle<float> (22.0f, 22.0f).withCentre ({ knob.getCentreX(), knob.getCentreY() + 65.0f }).toNearestInt());
    }
    // CORE and USER: two folder tabs rising from the macro panel's top-left edge (nothing below
    // them moves; their targets are larger than drawn).
    const auto panel = design::layout::macroPanel.translated (0.0f, -sourceTrim());
    bankTab->setBounds (MacroBankTab::boundsFor (panel).toNearestInt());
    bankTab->setPanel (panel, design::layout::panelRadius);
    assignOverlay.setBounds (content.getLocalBounds());
    assignOverlay.toFront (false);
}

osp::plugin::frame::MacroKnob* ShellEditor::userMacroKnob (int macro) noexcept
{
    return bankTab != nullptr && macro >= 0 && macro < frame::macroCount ? &userRow[static_cast<std::size_t> (macro)] : nullptr;
}

osp::plugin::MacroLens* ShellEditor::userMacroLens (int macro) noexcept
{
    return bankTab != nullptr && macro >= 0 && macro < frame::macroCount ? lenses[static_cast<std::size_t> (macro)].get() : nullptr;
}

void ShellEditor::showMacroBank (int bank)
{
    if (bankTab == nullptr)
        return;
    bank = bank == 1 ? 1 : 0;
    const bool changed = bank != bankShown;
    bankShown = bank;
    bankTab->setBank (bank);
    shell.setMacroBank (bank);
    if (changed)
    {
        cancelUserMacroAssign();
        if (popupIndex == anchoredPopup && panelMacro >= 0)
            closePopup();
        if (renameField != nullptr)
            renameField.reset();
        renaming = -1;
    }
    // Presentation only: the knobs shown change, the sound never does.
    for (std::size_t i = 0; i < macros.size(); ++i)
    {
        const bool core = bank == 0;
        macros[i].slider.setVisible (core);
        if (macros[i].label != nullptr)
            macros[i].label->setVisible (core);
        userRow[i].slider.setVisible (! core);
        if (userRow[i].label != nullptr)
            userRow[i].label->setVisible (! core);
        lenses[i]->setVisible (! core);
    }
    for (auto& [macro, lamp] : macroLamps)
        lamp->setVisible (bank == 0);
    content.repaint();
}

void ShellEditor::refreshUserMacros()
{
    const auto* um = shell.userMacros();
    if (um == nullptr)
        return;
    for (std::size_t i = 0; i < userRow.size(); ++i)
    {
        const int macro = static_cast<int> (i);
        const auto name = um->name (shell, macro);
        if (auto* label = userRow[i].label.get())
        {
            if (label->getTitle() != name)
            {
                // (the label's text is its own: a new one in its place, its handlers as before)
                auto onClick = label->onClick;
                const auto bounds = label->getBounds();
                const bool visible = label->isVisible();
                userRow[i].label = std::make_unique<MacroLabel> (name, true, false);
                userRow[i].label->setAccent (userMacroColour (macro));
                userRow[i].label->setTooltip ("User macro " + juce::String (macro + 1) + ": click for its assignments, double-click to rename");
                userRow[i].label->onClick = onClick;
                juce::Component::SafePointer<ShellEditor> safe (this);
                userRow[i].label->setRackActions ([safe, macro] { if (safe != nullptr) safe->startUserMacroRename (macro); },
                                                  [safe, macro] { if (safe != nullptr) safe->userMacroLabelMenu (macro); });
                binding::tag (*userRow[i].label, um->parameterIds[i]);
                content.addChildComponent (*userRow[i].label);
                userRow[i].label->setBounds (bounds);
                userRow[i].label->setVisible (visible);
                assignOverlay.toFront (false);
            }
            const bool lit = um->activeCount != nullptr && um->activeCount (shell, macro) > 0;
            if (userRow[i].label->isCustomised() != lit)
                userRow[i].label->setCustomised (lit);   // (its lens lights while it moves something)
            userRow[i].label->setOpen (panelMacro == macro);
        }
        lenses[i]->setTooltip ("Drag to assign " + name);
        userRow[i].slider.setTitle ("User Macro " + juce::String (macro + 1) + " (" + name + ")");
    }
    arcsMacro = -2;   // (the arcs recollected at the next tick)
    content.repaint();
}

void ShellEditor::openUserMacroPanel (int macro, const juce::String& select)
{
    const auto* um = shell.userMacros();
    if (um == nullptr || um->panel == nullptr || macro < 0 || macro >= frame::macroCount)
        return;
    cancelUserMacroAssign();
    auto panel = um->panel (shell, macro, select);
    if (panel == nullptr)
        return;
    auto* anchor = userRow[static_cast<std::size_t> (macro)].label.get();
    openAnchoredPopover (std::move (panel), *anchor);
    if (popupIndex == anchoredPopup)
    {
        anchorAbove = true;
        panelMacro = macro;
        positionPopup();
    }
    refreshUserMacros();
}

void ShellEditor::collectAssignTargets()
{
    // Every control on screen a USER macro may move (the product decides), front-most last.
    const auto* um = shell.userMacros();
    std::vector<MacroAssignOverlay::Control> found;
    std::function<void (juce::Component&)> walk = [&] (juce::Component& c) {
        for (auto* child : c.getChildren())
        {
            if (child == &assignOverlay || child == &modOverlay || ! child->isVisible())
                continue;
            if (auto* slider = dynamic_cast<juce::Slider*> (child))
            {
                juce::String id = slider->getProperties()["paramId"].toString();
                if (id.isEmpty())
                    id = binding::tagsOf (*slider)[0];
                const bool own = std::find (um->parameterIds.begin(), um->parameterIds.end(), id) != um->parameterIds.end();
                if (id.isNotEmpty() && ! own && um->eligible != nullptr && um->eligible (shell, id))
                {
                    const auto name = um->targetName != nullptr ? um->targetName (shell, id) : slider->getTitle();
                    found.push_back ({ id, name, content.getLocalArea (slider, slider->getLocalBounds()).toFloat() });
                }
            }
            walk (*child);
        }
    };
    if (um != nullptr)
        walk (content);
    assignOverlay.setControls (std::move (found));
}

void ShellEditor::beginUserMacroPick (int macro)
{
    const auto* um = shell.userMacros();
    if (um == nullptr || macro < 0 || macro >= frame::macroCount)
        return;
    closePopup();   // (the panel folds away while a control is chosen; it comes back after)
    pickMacro = macro;
    collectAssignTargets();
    const auto panel = design::layout::macroPanel.translated (0.0f, -sourceTrim());
    assignOverlay.beginPick (userMacroColour (macro), um->name (shell, macro), panel);
    assignOverlay.toFront (false);
}

void ShellEditor::cancelUserMacroAssign()
{
    if (dragMacro >= 0)
        if (auto* lens = lenses[static_cast<std::size_t> (dragMacro)].get())
            lens->cancelDrag();
    dragMacro = -1;
    pickMacro = -1;
    if (assignOverlay.mode() == MacroAssignOverlay::Mode::drag || assignOverlay.mode() == MacroAssignOverlay::Mode::pick)
        assignOverlay.stop();
}

void ShellEditor::dragUserMacroLens (int macro, juce::Point<int> from, juce::Point<int> to, bool cancel)
{
    auto* lens = userMacroLens (macro);
    if (lens == nullptr || lens->onDragMove == nullptr)
        return;
    lens->onDragMove (from);
    lens->onDragMove (to);
    lens->onDragEnd (to, cancel);
}

void ShellEditor::updateUserMacroArcs()
{
    // While a USER macro's name is hovered or its panel is open: thin arcs on what it moves.
    const auto* um = shell.userMacros();
    if (um != nullptr && um->revision != nullptr)
        if (const auto now = um->revision (shell); now != userMacroRevision)
        {
            userMacroRevision = now;   // (an undo, a preset, a session: names and lenses follow)
            refreshUserMacros();
        }
    if (um == nullptr || bankShown != 1 || dragMacro >= 0 || pickMacro >= 0)
    {
        if (arcsMacro >= 0 && assignOverlay.mode() == MacroAssignOverlay::Mode::show)
            assignOverlay.stop();
        arcsMacro = -1;
        return;
    }
    int shown = panelMacro;
    for (std::size_t i = 0; i < userRow.size() && shown < 0; ++i)
        if (userRow[i].label != nullptr && userRow[i].label->isMouseOver())
            shown = static_cast<int> (i);
    if (shown == arcsMacro)
        return;
    arcsMacro = shown;
    if (shown < 0)
    {
        assignOverlay.stop();
        return;
    }
    collectAssignTargets();
    std::vector<juce::String> ids;
    if (um->targets != nullptr)
        for (const auto& id : um->targets (shell, shown))
            ids.push_back (id);
    assignOverlay.show (std::move (ids), userMacroColour (shown));
}

void ShellEditor::startUserMacroRename (int macro)
{
    const auto* um = shell.userMacros();
    if (um == nullptr || um->rename == nullptr || macro < 0 || macro >= frame::macroCount)
        return;
    closePopup();
    auto* label = userRow[static_cast<std::size_t> (macro)].label.get();
    renaming = macro;
    renameField = std::make_unique<juce::TextEditor> ("Rename user macro");
    renameField->setTitle ("Rename user macro " + juce::String (macro + 1));
    renameField->setJustification (juce::Justification::centred);
    renameField->setFont (type::macroLabel (0.7f * static_cast<float> (label->getHeight())));
    renameField->setInputRestrictions (24);
    renameField->setText (um->name (shell, macro), false);
    renameField->setColour (juce::TextEditor::backgroundColourId, palette::ivory);
    renameField->setColour (juce::TextEditor::textColourId, palette::text);
    renameField->setColour (juce::TextEditor::outlineColourId, design::colour::hairline);
    renameField->setColour (juce::TextEditor::focusedOutlineColourId, userMacroColour (macro));
    content.addAndMakeVisible (*renameField);
    renameField->setBounds (label->getBounds().expanded (4, 2));
    renameField->selectAll();
    auto commit = [this] (bool keep) {
        if (renameField == nullptr || renaming < 0)
            return;
        const int m = renaming;
        const auto text = renameField->getText();
        renaming = -1;
        juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<ShellEditor> (this)] { if (safe != nullptr) safe->renameField.reset(); });
        if (keep && shell.userMacros()->rename != nullptr)
            shell.userMacros()->rename (shell, m, text);
        refreshUserMacros();
    };
    renameField->onReturnKey = [commit] { commit (true); };
    renameField->onEscapeKey = [commit] { commit (false); };
    renameField->onFocusLost = [commit] { commit (true); };
    if (isShowing())
        renameField->grabKeyboardFocus();
}

void ShellEditor::userMacroLabelMenu (int macro)
{
    const auto* um = shell.userMacros();
    if (um == nullptr)
        return;
    juce::PopupMenu menu;
    juce::Component::SafePointer<ShellEditor> safe (this);
    menu.addItem ("Edit Assignments", [safe, macro] { if (safe != nullptr) safe->openUserMacroPanel (macro); });
    menu.addItem ("Rename", [safe, macro] { if (safe != nullptr) safe->startUserMacroRename (macro); });
    menu.addSeparator();
    const bool any = um->targets != nullptr && ! um->targets (shell, macro).isEmpty();
    menu.addItem ("Clear Assignments", any, false, [safe, macro] {
        if (safe == nullptr)
            return;
        auto options = juce::MessageBoxOptions().withIconType (juce::MessageBoxIconType::QuestionIcon)
                           .withTitle ("Clear assignments")
                           .withMessage ("Remove every assignment of " + safe->shell.userMacros()->name (safe->shell, macro) + "? (Undo brings them back.)")
                           .withButton ("Clear").withButton ("Cancel").withAssociatedComponent (safe.getComponent());
        juce::AlertWindow::showAsync (options, [safe, macro] (int result) {
            if (safe != nullptr && result == 1)
            {
                safe->shell.userMacros()->clear (safe->shell, macro);
                safe->refreshUserMacros();
            }
        });
    });
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (userRow[static_cast<std::size_t> (macro)].label.get()));
}

} // namespace andor::shell
