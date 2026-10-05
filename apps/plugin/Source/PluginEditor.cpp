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
        header->name.setFont (fonts::make (14.0f, fonts::Weight::semibold));
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
    using namespace palette;
    auto face = ivory;
    if (isDown)
        face = ivory.interpolatedWith (accent, 0.32f);
    else if (isOver)
        face = ivory.darker (0.03f);
    g.setColour (face);
    g.fillRect (area);
    // A soft shade at the key's front and a hairline between keys.
    g.setGradientFill (juce::ColourGradient (juce::Colours::transparentBlack, 0.0f, area.getBottom() - 10.0f,
                                             juce::Colour (0x101e1c18), 0.0f, area.getBottom(), false));
    g.fillRect (area.withTop (area.getBottom() - 10.0f));
    g.setColour (hairline);
    g.drawVerticalLine (static_cast<int> (area.getRight()), area.getY(), area.getBottom());

    const auto label = getWhiteNoteText (note);
    if (label.isNotEmpty())
    {
        g.setColour (isDown ? accent.darker (0.4f) : textDim.withAlpha (0.8f));
        g.setFont (fonts::make (std::min (9.5f, area.getWidth() * 0.55f), fonts::Weight::medium));
        g.drawText (label, area.withTrimmedLeft (2.0f).withTrimmedBottom (4.0f).removeFromBottom (11.0f), juce::Justification::centredLeft, false);
    }
}

void OspKeyboard::drawBlackNote (int, juce::Graphics& g, juce::Rectangle<float> area, bool isDown, bool isOver, juce::Colour)
{
    using namespace palette;
    auto r = area.reduced (0.6f, 0.0f);
    auto face = isDown ? ebony.interpolatedWith (accent, 0.55f) : (isOver ? ebony.brighter (0.15f) : ebony);
    g.setColour (face);
    g.fillRoundedRectangle (r.withTrimmedTop (-3.0f), 2.0f);
    // A faint lit front edge.
    g.setColour (juce::Colours::white.withAlpha (isDown ? 0.06f : 0.1f));
    g.fillRoundedRectangle (r.withTop (r.getBottom() - r.getHeight() * 0.1f).reduced (r.getWidth() * 0.14f, 1.0f), 1.5f);
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
    g.setFont (fonts::make (24.0f, fonts::Weight::semibold, 0.12f));
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
    g.setFont (fonts::make (14.0f, fonts::Weight::semibold, 0.1f));
    g.drawText ("ADD LAYER " + letter, centre.withTrimmedTop (34.0f), juce::Justification::centredTop, false);
}

