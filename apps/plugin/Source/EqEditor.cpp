#include "EqEditor.h"

#include "OspLookAndFeel.h"

#include <cmath>

namespace osp::plugin
{

namespace
{
    constexpr double minHz = 20.0, maxHz = 20000.0, rangeDb = 18.0;

    mod::Dest layerDest (mod::Dest first, int layer)
    {
        return static_cast<mod::Dest> (static_cast<int> (first) + std::clamp (layer, 0, 2));
    }

    juce::String hzText (double hz)
    {
        return hz >= 1000.0 ? juce::String (hz / 1000.0, hz >= 10000.0 ? 1 : 2) + " kHz" : juce::String (juce::roundToInt (hz)) + " Hz";
    }

    juce::String dbText (double db)
    {
        return (db > 0.04 ? "+" : "") + juce::String (db, 1) + " dB";
    }
}

juce::Colour equi::bandColour (eq::Band band)
{
    static const std::array<juce::Colour, 5> colours { juce::Colour (0xff7f97b2), juce::Colour (0xffc9a052), juce::Colour (0xff8fae8a),
                                                       juce::Colour (0xffc07a5c), juce::Colour (0xff9a8bb0) };
    return colours[static_cast<std::size_t> (std::clamp (static_cast<int> (band), 0, 4))];
}

//==============================================================================
EqButton::EqButton (int l) : juce::Button ("EQ"), layer (l)
{
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
    setTooltip ("This layer's EQ (it opens over the waveform)");
    setTitle ("EQ");
}

void EqButton::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    using namespace design;
    const auto r = getLocalBounds().toFloat().reduced (1.0f);
    const bool open = getToggleState();
    if (open || down)
    {
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xffe2d9cc), 0.0f, r.getY(), juce::Colour (0xffebe4d9), 0.0f, r.getBottom(), false));
        g.fillRoundedRectangle (r, 7.0f);
        g.setColour (colour::hairline.darker (0.1f));
        g.drawRoundedRectangle (r.reduced (0.5f), 7.0f, 1.0f);
    }
    else
        draw::button (g, r, 7.0f, false, highlighted, colour::accent);
    // The light: the layer's EQ is heard.
    const bool narrow = r.getWidth() < 42.0f;
    if (active)
        draw::led (g, { r.getX() + (narrow ? 7.0f : 9.0f), r.getCentreY() }, narrow ? 5.0f : 6.0f, colour::identity (layer).led, 0.6f);
    g.setFont (type::button (narrow ? 12.5f : 13.5f));
    g.setColour (colour::text.withAlpha (open || active ? 0.92f : 0.62f));
    g.drawText ("EQ", r.withTrimmedLeft (active ? (narrow ? 9.0f : 12.0f) : 0.0f), juce::Justification::centred, false);
}

//==============================================================================
EqValueField::EqValueField (OspAudioProcessor& processor, const juce::String& id)
    : juce::Slider (juce::Slider::RotaryVerticalDrag, juce::Slider::NoTextBox)
{
    parameter = processor.parameters.getParameter (id);
    getProperties().set ("paramId", id);   // a modulation drop target
    getProperties().set ("modField", true);
    setMouseDragSensitivity (220);
    setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
    attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (processor.parameters, id, *this);
    if (parameter != nullptr)
        setTooltip (parameter->getName (64) + ": drag up or down, double-click to type");
    onValueChange = [this] { repaint(); };
}

void EqValueField::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (juce::Colour (0xff2b3031));
    g.fillRoundedRectangle (r, 5.0f);
    g.setColour (isMouseOverOrDragging() ? accent.withAlpha (0.8f) : juce::Colour (0xff3e4446));
    g.drawRoundedRectangle (r, 5.0f, 1.0f);
    if (editor != nullptr)
        return;
    const auto text = parameter != nullptr ? parameter->getCurrentValueAsText() : juce::String();
    g.setFont (type::popupValue (12.5f));
    g.setColour (juce::Colour (0xffeee8dd));
    g.drawText (text, r, juce::Justification::centred, false);
}

void EqValueField::mouseDoubleClick (const juce::MouseEvent&)
{
    beginTyping();
}

