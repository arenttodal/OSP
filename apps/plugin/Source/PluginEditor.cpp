#include "PluginEditor.h"

#include "core/PitchMath.h"
#include "io/AudioFileIO.h"

namespace osp::plugin
{

namespace colours
{
    const juce::Colour background { 0xff15161a };
    const juce::Colour panel { 0xff1e2026 };
    const juce::Colour wave { 0xffd8c7a3 };
    const juce::Colour text { 0xffe8e6e1 };
    const juce::Colour dim { 0xff8a8d96 };
    const juce::Colour accent { 0xffe0a458 };
}

//==============================================================================
void WaveformView::setInstrument (std::shared_ptr<const LoadedInstrument> newInstrument)
{
    instrument = std::move (newInstrument);
    repaint();
}

void WaveformView::setLoading (bool isLoading)
{
    if (loading != isLoading)
    {
        loading = isLoading;
        repaint();
    }
}

void WaveformView::setDragHighlight (bool on)
{
    dragHighlight = on;
    repaint();
}

void WaveformView::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    g.setColour (colours::panel);
    g.fillRoundedRectangle (bounds, 6.0f);

    if (instrument != nullptr && ! instrument->peakMax.empty())
    {
        const auto& lo = instrument->peakMin;
        const auto& hi = instrument->peakMax;
        const float mid = bounds.getCentreY();
        float maxAbs = 1.0e-4f; // display is scaled to the recording's own peak so quiet sources stay visible
        for (std::size_t i = 0; i < hi.size(); ++i)
            maxAbs = std::max ({ maxAbs, hi[i], -lo[i] });
        const float scale = bounds.getHeight() * 0.45f / maxAbs;
        const int width = getWidth();
        g.setColour (colours::wave.withAlpha (loading ? 0.3f : 0.85f));
        for (int x = 0; x < width; ++x)
        {
            const auto b = static_cast<std::size_t> (static_cast<double> (x) / width * static_cast<double> (hi.size()));
            const float top = mid - hi[std::min (b, hi.size() - 1)] * scale;
            const float bottom = mid - lo[std::min (b, lo.size() - 1)] * scale;
            g.drawVerticalLine (x, top, std::max (top + 1.0f, bottom));
        }
    }
    if (instrument != nullptr && instrument->startSeconds > 0.0 && instrument->durationSeconds > 0.0)
    {
        // Notes start here (analysed onset), not at the beginning of the file.
        const auto x = static_cast<float> (instrument->startSeconds / instrument->durationSeconds) * bounds.getWidth();
        g.setColour (colours::accent.withAlpha (0.8f));
        g.drawVerticalLine (static_cast<int> (x), bounds.getY(), bounds.getBottom());
    }

    if ((instrument == nullptr || instrument->peakMax.empty()) && ! loading)
    {
        g.setColour (colours::text);
        g.setFont (juce::FontOptions (28.0f, juce::Font::bold));
        g.drawText ("DROP A SOUND", getLocalBounds().withTrimmedBottom (24), juce::Justification::centred);
        g.setColour (colours::dim);
        g.setFont (juce::FontOptions (14.0f));
        g.drawText (juce::String::fromUTF8 ("WAV \xc2\xb7 AIFF \xc2\xb7 FLAC"), getLocalBounds().withTrimmedTop (40), juce::Justification::centred);
    }

    if (loading)
    {
        g.setColour (colours::text);
        g.setFont (juce::FontOptions (20.0f, juce::Font::bold));
        g.drawText (juce::String::fromUTF8 ("ANALYZING\xe2\x80\xa6"), getLocalBounds(), juce::Justification::centred);
    }

    if (dragHighlight)
    {
        g.setColour (colours::accent);
        g.drawRoundedRectangle (bounds.reduced (1.5f), 6.0f, 3.0f);
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
    g.setColour (colours::panel);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 6.0f);
}

