#include "ModulationPanel.h"

#include "EngineCard.h"
#include "OspLookAndFeel.h"

#include <cmath>
#include <optional>

namespace osp::plugin
{

namespace
{
    juce::RangedAudioParameter& parameter (OspAudioProcessor& p, const juce::String& id)
    {
        auto* ranged = p.parameters.getParameter (id);
        jassert (ranged != nullptr);
        return *ranged;
    }

    const std::array<const char*, 4> sourceLabels { "LFO 1", "LFO 2", "ENV 1", "ENV 2" };

    juce::String sourceParameter (int source, const juce::String& name)
    {
        return source < 2 ? OspAudioProcessor::modLfoId (source, name) : OspAudioProcessor::modEnvId (source - 2, name);
    }

    mod::LfoSettings lfoSettings (const OspAudioProcessor& p, int i)
    {
        // The same reading as the processor's (applyModulation), so the picture is the sound.
        mod::LfoSettings s;
        auto value = [&p, i] (const char* name) { return p.parameterValue (OspAudioProcessor::modLfoId (i, name)); };
        s.shape = static_cast<mod::LfoShape> (std::clamp (juce::roundToInt (value ("shape")), 0, mod::lfoShapeCount - 1));
        s.rateHz = value ("rate");
        s.sync = value ("sync") >= 0.5f;
        s.division = std::clamp (juce::roundToInt (value ("division")), 0, mod::syncDivisionCount - 1);
        s.phase = 0.01 * value ("phase");
        s.bipolar = value ("polarity") < 0.5f;
        s.mode = static_cast<mod::LfoMode> (std::clamp (juce::roundToInt (value ("mode")), 0, 2));
        s.scope = value ("scope") >= 0.5f ? mod::Scope::poly : mod::Scope::global;
        return s;
    }

    mod::EnvSettings envSettings (const OspAudioProcessor& p, int i)
    {
        mod::EnvSettings s;
        auto value = [&p, i] (const char* name) { return p.parameterValue (OspAudioProcessor::modEnvId (i, name)); };
        s.oneShotCurve = value ("mode") >= 0.5f;
        s.attackSeconds = 0.001 * value ("attack");
        s.decaySeconds = 0.001 * value ("decay");
        s.sustain = 0.01 * value ("sustain");
        s.releaseSeconds = 0.001 * value ("release");
        s.curve = 0.01 * value ("curve");
        s.lengthSeconds = 0.001 * value ("length");
        return s;
    }

    bool isPolySource (const OspAudioProcessor& p, int source)
    {
        return source >= 2 || p.parameterValue (OspAudioProcessor::modLfoId (source, "scope")) >= 0.5f;
    }

    bool isBipolarSource (const OspAudioProcessor& p, int source)
    {
        return source < 2 && p.parameterValue (OspAudioProcessor::modLfoId (source, "polarity")) < 0.5f;
    }

    mod::Dest offsetDest (mod::Dest first, int layer)
    {
        return static_cast<mod::Dest> (static_cast<int> (first) + layer);
    }

    /** Every control that is a destination (its parameter ID), in a stable order. */
    const juce::StringArray& destinationParameters()
    {
        static const juce::StringArray ids = [] {
            juce::StringArray list { "life", "drive", "character", "motion", "space", "character.resonance", "attack", "decay", "sustainLevel", "release" };
            for (int l = 0; l < OspAudioProcessor::numLayers; ++l)
            {
                list.add (OspAudioProcessor::reimaginedParameterId (l));
                for (const char* name : { "level", "pan", "tune", "granular.position", "granular.density", "granular.size", "granular.spread" })
                    list.add (OspAudioProcessor::layerParameterId (l, name));
                for (const char* name : { "bell.frequency", "bell.gain", "lowShelf.gain", "highShelf.gain" })
                    list.add (OspAudioProcessor::eqParameterId (l, name));
            }
            return list;
        }();
        return ids;
    }

    juce::String signedPercent (double v)
    {
        const int i = juce::roundToInt (v);
        return (i > 0 ? "+" : "") + juce::String (i) + "%";
    }
}

//==============================================================================
namespace modui
{
    juce::Colour sourceColour (int source)
    {
        static const std::array<juce::Colour, 4> colours { juce::Colour (0xff7f9a7a), juce::Colour (0xff7189a3), juce::Colour (0xffc49548), juce::Colour (0xffb06a4f) };
        return colours[static_cast<std::size_t> (juce::jlimit (0, 3, source))];
    }

    const char* sourceLabel (int source)
    {
        return sourceLabels[static_cast<std::size_t> (juce::jlimit (0, 3, source))];
    }

    int sourceOfTab (int tab) noexcept
    {
        // AMP, ENV 1, ENV 2, LFO 1, LFO 2 on screen; the sources' order is LFO 1, LFO 2, ENV 1, ENV 2.
        static constexpr std::array<int, tabCount> map { -1, 2, 3, 0, 1 };
        return map[static_cast<std::size_t> (std::clamp (tab, 0, tabCount - 1))];
    }

    int tabOfSource (int source) noexcept
    {
        static constexpr std::array<int, 4> map { 3, 4, 1, 2 };
        return source < 0 ? 0 : map[static_cast<std::size_t> (std::min (source, 3))];
    }

    juce::Colour sourceOnDark (int source)
    {
        return sourceColour (source).withMultipliedBrightness (1.38f).withMultipliedSaturation (1.05f);
    }

    mod::Dest destinationFor (const juce::String& id, bool polySource)
    {
        using mod::Dest;
        if (id == "life") return Dest::life;
        if (id == "drive") return Dest::drive;
        // The CHARACTER macro is a shared stage (one value) for a global source; a per-voice
        // source reaches the same filter per note, as its cutoff.
        if (id == "character") return polySource ? Dest::cutoff : Dest::character;
        if (id == "motion") return Dest::movement;
        if (id == "space") return Dest::space;
        if (id == "character.resonance") return Dest::resonance;
        if (id == "attack") return Dest::ampAttack;
        if (id == "decay") return Dest::ampDecay;
        if (id == "sustainLevel") return Dest::ampSustain;
        if (id == "release") return Dest::ampRelease;
        for (int l = 0; l < OspAudioProcessor::numLayers; ++l)
        {
            if (id == OspAudioProcessor::reimaginedParameterId (l)) return offsetDest (Dest::reimaginedA, l);
            if (id == OspAudioProcessor::layerParameterId (l, "level")) return offsetDest (Dest::levelA, l);
            if (id == OspAudioProcessor::layerParameterId (l, "pan")) return offsetDest (Dest::panA, l);
            if (id == OspAudioProcessor::layerParameterId (l, "tune")) return offsetDest (Dest::fineTuneA, l);
            if (id == OspAudioProcessor::layerParameterId (l, "granular.position")) return offsetDest (Dest::grainPositionA, l);
            if (id == OspAudioProcessor::layerParameterId (l, "granular.density")) return offsetDest (Dest::grainDensityA, l);
            if (id == OspAudioProcessor::layerParameterId (l, "granular.size")) return offsetDest (Dest::grainSizeA, l);
            if (id == OspAudioProcessor::layerParameterId (l, "granular.spread")) return offsetDest (Dest::grainSpreadA, l);
            if (id == OspAudioProcessor::eqParameterId (l, "bell.frequency")) return offsetDest (Dest::eqBellFrequencyA, l);
            if (id == OspAudioProcessor::eqParameterId (l, "bell.gain")) return offsetDest (Dest::eqBellGainA, l);
            if (id == OspAudioProcessor::eqParameterId (l, "lowShelf.gain")) return offsetDest (Dest::eqLowShelfGainA, l);
            if (id == OspAudioProcessor::eqParameterId (l, "highShelf.gain")) return offsetDest (Dest::eqHighShelfGainA, l);
        }
        return Dest::none;
    }

    std::vector<mod::Dest> destinationsShownBy (const juce::String& id)
    {
        if (id == "character")
            return { mod::Dest::character, mod::Dest::cutoff };
        const auto d = destinationFor (id, false);
        if (d == mod::Dest::none)
            return {};
        return { d };
    }

