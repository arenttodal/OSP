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

const PadLayer* WaveDisplay::shownLayer() const
{
    const auto& layers = proc.padLayers (proc.selectedPad).layers;
    if (layers.empty())
        return nullptr;
    const int i = juce::jlimit (0, static_cast<int> (layers.size()) - 1, proc.selectedLayer[static_cast<std::size_t> (proc.selectedPad)]);
    return &layers[static_cast<std::size_t> (i)];
}

bool WaveDisplay::padStartMarker() const { return proc.padLayers (proc.selectedPad).layers.size() <= 1; }

float WaveDisplay::startX() const
{
    float start = 0.0f;
    if (padStartMarker())
        start = proc.parameters().getRawParameterValue (DrumsProcessor::padParamId (proc.selectedPad, "start"))->load();
    else if (const auto* l = shownLayer())
        start = static_cast<float> (l->params.start);
    return wave().getX() + wave().getWidth() * start;
}

float WaveDisplay::endX() const
{
    const auto* l = shownLayer();
    return wave().getX() + wave().getWidth() * (l != nullptr ? static_cast<float> (l->params.end) : 1.0f);
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
    const auto* layer = shownLayer();
    const auto sound = layer != nullptr ? layer->sound : nullptr;
    if (sound != nullptr && ! sound->peakMax.empty())
    {
        const float sx = startX(), ex = endX();
        const float start = (sx - wave().getX()) / wave().getWidth();
        look::waveform (g, wave(), sound->peakMin, sound->peakMax, accent.interpolatedWith (juce::Colours::white, 0.1f), start);
        // START and END: handles on the region that plays; after END is dimmed.
        g.setColour (cc::accent.withAlpha (0.12f));
        g.fillRect (wave().withLeft (sx).withRight (std::max (sx, ex)));
        if (ex < wave().getRight() - 0.5f)
        {
            g.setColour (cc::well.withAlpha (0.7f));
            g.fillRect (wave().withLeft (ex));
        }
        for (const bool isEnd : { false, true })
        {
            const float x = isEnd ? ex : sx;
            if (isEnd && ex >= wave().getRight() - 0.5f)
                continue;   // END at the very end: no handle drawn over the frame (still draggable)
            g.setColour (cc::accent);
            g.fillRect (juce::Rectangle<float> (x - 0.75f, wave().getY(), 1.5f, wave().getHeight()));
            const auto handle = juce::Rectangle<float> (8.0f, 8.0f).withCentre ({ x, isEnd ? wave().getBottom() : wave().getY() });
            g.setColour (juce::Colours::white);
            g.fillRect (handle);
            g.setColour (cc::accent);
            g.drawRect (handle, 1.5f);
        }
        g.setColour (juce::Colour (0xff8d8c86));
        g.setFont (type::micro (11.5f));
        const double dur = sound->durationSeconds;
        for (int i = 0; i <= 4; ++i)
        {
            const float x = wave().getX() + wave().getWidth() * static_cast<float> (i) / 4.0f;
            const auto box = i == 0 ? juce::Rectangle<float> (x, inner.getBottom() - 17.0f, 48.0f, 14.0f)
                           : i == 4 ? juce::Rectangle<float> (x - 48.0f, inner.getBottom() - 17.0f, 48.0f, 14.0f)
                                    : juce::Rectangle<float> (x - 24.0f, inner.getBottom() - 17.0f, 48.0f, 14.0f);
            g.drawText (juce::String (dur * i / 4.0, 2) + " s", box,
                        i == 0 ? juce::Justification::centredLeft : i == 4 ? juce::Justification::centredRight : juce::Justification::centred);
        }
        if (! padStartMarker())
        {
            g.setColour (juce::Colour (0xffc9c4ba));
            g.setFont (type::micro (12.0f));
            g.drawText ("LAYER " + juce::String (proc.selectedLayer[static_cast<std::size_t> (proc.selectedPad)] + 1) + "  " + juce::String::fromUTF8 (layer->displayName().c_str()),
                        inner.reduced (12.0f, 6.0f).withHeight (16.0f).toNearestInt(), juce::Justification::centredRight);
        }
    }
    else
    {
        g.setColour (juce::Colour (0xff8d8c86));
        g.setFont (type::micro (14.0f));
        juce::String text = "Drop samples on the pad, or Add Layers...";
        if (info.loading)
            text = "Loading...";
        else if (layer != nullptr && layer->sound == nullptr)
            text = "Missing sample: " + juce::String::fromUTF8 (layer->fileName().c_str())
                   + ". Kit menu > Find missing samples, or right-click the layer > Relink.";
        else if (! info.missingFilename.empty())
            text = "Missing sample: " + juce::String::fromUTF8 (info.missingFilename.c_str()) + ". Load a file to relink it.";
        else if (! info.error.empty())
            text = "Could not load: " + juce::String::fromUTF8 (info.error.c_str());
        g.drawFittedText (text, inner.toNearestInt().reduced (20, 0), juce::Justification::centred, 2, 1.0f);
    }
}