void SamplesPanel::setInstrument (std::shared_ptr<const LoadedInstrument> instrument)
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
        header->name.setFont (juce::FontOptions (14.0f, juce::Font::bold));
        header->name.setColour (juce::Label::textColourId, colours::text);
        content.addAndMakeVisible (header->name);
        rows.push_back (std::move (header));
        for (int id : group.members)
        {
            const auto& member = set.members[static_cast<std::size_t> (id)];
            auto row = std::make_unique<Row>();
            row->name.setText (juce::String::fromUTF8 (member.filename.c_str()), juce::dontSendNotification);
            row->name.setColour (juce::Label::textColourId, colours::text);
            row->info.setText (member.userAssigned ? juce::String ("set by you")
                                                   : "auto " + juce::String (static_cast<int> (std::lround (member.confidence * 100))) + "%",
                               juce::dontSendNotification);
            row->info.setColour (juce::Label::textColourId, colours::dim);
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
OspAudioProcessorEditor::OspAudioProcessorEditor (OspAudioProcessor& p)
    : AudioProcessorEditor (p),
      ospProcessor (p),
      keyboard (p.keyboardState, juce::MidiKeyboardComponent::horizontalKeyboard)
{
    addAndMakeVisible (waveform);

    for (auto* label : { &rootLabel, &characterLabel, &detailLabel, &statusLabel })
    {
        label->setColour (juce::Label::textColourId, colours::text);
        addAndMakeVisible (*label);
    }
    rootLabel.setFont (juce::FontOptions (34.0f, juce::Font::bold));
    characterLabel.setFont (juce::FontOptions (15.0f, juce::Font::bold));
    detailLabel.setColour (juce::Label::textColourId, colours::dim);
    statusLabel.setColour (juce::Label::textColourId, colours::dim);

    rootBox.addItem ("Root: auto", 1);
    for (int note = 0; note < 128; ++note)
        rootBox.addItem ("Root: " + juce::String (midiNoteName (note)), note + 2);
    rootBox.onChange = [this] {
        const int id = rootBox.getSelectedId();
        if (id == 1)
            ospProcessor.changeRootOverride (std::nullopt);
        else if (id >= 2)
            ospProcessor.changeRootOverride (static_cast<double> (id - 2));
        refreshInstrumentInfo();
    };
    rootBox.setTitle ("Root note");
    addAndMakeVisible (rootBox);

    loadButton.onClick = [this] { chooseFile(); };
    exampleButton.onClick = [this] { ospProcessor.loadExample(); };
    addAndMakeVisible (loadButton);
    addAndMakeVisible (exampleButton);
    samplesButton.setClickingTogglesState (true);
    samplesButton.setTooltip ("How the dropped files were combined (pitch, velocity layers, round robins)");
    samplesButton.onClick = [this] { samplesPanel.setVisible (samplesButton.getToggleState()); };
    addAndMakeVisible (samplesButton);
    addChildComponent (samplesPanel);

    // Starting states (spec §101) and the menu (presets, instrument files, undo, size).
    for (int i = 0; i < ospProcessor.getNumPrograms(); ++i)
        stateBox.addItem (ospProcessor.getProgramName (i), i + 1);
    stateBox.setSelectedId (ospProcessor.getCurrentProgram() + 1, juce::dontSendNotification);
    stateBox.setTitle ("Starting state");
    stateBox.setTooltip ("Starting states change how the instrument behaves; your sound stays");
    stateBox.onChange = [this] { ospProcessor.setCurrentProgram (stateBox.getSelectedId() - 1); };
    addAndMakeVisible (stateBox);
    menuButton.setTitle ("Menu");
    menuButton.setTooltip ("Presets, instrument files, undo, size");
    menuButton.onClick = [this] { showMenu(); };
    addAndMakeVisible (menuButton);
    setWantsKeyboardFocus (true);

    auto setupKnob = [this] (Knob& knob, const char* id, const char* name, bool large) {
        knob.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 70, 18);
        knob.slider.setColour (juce::Slider::rotarySliderFillColourId, large ? colours::accent : colours::dim);
        knob.label.setText (juce::String::fromUTF8 (name), juce::dontSendNotification);
        knob.label.setJustificationType (juce::Justification::centred);
        knob.label.setColour (juce::Label::textColourId, large ? colours::text : colours::dim);
        if (large)
            knob.label.setFont (juce::FontOptions (14.0f, juce::Font::bold));
        knob.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (ospProcessor.parameters, id, knob.slider);
        knob.slider.setTitle (juce::String::fromUTF8 (name));
        addAndMakeVisible (knob.slider);
        addAndMakeVisible (knob.label);
    };
    const std::array<std::pair<const char*, const char*>, 6> macroInfo { {
        { "life", "LIFE" }, { "dynamics", "DYNAMICS" }, { "character", "CHARACTER" },
        { "motion", "MOTION" }, { "space", "SPACE" }, { "reimagined", "ORIGINAL \xe2\x86\x94 REIMAGINED" } } };
    for (std::size_t i = 0; i < macros.size(); ++i)
        setupKnob (macros[i], macroInfo[i].first, macroInfo[i].second, true);
    const std::array<std::pair<const char*, const char*>, 6> knobInfo { {
        { "attack", "Attack" }, { "release", "Release" }, { "velocityRange", "Velocity" },
        { "fineTune", "Fine" }, { "bendRange", "Bend" }, { "gain", "Output" } } };
    for (std::size_t i = 0; i < knobs.size(); ++i)
        setupKnob (knobs[i], knobInfo[i].first, knobInfo[i].second, false);

    pitchCharacterBox.addItemList ({ "Tape", "Natural" }, 1);
    sustainBox.addItemList ({ "Recording", "Endless" }, 1);
    pitchCharacterBox.setTitle ("Pitch character");
    sustainBox.setTitle ("Sustain");
    pitchCharacterAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (ospProcessor.parameters, "pitchCharacter", pitchCharacterBox);
    sustainAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (ospProcessor.parameters, "sustain", sustainBox);
    for (auto* label : { &pitchCharacterLabel, &sustainLabel, &advancedLabel })
    {
        label->setColour (juce::Label::textColourId, colours::dim);
        addAndMakeVisible (*label);
    }
    pitchCharacterLabel.setText ("Pitch character", juce::dontSendNotification);
    sustainLabel.setText ("Sustain", juce::dontSendNotification);
    advancedLabel.setText ("ADVANCED", juce::dontSendNotification);
    advancedLabel.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    addAndMakeVisible (pitchCharacterBox);
    addAndMakeVisible (sustainBox);
    reseedButton.setTooltip ("New variation seed: repeated notes vary differently");
    reseedButton.onClick = [this] {
        if (auto* seed = ospProcessor.parameters.getParameter ("seed"))
        {
            const auto range = ospProcessor.parameters.getParameterRange ("seed");
            const float next = std::fmod (range.convertFrom0to1 (seed->getValue()), 9999.0f) + 1.0f;
            seed->setValueNotifyingHost (range.convertTo0to1 (next));
        }
    };
    addAndMakeVisible (reseedButton);
    mpeToggle.setTooltip ("MPE controllers: per-note pitch bend (+/-48 st), pressure and slide");
    mpeToggle.setColour (juce::ToggleButton::textColourId, colours::dim);
    mpeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (ospProcessor.parameters, "mpe", mpeToggle);
    addAndMakeVisible (mpeToggle);

    keyboard.setAvailableRange (21, 108);
    keyboard.setOctaveForMiddleC (4); // MIDI 60 = C4, as everywhere else in OSP
    keyboard.setKeyWidth (14.0f);
    addAndMakeVisible (keyboard);

    setResizable (true, true);
    setResizeLimits (720, 560, 1800, 1200);
    setSize (920, 680);
    setScaleFactor (ospProcessor.uiScale());

    refreshInstrumentInfo();
    startTimerHz (15);
}