    juce::String parameterFor (mod::Dest dest)
    {
        for (const auto& id : destinationParameters())
            for (auto d : destinationsShownBy (id))
                if (d == dest)
                    return id;
        return {};
    }
}

//==============================================================================
// The display

ModCurveView::ModCurveView (OspAudioProcessor& p) : ospProcessor (p)
{
    setTitle ("Modulation shape");
}

void ModCurveView::setSource (int s)
{
    if (s == sourceIndex)
        return;
    sourceIndex = s;
    dragging = false;
    dragPoint = dragSegment = hoverPoint = -1;
    curve = ospProcessor.modulationCurve (curveIndex());
    repaint();
}

bool ModCurveView::isEditable() const
{
    if (sourceIndex < 2)
        return juce::roundToInt (ospProcessor.parameterValue (OspAudioProcessor::modLfoId (sourceIndex, "shape"))) == static_cast<int> (mod::LfoShape::custom);
    return ospProcessor.parameterValue (OspAudioProcessor::modEnvId (sourceIndex - 2, "mode")) >= 0.5f;
}

void ModCurveView::setCompact (bool c)
{
    compact = c;
    repaint();
}

juce::Rectangle<float> ModCurveView::plotArea() const
{
    if (compact)
        return getLocalBounds().toFloat().reduced (12.0f, 0.0f).withTrimmedTop (17.0f).withTrimmedBottom (7.0f);
    return getLocalBounds().toFloat().reduced (18.0f, 0.0f).withTrimmedTop (38.0f).withTrimmedBottom (34.0f);
}

juce::Rectangle<float> ModCurveView::polarityArea() const
{
    // The LFO's polarity, as a small tag at the display's top right (compact): a click asks
    // for the menu.
    if (! compact || sourceIndex >= 2)
        return {};
    const auto r = getLocalBounds().toFloat();
    return { r.getRight() - 92.0f, 2.0f, 84.0f, 15.0f };
}

juce::Rectangle<float> ModCurveView::resetArea() const
{
    const auto r = getLocalBounds().toFloat();
    return { r.getRight() - 70.0f, r.getBottom() - 28.0f, 56.0f, 20.0f };
}

juce::Point<float> ModCurveView::toScreen (float x, float y) const
{
    // A curve point's y (0..1) spans the whole plot in either polarity: CUSTOM's table is the
    // shape itself (bipolar -1..1 or unipolar 0..1), the one-shot envelope's its level.
    const auto plot = plotArea();
    return { plot.getX() + x * plot.getWidth(), plot.getBottom() - y * plot.getHeight() };
}

juce::Point<float> ModCurveView::fromScreen (juce::Point<float> p) const
{
    const auto plot = plotArea();
    return { juce::jlimit (0.0f, 1.0f, (p.x - plot.getX()) / plot.getWidth()), juce::jlimit (0.0f, 1.0f, (plot.getBottom() - p.y) / plot.getHeight()) };
}

int ModCurveView::pointAt (juce::Point<float> p) const
{
    int best = -1;
    float bestDistance = 9.0f;
    for (int i = 0; i < curve.count; ++i)
    {
        const auto& point = curve.points[static_cast<std::size_t> (i)];
        const float d = toScreen (point.x, point.y).getDistanceFrom (p);
        if (d < bestDistance)
        {
            bestDistance = d;
            best = i;
        }
    }
    return best;
}

void ModCurveView::refresh()
{
    // Repaint only when what is drawn changed: the sourceIndex's settings, its curve, the playhead.
    std::array<float, 24> signature {};
    std::size_t n = 0;
    if (sourceIndex < 2)
        for (const char* name : { "shape", "rate", "sync", "division", "phase", "polarity", "mode", "scope" })
            signature[n++] = ospProcessor.parameterValue (OspAudioProcessor::modLfoId (sourceIndex, name));
    else
        for (const char* name : { "mode", "attack", "decay", "sustain", "release", "curve", "length" })
            signature[n++] = ospProcessor.parameterValue (OspAudioProcessor::modEnvId (sourceIndex - 2, name));
    signature[n++] = ospProcessor.parameterValue ("seed");
    bool changed = signature != shownSignature;
    shownSignature = signature;
    if (! dragging)
    {
        const auto stored = ospProcessor.modulationCurve (curveIndex());
        if (! (stored == curve))
        {
            curve = stored;
            changed = true;
        }
    }
    const auto view = ospProcessor.modulationView();
    float playhead = -1.0f;   // LFO: its phase; envelope: stage x 100 + seconds in the stage
    if (sourceIndex < 2)
    {
        if (! isPolySource (ospProcessor, sourceIndex) || view.voice)
            playhead = view.lfoPhase[static_cast<std::size_t> (sourceIndex)];
    }
    else if (view.voice)
    {
        const auto i = static_cast<std::size_t> (sourceIndex - 2);
        playhead = view.envStage[i] >= 1 && view.envStage[i] <= 4 ? static_cast<float> (view.envStage[i]) * 100.0f + view.envTime[i] : -1.0f;
    }
    const float value = view.value[static_cast<std::size_t> (sourceIndex)];
    if (changed || std::abs (playhead - shownPlayhead) > 1.0e-4f || std::abs (value - shownValue) > 1.0e-4f)
    {
        shownPlayhead = playhead;
        shownValue = value;
        repaint();
    }
}

void ModCurveView::paint (juce::Graphics& g)
{
    using namespace design;
    const auto bounds = getLocalBounds().toFloat();
    draw::well (g, bounds, 12.0f, colour::wellA);
    const auto plot = plotArea();
    const auto colour = modui::sourceOnDark (sourceIndex);

    // A fine grid: eighths across, quarters stronger; the centre line.
    for (int i = 0; i <= 8; ++i)
    {
        const float x = plot.getX() + plot.getWidth() * static_cast<float> (i) / 8.0f;
        g.setColour (colour::wellGrid.withAlpha (i % 2 == 0 ? 0.95f : 0.5f));
        g.fillRect (juce::Rectangle<float> (x - 0.5f, plot.getY(), 1.0f, plot.getHeight()));
    }
    for (int i = 0; i <= 4; ++i)
    {
        const float y = plot.getY() + plot.getHeight() * static_cast<float> (i) / 4.0f;
        g.setColour (colour::wellGrid.withAlpha (i == 2 ? 0.95f : 0.5f));
        g.fillRect (juce::Rectangle<float> (plot.getX(), y - 0.5f, plot.getWidth(), 1.0f));
    }

    if (sourceIndex < 2)
        paintLfo (g, plot, colour);
    else
        paintEnvelope (g, plot, colour);

    if (isEditable())
        paintCurvePoints (g, colour);
    if (isEditable() && ! compact)
    {
        g.setFont (type::popupLabel (11.5f));
        g.setColour (colour::wellText.withAlpha (0.8f));
        g.drawText (juce::String::fromUTF8 ("Double-click: add / remove a point  \xc2\xb7  drag a line: bend"), bounds.reduced (18.0f, 8.0f).withTop (bounds.getBottom() - 28.0f),
                    juce::Justification::centredLeft, false);
        const auto reset = resetArea();
        g.setColour (colour::wellText.brighter (0.4f));
        g.drawRoundedRectangle (reset.reduced (0.5f), 5.0f, 1.0f);
        g.drawText ("RESET", reset, juce::Justification::centred, false);
    }
}

void ModCurveView::paintLfo (juce::Graphics& g, juce::Rectangle<float> plot, juce::Colour colour)
{
    using namespace design;
    const auto s = lfoSettings (ospProcessor, sourceIndex);
    const auto table = mod::compileCurve (curve);
    const auto seed = static_cast<std::uint64_t> (std::max (1.0f, ospProcessor.parameterValue ("seed")));
    auto yOf = [&] (double v) { return s.bipolar ? plot.getCentreY() - static_cast<float> (v) * 0.5f * plot.getHeight() : plot.getBottom() - static_cast<float> (v) * plot.getHeight(); };
    const float zero = s.bipolar ? plot.getCentreY() : plot.getBottom();

    // One cycle from phase 0, exactly the oscillator's output.
    juce::Path line, fill;
    constexpr int steps = 240;
    for (int i = 0; i <= steps; ++i)
    {
        const double x = static_cast<double> (i) / steps;
        const auto p = juce::Point<float> (plot.getX() + static_cast<float> (x) * plot.getWidth(), yOf (mod::lfoOutput (s, std::min (x, 0.99999), table, seed, 0)));
        if (i == 0)
        {
            line.startNewSubPath (p);
            fill.startNewSubPath (p.x, zero);
        }
        else
            line.lineTo (p);
        fill.lineTo (p);
    }
    fill.lineTo (plot.getRight(), zero);
    fill.closeSubPath();
    g.setGradientFill (juce::ColourGradient (colour.withAlpha (0.30f), 0.0f, plot.getY(), colour.withAlpha (0.04f), 0.0f, plot.getBottom(), false));
    g.fillPath (fill);
    g.setColour (colour);
    g.strokePath (line, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // Where RETRIGGER and ONE SHOT start.
    if (s.mode != mod::LfoMode::free || s.phase > 0.0)
    {
        const float x = plot.getX() + static_cast<float> (s.phase) * plot.getWidth();
        g.setColour (colour.withAlpha (0.55f));
        juce::Path tick;
        tick.addTriangle (x - 4.0f, plot.getBottom() + 9.0f, x + 4.0f, plot.getBottom() + 9.0f, x, plot.getBottom() + 3.0f);
        g.fillPath (tick);
    }

    // The playhead: the real oscillator (global, or the newest note's for a per-voice LFO).
    if (shownPlayhead >= 0.0f)
    {
        const float x = plot.getX() + shownPlayhead * plot.getWidth();
        g.setColour (juce::Colours::white.withAlpha (0.35f));
        g.fillRect (juce::Rectangle<float> (x - 0.5f, plot.getY(), 1.0f, plot.getHeight()));
        draw::led (g, { x, yOf (shownValue) }, 7.0f, colour.brighter (0.25f), 0.8f);
    }

    // Labels: what it is, how fast, how it runs (compact: one slim line inside the display).
    g.setFont (type::popupLabel (compact ? 10.5f : 12.5f));
    g.setColour (colour);
    const auto top = compact ? getLocalBounds().toFloat().reduced (12.0f, 0.0f).withY (2.0f).withHeight (15.0f)
                             : getLocalBounds().toFloat().reduced (18.0f, 0.0f).withHeight (36.0f);
    g.drawText (juce::String (sourceLabels[static_cast<std::size_t> (sourceIndex)]) + "  " + mod::lfoShapeName (s.shape), top, juce::Justification::centredLeft, false);
    const juce::String rate = s.sync ? juce::String (mod::syncName (s.division)) : format::hertz (s.rateHz);
    const char* modes[] { "FREE", "RETRIG", "ONE SHOT" };
    g.setColour (colour::wellText.brighter (0.3f));
    const auto dot = juce::String::fromUTF8 ("  \xc2\xb7  ");
    if (compact)
    {
        // The polarity tag (a door to the menu): what the curve is drawn around.
        g.setColour (colour::wellText.brighter (hoverPoint == -2 ? 0.7f : 0.3f));
        g.drawText (juce::String::fromUTF8 (s.bipolar ? "\xc2\xb1 BIPOLAR" : "+ UNIPOLAR"), polarityArea(), juce::Justification::centredRight, false);
    }
    else
        g.drawText (rate + dot + modes[static_cast<int> (s.mode)] + dot + (s.scope == mod::Scope::poly ? "POLY" : "GLOBAL"), top, juce::Justification::centredRight, false);
    if (s.scope == mod::Scope::poly && shownPlayhead < 0.0f && ! compact)
    {
        g.setColour (colour::wellText.withAlpha (0.7f));
        g.drawText ("per note: plays with each note", plot.withTrimmedTop (plot.getHeight() - 22.0f), juce::Justification::centredRight, false);
    }
}

void ModCurveView::paintEnvelope (juce::Graphics& g, juce::Rectangle<float> plot, juce::Colour colour)
{
    using namespace design;
    const auto s = envSettings (ospProcessor, sourceIndex - 2);
    const auto top = compact ? getLocalBounds().toFloat().reduced (12.0f, 0.0f).withY (2.0f).withHeight (15.0f)
                             : getLocalBounds().toFloat().reduced (18.0f, 0.0f).withHeight (36.0f);
    g.setFont (type::popupLabel (compact ? 10.5f : 12.5f));
    g.setColour (colour);
    g.drawText (juce::String (sourceLabels[static_cast<std::size_t> (sourceIndex)]) + (s.oneShotCurve ? "  ONE SHOT" : "  ADSR"), top, juce::Justification::centredLeft, false);
    g.setColour (colour::wellText.brighter (0.3f));
    g.drawText (s.oneShotCurve ? format::milliseconds (1000.0 * s.lengthSeconds) + juce::String::fromUTF8 ("  \xc2\xb7  PER NOTE") : juce::String ("PER NOTE"), top,
                juce::Justification::centredRight, false);

    juce::Path line, fill;
    const auto stage = juce::roundToInt (std::floor (std::max (0.0f, shownPlayhead) / 100.0f));
    const float stageTime = shownPlayhead >= 0.0f ? shownPlayhead - 100.0f * static_cast<float> (stage) : 0.0f;
    float playX = -1.0f, playY = 0.0f;
    if (s.oneShotCurve)
    {
        const auto table = mod::compileCurve (curve);
        for (int i = 0; i <= 200; ++i)
        {
            const float x = static_cast<float> (i) / 200.0f;
            const auto p = toScreen (x, static_cast<float> (mod::curveAt (table, x)));
            if (i == 0)
            {
                line.startNewSubPath (p);
                fill.startNewSubPath (p.x, plot.getBottom());
            }
            else
                line.lineTo (p);
            fill.lineTo (p);
        }
        if (shownPlayhead >= 0.0f)
        {
            const float x = juce::jlimit (0.0f, 1.0f, stageTime / static_cast<float> (std::max (0.001, s.lengthSeconds)));
            playX = plot.getX() + x * plot.getWidth();
            playY = toScreen (x, shownValue).y;
        }
    }
    else
    {
        // Attack, decay, a held sustain and release, in proportion to their times (the hold
        // a fixed share), each bent by CURVE exactly as the envelope moves.
        const double a = s.attackSeconds, d = s.decaySeconds, r = s.releaseSeconds;
        const double hold = std::max (0.15, 0.3 * (a + d + r));
        const double total = std::max (1.0e-3, a + d + hold + r);
        auto xOf = [&] (double t) { return plot.getX() + static_cast<float> (t / total) * plot.getWidth(); };
        auto yOf = [&] (double level) { return plot.getBottom() - static_cast<float> (level) * plot.getHeight(); };
        const double sustain = std::clamp (s.sustain, 0.0, 1.0);
        line.startNewSubPath (xOf (0.0), yOf (0.0));
        fill.startNewSubPath (xOf (0.0), plot.getBottom());
        fill.lineTo (xOf (0.0), yOf (0.0));
        constexpr int n = 60;
        for (int i = 1; i <= n; ++i)
        {
            const double x = static_cast<double> (i) / n;
            const auto p = juce::Point<float> (xOf (x * a), yOf (mod::shapeSegment (x, s.curve)));
            line.lineTo (p);
            fill.lineTo (p);
        }
        for (int i = 1; i <= n; ++i)
        {
            const double x = static_cast<double> (i) / n;
            const auto p = juce::Point<float> (xOf (a + x * d), yOf (1.0 + (sustain - 1.0) * mod::shapeSegment (x, -s.curve)));
            line.lineTo (p);
            fill.lineTo (p);
        }
        line.lineTo (xOf (a + d + hold), yOf (sustain));
        fill.lineTo (xOf (a + d + hold), yOf (sustain));
        for (int i = 1; i <= n; ++i)
        {
            const double x = static_cast<double> (i) / n;
            const auto p = juce::Point<float> (xOf (a + d + hold + x * r), yOf (sustain * (1.0 - mod::shapeSegment (x, -s.curve))));
            line.lineTo (p);
            fill.lineTo (p);
        }
        // Stage boundaries, faint.
        g.setColour (colour.withAlpha (0.22f));
        for (double t : { a, a + d, a + d + hold })
            g.fillRect (juce::Rectangle<float> (xOf (t) - 0.5f, plot.getY(), 1.0f, plot.getHeight()));
        if (shownPlayhead >= 0.0f)
        {
            double t = 0.0;
            switch (stage)
            {
                case 1: t = std::min<double> (stageTime, a); break;
                case 2: t = a + std::min<double> (stageTime, d); break;
                case 3: t = a + d + std::min<double> (stageTime, hold); break;
                case 4: t = a + d + hold + std::min<double> (stageTime, r); break;
                default: break;
            }
            playX = xOf (t);
            playY = yOf (shownValue);
        }
    }
    fill.lineTo (plot.getRight(), plot.getBottom());
    fill.closeSubPath();
    g.setGradientFill (juce::ColourGradient (colour.withAlpha (0.30f), 0.0f, plot.getY(), colour.withAlpha (0.04f), 0.0f, plot.getBottom(), false));
    g.fillPath (fill);
    g.setColour (colour);
    g.strokePath (line, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    if (playX >= 0.0f)
    {
        g.setColour (juce::Colours::white.withAlpha (0.35f));
        g.fillRect (juce::Rectangle<float> (playX - 0.5f, plot.getY(), 1.0f, plot.getHeight()));
        draw::led (g, { playX, playY }, 7.0f, colour.brighter (0.25f), 0.8f);
    }
}

void ModCurveView::paintCurvePoints (juce::Graphics& g, juce::Colour colour)
{
    for (int i = 0; i < curve.count; ++i)
    {
        const auto& point = curve.points[static_cast<std::size_t> (i)];
        const auto c = toScreen (point.x, point.y);
        const bool hot = i == hoverPoint || i == dragPoint;
        const float r = hot ? 5.5f : 4.0f;
        g.setColour (design::colour::wellA);
        g.fillEllipse (juce::Rectangle<float> (2.0f * r + 3.0f, 2.0f * r + 3.0f).withCentre (c));
        g.setColour (hot ? colour.brighter (0.3f) : colour);
        g.fillEllipse (juce::Rectangle<float> (2.0f * r, 2.0f * r).withCentre (c));
    }
}

void ModCurveView::mouseMove (const juce::MouseEvent& e)
{
    int now = isEditable() ? pointAt (e.position) : -1;
    if (now < 0 && polarityArea().contains (e.position))
        now = -2;   // the polarity tag
    setMouseCursor (now == -2 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
    if (now != hoverPoint)
    {
        hoverPoint = now;
        repaint();
    }
}

void ModCurveView::mouseExit (const juce::MouseEvent&)
{
    if (hoverPoint != -1)
    {
        hoverPoint = -1;
        repaint();
    }
}

void ModCurveView::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu() || polarityArea().contains (e.position))
    {
        if (onContextMenu != nullptr)
            onContextMenu();
        return;
    }
    if (! isEditable())
        return;
    curve = ospProcessor.modulationCurve (curveIndex());
    before = curve;
    if (resetArea().contains (e.position))
    {
        ospProcessor.setModulationCurve (curveIndex(), sourceIndex < 2 ? mod::defaultLfoCurve() : mod::defaultEnvCurve(), true);
        curve = ospProcessor.modulationCurve (curveIndex());
        repaint();
        return;
    }
    dragPoint = pointAt (e.position);
    dragSegment = -1;
    if (dragPoint < 0 && plotArea().expanded (4.0f).contains (e.position))
    {
        // A press on a line bends that segment (its end point's tension).
        const float x = fromScreen (e.position).x;
        for (int i = 1; i < curve.count; ++i)
            if (x <= curve.points[static_cast<std::size_t> (i)].x)
            {
                dragSegment = i;
                break;
            }
        if (dragSegment > 0)
            dragStartTension = curve.points[static_cast<std::size_t> (dragSegment)].tension;
    }
    dragStart = e.position;
    dragging = dragPoint >= 0 || dragSegment > 0;
}

void ModCurveView::mouseDrag (const juce::MouseEvent& e)
{
    if (! dragging)
        return;
    if (dragPoint >= 0)
    {
        auto& point = curve.points[static_cast<std::size_t> (dragPoint)];
        const auto p = fromScreen (e.position);
        // The ends stay at the cycle's edges; inner points keep their order.
        if (dragPoint > 0 && dragPoint < curve.count - 1)
            point.x = juce::jlimit (curve.points[static_cast<std::size_t> (dragPoint - 1)].x + 0.005f,
                                    curve.points[static_cast<std::size_t> (dragPoint + 1)].x - 0.005f, p.x);
        point.y = p.y;
    }
    else
    {
        // Up bends the segment's middle upwards, whichever way it runs.
        const auto& a = curve.points[static_cast<std::size_t> (dragSegment - 1)];
        auto& b = curve.points[static_cast<std::size_t> (dragSegment)];
        const float direction = b.y >= a.y ? 1.0f : -1.0f;
        b.tension = juce::jlimit (-1.0f, 1.0f, dragStartTension + direction * (e.position.y - dragStart.y) / 80.0f);
    }
    ospProcessor.setModulationCurve (curveIndex(), curve, false);   // heard while dragging; one undo step on release
    repaint();
}

void ModCurveView::mouseUp (const juce::MouseEvent&)
{
    if (dragging && ! (curve == before))
    {
        const auto edited = curve;
        ospProcessor.setModulationCurve (curveIndex(), before, false);
        ospProcessor.setModulationCurve (curveIndex(), edited, true);
    }
    dragging = false;
    dragPoint = dragSegment = -1;
    repaint();
}

void ModCurveView::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (! isEditable() || ! plotArea().expanded (6.0f).contains (e.position))
        return;
    auto next = ospProcessor.modulationCurve (curveIndex());
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
    ospProcessor.setModulationCurve (curveIndex(), next, true);
    curve = ospProcessor.modulationCurve (curveIndex());
    hoverPoint = -1;
    repaint();
}

//==============================================================================
// A source's controls (one row, the AMP envelope's knob row)

ModSourcePage::ModSourcePage (OspAudioProcessor& p, int s) : ospProcessor (p), source (s)
{
    auto& state = ospProcessor.parameters;
    const auto accent = modui::sourceColour (source);
    auto makeKnob = [&] (const char* name, const char* caption, MiniKnob::Formatter f) {
        auto knob = std::make_unique<MiniKnob> (state, sourceParameter (source, name), caption, std::move (f));
        knob->setArcColour (accent);
        addAndMakeVisible (*knob);
        return knob;
    };
    if (isLfo())
    {
        shape = std::make_unique<ValueSelector> (parameter (p, sourceParameter (source, "shape")), "SHAPE");
        mode = std::make_unique<ValueSelector> (parameter (p, sourceParameter (source, "mode")), "MODE");
        scope = std::make_unique<ValueSelector> (parameter (p, sourceParameter (source, "scope")), "VOICES");
        scope->setTooltip ("Global: one movement for the instrument. Poly: each note its own");
        for (auto* c : { shape.get(), mode.get(), scope.get() })
            addAndMakeVisible (c);
        // RATE: one knob in Hz, or (synced) over the musical divisions; its caption chooses.
        knobs.push_back (makeKnob ("rate", "RATE", [] (double v) { return format::hertz (v); }));
        division = makeKnob ("division", "RATE", [] (double v) { return juce::String (mod::syncName (juce::roundToInt (v))); });
        for (auto* k : { knobs[0].get(), division.get() })
        {
            juce::Component::SafePointer<ModSourcePage> safe (this);
            juce::Component::SafePointer<MiniKnob> knob (k);
            k->onCaptionClick = [safe, knob] {
                if (safe != nullptr && knob != nullptr)
                    safe->rateMenu().showMenuAsync (juce::PopupMenu::Options().withTargetComponent (knob.getComponent()));
            };
            k->slider.setTooltip ("Click RATE to choose Sync (musical divisions) or Hz");
        }
        knobs.push_back (makeKnob ("phase", "PHASE", [] (double v) { return juce::String (juce::roundToInt (3.6 * v)) + juce::String::fromUTF8 ("\xc2\xb0"); }));
    }
    else
    {
        mode = std::make_unique<ValueSelector> (parameter (p, sourceParameter (source, "mode")), "MODE");
        addAndMakeVisible (*mode);
        auto ms = [] (double v) { return format::milliseconds (v); };
        knobs.push_back (makeKnob ("attack", "ATTACK", ms));
        knobs.push_back (makeKnob ("decay", "DECAY", ms));
        knobs.push_back (makeKnob ("sustain", "SUSTAIN", [] (double v) { return juce::String (juce::roundToInt (v)) + " %"; }));
        knobs.push_back (makeKnob ("release", "RELEASE", ms));
        knobs.push_back (makeKnob ("curve", "CURVE", [] (double v) { return format::bipolar (v); }));
        knobs.push_back (makeKnob ("length", "LENGTH", ms));
    }
    refresh();
}

juce::PopupMenu ModSourcePage::rateMenu()
{
    // RATE's two ways (the lfoN.sync parameter): host-synced divisions or free Hz.
    juce::PopupMenu menu;
    if (! isLfo())
        return menu;
    menu.addSectionHeader ("RATE");
    const bool sync = ospProcessor.parameterValue (sourceParameter (source, "sync")) >= 0.5f;
    juce::Component::SafePointer<ModSourcePage> safe (this);
    auto set = [safe] (bool on) {
        if (safe == nullptr)
            return;
        auto& p = parameter (safe->ospProcessor, sourceParameter (safe->source, "sync"));
        safe->ospProcessor.undoManager.beginNewTransaction (on ? "LFO sync" : "LFO Hz");
        p.beginChangeGesture();
        p.setValueNotifyingHost (on ? 1.0f : 0.0f);
        p.endChangeGesture();
        safe->refresh();
    };
    menu.addItem ("Sync", true, sync, [set] { set (true); });
    menu.addItem ("Hz", true, ! sync, [set] { set (false); });
    return menu;
}

void ModSourcePage::refresh()
{
    const bool sync = isLfo() && ospProcessor.parameterValue (sourceParameter (source, "sync")) >= 0.5f;
    const bool oneShot = ! isLfo() && ospProcessor.parameterValue (sourceParameter (source, "mode")) >= 0.5f;
    if (sync == shownSync && oneShot == shownOneShot && laidOut)
        return;
    laidOut = true;
    shownSync = sync;
    shownOneShot = oneShot;
    if (isLfo())
    {
        knobs[0]->setVisible (! sync);   // RATE in Hz, or over the divisions when synced
        division->setVisible (sync);
    }
    else
    {
        for (std::size_t i = 0; i < 5; ++i)
            knobs[i]->setVisible (! oneShot);
        knobs[5]->setVisible (oneShot);
    }
    resized();
}

void ModSourcePage::resized()
{
    // The AMP envelope's row: cells across, knobs 87 high around the row's middle.
    const float w = static_cast<float> (getWidth()), h = static_cast<float> (getHeight());
    auto at = [] (juce::Rectangle<float> r) { return r.getSmallestIntegerContainer(); };
    const float cy = 0.5f * h;
    if (isLfo())
    {
        const float cell = w / 5.0f;
        auto centre = [cell] (int i) { return cell * (static_cast<float> (i) + 0.5f); };
        shape->setBounds (at (juce::Rectangle<float> (cell - 10.0f, 44.0f).withCentre ({ centre (0), cy })));
        mode->setBounds (at (juce::Rectangle<float> (cell - 10.0f, 44.0f).withCentre ({ centre (1), cy })));
        knobs[0]->setBounds (at (juce::Rectangle<float> (std::min (100.0f, cell), h).withCentre ({ centre (2), cy })));
        division->setBounds (knobs[0]->getBounds());
        knobs[1]->setBounds (at (juce::Rectangle<float> (std::min (100.0f, cell), h).withCentre ({ centre (3), cy })));
        scope->setBounds (at (juce::Rectangle<float> (cell - 10.0f, 44.0f).withCentre ({ centre (4), cy })));
        return;
    }
    const float cell = w / 6.0f;
    mode->setBounds (at (juce::Rectangle<float> (cell - 6.0f, 44.0f).withCentre ({ 0.5f * cell, cy })));
    for (std::size_t i = 0; i < 5; ++i)
        knobs[i]->setBounds (at (juce::Rectangle<float> (cell, h).withCentre ({ cell * (static_cast<float> (i) + 1.5f), cy })));
    knobs[5]->setBounds (at (juce::Rectangle<float> (cell, h).withCentre ({ 1.5f * cell, cy })));
}

//==============================================================================
// A route's depth

ModDepthBar::ModDepthBar (OspAudioProcessor& p, int slot, juce::Colour c)
    : colour (c),
      attachment (parameter (p, OspAudioProcessor::modRouteId (slot, "depth")), [this] (float v) {
          depth = v;
          repaint();
      }, &p.undoManager)
{
    attachment.sendInitialUpdate();
    setMouseCursor (juce::MouseCursor::LeftRightResizeCursor);
    setTitle ("Depth");
    setTooltip ("Depth: drag (double-click: 0). A negative depth turns the movement around");
}

void ModDepthBar::paint (juce::Graphics& g)
{
    using namespace design;
    const auto r = getLocalBounds().toFloat();
    const auto track = r.withTrimmedRight (44.0f).withSizeKeepingCentre (r.getWidth() - 44.0f, 6.0f);
    g.setColour (colour::knobTrack);
    g.fillRoundedRectangle (track, 3.0f);
    const float centre = track.getCentreX();
    const float end = centre + 0.5f * track.getWidth() * depth / 100.0f;
    g.setColour (colour);
    g.fillRoundedRectangle (juce::Rectangle<float> (std::min (centre, end), track.getY(), std::abs (end - centre), track.getHeight()), 3.0f);
    g.setColour (colour::text.withAlpha (0.35f));
    g.fillRect (juce::Rectangle<float> (centre - 0.5f, track.getY() - 2.0f, 1.0f, track.getHeight() + 4.0f));
    g.setFont (type::popupValue (13.0f));
    g.setColour (colour::text.withAlpha (0.88f));
    g.drawText (signedPercent (depth), r.withLeft (r.getRight() - 42.0f), juce::Justification::centredRight, false);
}

void ModDepthBar::mouseDown (const juce::MouseEvent&)
{
    dragFrom = depth;
    attachment.beginGesture();
}

void ModDepthBar::mouseDrag (const juce::MouseEvent& e)
{
    const float width = std::max (20.0f, static_cast<float> (getWidth()) - 44.0f);
    const float fine = e.mods.isAltDown() || e.mods.isShiftDown() ? 0.25f : 1.0f;
    attachment.setValueAsPartOfGesture (juce::jlimit (-100.0f, 100.0f, dragFrom + fine * 200.0f * static_cast<float> (e.getDistanceFromDragStartX()) / width));
}

void ModDepthBar::mouseUp (const juce::MouseEvent&)
{
    attachment.endGesture();
}

void ModDepthBar::mouseDoubleClick (const juce::MouseEvent&)
{
    attachment.setValueAsCompleteGesture (0.0f);
}

//==============================================================================
// The shared panel: AMP, ENV 1, ENV 2, LFO 1, LFO 2

namespace
{
    const std::array<const char*, modui::tabCount> tabNames { "AMP", "ENV 1", "ENV 2", "LFO 1", "LFO 2" };
    juce::Font tabFont() { return fonts::make (13.0f, fonts::Weight::medium, 0.08f); }
}

ModulationPanel::ModulationPanel (OspAudioProcessor& p) : ospProcessor (p), amp (p.parameters), curve (p)
{
    setTitle ("Envelope and modulation");
    amp.setShowsTitle (false);   // the tab row names it
    addAndMakeVisible (amp);
    curve.setCompact (true);
    addChildComponent (curve);
    for (int s = 0; s < 4; ++s)
    {
        pages[static_cast<std::size_t> (s)] = std::make_unique<ModSourcePage> (p, s);
        addChildComponent (*pages[static_cast<std::size_t> (s)]);
    }
    juce::Component::SafePointer<ModulationPanel> safe (this);
    curve.onContextMenu = [safe] {
        if (safe != nullptr && safe->selectedSource() >= 0)
            safe->sourceMenu (safe->selectedSource()).showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&safe->curve));
    };
    addAndMakeVisible (strip);
    strip.setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void ModulationPanel::showTab (int t)
{
    t = juce::jlimit (0, modui::tabCount - 1, t);
    if (t == currentTab)
        return;
    // Presentation only: every source keeps running, nothing is reset or restarted.
    currentTab = t;
    const int source = selectedSource();
    amp.setVisible (source < 0);
    curve.setVisible (source >= 0);
    if (source >= 0)
        curve.setSource (source);
    for (int s = 0; s < 4; ++s)
        pages[static_cast<std::size_t> (s)]->setVisible (s == source);
    if (source >= 0)
        pages[static_cast<std::size_t> (source)]->refresh();
    layoutTabs();
    strip.repaint();
    if (onTabChanged != nullptr)
        onTabChanged (currentTab);
}

