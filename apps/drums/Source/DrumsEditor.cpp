#include "DrumsEditor.h"

#include "DrumsLook.h"

namespace osp::drumsplugin
{

namespace
{
    namespace cc = ceramic::colour;
    namespace type = look::type;

    juce::String seconds (double s)
    {
        return s < 1.0 ? juce::String (s * 1000.0, s < 0.01 ? 1 : 0) + " ms" : juce::String (s, 2) + " s";
    }

    juce::String percent (double v) { return juce::String (juce::roundToInt (v * 100.0)) + " %"; }

    void sectionTitle (juce::Graphics& g, juce::Rectangle<int> r, const juce::String& title, const juce::String& scope = {})
    {
        g.setColour (cc::text);
        g.setFont (plugin::fonts::make (14.5f, look::Weight::semibold, 0.08f));
        g.drawText (title, r, juce::Justification::centredLeft);
        if (scope.isNotEmpty())
        {
            const int w = juce::GlyphArrangement::getStringWidthInt (plugin::fonts::make (14.5f, look::Weight::semibold, 0.08f), title);
            g.setColour (cc::textMicro);
            g.setFont (plugin::fonts::make (12.5f, look::Weight::medium, 0.04f));
            g.drawText (scope, r.withTrimmedLeft (w + 8), juce::Justification::centredLeft);
        }
    }

    void smallLabel (juce::Graphics& g, juce::Rectangle<int> r, const juce::String& text, juce::Justification j = juce::Justification::centredLeft)
    {
        g.setColour (cc::textSecondary);
        g.setFont (plugin::fonts::make (12.0f, look::Weight::medium, 0.08f));
        g.drawText (text, r, j);
    }

    void styleCombo (juce::ComboBox& c)
    {
        c.setColour (juce::ComboBox::backgroundColourId, cc::cardTop);
        c.setColour (juce::ComboBox::textColourId, cc::text);
        c.setColour (juce::ComboBox::outlineColourId, juce::Colour (0xffcfc4b4));
    }
} // namespace

// ---------------------------------------------------------------------------------------

void LabelledKnob::setup (juce::Component& parent, const juce::String& text, juce::Colour arc, bool bipolar, bool rendered)
{
    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 80, 20);
    slider.getProperties().set ("arc", static_cast<juce::int64> (arc.getARGB()));
    if (bipolar)
        slider.getProperties().set ("bipolar", true);
    if (rendered)
    {
        // The approved Blender macro knob (the same sprite as ANDOR/OSP's macros).
        slider.getProperties().set ("sprite", "macro-ivory");
        slider.getProperties().set ("spritePilot", true);
    }
    parent.addAndMakeVisible (slider);
    label.setText (text, juce::dontSendNotification);
    label.setFont (plugin::fonts::make (rendered ? 15.0f : 12.5f, look::Weight::medium, 0.08f));
    label.setJustificationType (juce::Justification::centred);
    label.setColour (juce::Label::textColourId, rendered ? cc::text : cc::textSecondary);
    parent.addAndMakeVisible (label);
}

void LabelledKnob::attach (juce::AudioProcessorValueTreeState& state, const juce::String& id)
{
    attachment.reset();
    attachment = std::make_unique<SliderAttachment> (state, id, slider);
}

void LabelledKnob::setBounds (juce::Rectangle<int> r, int labelHeight)
{
    label.setBounds (r.removeFromTop (labelHeight));
    slider.setBounds (r);
}

// ---------------------------------------------------------------------------------------
// Waveform with a draggable START marker
// ---------------------------------------------------------------------------------------

WaveDisplay::WaveDisplay (DrumsProcessor& p) : proc (p) {}

juce::Rectangle<float> WaveDisplay::wave() const { return getLocalBounds().toFloat().reduced (14.0f, 12.0f).withTrimmedBottom (16.0f); }

float WaveDisplay::startX() const
{
    const float start = proc.parameters().getRawParameterValue (DrumsProcessor::padParamId (proc.selectedPad, "start"))->load();
    return wave().getX() + wave().getWidth() * start;
}

void WaveDisplay::paint (juce::Graphics& g)
{
    const auto inner = ceramic::display (g, getLocalBounds().toFloat(), 8.0f);
    const auto& info = proc.padInfo (proc.selectedPad);
    const auto accent = look::category (info.category);
    // Grid
    g.setColour (juce::Colour (0xff2e3434).withAlpha (0.7f));
    for (int i = 1; i < 8; ++i)
        g.fillRect (juce::Rectangle<float> (inner.getX() + inner.getWidth() * static_cast<float> (i) / 8.0f, inner.getY() + 4.0f, 1.0f, inner.getHeight() - 22.0f));
    g.fillRect (juce::Rectangle<float> (inner.getX() + 6.0f, wave().getCentreY(), inner.getWidth() - 12.0f, 1.0f));
    if (info.sound != nullptr && ! info.sound->peakMax.empty())
    {
        const float start = proc.parameters().getRawParameterValue (DrumsProcessor::padParamId (proc.selectedPad, "start"))->load();
        look::waveform (g, wave(), info.sound->peakMin, info.sound->peakMax, accent.interpolatedWith (juce::Colours::white, 0.1f), start);
        // START: a handle on the region it keeps.
        const float sx = startX();
        g.setColour (cc::accent.withAlpha (0.12f));
        g.fillRect (wave().withLeft (sx));
        g.setColour (cc::accent);
        g.fillRect (juce::Rectangle<float> (sx - 0.75f, wave().getY(), 1.5f, wave().getHeight()));
        g.setColour (juce::Colours::white);
        g.fillRect (juce::Rectangle<float> (8.0f, 8.0f).withCentre ({ sx, wave().getY() }));
        g.setColour (cc::accent);
        g.drawRect (juce::Rectangle<float> (8.0f, 8.0f).withCentre ({ sx, wave().getY() }), 1.5f);
        g.setColour (juce::Colour (0xff8d8c86));
        g.setFont (type::micro (11.5f));
        const double dur = info.sound->durationSeconds;
        for (int i = 0; i <= 4; ++i)
        {
            const float x = wave().getX() + wave().getWidth() * static_cast<float> (i) / 4.0f;
            const auto box = i == 0 ? juce::Rectangle<float> (x, inner.getBottom() - 17.0f, 48.0f, 14.0f)
                           : i == 4 ? juce::Rectangle<float> (x - 48.0f, inner.getBottom() - 17.0f, 48.0f, 14.0f)
                                    : juce::Rectangle<float> (x - 24.0f, inner.getBottom() - 17.0f, 48.0f, 14.0f);
            g.drawText (juce::String (dur * i / 4.0, 2) + " s", box,
                        i == 0 ? juce::Justification::centredLeft : i == 4 ? juce::Justification::centredRight : juce::Justification::centred);
        }
    }
    else
    {
        g.setColour (juce::Colour (0xff8d8c86));
        g.setFont (type::micro (14.0f));
        juce::String text = "Drop a sample on the pad, or Replace Sample...";
        if (info.loading)
            text = "Loading...";
        else if (! info.missingFilename.empty())
            text = "Missing sample: " + juce::String::fromUTF8 (info.missingFilename.c_str()) + ". Load a file to relink it.";
        else if (! info.error.empty())
            text = "Could not load: " + juce::String::fromUTF8 (info.error.c_str());
        g.drawFittedText (text, inner.toNearestInt().reduced (20, 0), juce::Justification::centred, 2, 1.0f);
    }
}