//==============================================================================
void OspAudioProcessorEditor::HeaderMenuButton::paintButton (juce::Graphics& g, bool highlighted, bool)
{
    if (highlighted)
    {
        g.setColour (palette::recessed);
        g.fillRoundedRectangle (getLocalBounds().toFloat(), 6.0f);
    }
    icons::draw (g, icons::Kind::dots, getLocalBounds().toFloat().reduced (5.0f), palette::text);
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
    for (int i = 0; i < 5; ++i)
        if (c == macros[static_cast<std::size_t> (i)].label.get())
            pressed = i;
    if (c == &advancedButton)
        pressed = advancedPopup;
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
    presetBar.onChange = [this] { updateStatus(); };
    addAndMakeVisible (presetBar);
    volume.setRotaryParameters (OspLookAndFeel::rotaryStart, OspLookAndFeel::rotaryEnd, true);
    volume.getProperties().set ("mini", true);
    volume.setTitle ("Volume");
    volume.setTooltip ("Master volume");
    volume.setPopupDisplayEnabled (true, true, this);
    volumeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (ospProcessor.parameters, "gain", volume);
    volume.setDoubleClickReturnValue (true, 0.0);
    addAndMakeVisible (volume);
    menuButton.setTooltip ("Sounds, presets, instruments, undo, size");
    menuButton.onClick = [this] { showMenu(); };
    addAndMakeVisible (menuButton);
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
        addChildComponent (*card);
    }
    dropZone.onBrowse = [this] { chooseFile (0, true); };
    dropZone.onExample = [this] {
        ospProcessor.resetLayerControls (0);
        ospProcessor.loadExample (0);
    };
    addChildComponent (dropZone);
    addChildComponent (addTarget);
    addChildComponent (samplesPanel);

    addAndMakeVisible (mixSection);

    // The five macros: a name with settings behind it (click: its popup) over its knob.
    const std::array<std::pair<const char*, const char*>, 5> macroInfo { {
        { "life", "LIFE" }, { "dynamics", "DYNAMICS" }, { "character", "CHARACTER" }, { "motion", "MOVEMENT" }, { "space", "SPACE" } } };
    const std::array<const char*, 5> popupHints { "Life: how differently each note is performed", "Dynamics: how touch changes the sound",
                                                  "Character: the tonal shape (filter)", "Movement: how the sound changes through time",
                                                  "Space: the room it plays in" };
    for (std::size_t i = 0; i < macros.size(); ++i)
    {
        auto& knob = macros[i];
        knob.label = std::make_unique<MacroLabel> (macroInfo[i].second, true, false);
        knob.label->setTooltip (popupHints[i]);
        const int index = static_cast<int> (i);
        knob.label->onClick = [this, index] {
            if (closedByLabelPress == index)
            {
                closedByLabelPress = -1;
                return;
            }
            openPopup (index);
        };
        addAndMakeVisible (*knob.label);
        knob.slider.setRotaryParameters (OspLookAndFeel::rotaryStart, OspLookAndFeel::rotaryEnd, true);
        knob.slider.setPopupDisplayEnabled (true, true, this);
        knob.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (ospProcessor.parameters, macroInfo[i].first, knob.slider);
        if (auto* param = ospProcessor.parameters.getParameter (macroInfo[i].first))
            knob.slider.setDoubleClickReturnValue (true, param->convertFrom0to1 (param->getDefaultValue())); // resets the amount only
        knob.slider.setTitle (macroInfo[i].second);
        addAndMakeVisible (knob.slider);
    }
    addAndMakeVisible (envelope);

    addAndMakeVisible (pitchWheel);
    addAndMakeVisible (modWheel);
    keyboard.setAvailableRange (21, 108);
    keyboard.setOctaveForMiddleC (4); // MIDI 60 = C4, as everywhere else in OSP
    keyboard.setScrollButtonsVisible (false);
    keyboard.setBlackNoteLengthProportion (0.62f);
    keyboard.setColour (juce::MidiKeyboardComponent::shadowColourId, juce::Colours::transparentBlack);
    keyboard.setColour (juce::MidiKeyboardComponent::keySeparatorLineColourId, palette::hairline);
    keyboard.setColour (juce::MidiKeyboardComponent::whiteNoteColourId, palette::ivory);
    keyboard.setColour (juce::MidiKeyboardComponent::blackNoteColourId, palette::ebony);
    addAndMakeVisible (keyboard);

    statusLabel.setFont (fonts::make (12.0f));
    statusLabel.setColour (juce::Label::textColourId, palette::textDim);
    statusLabel.setBorderSize ({ 0, 0, 0, 0 });
    addAndMakeVisible (statusLabel);
    advancedButton.setTitle ("Advanced settings");
    advancedButton.setTooltip ("Tuning, bend range, pitch character, MPE, variation seed");
    advancedButton.onClick = [this] {
        if (closedByLabelPress == advancedPopup)
        {
            closedByLabelPress = -1;
            return;
        }
        openPopup (advancedPopup);
    };
    addAndMakeVisible (advancedButton);

    juce::Desktop::getInstance().addGlobalMouseListener (&outsideClicks);

    setResizable (true, true);
    setResizeLimits (900, 720, 1800, 1300);
    setSize (1060, 820);
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
    setLookAndFeel (nullptr);
}

//==============================================================================
// Popups

void OspAudioProcessorEditor::openPopup (int which)
{
    const bool keepAdvanced = which == advancedPopup;
    closePopup();
    if (which < 0 || which > advancedPopup)
        return;
    popup = which == advancedPopup ? createAdvancedPopup (ospProcessor) : createMacroPopup (static_cast<MacroPopup> (which), ospProcessor);
    popupIndex = which;
    popup->onSizeChanged = [this] { positionPopup(); };
    addAndMakeVisible (*popup);
    popup->toFront (false);
    positionPopup();
    if (which < 5)
        macros[static_cast<std::size_t> (which)].label->setOpen (true);
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
    if (popupIndex >= 0 && popupIndex < 5)
        macros[static_cast<std::size_t> (popupIndex)].label->setOpen (false);
    if (popupIndex == advancedPopup)
    {
        ospProcessor.setAdvancedOpen (false);
        advancedButton.setToggleState (false, juce::dontSendNotification);
    }
    popup.reset();
    popupIndex = -1;
}

