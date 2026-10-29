#include "KeyDnaEditor.h"
#include "CeramicSkin.h"
#include "OspLookAndFeel.h"

#include "core/PitchMath.h"
#include "core/Prng.h"

#include <algorithm>
#include <cmath>

namespace osp::plugin
{

namespace
{
    constexpr int lowestKey = 21, highestKey = 108;   // the keyboard's keys (A0 .. C8)

    juce::String percent (double v) { return juce::String (juce::roundToInt (100.0 * v)) + " %"; }

    /** What a parameter's offset means at the curve's ends, in its own units (the editor's
        scale, so DEPTH reads as something heard). */
    juce::String rangeText (keydna::Param p)
    {
        switch (p)
        {
            case keydna::Param::position: return juce::String::fromUTF8 ("\xc2\xb1") + "50 % of the recording";
            case keydna::Param::size: return juce::String::fromUTF8 ("\xc3\x97\xc3\xb7") + " grain length";
            case keydna::Param::density: return juce::String::fromUTF8 ("\xc3\x97\xc3\xb7") + " grains per second";
            case keydna::Param::spread: return juce::String::fromUTF8 ("\xc2\xb1") + "50 % spread";
            case keydna::Param::tune: return juce::String::fromUTF8 ("\xc2\xb1") + "12 st";
            case keydna::Param::scatter: return juce::String::fromUTF8 ("\xc2\xb1") + "6 st of scatter";
            case keydna::Param::pan: return juce::String::fromUTF8 ("\xc2\xb1") + "50 % stereo";
        }
        return {};
    }
}

KeyDnaEditor::KeyDnaEditor (InstrumentHost& h, int l) : host (h), layerIndex (l)
{
    amountParameter = host.apvts().getParameter (InstrumentHost::keyDnaAmountId (layerIndex));
    setWantsKeyboardFocus (true);
    setTitle ("KEY DNA " + InstrumentHost::layerName (layerIndex));
    fields = { { "amount", "AMOUNT", {}, 0.0, 1.0, 1.0 },
               { "individuality", "INDIVIDUALITY", {}, 0.0, 1.0, 0.3 },
               { "seed", "SEED", {}, 1.0, 9999.0, 1.0 },
               { "depth", "DEPTH", {}, -1.0, 1.0, 0.0 },
               { "variation", "VARIATION", {}, 0.0, 1.0, 0.0 } };
    settings = host.keyDnaSettings (layerIndex);
    curve = host.keyDnaCurve (layerIndex, shownParam);
    recompute();
    refresh();
}

KeyDnaEditor::~KeyDnaEditor() = default;

void KeyDnaEditor::recompute()
{
    auto shape = settings;
    shape.amount = 1.0;   // the processor's table: AMOUNT is applied live
    table.compute (shape);
}

void KeyDnaEditor::refresh()
{
    const auto now = host.keyDnaSettings (layerIndex);
    const auto nowCurve = host.keyDnaCurve (layerIndex, shownParam);
    const double nowAmount = amountParameter != nullptr ? 0.01 * host.parameterValue (InstrumentHost::keyDnaAmountId (layerIndex)) : 1.0;
    std::array<bool, 128> nowHeld {};
    if (isKeyDown != nullptr)
        for (int k = 0; k < 128; ++k)
            nowHeld[static_cast<std::size_t> (k)] = isKeyDown (k);
    const bool changed = ! (now == settings) || ! (nowCurve == curve) || std::abs (nowAmount - amount) > 1.0e-6 || nowHeld != held;
    if (! changed)
        return;
    if (! (now == settings))
    {
        settings = now;
        recompute();
    }
    if (! dragging)
        curve = nowCurve;
    amount = nowAmount;
    held = nowHeld;
    repaint();
}

void KeyDnaEditor::showParameter (keydna::Param p)
{
    shownParam = p;
    curve = host.keyDnaCurve (layerIndex, p);
    repaint();
}

//==============================================================================
double KeyDnaEditor::value (const juce::String& field) const
{
    const auto& ps = settings[shownParam];
    if (field == "amount")
        return amount;
    if (field == "individuality")
        return settings.individuality;
    if (field == "seed")
        return static_cast<double> (settings.seed);
    if (field == "depth")
        return ps.curveDepth;
    if (field == "variation")
        return ps.variation;
    return 0.0;
}

void KeyDnaEditor::setValue (const juce::String& field, double v, bool final)
{
    if (field == "amount")
    {
        // The host parameter: one gesture (automation and undo see it as the host does).
        // (A drag's gesture opens on the press and closes on the release.)
        if (amountParameter == nullptr)
            return;
        const bool own = dragField == nullptr;
        if (own)
            amountParameter->beginChangeGesture();
        amountParameter->setValueNotifyingHost (amountParameter->convertTo0to1 (static_cast<float> (100.0 * std::clamp (v, 0.0, 1.0))));
        if (own)
            amountParameter->endChangeGesture();
        juce::ignoreUnused (final);
        amount = std::clamp (v, 0.0, 1.0);
        repaint();
        return;
    }
    auto s = settings;
    juce::String name = "KEY DNA";
    if (field == "individuality")
    {
        s.individuality = std::clamp (v, 0.0, 1.0);
        name = "KEY DNA individuality";
    }
    else if (field == "seed")
    {
        s.seed = static_cast<std::uint64_t> (std::clamp (std::llround (v), 1LL, 999999999LL));
        name = "KEY DNA seed";
    }
    else if (field == "depth" || field == "variation")
    {
        auto& ps = s[shownParam];
        (field == "depth" ? ps.curveDepth : ps.variation) = field == "depth" ? std::clamp (v, -1.0, 1.0) : std::clamp (v, 0.0, 1.0);
        name = "KEY DNA " + juce::String (keydna::paramName (shownParam)) + " " + field;
        s.enabled = true;   // shaping it means hearing it
    }
    else
        return;
    settings = s;
    recompute();
    host.setKeyDnaSettings (layerIndex, s, name, final);
    repaint();
}

juce::String KeyDnaEditor::fieldText (const Field& f) const
{
    const double v = value (f.name);
    if (f.name == "seed")
        return "#" + juce::String (static_cast<juce::int64> (settings.seed % 100000));
    if (f.name == "depth")
        return (v > 0.0 ? "+" : "") + percent (v);
    if (f.name == "individuality")
        return v < 0.15 ? "SMOOTH" : (v > 0.85 ? "INDIVIDUAL" : percent (v));
    return percent (v);
}

const KeyDnaEditor::Field* KeyDnaEditor::fieldAt (juce::Point<float> p) const
{
    for (const auto& f : fields)
        if (f.area.contains (p))
            return &f;
    return nullptr;
}

juce::Rectangle<float> KeyDnaEditor::controlArea (const juce::String& name) const
{
    if (name == "power")
        return power;
    if (name == "randomize")
        return randomize;
    if (name == "reset")
        return reset;
    if (name == "close")
        return closeArea;
    for (int p = 0; p < keydna::paramCount; ++p)
        if (name == "param." + juce::String (keydna::paramId (static_cast<keydna::Param> (p))))
            return tabs[static_cast<std::size_t> (p)];
    for (const auto& f : fields)
        if (f.name == name)
            return f.area;
    return {};
}

//==============================================================================
float KeyDnaEditor::xForNote (double n) const
{
    // The keyboard's own key centres where they are shown; linear beyond its ends.
    auto centre = [this] (int key) -> float {
        if (keyBounds != nullptr)
            if (const auto r = keyBounds (key); ! r.isEmpty())
                return r.getCentreX();
        const float w = graph.getWidth() / static_cast<float> (highestKey - lowestKey + 1);
        return graph.getX() + (static_cast<float> (key - lowestKey) + 0.5f) * w;
    };
    const float lo = centre (lowestKey), hi = centre (highestKey);
    const float step = (hi - lo) / static_cast<float> (highestKey - lowestKey);
    if (n <= lowestKey)
        return lo - static_cast<float> (lowestKey - n) * step;
    if (n >= highestKey)
        return hi + static_cast<float> (n - highestKey) * step;
    const int k = static_cast<int> (std::floor (n));
    const float a = centre (k), b = centre (std::min (highestKey, k + 1));
    return a + static_cast<float> (n - k) * (b - a);
}

double KeyDnaEditor::noteForX (float x) const
{
    const float lo = xForNote (lowestKey), hi = xForNote (highestKey);
    if (x <= lo || x >= hi)
        return lowestKey + static_cast<double> (x - lo) / std::max (1.0e-3, static_cast<double> (hi - lo)) * (highestKey - lowestKey);
    // Monotone between the shown keys: a short bisection on the key centres.
    double a = lowestKey, b = highestKey;
    for (int i = 0; i < 24; ++i)
    {
        const double m = 0.5 * (a + b);
        (xForNote (m) < x ? a : b) = m;
    }
    return 0.5 * (a + b);
}

double KeyDnaEditor::shownOffset (int note) const
{
    return settings.enabled ? static_cast<double> (table.offset (note, shownParam)) * amount : 0.0;
}

juce::Point<float> KeyDnaEditor::toScreen (float x, float y) const
{
    // x over the keys (note / 127; the off-keyboard ends pinned to the graph's edges), y up.
    const float px = std::clamp (xForNote (static_cast<double> (x) * 127.0), graph.getX(), graph.getRight());
    return { px, graph.getBottom() - y * graph.getHeight() };
}

juce::Point<float> KeyDnaEditor::fromScreen (juce::Point<float> p) const
{
    return { static_cast<float> (std::clamp (noteForX (p.x) / 127.0, 0.0, 1.0)),
             std::clamp ((graph.getBottom() - p.y) / std::max (1.0f, graph.getHeight()), 0.0f, 1.0f) };
}

int KeyDnaEditor::pointAt (juce::Point<float> p) const
{
    int best = -1;
    float distance = 9.0f;
    for (int i = 0; i < curve.count; ++i)
    {
        const auto& pt = curve.points[static_cast<std::size_t> (i)];
        if (const float d = toScreen (pt.x, pt.y).getDistanceFrom (p); d < distance)
        {
            distance = d;
            best = i;
        }
    }
    return best;
}

//==============================================================================
void KeyDnaEditor::resized()
{
    const float w = static_cast<float> (getWidth()), h = static_cast<float> (getHeight());
    header = { 0.0f, 0.0f, w, 64.0f };
    closeArea = { w - 50.0f, 18.0f, 30.0f, 30.0f };
    // The layer's KEY DNA, right of the identity: ON, AMOUNT, INDIVIDUALITY, SEED, RANDOMIZE.
    float x = 300.0f;
    power = { x, 18.0f, 58.0f, 32.0f };
    x = power.getRight() + 26.0f;
    auto place = [this] (const juce::String& name, juce::Rectangle<float> r) {
        for (auto& f : fields)
            if (f.name == name)
                f.area = r;
    };
    place ("amount", { x, 10.0f, 112.0f, 46.0f });
    x += 124.0f;
    place ("individuality", { x, 10.0f, 132.0f, 46.0f });
    x += 144.0f;
    place ("seed", { x, 10.0f, 92.0f, 46.0f });
    x += 100.0f;
    randomize = { x, 22.0f, 104.0f, 30.0f };

    // The parameters, then the shown one's DEPTH and VARIATION and its reset.
    const float tabWidth = 78.0f;
    for (int p = 0; p < keydna::paramCount; ++p)
        tabs[static_cast<std::size_t> (p)] = { 24.0f + static_cast<float> (p) * (tabWidth + 6.0f), 84.0f, tabWidth, 30.0f };
    const float right = w - 24.0f;
    reset = { right - 84.0f, 84.0f, 84.0f, 30.0f };
    place ("variation", { reset.getX() - 14.0f - 118.0f, 76.0f, 118.0f, 46.0f });
    place ("depth", { reset.getX() - 14.0f - 2.0f * 118.0f - 12.0f, 76.0f, 118.0f, 46.0f });

    // The graph over the keys: as wide as the keyboard below, with room for its labels.
    float left = 24.0f, end = w - 24.0f;
    if (keyBounds != nullptr)
    {
        const auto a = keyBounds (lowestKey), b = keyBounds (highestKey);
        if (! a.isEmpty() && ! b.isEmpty())
        {
            left = a.getX();
            end = b.getRight();
        }
    }
    const float overviewHeight = 7.0f * 19.0f;
    note = { 24.0f, h - 30.0f, w - 48.0f, 20.0f };
    overview = { left, note.getY() - 14.0f - overviewHeight, end - left, overviewHeight };
    const float top = 170.0f;   // under the parameter row and the well's key labels
    graph = { left, top, end - left, overview.getY() - 34.0f - top };
}

void KeyDnaEditor::drawKey (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& text, bool on, bool hover, juce::Colour accent)
{
    using namespace design;
    const auto state = on ? skin::KeyState::active : (hover ? skin::KeyState::hover : skin::KeyState::normal);
    if (! skin::key (g, r, 7.0f, skin::KeyRole::generic, state))
        draw::button (g, r, 7.0f, on, hover, accent);
    g.setFont (type::button (13.0f));
    g.setColour (on ? (skin::drawnBySurface() ? colour::text : juce::Colours::white) : colour::text.withAlpha (0.72f));
    g.drawText (text, r, juce::Justification::centred, false);
}

void KeyDnaEditor::paint (juce::Graphics& g)
{
    using namespace design;
    const auto bounds = getLocalBounds().toFloat();
    draw::raised (g, bounds, layout::cardRadius, colour::housingTop, colour::housingBottom, 1.6f);
    const auto& id = colour::identity (layerIndex);

    // The identity, as the Library writes it, and what this page is.
    g.setColour (colour::text);
    g.setFont (fonts::make (30.0f, fonts::Weight::displayBold, 0.01f));
    g.drawText ("ANDOR/OSP", 24, 10, 220, 34, juce::Justification::centredLeft);
    g.setColour (colour::textMicro);
    g.setFont (type::popupTitle (12.0f));
    g.drawText ("KEY DNA " + juce::String::fromUTF8 ("\xc2\xb7") + " LAYER " + InstrumentHost::layerName (layerIndex), 26, 42, 240, 14,
                juce::Justification::centredLeft);

    drawKey (g, power, settings.enabled ? "ON" : "OFF", settings.enabled, hoverControl == "power", id.arc);
    for (const auto& f : fields)
    {
        if (f.area.isEmpty())
            continue;
        const bool dimmed = ! settings.enabled && f.name != "amount";
        g.setFont (type::popupLabel (11.0f));
        g.setColour (colour::textSecondary.withAlpha (dimmed ? 0.6f : 1.0f));
        g.drawText (f.caption, f.area.withHeight (16.0f), juce::Justification::centredLeft, false);
        const auto box = f.area.withTrimmedTop (18.0f);
        g.setColour (colour::panelBottom.darker (hoverControl == f.name ? 0.08f : 0.04f));
        g.fillRoundedRectangle (box, 5.0f);
        g.setColour (colour::hairline.darker (0.15f));
        g.drawRoundedRectangle (box.reduced (0.5f), 5.0f, 1.0f);
        g.setFont (type::popupValue (14.0f));
        g.setColour (colour::text.withAlpha (dimmed ? 0.5f : 0.9f));
        g.drawText (fieldText (f), box, juce::Justification::centred, false);
    }
    drawKey (g, randomize, "RANDOMIZE", false, hoverControl == "randomize", id.arc);
    {
        const auto x = closeArea.reduced (9.0f);
        g.setColour (colour::textSecondary);
        g.drawLine (x.getX(), x.getY(), x.getRight(), x.getBottom(), 1.5f);
        g.drawLine (x.getRight(), x.getY(), x.getX(), x.getBottom(), 1.5f);
    }
    g.setColour (colour::divider);
    g.fillRect (juce::Rectangle<float> (24.0f, 68.0f, bounds.getWidth() - 48.0f, 1.0f));

    // The parameters (a dot: it moves something), the shown one's depth, variation, reset.
    for (int p = 0; p < keydna::paramCount; ++p)
    {
        const auto param = static_cast<keydna::Param> (p);
        const auto& ps = settings[param];
        const bool shown = param == shownParam;
        const auto r = tabs[static_cast<std::size_t> (p)];
        drawKey (g, r, keydna::paramName (param), shown, hoverControl == "param." + juce::String (keydna::paramId (param)), id.arc);
        if (! juce::exactlyEqual (ps.curveDepth, 0.0) || ! juce::exactlyEqual (ps.variation, 0.0))
            draw::led (g, { r.getRight() - 9.0f, r.getY() + 8.0f }, 5.0f, id.led, settings.enabled ? 0.6f : 0.0f);
    }
    drawKey (g, reset, "RESET", false, hoverControl == "reset", id.arc);

    paintGraph (g);
    paintOverview (g);

    // What it is not (yet): honest about the modulation bay.
    g.setFont (type::annotation (12.0f));
    g.setColour (colour::textMicro);
    g.drawText ("AMOUNT is a host parameter: automate it in the DAW. The modulation bay's destinations are unchanged in this build, so it is not a modulation target.",
                note, juce::Justification::centredLeft, true);
}

void KeyDnaEditor::paintGraph (juce::Graphics& g)
{
    using namespace design;
    const auto& id = colour::identity (layerIndex);
    const auto well = graph.expanded (10.0f, 26.0f).withTrimmedBottom (8.0f);
    draw::well (g, well, 9.0f, colour::wellA);
    const float mid = graph.getCentreY();
    const float half = 0.5f * graph.getHeight();

    // The keys' grid (C of every octave) and the centre line (no offset).
    g.setFont (type::popupLabel (10.5f));
    for (int k = 24; k <= highestKey; k += 12)
    {
        const float x = xForNote (k);
        g.setColour (colour::wellGrid.withAlpha (0.9f));
        g.fillRect (juce::Rectangle<float> (x, graph.getY(), 1.0f, graph.getHeight()));
        g.setColour (colour::wellText);
        g.drawText (juce::String (midiNoteName (k)), juce::Rectangle<float> (x - 20.0f, well.getY() + 4.0f, 40.0f, 14.0f), juce::Justification::centred, false);
    }
    g.setColour (colour::wellGrid.brighter (0.25f));
    g.fillRect (juce::Rectangle<float> (graph.getX(), mid, graph.getWidth(), 1.0f));
    g.setColour (colour::wellText);
    g.drawText (juce::String (keydna::paramName (shownParam)) + "  " + rangeText (shownParam) + " at full depth",
                juce::Rectangle<float> (graph.getX(), well.getBottom() - 20.0f, graph.getWidth(), 14.0f), juce::Justification::centredLeft, false);

    // Under every key, the offset it plays (the computed profile, AMOUNT applied).
    for (int k = lowestKey; k <= highestKey; ++k)
    {
        const bool black = juce::MidiMessage::isMidiNoteBlack (k);
        float x = xForNote (k), width = 7.0f;
        if (keyBounds != nullptr)
            if (const auto r = keyBounds (k); ! r.isEmpty())
                width = std::max (2.0f, r.getWidth() * (black ? 0.55f : 0.42f));
        const double off = shownOffset (k);
        const float height = static_cast<float> (off) * half * 0.94f;
        const bool on = held[static_cast<std::size_t> (k)];
        auto bar = juce::Rectangle<float> (x - 0.5f * width, height > 0.0f ? mid - height : mid, width, std::max (1.0f, std::abs (height)));
        g.setColour ((black ? id.arc.darker (0.25f) : id.arc).withAlpha (on ? 1.0f : (settings.enabled ? 0.62f : 0.22f)));
        g.fillRoundedRectangle (bar, std::min (2.0f, 0.5f * width));
        if (on)
        {
            g.setColour (juce::Colours::white.withAlpha (0.9f));
            g.fillEllipse (juce::Rectangle<float> (5.0f, 5.0f).withCentre ({ x, height > 0.0f ? bar.getY() - 6.0f : bar.getBottom() + 6.0f }));
        }
    }

    // The curve, drawn exactly as compiled, over the shown keys; its points to drag.
    const auto compiled = keydna::curveFrom (curve);
    juce::Path line;
    const int columns = std::max (2, juce::roundToInt (graph.getWidth() / 2.0f));
    for (int c = 0; c <= columns; ++c)
    {
        const float x = graph.getX() + graph.getWidth() * static_cast<float> (c) / static_cast<float> (columns);
        const double n = std::clamp (noteForX (x), 0.0, 127.0);
        const float y = graph.getBottom() - static_cast<float> (mod::curveAt (compiled, n / 127.0)) * graph.getHeight();
        c == 0 ? line.startNewSubPath (x, y) : line.lineTo (x, y);
    }
    const bool heard = settings.enabled && ! juce::exactlyEqual (settings[shownParam].curveDepth, 0.0);
    g.setColour (juce::Colour (0xffeedfc8).withAlpha (heard ? 0.95f : 0.45f));
    g.strokePath (line, juce::PathStrokeType (heard ? 2.0f : 1.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    for (int i = 0; i < curve.count; ++i)
    {
        const auto& pt = curve.points[static_cast<std::size_t> (i)];
        const auto at = toScreen (pt.x, pt.y);
        const bool hot = i == dragPoint;
        g.setColour (colour::wellA);
        g.fillEllipse (juce::Rectangle<float> (13.0f, 13.0f).withCentre (at));
        g.setColour (juce::Colour (0xffeedfc8).withAlpha (heard ? 1.0f : 0.6f));
        g.fillEllipse (juce::Rectangle<float> (hot ? 11.0f : 9.0f, hot ? 11.0f : 9.0f).withCentre (at));
    }
    if (! settings.enabled)
    {
        g.setFont (type::popupLabel (12.5f));
        g.setColour (colour::wellText.brighter (0.2f));
        g.drawText ("KEY DNA is off: every key plays the layer's own POS, SIZE, DENS, SPREAD and TUNE", graph.withHeight (22.0f).translated (0.0f, 8.0f),
                    juce::Justification::centred, false);
    }
    else if (juce::exactlyEqual (settings[shownParam].curveDepth, 0.0) && juce::exactlyEqual (settings[shownParam].variation, 0.0))
    {
        g.setFont (type::popupLabel (12.5f));
        g.setColour (colour::wellText.brighter (0.2f));
        g.drawText ("Draw a curve or raise VARIATION: " + juce::String (keydna::paramName (shownParam)) + " then differs from key to key",
                    graph.withHeight (22.0f).translated (0.0f, 8.0f), juce::Justification::centred, false);
    }
}

void KeyDnaEditor::paintOverview (juce::Graphics& g)
{
    using namespace design;
    // All seven parameters at a glance, one thin row each, under the same keys.
    const auto& id = colour::identity (layerIndex);
    const float rowHeight = overview.getHeight() / static_cast<float> (keydna::paramCount);
    g.setFont (type::popupLabel (10.0f));
    for (int p = 0; p < keydna::paramCount; ++p)
    {
        const auto param = static_cast<keydna::Param> (p);
        const auto row = juce::Rectangle<float> (overview.getX(), overview.getY() + static_cast<float> (p) * rowHeight, overview.getWidth(), rowHeight);
        const bool shown = param == shownParam;
        g.setColour (colour::textMicro.withAlpha (shown ? 1.0f : 0.7f));
        g.drawText (keydna::paramName (param), juce::Rectangle<float> (row.getX() - 70.0f, row.getY(), 62.0f, row.getHeight()), juce::Justification::centredRight,
                    false);
        g.setColour (colour::divider.withAlpha (0.6f));
        g.fillRect (juce::Rectangle<float> (row.getX(), row.getCentreY(), row.getWidth(), 1.0f));
        if (! settings.enabled)
            continue;
        for (int k = lowestKey; k <= highestKey; ++k)
        {
            const float off = table.offset (k, param) * static_cast<float> (amount);
            if (std::abs (off) < 1.0e-4f)
                continue;
            const float x = xForNote (k);
            const float hgt = off * 0.5f * (rowHeight - 3.0f);
            g.setColour (id.arc.withAlpha (shown ? 0.95f : 0.55f));
            g.fillRect (juce::Rectangle<float> (x - 1.5f, hgt > 0.0f ? row.getCentreY() - hgt : row.getCentreY(), 3.0f, std::max (1.0f, std::abs (hgt))));
        }
    }
}

//==============================================================================
void KeyDnaEditor::mouseMove (const juce::MouseEvent& e)
{
    juce::String now;
    if (power.contains (e.position))
        now = "power";
    else if (randomize.contains (e.position))
        now = "randomize";
    else if (reset.contains (e.position))
        now = "reset";
    else if (const auto* f = fieldAt (e.position))
        now = f->name;
    for (int p = 0; p < keydna::paramCount; ++p)
        if (tabs[static_cast<std::size_t> (p)].contains (e.position))
            now = "param." + juce::String (keydna::paramId (static_cast<keydna::Param> (p)));
    if (now != hoverControl)
    {
        hoverControl = now;
        repaint();
    }
    setMouseCursor (now.isNotEmpty() || closeArea.contains (e.position) ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
    setTooltip (now == "individuality" ? "Smooth: neighbouring keys are alike. Individual: every key its own."
                : now == "depth"       ? "How far the curve moves this parameter across the keys"
                : now == "variation"   ? "How far each key's own character moves it (from the seed)"
                : now == "seed"        ? "Each seed gives every key another character; a session keeps it"
                                       : juce::String());
}

void KeyDnaEditor::mouseDown (const juce::MouseEvent& e)
{
    grabKeyboardFocus();
    const auto p = e.position;
    dragField = nullptr;
    dragging = false;
    dragPoint = dragSegment = -1;
    if (closeArea.contains (p))
    {
        if (onClose != nullptr)
            onClose();
        return;
    }
    if (power.contains (p))
    {
        auto s = settings;
        s.enabled = ! s.enabled;
        settings = s;
        recompute();
        host.setKeyDnaSettings (layerIndex, s, s.enabled ? "KEY DNA on" : "KEY DNA off", true);
        repaint();
        return;
    }
    if (randomize.contains (p))
    {
        // Another seed, decided from this one (reproducible: the session stores the result).
        const auto next = Prng::deriveSeed (settings.seed, 0x6b657964ull, 1) % 99999ull + 1ull;
        setValue ("seed", static_cast<double> (next), true);
        return;
    }
    if (reset.contains (p))
    {
        // The shown parameter back to neutral: flat, no depth, no variation (one undo step).
        mod::Curve flat;
        flat.count = 2;
        flat.points[0] = { 0.0f, 0.5f, 0.0f };
        flat.points[1] = { 1.0f, 0.5f, 0.0f };
        host.setKeyDnaCurve (layerIndex, shownParam, flat, false);
        auto s = host.keyDnaSettings (layerIndex);
        s[shownParam].curveDepth = 0.0;
        s[shownParam].variation = 0.0;
        host.setKeyDnaSettings (layerIndex, s, "KEY DNA " + juce::String (keydna::paramName (shownParam)) + " reset", true);
        settings = s;
        curve = flat;
        recompute();
        repaint();
        return;
    }
    for (int i = 0; i < keydna::paramCount; ++i)
        if (tabs[static_cast<std::size_t> (i)].contains (p))
        {
            showParameter (static_cast<keydna::Param> (i));
            return;
        }
    if (const auto* f = fieldAt (p))
    {
        dragField = f;
        dragFrom = value (f->name);
        dragStart = p;
        dragging = true;
        if (f->name == "amount" && amountParameter != nullptr)
            amountParameter->beginChangeGesture();
        return;
    }
    if (! graph.expanded (8.0f).contains (p))
        return;
    curve = host.keyDnaCurve (layerIndex, shownParam);
    dragPoint = pointAt (p);
    if (dragPoint < 0)
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
    if (dragging && (juce::exactlyEqual (settings[shownParam].curveDepth, 0.0) || ! settings.enabled))
    {
        // A curve drawn is a curve heard: half its range, in the same undo step.
        auto s = settings;
        s.enabled = true;
        if (juce::exactlyEqual (s[shownParam].curveDepth, 0.0))
            s[shownParam].curveDepth = 0.5;
        settings = s;
        recompute();
        host.setKeyDnaSettings (layerIndex, s, "KEY DNA curve", false);
    }
}

void KeyDnaEditor::mouseDrag (const juce::MouseEvent& e)
{
    if (! dragging)
        return;
    if (dragField != nullptr)
    {
        const double fine = e.mods.isShiftDown() ? 0.15 : 1.0;
        const double span = dragField->maximum - dragField->minimum;
        const double delta = static_cast<double> (dragStart.y - e.position.y) / 180.0 * fine;
        const double v = dragField->name == "seed" ? dragFrom + std::round (delta * 40.0) : dragFrom + delta * span;
        setValue (dragField->name, std::clamp (v, dragField->minimum, dragField->maximum), false);
        return;
    }
    if (dragPoint >= 0)
    {
        auto& point = curve.points[static_cast<std::size_t> (dragPoint)];
        const auto p = fromScreen (e.position);
        if (dragPoint > 0 && dragPoint < curve.count - 1)
            point.x = juce::jlimit (curve.points[static_cast<std::size_t> (dragPoint - 1)].x + 0.005f, curve.points[static_cast<std::size_t> (dragPoint + 1)].x - 0.005f,
                                    p.x);
        point.y = p.y;
    }
    else if (dragSegment > 0)
    {
        const auto& a = curve.points[static_cast<std::size_t> (dragSegment - 1)];
        auto& b = curve.points[static_cast<std::size_t> (dragSegment)];
        const float direction = b.y >= a.y ? 1.0f : -1.0f;   // up is more here
        b.tension = juce::jlimit (-1.0f, 1.0f, dragStartTension + direction * (e.position.y - dragStart.y) / 80.0f);
    }
    host.setKeyDnaCurve (layerIndex, shownParam, curve, false);   // heard while dragging; one undo step on release
    settings = host.keyDnaSettings (layerIndex);
    recompute();
    repaint();
}

void KeyDnaEditor::mouseUp (const juce::MouseEvent&)
{
    if (dragging && dragField != nullptr)
    {
        if (dragField->name == "amount")
        {
            if (amountParameter != nullptr)
                amountParameter->endChangeGesture();
        }
        else if (! juce::exactlyEqual (value (dragField->name), dragFrom))
            setValue (dragField->name, value (dragField->name), true);
    }
    else if (dragging)
    {
        host.setKeyDnaCurve (layerIndex, shownParam, curve, true);
        settings = host.keyDnaSettings (layerIndex);
        recompute();
    }
    dragging = false;
    dragField = nullptr;
    dragPoint = dragSegment = -1;
    repaint();
}

void KeyDnaEditor::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (const auto* f = fieldAt (e.position))
    {
        setValue (f->name, f->reset, true);   // its default
        return;
    }
    if (! graph.expanded (6.0f).contains (e.position))
        return;
    auto next = host.keyDnaCurve (layerIndex, shownParam);
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
    host.setKeyDnaCurve (layerIndex, shownParam, next, true);
    curve = host.keyDnaCurve (layerIndex, shownParam);
    settings = host.keyDnaSettings (layerIndex);
    recompute();
    repaint();
}

bool KeyDnaEditor::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress::escapeKey)
    {
        if (onClose != nullptr)
            onClose();
        return true;
    }
    if (key == juce::KeyPress::leftKey || key == juce::KeyPress::rightKey)
    {
        const int p = (static_cast<int> (shownParam) + (key == juce::KeyPress::rightKey ? 1 : keydna::paramCount - 1)) % keydna::paramCount;
        showParameter (static_cast<keydna::Param> (p));
        return true;
    }
    return false;
}

} // namespace osp::plugin
