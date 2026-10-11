#include "DrumsEditor.h"

#include "DrumsLook.h"

namespace osp::drumsplugin
{

namespace
{
    namespace colour = look::colour;
    namespace type = look::type;

    // Base layout (the content is scaled as a whole).
    const juce::Rectangle<int> gridBounds { 24, 80, 1232, 268 };
    constexpr int modeBarY = 358;
    const juce::Rectangle<int> sampleBounds { 24, 400, 1232, 242 };
    const juce::Rectangle<int> envelopeBounds { 24, 652, 470, 232 };
    const juce::Rectangle<int> effectsBounds { 506, 652, 430, 232 };
    const juce::Rectangle<int> kitBounds { 948, 652, 308, 232 };
    const juce::Rectangle<int> keyMapBounds { 24, 400, 1232, 484 };

    void comboWithLabel (juce::Component& parent, juce::ComboBox& box, juce::Label& label, const juce::String& text)
    {
        label.setText (text, juce::dontSendNotification);
        label.setFont (type::controlLabel (0.8f));
        label.setColour (juce::Label::textColourId, colour::textSecondary);
        parent.addAndMakeVisible (label);
        parent.addAndMakeVisible (box);
    }

    juce::String seconds (double s)
    {
        return s < 1.0 ? juce::String (juce::roundToInt (s * 1000.0)) + " ms" : juce::String (s, 2) + " s";
    }
} // namespace

// ---------------------------------------------------------------------------------------

void LabelledKnob::setup (juce::Component& parent, const juce::String& text, juce::Colour arc, bool bipolar)
{
    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 74, 18);
    slider.getProperties().set ("arc", static_cast<juce::int64> (arc.getARGB()));
    if (bipolar)
        slider.getProperties().set ("bipolar", true);
    parent.addAndMakeVisible (slider);
    label.setText (text, juce::dontSendNotification);
    label.setFont (type::controlLabel (0.85f));
    label.setJustificationType (juce::Justification::centred);
    label.setColour (juce::Label::textColourId, colour::textSecondary);
    parent.addAndMakeVisible (label);
}

void LabelledKnob::attach (juce::AudioProcessorValueTreeState& state, const juce::String& id)
{
    attachment.reset();
    attachment = std::make_unique<SliderAttachment> (state, id, slider);
}

void LabelledKnob::setBounds (juce::Rectangle<int> r)
{
    label.setBounds (r.removeFromTop (18));
    slider.setBounds (r);
}

// ---------------------------------------------------------------------------------------
// Selected pad: the sound
// ---------------------------------------------------------------------------------------

