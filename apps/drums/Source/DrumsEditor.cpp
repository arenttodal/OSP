#include "DrumsEditor.h"

#include "Design.h"

#include "core/PitchMath.h"

namespace osp::drumsplugin
{

namespace
{
    namespace colour = plugin::design::colour;
    namespace type = plugin::type;
    using plugin::fonts::Weight;

    constexpr int editorWidth = 1120;
    constexpr int editorHeight = 720;

    const char* const categoryNames[] = { "Kick", "Snare", "Clap", "Closed Hat", "Open Hat", "Tom", "Rim", "Cymbal", "Percussion", "Unknown" };

    void raisedPanel (juce::Graphics& g, juce::Rectangle<float> r, float radius = 10.0f)
    {
        g.setGradientFill (juce::ColourGradient (colour::panelTop, r.getX(), r.getY(), colour::panelBottom, r.getX(), r.getBottom(), false));
        g.fillRoundedRectangle (r, radius);
        g.setColour (colour::hairline);
        g.drawRoundedRectangle (r.reduced (0.5f), radius, 1.0f);
    }

    juce::String noteName (int note)
    {
        return juce::String (midiNoteName (note)) + " (" + juce::String (note) + ")";
    }
} // namespace

// ---------------------------------------------------------------------------------------

PadView::PadView (DrumsEditor& editor, int pad) : owner (editor), padIndex (pad)
{
    setRepaintsOnMouseActivity (false);
}

void PadView::refresh()
{
    auto& proc = owner.processor();
    const auto hits = proc.padHits (padIndex);
    const int voices = proc.padVoices (padIndex);
    bool dirty = false;
    if (hits != lastHits)
    {
        lastHits = hits;
        flash = 1.0f;
        dirty = true;
    }
    else if (flash > 0.0f)
    {
        flash = std::max (0.0f, flash - 0.12f);
        dirty = true;
    }
    if (voices != lastVoices)
    {
        lastVoices = voices;
        dirty = true;
    }
    if (dirty)
        repaint();
}

void PadView::paint (juce::Graphics& g)
{
    auto& proc = owner.processor();
    const auto& info = proc.padInfo (padIndex);
    const bool selected = proc.selectedPad == padIndex;
    const auto accent = DrumsEditor::categoryColour (info.category);
    auto r = getLocalBounds().toFloat().reduced (5.0f);

    // A raised ivory pad; the category colour as a quiet strip and a glow when it plays.
    g.setColour (juce::Colours::black.withAlpha (0.10f));
    g.fillRoundedRectangle (r.translated (0.0f, 2.0f), 9.0f);
    g.setGradientFill (juce::ColourGradient (colour::cardTop, r.getX(), r.getY(), colour::cardBottom, r.getX(), r.getBottom(), false));
    g.fillRoundedRectangle (r, 9.0f);
    if (flash > 0.0f || lastVoices > 0)
    {
        g.setColour (accent.withAlpha (0.10f + 0.28f * flash));
        g.fillRoundedRectangle (r, 9.0f);
    }
    g.setColour (selected ? accent.darker (0.2f) : colour::hairline);
    g.drawRoundedRectangle (r.reduced (0.5f), 9.0f, selected ? 2.2f : 1.0f);
    if (dragOver)
    {
        g.setColour (colour::accent);
        g.drawRoundedRectangle (r.reduced (2.0f), 8.0f, 2.0f);
    }
    g.setColour (accent);
    g.fillRoundedRectangle (r.withWidth (5.0f).reduced (0.0f, 12.0f).translated (9.0f, 0.0f), 2.5f);

    // Short pads (3 x 3) put the note beside the name, so the waveform keeps its height.
    const bool compact = r.getHeight() < 120.0f;
    auto inner = r.reduced (22.0f, compact ? 9.0f : 12.0f);
    auto top = inner.removeFromTop (22.0f);
    g.setColour (colour::text);
    g.setFont (type::controlLabel (1.05f));
    g.drawFittedText (proc.padDisplayName (padIndex).toUpperCase(), top.toNearestInt(), juce::Justification::centredLeft, 1);
    g.setColour (compact ? colour::textSecondary : colour::textMicro);
    g.setFont (type::micro (compact ? 13.0f : 12.5f));
    g.drawFittedText (compact ? owner.noteText (padIndex) : juce::String (padIndex + 1), top.toNearestInt(), juce::Justification::centredRight, 1);

    auto bottom = compact ? juce::Rectangle<float>() : inner.removeFromBottom (18.0f);
    g.setColour (colour::textSecondary);
    g.setFont (type::micro (13.0f));
    if (! compact)
        g.drawFittedText (owner.noteText (padIndex), bottom.toNearestInt(), juce::Justification::centredLeft, 1);
    else
        bottom = top;
    auto well = inner.reduced (0.0f, compact ? 4.0f : 6.0f);
    const bool muted = proc.parameters().getRawParameterValue (DrumsProcessor::padParamId (padIndex, "mute"))->load() > 0.5f;
    const bool soloed = proc.parameters().getRawParameterValue (DrumsProcessor::padParamId (padIndex, "solo"))->load() > 0.5f;
    const auto drawModeTag = [&] {
        if (! (muted || soloed))
            return;
        g.setColour (soloed ? colour::accent : colour::textMicro);
        g.drawFittedText (soloed ? "SOLO" : "MUTE", compact ? well.toNearestInt().reduced (6, 2) : bottom.toNearestInt(),
                          compact ? juce::Justification::topRight : juce::Justification::centredRight, 1);
    };

    // A compact waveform (a dark well, as OSP draws sounds), or the pad's state.
    g.setColour (colour::wellA);
    g.fillRoundedRectangle (well, 5.0f);
    if (info.sound != nullptr && ! info.sound->peakMax.empty())
    {
        const auto& lo = info.sound->peakMin;
        const auto& hi = info.sound->peakMax;
        const int n = static_cast<int> (hi.size());
        juce::Path wave;
        const float mid = well.getCentreY(), half = well.getHeight() * 0.45f;
        const int columns = std::max (1, static_cast<int> (well.getWidth() - 6.0f));
        for (int x = 0; x < columns; ++x)
        {
            const int b = std::min (n - 1, x * n / columns);
            const float px = well.getX() + 3.0f + static_cast<float> (x);
            wave.addRectangle (px, mid - half * std::min (1.0f, hi[static_cast<std::size_t> (b)]), 1.0f,
                               std::max (0.6f, half * (std::min (1.0f, hi[static_cast<std::size_t> (b)]) - std::max (-1.0f, lo[static_cast<std::size_t> (b)]))));
        }
        g.setColour (accent.interpolatedWith (juce::Colours::white, 0.25f).withAlpha (0.9f));
        g.fillPath (wave);
    }
    else
    {
        g.setColour (colour::wellText);
        g.setFont (type::micro (12.5f));
        const juce::String state = info.loading ? "Loading..." : ! info.error.empty() ? "Could not load" : ! info.missingFilename.empty() ? "Missing: " + juce::String::fromUTF8 (info.missingFilename.c_str()) : "Drop a sample";
        g.drawFittedText (state, well.toNearestInt().reduced (6, 0), juce::Justification::centred, 2);
    }
    drawModeTag();
}

void PadView::mouseDown (const juce::MouseEvent& e)
{
    owner.selectPad (padIndex);
    // Louder towards the top of the pad, like a velocity-sensitive pad.
    const float y = 1.0f - static_cast<float> (e.y) / static_cast<float> (std::max (1, getHeight()));
    owner.processor().auditionPad (padIndex, juce::jlimit (30, 127, static_cast<int> (50.0f + 77.0f * y)));
}

void PadView::mouseUp (const juce::MouseEvent&)
{
    owner.processor().releaseAuditionedPad (padIndex);
}

bool PadView::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (const auto& f : files)
        if (juce::File (f).hasFileExtension ("wav;aif;aiff;flac;caf"))
            return true;
    return false;
}