OspAudioProcessorEditor::~OspAudioProcessorEditor()
{
    stopTimer();
}

void OspAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (colours::background);
}

void OspAudioProcessorEditor::resized()
{
    auto area = getLocalBounds().reduced (14);

    auto header = area.removeFromTop (64);
    auto buttons = header.removeFromRight (220);
    auto topRow = buttons.removeFromTop (28);
    menuButton.setBounds (topRow.removeFromRight (34));
    topRow.removeFromRight (6);
    loadButton.setBounds (topRow.removeFromRight (80));
    topRow.removeFromRight (6);
    exampleButton.setBounds (topRow);
    stateBox.setBounds (header.removeFromRight (150).removeFromTop (28).translated (-10, 0));
    auto rootRow = buttons.removeFromBottom (28);
    samplesButton.setBounds (rootRow.removeFromLeft (80));
    rootRow.removeFromLeft (6);
    rootBox.setBounds (rootRow);
    rootLabel.setBounds (header.removeFromLeft (110));
    characterLabel.setBounds (header.removeFromTop (30));
    detailLabel.setBounds (header);

    area.removeFromTop (6);
    statusLabel.setBounds (area.removeFromBottom (22));
    keyboard.setBounds (area.removeFromBottom (80));
    keyboard.setKeyWidth (static_cast<float> (keyboard.getWidth()) / 52.0f); // 88 keys = 52 white keys
    area.removeFromBottom (8);

    // Advanced row: small knobs plus pitch character / sustain / reseed.
    auto advanced = area.removeFromBottom (106);
    advancedLabel.setBounds (advanced.removeFromTop (16));
    auto choices = advanced.removeFromRight (240);
    auto row1 = choices.removeFromTop (26);
    pitchCharacterLabel.setBounds (row1.removeFromLeft (110));
    pitchCharacterBox.setBounds (row1);
    choices.removeFromTop (6);
    auto row2 = choices.removeFromTop (26);
    sustainLabel.setBounds (row2.removeFromLeft (110));
    sustainBox.setBounds (row2);
    choices.removeFromTop (6);
    auto row3 = choices.removeFromTop (24);
    reseedButton.setBounds (row3.removeFromRight (110));
    mpeToggle.setBounds (row3.removeFromLeft (80));
    const int smallWidth = advanced.getWidth() / static_cast<int> (knobs.size());
    for (auto& knob : knobs)
    {
        auto cell = advanced.removeFromLeft (smallWidth);
        knob.label.setBounds (cell.removeFromTop (16));
        knob.slider.setBounds (cell);
    }
    area.removeFromBottom (10);

    // The instrument's primary controls.
    auto macroRow = area.removeFromBottom (130);
    const int macroWidth = macroRow.getWidth() / static_cast<int> (macros.size());
    for (auto& knob : macros)
    {
        auto cell = macroRow.removeFromLeft (macroWidth);
        knob.label.setBounds (cell.removeFromTop (20));
        knob.slider.setBounds (cell);
    }
    area.removeFromBottom (8);
    waveform.setBounds (area);
    samplesPanel.setBounds (area);
}