void WaveDisplay::mouseMove (const juce::MouseEvent& e)
{
    const bool near = std::abs (e.position.x - startX()) < 8.0f || (shownLayer() != nullptr && std::abs (e.position.x - endX()) < 8.0f);
    setMouseCursor (near ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::NormalCursor);
}

void WaveDisplay::mouseDown (const juce::MouseEvent& e)
{
    const float ds = std::abs (e.position.x - startX());
    const float de = shownLayer() != nullptr ? std::abs (e.position.x - endX()) : 1.0e9f;
    dragging = ds < 8.0f && ds <= de ? Drag::start : de < 8.0f ? Drag::end : Drag::none;
    if (dragging == Drag::start && padStartMarker())
        if (auto* p = proc.parameters().getParameter (DrumsProcessor::padParamId (proc.selectedPad, "start")))
            p->beginChangeGesture();
}

void WaveDisplay::mouseDrag (const juce::MouseEvent& e)
{
    if (dragging == Drag::none)
        return;
    const float at = juce::jlimit (0.0f, 1.0f, (e.position.x - wave().getX()) / wave().getWidth());
    const int pad = proc.selectedPad;
    if (dragging == Drag::start && padStartMarker())
    {
        if (auto* p = proc.parameters().getParameter (DrumsProcessor::padParamId (pad, "start")))
            p->setValueNotifyingHost (at);
    }
    else if (const auto* l = shownLayer())
    {
        // One undo step per drag: the first move opens it, the rest join it.
        auto params = l->params;
        if (dragging == Drag::start)
            params.start = std::min (static_cast<double> (at), params.end);
        else
            params.end = std::max (static_cast<double> (at), params.start);
        proc.setLayerParams (pad, proc.selectedLayer[static_cast<std::size_t> (pad)], params, firstMove);
        firstMove = false;
    }
    repaint();
}

void WaveDisplay::mouseUp (const juce::MouseEvent&)
{
    if (dragging == Drag::start && padStartMarker())
        if (auto* p = proc.parameters().getParameter (DrumsProcessor::padParamId (proc.selectedPad, "start")))
            p->endChangeGesture();
    dragging = Drag::none;
    firstMove = true;
}

// ---------------------------------------------------------------------------------------
// Selected pad
// ---------------------------------------------------------------------------------------