void OspAudioProcessorEditor::positionPopup()
{
    if (popup == nullptr)
        return;
    const auto size = popup->cardSize();
    const int m = MiniPanel::shadowMargin;
    const auto anchor = popupIndex < 5 ? macros[static_cast<std::size_t> (popupIndex)].label->getBounds() : advancedButton.getBounds();
    int x = popupIndex < 5 ? anchor.getCentreX() - size.x / 2 : anchor.getRight() - size.x;
    int y = anchor.getY() - 6 - size.y;
    const auto limits = getLocalBounds().reduced (14);
    x = juce::jlimit (limits.getX(), std::max (limits.getX(), limits.getRight() - size.x), x);
    y = std::max (limits.getY(), y);
    popup->setBounds (x - m, y - m, size.x + 2 * m, size.y + 2 * m);
}

void OspAudioProcessorEditor::updateCustomisedDots()
{
    for (int i = 0; i < 5; ++i)
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
    for (int l = 0; l < OspAudioProcessor::numLayers; ++l)
    {
        occupied[static_cast<std::size_t> (l)] = ospProcessor.isLayerOccupied (l);
        count += occupied[static_cast<std::size_t> (l)] ? 1 : 0;
    }
    const bool countChanged = count != shownCount;
    shownOccupied = occupied;
    shownCount = count;

    // While a sound is dragged over an instrument with room, the next layout is previewed:
    // the cards make space and the new layer's place says ADD LAYER.
    const bool showAdd = dragging && count >= 1 && count < OspAudioProcessor::numLayers;
    const int columns = count + (showAdd ? 1 : 0);
    dropZone.setVisible (count == 0);
    dropZone.setBounds (sourceArea);
    const auto density = columns <= 1 ? EngineLayoutDensity::hero : (columns == 2 ? EngineLayoutDensity::dual : EngineLayoutDensity::triple);
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
        addTarget.setLetter (OspAudioProcessor::layerName (ospProcessor.firstFreeLayer()));
    }
    mixSection.setLayers (occupied);
    updateFocus();
}

void OspAudioProcessorEditor::refreshNow()
{
    timerCallback();
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
        target.layer = ospProcessor.firstFreeLayer();
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
    const auto target = targetAt (where);
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
                                                                         ? "REPLACE " + OspAudioProcessor::layerName (l)
                                                                         : juce::String());
    addTarget.setHighlight (target.kind == DropTarget::Kind::add);
    dropZone.setHighlight (dragging && target.kind == DropTarget::Kind::empty);
}

void OspAudioProcessorEditor::previewDrag (bool on, juce::Point<int> where)
{
    dragging = on;
    layoutSources (false);
    showDropTarget (on ? targetAt (where) : DropTarget {});
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
    showDropTarget (targetAt ({ x, y }));
}

void OspAudioProcessorEditor::fileDragMove (const juce::StringArray&, int x, int y)
{
    const auto target = targetAt ({ x, y });
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
    const auto target = targetAt ({ x, y });
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
                const int first = ospProcessor.firstFreeLayer();
                if (ospProcessor.addLayers (loose) > 0 && folder.isEmpty())
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
    menu.addSubMenu ("Remove layer " + letter, confirm, ospProcessor.isLayerOccupied (layer) && ospProcessor.loadState (layer) != OspAudioProcessor::LoadState::loading);
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
    if (count > 0)
        addLayerSection (menu, ospProcessor.editLayer(), true);
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
    using namespace palette;
    g.fillAll (housing);

    // Identity: OSP/2-OSP and what kind of instrument it is now.
    {
        auto r = logoArea.toFloat();
        auto top = r.removeFromTop (r.getHeight() * 0.66f);
        const auto bold = fonts::make (28.0f, fonts::Weight::semibold, -0.01f);
        const auto light = fonts::make (28.0f, fonts::Weight::regular, -0.01f);
        g.setFont (bold);
        g.setColour (text);
        const float w = juce::GlyphArrangement::getStringWidth (bold, "OSP");
        g.drawText ("OSP", top.removeFromLeft (w + 1.0f), juce::Justification::bottomLeft, false);
        g.setFont (light);
        g.setColour (textDim);
        g.drawText ("/2-OSP", top, juce::Justification::bottomLeft, false);
        const int count = std::max (0, shownCount);
        const char* kind = count <= 1 ? "ONE SOURCE INSTRUMENT" : (count == 2 ? "TWO LAYER INSTRUMENT" : "THREE LAYER INSTRUMENT");
        g.setFont (fonts::make (9.5f, fonts::Weight::medium, 0.28f));
        g.drawText (kind, r.withTrimmedTop (3.0f), juce::Justification::topLeft, false);
    }
    g.setColour (textDim);
    g.setFont (fonts::label (9.5f));
    g.drawText ("VOLUME", volumeCaption, juce::Justification::centred, false);

    // The macros and the envelope share one panel.
    {
        const auto r = lowerPanel.toFloat().reduced (1.0f);
        juce::Path shape;
        shape.addRoundedRectangle (r, 10.0f);
        juce::DropShadow (juce::Colour (0x141e1c18), 3, { 0, 1 }).drawForPath (g, shape);
        g.setColour (raised);
        g.fillPath (shape);
        g.setColour (hairline);
        g.strokePath (shape, juce::PathStrokeType (1.0f));
        const float divider = static_cast<float> (envelope.getX()) - 18.0f;
        g.drawVerticalLine (juce::roundToInt (divider), r.getY() + 14.0f, r.getBottom() - 14.0f);
        g.setColour (text);
        g.setFont (fonts::label (11.5f));
        g.drawText ("MACROS", lowerPanel.reduced (18, 12).removeFromTop (16).toFloat(), juce::Justification::centredLeft, false);
    }

    // The keyboard's bed.
    {
        const auto bed = keyboard.getBounds().toFloat().expanded (2.0f);
        g.setColour (hairline);
        g.fillRoundedRectangle (bed, 4.0f);
    }
}