bool OspAudioProcessorEditor::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (const auto& f : files)
        if (juce::File (f).isDirectory() || io::isSupportedAudioExtension (std::filesystem::path (f.toStdString())))
            return true;
    return false;
}

void OspAudioProcessorEditor::filesDropped (const juce::StringArray& files, int, int)
{
    waveform.setDragHighlight (false);
    juce::Array<juce::File> audioFiles;
    for (const auto& f : files)
    {
        const juce::File file (f);
        if (file.isDirectory())
        {
            // A dropped folder is a set: every supported file directly inside it.
            for (const auto& entry : juce::RangedDirectoryIterator (file, false, "*", juce::File::findFiles))
                if (io::isSupportedAudioExtension (std::filesystem::path (entry.getFile().getFullPathName().toStdString())))
                    audioFiles.add (entry.getFile());
        }
        else if (io::isSupportedAudioExtension (std::filesystem::path (f.toStdString())))
            audioFiles.add (file);
    }
    if (! audioFiles.isEmpty())
        ospProcessor.loadFiles (audioFiles);
}

bool OspAudioProcessorEditor::keyPressed (const juce::KeyPress& key)
{
    const bool command = key.getModifiers().isCommandDown();
    if (command && key.getKeyCode() == 'Z')
    {
        if (key.getModifiers().isShiftDown())
            ospProcessor.undoManager.redo();
        else
            ospProcessor.undoManager.undo();
        refreshInstrumentInfo();
        return true;
    }
    return false;
}

void OspAudioProcessorEditor::showMenu()
{
    juce::PopupMenu menu;
    menu.addItem ("Save preset...", [this] { choosePresetFile (true, false); });
    menu.addItem ("Load preset...", [this] { choosePresetFile (false, false); });
    menu.addSeparator();
    menu.addItem ("Export instrument...", ospProcessor.currentInstrument() != nullptr, false, [this] { choosePresetFile (true, true); });
    menu.addItem ("Import instrument...", [this] { choosePresetFile (false, true); });
    menu.addSeparator();
    menu.addItem ("Undo " + ospProcessor.undoManager.getUndoDescription(), ospProcessor.undoManager.canUndo(), false,
                  [this] { ospProcessor.undoManager.undo(); refreshInstrumentInfo(); });
    menu.addItem ("Redo " + ospProcessor.undoManager.getRedoDescription(), ospProcessor.undoManager.canRedo(), false,
                  [this] { ospProcessor.undoManager.redo(); refreshInstrumentInfo(); });
    juce::PopupMenu size;
    for (int percent : { 80, 100, 125, 150, 200 })
        size.addItem (juce::String (percent) + " %", true, std::abs (ospProcessor.uiScale() * 100.0f - static_cast<float> (percent)) < 1.0f,
                      [this, percent] {
                          ospProcessor.setUiScale (static_cast<float> (percent) / 100.0f);
                          setScaleFactor (ospProcessor.uiScale());
                      });
    menu.addSubMenu ("Interface size", size);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (menuButton));
}