int ModulationPanel::routeCount (int source, bool activeOnly) const
{
    int n = 0;
    for (const auto& r : ospProcessor.modulationRoutes())
        if (mod::sourceIndex (r.route.source) == source && (! activeOnly || r.state == mod::RouteState::active))
            ++n;
    return n;
}

juce::Rectangle<float> ModulationPanel::tabArea (int t) const
{
    return tabs[static_cast<std::size_t> (juce::jlimit (0, modui::tabCount - 1, t))];
}

void ModulationPanel::layoutTabs()
{
    // Small, quiet words across the panel's title line; the right end for the shown
    // source's route count and (an editable curve) the expand key.
    const auto font = tabFont();
    float x = 16.0f;
    for (int t = 0; t < modui::tabCount; ++t)
    {
        const float w = juce::GlyphArrangement::getStringWidth (font, tabNames[static_cast<std::size_t> (t)]) + 8.0f;
        tabs[static_cast<std::size_t> (t)] = { x - 4.0f, 0.0f, w, 24.0f };
        x += w + 10.0f;
    }
    const float right = static_cast<float> (getWidth()) - 14.0f;
    const int source = selectedSource();
    routesKey = expandKey = {};
    if (source >= 0)
    {
        const int n = routeCount (source, false);
        const auto text = n == 0 ? juce::String ("+ ROUTE") : juce::String (n) + (n == 1 ? " ROUTE" : " ROUTES");
        const float w = juce::GlyphArrangement::getStringWidth (fonts::make (11.5f, fonts::Weight::medium, 0.08f), text) + 10.0f;
        routesKey = { right - w, 3.0f, w, 18.0f };
        if (curve.isEditable())
            expandKey = { routesKey.getX() - 26.0f, 3.0f, 18.0f, 18.0f };
    }
}