void EqValueField::beginTyping()
{
    editor = std::make_unique<juce::TextEditor>();
    editor->setJustification (juce::Justification::centred);
    editor->setFont (type::popupValue (12.5f));
    editor->setColour (juce::TextEditor::backgroundColourId, juce::Colour (0xff1c2122));
    editor->setColour (juce::TextEditor::textColourId, juce::Colour (0xffeee8dd));
    editor->setColour (juce::TextEditor::outlineColourId, accent);
    editor->setColour (juce::TextEditor::focusedOutlineColourId, accent);
    editor->setText (parameter != nullptr ? parameter->getCurrentValueAsText() : juce::String(), false);
    editor->selectAll();
    juce::Component::SafePointer<EqValueField> safe (this);
    editor->onReturnKey = [safe] { if (safe != nullptr && safe->editor != nullptr) safe->commitTyping (safe->editor->getText()); };
    editor->onEscapeKey = [safe] { if (safe != nullptr) juce::MessageManager::callAsync ([safe] { if (safe != nullptr) { safe->editor.reset(); safe->repaint(); } }); };
    editor->onFocusLost = editor->onReturnKey;
    addAndMakeVisible (*editor);
    editor->setBounds (getLocalBounds());
    if (isShowing())
        editor->grabKeyboardFocus();
    repaint();
}

void EqValueField::commitTyping (const juce::String& text)
{
    // "1.2k", "1200", "1.2 kHz", "-3.5", "2" (Q): the number, times 1000 with a k.
    const auto t = text.trim().toLowerCase();
    if (t.containsAnyOf ("0123456789"))
    {
        double value = t.retainCharacters ("0123456789.-+").getDoubleValue();
        if (t.contains ("k"))
            value *= 1000.0;
        setValue (value, juce::sendNotificationSync);
    }
    juce::Component::SafePointer<EqValueField> safe (this);
    juce::MessageManager::callAsync ([safe] { if (safe != nullptr) { safe->editor.reset(); safe->repaint(); } });
}

//==============================================================================
EqEditor::EqEditor (OspAudioProcessor& p, int l) : ospProcessor (p), layer (l)
{
    setTitle ("EQ " + OspAudioProcessor::layerName (layer));
    shown = ospProcessor.layerEqSettings (layer);
    setRepaintsOnMouseActivity (false);
}

double EqEditor::sampleRate() const
{
    const double rate = ospProcessor.getSampleRate();
    return rate > 0.0 ? rate : 48000.0;
}

juce::RangedAudioParameter* EqEditor::bandParameter (int band, const char* name) const
{
    return ospProcessor.parameters.getParameter (
        OspAudioProcessor::eqParameterId (layer, juce::String (OspAudioProcessor::eqBandKey (static_cast<eq::Band> (band))) + "." + name));
}

void EqEditor::setValue (juce::RangedAudioParameter* p, float value)
{
    if (p == nullptr)
        return;
    p->beginChangeGesture();
    p->setValueNotifyingHost (p->convertTo0to1 (value));
    p->endChangeGesture();
}

void EqEditor::switchBand (int band, bool on)
{
    ospProcessor.undoManager.beginNewTransaction (on ? "EQ band on" : "EQ band off");
    // The first band switched on also switches the EQ on (what else would the musician
    // mean); once anything is on, the EQ's own switch is theirs alone.
    if (on && ! shown.enabled)
    {
        bool anyOn = false;
        for (const auto& b : shown.bands)
            anyOn = anyOn || b.enabled;
        if (! anyOn)
            setValue (ospProcessor.parameters.getParameter (OspAudioProcessor::eqParameterId (layer, "enabled")), 1.0f);
    }
    setValue (bandParameter (band, "enabled"), on ? 1.0f : 0.0f);
    shown = ospProcessor.layerEqSettings (layer);
    repaint();
}

juce::Rectangle<float> EqEditor::graphArea() const
{
    return getLocalBounds().toFloat().withTrimmedLeft (38.0f).withTrimmedRight (14.0f).withTrimmedTop (44.0f).withTrimmedBottom (24.0f);
}

juce::Rectangle<float> EqEditor::powerArea() const
{
    return { 12.0f, 10.0f, 22.0f, 22.0f };
}

float EqEditor::xForHz (double hz) const
{
    const auto g = graphArea();
    const double t = std::log (std::clamp (hz, minHz, maxHz) / minHz) / std::log (maxHz / minHz);
    return g.getX() + static_cast<float> (t) * g.getWidth();
}