void WaveDisplay::mouseMove (const juce::MouseEvent& e)
{
    setMouseCursor (std::abs (e.position.x - startX()) < 8.0f ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::NormalCursor);
}

void WaveDisplay::mouseDown (const juce::MouseEvent& e)
{
    dragging = std::abs (e.position.x - startX()) < 8.0f;
    if (dragging)
        if (auto* p = proc.parameters().getParameter (DrumsProcessor::padParamId (proc.selectedPad, "start")))
            p->beginChangeGesture();
}

void WaveDisplay::mouseDrag (const juce::MouseEvent& e)
{
    if (! dragging)
        return;
    if (auto* p = proc.parameters().getParameter (DrumsProcessor::padParamId (proc.selectedPad, "start")))
        p->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, (e.position.x - wave().getX()) / wave().getWidth()));
    repaint();
}

void WaveDisplay::mouseUp (const juce::MouseEvent&)
{
    if (dragging)
        if (auto* p = proc.parameters().getParameter (DrumsProcessor::padParamId (proc.selectedPad, "start")))
            p->endChangeGesture();
    dragging = false;
}

// ---------------------------------------------------------------------------------------
// Selected pad
// ---------------------------------------------------------------------------------------

SamplePanel::SamplePanel (DrumsProcessor& p) : proc (p), wave (p)
{
    name.setEditable (false, true, false);
    name.setFont (plugin::fonts::make (26.0f, look::Weight::semibold, 0.04f));
    name.setColour (juce::Label::textColourId, cc::text);
    name.setTooltip ("Double-click to rename");
    name.onTextChange = [this] { proc.setPadName (proc.selectedPad, name.getText()); };
    addAndMakeVisible (name);
    previous.icon = [] (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c) { ceramic::iconChevron (g, r, c, false); };
    next.icon = [] (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c) { ceramic::iconChevron (g, r, c, true); };
    menu.icon = [] (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c) { ceramic::iconGear (g, r, c); };
    auditionLayer.icon = [] (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c) { ceramic::iconPlay (g, r, c); };
    previous.onClick = [this] {
        const int n = proc.visiblePads();
        if (onSelect)
            onSelect (proc.padAtSlot ((proc.slotOfPad (proc.selectedPad) + n - 1) % n));
    };
    next.onClick = [this] {
        const int n = proc.visiblePads();
        if (onSelect)
            onSelect (proc.padAtSlot ((proc.slotOfPad (proc.selectedPad) + 1) % n));
    };
    menu.onClick = [this] { showMenu(); };
    replace.onClick = [this] {
        if (onLoad)
            onLoad();
    };
    auditionLayer.onStateChange = [this] {
        static bool wasDown = false;
        const bool down = auditionLayer.isDown();
        if (down && ! wasDown)
            proc.auditionPad (proc.selectedPad, 100);
        else if (! down && wasDown)
            proc.releaseAuditionedPad (proc.selectedPad);
        wasDown = down;
    };
    for (auto* b : { &previous, &next, &menu, &replace, &auditionLayer })
        addAndMakeVisible (b);
    addAndMakeVisible (wave);

    start.setup (*this, "START", cc::accent);
    tune.setup (*this, "TUNE", cc::accent, true);
    volume.setup (*this, "VOLUME", cc::accent);
    pan.setup (*this, "PAN", cc::accent, true);
    addAndMakeVisible (reverse);

    voice.addItem ("Polyphonic", 1);
    voice.addItem ("Mono retrigger", 2);
    voice.onChange = [this] { proc.setPadVoiceMode (proc.selectedPad, voice.getSelectedId() == 2 ? drums::VoiceMode::monoRetrigger : drums::VoiceMode::poly); };
    choke.addItem ("None", 1);
    for (int i = 1; i <= 8; ++i)
        choke.addItem ("Group " + juce::String (i), i + 1);
    choke.onChange = [this] { proc.setPadChoke (proc.selectedPad, choke.getSelectedId() - 1); };
    for (auto* c : { &voice, &choke })
    {
        styleCombo (*c);
        addAndMakeVisible (*c);
    }
    oneShot.setTooltip ("On: the sample plays to its end. Off (gate): note-off releases it.");
    oneShot.onClick = [this] { proc.setPadTrigger (proc.selectedPad, oneShot.getToggleState() ? drums::TriggerMode::oneShot : drums::TriggerMode::gate); };
    addAndMakeVisible (oneShot);
}