void ModulationPanel::resized()
{
    const int w = getWidth(), h = getHeight();
    amp.setBounds (getLocalBounds());
    curve.setBounds (12, 26, w - 24, std::max (40, h - 116));   // the AMP display's place
    for (auto& page : pages)
        page->setBounds (0, h - 89, w, 87);                      // the AMP knob row's place
    strip.setBounds (0, 0, w, 24);
    layoutTabs();
}

void ModulationPanel::refresh()
{
    const int source = selectedSource();
    if (source >= 0)
    {
        curve.refresh();
        pages[static_cast<std::size_t> (source)]->refresh();
    }
    // The tabs' route dots and the route count follow the routes.
    std::array<int, 8> counts {};
    for (int s = 0; s < 4; ++s)
    {
        counts[static_cast<std::size_t> (s)] = routeCount (s, true);
        counts[static_cast<std::size_t> (4 + s)] = routeCount (s, false);
    }
    const bool editable = source >= 0 && curve.isEditable();
    if (counts != shownRouteCounts || editable != shownEditable)
    {
        shownRouteCounts = counts;
        shownEditable = editable;
        layoutTabs();
        strip.repaint();
    }
}

juce::PopupMenu ModulationPanel::sourceMenu (int source)
{
    juce::PopupMenu menu;
    juce::Component::SafePointer<ModulationPanel> safe (this);
    auto& p = ospProcessor;
    if (source < 2)
    {
        // Polarity: a secondary setting, here rather than a permanent switch.
        menu.addSectionHeader ("POLARITY");
        const bool bipolar = p.parameterValue (OspAudioProcessor::modLfoId (source, "polarity")) < 0.5f;
        auto set = [&p, source] (float v) {
            if (auto* param = p.parameters.getParameter (OspAudioProcessor::modLfoId (source, "polarity")))
            {
                p.undoManager.beginNewTransaction ("LFO polarity");
                param->beginChangeGesture();
                param->setValueNotifyingHost (param->convertTo0to1 (v));
                param->endChangeGesture();
            }
        };
        menu.addItem ("Bipolar", true, bipolar, [set] { set (0.0f); });
        menu.addItem ("Unipolar", true, ! bipolar, [set] { set (1.0f); });
        menu.addSeparator();
    }
    const bool editable = source < 2 ? juce::roundToInt (p.parameterValue (OspAudioProcessor::modLfoId (source, "shape"))) == static_cast<int> (mod::LfoShape::custom)
                                     : p.parameterValue (OspAudioProcessor::modEnvId (source - 2, "mode")) >= 0.5f;
    if (editable)
    {
        menu.addItem (source < 2 ? "Edit shape..." : "Edit curve...", [safe, source] { if (safe != nullptr && safe->onExpandCurve != nullptr) safe->onExpandCurve (source); });
        menu.addItem (source < 2 ? "Reset shape" : "Reset curve", [&p, source] {
            p.setModulationCurve (source, source < 2 ? mod::defaultLfoCurve() : mod::defaultEnvCurve(), true);
        });
        menu.addSeparator();
    }
    menu.addItem ("Routes...", [safe, source] { if (safe != nullptr && safe->onShowRoutes != nullptr) safe->onShowRoutes (source); });
    return menu;
}