void PadView::filesDropped (const juce::StringArray& files, int, int)
{
    dragOver = false;
    // Milestone 1: the first audio file. Several files onto one pad (round robin, velocity
    // layers, stacks) arrive with Milestone 3's import choices.
    for (const auto& f : files)
        if (juce::File (f).hasFileExtension ("wav;aif;aiff;flac;caf"))
        {
            owner.processor().loadPad (padIndex, juce::File (f));
            owner.selectPad (padIndex);
            break;
        }
    repaint();
}

// ---------------------------------------------------------------------------------------

DrumsEditor::DrumsEditor (DrumsProcessor& p) : AudioProcessorEditor (p), proc (p)
{
    setLookAndFeel (&lookAndFeel);

    auto segment = [this] (juce::TextButton& b, int group) {
        b.setClickingTogglesState (true);
        b.setRadioGroupId (group);
        addAndMakeVisible (b);
    };
    segment (padMode, 1);
    segment (expandedMode, 1);
    padMode.onClick = [this] { proc.setKeyboardMode (drums::KeyboardMode::pad); };
    expandedMode.onClick = [this] { proc.setKeyboardMode (drums::KeyboardMode::expanded); };
    segment (layout8, 2);
    segment (layout9, 2);
    layout8.onClick = [this] { proc.nineLayout = false; layoutPads(); };
    layout9.onClick = [this] { proc.nineLayout = true; layoutPads(); };
    ninthPadChip.setTooltip ("Pad 9 holds a sound; it keeps playing in the 2 x 4 view. Click to edit it.");
    ninthPadChip.onClick = [this] { selectPad (8); };
    addChildComponent (ninthPadChip);

    for (int i = 0; i < DrumsProcessor::numPads; ++i)
    {
        padViews[static_cast<std::size_t> (i)] = std::make_unique<PadView> (*this, i);
        addAndMakeVisible (*padViews[static_cast<std::size_t> (i)]);
    }

    padName.setEditable (false, true, false);
    padName.setFont (type::sectionTitle());
    padName.setColour (juce::Label::textColourId, colour::text);
    padName.onTextChange = [this] { proc.setPadName (proc.selectedPad, padName.getText()); };
    addAndMakeVisible (padName);
    padStatus.setFont (type::micro (14.0f));
    padStatus.setColour (juce::Label::textColourId, colour::textSecondary);
    addAndMakeVisible (padStatus);

    auto knob = [this] (juce::Slider& s, juce::Label& l, const juce::String& text, bool bipolar) {
        s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 70, 18);
        if (bipolar)
            s.getProperties().set ("bipolar", true);
        addAndMakeVisible (s);
        l.setText (text, juce::dontSendNotification);
        l.setFont (type::controlLabel());
        l.setJustificationType (juce::Justification::centred);
        l.setColour (juce::Label::textColourId, colour::textSecondary);
        addAndMakeVisible (l);
    };
    knob (gain, gainLabel, "GAIN", false);
    knob (pan, panLabel, "PAN", true);
    knob (tune, tuneLabel, "TUNE", true);
    for (auto* b : { &mute, &solo })
    {
        b->setClickingTogglesState (true);
        addAndMakeVisible (*b);
    }
    loadButton.onClick = [this] {
        chooser = std::make_unique<juce::FileChooser> ("Load a sample onto " + proc.padDisplayName (proc.selectedPad), juce::File(), "*.wav;*.aif;*.aiff;*.flac;*.caf");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this] (const juce::FileChooser& fc) {
            if (fc.getResult().existsAsFile())
                proc.loadPad (proc.selectedPad, fc.getResult());
        });
    };
    clearButton.onClick = [this] { proc.clearPad (proc.selectedPad); };
    addAndMakeVisible (loadButton);
    addAndMakeVisible (clearButton);

    for (int i = 0; i < 10; ++i)
        category.addItem (categoryNames[i], i + 1);
    category.onChange = [this] {
        const auto c = static_cast<drums::DrumCategory> (category.getSelectedId() - 1);
        if (c != proc.padInfo (proc.selectedPad).category)
            proc.setPadCategory (proc.selectedPad, c, true);
    };
    choke.addItem ("No choke", 1);
    for (int i = 1; i <= 8; ++i)
        choke.addItem ("Choke " + juce::String (i), i + 1);
    choke.onChange = [this] { proc.setPadChoke (proc.selectedPad, choke.getSelectedId() - 1); };
    voiceMode.addItem ("Poly", 1);
    voiceMode.addItem ("Mono retrigger", 2);
    voiceMode.onChange = [this] { proc.setPadVoiceMode (proc.selectedPad, voiceMode.getSelectedId() == 2 ? drums::VoiceMode::monoRetrigger : drums::VoiceMode::poly); };
    trigger.addItem ("One shot", 1);
    trigger.addItem ("Gate", 2);
    trigger.onChange = [this] { proc.setPadTrigger (proc.selectedPad, trigger.getSelectedId() == 2 ? drums::TriggerMode::gate : drums::TriggerMode::oneShot); };
    for (auto* c : { &category, &choke, &voiceMode, &trigger })
        addAndMakeVisible (*c);

    const char* kitIds[] = { "kit.life", "kit.dynamics", "kit.character", "kit.drive", "kit.motion", "kit.echo", "kit.space", "kit.volume" };
    const char* kitNames[] = { "LIFE", "DYNAMICS", "CHARACTER", "DRIVE", "MOVEMENT", "ECHO", "SPACE", "VOLUME" };
    for (std::size_t i = 0; i < kitKnobs.size(); ++i)
    {
        auto& k = kitKnobs[i];
        k.slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        k.slider.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
        if (i < 6)
            k.slider.getProperties().set ("arc", static_cast<juce::int64> (plugin::design::colour::macro (static_cast<int> (i % 6)).getARGB()));
        k.slider.setPopupDisplayEnabled (true, true, this);
        addAndMakeVisible (k.slider);
        k.label.setText (kitNames[i], juce::dontSendNotification);
        k.label.setFont (type::controlLabel (0.9f));
        k.label.setJustificationType (juce::Justification::centred);
        k.label.setColour (juce::Label::textColourId, colour::textSecondary);
        addAndMakeVisible (k.label);
        k.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.parameters(), kitIds[i], k.slider);
    }

    proc.addChangeListener (this);
    setSize (editorWidth, editorHeight);
    bindSelectedPad();
    startTimerHz (30);
}