void SamplePanel::bind()
{
    const int p = proc.selectedPad;
    auto& state = proc.parameters();
    start.attach (state, DrumsProcessor::padParamId (p, "start"));
    tune.attach (state, DrumsProcessor::padParamId (p, "tune"));
    volume.attach (state, DrumsProcessor::padParamId (p, "gain"));
    pan.attach (state, DrumsProcessor::padParamId (p, "pan"));
    reverseAttachment.reset();
    reverseAttachment = std::make_unique<ButtonAttachment> (state, DrumsProcessor::padParamId (p, "reverse"), reverse);
    // Value text after attaching (an attachment installs the parameter's own).
    start.slider.textFromValueFunction = [] (double v) { return percent (v); };
    tune.slider.textFromValueFunction = [] (double v) { return (v > 0.005 ? "+" : "") + juce::String (v, 1) + " st"; };
    volume.slider.textFromValueFunction = [] (double v) { return juce::String (v, 1) + " dB"; };
    pan.slider.textFromValueFunction = [] (double v) {
        return std::abs (v) < 0.005 ? juce::String ("C") : (v < 0.0 ? "L" : "R") + juce::String (juce::roundToInt (std::abs (v) * 100.0));
    };
    const auto& info = proc.padInfo (p);
    const auto accent = look::category (info.category);
    for (auto* k : { &start, &tune, &volume, &pan })
    {
        k->slider.getProperties().set ("arc", static_cast<juce::int64> (accent.getARGB()));
        k->slider.updateText();
        k->slider.repaint();
    }
    name.setText (proc.padDisplayName (p).toUpperCase(), juce::dontSendNotification);
    voice.setSelectedId (info.voiceMode == drums::VoiceMode::monoRetrigger ? 2 : 1, juce::dontSendNotification);
    choke.setSelectedId (info.chokeGroup + 1, juce::dontSendNotification);
    oneShot.setToggleState (info.trigger == drums::TriggerMode::oneShot, juce::dontSendNotification);
    auditionLayer.setEnabled (info.sound != nullptr);
    repaint();
    wave.repaint();
}

void SamplePanel::showMenu()
{
    const int p = proc.selectedPad;
    juce::PopupMenu m;
    m.addItem ("Load sample...", [this] {
        if (onLoad)
            onLoad();
    });
    m.addItem ("Rename", [this] { name.showEditor(); });
    juce::PopupMenu categories;
    for (int c = 0; c < 10; ++c)
    {
        const auto cat = static_cast<drums::DrumCategory> (c);
        categories.addItem (look::categoryName (cat), true, proc.padInfo (p).category == cat, [this, p, cat] { proc.setPadCategory (p, cat, true); });
    }
    m.addSubMenu ("Category", categories);
    m.addSeparator();
    m.addItem ("Clear pad", proc.padInfo (p).sound != nullptr || ! proc.padInfo (p).missingFilename.empty(), false, [this, p] { proc.clearPad (p); });
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&menu));
}

void SamplePanel::mouseDown (const juce::MouseEvent& e)
{
    if (keyMapTabArea().contains (e.getPosition()) && onOpenKeyMap)
        onOpenKeyMap();
}

void SamplePanel::resized()
{
    name.setBounds (64, 9, 220, 34);
    previous.setBounds (290, 12, 32, 30);
    next.setBounds (324, 12, 32, 30);
    menu.setBounds (getWidth() - 50, 12, 34, 30);

    wave.setBounds (16, 56, 944, 200);
    auto knobs = juce::Rectangle<int> (16, 262, 520, 80);
    for (auto* k : { &start, &tune })
        k->setBounds (knobs.removeFromLeft (100));
    knobs.removeFromLeft (24);
    reverse.setBounds (knobs.removeFromLeft (70).withTrimmedTop (30).withHeight (28));

    // Layers column
    auditionLayer.setBounds (982, 92, 30, 26);
    replace.setBounds (980, 214, 200, 32);

    // Play settings column
    voice.setBounds (1272, 74, 160, 28);
    choke.setBounds (1272, 130, 160, 28);
    oneShot.setBounds (1270, 166, 140, 26);
    auto mix = juce::Rectangle<int> (1262, 200, 180, 140);
    volume.setBounds (mix.removeFromLeft (90));
    pan.setBounds (mix);
}

void SamplePanel::paint (juce::Graphics& g)
{
    ceramic::card (g, getLocalBounds().toFloat(), 12.0f);
    const int p = proc.selectedPad;
    const auto& info = proc.padInfo (p);
    ceramic::badge (g, juce::Rectangle<float> (18.0f, 12.0f, 36.0f, 30.0f), juce::String (p + 1), cc::accent, juce::Colours::white, 17.0f);

    // Tabs (B1 refined flat): SAMPLE is this view; KEY MAP opens the editor.
    g.setFont (plugin::fonts::make (14.0f, look::Weight::semibold, 0.1f));
    g.setColour (cc::text);
    g.drawText ("SAMPLE", juce::Rectangle<int> (392, 12, 84, 30), juce::Justification::centred);
    ceramic::tabUnderline (g, 400.0f, 41.0f, 68.0f);
    g.setColour (cc::textSecondary.withAlpha (0.88f));
    g.drawText ("KEY MAP", keyMapTabArea(), juce::Justification::centred);
    g.setColour (juce::Colour (0xffded4c5));
    g.fillRect (16, 50, getWidth() - 32, 1);

    // Sample name in the header (right).
    g.setColour (cc::textSecondary);
    g.setFont (type::micro (13.5f));
    if (info.sound != nullptr)
        g.drawFittedText (juce::String::fromUTF8 (info.sound->filename.c_str()), juce::Rectangle<int> (getWidth() - 360, 12, 300, 30), juce::Justification::centredRight, 1, 1.0f);

    // Layers column
    g.setColour (juce::Colour (0xffded4c5));
    g.fillRect (968, 60, 1, getHeight() - 76);
    g.fillRect (1252, 60, 1, getHeight() - 76);
    smallLabel (g, { 982, 56, 200, 20 }, "SAMPLE LAYER");
    auto list = juce::Rectangle<float> (978.0f, 84.0f, 262.0f, 120.0f);
    ceramic::recess (g, list, 6.0f, juce::Colour (0xfff7f2ea));
    if (info.sound != nullptr)
    {
        auto row = juce::Rectangle<float> (list.getX() + 4.0f, list.getY() + 4.0f, list.getWidth() - 8.0f, 30.0f);
        g.setColour (cc::keyOn.withAlpha (0.8f));
        g.fillRoundedRectangle (row, 4.0f);
        g.setColour (cc::accent);
        g.fillRect (row.withWidth (2.5f));
        g.setColour (cc::text);
        g.setFont (type::micro (13.5f));
        g.drawFittedText (juce::String::fromUTF8 (info.sound->filename.c_str()), row.withTrimmedLeft (40.0f).withTrimmedRight (52.0f).toNearestInt(), juce::Justification::centredLeft, 1, 1.0f);
        g.setColour (cc::textSecondary);
        g.drawText (juce::String (info.sound->durationSeconds, 2) + " s", row.withTrimmedRight (8.0f), juce::Justification::centredRight);
        g.setColour (cc::textMicro);
        g.setFont (type::micro (12.0f));
        g.drawFittedText ("Round robin, velocity layers and stacks come with multi-file drops (next).", list.withTrimmedTop (42.0f).reduced (10.0f, 4.0f).toNearestInt(),
                          juce::Justification::topLeft, 3, 1.0f);
    }
    else
    {
        g.setColour (cc::textMicro);
        g.setFont (type::micro (13.0f));
        g.drawText ("No sample", list.reduced (12.0f, 0.0f), juce::Justification::centredLeft);
    }

    // Play settings column
    smallLabel (g, { 1272, 54, 160, 18 }, "PLAY MODE");
    smallLabel (g, { 1272, 110, 160, 18 }, "CHOKE GROUP");
    g.setColour (cc::textSecondary);
    g.setFont (plugin::fonts::make (12.5f, look::Weight::medium, 0.08f));
    g.drawText ("REVERSE", reverse.getBounds().translated (0, -26).withWidth (90).translated (-10, 0), juce::Justification::centred);
}