SamplePanel::SamplePanel (DrumsProcessor& p) : proc (p), layers (p), wave (p)
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
    addLayers.onClick = [this] { openAddDialog(); };
    addLayers.setTooltip ("Add samples to this pad as round robins, velocity layers or a stack");
    for (auto* b : { &previous, &next, &menu, &addLayers })
        addAndMakeVisible (b);
    addAndMakeVisible (wave);

    // Layers: how hits use them, the list, the round-robin order.
    const std::array<std::pair<ceramic::KeyButton*, drums::LayerMode>, 3> modes { { { &modeRobin, drums::LayerMode::roundRobin },
                                                                                      { &modeVelocity, drums::LayerMode::velocity },
                                                                                      { &modeStack, drums::LayerMode::stack } } };
    for (const auto& [button, mode] : modes)
    {
        button->fontHeight = 11.5f;
        button->onClick = [this, m = mode] { proc.setLayerMode (proc.selectedPad, m); };
        addAndMakeVisible (button);
    }
    modeRobin.setTooltip ("Round robin: a different layer every hit");
    modeVelocity.setTooltip ("Velocity layers: how hard you play chooses the layer");
    modeStack.setTooltip ("Stack: every layer sounds together");
    order.addItem ("Shuffle", 1);
    order.addItem ("Sequential", 2);
    order.addItem ("Random", 3);
    order.setTooltip ("Round-robin order. Shuffle: every layer once before any repeats. Random: never the same twice in a row.");
    order.onChange = [this] {
        const int id = order.getSelectedId();
        proc.setLayerOrder (proc.selectedPad, id == 2 ? drums::RoundRobinOrder::sequential : id == 3 ? drums::RoundRobinOrder::random : drums::RoundRobinOrder::shuffle);
    };
    styleCombo (order);
    addAndMakeVisible (order);
    layers.onSelectionChanged = [this] {
        bindLayer();
        wave.repaint();
        repaint();
    };
    addAndMakeVisible (layers);

    // The selected layer's LEVEL and VELOCITY range.
    layerLevel.setup (*this, "LAYER LEVEL", cc::accent);
    layerLevel.slider.setRange (-60.0, 12.0, 0.1);
    layerLevel.slider.setDoubleClickReturnValue (true, 0.0);
    layerLevel.slider.setSkewFactorFromMidPoint (-12.0);
    layerLevel.slider.textFromValueFunction = [] (double v) { return v <= -59.95 ? juce::String ("off") : juce::String (v, 1) + " dB"; };
    layerLevel.slider.onDragStart = [this] { levelGesture = true; };
    layerLevel.slider.onValueChange = [this] {
        const int pad = proc.selectedPad;
        const int l = proc.selectedLayer[static_cast<std::size_t> (pad)];
        const auto& list = proc.padLayers (pad).layers;
        if (l < 0 || l >= static_cast<int> (list.size()))
            return;
        auto params = list[static_cast<std::size_t> (l)].params;
        params.gainDb = layerLevel.slider.getValue();
        proc.setLayerParams (pad, l, params, levelGesture || ! layerLevel.slider.isMouseButtonDown());
        levelGesture = false;
    };
    velocity.onChange = [this] (int low, int high, bool gestureStart) {
        const int pad = proc.selectedPad;
        const int l = proc.selectedLayer[static_cast<std::size_t> (pad)];
        const auto& list = proc.padLayers (pad).layers;
        if (l < 0 || l >= static_cast<int> (list.size()))
            return;
        auto params = list[static_cast<std::size_t> (l)].params;
        params.velocityLow = low;
        params.velocityHigh = high;
        proc.setLayerParams (pad, l, params, gestureStart);
        repaint (velocityCaption());
    };
    addAndMakeVisible (velocity);

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
    const auto& padLayers = proc.padLayers (p);
    modeRobin.setToggleState (padLayers.mode == drums::LayerMode::roundRobin, juce::dontSendNotification);
    modeVelocity.setToggleState (padLayers.mode == drums::LayerMode::velocity, juce::dontSendNotification);
    modeStack.setToggleState (padLayers.mode == drums::LayerMode::stack, juce::dontSendNotification);
    order.setSelectedId (padLayers.order == drums::RoundRobinOrder::sequential ? 2 : padLayers.order == drums::RoundRobinOrder::random ? 3 : 1,
                         juce::dontSendNotification);
    order.setEnabled (padLayers.mode != drums::LayerMode::stack && padLayers.layers.size() > 1);
    for (auto* b : { &modeRobin, &modeVelocity, &modeStack })
        b->setEnabled (padLayers.layers.size() > 1);
    addLayers.setEnabled (padLayers.layers.size() < static_cast<std::size_t> (drums::maxLayers));
    layers.refresh();
    bindLayer();
    repaint();
    wave.repaint();
}

void SamplePanel::bindLayer()
{
    const int p = proc.selectedPad;
    const auto& list = proc.padLayers (p).layers;
    const int l = proc.selectedLayer[static_cast<std::size_t> (p)];
    const bool has = l >= 0 && l < static_cast<int> (list.size());
    for (juce::Component* c : { static_cast<juce::Component*> (&layerLevel.slider), static_cast<juce::Component*> (&layerLevel.label),
                                static_cast<juce::Component*> (&velocity) })
        c->setVisible (has);
    if (! has)
        return;
    const auto& params = list[static_cast<std::size_t> (l)].params;
    const auto accent = look::category (proc.padInfo (p).category);
    layerLevel.slider.getProperties().set ("arc", static_cast<juce::int64> (accent.getARGB()));
    if (! layerLevel.slider.isMouseButtonDown())
        layerLevel.slider.setValue (params.gainDb, juce::dontSendNotification);
    layerLevel.slider.updateText();
    layerLevel.slider.repaint();
    velocity.setRange (params.velocityLow, params.velocityHigh, accent);
    repaint (velocityCaption());
}