void ModulationPanel::paintTabs (juce::Graphics& g)
{
    using namespace design;
    const auto font = tabFont();
    g.setFont (font);
    for (int t = 0; t < modui::tabCount; ++t)
    {
        const auto r = tabs[static_cast<std::size_t> (t)];
        const bool on = t == currentTab;
        const int source = modui::sourceOfTab (t);
        // Muted words; the shown one a step stronger with a fine underline in its colour.
        g.setColour (colour::text.withAlpha (on ? 0.92f : (t == hoverTab ? 0.72f : 0.5f)));
        g.drawText (tabNames[static_cast<std::size_t> (t)], r, juce::Justification::centred, false);
        if (on)
        {
            g.setColour (source < 0 ? colour::accent : modui::sourceColour (source));
            g.fillRoundedRectangle (r.reduced (4.0f, 0.0f).withY (r.getBottom() - 4.0f).withHeight (1.6f), 0.8f);
        }
        // A source in use: a tiny dot after its name.
        if (source >= 0 && shownRouteCounts[static_cast<std::size_t> (source)] > 0)
        {
            const float tw = juce::GlyphArrangement::getStringWidth (font, tabNames[static_cast<std::size_t> (t)]);
            draw::led (g, { r.getCentreX() + 0.5f * tw + 4.5f, r.getCentreY() - 4.0f }, 4.0f, modui::sourceColour (source), 0.0f);
        }
    }
    if (! routesKey.isEmpty())
    {
        const int n = shownRouteCounts[static_cast<std::size_t> (4 + selectedSource())];
        g.setFont (fonts::make (11.5f, fonts::Weight::medium, 0.08f));
        g.setColour (colour::text.withAlpha (hoverRoutes ? 0.9f : 0.55f));
        g.drawText (n == 0 ? juce::String ("+ ROUTE") : juce::String (n) + (n == 1 ? " ROUTE" : " ROUTES"), routesKey, juce::Justification::centredRight, false);
    }
    if (! expandKey.isEmpty())
    {
        // Expand: two small corner marks (the larger editor for the curve).
        const auto e = expandKey.reduced (4.0f);
        g.setColour (colour::text.withAlpha (hoverExpand ? 0.9f : 0.5f));
        juce::Path p;
        p.startNewSubPath (e.getX(), e.getY() + 4.0f);
        p.lineTo (e.getX(), e.getY());
        p.lineTo (e.getX() + 4.0f, e.getY());
        p.startNewSubPath (e.getRight() - 4.0f, e.getBottom());
        p.lineTo (e.getRight(), e.getBottom());
        p.lineTo (e.getRight(), e.getBottom() - 4.0f);
        g.strokePath (p, juce::PathStrokeType (1.3f));
    }
}

void ModulationPanel::tabMouseMove (juce::Point<float> at)
{
    int t = -1;
    for (int i = 0; i < modui::tabCount; ++i)
        if (tabs[static_cast<std::size_t> (i)].contains (at))
            t = i;
    const bool routes = routesKey.contains (at), expand = expandKey.contains (at);
    if (t != hoverTab || routes != hoverRoutes || expand != hoverExpand)
    {
        hoverTab = t;
        hoverRoutes = routes;
        hoverExpand = expand;
        strip.setMouseCursor (t >= 0 || routes || expand ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
        strip.repaint();
    }
}

void ModulationPanel::tabMouseDown (const juce::MouseEvent& e)
{
    pressAt = e.position;
    pressTab = -1;
    dragSource = -1;
    for (int i = 0; i < modui::tabCount; ++i)
        if (tabs[static_cast<std::size_t> (i)].contains (e.position))
            pressTab = i;
    if (e.mods.isPopupMenu())
    {
        // A source's tab: its menu (polarity, curve, routes).
        const int source = modui::sourceOfTab (pressTab);
        if (pressTab >= 0 && source >= 0)
            sourceMenu (source).showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&strip));
        pressTab = -1;
        return;
    }
    if (routesKey.contains (e.position) && onShowRoutes != nullptr)
        onShowRoutes (selectedSource());
    else if (expandKey.contains (e.position) && onExpandCurve != nullptr)
        onExpandCurve (selectedSource());
}

void ModulationPanel::tabMouseDrag (const juce::MouseEvent& e)
{
    // A source's tab dragged past a small threshold becomes a cable (a click never does).
    const int source = modui::sourceOfTab (pressTab);
    if (pressTab < 0 || source < 0)
        return;
    if (dragSource < 0)
    {
        if (e.position.getDistanceFrom (pressAt) < 6.0f)
            return;
        dragSource = source;
    }
    strip.setMouseCursor (juce::MouseCursor::DraggingHandCursor);
    if (onDragMove != nullptr)
        onDragMove (dragSource, e.getScreenPosition());
}