double EqEditor::hzForX (float x) const
{
    const auto g = graphArea();
    const double t = std::clamp (static_cast<double> ((x - g.getX()) / g.getWidth()), 0.0, 1.0);
    return minHz * std::pow (maxHz / minHz, t);
}

float EqEditor::yForDb (double db) const
{
    const auto g = graphArea().reduced (0.0f, 4.0f);
    return g.getCentreY() - static_cast<float> (std::clamp (db, -rangeDb - 6.0, rangeDb + 6.0) / rangeDb) * 0.5f * g.getHeight();
}

juce::Point<float> EqEditor::nodePosition (int band) const
{
    const auto b = static_cast<eq::Band> (band);
    const auto& s = shown.bands[static_cast<std::size_t> (band)];
    const float x = xForHz (s.frequencyHz);
    if (! s.enabled)
        return { x, yForDb (0.0) };
    if (eq::hasGain (b))
        return { x, yForDb (s.gainDb) };
    return { x, yForDb (eq::bandResponseDb (b, s, s.frequencyHz, sampleRate())) };   // on its own curve, at the cutoff
}

int EqEditor::bandAt (juce::Point<float> p) const
{
    int best = -1;
    float bestDistance = 11.0f;
    // Bands that are on first (on top), then the quiet markers.
    for (int pass = 0; pass < 2 && best < 0; ++pass)
        for (int b = 0; b < eq::bandCount; ++b)
        {
            if (shown.bands[static_cast<std::size_t> (b)].enabled != (pass == 0))
                continue;
            const float d = nodePosition (b).getDistanceFrom (p);
            if (d < bestDistance)
            {
                bestDistance = d;
                best = b;
            }
        }
    return best;
}

std::array<double, 4> EqEditor::modulationOffsets() const
{
    // The global LFOs' routes to this layer's EQ, now (as the engine adds them).
    std::array<double, 4> o {};
    const auto view = ospProcessor.modulationView();
    const std::array<mod::Dest, 4> dests { layerDest (mod::Dest::eqBellFrequencyA, layer), layerDest (mod::Dest::eqBellGainA, layer),
                                           layerDest (mod::Dest::eqLowShelfGainA, layer), layerDest (mod::Dest::eqHighShelfGainA, layer) };
    for (const auto& r : ospProcessor.modulationRoutes())
        if (r.state == mod::RouteState::active)
            for (std::size_t i = 0; i < dests.size(); ++i)
                if (r.route.dest == dests[i])
                    o[i] += mod::destInfo (dests[i]).span * r.route.depth * view.value[static_cast<std::size_t> (mod::sourceIndex (r.route.source))];
    return o;
}

eq::Settings EqEditor::effective() const
{
    auto s = shown;
    const auto& o = shownModulation;
    auto& bell = s.bands[static_cast<std::size_t> (eq::Band::bell)];
    bell.frequencyHz = std::clamp (bell.frequencyHz * std::exp2 (o[0]), 20.0, 20000.0);
    bell.gainDb = std::clamp (bell.gainDb + o[1], -eq::maxGainDb, eq::maxGainDb);
    auto& low = s.bands[static_cast<std::size_t> (eq::Band::lowShelf)];
    low.gainDb = std::clamp (low.gainDb + o[2], -eq::maxGainDb, eq::maxGainDb);
    auto& high = s.bands[static_cast<std::size_t> (eq::Band::highShelf)];
    high.gainDb = std::clamp (high.gainDb + o[3], -eq::maxGainDb, eq::maxGainDb);
    return s;
}

void EqEditor::refresh()
{
    const auto now = ospProcessor.layerEqSettings (layer);
    const auto offsets = modulationOffsets();
    bool moved = false;
    for (std::size_t i = 0; i < offsets.size(); ++i)
        moved = moved || std::abs (offsets[i] - shownModulation[i]) > 1.0e-4;
    if (! (now == shown) || moved)
    {
        shown = now;
        shownModulation = offsets;
        repaint();
    }
}

void EqEditor::selectBand (int band)
{
    if (band == selected)
        return;
    selected = band;
    rebuildInspector();
    repaint();
}