void SamplePanel::openAddDialog()
{
    const int pad = proc.selectedPad;
    chooser = std::make_unique<juce::FileChooser> ("Add layers to " + proc.padDisplayName (pad), juce::File(), "*.wav;*.aif;*.aiff;*.flac;*.caf");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::canSelectMultipleItems,
                          [this, pad] (const juce::FileChooser& fc) {
                              const auto files = fc.getResults();
                              if (! files.isEmpty())
                                  offerLayerDrop (proc, pad, files, &addLayers, LayerDrop::roundRobin);
                          });
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
    m.addItem ("Add layers...", proc.padLayers (p).layers.size() < static_cast<std::size_t> (drums::maxLayers), false, [this] { openAddDialog(); });
    m.addSubMenu ("Duplicate to", duplicateMenu (proc, p, onSelect));
    m.addSeparator();
    m.addItem ("Clear pad", ! proc.padLayers (p).layers.empty(), false, [this, p] { proc.clearPad (p); });
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

    // The selected layer, after REVERSE
    layerLevel.setBounds ({ 352, 262, 100, 80 });
    velocity.setBounds (480, 300, 300, 26);

    // Layers column
    modeRobin.setBounds (980, 80, 76, 26);
    modeVelocity.setBounds (1058, 80, 98, 26);
    modeStack.setBounds (1158, 80, 82, 26);
    layers.setBounds (978, 114, 262, 6 + 5 * static_cast<int> (LayerList::rowHeight));
    order.setBounds (980, 258, 112, 28);
    addLayers.setBounds (1098, 256, 142, 32);

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
    {
        // The layer shown below (with several, which one of how many).
        const auto& list = proc.padLayers (p).layers;
        const int l = juce::jlimit (0, std::max (0, static_cast<int> (list.size()) - 1), proc.selectedLayer[static_cast<std::size_t> (p)]);
        if (! list.empty())
            g.drawFittedText (juce::String::fromUTF8 (list[static_cast<std::size_t> (l)].fileName().c_str()), juce::Rectangle<int> (getWidth() - 360, 12, 300, 30),
                              juce::Justification::centredRight, 1, 1.0f);
    }

    // Layers column
    g.setColour (juce::Colour (0xffded4c5));
    g.fillRect (968, 60, 1, getHeight() - 76);
    g.fillRect (1252, 60, 1, getHeight() - 76);
    smallLabel (g, { 982, 54, 160, 20 }, "SAMPLE LAYERS");
    const auto& padLayers = proc.padLayers (p);
    if (! padLayers.layers.empty())
        smallLabel (g, { 1140, 54, 100, 20 }, juce::String (padLayers.layers.size()) + " / " + juce::String (drums::maxLayers), juce::Justification::centredRight);

    // The selected layer's velocity range
    if (velocity.isVisible())
    {
        const int l = proc.selectedLayer[static_cast<std::size_t> (p)];
        smallLabel (g, velocityCaption(), "LAYER " + juce::String (l + 1) + " VELOCITY");
        g.setColour (cc::text);
        g.setFont (plugin::fonts::make (13.0f, look::Weight::semibold));
        g.drawText (juce::String (velocity.low()) + juce::String (juce::CharPointer_UTF8 ("\xe2\x80\x93")) + juce::String (velocity.high()), velocityCaption(), juce::Justification::centredRight);
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

juce::Rectangle<float> KeyboardRangeStrip::keys() const { return { 18.0f, 70.0f, static_cast<float> (getWidth()) - 36.0f, 56.0f }; }

namespace
{
    // A real piano (as the mockup): 75 equal white keys for MIDI 0..127, black keys
    // narrower and shorter, centred on the line between their two white neighbours.
    constexpr int whiteKeys = 75;
    constexpr float blackWidth = 0.58f;    // of a white key
    constexpr float blackHeight = 0.62f;   // of the key height

    int whiteIndex (int note) noexcept
    {
        static const int inOctave[] = { 0, 0, 1, 1, 2, 3, 3, 4, 4, 5, 5, 6 };
        return note / 12 * 7 + inOctave[note % 12];
    }

    /** The zone-tab colours sampled from the mockup, by category: strong, but still warm. */
    juce::Colour zoneColour (drums::DrumCategory c)
    {
        switch (c)
        {
            case drums::DrumCategory::kick: return juce::Colour (0xffe8895b);
            case drums::DrumCategory::snare: return juce::Colour (0xff819ec6);
            case drums::DrumCategory::closedHat: return juce::Colour (0xffa2bca1);
            case drums::DrumCategory::openHat: return juce::Colour (0xff91ad96);
            case drums::DrumCategory::clap: return juce::Colour (0xffb8a996);
            case drums::DrumCategory::tom: return juce::Colour (0xff9d87b6);
            case drums::DrumCategory::rim: return juce::Colour (0xffafafad);
            case drums::DrumCategory::percussion: return juce::Colour (0xffe5b779);
            case drums::DrumCategory::cymbal: return juce::Colour (0xff86b4b0);
            case drums::DrumCategory::unknown: break;
        }
        return juce::Colour (0xffafafad);
    }
} // namespace

juce::Rectangle<float> KeyboardRangeStrip::keyRect (int note) const
{
    const auto k = keys();
    const float w = k.getWidth() / static_cast<float> (whiteKeys);
    if (! look::isBlackKey (note))
        return { k.getX() + w * static_cast<float> (whiteIndex (note)), k.getY(), w, k.getHeight() };
    const float boundary = k.getX() + w * static_cast<float> (whiteIndex (note) + 1);
    return { boundary - w * blackWidth * 0.5f, k.getY(), w * blackWidth, k.getHeight() * blackHeight };
}

float KeyboardRangeStrip::noteX (double note) const
{
    return keyRect (juce::jlimit (0, 127, static_cast<int> (note))).getX();
}

int KeyboardRangeStrip::noteAt (juce::Point<float> p) const
{
    // Black keys sit on top: test them first.
    for (int n = 0; n < 128; ++n)
        if (look::isBlackKey (n) && keyRect (n).contains (p))
            return n;
    for (int n = 0; n < 128; ++n)
        if (! look::isBlackKey (n) && keyRect (n).withY (keys().getY() - 26.0f).withHeight (keys().getHeight() + 26.0f).contains (p))
            return n;
    return -1;
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
    // In the title row, so the pad numbers above the zones are never covered.
    root.setBounds (getWidth() - 468, 9, 150, 28);
    presets.setBounds (getWidth() - 214, 9, 156, 28);
    open.setBounds (getWidth() - 50, 8, 34, 30);
}

void KeyboardRangeStrip::mouseDown (const juce::MouseEvent& e)
{
    // A click on a zone (its tab or its keys) or a PAD note selects that pad.
    const int note = noteAt (e.position);
    if (note < 0)
        return;
    std::array<drums::Hit, drums::maxPads> hits {};
    if (drums::resolve (proc.keyMap(), proc.keyboardMode(), note, hits) > 0 && onSelect)
        onSelect (hits[0].pad);
}

void KeyboardRangeStrip::paint (juce::Graphics& g)
{
    ceramic::card (g, getLocalBounds().toFloat(), 12.0f);
    sectionTitle (g, { 18, 9, 180, 28 }, "KEYBOARD RANGE");
    smallLabel (g, { getWidth() - 552, 9, 80, 28 }, "ROOT NOTE", juce::Justification::centredRight);
    smallLabel (g, { getWidth() - 312, 9, 94, 28 }, "ZONE PRESETS", juce::Justification::centredRight);

    const auto k = keys();
    const auto& map = proc.keyMap();
    const auto mode = proc.keyboardMode();

    // Which pad each key plays (the first hit; -1 = none). Everything below is drawn per
    // key, inside that key's own shape, so a colour never reaches a neighbour.
    std::array<int, 128> owner {};
    std::array<drums::Hit, drums::maxPads> hits {};
    for (int n = 0; n < 128; ++n)
        owner[static_cast<std::size_t> (n)] = drums::resolve (map, mode, n, hits) > 0 ? hits[0].pad : -1;
    const auto ownerOf = [&owner] (int n) { return n < 0 || n > 127 ? -1 : owner[static_cast<std::size_t> (n)]; };
    const auto colourOf = [this] (int pad) { return zoneColour (proc.padInfo (pad).category); };

    // The keybed: ivory white keys; a mapped white key takes its pad's colour on its face.
    // (The black keys are drawn afterwards, on top, so the tint shows only where the white
    // key is visible.)
    ceramic::recess (g, k.expanded (3.0f), 4.0f);
    for (int n = 0; n < 128; ++n)
        if (! look::isBlackKey (n))
        {
            const auto r = keyRect (n);
            g.setGradientFill (juce::ColourGradient (juce::Colour (0xfff6f3ec), r.getX(), r.getY(), juce::Colour (0xffe9e5dc), r.getX(), r.getBottom(), false));
            g.fillRect (r);
            if (const int pad = ownerOf (n); pad >= 0)
            {
                const auto c = colourOf (pad);
                g.setGradientFill (juce::ColourGradient (c.withAlpha (0.78f), r.getX(), r.getY(), c.withAlpha (0.58f), r.getX(), r.getBottom(), false));
                g.fillRect (r.withTrimmedRight (0.8f));
            }
            g.setColour (juce::Colour (0xffcdc6ba));
            g.fillRect (juce::Rectangle<float> (r.getRight() - 0.5f, r.getY(), 0.8f, r.getHeight()));
        }

    // Black keys on top: dark, or (mapped) their pad's colour inside a dark rim.
    for (int n = 0; n < 128; ++n)
        if (look::isBlackKey (n))
        {
            const auto r = keyRect (n);
            g.setColour (juce::Colours::black.withAlpha (0.18f));
            g.fillRoundedRectangle (r.translated (0.6f, 0.8f), 1.5f);
            g.setGradientFill (juce::ColourGradient (juce::Colour (0xff4a4a4c), r.getX(), r.getY(), juce::Colour (0xff232324), r.getX(), r.getBottom(), false));
            g.fillRoundedRectangle (r, 1.5f);
            if (const int pad = ownerOf (n); pad >= 0)
            {
                const auto c = colourOf (pad);
                const auto face = r.reduced (1.2f, 0.0f).withTrimmedBottom (1.6f);
                g.setGradientFill (juce::ColourGradient (c.darker (0.3f), face.getX(), face.getY(), c.darker (0.8f), face.getX(), face.getBottom(), false));
                g.fillRoundedRectangle (face, 1.0f);
            }
            else
            {
                g.setColour (juce::Colours::white.withAlpha (0.12f));
                g.fillRect (r.reduced (r.getWidth() * 0.22f, 0.0f).withHeight (r.getHeight() - 4.0f).withTrimmedTop (2.0f).withWidth (1.0f));
            }
        }

    // Held notes.
    for (int n = 0; n < 128; ++n)
        if (held[static_cast<std::size_t> (n)])
        {
            const auto r = keyRect (n);
            const bool black = look::isBlackKey (n);
            g.setColour (cc::accent.withAlpha (black ? 0.85f : 0.6f));
            black ? g.fillRoundedRectangle (r, 1.5f) : g.fillRect (r.withTrimmedTop (r.getHeight() * blackHeight));
        }

    // Caps: a slim bar above each run of keys one pad owns. A white key's cap gives up the
    // half of a neighbouring black key that belongs to another pad, so caps never overlap.
    const auto capSpan = [&] (int n) -> juce::Range<float>
    {
        const auto r = keyRect (n);
        float x0 = r.getX(), x1 = r.getRight();
        if (! look::isBlackKey (n))
        {
            const int pad = ownerOf (n);
            if (n > 0 && look::isBlackKey (n - 1) && ownerOf (n - 1) >= 0 && ownerOf (n - 1) != pad)
                x0 = keyRect (n - 1).getRight();
            if (n < 127 && look::isBlackKey (n + 1) && ownerOf (n + 1) >= 0 && ownerOf (n + 1) != pad)
                x1 = keyRect (n + 1).getX();
        }
        return { x0, x1 };
    };
    struct Run
    {
        int pad;
        float x0, x1;
    };
    std::vector<Run> runs;
    for (int n = 0; n < 128;)
    {
        const int pad = ownerOf (n);
        int end = n;
        while (end + 1 < 128 && ownerOf (end + 1) == pad)
            ++end;
        if (pad >= 0)
            runs.push_back ({ pad, capSpan (n).getStart(), capSpan (end).getEnd() });
        n = end + 1;
    }

    const float capHeight = 6.0f, capGap = 3.0f;
    const float capY = k.getY() - capGap - capHeight;
    for (const auto& run : runs)
    {
        const auto c = colourOf (run.pad);
        const bool selected = proc.selectedPad == run.pad;
        const auto cap = juce::Rectangle<float> (run.x0 + 0.6f, capY, run.x1 - run.x0 - 1.2f, capHeight);
        g.setColour (c);
        g.fillRoundedRectangle (cap, 2.0f);
        g.setColour (c.darker (selected ? 0.5f : 0.25f).withAlpha (selected ? 0.95f : 0.55f));
        g.drawRoundedRectangle (cap, 2.0f, selected ? 1.4f : 0.8f);
    }

    // Pad numbers above their caps (once per pad, over its widest run); the selected
    // pad's in the accent colour.
    std::array<const Run*, drums::maxPads> widest {};
    for (const auto& run : runs)
    {
        auto& w = widest[static_cast<std::size_t> (run.pad)];
        if (w == nullptr || run.x1 - run.x0 > w->x1 - w->x0)
            w = &run;
    }
    g.setFont (plugin::fonts::make (12.5f, look::Weight::semibold));
    for (int p = 0; p < drums::maxPads; ++p)
        if (const auto* run = widest[static_cast<std::size_t> (p)])
        {
            const float centre = (run->x0 + run->x1) * 0.5f;
            g.setColour (proc.selectedPad == p ? cc::accent : juce::Colour (0xff55524c));
            g.drawText (juce::String (p + 1), juce::Rectangle<float> (centre - 15.0f, capY - 17.0f, 30.0f, 16.0f), juce::Justification::centred);
        }

    // Octave names under the C keys.
    g.setColour (cc::textSecondary);
    g.setFont (type::micro (11.5f));
    for (int n = 0; n < 128; n += 12)
    {
        const auto r = keyRect (n);
        g.drawText (look::noteName (n), juce::Rectangle<float> (r.getX() - 4.0f, k.getBottom() + 4.0f, 40.0f, 14.0f), juce::Justification::centredLeft);
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

DrumsContent::DrumsContent (DrumsProcessor& p) : proc (p), grid (p), sample (p), range (p), envelope (p), keyMap (p), breaks (p), imports (p)
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
    breakButton.setTooltip ("Slice a break into a kit: click to choose a file, or drop one here");
    breakButton.onClick = [this] { openBreakDialog(); };
    breakButton.onFile = [this] (const juce::File& f) { proc.importBreak (f); };
    breakButton.onFiles = [this] (const juce::Array<juce::File>& files) { proc.importFiles (files); };
    addChildComponent (imports);
    imports.onClose = [this] { syncBreakView(); };
    imports.onReport = [this] (const juce::String& title, const juce::String& message) { tell (title, message); };
    addAndMakeVisible (breakButton);
    addChildComponent (breaks);
    breaks.onClose = [this] { syncBreakView(); };
    breaks.onReport = [this] (const juce::String& title, const juce::String& message) { tell (title, message); };
    kitMenu.icon = [] (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c) { ceramic::iconMenu (g, r, c); };
    kitMenu.setTooltip ("Kits: new, open, save, export a portable kit, find missing samples");
    kitMenu.onClick = [this] { showKitMenu(); };
    for (auto* c : std::initializer_list<juce::Component*> { &undoButton, &redoButton, &kitName, &kitMenu, &layout1x8, &layout2x4, &layout3x3, &volume, &meter })
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
    if (open && (breaks.isVisible() || imports.isVisible()))
        return;   // finish the break or import first
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
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::canSelectMultipleItems,
                          [this, pad] (const juce::FileChooser& fc) {
                              // Load replaces the pad (several files: round robins); Add Layers... adds.
                              if (! fc.getResults().isEmpty())
                                  proc.addPadLayers (pad, fc.getResults(), LayerDrop::replace);
                          });
}

void DrumsContent::tell (const juce::String& title, const juce::String& message)
{
    if (getPeer() != nullptr)
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, title, message);
}