SamplePanel::SamplePanel (DrumsProcessor& p) : proc (p)
{
    name.setEditable (false, true, false);
    name.setFont (plugin::fonts::make (24.0f, look::Weight::semibold, 0.04f));
    name.setColour (juce::Label::textColourId, colour::text);
    name.setTooltip ("Double-click to rename");
    name.onTextChange = [this] { proc.setPadName (proc.selectedPad, name.getText()); };
    addAndMakeVisible (name);
    previous.onClick = [this] {
        const int slot = (proc.slotOfPad (proc.selectedPad) + (proc.nineLayout ? 8 : 7)) % (proc.nineLayout ? 9 : 8);
        if (onSelect)
            onSelect (proc.padAtSlot (slot));
    };
    next.onClick = [this] {
        const int slot = (proc.slotOfPad (proc.selectedPad) + 1) % (proc.nineLayout ? 9 : 8);
        if (onSelect)
            onSelect (proc.padAtSlot (slot));
    };
    load.onClick = [this] {
        if (onLoad)
            onLoad();
    };
    clear.onClick = [this] { proc.clearPad (proc.selectedPad); };
    for (auto* b : { &previous, &next, &load, &clear })
        addAndMakeVisible (b);

    start.setup (*this, "START", colour::accent);
    tune.setup (*this, "TUNE", colour::accent, true);
    gain.setup (*this, "GAIN", colour::accent);
    pan.setup (*this, "PAN", colour::accent, true);
    addAndMakeVisible (reverse);

    trigger.addItem ("One shot", 1);
    trigger.addItem ("Gate", 2);
    trigger.onChange = [this] { proc.setPadTrigger (proc.selectedPad, trigger.getSelectedId() == 2 ? drums::TriggerMode::gate : drums::TriggerMode::oneShot); };
    voice.addItem ("Polyphonic", 1);
    voice.addItem ("Mono retrigger", 2);
    voice.onChange = [this] { proc.setPadVoiceMode (proc.selectedPad, voice.getSelectedId() == 2 ? drums::VoiceMode::monoRetrigger : drums::VoiceMode::poly); };
    choke.addItem ("None", 1);
    for (int i = 1; i <= 8; ++i)
        choke.addItem ("Group " + juce::String (i), i + 1);
    choke.onChange = [this] { proc.setPadChoke (proc.selectedPad, choke.getSelectedId() - 1); };
    for (int c = 0; c < 10; ++c)
        category.addItem (look::categoryName (static_cast<drums::DrumCategory> (c)), c + 1);
    category.onChange = [this] {
        const auto c = static_cast<drums::DrumCategory> (category.getSelectedId() - 1);
        if (c != proc.padInfo (proc.selectedPad).category)
            proc.setPadCategory (proc.selectedPad, c, true);
    };
    comboWithLabel (*this, trigger, triggerLabel, "PLAY MODE");
    comboWithLabel (*this, voice, voiceLabel, "VOICES");
    comboWithLabel (*this, choke, chokeLabel, "CHOKE GROUP");
    comboWithLabel (*this, category, categoryLabel, "CATEGORY");
}

void SamplePanel::bind()
{
    const int p = proc.selectedPad;
    auto& state = proc.parameters();
    start.attach (state, DrumsProcessor::padParamId (p, "start"));
    tune.attach (state, DrumsProcessor::padParamId (p, "tune"));
    gain.attach (state, DrumsProcessor::padParamId (p, "gain"));
    pan.attach (state, DrumsProcessor::padParamId (p, "pan"));
    reverseAttachment.reset();
    reverseAttachment = std::make_unique<ButtonAttachment> (state, DrumsProcessor::padParamId (p, "reverse"), reverse);
    const auto& info = proc.padInfo (p);
    const auto accent = look::category (info.category);
    for (auto* k : { &start, &tune, &gain, &pan })
    {
        k->slider.getProperties().set ("arc", static_cast<juce::int64> (accent.getARGB()));
        k->slider.repaint();
    }
    name.setText (proc.padDisplayName (p), juce::dontSendNotification);
    trigger.setSelectedId (info.trigger == drums::TriggerMode::gate ? 2 : 1, juce::dontSendNotification);
    voice.setSelectedId (info.voiceMode == drums::VoiceMode::monoRetrigger ? 2 : 1, juce::dontSendNotification);
    choke.setSelectedId (info.chokeGroup + 1, juce::dontSendNotification);
    category.setSelectedId (static_cast<int> (info.category) + 1, juce::dontSendNotification);
    clear.setEnabled (info.sound != nullptr || ! info.missingFilename.empty());
    repaint();
}

juce::Rectangle<int> SamplePanel::waveArea() const { return { 18, 54, 700, 104 }; }