DrumsEditor::~DrumsEditor()
{
    proc.removeChangeListener (this);
    setLookAndFeel (nullptr);
}

juce::Colour DrumsEditor::categoryColour (drums::DrumCategory c)
{
    // Restrained accents from OSP's macro identities (no new palette).
    switch (c)
    {
        case drums::DrumCategory::kick: return colour::macro (0);
        case drums::DrumCategory::snare: return colour::macro (1);
        case drums::DrumCategory::closedHat:
        case drums::DrumCategory::openHat: return colour::macro (2);
        case drums::DrumCategory::clap:
        case drums::DrumCategory::rim: return colour::macro (3);
        case drums::DrumCategory::tom: return colour::macro (4);
        case drums::DrumCategory::cymbal:
        case drums::DrumCategory::percussion: return colour::macro (5);
        case drums::DrumCategory::unknown: break;
    }
    return colour::textMicro;
}

juce::String DrumsEditor::noteText (int pad) const
{
    const auto& map = proc.keyMap();
    const auto i = static_cast<std::size_t> (pad);
    if (proc.keyboardMode() == drums::KeyboardMode::pad)
        return map.padNote[i] >= 0 ? noteName (map.padNote[i]) : juce::String ("No note");
    const auto& z = map.zone[i];
    if (! z.enabled())
        return "No zone";
    return juce::String (midiNoteName (z.low)) + "-" + juce::String (midiNoteName (z.high)) + "  root " + juce::String (midiNoteName (z.rootNote()));
}