// ---------------------------------------------------------------------------------------
// Keyboard range strip
// ---------------------------------------------------------------------------------------

KeyboardRangeStrip::KeyboardRangeStrip (DrumsProcessor& p) : proc (p)
{
    look::makeSegment (padMode, 41);
    look::makeSegment (expandedMode, 41);
    padMode.onClick = [this] {
        proc.setKeyboardMode (drums::KeyboardMode::pad);
        if (onModeChanged)
            onModeChanged();
    };
    expandedMode.onClick = [this] {
        proc.setKeyboardMode (drums::KeyboardMode::expanded);
        if (onModeChanged)
            onModeChanged();
    };
    open.icon = [] (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c) { ceramic::iconKeys (g, r, c); };
    open.setTooltip ("KEY MAP: edit every pad's note or key range");
    open.onClick = [this] {
        if (onOpenKeyMap)
            onOpenKeyMap();
    };
    root.addItem ("Lowest Key", 1);
    root.addItem ("Middle of Zone", 2);
    root.addItem ("Highest Key", 3);
    root.addItem ("Custom", 4);
    root.setItemEnabled (4, false);
    root.onChange = [this] {
        const int pad = proc.selectedPad;
        auto next = proc.arrangement();
        const auto& z = next.map.zone[static_cast<std::size_t> (pad)];
        if (! z.enabled())
            return;
        const int id = root.getSelectedId();
        const int note = id == 1 ? drums::noNote : id == 2 ? (z.low + z.high) / 2 : id == 3 ? z.high : z.root;
        if (id < 4 && drums::setZoneRoot (next.map, pad, note == z.low ? drums::noNote : note).applied)
            proc.applyEdit (next, "Root note");
    };
    presets.setTextWhenNothingSelected ("Zone Presets");
    presets.addItem ("Standard Drum Kit", 1);
    presets.addItem ("Compact Chromatic", 2);
    presets.addItem ("One Octave Each", 3);
    presets.addItem ("Two Octaves Each", 4);
    presets.addItem ("Fit From Selected Root", 5);
    presets.onChange = [this] {
        const int id = presets.getSelectedId();
        presets.setSelectedId (0, juce::dontSendNotification);
        if (id > 0 && onAutoMap)
            onAutoMap (id);
    };
    for (auto* c : { &root, &presets })
    {
        styleCombo (*c);
        addAndMakeVisible (*c);
    }
    for (auto* b : { &padMode, &expandedMode, &open })
        addAndMakeVisible (b);
    refresh();
}

juce::Rectangle<float> KeyboardRangeStrip::keys() const { return { 18.0f, 64.0f, static_cast<float> (getWidth()) - 36.0f, 58.0f }; }

float KeyboardRangeStrip::noteX (double note) const
{
    const auto k = keys();
    return k.getX() + static_cast<float> (note / 128.0) * k.getWidth();
}

void KeyboardRangeStrip::refresh()
{
    const bool exp = proc.keyboardMode() == drums::KeyboardMode::expanded;
    padMode.setToggleState (! exp, juce::dontSendNotification);
    expandedMode.setToggleState (exp, juce::dontSendNotification);
    root.setEnabled (exp);
    const auto& z = proc.keyMap().zone[static_cast<std::size_t> (proc.selectedPad)];
    int id = 1;
    if (z.root >= 0 && z.root != z.low)
        id = z.root == z.high ? 3 : z.root == (z.low + z.high) / 2 ? 2 : 4;
    root.setSelectedId (id, juce::dontSendNotification);
    presets.setItemEnabled (3, exp);
    presets.setItemEnabled (4, exp);
    for (int n = 0; n < 128; ++n)
        held[static_cast<std::size_t> (n)] = proc.isNoteHeld (n);
    repaint();
}

void KeyboardRangeStrip::resized()
{
    padMode.setBounds (200, 9, 72, 28);
    expandedMode.setBounds (272, 9, 104, 28);
    root.setBounds (getWidth() - 360, 26, 150, 28);
    presets.setBounds (getWidth() - 196, 26, 140, 28);
    open.setBounds (getWidth() - 50, 25, 34, 30);
}

void KeyboardRangeStrip::mouseDown (const juce::MouseEvent& e)
{
    // A click on a zone (or a PAD note) selects its pad.
    if (! keys().expanded (0.0f, 22.0f).contains (e.position))
        return;
    const int note = juce::jlimit (0, 127, static_cast<int> ((e.position.x - keys().getX()) / keys().getWidth() * 128.0f));
    std::array<drums::Hit, drums::maxPads> hits {};
    if (drums::resolve (proc.keyMap(), proc.keyboardMode(), note, hits) > 0 && onSelect)
        onSelect (hits[0].pad);
}