void ModulationPanel::tabMouseUp (const juce::MouseEvent& e)
{
    if (dragSource >= 0)
    {
        if (onDragEnd != nullptr)
            onDragEnd (dragSource, e.getScreenPosition());
    }
    else if (pressTab >= 0 && tabs[static_cast<std::size_t> (pressTab)].contains (e.position))
        showTab (pressTab);
    dragSource = -1;
    pressTab = -1;
    strip.setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

//==============================================================================
// A source's routes (a popover)

ModRoutesPopup::Row::Row (ModRoutesPopup& o, const OspAudioProcessor::ModRouteInfo& i)
    : owner (o), info (i), depth (o.ospProcessor, i.slot, modui::sourceColour (mod::sourceIndex (i.route.source)))
{
    addAndMakeVisible (depth);
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

juce::Rectangle<float> ModRoutesPopup::Row::bypassArea() const
{
    return { static_cast<float> (getWidth()) - 50.0f, 0.5f * static_cast<float> (getHeight()) - 9.0f, 18.0f, 18.0f };
}

juce::Rectangle<float> ModRoutesPopup::Row::removeArea() const
{
    return { static_cast<float> (getWidth()) - 24.0f, 0.5f * static_cast<float> (getHeight()) - 9.0f, 18.0f, 18.0f };
}

void ModRoutesPopup::Row::resized()
{
    depth.setBounds (juce::Rectangle<float> (static_cast<float> (getWidth()) - 160.0f, 0.0f, 100.0f, static_cast<float> (getHeight())).getSmallestIntegerContainer());
}

void ModRoutesPopup::Row::paint (juce::Graphics& g)
{
    using namespace design;
    const auto r = getLocalBounds().toFloat();
    const int s = mod::sourceIndex (info.route.source);
    const bool selectedRow = owner.selectedSlot == info.slot;
    const bool live = info.state == mod::RouteState::active;
    if (selectedRow || isMouseOver (true))
    {
        g.setColour (selectedRow ? colour::accentSoft.withAlpha (0.45f) : colour::text.withAlpha (0.04f));
        g.fillRoundedRectangle (r.reduced (1.0f, 2.0f), 7.0f);
    }
    g.setColour (colour::hairline);
    g.fillRect (juce::Rectangle<float> (6.0f, r.getBottom() - 1.0f, r.getWidth() - 12.0f, 1.0f));
    const float dim = live ? 1.0f : 0.45f;
    g.setFont (type::popupLabel (12.5f));
    g.setColour (info.state == mod::RouteState::scope ? colour::accent.withAlpha (0.8f) : colour::text.withAlpha (0.86f * dim));
    g.drawFittedText (juce::String::fromUTF8 ("\xe2\x86\x92 ") + mod::destInfo (info.route.dest).name,
                      juce::Rectangle<float> (10.0f, 0.0f, r.getWidth() - 10.0f - 166.0f, r.getHeight()).getSmallestIntegerContainer(), juce::Justification::centredLeft, 1, 0.8f);
    const auto bypass = bypassArea();
    g.setColour (colour::hairline.darker (0.15f));
    g.drawEllipse (bypass.reduced (2.5f), 1.0f);
    if (info.route.enabled)
        draw::led (g, bypass.getCentre(), 7.0f, modui::sourceColour (s), 0.5f);
    const auto remove = removeArea().reduced (5.0f);
    const bool hot = isMouseOver() && removeArea().contains (getMouseXYRelative().toFloat());
    g.setColour (colour::text.withAlpha (hot ? 0.8f : 0.4f));
    g.drawLine (remove.getX(), remove.getY(), remove.getRight(), remove.getBottom(), 1.3f);
    g.drawLine (remove.getRight(), remove.getY(), remove.getX(), remove.getBottom(), 1.3f);
}

void ModRoutesPopup::Row::mouseDown (const juce::MouseEvent& e)
{
    auto& p = owner.ospProcessor;
    if (bypassArea().expanded (3.0f).contains (e.position))
    {
        auto& enabled = parameter (p, OspAudioProcessor::modRouteId (info.slot, "enabled"));
        p.undoManager.beginNewTransaction (info.route.enabled ? "Bypass modulation" : "Enable modulation");
        enabled.beginChangeGesture();
        enabled.setValueNotifyingHost (info.route.enabled ? 0.0f : 1.0f);
        enabled.endChangeGesture();
        return;
    }
    if (removeArea().expanded (3.0f).contains (e.position))
    {
        const int slot = info.slot;
        juce::Component::SafePointer<ModRoutesPopup> safe (&owner);
        // The row is rebuilt (deleted) by the change: act after this event.
        juce::MessageManager::callAsync ([safe, slot] {
            if (safe != nullptr)
            {
                safe->ospProcessor.removeModulationRoute (slot);
                safe->refreshContent();
            }
        });
        return;
    }
    if (owner.onSelectRoute != nullptr)
        owner.onSelectRoute (info.slot);
    owner.setSelectedSlot (info.slot);
}

ModRoutesPopup::ModRoutesPopup (OspAudioProcessor& p, int s)
    : MiniPanel (juce::String (modui::sourceLabel (s)) + " ROUTES"), ospProcessor (p), sourceIndex (s)
{
    rebuild();
}

void ModRoutesPopup::setSelectedSlot (int slot)
{
    selectedSlot = slot;
    for (auto& row : rows)
        row->repaint();
}

juce::Point<int> ModRoutesPopup::cardSize() const
{
    const int n = std::max (1, static_cast<int> (rows.size()));
    return { 400, 32 + 34 * std::min (n, 8) + 44 };
}

void ModRoutesPopup::rebuild()
{
    std::vector<OspAudioProcessor::ModRouteInfo> mine;
    for (const auto& r : ospProcessor.modulationRoutes())
        if (mod::sourceIndex (r.route.source) == sourceIndex)
            mine.push_back (r);
    juce::String signature;
    for (const auto& r : mine)
        signature << r.slot << ":" << static_cast<int> (r.route.dest) << ";";
    if (signature == shownRoutes && ! rows.empty())
    {
        // Same routes: keep the rows (a depth being dragged stays), follow their state.
        for (std::size_t i = 0; i < rows.size() && i < mine.size(); ++i)
            if (rows[i]->info.state != mine[i].state || rows[i]->info.route.enabled != mine[i].route.enabled)
            {
                rows[i]->info = mine[i];
                rows[i]->repaint();
            }
        return;
    }
    const bool resizedCard = (rows.size() != mine.size());
    shownRoutes = signature;
    rows.clear();
    for (const auto& r : mine)
    {
        rows.push_back (std::make_unique<Row> (*this, r));
        addAndMakeVisible (*rows.back());
    }
    if (resizedCard && onSizeChanged != nullptr)
        onSizeChanged();
    resized();
    repaint();
}

void ModRoutesPopup::refreshContent()
{
    rebuild();
}

void ModRoutesPopup::layoutContent (juce::Rectangle<int> area)
{
    auto r = area.toFloat();
    addKey = r.removeFromBottom (32.0f).withWidth (82.0f).withTrimmedTop (4.0f);
    listArea = r;
    float y = r.getY();
    for (auto& row : rows)
    {
        row->setBounds (juce::Rectangle<float> (r.getX(), y, r.getWidth(), 34.0f).getSmallestIntegerContainer());
        row->setVisible (y + 34.0f <= r.getBottom() + 1.0f);
        y += 34.0f;
    }
}

void ModRoutesPopup::paint (juce::Graphics& g)
{
    using namespace design;
    MiniPanel::paint (g);
    draw::led (g, { card().toFloat().getRight() - 22.0f, card().toFloat().getY() + 17.0f }, 7.0f, modui::sourceColour (sourceIndex), 0.0f);
    if (rows.empty())
    {
        g.setFont (type::popupLabel (12.5f));
        g.setColour (colour::textMicro);
        g.drawText ("No routes yet: drag the tab onto a knob, or add one.", listArea, juce::Justification::centred, false);
    }
    draw::button (g, addKey, 7.0f, false, false, colour::accent);
    g.setFont (type::popupLabel (12.5f));
    g.setColour (colour::text.withAlpha (0.86f));
    g.drawText ("+ ADD", addKey, juce::Justification::centred, false);
}

juce::PopupMenu ModRoutesPopup::addMenu()
{
    // SOURCE -> DESTINATION without dragging (precise, hidden controls, accessibility).
    juce::PopupMenu menu;
    juce::Component::SafePointer<ModRoutesPopup> safe (this);
    const auto source = static_cast<mod::Source> (sourceIndex + 1);
    auto add = [&] (juce::PopupMenu& sub, mod::Dest d) {
        sub.addItem (mod::destInfo (d).name, ospProcessor.canModulate (source, d), false, [safe, source, d] {
            if (safe == nullptr)
                return;
            const int slot = safe->ospProcessor.addModulationRoute (source, d, 50.0f);
            safe->refreshContent();
            if (slot >= 0 && safe->onSelectRoute != nullptr)
                safe->onSelectRoute (slot);
            safe->setSelectedSlot (slot);
        });
    };
    juce::PopupMenu macros, character, ampEnv;
    for (auto d : { mod::Dest::life, mod::Dest::drive, mod::Dest::character, mod::Dest::movement, mod::Dest::space })
        add (macros, d);
    add (character, mod::Dest::cutoff);
    add (character, mod::Dest::resonance);
    for (auto d : { mod::Dest::ampAttack, mod::Dest::ampDecay, mod::Dest::ampSustain, mod::Dest::ampRelease })
        add (ampEnv, d);
    menu.addSubMenu ("Macros", macros);
    menu.addSubMenu ("Character", character);
    menu.addSubMenu ("Amp envelope", ampEnv);
    for (int l = 0; l < OspAudioProcessor::numLayers; ++l)
    {
        juce::PopupMenu layer;
        for (auto first : { mod::Dest::levelA, mod::Dest::panA, mod::Dest::fineTuneA, mod::Dest::reimaginedA, mod::Dest::grainPositionA,
                            mod::Dest::grainDensityA, mod::Dest::grainSizeA, mod::Dest::grainSpreadA, mod::Dest::eqBellFrequencyA,
                            mod::Dest::eqBellGainA, mod::Dest::eqLowShelfGainA, mod::Dest::eqHighShelfGainA })
            add (layer, offsetDest (first, l));
        menu.addSubMenu ("Layer " + OspAudioProcessor::layerName (l), layer);
    }
    return menu;
}

void ModRoutesPopup::mouseDown (const juce::MouseEvent& e)
{
    if (addKey.contains (e.position))
        addMenu().showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea (localAreaToGlobal (addKey.getSmallestIntegerContainer())));
}

//==============================================================================
// The larger curve editor (a popover)

ModCurvePopup::ModCurvePopup (OspAudioProcessor& p, int s)
    : MiniPanel (juce::String (modui::sourceLabel (s)) + (s < 2 ? " CUSTOM SHAPE" : " ONE SHOT CURVE")), view (p)
{
    view.setSource (s);
    addAndMakeVisible (view);
}

//==============================================================================
// The halos

namespace
{
    /** A parameter's value (normalised) as its own text, with its unit when the text lacks it. */
    juce::String valueText (juce::RangedAudioParameter& p, float normalised)
    {
        auto text = p.getText (juce::jlimit (0.0f, 1.0f, normalised), 0);
        const auto label = p.getLabel();
        if (label.isNotEmpty() && ! text.containsIgnoreCase (label.trim()))
            text << " " << label;
        return text;
    }
}

ModulationOverlay::ModulationOverlay (OspAudioProcessor& p, juce::Component& r) : ospProcessor (p), root (r)
{
    setInterceptsMouseClicks (true, false);
    setTitle ("Modulation halos");
}

void ModulationOverlay::collect (juce::Component& c)
{
    for (auto* child : c.getChildren())
    {
        if (child == this || ! child->isVisible())
            continue;
        if (auto* slider = dynamic_cast<juce::Slider*> (child))
        {
            const auto id = slider->getProperties()["paramId"].toString();
            if (id.isNotEmpty() && ! modui::destinationsShownBy (id).empty())
            {
                Target t;
                t.slider = slider;
                t.parameterId = id;
                const auto area = root.getLocalArea (slider, slider->getLocalBounds()).toFloat();
                t.area = area;
                t.field = static_cast<bool> (slider->getProperties()["modField"]);   // a value field: an underline, not a ring
                const float side = std::min (area.getWidth(), area.getHeight());
                const auto& props = slider->getProperties();
                // The knob's own outermost mark (ticks, or its value arc), as OspLookAndFeel
                // draws it; the halo sits clear outside it, with a visible gap.
                float reach = 0.0f, gap = 0.0f;
                if (static_cast<bool> (props["popup"]))
                {
                    reach = 1.29f * 0.5f * side / 1.36f;
                    gap = 6.0f;
                }
                else if (! static_cast<bool> (props["mini"]) && ! static_cast<bool> (props["noTicks"]))
                {
                    reach = 1.37f * 0.5f * side / 1.42f;
                    gap = std::clamp (0.1f * reach, 5.0f, 7.0f);
                }
                else
                {
                    reach = 1.13f * 0.5f * side / 1.2f + 1.5f;
                    gap = 4.5f;
                }
                // A knob with its caption close above (a card's knobs) keeps its halo nearer,
                // and the ring breaks around the caption's text.
                if (props.contains ("haloGap"))
                    gap = static_cast<float> (static_cast<double> (props["haloGap"]));
                if (props.contains ("haloAvoid"))
                {
                    const auto avoid = juce::Rectangle<float>::fromString (props["haloAvoid"].toString());
                    if (! avoid.isEmpty())
                        t.avoid = avoid.translated (area.getX(), area.getY()).expanded (4.0f, 2.0f);
                }
                t.bodyRadius = reach;
                t.radius = reach + gap;
                t.centre = area.getCentre();
                const auto rotary = slider->getRotaryParameters();
                t.start = rotary.startAngleRadians;
                t.end = rotary.endAngleRadians;
                targets.push_back (t);
            }
        }
        collect (*child);
    }
}

std::vector<const OspAudioProcessor::ModRouteInfo*> ModulationOverlay::routesOf (const Target& t) const
{
    const auto dests = modui::destinationsShownBy (t.parameterId);
    std::vector<const OspAudioProcessor::ModRouteInfo*> list;
    for (const auto& r : routes)
        if (std::find (dests.begin(), dests.end(), r.route.dest) != dests.end())
            list.push_back (&r);
    return list;
}

const OspAudioProcessor::ModRouteInfo* ModulationOverlay::emphasised (const Target& t) const
{
    // The one route the halo shows and edits: the selected route, else the selected source's,
    // else the first.
    const auto list = routesOf (t);
    if (list.empty())
        return nullptr;
    for (const auto* r : list)
        if (r->slot == selectedSlot)
            return r;
    for (const auto* r : list)
        if (mod::sourceIndex (r->route.source) == selectedSource)
            return r;
    return list.front();
}

float ModulationOverlay::positionOf (const Target& t, double offset, mod::Dest dest) const
{
    // The knob position (0..1) of base + offset, offset in the destination's own units
    // (span x contribution), mapped the way the engine applies it and clamped where the
    // parameter ends.
    auto* param = ospProcessor.parameters.getParameter (t.parameterId);
    if (param == nullptr)
        return 0.0f;
    const auto& range = param->getNormalisableRange();
    const double base = param->convertFrom0to1 (param->getValue());
    const double x = mod::destInfo (dest).span * offset;
    const double length = static_cast<double> (range.end - range.start);
    double value = base;
    switch (mod::destInfo (dest).domain)
    {
        case mod::Domain::unit:
        case mod::Domain::pan: value = base + x * (length > 2.5 ? 100.0 : 1.0); break;
        case mod::Domain::decibels: value = base + x; break;
        case mod::Domain::cents: value = base + x / 100.0; break;   // the knob is in semitones
        case mod::Domain::octaves:
            // CHARACTER's cutoff on the CHARACTER macro: its travel spans about four octaves.
            value = t.parameterId == "character" ? base + 100.0 * x / 4.0 : (base > 0.0 ? base * std::exp2 (x) : base);
            break;
    }
    value = std::clamp (value, static_cast<double> (range.start), static_cast<double> (range.end));
    return param->convertTo0to1 (static_cast<float> (value));
}

ModulationOverlay::Sweep ModulationOverlay::sweepOf (const Target& t, const OspAudioProcessor::ModRouteInfo& r) const
{
    Sweep s;
    auto* param = ospProcessor.parameters.getParameter (t.parameterId);
    s.base = param != nullptr ? param->getValue() : 0.0f;
    const int source = mod::sourceIndex (r.route.source);
    const double d = r.route.depth;
    const bool bipolar = isBipolarSource (ospProcessor, source);
    // A bipolar source swings both ways around the base; a unipolar one (and an envelope)
    // only towards the depth's sign. The handle is where the source at its top takes it.
    s.lo = positionOf (t, bipolar ? -std::abs (d) : std::min (0.0, d), r.route.dest);
    s.hi = positionOf (t, bipolar ? std::abs (d) : std::max (0.0, d), r.route.dest);
    s.handle = positionOf (t, d, r.route.dest);
    if (r.state == mod::RouteState::active && (! isPolySource (ospProcessor, source) || view.voice))
    {
        s.now = positionOf (t, d * static_cast<double> (view.value[static_cast<std::size_t> (source)]), r.route.dest);
        s.nowKnown = true;
    }
    return s;
}

const ModulationOverlay::Target* ModulationOverlay::find (const juce::String& id) const
{
    for (auto it = targets.rbegin(); it != targets.rend(); ++it)
        if (it->parameterId == id && it->slider != nullptr)
            return &*it;
    return nullptr;
}

juce::Point<float> ModulationOverlay::haloCentre (const juce::String& id) const
{
    const auto* t = find (id);
    return t != nullptr ? t->centre : juce::Point<float>();
}

float ModulationOverlay::haloRadius (const juce::String& id) const
{
    const auto* t = find (id);
    return t != nullptr && ! routesOf (*t).empty() ? t->radius : 0.0f;
}

int ModulationOverlay::haloRoute (const juce::String& id) const
{
    const auto* t = find (id);
    const auto* r = t != nullptr ? emphasised (*t) : nullptr;
    return r != nullptr ? r->slot : -1;
}

juce::StringArray ModulationOverlay::haloReadout (const juce::String& id) const
{
    juce::StringArray lines;
    const auto* t = find (id);
    const auto* r = t != nullptr ? emphasised (*t) : nullptr;
    auto* param = t != nullptr ? ospProcessor.parameters.getParameter (t->parameterId) : nullptr;
    if (r == nullptr || param == nullptr)
        return lines;
    const auto s = sweepOf (*t, *r);
    lines.add (juce::String (modui::sourceLabel (mod::sourceIndex (r->route.source))) + juce::String::fromUTF8 (" \xe2\x86\x92 ") + mod::destInfo (r->route.dest).name);
    lines.add ("Depth " + signedPercent (100.0 * r->route.depth) + (r->route.enabled ? juce::String() : juce::String ("  (bypassed)")));
    lines.add ("Base " + valueText (*param, s.base));
    lines.add ("Range " + valueText (*param, std::min (s.lo, s.hi)) + juce::String::fromUTF8 (" \xe2\x80\x93 ") + valueText (*param, std::max (s.lo, s.hi)));
    const auto others = static_cast<int> (routesOf (*t).size()) - 1;
    if (others > 0)
        lines.add ("+ " + juce::String (others) + (others == 1 ? " other route" : " other routes"));
    return lines;
}

juce::Rectangle<float> ModulationOverlay::readoutArea (const Target& t) const
{
    const float lines = static_cast<float> (std::max (4, haloReadout (t.parameterId).size()));
    const float w = 176.0f, h = 10.0f + 15.0f * lines;
    const auto bounds = getLocalBounds().toFloat().reduced (6.0f);
    float x = t.centre.x + t.radius + 12.0f;
    if (x + w > bounds.getRight())
        x = t.centre.x - t.radius - 12.0f - w;
    return juce::Rectangle<float> (x, t.centre.y - 0.5f * h, w, h).constrainedWithin (bounds);
}

juce::Rectangle<int> ModulationOverlay::repaintArea (const Target& t) const
{
    if (t.field)
        return t.area.expanded (6.0f).getSmallestIntegerContainer();
    auto r = juce::Rectangle<float> (2.0f * t.radius + 34.0f, 2.0f * t.radius + 34.0f).withCentre (t.centre);
    return r.getSmallestIntegerContainer();
}

void ModulationOverlay::refresh()
{
    const auto before = targets.size();
    routes = ospProcessor.modulationRoutes();
    view = ospProcessor.modulationView();
    targets.clear();
    // Walk the instrument only when something could be drawn (a route, a drag).
    if (! routes.empty() || dragSource >= 0 || before > 0 || shownRings > 0)
        collect (root);
    if (hover >= static_cast<int> (targets.size()))
        hover = -1;
    // Repaint only around the halos (now and as last drawn): the instrument under the
    // overlay is never redrawn for a marker that moved.
    std::vector<juce::Rectangle<int>> areas;
    for (std::size_t i = 0; i < targets.size(); ++i)
    {
        const auto& t = targets[i];
        if (dragSource >= 0 || ! routesOf (t).empty())
            areas.push_back (repaintArea (t));
        if (static_cast<int> (i) == hover || static_cast<int> (i) == editTarget)
            areas.push_back (readoutArea (t).expanded (2.0f).getSmallestIntegerContainer());
    }
    for (const auto& a : shownAreas)
        repaint (a);
    for (const auto& a : areas)
        repaint (a);
    shownAreas = std::move (areas);
}

void ModulationOverlay::setSelection (int source, int slot)
{
    if (source == selectedSource && slot == selectedSlot)
        return;
    selectedSource = source;
    selectedSlot = slot;
    for (const auto& a : shownAreas)
        repaint (a);
}

void ModulationOverlay::setDrag (int source, juce::Point<float> where)
{
    const bool changed = source != dragSource;
    dragSource = source;
    dragPoint = where;
    if (changed)
    {
        targets.clear();
        collect (root);
        repaint();
    }
    else
        refresh();
}

juce::Slider* ModulationOverlay::controlAt (juce::Point<float> where) const
{
    // The front-most (collected last) control under the point (its knob or its halo).
    for (auto it = targets.rbegin(); it != targets.rend(); ++it)
        if (it->slider != nullptr && (it->field ? it->area.expanded (3.0f).contains (where) : it->centre.getDistanceFrom (where) <= it->radius + 4.0f))
            return it->slider.getComponent();
    return nullptr;
}

int ModulationOverlay::targetAt (juce::Point<float> p, bool includeBody) const
{
    // The halo's grip: a band well wider than its stroke, clear of the knob it surrounds.
    // With Option held, the knob itself too (but never TUNE, whose Option-drag is fine tuning).
    for (int i = static_cast<int> (targets.size()) - 1; i >= 0; --i)
    {
        const auto& t = targets[static_cast<std::size_t> (i)];
        if (t.slider == nullptr || t.field || routesOf (t).empty())
            continue;
        const float d = t.centre.getDistanceFrom (p);
        if (d >= t.radius - 5.0f && d <= t.radius + 9.0f)
            return i;
        if (includeBody && d < t.bodyRadius && ! t.parameterId.endsWith (".tune"))
            return i;
    }
    return -1;
}

bool ModulationOverlay::hitTest (int x, int y)
{
    if (dragSource >= 0)
        return false;
    if (editTarget >= 0)
        return true;
    return targetAt ({ static_cast<float> (x), static_cast<float> (y) }, juce::ModifierKeys::currentModifiers.isAltDown()) >= 0;
}

void ModulationOverlay::paintHalo (juce::Graphics& g, const Target& t, int index)
{
    using namespace design;
    auto arc = [&g, &t] (float radius, float from, float to, float width, juce::Colour c) {
        juce::Path p;
        const float a0 = t.start + juce::jlimit (0.0f, 1.0f, from) * (t.end - t.start);
        const float a1 = t.start + juce::jlimit (0.0f, 1.0f, to) * (t.end - t.start);
        if (std::abs (a1 - a0) < 0.03f)
            p.addCentredArc (t.centre.x, t.centre.y, radius, radius, 0.0f, a0 - 0.035f, a0 + 0.035f, true);
        else
            p.addCentredArc (t.centre.x, t.centre.y, radius, radius, 0.0f, std::min (a0, a1), std::max (a0, a1), true);
        g.setColour (c);
        g.strokePath (p, juce::PathStrokeType (width, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    };
    auto pointAt = [&t] (float radius, float position) {
        return t.centre.getPointOnCircumference (radius, t.start + juce::jlimit (0.0f, 1.0f, position) * (t.end - t.start));
    };
    const auto list = routesOf (t);
    const auto* main = emphasised (t);
    if (main == nullptr)
        return;
    const bool editing = index == editTarget;
    const bool hovered = index == hover || editing;
    const int source = mod::sourceIndex (main->route.source);
    const auto c = modui::sourceColour (source).darker (0.08f);
    const bool live = main->state == mod::RouteState::active;
    const auto s = sweepOf (t, *main);

    if (t.field)
    {
        // A value field (EQ): its range as a thin bar under it, the value now as a tick.
        const auto bar = juce::Rectangle<float> (t.area.getX() + 3.0f, t.area.getBottom() + 2.0f, t.area.getWidth() - 6.0f, 2.0f);
        g.setColour (c.withAlpha (0.25f));
        g.fillRect (bar);
        if (live)
        {
            const float lo = std::min (s.lo, s.hi), hi = std::max (s.lo, s.hi);
            g.setColour (c.withAlpha (0.9f));
            g.fillRect (juce::Rectangle<float> (bar.getX() + lo * bar.getWidth(), bar.getY(), (hi - lo) * bar.getWidth(), bar.getHeight()));
            if (s.nowKnown)
                g.fillRect (juce::Rectangle<float> (bar.getX() + s.now * bar.getWidth() - 1.0f, bar.getY() - 2.0f, 2.0f, bar.getHeight() + 4.0f));
        }
        return;
    }

    // The ring breaks around a caption close above it; the readout below is not clipped.
    std::optional<juce::Graphics::ScopedSaveState> clipped;
    if (! t.avoid.isEmpty())
    {
        clipped.emplace (g);
        g.excludeClipRegion (t.avoid.getSmallestIntegerContainer());
    }
    // The selected route's control: a soft glow along the whole sweep.
    if (selectedSlot >= 0 && main->slot == selectedSlot && ! hovered)
        arc (t.radius, 0.0f, 1.0f, 9.0f, c.withAlpha (0.12f));
    // Hovered: the full guide track of the knob's sweep, faint and neutral, behind the arc.
    if (hovered)
        arc (t.radius, 0.0f, 1.0f, 2.4f, colour::textSecondary.withAlpha (0.3f));
    // Other routes on this control: thin, quiet arcs just outside.
    for (const auto* r : list)
    {
        if (r == main)
            continue;
        const auto o = sweepOf (t, *r);
        arc (t.radius + 6.5f, o.lo, o.hi, 1.6f, modui::sourceColour (mod::sourceIndex (r->route.source)).withAlpha (r->state == mod::RouteState::active ? 0.45f : 0.2f));
    }
    // The route itself: its sweep from the base (a zero or bypassed route a faint tick).
    const float width = editing ? 6.5f : (hovered ? 5.5f : 4.5f);
    if (live)
        arc (t.radius, s.lo, s.hi, width, hovered ? c.brighter (0.12f) : c.withAlpha (0.92f));
    else
        arc (t.radius, s.base, s.base, 3.0f, c.withAlpha (0.4f));
    if (s.nowKnown && ! editing)
    {
        const auto at = pointAt (t.radius, s.now);
        g.setColour (juce::Colours::white.withAlpha (0.92f));
        g.fillEllipse (juce::Rectangle<float> (width + 2.5f, width + 2.5f).withCentre (at));
        g.setColour (c.darker (0.25f));
        g.fillEllipse (juce::Rectangle<float> (width - 1.0f, width - 1.0f).withCentre (at));
    }
    clipped.reset();
    if (hovered)
    {
        // The endpoint handle: where the source at its top takes the value (its direction).
        const auto at = pointAt (t.radius, s.handle);
        const float r = editing ? 7.5f : 6.0f;
        g.setColour (juce::Colour (0x33302418));
        g.fillEllipse (juce::Rectangle<float> (2.0f * r, 2.0f * r).withCentre (at.translated (0.0f, 1.0f)));
        g.setColour (colour::panelTop);
        g.fillEllipse (juce::Rectangle<float> (2.0f * r, 2.0f * r).withCentre (at));
        g.setColour (c);
        g.drawEllipse (juce::Rectangle<float> (2.0f * r - 2.0f, 2.0f * r - 2.0f).withCentre (at), 2.0f);
        // The readout: route, depth, base, range.
        const auto box = readoutArea (t);
        juce::Path card;
        card.addRoundedRectangle (box, 7.0f);
        CachedShadow (juce::Colour (0x22302418), 6, { 0, 2 }).drawForPath (g, card);
        g.setColour (juce::Colour (0xf41c2122));
        g.fillPath (card);
        const auto lines = haloReadout (t.parameterId);
        auto row = box.reduced (10.0f, 5.0f).withHeight (15.0f);
        for (int i = 0; i < lines.size(); ++i)
        {
            g.setFont (i == 0 ? type::popupLabel (11.5f) : type::popupValue (12.0f));
            g.setColour (i == 0 ? modui::sourceOnDark (source) : juce::Colour (0xffeee8dd).withAlpha (i == 1 ? 1.0f : 0.78f));
            g.drawFittedText (lines[i], row.getSmallestIntegerContainer(), juce::Justification::centredLeft, 1, 0.85f);
            row.translate (0.0f, 15.0f);
        }
    }
}

void ModulationOverlay::paint (juce::Graphics& g)
{
    int rings = 0;
    for (std::size_t i = 0; i < targets.size(); ++i)
    {
        const auto& t = targets[i];
        if (t.slider == nullptr)
            continue;
        // Assigning: where this source can go, quietly; the control under the pointer clearly.
        if (dragSource >= 0)
        {
            const auto dest = modui::destinationFor (t.parameterId, isPolySource (ospProcessor, dragSource));
            if (dest == mod::Dest::none || ! ospProcessor.canModulate (static_cast<mod::Source> (dragSource + 1), dest))
                continue;
            const auto c = modui::sourceColour (dragSource);
            ++rings;
            if (t.field)
            {
                const bool over = t.area.expanded (3.0f).contains (dragPoint);
                g.setColour (c.withAlpha (over ? 0.95f : 0.5f));
                g.drawRoundedRectangle (t.area.expanded (2.0f), 6.0f, over ? 2.0f : 1.0f);
                continue;
            }
            const bool hot = t.centre.getDistanceFrom (dragPoint) <= t.radius + 4.0f;
            juce::Path p;
            p.addCentredArc (t.centre.x, t.centre.y, t.radius, t.radius, 0.0f, t.start, t.end, true);
            if (hot)
            {
                g.setColour (c.withAlpha (0.2f));
                g.strokePath (p, juce::PathStrokeType (9.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            }
            g.setColour (c.withAlpha (hot ? 0.95f : 0.45f));
            g.strokePath (p, juce::PathStrokeType (hot ? 3.0f : 1.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            continue;
        }
        if (routesOf (t).empty())
            continue;
        ++rings;
        paintHalo (g, t, static_cast<int> (i));
    }
    shownRings = rings;
}

void ModulationOverlay::mouseMove (const juce::MouseEvent& e)
{
    const int now = targetAt (e.position, e.mods.isAltDown());
    if (now != hover)
    {
        hover = now;
        setMouseCursor (now >= 0 ? juce::MouseCursor::UpDownResizeCursor : juce::MouseCursor::NormalCursor);
        refresh();
    }
}

void ModulationOverlay::mouseExit (const juce::MouseEvent&)
{
    if (hover >= 0 && editTarget < 0)
    {
        hover = -1;
        refresh();
    }
}

void ModulationOverlay::mouseDown (const juce::MouseEvent& e)
{
    const int index = targetAt (e.position, e.mods.isAltDown());
    if (index < 0)
        return;
    const auto& t = targets[static_cast<std::size_t> (index)];
    if (e.mods.isPopupMenu())
    {
        // The routes on this control and what can be done to each.
        juce::PopupMenu menu;
        auto& p = ospProcessor;
        juce::Component::SafePointer<ModulationOverlay> safe (this);
        const auto list = routesOf (t);
        for (const auto* r : list)
        {
            const int slot = r->slot, source = mod::sourceIndex (r->route.source);
            const bool enabled = r->route.enabled;
            juce::PopupMenu items;
            items.addItem ("Select", [safe, source, slot] { if (safe != nullptr && safe->onSelect != nullptr) safe->onSelect (source, slot); });
            items.addItem (enabled ? "Bypass" : "Enable", [&p, slot, enabled] {
                if (auto* param = p.parameters.getParameter (OspAudioProcessor::modRouteId (slot, "enabled")))
                {
                    p.undoManager.beginNewTransaction (enabled ? "Bypass modulation" : "Enable modulation");
                    param->beginChangeGesture();
                    param->setValueNotifyingHost (enabled ? 0.0f : 1.0f);
                    param->endChangeGesture();
                }
            });
            items.addItem ("Depth to 0", [&p, slot] {
                if (auto* param = p.parameters.getParameter (OspAudioProcessor::modRouteId (slot, "depth")))
                {
                    p.undoManager.beginNewTransaction ("Modulation depth");
                    param->beginChangeGesture();
                    param->setValueNotifyingHost (param->convertTo0to1 (0.0f));
                    param->endChangeGesture();
                }
            });
            items.addItem ("Remove", [&p, slot] { p.removeModulationRoute (slot); });
            items.addSeparator();
            items.addItem (juce::String (modui::sourceLabel (source)) + " routes...", [safe, source] { if (safe != nullptr && safe->onShowRoutes != nullptr) safe->onShowRoutes (source); });
            const auto title = juce::String (modui::sourceLabel (source)) + juce::String::fromUTF8 (" \xe2\x86\x92 ") + mod::destInfo (r->route.dest).name + "  "
                               + signedPercent (100.0 * r->route.depth);
            if (list.size() == 1)
            {
                menu.addSectionHeader (title);
                for (juce::PopupMenu::MenuItemIterator it (items); it.next();)
                    menu.addItem (it.getItem());
            }
            else
                menu.addSubMenu (title, items);
        }
        menu.showMenuAsync (juce::PopupMenu::Options().withDeletionCheck (*this));
        return;
    }
    const auto* r = emphasised (t);
    if (r == nullptr)
        return;
    editSlot = r->slot;
    editTarget = index;
    hover = index;
    editParameter = ospProcessor.parameters.getParameter (OspAudioProcessor::modRouteId (editSlot, "depth"));
    if (onSelect != nullptr)
        onSelect (mod::sourceIndex (r->route.source), r->slot);
    setSelection (mod::sourceIndex (r->route.source), r->slot);
    if (editParameter == nullptr)
    {
        editSlot = editTarget = -1;
        return;
    }
    editFrom = editParameter->convertFrom0to1 (editParameter->getValue());
    ospProcessor.undoManager.beginNewTransaction ("Modulation depth");
    editParameter->beginChangeGesture();
    refresh();
}

void ModulationOverlay::mouseDrag (const juce::MouseEvent& e)
{
    if (editParameter == nullptr)
        return;
    // Up deepens, down reduces and goes through zero to the other way; the same route the
    // list shows. The base value is never touched.
    const float fine = e.mods.isShiftDown() ? 0.15f : 0.5f;
    const float depth = juce::jlimit (-100.0f, 100.0f, editFrom - fine * static_cast<float> (e.getDistanceFromDragStartY()));
    editParameter->setValueNotifyingHost (editParameter->convertTo0to1 (depth));
    refresh();
}

void ModulationOverlay::mouseUp (const juce::MouseEvent& e)
{
    if (editParameter != nullptr)
        editParameter->endChangeGesture();
    editParameter = nullptr;
    editSlot = -1;
    editTarget = -1;
    hover = targetAt (e.position, e.mods.isAltDown());
    refresh();
}

} // namespace osp::plugin