void DrumsContent::saveKitAs()
{
    const auto start = proc.kitFile != juce::File() ? proc.kitFile
                                                     : juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                                                           .getChildFile (juce::File::createLegalFileName (juce::String::fromUTF8 (proc.kitName.c_str())) + DrumsProcessor::kitExtension);
    chooser = std::make_unique<juce::FileChooser> ("Save kit", start, juce::String ("*") + DrumsProcessor::kitExtension);
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting,
                          [this] (const juce::FileChooser& fc) {
                              if (fc.getResult() == juce::File())
                                  return;
                              juce::String error;
                              if (! proc.saveKit (fc.getResult(), error))
                                  tell ("Save kit", error);
                          });
}

void DrumsContent::showKitMenu()
{
    juce::PopupMenu m;
    m.addItem ("New kit", [this] { proc.newKit(); });
    m.addItem ("Open kit...", [this] {
        chooser = std::make_unique<juce::FileChooser> ("Open kit", proc.kitFile.getParentDirectory(), juce::String ("*") + DrumsProcessor::kitExtension);
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this] (const juce::FileChooser& fc) {
            if (! fc.getResult().existsAsFile())
                return;
            juce::String error;
            if (! proc.loadKit (fc.getResult(), error))
                tell ("Open kit", error);
        });
    });
    m.addSeparator();
    m.addItem ("Save kit", [this] {
        juce::String error;
        if (proc.kitFile == juce::File())
            saveKitAs();
        else if (! proc.saveKit (proc.kitFile, error))
            tell ("Save kit", error);
    });
    m.addItem ("Save kit as...", [this] { saveKitAs(); });
    m.addItem ("Export portable kit...", [this] {
        chooser = std::make_unique<juce::FileChooser> ("Export a portable kit into a folder", juce::File::getSpecialLocation (juce::File::userDocumentsDirectory));
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories, [this] (const juce::FileChooser& fc) {
            if (! fc.getResult().isDirectory())
                return;
            juce::String error;
            const auto kit = proc.exportPortableKit (fc.getResult(), error);
            tell ("Export portable kit", kit == juce::File() ? error
                                                             : "Saved " + kit.getParentDirectory().getFullPathName() + " with its samples." + (error.isEmpty() ? "" : "\n" + error));
        });
    });
    m.addSeparator();
    m.addItem ("Import samples...", [this] {
        chooser = std::make_unique<juce::FileChooser> ("Import samples into the kit", juce::File(), "*.wav;*.aif;*.aiff;*.flac;*.caf");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::canSelectMultipleItems,
                              [this] (const juce::FileChooser& fc) {
                                  if (! fc.getResults().isEmpty())
                                      proc.importFiles (fc.getResults());
                              });
    });
    m.addItem ("Slice a break...", [this] { openBreakDialog(); });
    m.addItem ("Save groove as MIDI...", ! proc.groove.hits.empty(), false, [this] {
        chooser = std::make_unique<juce::FileChooser> ("Save the groove as MIDI", juce::File::getSpecialLocation (juce::File::userDocumentsDirectory), "*.mid");
        chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting,
                              [this] (const juce::FileChooser& fc) {
                                  juce::String error;
                                  if (fc.getResult() != juce::File() && ! proc.writeGrooveMidi (fc.getResult().withFileExtension ("mid"), error))
                                      tell ("Save groove", error);
                              });
    });
    m.addSeparator();
    const int missing = proc.missingLayerCount();
    m.addItem (missing > 0 ? "Find missing samples (" + juce::String (missing) + ")..." : juce::String ("Find missing samples..."), missing > 0, false, [this] {
        chooser = std::make_unique<juce::FileChooser> ("Find missing samples in a folder", juce::File::getSpecialLocation (juce::File::userHomeDirectory));
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories, [this] (const juce::FileChooser& fc) {
            if (fc.getResult().isDirectory())
                proc.findMissingSamples (fc.getResult());
        });
    });
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&kitMenu));
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