void SamplePanel::resized()
{
    previous.setBounds (64, 12, 28, 28);
    next.setBounds (94, 12, 28, 28);
    name.setBounds (132, 10, 380, 32);
    clear.setBounds (waveArea().getRight() - 72, 12, 72, 28);
    load.setBounds (waveArea().getRight() - 152, 12, 72, 28);

    const auto w = waveArea();
    auto knobs = juce::Rectangle<int> (w.getX(), w.getBottom() + 8, w.getWidth(), getHeight() - w.getBottom() - 14);
    const int kw = 96;
    for (auto* k : { &start, &tune, &gain, &pan })
        k->setBounds (knobs.removeFromLeft (kw));
    knobs.removeFromLeft (16);
    reverse.setBounds (knobs.removeFromLeft (110).withSizeKeepingCentre (110, 28));

    // The right column: the sound's layers, then the play settings.
    auto right = getLocalBounds().withTrimmedLeft (w.getRight() + 24).reduced (0, 12).withTrimmedRight (18);
    right.removeFromTop (120);   // the layer list (painted)
    const int half = (right.getWidth() - 12) / 2;
    auto row1 = right.removeFromTop (48);
    auto c1 = row1.removeFromLeft (half);
    triggerLabel.setBounds (c1.removeFromTop (18));
    trigger.setBounds (c1.withHeight (26));
    row1.removeFromLeft (12);
    voiceLabel.setBounds (row1.removeFromTop (18));
    voice.setBounds (row1.withHeight (26));
    right.removeFromTop (6);
    auto row2 = right.removeFromTop (48);
    auto c2 = row2.removeFromLeft (half);
    chokeLabel.setBounds (c2.removeFromTop (18));
    choke.setBounds (c2.withHeight (26));
    row2.removeFromLeft (12);
    categoryLabel.setBounds (row2.removeFromTop (18));
    category.setBounds (row2.withHeight (26));
}

void SamplePanel::paint (juce::Graphics& g)
{
    look::panel (g, getLocalBounds().toFloat());
    const int p = proc.selectedPad;
    const auto& info = proc.padInfo (p);
    const auto accent = look::category (info.category);
    look::badge (g, juce::Rectangle<float> (18.0f, 12.0f, 32.0f, 28.0f), p + 1, colour::accent, juce::Colours::white);

    // Waveform well with START shown (the part before it dimmed).
    const auto w = waveArea().toFloat();
    g.setColour (colour::wellA);
    g.fillRoundedRectangle (w, 7.0f);
    if (info.sound != nullptr && ! info.sound->peakMax.empty())
    {
        const float startFraction = proc.parameters().getRawParameterValue (DrumsProcessor::padParamId (p, "start"))->load();
        look::waveform (g, w.reduced (10.0f, 10.0f), info.sound->peakMin, info.sound->peakMax, accent.interpolatedWith (juce::Colours::white, 0.15f),
                        startFraction);
        const float sx = w.getX() + 10.0f + (w.getWidth() - 20.0f) * startFraction;
        g.setColour (colour::accent);
        g.fillRect (juce::Rectangle<float> (sx - 0.75f, w.getY() + 6.0f, 1.5f, w.getHeight() - 12.0f));
        // Time ruler
        g.setColour (colour::wellText);
        g.setFont (type::micro (11.0f));
        const double dur = info.sound->durationSeconds;
        for (int i = 0; i <= 4; ++i)
        {
            const float x = w.getX() + 10.0f + (w.getWidth() - 20.0f) * static_cast<float> (i) / 4.0f;
            g.drawText (juce::String (dur * i / 4.0, 2) + "s", juce::Rectangle<float> (x - 20.0f, w.getBottom() - 15.0f, 40.0f, 13.0f), juce::Justification::centred);
        }
    }
    else
    {
        g.setColour (colour::wellText);
        g.setFont (type::micro (14.0f));
        juce::String text = "Drop a sample on the pad, or LOAD.";
        if (info.loading)
            text = "Loading...";
        else if (! info.missingFilename.empty())
            text = "Missing sample: " + juce::String::fromUTF8 (info.missingFilename.c_str()) + ". LOAD a file to relink it.";
        else if (! info.error.empty())
            text = "Could not load: " + juce::String::fromUTF8 (info.error.c_str());
        g.drawFittedText (text, w.toNearestInt().reduced (16, 0), juce::Justification::centred, 2, 1.0f);
    }

    // Layers
    auto right = getLocalBounds().withTrimmedLeft (waveArea().getRight() + 24).reduced (0, 12).withTrimmedRight (18).toFloat();
    look::panelTitle (g, right.removeFromTop (22.0f).toNearestInt(), "SAMPLE LAYERS", "this pad");
    auto list = right.removeFromTop (92.0f).reduced (0.0f, 4.0f);
    g.setColour (juce::Colour (0xfff7f2ea));
    g.fillRoundedRectangle (list, 6.0f);
    g.setColour (colour::hairline);
    g.drawRoundedRectangle (list, 6.0f, 1.0f);
    auto rowArea = list.reduced (10.0f, 6.0f);
    g.setFont (type::micro (13.0f));
    if (info.sound != nullptr)
    {
        const auto& s = *info.sound;
        auto line = rowArea.removeFromTop (22.0f);
        g.setColour (accent);
        g.fillEllipse (line.removeFromLeft (8.0f).withSizeKeepingCentre (7.0f, 7.0f));
        line.removeFromLeft (8.0f);
        g.setColour (colour::text);
        g.drawFittedText (juce::String::fromUTF8 (s.filename.c_str()), line.withTrimmedRight (70.0f).toNearestInt(), juce::Justification::centredLeft, 1, 1.0f);
        g.setColour (colour::textSecondary);
        g.drawText (juce::String (s.durationSeconds, 2) + " s", line.removeFromRight (64.0f), juce::Justification::centredRight);
        g.setColour (colour::textMicro);
        g.setFont (type::micro (12.0f));
        g.drawFittedText ("1 layer. Round robin, velocity layers and stacks arrive with the next round (multi-file drop).",
                          rowArea.toNearestInt(), juce::Justification::topLeft, 2, 1.0f);
    }
    else
    {
        g.setColour (colour::textMicro);
        g.drawText ("No sample", rowArea, juce::Justification::centredLeft);
    }
}

