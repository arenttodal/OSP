#include "EqEditor.h"
#include "Binding.h"

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

    // The display language shared with NOTE MAP: one light ink, hairlines on the device pixel
    // grid, text tabs, flat segmented choices, one node vocabulary for EQ and DAMPEN.
    const juce::Colour light { 0xffeee8dd };
    const juce::Colour rim = light.withAlpha (0.16f);

    void vline (juce::Graphics& g, float x, float y0, float y1, float px = 1.0f)
    {
        const float s = std::max (1.0f, g.getInternalContext().getPhysicalPixelScaleFactor());
        g.fillRect (juce::Rectangle<float> (std::floor (x * s) / s, y0, px / s, y1 - y0));
    }
    void hline (juce::Graphics& g, float x0, float x1, float y, float px = 1.0f)
    {
        const float s = std::max (1.0f, g.getInternalContext().getPhysicalPixelScaleFactor());
        g.fillRect (juce::Rectangle<float> (x0, std::floor (y * s) / s, x1 - x0, px / s));
    }

    void drawPower (juce::Graphics& g, juce::Rectangle<float> p, bool on)
    {
        g.setColour (on ? osp::plugin::design::colour::accent : osp::plugin::design::colour::wellText.withAlpha (0.7f));
        juce::Path icon;
        icon.addCentredArc (p.getCentreX(), p.getCentreY() + 1.0f, 6.5f, 6.5f, 0.0f, 0.65f, 2.0f * juce::MathConstants<float>::pi - 0.65f, true);
        icon.startNewSubPath (p.getCentreX(), p.getY() + 2.5f);
        icon.lineTo (p.getCentreX(), p.getCentreY());
        g.strokePath (icon, juce::PathStrokeType (1.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    void drawClose (juce::Graphics& g, juce::Rectangle<float> area)
    {
        const auto x = area.reduced (6.5f);
        juce::Path cross;
        cross.startNewSubPath (x.getTopLeft());
        cross.lineTo (x.getBottomRight());
        cross.startNewSubPath (x.getTopRight());
        cross.lineTo (x.getBottomLeft());
        g.setColour (osp::plugin::design::colour::wellText.brighter (0.3f));
        g.strokePath (cross, juce::PathStrokeType (1.3f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    /** The open page's name, bright and underlined in the layer's colour ("Off" after it while
        it is off), the other page a quiet text tab; a hairline under the whole row. */
    void drawTabs (juce::Graphics& g, juce::Rectangle<float> title, const juce::String& name, bool on, juce::Rectangle<float> other,
                   const juce::String& otherName, bool otherHot, int layer, float right, float rule = 38.0f)
    {
        using namespace osp::plugin::design;
        g.setFont (type::popupLabel (12.0f));
        g.setColour (light.withAlpha (on ? 0.95f : 0.6f));
        g.drawText (name, title, juce::Justification::centredLeft, false);
        const float tw = juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), name);
        g.setColour (colour::identity (layer).arc.withAlpha (on ? 1.0f : 0.5f));
        g.fillRect (juce::Rectangle<float> (title.getX(), title.getBottom() - 2.0f, tw, 1.5f));
        if (! on)
        {
            g.setFont (type::popupLabel (10.5f));
            g.setColour (colour::wellText);
            g.drawText ("Off", juce::Rectangle<float> (title.getX() + tw + 8.0f, title.getY(), 30.0f, title.getHeight()), juce::Justification::centredLeft, false);
            g.setFont (type::popupLabel (12.0f));
        }
        g.setColour (otherHot ? light.withAlpha (0.85f) : colour::wellText.brighter (0.15f));
        g.drawText (otherName, other, juce::Justification::centred, false);
        g.setColour (light.withAlpha (0.08f));
        hline (g, 12.0f, right - 12.0f, rule);
    }

    /** The second row's top and the graph's top when a page's values sit under its tabs. */
    constexpr float secondRowY = 37.0f, rowDrop = 28.0f;

    /** A segmented choice: one hairline frame, the chosen part filled once. */
    void drawSegments (juce::Graphics& g, juce::Rectangle<float> area, std::initializer_list<const char*> parts, int chosen, bool numerals)
    {
        using namespace osp::plugin::design;
        g.setColour (rim);
        g.drawRoundedRectangle (area.reduced (0.5f), 4.0f, 1.0f);
        const float w = area.getWidth() / static_cast<float> (parts.size());
        int k = 0;
        for (const auto* text : parts)
        {
            const auto cell = area.withWidth (w).translated (w * static_cast<float> (k), 0.0f);
            if (k == chosen)
            {
                g.setColour (light.withAlpha (0.1f));
                g.fillRoundedRectangle (cell.reduced (2.0f), 3.0f);
            }
            g.setFont (numerals ? type::popupValue (11.5f) : type::popupLabel (10.5f));
            g.setColour (light.withAlpha (k == chosen ? 0.95f : 0.5f));
            g.drawText (text, cell, juce::Justification::centred, false);
            ++k;
        }
    }

    /** A node: a disc in its colour with a graphite gap ring; selected adds one fine outer ring. */
    void drawNode (juce::Graphics& g, juce::Point<float> at, juce::Colour c, bool heard, bool hot, bool selected)
    {
        using namespace osp::plugin::design;
        const float r = hot || selected ? 6.0f : 5.0f;
        if (selected)
        {
            g.setColour (c.withAlpha (0.45f));
            g.drawEllipse (juce::Rectangle<float> (2.0f * r + 9.0f, 2.0f * r + 9.0f).withCentre (at), 1.2f);
        }
        g.setColour (colour::wellA);
        g.fillEllipse (juce::Rectangle<float> (2.0f * r + 3.0f, 2.0f * r + 3.0f).withCentre (at));
        g.setColour (heard ? c : c.withMultipliedSaturation (0.5f).withAlpha (0.7f));
        g.fillEllipse (juce::Rectangle<float> (2.0f * r, 2.0f * r).withCentre (at));
    }

    /** The graph's corner note (a hint or a state), small, never over the curve's middle. */
    void drawCornerNote (juce::Graphics& g, juce::Rectangle<float> graph, const juce::String& text)
    {
        using namespace osp::plugin::design;
        g.setFont (type::popupLabel (11.5f));
        g.setColour (colour::wellText);
        g.drawText (text, juce::Rectangle<float> (graph.getX() + 8.0f, graph.getY() + 6.0f, graph.getWidth() - 16.0f, 18.0f), juce::Justification::centredLeft, false);
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
    binding::tag (*this, InstrumentHost::eqParameterId (l, "*") + " " + InstrumentHost::dampenParameterId (l, "*"));
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
    setTooltip ("This layer's EQ (it opens over the waveform)");
    setTitle ("EQ");
}

void EqButton::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    using namespace design;
    const auto r = getLocalBounds().toFloat().reduced (1.0f);
    const bool open = getToggleState();
    if (skin::drawnBySurface())
    {
        // Ceramic: the card draws the key (pressed in while open).
    }
    else if (open || down)
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
    {
        // A small flat telltale in the layer's colour (as NOTES' amber one), not a lamp.
        g.setColour (colour::identity (layer).arc);
        g.fillEllipse (juce::Rectangle<float> (4.5f, 4.5f).withCentre ({ r.getX() + (narrow ? 7.0f : 9.5f), r.getCentreY() }));
    }
    g.setFont (type::button (narrow ? 12.5f : 13.5f));
    g.setColour (colour::text.withAlpha (open || active ? 0.92f : 0.62f));
    g.drawText ("EQ", r.withTrimmedLeft (active ? (narrow ? 9.0f : 12.0f) : 0.0f), juce::Justification::centred, false);
}

//==============================================================================
EqValueField::EqValueField (InstrumentHost& processor, const juce::String& id)
    : juce::Slider (juce::Slider::RotaryVerticalDrag, juce::Slider::NoTextBox)
{
    parameter = processor.apvts().getParameter (id);
    getProperties().set ("paramId", id);   // a modulation drop target
    getProperties().set ("modField", true);
    binding::tag (*this, id);
    setMouseDragSensitivity (220);
    setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
    attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (processor.apvts(), id, *this);
    if (parameter != nullptr)
        setTooltip (parameter->getName (64) + ": drag up or down, double-click to type");
    onValueChange = [this] { repaint(); };
}

void EqValueField::paint (juce::Graphics& g)
{
    // A readout, not a box: the value over a hairline that takes the band's colour under the
    // pointer (drag it, double-click to type).
    const auto r = getLocalBounds().toFloat();
    const bool hot = isMouseOverOrDragging();
    if (hot)
    {
        g.setColour (light.withAlpha (0.05f));
        g.fillRoundedRectangle (r.reduced (0.5f, 1.5f), 4.0f);
    }
    g.setColour (hot ? accent.withAlpha (0.85f) : rim);
    hline (g, r.getX() + 3.0f, r.getRight() - 3.0f, r.getBottom() - 2.0f);
    if (editor != nullptr)
        return;
    const auto text = parameter != nullptr ? parameter->getCurrentValueAsText() : juce::String();
    g.setFont (type::popupValue (12.5f));
    g.setColour (light.withAlpha (hot ? 1.0f : 0.92f));
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
EqEditor::EqEditor (InstrumentHost& p, int l) : ospProcessor (p), layer (l)
{
    binding::tag (*this, InstrumentHost::eqParameterId (l, "*"));
    setTitle ("EQ " + InstrumentHost::layerName (layer));
    shown = ospProcessor.layerEqSettings (layer);
    setRepaintsOnMouseActivity (false);
    dampenPage = std::make_unique<DampenEditor> (ospProcessor, layer);
    dampenPage->onShowEq = [this] { showDampen (false); };
    dampenPage->onClose = [this] {
        if (onClose != nullptr)
            onClose();
    };
    addChildComponent (*dampenPage);
}

void EqEditor::showDampen (bool show)
{
    dampenPage->setVisible (show);
    if (show)
    {
        dampenPage->toFront (false);
        dampenPage->refresh();
    }
    repaint();
}

double EqEditor::sampleRate() const
{
    const double rate = ospProcessor.hostSampleRate();
    return rate > 0.0 ? rate : 48000.0;
}

juce::RangedAudioParameter* EqEditor::bandParameter (int band, const char* name) const
{
    return ospProcessor.apvts().getParameter (
        InstrumentHost::eqParameterId (layer, juce::String (InstrumentHost::eqBandKey (static_cast<eq::Band> (band))) + "." + name));
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
    ospProcessor.undo().beginNewTransaction (on ? "EQ band on" : "EQ band off");
    // The first band switched on also switches the EQ on (what else would the musician
    // mean); once anything is on, the EQ's own switch is theirs alone.
    if (on && ! shown.enabled)
    {
        bool anyOn = false;
        for (const auto& b : shown.bands)
            anyOn = anyOn || b.enabled;
        if (! anyOn)
            setValue (ospProcessor.apvts().getParameter (InstrumentHost::eqParameterId (layer, "enabled")), 1.0f);
    }
    setValue (bandParameter (band, "enabled"), on ? 1.0f : 0.0f);
    shown = ospProcessor.layerEqSettings (layer);
    repaint();
}

juce::Rectangle<float> EqEditor::graphArea() const
{
    return getLocalBounds().toFloat().withTrimmedLeft (38.0f).withTrimmedRight (14.0f).withTrimmedTop (twoRows ? 44.0f + rowDrop : 44.0f).withTrimmedBottom (24.0f);
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
    if (dampenShown())
        dampenPage->refresh();
    const auto now = ospProcessor.layerEqSettings (layer);
    const auto offsets = modulationOffsets();
    bool moved = false;
    for (std::size_t i = 0; i < offsets.size(); ++i)
        moved = moved || std::abs (offsets[i] - shownModulation[i]) > 1.0e-4;
    if (! (now == shown) || moved)
    {
        const bool switched = now.enabled != shown.enabled;
        shown = now;
        shownModulation = offsets;
        if (switched)
            resized();   // "Off" after the name moves the DAMPEN tab
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
        const juce::String key = InstrumentHost::eqBandKey (band);
        auto make = [&] (const char* name) {
            auto f = std::make_unique<EqValueField> (ospProcessor, InstrumentHost::eqParameterId (layer, key + "." + name));
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
    // The open page's name, then the other page's tab one even gap after it (after "Off" while
    // the EQ is off), as NOTE MAP's tabs are spaced.
    const float tw = juce::GlyphArrangement::getStringWidth (type::popupLabel (12.0f), "EQ " + InstrumentHost::layerName (layer));
    titleArea = { 40.0f, 9.0f, tw + (shown.enabled ? 4.0f : 30.0f), 24.0f };
    dampenTab = { titleArea.getRight() + 10.0f, 9.0f, 64.0f, 24.0f };
    if (dampenPage != nullptr)
        dampenPage->setBounds (getLocalBounds());
    closeArea = { w - 32.0f, 10.0f, 22.0f, 22.0f };
    // The widest band's values (a bell: Q, gain, frequency, its switch and reset) beside the
    // tabs when they fit; else on a second row under them (three cards), the graph lower.
    // Decided by the width alone, so selecting a band never moves the graph.
    const float widest = 24.0f + 24.0f + 52.0f + 64.0f + 74.0f + 5.0f * 6.0f;
    twoRows = closeArea.getX() - 10.0f - widest < dampenTab.getRight() + 8.0f;
    const float rowY = twoRows ? secondRowY : 9.0f;
    float x = twoRows ? w - 10.0f : closeArea.getX() - 10.0f;
    auto take = [&x, rowY] (float width) {
        x -= width;
        const juce::Rectangle<float> r (x, rowY, width, 24.0f);
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
    const float left = twoRows ? 12.0f : dampenTab.getRight();
    nameArea = x - left > 90.0f ? juce::Rectangle<float> (x - 90.0f, rowY, 90.0f, 24.0f) : juce::Rectangle<float>();
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
    // The display's graphite, opaque: graph first, nothing of the waveform's labels showing
    // through (NOTE MAP's rule).
    g.setColour (colour::wellA);
    g.fillRoundedRectangle (bounds, 10.0f);
    const auto graph = graphArea();
    const bool heard = shown.enabled;

    // Grid: frequencies (decades stronger) and gain (0 dB stronger), on the device pixel grid,
    // with quiet labels that never leave the display.
    g.setFont (type::popupLabel (10.5f));
    for (double hz : { 20.0, 30.0, 40.0, 50.0, 60.0, 70.0, 80.0, 90.0, 100.0, 200.0, 300.0, 400.0, 500.0, 600.0, 700.0, 800.0, 900.0, 1000.0, 2000.0,
                       3000.0, 4000.0, 5000.0, 6000.0, 7000.0, 8000.0, 9000.0, 10000.0, 20000.0 })
    {
        const int whole = juce::roundToInt (hz);
        const bool decade = whole == 100 || whole == 1000 || whole == 10000;
        g.setColour (colour::wellGrid.withAlpha (decade ? 0.9f : 0.4f));
        vline (g, xForHz (hz), graph.getY(), graph.getBottom());
    }
    const std::array<std::pair<double, const char*>, 7> labels { { { 20.0, "20" }, { 100.0, "100" }, { 500.0, "500" }, { 1000.0, "1k" },
                                                                  { 5000.0, "5k" }, { 10000.0, "10k" }, { 20000.0, "20k" } } };
    g.setColour (colour::wellText.withAlpha (0.8f));
    float lastRight = -100.0f;
    for (const auto& [hz, text] : labels)
    {
        const float x = xForHz (hz);
        const int whole = juce::roundToInt (hz);
        // The ends start or finish on their line, inside the display; the rest are centred.
        auto r = juce::Rectangle<float> (x - 18.0f, graph.getBottom() + 4.0f, 36.0f, 14.0f);
        auto just = juce::Justification::centred;
        if (whole == 20)
        {
            r = r.withX (x);
            just = juce::Justification::centredLeft;
        }
        else if (whole == 20000)
        {
            r = r.withRightX (x);
            just = juce::Justification::centredRight;
        }
        if (r.getX() < lastRight + 2.0f)
            continue;   // never crowded
        g.drawText (text, r, just, false);
        lastRight = r.getRight();
    }
    for (int db = -18; db <= 18; db += 6)
    {
        const float y = yForDb (db);
        g.setColour (db == 0 ? colour::wellText.withAlpha (0.4f) : colour::wellGrid.withAlpha (0.6f));
        hline (g, graph.getX(), graph.getRight(), y);
        if (db % 12 == 0)
        {
            g.setColour (colour::wellText.withAlpha (0.8f));
            g.drawText (db > 0 ? "+" + juce::String (db) : juce::String (db), juce::Rectangle<float> (2.0f, y - 7.0f, graph.getX() - 10.0f, 14.0f),
                        juce::Justification::centredRight, false);
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
        g.setColour (equi::bandColour (band).withAlpha (0.1f));
        g.fillPath (fill);
        g.setColour (equi::bandColour (band).withAlpha (0.5f));
        g.strokePath (own, juce::PathStrokeType (1.0f));
    }

    // The EQ as heard: one crisp line (its switch off: a quiet dashed line of the shape it would have).
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
            g.setColour (warm.withAlpha (0.06f));
            g.fillPath (fill);
            g.setColour (warm);
            g.strokePath (curve, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
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

    // The bands: quiet hollow markers on 0 dB while off, nodes while on.
    for (int b = 0; b < eq::bandCount; ++b)
    {
        const auto band = static_cast<eq::Band> (b);
        const auto& s = shown.bands[static_cast<std::size_t> (b)];
        const auto c = equi::bandColour (band);
        const auto at = nodePosition (b);
        const bool hot = b == hover || b == dragBand;
        if (! s.enabled)
        {
            const float r = hot ? 5.0f : 4.0f;
            g.setColour (c.withAlpha (hot ? 0.95f : 0.45f));
            g.drawEllipse (juce::Rectangle<float> (2.0f * r, 2.0f * r).withCentre (at), 1.2f);
            continue;
        }
        // Modulated: the range it sweeps (a thin line through the node) and where it is now.
        const auto eff = effective().bands[static_cast<std::size_t> (b)];
        if (std::abs (eff.frequencyHz - s.frequencyHz) > 1.0e-3 || std::abs (eff.gainDb - s.gainDb) > 1.0e-3)
        {
            const auto now = juce::Point<float> (xForHz (eff.frequencyHz), eq::hasGain (band) ? yForDb (eff.gainDb) : at.y);
            g.setColour (c.withAlpha (0.5f));
            g.drawLine ({ at, now }, 1.0f);
            g.setColour (c.withAlpha (0.8f));
            g.drawEllipse (juce::Rectangle<float> (7.0f, 7.0f).withCentre (now), 1.2f);
        }
        drawNode (g, at, c, heard, hot, b == selected);
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
        g.fillRoundedRectangle (tip, 4.0f);
        g.setColour (equi::bandColour (band).brighter (0.3f));
        g.drawText (text, tip, juce::Justification::centred, false);
    }

    // The top row: the EQ's switch, EQ | DAMPEN as text tabs, the selected band's values.
    drawPower (g, powerArea(), heard);
    drawTabs (g, titleArea, "EQ " + InstrumentHost::layerName (layer), heard, dampenTab, "DAMPEN", hotTab, layer, bounds.getRight(),
              twoRows ? 38.0f + rowDrop : 38.0f);
    if (selected >= 0)
    {
        const auto band = static_cast<eq::Band> (selected);
        const auto c = equi::bandColour (band);
        const auto& s = shown.bands[static_cast<std::size_t> (selected)];
        if (! nameArea.isEmpty())
        {
            g.setFont (type::popupLabel (10.5f));
            g.setColour (c.brighter (0.2f));
            g.drawText (eq::bandName (band), nameArea, juce::Justification::centredRight, false);
        }
        if (! slopeArea.isEmpty())
            drawSegments (g, slopeArea, { "12", "24" }, s.steep ? 1 : 0, true);
        // The band's own switch and its reset (the same 1.3 px line as close).
        const auto sw = bandSwitchArea.reduced (4.0f);
        g.setColour (s.enabled ? c : rim.withAlpha (0.4f));
        g.drawEllipse (sw.reduced (0.65f), 1.3f);
        if (s.enabled)
            g.fillEllipse (sw.reduced (4.0f));
        const auto re = resetArea.reduced (5.0f);
        juce::Path arrow;
        arrow.addCentredArc (re.getCentreX(), re.getCentreY(), 6.0f, 6.0f, 0.0f, -2.2f, 2.4f, true);
        g.setColour (colour::wellText.brighter (0.3f));
        g.strokePath (arrow, juce::PathStrokeType (1.3f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.fillEllipse (juce::Rectangle<float> (2.8f, 2.8f).withCentre (re.getCentre().getPointOnCircumference (6.0f, -2.2f)));
    }
    drawClose (g, closeArea);
    if (shown.enabled == false && selected < 0)
        drawCornerNote (g, graph, "Drag a marker to shape this sound");
}

void EqEditor::mouseMove (const juce::MouseEvent& e)
{
    const int now = bandAt (e.position);
    const bool tab = dampenTab.contains (e.position);
    if (now != hover || tab != hotTab)
    {
        hover = now;
        hotTab = tab;
        setMouseCursor (now >= 0 ? juce::MouseCursor::DraggingHandCursor
                                 : (tab ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor));
        repaint();
    }
}

void EqEditor::mouseExit (const juce::MouseEvent&)
{
    if (hover >= 0 || hotTab)
    {
        hover = -1;
        hotTab = false;
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
    if (dampenTab.contains (p))
    {
        showDampen (true);
        return;
    }
    if (powerArea().expanded (4.0f).contains (p) || titleArea.contains (p))
    {
        ospProcessor.undo().beginNewTransaction (shown.enabled ? "EQ off" : "EQ on");
        setValue (ospProcessor.apvts().getParameter (InstrumentHost::eqParameterId (layer, "enabled")), shown.enabled ? 0.0f : 1.0f);
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
            ospProcessor.undo().beginNewTransaction ("EQ band reset");
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
            ospProcessor.undo().beginNewTransaction ("EQ slope");
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
    ospProcessor.undo().beginNewTransaction ("EQ band");
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
    ospProcessor.undo().beginNewTransaction ("EQ band reset");
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

//==============================================================================
// DAMPEN

namespace
{
    const juce::Colour dampenColour { 0xff93a9b8 };   // a cool, quiet blue-grey: taking the highs away
}

DampenEditor::DampenEditor (InstrumentHost& p, int l) : ospProcessor (p), layer (l)
{
    binding::tag (*this, InstrumentHost::dampenParameterId (l, "*"));
    setTitle ("DAMPEN " + InstrumentHost::layerName (layer));
    setTooltip ("DAMPEN: each note loses highs by its key. Drag a point, double-click to add or remove one");
    auto make = [this] (const char* name) {
        auto f = std::make_unique<EqValueField> (ospProcessor, InstrumentHost::dampenParameterId (layer, name));
        f->setColour (dampenColour);
        addAndMakeVisible (*f);
        return f;
    };
    amountField = make ("amount");
    frequencyField = make ("frequency");
    cutoffField = make ("cutoff");
    curve = ospProcessor.modulationCurve (InstrumentHost::dampenCurveIndex (layer));
}

juce::RangedAudioParameter* DampenEditor::parameter (const char* name) const
{
    return ospProcessor.apvts().getParameter (InstrumentHost::dampenParameterId (layer, name));
}

bool DampenEditor::enabled() const
{
    return ospProcessor.parameterValue (InstrumentHost::dampenParameterId (layer, "enabled")) >= 0.5f;
}

bool DampenEditor::lowPass() const
{
    return ospProcessor.parameterValue (InstrumentHost::dampenParameterId (layer, "mode")) >= 0.5f;
}

juce::Rectangle<float> DampenEditor::graphArea() const
{
    return getLocalBounds().toFloat().withTrimmedLeft (38.0f).withTrimmedRight (14.0f).withTrimmedTop (twoRows ? 44.0f + rowDrop : 44.0f).withTrimmedBottom (24.0f);
}

juce::Point<float> DampenEditor::toScreen (float x, float y) const
{
    // Keys across; no damping at the top, full damping at the bottom.
    const auto g = graphArea();
    return { g.getX() + x * g.getWidth(), g.getY() + y * g.getHeight() };
}

juce::Point<float> DampenEditor::fromScreen (juce::Point<float> p) const
{
    const auto g = graphArea();
    return { juce::jlimit (0.0f, 1.0f, (p.x - g.getX()) / std::max (1.0f, g.getWidth())),
             juce::jlimit (0.0f, 1.0f, (p.y - g.getY()) / std::max (1.0f, g.getHeight())) };
}

juce::Point<float> DampenEditor::pointPosition (int index) const
{
    const auto& pt = curve.points[static_cast<std::size_t> (juce::jlimit (0, std::max (0, curve.count - 1), index))];
    return toScreen (pt.x, pt.y);
}

double DampenEditor::dampingAt (int note) const
{
    return mod::curveAt (mod::compileCurve (curve), juce::jlimit (0, 127, note) / 127.0);
}

int DampenEditor::pointAt (juce::Point<float> p) const
{
    int best = -1;
    float distance = 9.0f;
    for (int i = 0; i < curve.count; ++i)
        if (const float d = pointPosition (i).getDistanceFrom (p); d < distance)
        {
            distance = d;
            best = i;
        }
    return best;
}

void DampenEditor::refresh()
{
    const auto now = ospProcessor.modulationCurve (InstrumentHost::dampenCurveIndex (layer));
    std::array<float, 6> signature { enabled() ? 1.0f : 0.0f, lowPass() ? 1.0f : 0.0f,
                                     ospProcessor.parameterValue (InstrumentHost::dampenParameterId (layer, "amount")),
                                     ospProcessor.parameterValue (InstrumentHost::dampenParameterId (layer, "frequency")),
                                     ospProcessor.parameterValue (InstrumentHost::dampenParameterId (layer, "cutoff")), 0.0f };
    const int note = ospProcessor.lastPlayedNoteNumber();
    const bool modeMoved = signature[1] != shownSignature[1];
    if ((! dragging && ! (now == curve)) || signature != shownSignature || note != shownNote)
    {
        if (! dragging)
            curve = now;
        const bool switched = signature[0] != shownSignature[0];
        shownSignature = signature;
        shownNote = note;
        if (switched)
            resized();   // "Off" after the name moves the EQ tab
        else if (modeMoved)
            layoutFields();
        repaint();
    }
}

void DampenEditor::applyPreset (int preset)
{
    const auto next = preset <= 0 ? dampen::defaultCurve() : mod::presetCurve (static_cast<mod::CurvePreset> (preset - 1));
    ospProcessor.undo().beginNewTransaction ("DAMPEN curve");
    ospProcessor.setModulationCurve (InstrumentHost::dampenCurveIndex (layer), next, true);
    curve = ospProcessor.modulationCurve (InstrumentHost::dampenCurveIndex (layer));
    repaint();
}

void DampenEditor::resized()
{
    const float w = static_cast<float> (getWidth());
    const float tw = juce::GlyphArrangement::getStringWidth (type::popupLabel (12.0f), "DAMPEN " + InstrumentHost::layerName (layer));
    title = { 40.0f, 9.0f, tw + (enabled() ? 4.0f : 30.0f), 24.0f };
    eqTab = { title.getRight() + 10.0f, 9.0f, 34.0f, 24.0f };
    closeArea = { w - 32.0f, 10.0f, 22.0f, 22.0f };
    // Everything beside the tabs when it fits; else (three cards) the curve's presets stay
    // up there and the mode, AMOUNT and the value take a second row, compact.
    const float full = 64.0f + 74.0f + 34.0f + 58.0f + 52.0f + 136.0f + 6.0f * 6.0f;
    twoRows = closeArea.getX() - 10.0f - full < eqTab.getRight() + 8.0f;
    float x = closeArea.getX() - 10.0f, rowY = 9.0f;
    auto take = [&x, &rowY] (float width) {
        x -= width;
        const juce::Rectangle<float> r (x, rowY, width, 24.0f);
        x -= 6.0f;
        return r;
    };
    presets = take (64.0f);
    if (twoRows)
    {
        x = w - 10.0f;
        rowY = secondRowY;
    }
    const auto value = take (twoRows ? 60.0f : 74.0f);
    valueCaption = take (twoRows ? 32.0f : 34.0f);
    const auto amount = take (twoRows ? 50.0f : 58.0f);
    amountCaption = take (twoRows ? 48.0f : 52.0f);
    mode = take (twoRows ? std::clamp (x - 12.0f, 72.0f, 136.0f) : 136.0f);
    amountField->setBounds (amount.getSmallestIntegerContainer());
    frequencyField->setBounds (value.getSmallestIntegerContainer());
    cutoffField->setBounds (value.getSmallestIntegerContainer());
    layoutFields();
}

void DampenEditor::layoutFields()
{
    frequencyField->setVisible (! lowPass());
    cutoffField->setVisible (lowPass());
}

void DampenEditor::paint (juce::Graphics& g)
{
    using namespace design;
    const auto bounds = getLocalBounds().toFloat();
    // Opaque, as the EQ page it covers: nothing of that page shows through.
    g.setColour (colour::wellA);
    g.fillRoundedRectangle (bounds, 10.0f);
    const auto graph = graphArea();
    const bool heard = enabled();

    // Grid: the octaves (C0 .. C8) across, damping in quarters down, on the device pixel grid.
    g.setFont (type::popupLabel (10.5f));
    for (int octave = 0; octave <= 8; ++octave)
    {
        const float x = toScreen (static_cast<float> (12 * (octave + 1)) / 127.0f, 0.0f).x;
        g.setColour (colour::wellGrid.withAlpha (octave == 4 ? 0.9f : 0.45f));
        vline (g, x, graph.getY(), graph.getBottom());
        g.setColour (colour::wellText.withAlpha (0.8f));
        g.drawText ("C" + juce::String (octave), juce::Rectangle<float> (x - 14.0f, graph.getBottom() + 4.0f, 28.0f, 14.0f), juce::Justification::centred, false);
    }
    for (int q = 0; q <= 4; ++q)
    {
        const float y = graph.getY() + graph.getHeight() * static_cast<float> (q) / 4.0f;
        g.setColour (q == 0 ? colour::wellText.withAlpha (0.4f) : colour::wellGrid.withAlpha (0.6f));
        hline (g, graph.getX(), graph.getRight(), y);
    }
    // The scale's ends, clear of a point sitting on the edge.
    g.setColour (colour::wellText.withAlpha (0.8f));
    g.drawText ("0", juce::Rectangle<float> (2.0f, graph.getY() - 7.0f, graph.getX() - 10.0f, 14.0f), juce::Justification::centredRight, false);
    g.drawText (lowPass() ? "LP" : "-18", juce::Rectangle<float> (2.0f, graph.getBottom() - 7.0f, graph.getX() - 10.0f, 14.0f), juce::Justification::centredRight, false);

    // The curve, the damping it takes filled below the top (dashed while DAMPEN is off).
    const auto table = mod::compileCurve (curve);
    juce::Path line;
    for (int i = 0; i <= 200; ++i)
    {
        const float x = static_cast<float> (i) / 200.0f;
        const auto p = toScreen (x, static_cast<float> (mod::curveAt (table, x)));
        i == 0 ? line.startNewSubPath (p) : line.lineTo (p);
    }
    if (heard)
    {
        auto fill = line;
        fill.lineTo (graph.getRight(), graph.getY());
        fill.lineTo (graph.getX(), graph.getY());
        fill.closeSubPath();
        g.setColour (dampenColour.withAlpha (0.1f));
        g.fillPath (fill);
        g.setColour (dampenColour);
        g.strokePath (line, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
    else
    {
        juce::Path dashed;
        const float dashes[] { 4.0f, 4.0f };
        juce::PathStrokeType (1.2f).createDashedStroke (dashed, line, dashes, 2);
        g.setColour (dampenColour.withAlpha (0.45f));
        g.fillPath (dashed);
    }
    for (int i = 0; i < curve.count; ++i)
        drawNode (g, pointPosition (i), dampenColour, heard, i == hoverPoint || i == dragPoint, false);
    // The last key played: where it sits on the curve.
    if (const int note = ospProcessor.lastPlayedNoteNumber(); note >= 0)
    {
        const float x = static_cast<float> (note) / 127.0f;
        const auto at = toScreen (x, static_cast<float> (mod::curveAt (table, x)));
        g.setColour (colour::accent.withAlpha (0.45f));
        vline (g, at.x, graph.getY(), graph.getBottom());
        g.setColour (colour::accent);
        g.fillEllipse (juce::Rectangle<float> (6.0f, 6.0f).withCentre (at));
    }

    // The top row: DAMPEN's switch, DAMPEN | EQ as text tabs, the mode, AMOUNT, the value, the
    // curve's presets, close (the EQ page's vocabulary).
    drawPower (g, powerArea(), heard);
    drawTabs (g, title, "DAMPEN " + InstrumentHost::layerName (layer), heard, eqTab, "EQ", hotTab, layer, bounds.getRight(),
              twoRows ? 38.0f + rowDrop : 38.0f);
    if (mode.getWidth() >= 110.0f)
        drawSegments (g, mode, { "Shelf", "Low-pass" }, lowPass() ? 1 : 0, false);
    else
        drawSegments (g, mode, { "Shelf", "LP" }, lowPass() ? 1 : 0, false);
    g.setFont (type::popupLabel (10.5f));
    g.setColour (colour::wellText.brighter (0.1f));
    g.drawText ("AMOUNT", amountCaption, juce::Justification::centredRight, false);
    g.drawText (lowPass() ? "MIN" : "FREQ", valueCaption, juce::Justification::centredRight, false);
    {
        // An action: a hairline outline, filled faintly under the pointer.
        const auto box = presets.reduced (0.5f, 1.5f);
        if (hotPresets)
        {
            g.setColour (light.withAlpha (0.05f));
            g.fillRoundedRectangle (box, 4.0f);
        }
        g.setColour (rim);
        g.drawRoundedRectangle (box.reduced (0.5f), 4.0f, 1.0f);
        g.setFont (type::popupValue (11.5f));
        g.setColour (light.withAlpha (hotPresets ? 0.95f : 0.72f));
        g.drawText (juce::String::fromUTF8 ("Curve \xe2\x96\xbe"), box, juce::Justification::centred, false);
    }
    drawClose (g, closeArea);
    if (! heard)
        drawCornerNote (g, graph, "DAMPEN is off: switch it on to tame bright high notes");
}

void DampenEditor::mouseExit (const juce::MouseEvent&)
{
    if (hoverPoint >= 0 || hotTab || hotPresets)
    {
        hoverPoint = -1;
        hotTab = hotPresets = false;
        repaint();
    }
}

void DampenEditor::mouseMove (const juce::MouseEvent& e)
{
    const int now = pointAt (e.position);
    const bool tab = eqTab.contains (e.position), preset = presets.contains (e.position);
    if (now != hoverPoint || tab != hotTab || preset != hotPresets)
    {
        hoverPoint = now;
        hotTab = tab;
        hotPresets = preset;
        setMouseCursor (now >= 0 ? juce::MouseCursor::DraggingHandCursor
                                 : (tab || preset ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor));
        repaint();
    }
}

void DampenEditor::mouseDown (const juce::MouseEvent& e)
{
    const auto p = e.position;
    auto set = [] (juce::RangedAudioParameter* param, float value) {
        if (param == nullptr)
            return;
        param->beginChangeGesture();
        param->setValueNotifyingHost (param->convertTo0to1 (value));
        param->endChangeGesture();
    };
    if (closeArea.contains (p))
    {
        if (onClose != nullptr)
            onClose();
        return;
    }
    if (eqTab.contains (p))
    {
        if (onShowEq != nullptr)
            onShowEq();
        return;
    }
    if (powerArea().expanded (4.0f).contains (p) || title.contains (p))
    {
        ospProcessor.undo().beginNewTransaction (enabled() ? "DAMPEN off" : "DAMPEN on");
        set (parameter ("enabled"), enabled() ? 0.0f : 1.0f);
        refresh();
        return;
    }
    if (mode.contains (p))
    {
        ospProcessor.undo().beginNewTransaction ("DAMPEN mode");
        set (parameter ("mode"), p.x >= mode.getCentreX() ? 1.0f : 0.0f);
        refresh();
        return;
    }
    if (presets.contains (p))
    {
        juce::PopupMenu menu;
        juce::Component::SafePointer<DampenEditor> safe (this);
        menu.addItem ("High notes (default)", [safe] { if (safe != nullptr) safe->applyPreset (0); });
        for (const auto preset : { mod::CurvePreset::lowToHigh, mod::CurvePreset::highOnly, mod::CurvePreset::highToLow, mod::CurvePreset::lowOnly,
                                   mod::CurvePreset::midEmphasis, mod::CurvePreset::flat })
            menu.addItem (mod::curvePresetName (preset), [safe, preset] { if (safe != nullptr) safe->applyPreset (static_cast<int> (preset) + 1); });
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea (localAreaToGlobal (presets.getSmallestIntegerContainer())));
        return;
    }
    curve = ospProcessor.modulationCurve (InstrumentHost::dampenCurveIndex (layer));
    before = curve;
    dragPoint = pointAt (p);
    dragSegment = -1;
    if (dragPoint < 0 && graphArea().contains (p))
    {
        // A press on the line bends that segment (its end point's tension).
        const float x = fromScreen (p).x;
        for (int i = 1; i < curve.count; ++i)
            if (x <= curve.points[static_cast<std::size_t> (i)].x)
            {
                dragSegment = i;
                break;
            }
        if (dragSegment > 0)
            dragStartTension = curve.points[static_cast<std::size_t> (dragSegment)].tension;
    }
    dragStart = p;
    dragging = dragPoint >= 0 || dragSegment > 0;
}

void DampenEditor::mouseDrag (const juce::MouseEvent& e)
{
    if (! dragging)
        return;
    if (dragPoint >= 0)
    {
        auto& point = curve.points[static_cast<std::size_t> (dragPoint)];
        const auto p = fromScreen (e.position);
        if (dragPoint > 0 && dragPoint < curve.count - 1)
            point.x = juce::jlimit (curve.points[static_cast<std::size_t> (dragPoint - 1)].x + 0.005f,
                                    curve.points[static_cast<std::size_t> (dragPoint + 1)].x - 0.005f, p.x);
        point.y = p.y;
    }
    else
    {
        const auto& a = curve.points[static_cast<std::size_t> (dragSegment - 1)];
        auto& b = curve.points[static_cast<std::size_t> (dragSegment)];
        const float direction = b.y >= a.y ? -1.0f : 1.0f;   // the graph runs downwards
        b.tension = juce::jlimit (-1.0f, 1.0f, dragStartTension + direction * (e.position.y - dragStart.y) / 80.0f);
    }
    ospProcessor.setModulationCurve (InstrumentHost::dampenCurveIndex (layer), curve, false);   // heard while dragging; one undo step on release
    repaint();
}

void DampenEditor::mouseUp (const juce::MouseEvent&)
{
    if (dragging && ! (curve == before))
    {
        const auto edited = curve;
        ospProcessor.setModulationCurve (InstrumentHost::dampenCurveIndex (layer), before, false);
        ospProcessor.undo().beginNewTransaction ("DAMPEN curve");
        ospProcessor.setModulationCurve (InstrumentHost::dampenCurveIndex (layer), edited, true);
    }
    dragging = false;
    dragPoint = dragSegment = -1;
    repaint();
}

void DampenEditor::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (! graphArea().expanded (6.0f).contains (e.position))
        return;
    auto next = ospProcessor.modulationCurve (InstrumentHost::dampenCurveIndex (layer));
    curve = next;
    const int hit = pointAt (e.position);
    if (hit > 0 && hit < next.count - 1)
        next.remove (hit);
    else if (hit < 0)
    {
        const auto p = fromScreen (e.position);
        next.add ({ p.x, p.y, 0.0f });
    }
    else
        return;
    ospProcessor.undo().beginNewTransaction ("DAMPEN curve");
    ospProcessor.setModulationCurve (InstrumentHost::dampenCurveIndex (layer), next, true);
    curve = ospProcessor.modulationCurve (InstrumentHost::dampenCurveIndex (layer));
    hoverPoint = -1;
    repaint();
}

} // namespace osp::plugin