void DrumsEditor::selectPad (int pad)
{
    if (proc.selectedPad == pad)
        return;
    const int old = proc.selectedPad;
    proc.selectedPad = pad;
    bindSelectedPad();
    padViews[static_cast<std::size_t> (old)]->repaint();
    padViews[static_cast<std::size_t> (pad)]->repaint();
}

void DrumsEditor::bindSelectedPad()
{
    const int p = proc.selectedPad;
    auto& state = proc.parameters();
    gainAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (state, DrumsProcessor::padParamId (p, "gain"), gain);
    panAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (state, DrumsProcessor::padParamId (p, "pan"), pan);
    tuneAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (state, DrumsProcessor::padParamId (p, "tune"), tune);
    muteAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (state, DrumsProcessor::padParamId (p, "mute"), mute);
    soloAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (state, DrumsProcessor::padParamId (p, "solo"), solo);
    for (auto* s : { &gain, &pan, &tune })
        s->getProperties().set ("arc", static_cast<juce::int64> (categoryColour (proc.padInfo (p).category).getARGB()));
    refreshSelectedInfo();
    repaint();
}

void DrumsEditor::refreshSelectedInfo()
{
    const int p = proc.selectedPad;
    const auto& info = proc.padInfo (p);
    padName.setText (proc.padDisplayName (p), juce::dontSendNotification);
    juce::String status;
    if (info.loading)
        status = "Loading...";
    else if (! info.error.empty())
        status = "Could not load: " + juce::String::fromUTF8 (info.error.c_str());
    else if (info.sound != nullptr)
        status = juce::String::fromUTF8 (info.sound->filename.c_str()) + "   " + juce::String (info.sound->durationSeconds, 2) + " s   " + noteText (p);
    else if (! info.missingFilename.empty())
        status = "Missing sample: " + juce::String::fromUTF8 (info.missingFilename.c_str());
    else
        status = "Empty: drop a sample on the pad, or LOAD.   " + noteText (p);
    padStatus.setText (status, juce::dontSendNotification);
    category.setSelectedId (static_cast<int> (info.category) + 1, juce::dontSendNotification);
    choke.setSelectedId (info.chokeGroup + 1, juce::dontSendNotification);
    voiceMode.setSelectedId (info.voiceMode == drums::VoiceMode::monoRetrigger ? 2 : 1, juce::dontSendNotification);
    trigger.setSelectedId (info.trigger == drums::TriggerMode::gate ? 2 : 1, juce::dontSendNotification);
    const bool expanded = proc.keyboardMode() == drums::KeyboardMode::expanded;
    padMode.setToggleState (! expanded, juce::dontSendNotification);
    expandedMode.setToggleState (expanded, juce::dontSendNotification);
    layout8.setToggleState (! proc.nineLayout, juce::dontSendNotification);
    layout9.setToggleState (proc.nineLayout, juce::dontSendNotification);
    ninthPadChip.setVisible (! proc.nineLayout && proc.padInfo (8).sound != nullptr);
}