// ---------------------------------------------------------------------------------------
// Selected pad: the AMP envelope
// ---------------------------------------------------------------------------------------

EnvelopePanel::EnvelopePanel (DrumsProcessor& p) : proc (p)
{
    attack.setup (*this, "ATTACK", colour::accent);
    decay.setup (*this, "DECAY", colour::accent);
    sustain.setup (*this, "SUSTAIN", colour::accent);
    release.setup (*this, "RELEASE", colour::accent);
    for (auto* k : { &attack, &decay, &sustain, &release })
        k->slider.onValueChange = [this] { repaint(); };
}

void EnvelopePanel::bind()
{
    const int p = proc.selectedPad;
    auto& state = proc.parameters();
    attack.attach (state, DrumsProcessor::padParamId (p, "attack"));
    decay.attach (state, DrumsProcessor::padParamId (p, "decay"));
    sustain.attach (state, DrumsProcessor::padParamId (p, "sustain"));
    release.attach (state, DrumsProcessor::padParamId (p, "release"));
    attack.slider.textFromValueFunction = decay.slider.textFromValueFunction = release.slider.textFromValueFunction = [] (double v) { return seconds (v); };
    sustain.slider.textFromValueFunction = [] (double v) { return juce::String (juce::roundToInt (v * 100.0)) + " %"; };
    for (auto* k : { &attack, &decay, &sustain, &release })
        k->slider.updateText();
    repaint();
}

void EnvelopePanel::resized()
{
    auto knobs = getLocalBounds().reduced (16, 12).removeFromBottom (96);
    const int kw = knobs.getWidth() / 4;
    for (auto* k : { &attack, &decay, &sustain, &release })
        k->setBounds (knobs.removeFromLeft (kw));
}