void EqEditor::rebuildInspector()
{
    frequencyField.reset();
    gainField.reset();
    qField.reset();
    if (selected >= 0)
    {
        const auto band = static_cast<eq::Band> (selected);
        const juce::String key = OspAudioProcessor::eqBandKey (band);
        auto make = [&] (const char* name) {
            auto f = std::make_unique<EqValueField> (ospProcessor, OspAudioProcessor::eqParameterId (layer, key + "." + name));
            f->setColour (equi::bandColour (band));
            addAndMakeVisible (*f);
            return f;
        };
        frequencyField = make ("frequency");
        if (eq::hasGain (band))
        {
            gainField = make ("gain");
            qField = make ("q");
        }
    }
    resized();
}

void EqEditor::resized()
{
    // The top row: the EQ's switch and name at the left, the selected band's values at the
    // right, then close.
    const float w = static_cast<float> (getWidth());
    titleArea = { 40.0f, 8.0f, 70.0f, 26.0f };
    closeArea = { w - 32.0f, 10.0f, 22.0f, 22.0f };
    float x = closeArea.getX() - 10.0f;
    auto take = [&x] (float width) {
        x -= width;
        const juce::Rectangle<float> r (x, 9.0f, width, 24.0f);
        x -= 6.0f;
        return r;
    };
    resetArea = take (24.0f);
    bandSwitchArea = take (24.0f);
    slopeArea = {};
    juce::Rectangle<float> q, gain, frequency;
    if (selected >= 0)
    {
        if (eq::hasQ (static_cast<eq::Band> (selected)))
        {
            q = take (52.0f);
            gain = take (64.0f);
        }
        else
            slopeArea = take (70.0f);
        frequency = take (74.0f);
    }
    nameArea = x - titleArea.getRight() > 90.0f ? juce::Rectangle<float> (x - 90.0f, 9.0f, 90.0f, 24.0f) : juce::Rectangle<float>();
    auto place = [] (std::unique_ptr<EqValueField>& f, juce::Rectangle<float> r) {
        if (f != nullptr)
            f->setBounds (r.getSmallestIntegerContainer());
    };
    place (frequencyField, frequency);
    place (gainField, gain);
    place (qField, q);
}