void KeyboardRangeStrip::paint (juce::Graphics& g)
{
    ceramic::card (g, getLocalBounds().toFloat(), 12.0f);
    sectionTitle (g, { 18, 9, 180, 28 }, "KEYBOARD RANGE");
    smallLabel (g, { getWidth() - 360, 6, 150, 18 }, "ROOT NOTE");
    smallLabel (g, { getWidth() - 196, 6, 140, 18 }, "ZONE PRESETS");

    const auto k = keys();
    ceramic::recess (g, k.expanded (3.0f), 5.0f);
    const float px = k.getWidth() / 128.0f;
    const bool exp = proc.keyboardMode() == drums::KeyboardMode::expanded;
    const auto& map = proc.keyMap();
    // Keys: equal semitone columns (exactly under the zones), white then black on top.
    for (int n = 0; n < 128; ++n)
    {
        const auto col = juce::Rectangle<float> (noteX (n), k.getY(), px, k.getHeight());
        std::array<drums::Hit, drums::maxPads> hits {};
        const int count = drums::resolve (map, proc.keyboardMode(), n, hits);
        auto face = cc::keyNormal;
        if (count > 0)
            face = face.interpolatedWith (look::category (proc.padInfo (hits[0].pad).category), proc.selectedPad == hits[0].pad ? 0.55f : 0.38f);
        if (held[static_cast<std::size_t> (n)])
            face = cc::keyOn.interpolatedWith (cc::accent, 0.45f);
        g.setColour (face);
        g.fillRect (col.withTrimmedTop (k.getHeight() * 0.6f));
        if (! look::isBlackKey (n))
            g.fillRect (col.withHeight (k.getHeight() * 0.6f));
    }
    for (int n = 0; n < 128; ++n)
    {
        const float x = noteX (n);
        const bool whiteBoundary = n % 12 == 0 || n % 12 == 5;
        g.setColour (juce::Colour (0xffd2cabd));
        g.fillRect (juce::Rectangle<float> (x, whiteBoundary ? k.getY() : k.getY() + k.getHeight() * 0.6f, 0.8f, whiteBoundary ? k.getHeight() : k.getHeight() * 0.4f));
        if (look::isBlackKey (n))
        {
            const auto blackKey = juce::Rectangle<float> (x + px * 0.08f, k.getY(), px * 0.84f, k.getHeight() * 0.6f);
            g.setGradientFill (juce::ColourGradient (juce::Colour (0xff3a3936), blackKey.getX(), blackKey.getY(), juce::Colour (0xff1e1e1d), blackKey.getX(), blackKey.getBottom(), false));
            g.fillRoundedRectangle (blackKey, 1.5f);
            if (held[static_cast<std::size_t> (n)])
            {
                g.setColour (cc::accent.withAlpha (0.8f));
                g.fillRoundedRectangle (blackKey, 1.5f);
            }
        }
        if (n % 12 == 0)
        {
            g.setColour (cc::textSecondary);
            g.setFont (type::micro (11.5f));
            g.drawText (look::noteName (n), juce::Rectangle<float> (x - 18.0f, k.getBottom() + 4.0f, 36.0f, 14.0f), juce::Justification::centred);
        }
    }
    // Pad numbers above their zones (or notes), in the pad's colour.
    for (int p = 0; p < drums::maxPads; ++p)
    {
        const auto i = static_cast<std::size_t> (p);
        int lo = -1, hi = -1;
        if (exp && map.zone[i].enabled())
        {
            lo = map.zone[i].low;
            hi = map.zone[i].high;
        }
        else if (! exp && map.padNote[i] >= 0)
            lo = hi = map.padNote[i];
        if (lo < 0)
            continue;
        const auto band = juce::Rectangle<float> (noteX (lo), k.getY() - 16.0f, noteX (hi + 1) - noteX (lo), 14.0f);
        const auto c = look::category (proc.padInfo (p).category);
        g.setColour (c.withAlpha (proc.selectedPad == p ? 0.95f : 0.7f));
        g.fillRoundedRectangle (band.withTrimmedTop (10.0f), 1.5f);
        g.setColour (proc.selectedPad == p ? cc::accent : cc::text);
        g.setFont (plugin::fonts::make (12.0f, look::Weight::semibold));
        g.drawText (juce::String (p + 1), band.withTrimmedBottom (4.0f).expanded (8.0f, 0.0f), juce::Justification::centred);
    }
}

// ---------------------------------------------------------------------------------------
// AMP envelope
// ---------------------------------------------------------------------------------------

EnvelopePanel::EnvelopePanel (DrumsProcessor& p) : proc (p)
{
    attack.setup (*this, "ATTACK", cc::accent);
    decay.setup (*this, "DECAY", cc::accent);
    sustain.setup (*this, "SUSTAIN", cc::accent);
    release.setup (*this, "RELEASE", cc::accent);
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
    sustain.slider.textFromValueFunction = [] (double v) { return percent (v); };
    for (auto* k : { &attack, &decay, &sustain, &release })
        k->slider.updateText();
    repaint();
}

void EnvelopePanel::resized()
{
    auto knobs = getLocalBounds().reduced (14, 8).removeFromBottom (86);
    const int kw = knobs.getWidth() / 4;
    for (auto* k : { &attack, &decay, &sustain, &release })
        k->setBounds (knobs.removeFromLeft (kw), 16);
}