void EnvelopePanel::paint (juce::Graphics& g)
{
    look::panel (g, getLocalBounds().toFloat());
    auto r = getLocalBounds().reduced (16, 10);
    look::panelTitle (g, r.removeFromTop (22), "AMP ENVELOPE", "selected pad: " + proc.padDisplayName (proc.selectedPad));
    g.setColour (colour::textMicro);
    g.setFont (type::micro (11.5f));
    g.drawText ("LFO / ENV / VEL / NOTE routes for pads: next round", r.removeFromTop (16), juce::Justification::centredLeft);

    // The envelope's shape from its own values (log time, so short drum attacks are visible).
    auto well = r.removeFromTop (r.getHeight() - 104).toFloat().reduced (0.0f, 4.0f);
    g.setColour (colour::wellA);
    g.fillRoundedRectangle (well, 6.0f);
    const double a = attack.slider.getValue(), d = decay.slider.getValue(), s = sustain.slider.getValue(), rel = release.slider.getValue();
    auto tx = [] (double secs) { return std::log10 (1.0 + secs * 100.0); };
    const double hold = 0.25;   // a drawn hold so sustain is visible
    const double total = tx (a) + tx (d) + tx (hold) + tx (rel) + 1.0e-9;
    const auto inner = well.reduced (10.0f, 10.0f);
    auto px = [&] (double t) { return inner.getX() + static_cast<float> (t / total) * inner.getWidth(); };
    auto py = [&] (double level) { return inner.getBottom() - static_cast<float> (level) * inner.getHeight(); };
    juce::Path env;
    double t = 0.0;
    env.startNewSubPath (px (0.0), py (0.0));
    t += tx (a);
    env.lineTo (px (t), py (1.0));
    t += tx (d);
    env.lineTo (px (t), py (d > 0.0 ? s : 1.0));
    t += tx (hold);
    env.lineTo (px (t), py (d > 0.0 ? s : 1.0));
    t += tx (rel);
    env.lineTo (px (t), py (0.0));
    juce::Path fill (env);
    fill.lineTo (px (0.0), py (0.0));
    fill.closeSubPath();
    g.setColour (colour::accent.withAlpha (0.18f));
    g.fillPath (fill);
    g.setColour (colour::accent);
    g.strokePath (env, juce::PathStrokeType (1.8f));
}

// ---------------------------------------------------------------------------------------
// Content
// ---------------------------------------------------------------------------------------