void DrumsContent::openBreakDialog()
{
    chooser = std::make_unique<juce::FileChooser> ("Slice a break", juce::File(), "*.wav;*.aif;*.aiff;*.flac;*.caf");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this] (const juce::FileChooser& fc) {
        if (fc.getResult().existsAsFile())
            proc.importBreak (fc.getResult());
    });
}

void DrumsContent::syncBreakView()
{
    // The break and import plans open over the pad editor (one at a time).
    const bool breakOpen = proc.breakSession().open();
    const bool importOpen = ! breakOpen && proc.importSession().open();
    if ((breakOpen || importOpen) && keyMap.isVisible())
        setKeyMapOpen (false);
    if (breaks.isVisible() != breakOpen)
    {
        breaks.setVisible (breakOpen);
        if (breakOpen)
        {
            breaks.selection.clear();
            breaks.grabKeyboardFocus();
        }
    }
    if (imports.isVisible() != importOpen)
    {
        imports.setVisible (importOpen);
        if (importOpen)
            imports.selection.clear();
    }
    const bool overlay = breakOpen || importOpen || keyMap.isVisible();
    sample.setVisible (! overlay);
    range.setVisible (! overlay);
    breakButton.setToggleState (breakOpen || importOpen, juce::dontSendNotification);
    if (breakOpen)
        breaks.refresh();
    if (importOpen)
        imports.refresh();
}

void DrumsContent::changeListenerCallback (juce::ChangeBroadcaster*)
{
    syncBreakView();
    if (proc.relinkReport != shownRelinkReport)
    {
        shownRelinkReport = proc.relinkReport;
        if (shownRelinkReport.startsWith ("Found"))
            tell ("Find missing samples", shownRelinkReport);
    }
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
    kitName.setBounds (532, 28, 316, 36);
    kitMenu.setBounds (852, 28, 40, 36);
    breakButton.setBounds (318, 28, 104, 36);
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
    breaks.setBounds (editorCard().getUnion (rangeCard()));
    imports.setBounds (editorCard().getUnion (rangeCard()));

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