void EqEditor::paint (juce::Graphics& g)
{
    using namespace design;
    const auto bounds = getLocalBounds().toFloat();
    // Over the waveform: the same graphite, the recording still faintly there.
    g.setColour (colour::wellA.withAlpha (0.86f));
    g.fillRoundedRectangle (bounds, 10.0f);
    const auto graph = graphArea();
    const bool heard = shown.enabled;

    // Grid: frequencies (decades stronger) and gain (0 dB stronger), with quiet labels.
    g.setFont (type::popupLabel (10.5f));
    for (double hz : { 20.0, 30.0, 40.0, 50.0, 60.0, 70.0, 80.0, 90.0, 100.0, 200.0, 300.0, 400.0, 500.0, 600.0, 700.0, 800.0, 900.0, 1000.0, 2000.0,
                       3000.0, 4000.0, 5000.0, 6000.0, 7000.0, 8000.0, 9000.0, 10000.0, 20000.0 })
    {
        const float x = xForHz (hz);
        const int whole = juce::roundToInt (hz);
        const bool decade = whole == 100 || whole == 1000 || whole == 10000;
        g.setColour (colour::wellGrid.withAlpha (decade ? 1.0f : 0.45f));
        g.fillRect (juce::Rectangle<float> (x - 0.5f, graph.getY(), 1.0f, graph.getHeight()));
    }
    const std::array<std::pair<double, const char*>, 7> labels { { { 20.0, "20" }, { 100.0, "100" }, { 500.0, "500" }, { 1000.0, "1k" },
                                                                  { 5000.0, "5k" }, { 10000.0, "10k" }, { 20000.0, "20k" } } };
    g.setColour (colour::wellText.withAlpha (0.85f));
    float lastRight = -100.0f;
    for (const auto& [hz, text] : labels)
    {
        const float x = xForHz (hz);
        const auto r = juce::Rectangle<float> (x - 18.0f, graph.getBottom() + 4.0f, 36.0f, 14.0f);
        if (r.getX() < lastRight + 2.0f)
            continue;   // never crowded
        const int whole = juce::roundToInt (hz);
        g.drawText (text, r, whole == 20 ? juce::Justification::centredLeft : (whole == 20000 ? juce::Justification::centredRight : juce::Justification::centred), false);
        lastRight = r.getRight();
    }
    for (int db = -18; db <= 18; db += 6)
    {
        const float y = yForDb (db);
        g.setColour (db == 0 ? colour::wellText.withAlpha (0.45f) : colour::wellGrid.withAlpha (0.7f));
        g.fillRect (juce::Rectangle<float> (graph.getX(), y - 0.5f, graph.getWidth(), 1.0f));
        if (db % 12 == 0)
        {
            g.setColour (colour::wellText.withAlpha (0.8f));
            g.drawText (db > 0 ? "+" + juce::String (db) : juce::String (db), juce::Rectangle<float> (4.0f, y - 7.0f, 30.0f, 14.0f), juce::Justification::centredRight, false);
        }
    }

    // The selected band's own shape, faint, in its colour.
    const double rate = sampleRate();
    if (selected >= 0 && shown.bands[static_cast<std::size_t> (selected)].enabled)
    {
        const auto band = static_cast<eq::Band> (selected);
        juce::Path own;
        const auto s = effective().bands[static_cast<std::size_t> (selected)];
        for (int i = 0; i <= 160; ++i)
        {
            const float x = graph.getX() + graph.getWidth() * static_cast<float> (i) / 160.0f;
            const float y = yForDb (eq::bandResponseDb (band, s, hzForX (x), rate));
            i == 0 ? own.startNewSubPath (x, y) : own.lineTo (x, y);
        }
        auto fill = own;
        fill.lineTo (graph.getRight(), yForDb (0.0));
        fill.lineTo (graph.getX(), yForDb (0.0));
        fill.closeSubPath();
        g.setColour (equi::bandColour (band).withAlpha (0.14f));
        g.fillPath (fill);
        g.setColour (equi::bandColour (band).withAlpha (0.5f));
        g.strokePath (own, juce::PathStrokeType (1.0f));
    }

    // The EQ as heard (its switch off: a quiet flat line, the shape it would have dashed).
    {
        const auto heardSettings = effective();
        auto probe = heardSettings;
        probe.enabled = true;
        juce::Path curve;
        for (int i = 0; i <= 240; ++i)
        {
            const float x = graph.getX() + graph.getWidth() * static_cast<float> (i) / 240.0f;
            const float y = yForDb (eq::responseDb (probe, hzForX (x), rate));
            i == 0 ? curve.startNewSubPath (x, y) : curve.lineTo (x, y);
        }
        const auto warm = juce::Colour (0xffeedfc8);
        if (heard)
        {
            auto fill = curve;
            fill.lineTo (graph.getRight(), yForDb (0.0));
            fill.lineTo (graph.getX(), yForDb (0.0));
            fill.closeSubPath();
            g.setColour (warm.withAlpha (0.10f));
            g.fillPath (fill);
            g.setColour (warm);
            g.strokePath (curve, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
        else
        {
            juce::Path dashed;
            const float dashes[] { 4.0f, 4.0f };
            juce::PathStrokeType (1.2f).createDashedStroke (dashed, curve, dashes, 2);
            g.setColour (warm.withAlpha (0.35f));
            g.fillPath (dashed);
        }
    }

    // The bands: quiet markers on 0 dB while off, coloured nodes while on.
    for (int b = 0; b < eq::bandCount; ++b)
    {
        const auto band = static_cast<eq::Band> (b);
        const auto& s = shown.bands[static_cast<std::size_t> (b)];
        const auto c = equi::bandColour (band);
        const auto at = nodePosition (b);
        const bool hot = b == hover || b == dragBand;
        if (! s.enabled)
        {
            const float r = hot ? 5.5f : 4.0f;
            g.setColour (c.withAlpha (hot ? 0.95f : 0.45f));
            g.drawEllipse (juce::Rectangle<float> (2.0f * r, 2.0f * r).withCentre (at), 1.3f);
            continue;
        }
        // Modulated: the range it sweeps (a thin line through the node) and where it is now.
        const auto eff = effective().bands[static_cast<std::size_t> (b)];
        if (std::abs (eff.frequencyHz - s.frequencyHz) > 1.0e-3 || std::abs (eff.gainDb - s.gainDb) > 1.0e-3)
        {
            const auto now = juce::Point<float> (xForHz (eff.frequencyHz), eq::hasGain (band) ? yForDb (eff.gainDb) : at.y);
            g.setColour (c.withAlpha (0.5f));
            g.drawLine ({ at, now }, 1.2f);
            g.setColour (c.withAlpha (0.8f));
            g.drawEllipse (juce::Rectangle<float> (7.0f, 7.0f).withCentre (now), 1.2f);
        }
        const float r = hot || b == selected ? 7.0f : 6.0f;
        if (b == selected)
        {
            g.setColour (c.withAlpha (0.3f));
            g.drawEllipse (juce::Rectangle<float> (2.0f * r + 9.0f, 2.0f * r + 9.0f).withCentre (at), 1.5f);
        }
        g.setColour (colour::wellA);
        g.fillEllipse (juce::Rectangle<float> (2.0f * r + 3.0f, 2.0f * r + 3.0f).withCentre (at));
        g.setColour (heard ? c : c.withMultipliedSaturation (0.5f).withAlpha (0.7f));
        g.fillEllipse (juce::Rectangle<float> (2.0f * r, 2.0f * r).withCentre (at));
    }

    // The pointer's band: what it is (and, off, how to start it).
    if (hover >= 0 && dragBand < 0)
    {
        const auto band = static_cast<eq::Band> (hover);
        const auto& s = shown.bands[static_cast<std::size_t> (hover)];
        juce::String text = eq::bandName (band);
        if (! s.enabled)
            text << "  " << juce::String::fromUTF8 ("\xc2\xb7") << "  drag to use";
        else
            text << "  " << hzText (s.frequencyHz) << (eq::hasGain (band) ? "  " + dbText (s.gainDb) : juce::String());
        g.setFont (type::popupLabel (11.5f));
        const float tw = juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), text) + 14.0f;
        const auto at = nodePosition (hover);
        auto tip = juce::Rectangle<float> (tw, 20.0f).withCentre ({ at.x, at.y - 20.0f });
        tip = tip.withX (juce::jlimit (graph.getX(), graph.getRight() - tw, tip.getX()));
        if (tip.getY() < graph.getY())
            tip = tip.withY (at.y + 12.0f);
        g.setColour (juce::Colour (0xee141819));
        g.fillRoundedRectangle (tip, 5.0f);
        g.setColour (equi::bandColour (band).brighter (0.3f));
        g.drawText (text, tip, juce::Justification::centred, false);
    }

    // The top row: the EQ's switch, its name, the selected band.
    {
        const auto p = powerArea();
        g.setColour (heard ? colour::accent : colour::wellText.withAlpha (0.6f));
        juce::Path icon;
        icon.addCentredArc (p.getCentreX(), p.getCentreY() + 1.0f, 7.0f, 7.0f, 0.0f, 0.65f, 2.0f * juce::MathConstants<float>::pi - 0.65f, true);
        g.strokePath (icon, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.drawLine (p.getCentreX(), p.getY() + 2.0f, p.getCentreX(), p.getCentreY(), 1.6f);
        g.setFont (type::panelHeader());
        g.setColour (juce::Colour (0xffeee8dd).withAlpha (heard ? 0.95f : 0.6f));
        g.drawText ("EQ " + OspAudioProcessor::layerName (layer), titleArea, juce::Justification::centredLeft, false);
        if (! heard)
        {
            g.setFont (type::popupLabel (11.0f));
            g.setColour (colour::wellText);
            g.drawText ("OFF", titleArea.translated (titleArea.getWidth() - 18.0f, 0.0f), juce::Justification::centredLeft, false);
        }
    }
    if (selected >= 0)
    {
        const auto band = static_cast<eq::Band> (selected);
        const auto c = equi::bandColour (band);
        const auto& s = shown.bands[static_cast<std::size_t> (selected)];
        if (! nameArea.isEmpty())
        {
            g.setFont (type::popupLabel (12.0f));
            g.setColour (c.brighter (0.2f));
            g.drawText (eq::bandName (band), nameArea, juce::Justification::centredRight, false);
        }
        if (! slopeArea.isEmpty())
        {
            for (int k = 0; k < 2; ++k)
            {
                const auto cell = slopeArea.withWidth (0.5f * slopeArea.getWidth()).translated (static_cast<float> (k) * 0.5f * slopeArea.getWidth(), 0.0f).reduced (1.0f, 0.0f);
                const bool on = s.steep == (k == 1);
                g.setColour (on ? c.withAlpha (0.35f) : juce::Colour (0xff2b3031));
                g.fillRoundedRectangle (cell, 5.0f);
                g.setColour (on ? c : juce::Colour (0xff3e4446));
                g.drawRoundedRectangle (cell.reduced (0.5f), 5.0f, 1.0f);
                g.setFont (type::popupValue (12.0f));
                g.setColour (juce::Colour (0xffeee8dd).withAlpha (on ? 1.0f : 0.6f));
                g.drawText (k == 0 ? "12" : "24", cell, juce::Justification::centred, false);
            }
        }
        // The band's own switch and its reset.
        const auto sw = bandSwitchArea.reduced (3.0f);
        g.setColour (s.enabled ? c : juce::Colour (0xff3e4446));
        g.drawEllipse (sw.reduced (1.0f), 1.3f);
        if (s.enabled)
            g.fillEllipse (sw.reduced (5.0f));
        const auto re = resetArea.reduced (5.0f);
        juce::Path arrow;
        arrow.addCentredArc (re.getCentreX(), re.getCentreY(), 6.0f, 6.0f, 0.0f, -2.2f, 2.4f, true);
        g.setColour (colour::wellText.brighter (0.3f));
        g.strokePath (arrow, juce::PathStrokeType (1.3f));
        g.fillEllipse (juce::Rectangle<float> (3.0f, 3.0f).withCentre (re.getCentre().getPointOnCircumference (6.0f, -2.2f)));
    }
    // Close.
    {
        const auto x = closeArea.reduced (6.0f);
        g.setColour (colour::wellText.brighter (0.3f));
        g.drawLine (x.getX(), x.getY(), x.getRight(), x.getBottom(), 1.4f);
        g.drawLine (x.getRight(), x.getY(), x.getX(), x.getBottom(), 1.4f);
    }
    if (shown.enabled == false && selected < 0)
    {
        g.setFont (type::popupLabel (12.0f));
        g.setColour (colour::wellText);
        g.drawText ("Drag a marker to shape this sound", graph.withHeight (22.0f).translated (0.0f, 6.0f), juce::Justification::centred, false);
    }
}

void EqEditor::mouseMove (const juce::MouseEvent& e)
{
    const int now = bandAt (e.position);
    if (now != hover)
    {
        hover = now;
        setMouseCursor (now >= 0 ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::NormalCursor);
        repaint();
    }
}

void EqEditor::mouseExit (const juce::MouseEvent&)
{
    if (hover >= 0)
    {
        hover = -1;
        repaint();
    }
}

void EqEditor::mouseDown (const juce::MouseEvent& e)
{
    const auto p = e.position;
    if (closeArea.contains (p))
    {
        if (onClose != nullptr)
            onClose();
        return;
    }
    if (powerArea().expanded (4.0f).contains (p) || titleArea.contains (p))
    {
        ospProcessor.undoManager.beginNewTransaction (shown.enabled ? "EQ off" : "EQ on");
        setValue (ospProcessor.parameters.getParameter (OspAudioProcessor::eqParameterId (layer, "enabled")), shown.enabled ? 0.0f : 1.0f);
        refresh();
        return;
    }
    if (selected >= 0)
    {
        const auto band = static_cast<eq::Band> (selected);
        if (bandSwitchArea.contains (p))
        {
            switchBand (selected, ! shown.bands[static_cast<std::size_t> (selected)].enabled);
            return;
        }
        if (resetArea.contains (p))
        {
            ospProcessor.undoManager.beginNewTransaction ("EQ band reset");
            const auto range = eq::frequencyRange (band);
            setValue (bandParameter (selected, "frequency"), static_cast<float> (range.defaultHz));
            if (eq::hasGain (band))
            {
                setValue (bandParameter (selected, "gain"), 0.0f);
                setValue (bandParameter (selected, "q"), band == eq::Band::bell ? 1.0f : 0.707f);
            }
            else
                setValue (bandParameter (selected, "slope"), 0.0f);
            refresh();
            return;
        }
        if (slopeArea.contains (p))
        {
            ospProcessor.undoManager.beginNewTransaction ("EQ slope");
            setValue (bandParameter (selected, "slope"), p.x > slopeArea.getCentreX() ? 1.0f : 0.0f);
            refresh();
            return;
        }
    }
    const int band = bandAt (p);
    if (band < 0)
        return;
    selectBand (band);
    if (e.mods.isPopupMenu())
    {
        juce::PopupMenu menu;
        juce::Component::SafePointer<EqEditor> safe (this);
        const bool on = shown.bands[static_cast<std::size_t> (band)].enabled;
        menu.addItem (on ? "Turn off" : "Turn on", [safe, band, on] { if (safe != nullptr) safe->switchBand (band, ! on); });
        menu.showMenuAsync (juce::PopupMenu::Options().withDeletionCheck (*this));
        return;
    }
    // Pressing a band that is off starts using it: on, and dragging at once.
    if (! shown.bands[static_cast<std::size_t> (band)].enabled)
        switchBand (band, true);
    ospProcessor.undoManager.beginNewTransaction ("EQ band");
    dragBand = band;
    dragFromX = xForHz (shown.bands[static_cast<std::size_t> (band)].frequencyHz);
    dragFromY = eq::hasGain (static_cast<eq::Band> (band)) ? yForDb (shown.bands[static_cast<std::size_t> (band)].gainDb) : 0.0f;
    if (auto* f = bandParameter (band, "frequency"))
        f->beginChangeGesture();
    if (eq::hasGain (static_cast<eq::Band> (band)))
        if (auto* gp = bandParameter (band, "gain"))
            gp->beginChangeGesture();
}

void EqEditor::mouseDrag (const juce::MouseEvent& e)
{
    if (dragBand < 0)
        return;
    const auto band = static_cast<eq::Band> (dragBand);
    const float fine = e.mods.isShiftDown() ? 0.25f : 1.0f;
    const auto range = eq::frequencyRange (band);
    // Across: frequency (the band's own range); up and down: gain (bell and shelves only).
    const double hz = std::clamp (hzForX (dragFromX + fine * static_cast<float> (e.getDistanceFromDragStartX())), range.minHz, range.maxHz);
    if (auto* f = bandParameter (dragBand, "frequency"))
        f->setValueNotifyingHost (f->convertTo0to1 (static_cast<float> (hz)));
    if (eq::hasGain (band))
        if (auto* gp = bandParameter (dragBand, "gain"))
        {
            const auto g = graphArea().reduced (0.0f, 4.0f);
            const float y = dragFromY + fine * static_cast<float> (e.getDistanceFromDragStartY());
            const double db = std::clamp ((g.getCentreY() - y) / (0.5 * g.getHeight()) * rangeDb, -eq::maxGainDb, eq::maxGainDb);
            gp->setValueNotifyingHost (gp->convertTo0to1 (static_cast<float> (db)));
        }
    refresh();
}

void EqEditor::mouseUp (const juce::MouseEvent&)
{
    if (dragBand >= 0)
    {
        if (auto* f = bandParameter (dragBand, "frequency"))
            f->endChangeGesture();
        if (eq::hasGain (static_cast<eq::Band> (dragBand)))
            if (auto* gp = bandParameter (dragBand, "gain"))
                gp->endChangeGesture();
    }
    dragBand = -1;
    repaint();
}

void EqEditor::mouseDoubleClick (const juce::MouseEvent& e)
{
    // A band's principal value back to its default: gain to 0 dB (HP / LP: the frequency).
    const int band = bandAt (e.position);
    if (band < 0)
        return;
    ospProcessor.undoManager.beginNewTransaction ("EQ band reset");
    if (eq::hasGain (static_cast<eq::Band> (band)))
        setValue (bandParameter (band, "gain"), 0.0f);
    else
        setValue (bandParameter (band, "frequency"), static_cast<float> (eq::frequencyRange (static_cast<eq::Band> (band)).defaultHz));
    refresh();
}

void EqEditor::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    // Over a band (or with one selected): its Q (bell, shelves) or its slope (HP, LP).
    int band = bandAt (e.position);
    if (band < 0)
        band = selected;
    if (band < 0 || std::abs (wheel.deltaY) < 1.0e-4f)
        return;
    selectBand (band);
    if (eq::hasQ (static_cast<eq::Band> (band)))
    {
        if (auto* q = bandParameter (band, "q"))
        {
            const float now = q->convertFrom0to1 (q->getValue());
            setValue (q, now * std::exp2 (wheel.deltaY > 0.0f ? 0.25f : -0.25f));
        }
    }
    else
        setValue (bandParameter (band, "slope"), wheel.deltaY > 0.0f ? 1.0f : 0.0f);
    refresh();
}

} // namespace osp::plugin
