#include "PluginEditor.h"

#include "core/PitchMath.h"
#include "io/AudioFileIO.h"

#include <map>

namespace osp::plugin
{

namespace
{
    const juce::String dot = juce::String::fromUTF8 ("  \xc2\xb7  ");

    bool isAudioFile (const juce::File& file)
    {
        return io::isSupportedAudioExtension (std::filesystem::path (file.getFullPathName().toStdString()));
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
            row->role.onChange = apply;
            row->layer.onChange = apply;
            row->root.onChange = apply;
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
void OspKeyboard::drawWhiteNote (int note, juce::Graphics& g, juce::Rectangle<float> area, bool isDown, bool isOver,
                                 juce::Colour, juce::Colour)
{
    using namespace design;
    // Warm white keys with a rounded front, a hairline between them; a held key only warms.
    const auto key = area.withTrimmedRight (1.0f);
    juce::Path shape;
    shape.addRoundedRectangle (key.getX(), key.getY() - 4.0f, key.getWidth(), key.getHeight() + 4.0f, 3.5f, 3.5f, false, false, true, true);
    auto top = colour::whiteKeyTop, bottom = colour::whiteKeyBottom;
    if (isDown)
    {
        top = top.interpolatedWith (colour::accent, 0.12f);
        bottom = bottom.interpolatedWith (colour::accent, 0.16f);
    }
    else if (isOver)
        top = top.darker (0.02f);
    g.setGradientFill (juce::ColourGradient (top, 0.0f, key.getY(), bottom, 0.0f, key.getBottom(), false));
    g.fillPath (shape);
    g.setColour (colour::keyLine);
    g.fillRect (juce::Rectangle<float> (area.getRight() - 1.0f, area.getY(), 1.0f, area.getHeight()));

    const auto label = getWhiteNoteText (note);
    if (label.isNotEmpty())
    {
        g.setColour (colour::textSecondary);
        g.setFont (fonts::make (std::min (14.0f, area.getWidth() * 0.6f), fonts::Weight::regular, 0.04f));
        // The octave number may run a little past a narrow key: "C4" must stay whole.
        const auto line = area.withTrimmedBottom (7.0f).removeFromBottom (16.0f);
        g.drawText (label, line.withX (area.getX() + 0.2f * area.getWidth()).withWidth (std::max (area.getWidth(), 24.0f)), juce::Justification::centredLeft, false);
    }
}

void OspKeyboard::drawBlackNote (int, juce::Graphics& g, juce::Rectangle<float> area, bool isDown, bool isOver, juce::Colour)
{
    using namespace design;
    const auto r = area.reduced (0.8f, 0.0f).withTrimmedTop (-4.0f);
    juce::Path shape;
    shape.addRoundedRectangle (r.getX(), r.getY(), r.getWidth(), r.getHeight(), 2.5f, 2.5f, false, false, true, true);
    design::CachedShadow (juce::Colour (0x55000000), 3, { 1, 2 }).drawForPath (g, shape);
    auto top = colour::blackKeyTop, bottom = colour::blackKeyBottom;
    if (isDown)
    {
        top = top.interpolatedWith (colour::accent, 0.2f);
        bottom = bottom.interpolatedWith (colour::accent, 0.14f);
    }
    else if (isOver)
        top = top.brighter (0.15f);
    g.setGradientFill (juce::ColourGradient (bottom, r.getX(), 0.0f, bottom, r.getRight(), 0.0f, false));
    g.fillPath (shape);
    // A rounded body: lighter down the middle, a lit front bevel.
    juce::ColourGradient body (top.withAlpha (0.0f), r.getX(), 0.0f, top.withAlpha (0.0f), r.getRight(), 0.0f, false);
    body.addColour (0.45, top);
    g.setGradientFill (body);
    g.fillRoundedRectangle (r.reduced (1.5f, 0.0f).withTrimmedBottom (0.12f * r.getHeight()), 2.0f);
    g.setColour (juce::Colours::white.withAlpha (0.16f));
    g.fillRoundedRectangle (r.withTop (r.getBottom() - 0.12f * r.getHeight()).reduced (1.5f, 1.0f), 1.5f);
}

juce::String OspKeyboard::getWhiteNoteText (int note)
{
    // MIDI 60 = C4, as everywhere else in OSP.
    return note % 12 == 0 ? "C" + juce::String (note / 12 - 1) : juce::String();
}

//==============================================================================
DropZone::DropZone()
{
    for (auto* b : { &browse, &example })
        addAndMakeVisible (*b);
    browse.onClick = [this] { if (onBrowse != nullptr) onBrowse(); };
    example.onClick = [this] { if (onExample != nullptr) onExample(); };
    setTitle ("Drop a sound");
}

void DropZone::setHighlight (bool on)
{
    if (highlight != on)
    {
        highlight = on;
        repaint();
    }
}

void DropZone::resized()
{
    auto r = getLocalBounds().withSizeKeepingCentre (260, 30).translated (0, 52);
    browse.setBounds (r.removeFromLeft (124));
    r.removeFromLeft (12);
    example.setBounds (r);
}

void DropZone::paint (juce::Graphics& g)
{
    using namespace palette;
    const auto r = getLocalBounds().toFloat().reduced (1.5f);
    g.setColour (highlight ? recessed.interpolatedWith (accent, 0.06f) : recessed.interpolatedWith (housing, 0.4f));
    g.fillRoundedRectangle (r, 12.0f);
    juce::Path outline;
    outline.addRoundedRectangle (r, 12.0f);
    juce::Path dashed;
    const float dashes[] = { 6.0f, 5.0f };
    juce::PathStrokeType (1.2f).createDashedStroke (dashed, outline, dashes, 2);
    g.setColour (highlight ? accent : hairline.darker (0.12f));
    g.fillPath (dashed);

    auto text = getLocalBounds().toFloat().withSizeKeepingCentre (r.getWidth(), 90.0f).translated (0.0f, -22.0f);
    g.setColour (text.isEmpty() ? palette::text : palette::text);
    g.setFont (fonts::make (24.0f, fonts::Weight::regular, 0.14f));
    g.drawText ("DROP A SOUND", text.removeFromTop (36.0f), juce::Justification::centred, false);
    g.setColour (textDim);
    g.setFont (fonts::make (13.5f));
    g.drawText (juce::String::fromUTF8 ("One tonal recording is enough \xc2\xb7 WAV, AIFF, FLAC \xc2\xb7 add up to three sounds later"),
                text.removeFromTop (22.0f), juce::Justification::centred, false);
    g.drawText ("A folder becomes one multi-sample sound", text.removeFromTop (20.0f), juce::Justification::centred, false);
}

//==============================================================================
void AddLayerTarget::setLetter (const juce::String& l)
{
    if (letter != l)
    {
        letter = l;
        repaint();
    }
}

void AddLayerTarget::setHighlight (bool on)
{
    if (highlight != on)
    {
        highlight = on;
        repaint();
    }
}

void AddLayerTarget::paint (juce::Graphics& g)
{
    using namespace palette;
    const auto r = getLocalBounds().toFloat().reduced (1.5f);
    g.setColour (highlight ? recessed.interpolatedWith (accent, 0.08f) : recessed.interpolatedWith (housing, 0.5f));
    g.fillRoundedRectangle (r, 10.0f);
    juce::Path outline;
    outline.addRoundedRectangle (r, 10.0f);
    juce::Path dashed;
    const float dashes[] = { 6.0f, 5.0f };
    juce::PathStrokeType (1.2f).createDashedStroke (dashed, outline, dashes, 2);
    g.setColour (highlight ? accent : hairline.darker (0.15f));
    g.fillPath (dashed);
    const auto centre = r.withSizeKeepingCentre (r.getWidth(), 60.0f);
    icons::draw (g, icons::Kind::plus, centre.withHeight (28.0f).withSizeKeepingCentre (28.0f, 28.0f), highlight ? accent : textDim, 1.8f);
    g.setColour (highlight ? palette::text : textDim);
    g.setFont (fonts::make (14.0f, fonts::Weight::medium, 0.1f));
    g.drawText ("ADD LAYER " + letter, centre.withTrimmedTop (34.0f), juce::Justification::centredTop, false);
}

//==============================================================================
void OspAudioProcessorEditor::HeaderMenuButton::paintButton (juce::Graphics& g, bool highlighted, bool)
{
    const auto r = getLocalBounds().toFloat();
    if (highlighted)
    {
        g.setColour (design::colour::text.withAlpha (0.06f));
        g.fillRoundedRectangle (r, 6.0f);
    }
    // Three round dots, 11 px apart (reference).
    g.setColour (design::colour::text);
    for (int i = -1; i <= 1; ++i)
        g.fillEllipse (juce::Rectangle<float> (6.0f, 6.0f).withCentre (r.getCentre().translated (0.0f, 11.0f * static_cast<float> (i))));
}

void OspAudioProcessorEditor::OutsideClickWatcher::mouseDown (const juce::MouseEvent& e)
{
    editor.mouseDownAnywhere (e.eventComponent);
}

void OspAudioProcessorEditor::mouseDownAnywhere (juce::Component* c)
{
    if (popup == nullptr)
    {
        closedByLabelPress = -1;
        return;
    }
    if (c == nullptr || c == popup.get() || popup->isParentOf (c))
        return;
    // Only clicks inside this editor close the popup. The global listener also hears clicks in
    // other JUCE windows - above all the popup's own menus (PATTERN, RATE): closing the popup
    // there would delete the control whose menu item is being chosen, before its choice arrives.
    if (c != this && ! isParentOf (c))
        return;
    int pressed = -1;
    for (int i = 0; i < static_cast<int> (macros.size()); ++i)
        if (c == macros[static_cast<std::size_t> (i)].label.get())
            pressed = popupOfMacro (i);
    if (c == &advancedButton)
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

    // Header: the preset, the master volume and the menu (utilities live there).
    presetBar.onChange = [this] { updateStatus(); timerCallback(); };
    presetBar.onSaveStartingState = [this] { saveStartingState(); };
    presetBar.onOpenLibrary = [this] { openLibrary(); };
    content.addAndMakeVisible (presetBar);
    content.addAndMakeVisible (volume);
    menuButton.setTooltip ("Sounds, presets, instruments, undo, size");
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
        // One EQ editor open at a time (opening B's closes A's editor; A's EQ keeps playing).
        card->onEqToggle = [this] (int l, bool open) {
            for (auto& c : cards)
                c->setEqOpen (open && c->layer() == l);
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
        content.addChildComponent (*card);
    }
    dropZone.onBrowse = [this] { chooseFile (0, true); };
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
    content.addChildComponent (headerMix);

    // The six macros: a name with settings behind it (click: its popup) over its knob.
    const std::array<std::pair<const char*, const char*>, 6> macroInfo { {
        { "life", "LIFE" }, { "drive", "DRIVE" }, { "character", "CHARACTER" }, { "motion", "MOVEMENT" }, { "space", "SPACE" }, { "echo", "ECHO" } } };
    const std::array<const char*, 6> popupHints { "Life: how differently each note is performed",
                                                  "Drive: saturation, from a little warmth to beautiful breakup (tube, tape or crunch)",
                                                  "Character: the tonal shape (filter)", "Movement: how the sound changes through time",
                                                  "Space: the room it plays in", "Echo: repeats of the sound, from a tape echo or a bucket-brigade delay" };
    for (std::size_t i = 0; i < macros.size(); ++i)
    {
        auto& knob = macros[i];
        knob.label = std::make_unique<MacroLabel> (macroInfo[i].second, true, false);
        knob.label->setAccent (design::colour::macro (static_cast<int> (i)));
        // The macro's identity colour on a thin value ring (the knob itself stays neutral).
        knob.slider.getProperties().set ("arc", static_cast<juce::int64> (design::colour::macro (static_cast<int> (i)).getARGB()));
        knob.slider.getProperties().set ("thinArc", 2.4);
        knob.slider.getProperties().set ("paramId", macroInfo[i].first);   // a modulation drop target
        knob.label->setTooltip (popupHints[i]);
        const int index = popupOfMacro (static_cast<int> (i));
        knob.label->onClick = [this, index] {
            if (closedByLabelPress == index)
            {
                closedByLabelPress = -1;
                return;
            }
            openPopup (index);
        };
        content.addAndMakeVisible (*knob.label);
        knob.slider.setRotaryParameters (OspLookAndFeel::rotaryStart, OspLookAndFeel::rotaryEnd, true);
        knob.slider.setPopupDisplayEnabled (true, true, &content);
        knob.slider.setColour (juce::TooltipWindow::textColourId, palette::valueBubbleText);   // light on the graphite bubble
        knob.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (ospProcessor.parameters, macroInfo[i].first, knob.slider);
        if (auto* param = ospProcessor.parameters.getParameter (macroInfo[i].first))
            knob.slider.setDoubleClickReturnValue (true, param->convertFrom0to1 (param->getDefaultValue())); // resets the amount only
        knob.slider.setTitle (macroInfo[i].second);
        // The value bubble: whole percent ("63 %"), set after the attachment (which installs the parameter's own text).
        knob.slider.textFromValueFunction = [] (double v) { return juce::String (juce::roundToInt (v)) + " %"; };
        knob.slider.updateText();
        content.addAndMakeVisible (knob.slider);
    }
    content.addAndMakeVisible (modPanel);

    content.addAndMakeVisible (pitchWheel);
    content.addAndMakeVisible (modWheel);
    keyboard.setAvailableRange (21, 108);
    keyboard.setOctaveForMiddleC (4); // MIDI 60 = C4, as everywhere else in OSP
    keyboard.setScrollButtonsVisible (false);
    keyboard.setBlackNoteLengthProportion (0.62f);
    keyboard.setColour (juce::MidiKeyboardComponent::shadowColourId, juce::Colours::transparentBlack);
    keyboard.setColour (juce::MidiKeyboardComponent::keySeparatorLineColourId, palette::hairline);
    keyboard.setColour (juce::MidiKeyboardComponent::whiteNoteColourId, palette::ivory);
    keyboard.setColour (juce::MidiKeyboardComponent::blackNoteColourId, palette::ebony);
    content.addAndMakeVisible (keyboard);

    statusLabel.setFont (type::micro (16.0f));
    statusLabel.setColour (juce::Label::textColourId, design::colour::textMicro.darker (0.15f));
    statusLabel.setBorderSize ({ 0, 0, 0, 0 });
    content.addAndMakeVisible (statusLabel);
    advancedButton.setTitle ("Advanced settings");
    advancedButton.setTooltip ("Tuning, bend range, voices, pitch character, dynamics (how touch changes the sound), MPE, variation seed");
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
    modOverlay.onShowRoutes = [this] (int source) { openModulationPopup (routesPopup, source); };
    modPanel.showTab (ospProcessor.modPanelTab());
    content.addAndMakeVisible (modOverlay);   // last: above everything (popups bring it back up)

    juce::Desktop::getInstance().addGlobalMouseListener (&outsideClicks);

    // The instrument is laid out once at the reference size (design::width x height) and
    // scaled as a whole: proportions never drift between sizes.
    content.onPaint = [this] (juce::Graphics& g) { paintInstrument (g); };
    addAndMakeVisible (content);
    layoutInstrument();
    setResizable (true, true);
    applyWindowShape (false);
    // The default size (1086 wide), not a scale measured before the window had any size.
    constexpr float defaultScale = 1086.0f / design::width;
    setSize (juce::roundToInt (defaultScale * design::width), juce::roundToInt (defaultScale * instrumentHeight()));
    setScaleFactor (ospProcessor.uiScale());

    if (ospProcessor.advancedOpen())
        openPopup (advancedPopup);
    timerCallback();
    startTimerHz (30);   // grains and read heads move at the display rate
}

OspAudioProcessorEditor::~OspAudioProcessorEditor()
{
    stopTimer();
    juce::Desktop::getInstance().removeGlobalMouseListener (&outsideClicks);
    for (auto& card : cards)
        juce::Desktop::getInstance().getAnimator().cancelAnimation (card.get(), false);
    popup.reset();
    libraryPanel.reset();
    setLookAndFeel (nullptr);
}

void OspAudioProcessorEditor::openLibrary (LibraryPanel::View view)
{
    closePopup();
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

void OspAudioProcessorEditor::placeLibrary()
{
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
    if (which < 0 || which > lastPopup || (which >= reimaginedPopup + OspAudioProcessor::numLayers && which < echoPopup))
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
    else
        popup = createMacroPopup (static_cast<MacroPopup> (macroOfPopup (which)), ospProcessor);
    popupIndex = which;
    popup->onSizeChanged = [this] { positionPopup(); };
    juce::Component::SafePointer<OspAudioProcessorEditor> safe (this);
    // The close button lives on the popup: close it after the click has been handled.
    popup->onClose = [safe] { juce::MessageManager::callAsync ([safe] { if (safe != nullptr) safe->closePopup(); }); };
    content.addAndMakeVisible (*popup);
    popup->toFront (false);
    modOverlay.toFront (false);
    positionPopup();
    {
        // Opening: a short fade and a 3 px settle (110 ms), nothing more.
        const auto final = popup->getBounds();
        popup->setBounds (final.translated (0, 3));
        popup->setAlpha (0.0f);
        juce::Desktop::getInstance().getAnimator().animateComponent (popup.get(), final, 1.0f, 110, false, 1.0, 0.0);
    }
    if (const int m = macroOfPopup (which); m >= 0)
        macros[static_cast<std::size_t> (m)].label->setOpen (true);
    if (reimagined)
        cards[static_cast<std::size_t> (which - reimaginedPopup)]->setReimaginedOpen (true);
    if (keepAdvanced)
    {
        ospProcessor.setAdvancedOpen (true);
        advancedButton.setToggleState (true, juce::dontSendNotification);
    }
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
    if (popupIndex == advancedPopup)
    {
        ospProcessor.setAdvancedOpen (false);
        advancedButton.setToggleState (false, juce::dontSendNotification);
    }
    juce::Desktop::getInstance().getAnimator().cancelAnimation (popup.get(), false);
    popup.reset();
    popupIndex = -1;
}

void OspAudioProcessorEditor::positionPopup()
{
    if (popup == nullptr)
        return;
    juce::Desktop::getInstance().getAnimator().cancelAnimation (popup.get(), false);
    popup->setAlpha (1.0f);
    // A panel laid out at a smaller unit is scaled up so every popup's shell reads alike.
    const float scale = 1.0f / popup->unit();
    const auto size = (popup->cardSize().toFloat() * scale);
    const float m = static_cast<float> (MiniPanel::shadowMargin) * scale;
    float x = 0.0f, y = 0.0f;
    if (const int macro = macroOfPopup (popupIndex); macro >= 0)
    {
        // A macro's popover unfolds directly above its name: centred on it, its foot 10 px
        // above, kept inside the instrument at the edges (LIFE, SPACE).
        const auto anchor = macros[static_cast<std::size_t> (macro)].label->getBounds().toFloat();
        x = anchor.getCentreX() - 0.5f * size.x;
        y = anchor.getY() - 10.0f - size.y;
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
        // The modulation popovers unfold above the envelope panel, at its right.
        const auto anchor = modPanel.getBounds().toFloat();
        x = anchor.getRight() - size.x + 10.0f;
        y = anchor.getY() - 6.0f - size.y;
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
        const auto anchor = advancedButton.getBounds().toFloat();
        x = anchor.getRight() - size.x;
        y = anchor.getY() - 8.0f - size.y;
    }
    // Inside the instrument itself (never over the modulation bay).
    const auto limits = content.getLocalBounds().toFloat().withWidth (design::width).reduced (14.0f);
    x = juce::jlimit (limits.getX(), std::max (limits.getX(), limits.getRight() - size.x), x);
    y = std::max (limits.getY(), y);
    popup->setTransform (juce::AffineTransform::scale (scale));
    const auto card = popup->cardSize();
    popup->setBounds (juce::roundToInt ((x - m) / scale), juce::roundToInt ((y - m) / scale),
                      card.x + 2 * MiniPanel::shadowMargin, card.y + 2 * MiniPanel::shadowMargin);
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

void OspAudioProcessorEditor::layoutSources (bool animate)
{
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
    const int gap = 12;
    const int width = columns > 0 ? (sourceArea.getWidth() - gap * (columns - 1)) / columns : sourceArea.getWidth();
    auto column = [&] (int i) { return juce::Rectangle<int> (sourceArea.getX() + i * (width + gap), sourceArea.getY(), width, sourceArea.getHeight()); };

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

    // Loose files are one sound each; a folder is one multi-sample sound.
    juce::Array<juce::File> loose, folder;
    for (const auto& f : files)
    {
        const juce::File file (f);
        if (file.isDirectory())
        {
            for (const auto& entry : juce::RangedDirectoryIterator (file, false, "*", juce::File::findFiles))
                if (isAudioFile (entry.getFile()))
                    folder.add (entry.getFile());
        }
        else if (isAudioFile (file))
            loose.add (file);
    }
    switch (target.kind)
    {
        case DropTarget::Kind::empty:
        case DropTarget::Kind::add:
            if (! folder.isEmpty())
                ospProcessor.setEditLayer (std::max (0, ospProcessor.addLayerSet (folder)));
            if (! loose.isEmpty())
            {
                const int first = target.kind == DropTarget::Kind::add ? target.layer : ospProcessor.firstFreeLayer();
                if (ospProcessor.addLayers (loose, first) > 0 && folder.isEmpty())
                    ospProcessor.setEditLayer (std::max (0, first));
            }
            break;
        case DropTarget::Kind::replace:
            if (! folder.isEmpty())
                ospProcessor.replaceLayer (folder, target.layer);
            else if (! loose.isEmpty())
            {
                ospProcessor.replaceLayer ({ loose.getFirst() }, target.layer);
                loose.remove (0);
            }
            if (! folder.isEmpty() || ! loose.isEmpty())
                ospProcessor.addLayers (loose);   // the rest become new layers while there is room
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

void OspAudioProcessorEditor::addLayerSection (juce::PopupMenu& menu, int layer, bool header)
{
    const auto letter = OspAudioProcessor::layerName (layer);
    const auto loaded = ospProcessor.currentInstrument (layer);
    if (header)
        menu.addSectionHeader ("Layer " + letter + (loaded != nullptr ? dot + juce::String::fromUTF8 (loaded->filename.c_str()) : juce::String()));
    juce::Component::SafePointer<OspAudioProcessorEditor> safe (this);
    menu.addItem ("Replace sound" + juce::String::fromUTF8 ("\xe2\x80\xa6"), [safe, layer] { if (safe != nullptr) safe->chooseFile (layer, false); });
    menu.addItem ("Load example", [safe, layer] { if (safe != nullptr) safe->ospProcessor.replaceLayer ({}, layer), safe->ospProcessor.loadExample (layer); });
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
        menu.addSubMenu ("Root" + (overrideMidi ? ": " + juce::String (midiNoteName (static_cast<int> (std::lround (*overrideMidi)))) : juce::String (": auto")),
                         root, loaded != nullptr);
    }
    menu.addItem ("Samples", loaded != nullptr && loaded->set != nullptr, samplesShown && ospProcessor.editLayer() == layer, [safe, layer] {
        if (safe == nullptr)
            return;
        safe->ospProcessor.setEditLayer (layer);
        safe->samplesShown = ! safe->samplesShown;
        safe->samplesPanel.setVisible (safe->samplesShown);
        safe->samplesPanel.setInstrument (safe->ospProcessor.currentInstrument (layer), layer);
        safe->updateFocus();
    });
    // Removing a loaded sound asks once more (a sub-menu), and can be restored from the menu.
    juce::PopupMenu confirm;
    confirm.addItem ("Remove " + (loaded != nullptr ? juce::String::fromUTF8 (loaded->filename.c_str()) : "layer " + letter),
                     [safe, layer] { if (safe != nullptr) safe->ospProcessor.removeLayer (layer), safe->timerCallback(); });
    menu.addSubMenu ("Remove layer " + letter, confirm, (ospProcessor.isLayerOccupied (layer) || ospProcessor.isKeptEmptySlot (layer))
                                                            && ospProcessor.loadState (layer) != OspAudioProcessor::LoadState::loading);
}

void OspAudioProcessorEditor::showLayerMenu (int layer, juce::Component& target)
{
    ospProcessor.setEditLayer (layer);
    updateFocus();
    juce::PopupMenu menu;
    addLayerSection (menu, layer, false);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (target).withDeletionCheck (*this));
}

void OspAudioProcessorEditor::showMenu()
{
    juce::PopupMenu menu;
    juce::Component::SafePointer<OspAudioProcessorEditor> safe (this);
    const int count = ospProcessor.occupiedLayerCount();
    if (count < OspAudioProcessor::numLayers)
        menu.addItem (count == 0 ? juce::String::fromUTF8 ("Load sound\xe2\x80\xa6") : juce::String::fromUTF8 ("Add layer\xe2\x80\xa6"),
                      [safe] { if (safe != nullptr) safe->chooseFile (-1, true); });
    if (count == 0)
        menu.addItem ("Load example", [safe] { if (safe != nullptr) { safe->ospProcessor.resetLayerControls (0); safe->ospProcessor.loadExample (0); } });
    if (ospProcessor.canRestoreRemovedLayer())
        menu.addItem ("Restore removed layer", [safe] { if (safe != nullptr) safe->ospProcessor.restoreRemovedLayer(), safe->timerCallback(); });
    if (ospProcessor.slotCount() > 0)
        addLayerSection (menu, ospProcessor.editLayer(), true);
    {
        // Every sound out, every setting and slot kept (a sub-menu asks once more).
        juce::PopupMenu confirm;
        confirm.addItem ("Clear all samples (settings are kept)", [safe] {
            if (safe != nullptr)
                safe->ospProcessor.clearAllSamples(), safe->timerCallback();
        });
        menu.addSubMenu ("Clear all samples", confirm, count > 0);
    }
    menu.addSeparator();

    // The user's presets and instruments, by folder (sub-folders become sub-menus).
    auto browse = [this] (const juce::File& folder, const juce::String& extension, bool instrument) {
        juce::PopupMenu list;
        std::map<juce::String, juce::PopupMenu> subFolders;
        const auto current = ospProcessor.currentPresetFile();
        juce::Component::SafePointer<OspAudioProcessorEditor> editor (this);
        for (const auto& file : OspAudioProcessor::findFiles (folder, extension))
        {
            const auto parent = file.getParentDirectory();
            auto& target = parent == folder ? list : subFolders[parent.getRelativePathFrom (folder)];
            target.addItem (file.getFileNameWithoutExtension(), true, ! instrument && file == current, [editor, file, instrument] {
                if (editor == nullptr)
                    return;
                juce::String error;
                const bool ok = instrument ? editor->ospProcessor.importInstrument (file, error) : editor->ospProcessor.loadPreset (file);
                editor->ospProcessor.showMessage (ok ? "Opened " + file.getFileNameWithoutExtension() : (error.isEmpty() ? "Could not open " + file.getFileName() : error));
                editor->timerCallback();
            });
        }
        for (auto& [name, sub] : subFolders)
            list.addSubMenu (name, sub);
        if (list.getNumItems() == 0)
            list.addItem (instrument ? "No instruments yet" : "No presets yet", false, false, [] {});
        list.addSeparator();
        list.addItem ("Show folder", [folder] {
            folder.createDirectory();
            folder.revealToUser();
        });
        return list;
    };
    menu.addItem (juce::String::fromUTF8 ("Libraryâ¦"), [safe] { if (safe != nullptr) safe->openLibrary(); });
    menu.addSubMenu ("Presets", browse (OspAudioProcessor::presetFolder(), OspAudioProcessor::presetExtension, false));
    menu.addItem ("Save preset...", [safe] { if (safe != nullptr) safe->choosePresetFile (true, false); });
    menu.addItem ("Open preset file...", [safe] { if (safe != nullptr) safe->choosePresetFile (false, false); });
    menu.addSeparator();
    menu.addSubMenu ("Instruments", browse (OspAudioProcessor::instrumentFolder(), OspAudioProcessor::instrumentExtension, true));
    menu.addItem ("Export instrument...", count > 0, false, [safe] { if (safe != nullptr) safe->choosePresetFile (true, true); });
    menu.addItem ("Open instrument file...", [safe] { if (safe != nullptr) safe->choosePresetFile (false, true); });
    menu.addSeparator();
    menu.addItem ("Undo " + ospProcessor.undoManager.getUndoDescription(), ospProcessor.undoManager.canUndo(), false,
                  [safe] { if (safe != nullptr) safe->ospProcessor.undoManager.undo(); });
    menu.addItem ("Redo " + ospProcessor.undoManager.getRedoDescription(), ospProcessor.undoManager.canRedo(), false,
                  [safe] { if (safe != nullptr) safe->ospProcessor.undoManager.redo(); });
    juce::PopupMenu size;
    for (int percent : { 80, 100, 125, 150, 200 })
        size.addItem (juce::String (percent) + " %", true, std::abs (ospProcessor.uiScale() * 100.0f - static_cast<float> (percent)) < 1.0f,
                      [safe, percent] {
                          if (safe == nullptr)
                              return;
                          safe->ospProcessor.setUiScale (static_cast<float> (percent) / 100.0f);
                          safe->setScaleFactor (safe->ospProcessor.uiScale());
                      });
    menu.addSubMenu ("Interface size", size);
    menu.addItem ("Advanced settings", [safe] { if (safe != nullptr) safe->openPopup (advancedPopup); });
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (menuButton).withDeletionCheck (*this));
}

void OspAudioProcessorEditor::saveStartingState()
{
    auto folder = OspAudioProcessor::startingStateFolder();
    folder.createDirectory();
    chooser = std::make_unique<juce::FileChooser> ("Save starting state", folder, juce::String ("*") + OspAudioProcessor::startingStateExtension);
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting | juce::FileBrowserComponent::canSelectFiles,
                          [this] (const juce::FileChooser& fc) {
                              auto file = fc.getResult();
                              if (file == juce::File())
                                  return;
                              if (file.getFileExtension() != OspAudioProcessor::startingStateExtension)
                                  file = file.withFileExtension (OspAudioProcessor::startingStateExtension);
                              const bool ok = ospProcessor.saveStartingState (file);
                              ospProcessor.showMessage (ok ? "Saved starting state " + file.getFileNameWithoutExtension() : "Could not save " + file.getFileName());
                              presetBar.refresh();
                              timerCallback();
                          });
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
        timerCallback();
    });
}

void OspAudioProcessorEditor::chooseFile (int layer, bool addAsNewLayer)
{
    chooser = std::make_unique<juce::FileChooser> (addAsNewLayer ? "Choose sounds (each becomes a layer)" : "Choose a sound", juce::File(),
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
                              if (addAsNewLayer)
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
    if (key == juce::KeyPress::escapeKey && popup != nullptr)
    {
        // Closed asynchronously: the key may have come through one of the popup's children.
        juce::Component::SafePointer<OspAudioProcessorEditor> safe (this);
        juce::MessageManager::callAsync ([safe] { if (safe != nullptr) safe->closePopup(); });
        return true;
    }
    const bool command = key.getModifiers().isCommandDown();
    if (command && (key.getKeyCode() == 'L' || key.getKeyCode() == 'l'))
    {
        if (libraryPanel != nullptr)
            closeLibrary();
        else
            openLibrary();
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
    g.fillAll (design::colour::backdropTop);
}

void OspAudioProcessorEditor::paintInstrument (juce::Graphics& g)
{
    using namespace design;
    draw::housing (g, content.getLocalBounds().toFloat());

    // Identity: ANDOR/OSP, One Shot Performer.
    {
        const auto bold = fonts::make (60.0f, fonts::Weight::displayBold).withHorizontalScale (1.07f);
        const auto light = fonts::make (60.0f, fonts::Weight::displayLight).withHorizontalScale (1.07f);
        juce::GlyphArrangement osp, rest;
        osp.addLineOfText (bold, "ANDOR", layout::logo.x, layout::logo.y);
        const float w = osp.getBoundingBox (0, -1, true).getRight() - layout::logo.x + 1.0f;
        rest.addLineOfText (light, "/OSP", layout::logo.x + w, layout::logo.y);
        g.setColour (colour::text);
        osp.draw (g);
        // The slash a shade lighter, as in the identity.
        rest.draw (g);
        // As written (not set in capitals), with a little air between the letters.
        g.setFont (fonts::make (15.0f, fonts::Weight::regular, 0.16f));
        g.setColour (colour::textSecondary);
        g.drawText ("One Shot Performer", layout::logoSubtitle.withHeight (17.0f), juce::Justification::centredLeft, false);
    }

    // The keyboard's frame: a quiet rim, the keys set into it.
    {
        const auto bed = layout::keyboard.translated (0.0f, arpShown ? layout::arpShift : 0.0f).expanded (2.0f);
        juce::Path frame;
        frame.addRoundedRectangle (bed, 6.0f);
        design::CachedShadow (juce::Colour (0x22302418), 3, { 0, 1 }).drawForPath (g, frame);
        g.setColour (juce::Colour (0xffd9d0c3));
        g.fillPath (frame);
        g.setColour (juce::Colour (0xffc4bbad));
        g.strokePath (frame, juce::PathStrokeType (1.0f));
    }

    // The macros and the envelope share one panel.
    {
        const auto r = layout::macroPanel;
        draw::raised (g, r, layout::panelRadius, colour::panelTop, colour::panelBottom);
        g.setColour (colour::divider);
        g.fillRect (juce::Rectangle<float> (layout::envelopeX - 10.0f, 695.0f, 1.0f, 190.0f));
        g.setColour (colour::text.withAlpha (0.9f));
        g.setFont (type::panelHeader());
        // The title on the panel's own first line (the names moved up to clear the halos).
        g.drawText ("MACROS", juce::Rectangle<float> (59.0f, 688.0f, 200.0f, 24.0f), juce::Justification::centredLeft, false);
        // Under each macro a small light: lit when the macro's own settings are in use.
        for (std::size_t i = 0; i < macros.size(); ++i)
        {
            const auto knob = macros[i].slider.getBounds().toFloat();
            const bool lit = macros[i].label != nullptr && macros[i].label->isCustomised();
            const auto identity = colour::macro (static_cast<int> (i));
            draw::led (g, { knob.getCentreX(), knob.getCentreY() + 65.0f }, 9.0f, lit ? identity.withMultipliedBrightness (1.2f) : identity.interpolatedWith (colour::panelBottom, 0.35f), lit ? 0.8f : 0.0f);
        }
    }
}

void OspAudioProcessorEditor::resized()
{
    // Scaled uniformly to fit: if a host keeps the old window shape (it refused the resize),
    // the instrument still shows whole, with a margin, never cut off.
    const float w = design::width, h = instrumentHeight();
    const float k = std::min (static_cast<float> (getWidth()) / w, static_cast<float> (getHeight()) / h);
    const float x = 0.5f * (static_cast<float> (getWidth()) - w * k);
    const float y = 0.5f * (static_cast<float> (getHeight()) - h * k);
    content.setBounds (0, 0, static_cast<int> (w), static_cast<int> (h));
    content.setTransform (juce::AffineTransform::scale (k).translated (x, y));
    modOverlay.setBounds (content.getLocalBounds());
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

void OspAudioProcessorEditor::applyWindowShape (bool resizeWindow)
{
    // The window keeps the instrument's proportions: the same width range as always, the
    // height following the instrument (taller with the arpeggiator's editor shown).
    const float h = instrumentHeight();
    setResizeLimits (869, juce::roundToInt (869.0f * h / design::width), 1810, juce::roundToInt (1810.0f * h / design::width));
    if (auto* windowShape = getConstrainer())
        windowShape->setFixedAspectRatio (static_cast<double> (design::width / h));
    if (resizeWindow)
    {
        const int newHeight = juce::roundToInt (static_cast<float> (getWidth()) * h / design::width);
        if (newHeight != getHeight())
            setSize (getWidth(), newHeight);
        else
            resized();
    }
}

void OspAudioProcessorEditor::selectModulation (int source, int slot)
{
    // The one selection: the panel shows the source (when it shows a source at all), the
    // halos emphasise its routes (the chosen one most), an open routes popover marks the row.
    modSelSource = source;
    modSelSlot = slot;
    if (source >= 0 && source < 4 && modPanel.tab() != 0 && modPanel.selectedSource() != source)   // the wheel has no tab
        modPanel.showTab (modui::tabOfSource (source));
    modOverlay.setSelection (modSelSource, modSelSlot);
    if (auto* routes = dynamic_cast<ModRoutesPopup*> (popup.get()))
        routes->setSelectedSlot (modSelSlot);
}

void OspAudioProcessorEditor::openModulationPopup (int which, int source)
{
    if (source > 3)
        return;   // the wheel's routes are on its controls (no source page)
    popupSource = juce::jlimit (0, 3, source);
    openPopup (which);
}

void OspAudioProcessorEditor::dragModulation (int source, juce::Point<float> where)
{
    modOverlay.setDrag (source, where);
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
    slot = modOverlay.drop (source, where);
    if (slot >= 0)
        modPanel.refresh();
    hoverMacro = -1;
    modOverlay.setDrag (-1, {});
    return slot;
}

void OspAudioProcessorEditor::layoutInstrument()
{
    using namespace design;
    auto at = [] (juce::Rectangle<float> r) { return r.getSmallestIntegerContainer(); };

    // Header: identity left (painted), preset in the middle, volume and menu right.
    presetBar.setBounds (at (layout::presetBar));
    volume.setBounds (at (layout::volume));
    menuButton.setBounds (at (layout::menu));

    // The macros: name, knob and its light, at the reference's places.
    // Six across the narrower macro side (data order LIFE..SPACE, ECHO; ECHO sits before SPACE).
    static constexpr std::array<float, 6> centres { 128.0f, 274.0f, 420.0f, 566.0f, 858.0f, 712.0f };
    for (std::size_t i = 0; i < macros.size(); ++i)
    {
        auto& knob = macros[i];
        // The name stands clear above everything the knob's modulation halo can draw (its
        // outer route arcs, the selection glow and the handle reach 77 px above the knob's
        // centre): the name a little higher, the knob a little lower than before.
        knob.label->setBounds (at (juce::Rectangle<float> (150.0f, 22.0f).withCentre ({ centres[i], 719.0f })));
        knob.slider.setBounds (at (juce::Rectangle<float> (132.0f, 132.0f).withCentre ({ centres[i], 818.0f })));
    }
    // The envelope panel's tab row on the macros' title line; its display takes the extra height.
    modPanel.setBounds (at (juce::Rectangle<float> (layout::envelopeX, 688.0f, 1405.0f - layout::envelopeX, 206.0f)));

    // The arpeggiator's editor (when shown) takes the keyboard row's place; the row and the
    // footer move down by its height.
    arpPanel.setBounds (at (layout::arpPanel));
    const float shift = arpShown ? layout::arpShift : 0.0f;
    auto low = [&] (juce::Rectangle<float> r) { return at (r.translated (0.0f, shift)); };

    // Keyboard row and footer.
    pitchWheel.setBounds (low (layout::pitchWheel.withHeight (115.0f)));
    modWheel.setBounds (low (layout::modWheel.withHeight (115.0f)));
    keyboard.setBounds (low (layout::keyboard));
    keyboard.setKeyWidth (layout::keyboard.getWidth() / 52.0f);   // 88 keys = 52 white keys
    statusLabel.setBounds (low (layout::status));
    arpButton.setBounds (low (layout::arpControl));
    advancedButton.setBounds (low (layout::advanced));

    modOverlay.setBounds (content.getLocalBounds());

    headerMix.setBounds (at (layout::headerMix));
    sourceArea = at (layout::sources);
    layoutSources (false);
}

void OspAudioProcessorEditor::updateStatus()
{
    const int layer = ospProcessor.editLayer();
    juce::String status;
    switch (ospProcessor.loadState (layer))
    {
        case OspAudioProcessor::LoadState::empty: status = shownCount > 0 ? "" : "No sound loaded"; break;
        case OspAudioProcessor::LoadState::loading: status = juce::String::fromUTF8 ("Understanding the sound\xe2\x80\xa6"); break;
        case OspAudioProcessor::LoadState::ready: status = ospProcessor.stageMessage(); break;
        case OspAudioProcessor::LoadState::failed: status = "Could not load that file"; break;
    }
    const int voices = ospProcessor.activeVoices.load();
    if (voices > 0)
        status << (status.isNotEmpty() ? dot : juce::String()) << voices << (voices == 1 ? " note" : " notes");
    const auto message = ospProcessor.statusMessage();
    if (message.isNotEmpty())
        status << (status.isNotEmpty() ? dot : juce::String()) << message;
    if (statusLabel.getText() != status)
        statusLabel.setText (status, juce::dontSendNotification);
}

void OspAudioProcessorEditor::timerCallback()
{
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
    arpButton.refresh();
    if (arpShown)
        arpPanel.refresh();
    // Modulation: the bay (a recalled session may open or close it), the rings, MOD's light.
    if (ospProcessor.modPanelTab() != modPanel.tab())
        modPanel.showTab (ospProcessor.modPanelTab());   // a recalled session
    modPanel.refresh();
    modOverlay.refresh();
    if (popup != nullptr)
        popup->refreshContent();
    bool wheelInUse = false;
    for (const auto& route : ospProcessor.modulationRoutes())
        wheelInUse = wheelInUse || (route.route.source == mod::Source::modWheel && route.state == mod::RouteState::active);
    modWheel.setInUse (wheelInUse);
}

} // namespace osp::plugin