void EnvelopePanel::paint (juce::Graphics& g)
{
    ceramic::card (g, getLocalBounds().toFloat(), 12.0f);
    auto r = getLocalBounds().reduced (18, 8);
    auto header = r.removeFromTop (26);
    sectionTitle (g, header.removeFromLeft (150), "MODULATION");
    g.setFont (plugin::fonts::make (13.0f, look::Weight::semibold, 0.1f));
    g.setColour (cc::text);
    g.drawText ("AMP", header.removeFromLeft (44), juce::Justification::centred);
    ceramic::tabUnderline (g, static_cast<float> (header.getX()) - 38.0f, static_cast<float> (header.getBottom()) - 2.0f, 32.0f);
    g.setColour (cc::textMicro);
    g.setFont (type::micro (12.0f));
    g.drawFittedText ("of " + proc.padDisplayName (proc.selectedPad).toUpperCase() + ".  LFO / ENV / VEL / NOTE routes: next round", header.withTrimmedLeft (8),
                      juce::Justification::centredLeft, 1, 1.0f);

    auto well = r.removeFromTop (r.getHeight() - 92).toFloat().reduced (0.0f, 4.0f);
    const auto inner = ceramic::display (g, well, 7.0f);
    const double a = attack.slider.getValue(), d = decay.slider.getValue(), s = sustain.slider.getValue(), rel = release.slider.getValue();
    auto tx = [] (double secs) { return std::log10 (1.0 + secs * 100.0); };
    const double hold = 0.25;
    const double total = tx (a) + tx (d) + tx (hold) + tx (rel) + 1.0e-9;
    const auto area = inner.reduced (12.0f, 8.0f);
    auto px = [&] (double t) { return area.getX() + static_cast<float> (t / total) * area.getWidth(); };
    auto py = [&] (double level) { return area.getBottom() - static_cast<float> (level) * area.getHeight(); };
    juce::Path env;
    double t = 0.0;
    env.startNewSubPath (px (0.0), py (0.0));
    t += tx (a);
    const juce::Point<float> peak (px (t), py (1.0));
    env.lineTo (peak);
    t += tx (d);
    const juce::Point<float> knee (px (t), py (d > 0.0 ? s : 1.0));
    env.lineTo (knee);
    t += tx (hold);
    env.lineTo (px (t), py (d > 0.0 ? s : 1.0));
    t += tx (rel);
    const juce::Point<float> end (px (t), py (0.0));
    env.lineTo (end);
    juce::Path fill (env);
    fill.lineTo (px (0.0), py (0.0));
    fill.closeSubPath();
    g.setGradientFill (juce::ColourGradient (cc::accent.withAlpha (0.35f), 0.0f, area.getY(), cc::accent.withAlpha (0.05f), 0.0f, area.getBottom(), false));
    g.fillPath (fill);
    g.setColour (cc::accent);
    g.strokePath (env, juce::PathStrokeType (1.8f));
    for (auto pt : { juce::Point<float> (px (0.0), py (0.0)), peak, knee, end })
    {
        g.setColour (juce::Colours::white);
        g.fillEllipse (juce::Rectangle<float> (8.0f, 8.0f).withCentre (pt));
        g.setColour (cc::accent);
        g.drawEllipse (juce::Rectangle<float> (8.0f, 8.0f).withCentre (pt), 1.4f);
    }
}

// ---------------------------------------------------------------------------------------
// Content
// ---------------------------------------------------------------------------------------

int DrumsContent::extraHeight (DrumsProcessor::PadLayout layout) noexcept
{
    switch (layout)
    {
        case DrumsProcessor::PadLayout::row8: return 0;
        case DrumsProcessor::PadLayout::grid8: return 150;
        case DrumsProcessor::PadLayout::grid9: return 230;
    }
    return 0;
}

juce::Rectangle<int> DrumsContent::padCard() const { return { 44, 90, 1448, 192 + extraHeight (proc.padLayout) }; }
juce::Rectangle<int> DrumsContent::editorCard() const { return { 44, padCard().getBottom() + 12, 1448, 345 }; }
juce::Rectangle<int> DrumsContent::rangeCard() const { return { 44, editorCard().getBottom() + 10, 1448, 150 }; }
juce::Rectangle<int> DrumsContent::bottomRow() const { return { 44, rangeCard().getBottom() + 10, 1448, 195 }; }