void DrumsEditor::changeListenerCallback (juce::ChangeBroadcaster*)
{
    refreshSelectedInfo();
    layoutPads();
    for (auto& v : padViews)
        v->repaint();
    repaint (waveformArea);
}

void DrumsEditor::timerCallback()
{
    for (auto& v : padViews)
        v->refresh();
    // The mode may move by automation or a host's undo.
    const bool expanded = proc.keyboardMode() == drums::KeyboardMode::expanded;
    if (expanded != expandedMode.getToggleState())
    {
        refreshSelectedInfo();
        for (auto& v : padViews)
            v->repaint();
        repaint();
    }
}

juce::Rectangle<int> DrumsEditor::gridArea() const
{
    return { 24, 380, editorWidth - 48, editorHeight - 380 - 44 };
}

void DrumsEditor::layoutPads()
{
    const auto area = gridArea();
    const int columns = proc.nineLayout ? 3 : 4;
    const int rows = proc.nineLayout ? 3 : 2;
    const int shown = columns * rows;
    // 3x3 stays square-ish and centred; 2x4 uses the width (the pads must not crowd the rest).
    auto grid = proc.nineLayout ? area.withSizeKeepingCentre (std::min (area.getWidth() - 80, area.getHeight() * 3), area.getHeight())
                                : area.withTrimmedRight (40);
    const int w = grid.getWidth() / columns, h = grid.getHeight() / rows;
    for (int i = 0; i < DrumsProcessor::numPads; ++i)
    {
        auto& v = *padViews[static_cast<std::size_t> (i)];
        v.setVisible (i < shown);
        if (i < shown)
            v.setBounds (grid.getX() + (i % columns) * w, grid.getY() + (i / columns) * h, w, h);
    }
    ninthPadChip.setBounds (area.getRight() - 34, area.getCentreY() - 17, 34, 34);
    ninthPadChip.setVisible (! proc.nineLayout && proc.padInfo (8).sound != nullptr);
}