void OspAudioProcessorEditor::resized()
{
    auto area = getLocalBounds().reduced (18, 14);

    // Header: identity left, preset in the middle, volume and menu right.
    headerArea = area.removeFromTop (54);
    {
        auto header = headerArea;
        logoArea = header.removeFromLeft (260).withSizeKeepingCentre (260, 46);
        menuButton.setBounds (header.removeFromRight (32).withSizeKeepingCentre (32, 32));
        header.removeFromRight (10);
        auto vol = header.removeFromRight (60);
        volume.setBounds (vol.withHeight (40).withSizeKeepingCentre (40, 40).withY (vol.getY()));
        volumeCaption = vol.withTop (volume.getBottom() + 1).withHeight (12);
        presetBar.setBounds (headerArea.withSizeKeepingCentre (std::min (340, headerArea.getWidth() - 2 * 300), 36));
    }
    area.removeFromTop (12);

    // Bottom: status left, Advanced right (under the keyboard's right end).
    auto bottom = area.removeFromBottom (26);
    advancedButton.setBounds (bottom.removeFromRight (124));
    statusLabel.setBounds (bottom.withTrimmedLeft (4));
    area.removeFromBottom (8);

    auto keys = area.removeFromBottom (80);
    pitchWheel.setBounds (keys.removeFromLeft (30));
    keys.removeFromLeft (4);
    modWheel.setBounds (keys.removeFromLeft (30));
    keys.removeFromLeft (10);
    keyboard.setBounds (keys.reduced (2, 2));
    keyboard.setKeyWidth (static_cast<float> (keyboard.getWidth()) / 52.0f); // 88 keys = 52 white keys
    area.removeFromBottom (12);

    // Macros and envelope.
    lowerPanel = area.removeFromBottom (160);
    {
        auto inner = lowerPanel.reduced (18, 12);
        envelope.setBounds (inner.removeFromRight (juce::roundToInt (inner.getWidth() * 0.34f)));
        inner.removeFromRight (36);
        inner.removeFromTop (18);
        const int cell = inner.getWidth() / static_cast<int> (macros.size());
        for (auto& knob : macros)
        {
            auto c = inner.removeFromLeft (cell);
            knob.label->setBounds (c.removeFromTop (20));
            c.removeFromTop (2);
            const int side = std::min ({ c.getWidth() - 8, c.getHeight() - 4, 96 });
            knob.slider.setBounds (c.withSizeKeepingCentre (side, side).withY (c.getY()));
        }
    }
    area.removeFromBottom (12);

    // The mix band (fixed height), then the sources take the rest.
    mixSection.setBounds (area.removeFromBottom (64));
    area.removeFromBottom (12);
    sourceArea = area;
    layoutSources (false);
    positionPopup();
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
        const bool occupied = ospProcessor.isLayerOccupied (l);
        changed = changed || occupied != shownOccupied[static_cast<std::size_t> (l)];
        count += occupied ? 1 : 0;
    }
    if (changed || shownCount < 0)
    {
        const bool animate = shownCount >= 1 && count >= 1;
        layoutSources (animate);
        repaint (logoArea);
    }
    for (auto& card : cards)
        if (card->isVisible())
            card->refresh();
    updateFocus();
    presetBar.refresh();
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
    if (shownCount == 3)
        mixSection.repaint();
    envelope.repaint (envelope.graphBounds());
    updateCustomisedDots();
    updateStatus();
}

} // namespace osp::plugin