DrumsContent::DrumsContent (DrumsProcessor& p) : proc (p), grid (p), sample (p), range (p), envelope (p), keyMap (p)
{
    setWantsKeyboardFocus (true);
    addAndMakeVisible (grid);
    grid.onSelect = [this] (int pad) { selectPad (pad); };
    grid.onLoadRequest = [this] (int pad) { openLoadDialog (pad); };

    undoButton.icon = [] (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c) { ceramic::iconUndo (g, r, c, false); };
    redoButton.icon = [] (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c) { ceramic::iconUndo (g, r, c, true); };
    undoButton.setTooltip ("Undo (Cmd+Z)");
    redoButton.setTooltip ("Redo (Shift+Cmd+Z)");
    undoButton.onClick = [this] { proc.undoManager().undo(); };
    redoButton.onClick = [this] { proc.undoManager().redo(); };
    kitName.setEditable (false, true, false);
    kitName.setJustificationType (juce::Justification::centred);
    kitName.setFont (plugin::fonts::make (19.0f, look::Weight::medium, 0.02f));
    kitName.setColour (juce::Label::textColourId, cc::text);
    kitName.setTooltip ("Double-click to rename the kit");
    kitName.onTextChange = [this] { proc.kitName = kitName.getText().trim().toStdString(); };
    for (auto* b : { &layout1x8, &layout2x4, &layout3x3 })
        look::makeSegment (*b, 12);
    layout1x8.onClick = [this] { setLayout (DrumsProcessor::PadLayout::row8); };
    layout2x4.onClick = [this] { setLayout (DrumsProcessor::PadLayout::grid8); };
    layout3x3.onClick = [this] { setLayout (DrumsProcessor::PadLayout::grid9); };
    ninthPad.setTooltip ("Pad 9 holds a sound and keeps playing in this layout. Click to edit it.");
    ninthPad.onClick = [this] { selectPad (proc.padAtSlot (8)); };
    for (auto* c : std::initializer_list<juce::Component*> { &undoButton, &redoButton, &kitName, &layout1x8, &layout2x4, &layout3x3, &volume, &meter })
        addAndMakeVisible (c);
    addChildComponent (ninthPad);
    volume.setTooltip ("Kit volume");
    volume.setDoubleClickReturnValue (true, -9.0);
    volumeAttachment = std::make_unique<SliderAttachment> (proc.parameters(), "kit.volume", volume);

    addAndMakeVisible (sample);
    sample.onSelect = [this] (int pad) { selectPad (pad); };
    sample.onLoad = [this] { openLoadDialog (proc.selectedPad); };
    sample.onOpenKeyMap = [this] { setKeyMapOpen (true); };
    addAndMakeVisible (range);
    range.onSelect = [this] (int pad) { selectPad (pad); };
    range.onOpenKeyMap = [this] { setKeyMapOpen (true); };
    range.onModeChanged = [this] { refreshModeBar(); };
    range.onAutoMap = [this] (int id) {
        setKeyMapOpen (true);
        keyMap.previewAutoMap (static_cast<KeyMapEditor::AutoMap> (id));
    };
    addAndMakeVisible (envelope);

    // Effects (whole kit) and the global macros: the rendered macro knob, OSP's colours.
    const char* fxIds[] = { "kit.drive", "kit.motion", "kit.echo", "kit.space" };
    const char* fxNames[] = { "DRIVE", "MOVEMENT", "ECHO", "SPACE" };
    const int fxColours[] = { 1, 3, 4, 5 };   // OSP: LIFE 0, DRIVE 1, CHARACTER 2, MOVEMENT 3, ECHO 4, SPACE 5
    for (std::size_t i = 0; i < effects.size(); ++i)
    {
        effects[i].setup (*this, fxNames[i], plugin::design::colour::macro (fxColours[i]), false, true);
        effects[i].label.setVisible (false);   // the name sits in the effect's header, beside its power key
        effects[i].attach (proc.parameters(), fxIds[i]);
        effects[i].slider.textFromValueFunction = [] (double v) { return percent (v); };
        effects[i].slider.updateText();
        auto& power = effectPower[i];
        power.role = 0.6f;
        power.corner = 5.0f;
        power.icon = [] (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c) { ceramic::iconPower (g, r, c); };
        power.setTooltip (juce::String (fxNames[i]) + " on / off (off sets it to 0 %, on brings back the last setting)");
        power.onClick = [this, i, id = juce::String (fxIds[i])] {
            if (auto* param = proc.parameters().getParameter (id))
            {
                const float now = param->getValue();
                param->beginChangeGesture();
                if (now > 0.0001f)
                {
                    effectMemory[i] = now;
                    param->setValueNotifyingHost (0.0f);
                }
                else
                    param->setValueNotifyingHost (effectMemory[i]);
                param->endChangeGesture();
            }
        };
        addAndMakeVisible (power);
    }
    const char* macroIds[] = { "kit.life", "kit.dynamics", "kit.character" };
    const char* macroNames[] = { "LIFE", "DYNAMICS", "CHARACTER" };
    const int macroColours[] = { 0, 1, 2 };
    for (std::size_t i = 0; i < macros.size(); ++i)
    {
        macros[i].setup (*this, macroNames[i], plugin::design::colour::macro (macroColours[i]), false, true);
        macros[i].attach (proc.parameters(), macroIds[i]);
        macros[i].slider.textFromValueFunction = [] (double v) { return percent (v); };
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
    setSize (baseWidth, contentHeight());
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
    range.refresh();
    keyMap.refreshFromProcessor();
}

void DrumsContent::setLayout (DrumsProcessor::PadLayout layout)
{
    proc.padLayout = layout;
    setSize (baseWidth, contentHeight());
    grid.layoutChanged();
    keyMap.refreshFromProcessor();
    refreshModeBar();
    if (onHeightChanged)
        onHeightChanged();
}

void DrumsContent::setKeyMapOpen (bool open)
{
    keyMap.setVisible (open);
    sample.setVisible (! open);
    range.setVisible (! open);
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
    lastExpanded = proc.keyboardMode() == drums::KeyboardMode::expanded;
    using L = DrumsProcessor::PadLayout;
    layout1x8.setToggleState (proc.padLayout == L::row8, juce::dontSendNotification);
    layout2x4.setToggleState (proc.padLayout == L::grid8, juce::dontSendNotification);
    layout3x3.setToggleState (proc.padLayout == L::grid9, juce::dontSendNotification);
    ninthPad.setVisible (! proc.isNineLayout() && proc.padInfo (proc.padAtSlot (8)).sound != nullptr);
    undoButton.setEnabled (proc.undoManager().canUndo());
    redoButton.setEnabled (proc.undoManager().canRedo());
    kitName.setText (juce::String::fromUTF8 (proc.kitName.c_str()), juce::dontSendNotification);
    range.refresh();
    for (std::size_t i = 0; i < effects.size(); ++i)
        effectPower[i].setToggleState (effects[i].slider.getValue() > 0.0001, juce::dontSendNotification);
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
    const auto [pl, pr] = proc.takeOutputPeaks();
    meter.setLevels (pl, pr);
    const bool exp = proc.keyboardMode() == drums::KeyboardMode::expanded;
    bool powerChanged = false;
    for (std::size_t i = 0; i < effects.size(); ++i)
        powerChanged = powerChanged || effectPower[i].getToggleState() != (effects[i].slider.getValue() > 0.0001);
    if (exp != lastExpanded || powerChanged || undoButton.isEnabled() != proc.undoManager().canUndo() || redoButton.isEnabled() != proc.undoManager().canRedo())
        refreshModeBar();   // automation, a host undo, or the KEY MAP's own buttons
    if (range.isVisible())
    {
        bool dirty = false;
        for (int n = 0; n < 128 && ! dirty; ++n)
            dirty = proc.isNoteHeld (n);
        static bool wasHeld = false;
        if (dirty || wasHeld)
            range.refresh();
        wasHeld = dirty;
    }
    repaint (bottomRow().withTrimmedTop (bottomRow().getHeight() - 28));   // the LEDs follow their macros
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
    // Top bar: the kit bar (undo, name, redo), the layout switch, the volume fader and meter.
    undoButton.setBounds (440, 28, 40, 36);
    redoButton.setBounds (484, 28, 40, 36);
    kitName.setBounds (532, 28, 360, 36);
    layout1x8.setBounds (960, 30, 66, 32);
    layout2x4.setBounds (1026, 30, 66, 32);
    layout3x3.setBounds (1092, 30, 66, 32);
    volume.setBounds (1250, 36, 190, 24);
    meter.setBounds (1458, 22, 18, 52);

    const auto pc = padCard();
    grid.setBounds (pc.reduced (12, 10));
    ninthPad.setBounds (1166, 30, 70, 32);
    sample.setBounds (editorCard());
    range.setBounds (rangeCard());
    keyMap.setBounds (editorCard().getUnion (rangeCard()));

    const auto row = bottomRow();
    envelope.setBounds (row.withWidth (556));
    // Effects: four ceramic cells (power key + name, the rendered knob, its value).
    const auto fx = juce::Rectangle<int> (row.getX() + 568, row.getY(), 564, row.getHeight());
    const int cw = (fx.getWidth() - 24) / 4;
    for (std::size_t i = 0; i < effects.size(); ++i)
    {
        const auto cell = juce::Rectangle<int> (fx.getX() + 12 + static_cast<int> (i) * cw, fx.getY() + 36, cw, fx.getHeight() - 44);
        effectPower[i].setBounds (cell.getX() + 8, cell.getY() + 4, 26, 24);
        effects[i].setBounds (cell.withTrimmedTop (34).reduced (6, 0), 0);
    }
    const auto global = juce::Rectangle<int> (fx.getRight() + 12, row.getY(), row.getRight() - fx.getRight() - 12, row.getHeight());
    const int mw = (global.getWidth() - 20) / 3;
    for (std::size_t i = 0; i < macros.size(); ++i)
        macros[i].setBounds (juce::Rectangle<int> (global.getX() + 10 + static_cast<int> (i) * mw, global.getY() + 34, mw, global.getHeight() - 58), 22);
}

void DrumsContent::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    g.fillAll (cc::backdrop);
    // The housing, then the teak cheeks in the side margins (they stand 1.5 mm proud of it).
    ceramic::housing (g, { 28.0f, 10.0f, bounds.getWidth() - 56.0f, bounds.getHeight() - 20.0f });
    ceramic::teakCheek (g, { 3.0f, 13.0f, 22.0f, bounds.getHeight() - 26.0f });
    ceramic::teakCheek (g, { bounds.getRight() - 25.0f, 13.0f, 22.0f, bounds.getHeight() - 26.0f });

    // Wordmark: ANDOR over spaced DRUMS.
    g.setColour (cc::text);
    g.setFont (plugin::fonts::make (40.0f, look::Weight::displayLight, 0.34f));
    g.drawText ("ANDOR", 50, 18, 330, 44, juce::Justification::centredLeft);
    g.setFont (plugin::fonts::make (15.0f, look::Weight::displayBold, 1.0f));
    g.drawText ("DRUMS", 52, 60, 300, 20, juce::Justification::centredLeft);

    // The kit bar: the panel colour, a soft recess (§5: large keys read as panel, not keys).
    ceramic::recess (g, { 432.0f, 24.0f, 470.0f, 44.0f }, 8.0f, cc::presetBar);
    g.setColour (juce::Colour (0xffded4c5));
    g.fillRect (528, 30, 1, 32);
    g.setColour (cc::textSecondary);
    g.setFont (plugin::fonts::make (12.0f, look::Weight::medium, 0.1f));
    g.drawText ("PADS", 960, 10, 198, 18, juce::Justification::centred);
    g.drawText ("VOLUME", 1250, 14, 120, 18, juce::Justification::centredLeft);
    g.drawText (juce::String (proc.parameters().getRawParameterValue ("kit.volume")->load(), 1) + " dB", 1340, 14, 100, 18, juce::Justification::centredRight);

    // The cards.
    ceramic::card (g, padCard().toFloat(), 12.0f);
    const auto row = bottomRow();
    const auto fx = juce::Rectangle<int> (row.getX() + 568, row.getY(), 564, row.getHeight());
    const auto global = juce::Rectangle<int> (fx.getRight() + 12, row.getY(), row.getRight() - fx.getRight() - 12, row.getHeight());
    {
        ceramic::card (g, fx.toFloat(), 12.0f);
        sectionTitle (g, fx.reduced (18, 8).removeFromTop (26), "EFFECTS", "(KIT)");
        const int cw = (fx.getWidth() - 24) / 4;
        static const char* fxNames[] = { "DRIVE", "MOVEMENT", "ECHO", "SPACE" };
        for (int i = 0; i < 4; ++i)
        {
            const auto cell = juce::Rectangle<float> (static_cast<float> (fx.getX() + 12 + i * cw), static_cast<float> (fx.getY() + 36), static_cast<float> (cw) - 8.0f,
                                                      static_cast<float> (fx.getHeight()) - 46.0f);
            ceramic::panel (g, cell, 8.0f);
            g.setColour (cc::text);
            g.setFont (plugin::fonts::make (13.0f, look::Weight::semibold, 0.08f));
            g.drawText (fxNames[i], cell.withTrimmedLeft (38.0f).withHeight (32.0f), juce::Justification::centredLeft);
        }
        ceramic::card (g, global.toFloat(), 12.0f);
        sectionTitle (g, global.reduced (18, 8).removeFromTop (26), "GLOBAL", "(KIT)");
        // Macro LEDs: lit while the macro does something (the glass lens, §9).
        const int mw = (global.getWidth() - 20) / 3;
        static const char* ids[] = { "kit.life", "kit.dynamics", "kit.character" };
        for (int i = 0; i < 3; ++i)
        {
            const float value = proc.parameters().getRawParameterValue (ids[i])->load();
            ceramic::lens (g, { static_cast<float> (global.getX() + 10 + i * mw + mw / 2), static_cast<float> (global.getBottom() - 14) }, 9.0f,
                           plugin::design::colour::macro (i), value > 0.0001f);
        }
    }
}