void OspAudioProcessorEditor::choosePresetFile (bool save, bool instrument)
{
    const juce::String pattern = juce::String ("*") + (instrument ? OspAudioProcessor::instrumentExtension : OspAudioProcessor::presetExtension);
    chooser = std::make_unique<juce::FileChooser> (save ? (instrument ? "Export instrument" : "Save preset") : (instrument ? "Import instrument" : "Load preset"),
                                                   juce::File::getSpecialLocation (juce::File::userDocumentsDirectory), pattern);
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
    });
}

void OspAudioProcessorEditor::chooseFile()
{
    chooser = std::make_unique<juce::FileChooser> ("Choose a sound", juce::File(), "*.wav;*.wave;*.aif;*.aiff;*.aifc;*.flac");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles
                              | juce::FileBrowserComponent::canSelectMultipleItems,
                          [this] (const juce::FileChooser& fc) {
                              juce::Array<juce::File> files;
                              for (const auto& f : fc.getResults())
                                  if (f.existsAsFile())
                                      files.add (f);
                              if (! files.isEmpty())
                                  ospProcessor.loadFiles (files);
                          });
}

void OspAudioProcessorEditor::refreshInstrumentInfo()
{
    const auto instrument = ospProcessor.currentInstrument();
    waveform.setInstrument (instrument);
    samplesPanel.setInstrument (instrument);
    const bool isSet = instrument != nullptr && instrument->set != nullptr;
    samplesButton.setEnabled (isSet);
    if (! isSet && samplesButton.getToggleState())
    {
        samplesButton.setToggleState (false, juce::dontSendNotification);
        samplesPanel.setVisible (false);
    }

    const auto overrideMidi = ospProcessor.rootOverride();
    rootBox.setSelectedId (overrideMidi ? static_cast<int> (std::lround (*overrideMidi)) + 2 : 1, juce::dontSendNotification);

    if (instrument == nullptr)
    {
        rootLabel.setText ("", juce::dontSendNotification);
        characterLabel.setText ("", juce::dontSendNotification);
        detailLabel.setText ("", juce::dontSendNotification);
        return;
    }

    const auto& pitch = instrument->analysis.pitch;
    juce::String root;
    if (overrideMidi)
        root = midiNoteName (static_cast<int> (std::lround (*overrideMidi)));
    else if (pitch.detected)
        root = pitch.noteName;
    else
        root = "?";
    rootLabel.setText (root, juce::dontSendNotification);
    characterLabel.setText (juce::String::fromUTF8 (instrument->character.c_str()), juce::dontSendNotification);

    const auto dot = juce::String::fromUTF8 ("  \xc2\xb7  ");
    juce::String detail = juce::String (instrument->filename) + dot + juce::String (instrument->durationSeconds, 2) + " s";
    if (pitch.midiNote >= 0)
        detail << dot << "detected " << pitch.noteName << " " << (pitch.centsOffset >= 0 ? "+" : "")
               << juce::String (static_cast<int> (std::lround (pitch.centsOffset))) << "c (" << pitch.confidenceLevel << ")";
    else
        detail << dot << "no pitch detected - choose a root";
    detailLabel.setText (detail, juce::dontSendNotification);
}

void OspAudioProcessorEditor::timerCallback()
{
    const auto state = ospProcessor.loadState();
    waveform.setLoading (state == OspAudioProcessor::LoadState::loading);
    if (stateBox.getSelectedId() != ospProcessor.getCurrentProgram() + 1)
        stateBox.setSelectedId (ospProcessor.getCurrentProgram() + 1, juce::dontSendNotification);

    const auto instrument = ospProcessor.currentInstrument();
    const auto generation = instrument != nullptr ? instrument->generation : 0;
    if (generation != shownGeneration || state != shownState)
    {
        shownGeneration = generation;
        shownState = state;
        refreshInstrumentInfo();
    }

    juce::String status;
    switch (state)
    {
        case OspAudioProcessor::LoadState::empty: status = "No sound loaded"; break;
        case OspAudioProcessor::LoadState::loading: status = juce::String::fromUTF8 ("Understanding pitch\xe2\x80\xa6"); break;
        case OspAudioProcessor::LoadState::ready: status = ospProcessor.stageMessage(); break;
        case OspAudioProcessor::LoadState::failed: status = "Could not load that file"; break;
    }
    const auto dot = juce::String::fromUTF8 ("  \xc2\xb7  ");
    status << dot << "voices " << ospProcessor.activeVoices.load();
    const auto message = ospProcessor.statusMessage();
    if (message.isNotEmpty())
        status << dot << message;
    statusLabel.setText (status, juce::dontSendNotification);
}

} // namespace osp::plugin