void DrumsEditor::resized()
{
    auto r = getLocalBounds();
    auto header = r.removeFromTop (70).reduced (24, 16);
    auto right = header.removeFromRight (200);
    layout8.setBounds (right.removeFromLeft (100));
    layout9.setBounds (right);
    auto modes = header.withSizeKeepingCentre (260, header.getHeight());
    padMode.setBounds (modes.removeFromLeft (110));
    expandedMode.setBounds (modes);

    padPanel = { 24, 78, 700, 290 };
    kitPanel = { 740, 78, editorWidth - 764, 290 };
    auto pp = padPanel.reduced (18, 14);
    auto titleRow = pp.removeFromTop (34);
    clearButton.setBounds (titleRow.removeFromRight (72).reduced (0, 3));
    titleRow.removeFromRight (8);
    loadButton.setBounds (titleRow.removeFromRight (72).reduced (0, 3));
    padName.setBounds (titleRow);
    padStatus.setBounds (pp.removeFromTop (22));
    pp.removeFromTop (6);
    waveformArea = pp.removeFromTop (92);
    pp.removeFromTop (10);
    auto controls = pp;
    auto knobs = controls.removeFromLeft (300);
    const int kw = knobs.getWidth() / 3;
    for (auto [slider, label] : { std::pair { &gain, &gainLabel }, std::pair { &pan, &panLabel }, std::pair { &tune, &tuneLabel } })
    {
        auto cell = knobs.removeFromLeft (kw);
        label->setBounds (cell.removeFromTop (18));
        slider->setBounds (cell);
    }
    controls.removeFromLeft (12);
    auto column = controls.removeFromLeft (90);
    mute.setBounds (column.removeFromTop (30).reduced (0, 2));
    column.removeFromTop (8);
    solo.setBounds (column.removeFromTop (30).reduced (0, 2));
    controls.removeFromLeft (12);
    // CATEGORY / CHOKE over VOICE / TRIGGER.
    const int comboW = (controls.getWidth() - 8) / 2;
    auto row1 = controls.removeFromTop (28);
    controls.removeFromTop (10);
    auto row2 = controls.removeFromTop (28);
    category.setBounds (row1.removeFromLeft (comboW));
    choke.setBounds (row1.removeFromRight (comboW));
    voiceMode.setBounds (row2.removeFromLeft (comboW));
    trigger.setBounds (row2.removeFromRight (comboW));

    auto kp = kitPanel.reduced (14, 14);
    kp.removeFromTop (26);
    const int cw = kp.getWidth() / 4, ch = kp.getHeight() / 2;
    for (std::size_t i = 0; i < kitKnobs.size(); ++i)
    {
        juce::Rectangle<int> cell (kp.getX() + static_cast<int> (i % 4) * cw, kp.getY() + static_cast<int> (i / 4) * ch, cw, ch);
        kitKnobs[i].label.setBounds (cell.removeFromBottom (20));
        kitKnobs[i].slider.setBounds (cell.reduced (6));
    }
    layoutPads();
}

void DrumsEditor::paint (juce::Graphics& g)
{
    g.setGradientFill (juce::ColourGradient (colour::backdropTop, 0.0f, 0.0f, colour::backdropBottom, 0.0f, static_cast<float> (getHeight()), false));
    g.fillAll();

    g.setColour (colour::text);
    g.setFont (plugin::fonts::make (30.0f, Weight::displayBold, 0.04f));
    g.drawText ("ANDOR", 24, 16, 120, 38, juce::Justification::centredLeft);
    g.setFont (plugin::fonts::make (30.0f, Weight::displayLight, 0.04f));
    g.drawText ("DRUMS", 138, 16, 140, 38, juce::Justification::centredLeft);

    raisedPanel (g, padPanel.toFloat());
    raisedPanel (g, kitPanel.toFloat());
    g.setColour (colour::textSecondary);
    g.setFont (type::panelHeader());
    g.drawText ("KIT  (SHARED BY EVERY PAD)", kitPanel.reduced (16, 12).removeFromTop (20), juce::Justification::centredLeft);

    // The selected pad's waveform, full width (OSP's dark well).
    const auto& info = proc.padInfo (proc.selectedPad);
    auto well = waveformArea.toFloat();
    g.setColour (colour::wellA);
    g.fillRoundedRectangle (well, 7.0f);
    if (info.sound != nullptr && ! info.sound->peakMax.empty())
    {
        const auto& lo = info.sound->peakMin;
        const auto& hi = info.sound->peakMax;
        const int n = static_cast<int> (hi.size());
        const float mid = well.getCentreY(), half = well.getHeight() * 0.44f;
        const int columns = static_cast<int> (well.getWidth() - 12.0f);
        juce::Path wave;
        for (int x = 0; x < columns; ++x)
        {
            const auto b = static_cast<std::size_t> (std::min (n - 1, x * n / std::max (1, columns)));
            const float top = mid - half * std::min (1.0f, hi[b]);
            wave.addRectangle (well.getX() + 6.0f + static_cast<float> (x), top, 1.0f, std::max (0.6f, half * (std::min (1.0f, hi[b]) - std::max (-1.0f, lo[b]))));
        }
        g.setColour (categoryColour (info.category).interpolatedWith (juce::Colours::white, 0.3f));
        g.fillPath (wave);
    }

    // Which keyboard mode is live: MIDI clips play differently in each.
    g.setColour (colour::textMicro);
    g.setFont (type::micro (13.5f));
    const bool expanded = proc.keyboardMode() == drums::KeyboardMode::expanded;
    g.drawText (expanded ? "EXPANDED: every key in a pad's zone plays that pad, transposed from its root. Drum clips made for PAD mode will sound different."
                         : "PAD: each pad answers its own MIDI note (GM-style). EXPANDED keeps its own zones; switching never changes either map.",
                24, editorHeight - 36, editorWidth - 48, 20, juce::Justification::centredLeft);
}

} // namespace osp::drumsplugin