// ---------------------------------------------------------------------------------------
// Editor
// ---------------------------------------------------------------------------------------

DrumsEditor::DrumsEditor (DrumsProcessor& p) : AudioProcessorEditor (p), body (p)
{
    setLookAndFeel (&lookAndFeel);
    addAndMakeVisible (body);
    body.onHeightChanged = [this] { updateSizeForContent(); };
    setResizable (true, true);
    updateSizeForContent();
}

DrumsEditor::~DrumsEditor()
{
    setLookAndFeel (nullptr);
}

void DrumsEditor::updateSizeForContent()
{
    const int h = body.contentHeight();
    const double keep = scale;   // setting the limits may resize (and so rescale) first
    setResizeLimits (static_cast<int> (DrumsContent::baseWidth * minScale), static_cast<int> (h * minScale),
                     static_cast<int> (DrumsContent::baseWidth * maxScale), static_cast<int> (h * maxScale));
    if (auto* c = getConstrainer())
        c->setFixedAspectRatio (static_cast<double> (DrumsContent::baseWidth) / h);
    setSize (juce::roundToInt (DrumsContent::baseWidth * keep), juce::roundToInt (h * keep));
}

void DrumsEditor::resized()
{
    scale = static_cast<double> (getWidth()) / DrumsContent::baseWidth;
    body.setBounds (0, 0, DrumsContent::baseWidth, body.contentHeight());
    body.setTransform (juce::AffineTransform::scale (static_cast<float> (scale)));
}

void DrumsEditor::paint (juce::Graphics& g)
{
    g.fillAll (ceramic::colour::backdrop);
}

} // namespace osp::drumsplugin