DrumsContent::DrumsContent (DrumsProcessor& p) : proc (p), grid (p), sample (p), envelope (p), keyMap (p)
{
    setWantsKeyboardFocus (true);
    addAndMakeVisible (grid);
    grid.onSelect = [this] (int pad) { selectPad (pad); };
    grid.onLoadRequest = [this] (int pad) { openLoadDialog (pad); };

    look::makeSegment (padMode, 11);
    look::makeSegment (expandedMode, 11);
    padMode.onClick = [this] { proc.setKeyboardMode (drums::KeyboardMode::pad); refreshModeBar(); };
    expandedMode.onClick = [this] { proc.setKeyboardMode (drums::KeyboardMode::expanded); refreshModeBar(); };
    padMode.setTooltip ("PAD: each pad answers its own MIDI note (drum clips play as usual)");
    expandedMode.setTooltip ("EXPANDED: each pad owns a key range and plays transposed across it (a separate map)");
    keyMapButton.setClickingTogglesState (true);
    keyMapButton.onClick = [this] { setKeyMapOpen (keyMapButton.getToggleState()); };
    keyMapButton.setTooltip ("Edit every pad's MIDI note or key range");
    look::makeSegment (layout8, 12);
    look::makeSegment (layout9, 12);
    layout8.onClick = [this] { proc.nineLayout = false; grid.layoutChanged(); keyMap.refreshFromProcessor(); refreshModeBar(); };
    layout9.onClick = [this] { proc.nineLayout = true; grid.layoutChanged(); keyMap.refreshFromProcessor(); refreshModeBar(); };
    ninthPad.setTooltip ("Pad 9 holds a sound and keeps playing in the 2 x 4 view. Click to edit it.");
    ninthPad.onClick = [this] { selectPad (proc.padAtSlot (8)); };
    undoButton.onClick = [this] { proc.undoManager().undo(); };
    redoButton.onClick = [this] { proc.undoManager().redo(); };
    for (auto* b : { &padMode, &expandedMode, &keyMapButton, &layout8, &layout9, &undoButton, &redoButton })
        addAndMakeVisible (b);
    addChildComponent (ninthPad);

    volume.setSliderStyle (juce::Slider::LinearHorizontal);
    volume.setTextBoxStyle (juce::Slider::TextBoxRight, false, 64, 20);
    volume.getProperties().set ("arc", static_cast<juce::int64> (colour::accent.getARGB()));
    volumeAttachment = std::make_unique<SliderAttachment> (proc.parameters(), "kit.volume", volume);
    addAndMakeVisible (volume);

    addAndMakeVisible (sample);
    sample.onSelect = [this] (int pad) { selectPad (pad); };
    sample.onLoad = [this] { openLoadDialog (proc.selectedPad); };
    addAndMakeVisible (envelope);

    const char* fxIds[] = { "kit.drive", "kit.motion", "kit.echo", "kit.space" };
    const char* fxNames[] = { "DRIVE", "MOVEMENT", "ECHO", "SPACE" };
    const int fxColours[] = { 0, 3, 4, 5 };
    for (std::size_t i = 0; i < effects.size(); ++i)
    {
        effects[i].setup (*this, fxNames[i], colour::macro (fxColours[i]));
        effects[i].attach (proc.parameters(), fxIds[i]);
        effects[i].slider.textFromValueFunction = [] (double v) { return juce::String (juce::roundToInt (v * 100.0)) + " %"; };
        effects[i].slider.updateText();
    }
    const char* macroIds[] = { "kit.life", "kit.dynamics", "kit.character" };
    const char* macroNames[] = { "LIFE", "DYNAMICS", "CHARACTER" };
    for (std::size_t i = 0; i < macros.size(); ++i)
    {
        macros[i].setup (*this, macroNames[i], colour::macro (static_cast<int> (i)));
        macros[i].attach (proc.parameters(), macroIds[i]);
        macros[i].slider.textFromValueFunction = [] (double v) { return juce::String (juce::roundToInt (v * 100.0)) + " %"; };
        macros[i].slider.updateText();
    }

    addChildComponent (keyMap);
    keyMap.onClose = [this] { setKeyMapOpen (false); };
    keyMap.onSelect = [this] (int pad) { selectPad (pad); };
    keyMap.onModeChanged = [this] { refreshModeBar(); };

    proc.addChangeListener (this);
    sample.bind();
    envelope.bind();
    refreshModeBar();
    startTimerHz (30);
}

DrumsContent::~DrumsContent()
{
    proc.removeChangeListener (this);
}

void DrumsContent::selectPad (int pad)
{
    if (proc.selectedPad != pad)
    {
        proc.selectedPad = pad;
        sample.bind();
        envelope.bind();
        grid.repaintAll();
    }
    keyMap.refreshFromProcessor();
}

void DrumsContent::setKeyMapOpen (bool open)
{
    keyMap.setVisible (open);
    sample.setVisible (! open);
    envelope.setVisible (! open);
    for (auto* k : { &effects[0], &effects[1], &effects[2], &effects[3], &macros[0], &macros[1], &macros[2] })
    {
        k->slider.setVisible (! open);
        k->label.setVisible (! open);
    }
    keyMapButton.setToggleState (open, juce::dontSendNotification);
    if (open)
    {
        keyMap.refreshFromProcessor();
        keyMap.grabKeyboardFocus();
    }
    repaint();
}

void DrumsContent::openLoadDialog (int pad)
{
    chooser = std::make_unique<juce::FileChooser> ("Load a sample onto " + proc.padDisplayName (pad), juce::File(), "*.wav;*.aif;*.aiff;*.flac;*.caf");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this, pad] (const juce::FileChooser& fc) {
        if (fc.getResult().existsAsFile())
            proc.loadPad (pad, fc.getResult());
    });
}

void DrumsContent::refreshModeBar()
{
    const bool exp = proc.keyboardMode() == drums::KeyboardMode::expanded;
    lastExpanded = exp;
    padMode.setToggleState (! exp, juce::dontSendNotification);
    expandedMode.setToggleState (exp, juce::dontSendNotification);
    layout8.setToggleState (! proc.nineLayout, juce::dontSendNotification);
    layout9.setToggleState (proc.nineLayout, juce::dontSendNotification);
    ninthPad.setVisible (! proc.nineLayout && proc.padInfo (proc.padAtSlot (8)).sound != nullptr);
    undoButton.setEnabled (proc.undoManager().canUndo());
    redoButton.setEnabled (proc.undoManager().canRedo());
    grid.repaintAll();
    repaint();
}

void DrumsContent::changeListenerCallback (juce::ChangeBroadcaster*)
{
    sample.bind();
    envelope.repaint();
    refreshModeBar();
    if (keyMap.isVisible())
        keyMap.refreshFromProcessor();
}

void DrumsContent::timerCallback()
{
    grid.refresh();
    const bool exp = proc.keyboardMode() == drums::KeyboardMode::expanded;
    if (exp != lastExpanded || undoButton.isEnabled() != proc.undoManager().canUndo() || redoButton.isEnabled() != proc.undoManager().canRedo())
        refreshModeBar();   // automation, a host undo, or the KEY MAP's own buttons
}

bool DrumsContent::keyPressed (const juce::KeyPress& key)
{
    const auto mods = key.getModifiers();
    if (key.getKeyCode() == 'Z' || key.getKeyCode() == 'z')
        if (mods.isCommandDown())
        {
            mods.isShiftDown() ? proc.undoManager().redo() : proc.undoManager().undo();
            return true;
        }
    if (key == juce::KeyPress::escapeKey && keyMap.isVisible())
        return keyMap.keyPressed (key) || (setKeyMapOpen (false), true);
    return false;
}

void DrumsContent::resized()
{
    undoButton.setBounds (520, 22, 66, 28);
    redoButton.setBounds (592, 22, 66, 28);
    volume.setBounds (1040, 24, 216, 24);
    grid.setBounds (gridBounds);

    auto bar = juce::Rectangle<int> (24, modeBarY, 1232, 32);
    padMode.setBounds (bar.removeFromLeft (70));
    expandedMode.setBounds (bar.removeFromLeft (104));
    bar.removeFromLeft (12);
    keyMapButton.setBounds (bar.removeFromLeft (100));
    layout9.setBounds (bar.removeFromRight (70));
    layout8.setBounds (bar.removeFromRight (70));
    bar.removeFromRight (12);
    ninthPad.setBounds (bar.removeFromRight (74));

    sample.setBounds (sampleBounds);
    envelope.setBounds (envelopeBounds);
    auto fx = effectsBounds.reduced (16, 12).withTrimmedTop (34);
    const int fw = fx.getWidth() / 4;
    for (auto& k : effects)
        k.setBounds (fx.removeFromLeft (fw).withTrimmedBottom (30));
    auto mk = kitBounds.reduced (16, 12).withTrimmedTop (34);
    const int mw = mk.getWidth() / 3;
    for (auto& k : macros)
        k.setBounds (mk.removeFromLeft (mw).withTrimmedBottom (30));
    keyMap.setBounds (keyMapBounds);
}

void DrumsContent::paint (juce::Graphics& g)
{
    g.setGradientFill (juce::ColourGradient (colour::backdropTop, 0.0f, 0.0f, colour::backdropBottom, 0.0f, static_cast<float> (getHeight()), false));
    g.fillAll();

    // Wordmark: ANDOR over spaced DRUMS (Outfit, the ANDOR identity).
    g.setColour (colour::text);
    g.setFont (plugin::fonts::make (30.0f, look::Weight::displayLight, 0.32f));
    g.drawText ("ANDOR", 24, 10, 220, 34, juce::Justification::centredLeft);
    g.setFont (plugin::fonts::make (13.0f, look::Weight::displayBold, 0.9f));
    g.drawText ("DRUMS", 26, 44, 220, 18, juce::Justification::centredLeft);

    // Kit summary between undo/redo and the volume.
    int loaded = 0;
    for (int p = 0; p < DrumsProcessor::numPads; ++p)
        loaded += proc.padInfo (p).sound != nullptr ? 1 : 0;
    g.setColour (colour::textSecondary);
    g.setFont (type::micro (13.5f));
    g.drawText (juce::String (loaded) + " of 9 pads loaded   \xc2\xb7   " + (lastExpanded ? "EXPANDED keyboard" : "PAD keyboard"),
                juce::Rectangle<int> (676, 22, 330, 28), juce::Justification::centredLeft);
    g.setColour (colour::textSecondary);
    g.setFont (type::controlLabel (0.8f));
    g.drawText ("VOLUME", juce::Rectangle<int> (970, 24, 66, 24), juce::Justification::centredRight);

    // Mode bar hint: what the active mode means for MIDI clips.
    g.setColour (colour::textMicro);
    g.setFont (type::micro (12.5f));
    g.drawFittedText (lastExpanded ? "EXPANDED: keys transpose pads across their zones. PAD drum clips will sound different; the PAD map is kept."
                                   : "PAD: one note per pad. EXPANDED keeps its own zones; switching never edits either map.",
                      juce::Rectangle<int> (330, modeBarY, 640, 32), juce::Justification::centredLeft, 1, 1.0f);

    if (! keyMap.isVisible())
    {
        look::panel (g, effectsBounds.toFloat());
        look::panelTitle (g, effectsBounds.reduced (16, 10).removeFromTop (22), "EFFECTS", "whole kit, shared");
        look::panel (g, kitBounds.toFloat());
        look::panelTitle (g, kitBounds.reduced (16, 10).removeFromTop (22), "KIT MACROS", "whole kit");
        g.setColour (colour::textMicro);
        g.setFont (type::micro (11.5f));
        g.drawFittedText ("Every pad hears one DRIVE, MOVEMENT, ECHO and SPACE.", effectsBounds.reduced (16, 12).removeFromBottom (22), juce::Justification::centredLeft, 1, 1.0f);
        g.drawFittedText ("Per-voice character on every pad.", kitBounds.reduced (16, 12).removeFromBottom (22), juce::Justification::centredLeft, 1, 1.0f);
    }
}

// ---------------------------------------------------------------------------------------
// Editor
// ---------------------------------------------------------------------------------------

DrumsEditor::DrumsEditor (DrumsProcessor& p) : AudioProcessorEditor (p), body (p)
{
    setLookAndFeel (&lookAndFeel);
    addAndMakeVisible (body);
    body.setBounds (0, 0, DrumsContent::baseWidth, DrumsContent::baseHeight);
    setResizable (true, true);
    setResizeLimits (static_cast<int> (DrumsContent::baseWidth * minScale), static_cast<int> (DrumsContent::baseHeight * minScale),
                     static_cast<int> (DrumsContent::baseWidth * maxScale), static_cast<int> (DrumsContent::baseHeight * maxScale));
    if (auto* c = getConstrainer())
        c->setFixedAspectRatio (static_cast<double> (DrumsContent::baseWidth) / DrumsContent::baseHeight);
    setSize (static_cast<int> (DrumsContent::baseWidth * defaultScale), static_cast<int> (DrumsContent::baseHeight * defaultScale));
}

DrumsEditor::~DrumsEditor()
{
    setLookAndFeel (nullptr);
}

void DrumsEditor::resized()
{
    const float scale = static_cast<float> (getWidth()) / static_cast<float> (DrumsContent::baseWidth);
    body.setTransform (juce::AffineTransform::scale (scale));
}

void DrumsEditor::paint (juce::Graphics& g)
{
    g.fillAll (look::colour::backdropBottom);
}

} // namespace osp::drumsplugin
