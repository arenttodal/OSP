#include "ModulationBay.h"

#include "EngineCard.h"
#include "OspLookAndFeel.h"

#include <cmath>

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
    if (s == source)
        return;
    source = s;
    dragging = false;
    dragPoint = dragSegment = hoverPoint = -1;
    curve = ospProcessor.modulationCurve (curveIndex());
    repaint();
}

bool ModCurveView::isEditable() const
{
    if (source < 2)
        return juce::roundToInt (ospProcessor.parameterValue (OspAudioProcessor::modLfoId (source, "shape"))) == static_cast<int> (mod::LfoShape::custom);
    return ospProcessor.parameterValue (OspAudioProcessor::modEnvId (source - 2, "mode")) >= 0.5f;
}

juce::Rectangle<float> ModCurveView::plotArea() const
{
    return getLocalBounds().toFloat().reduced (18.0f, 0.0f).withTrimmedTop (38.0f).withTrimmedBottom (34.0f);
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
    // Repaint only when what is drawn changed: the source's settings, its curve, the playhead.
    std::array<float, 24> signature {};
    std::size_t n = 0;
    if (source < 2)
        for (const char* name : { "shape", "rate", "sync", "division", "phase", "polarity", "mode", "scope" })
            signature[n++] = ospProcessor.parameterValue (OspAudioProcessor::modLfoId (source, name));
    else
        for (const char* name : { "mode", "attack", "decay", "sustain", "release", "curve", "length" })
            signature[n++] = ospProcessor.parameterValue (OspAudioProcessor::modEnvId (source - 2, name));
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
    if (source < 2)
    {
        if (! isPolySource (ospProcessor, source) || view.voice)
            playhead = view.lfoPhase[static_cast<std::size_t> (source)];
    }
    else if (view.voice)
    {
        const auto i = static_cast<std::size_t> (source - 2);
        playhead = view.envStage[i] >= 1 && view.envStage[i] <= 4 ? static_cast<float> (view.envStage[i]) * 100.0f + view.envTime[i] : -1.0f;
    }
    const float value = view.value[static_cast<std::size_t> (source)];
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
    const auto colour = modui::sourceOnDark (source);

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

    if (source < 2)
        paintLfo (g, plot, colour);
    else
        paintEnvelope (g, plot, colour);

    if (isEditable())
    {
        paintCurvePoints (g, colour);
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
    const auto s = lfoSettings (ospProcessor, source);
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

    // Labels: what it is, how fast, how it runs.
    g.setFont (type::popupLabel (12.5f));
    g.setColour (colour);
    const auto top = getLocalBounds().toFloat().reduced (18.0f, 0.0f).withHeight (36.0f);
    g.drawText (juce::String (sourceLabels[static_cast<std::size_t> (source)]) + "  " + mod::lfoShapeName (s.shape), top, juce::Justification::centredLeft, false);
    const juce::String rate = s.sync ? juce::String (mod::syncName (s.division)) : format::hertz (s.rateHz);
    const char* modes[] { "FREE", "RETRIG", "ONE SHOT" };
    g.setColour (colour::wellText.brighter (0.3f));
    const auto dot = juce::String::fromUTF8 ("  \xc2\xb7  ");
    g.drawText (rate + dot + modes[static_cast<int> (s.mode)] + dot + (s.scope == mod::Scope::poly ? "POLY" : "GLOBAL"), top, juce::Justification::centredRight, false);
    if (s.scope == mod::Scope::poly && shownPlayhead < 0.0f)
    {
        g.setColour (colour::wellText.withAlpha (0.7f));
        g.drawText ("per note: plays with each note", plot.withTrimmedTop (plot.getHeight() - 22.0f), juce::Justification::centredRight, false);
    }
}

void ModCurveView::paintEnvelope (juce::Graphics& g, juce::Rectangle<float> plot, juce::Colour colour)
{
    using namespace design;
    const auto s = envSettings (ospProcessor, source - 2);
    const auto top = getLocalBounds().toFloat().reduced (18.0f, 0.0f).withHeight (36.0f);
    g.setFont (type::popupLabel (12.5f));
    g.setColour (colour);
    g.drawText (juce::String (sourceLabels[static_cast<std::size_t> (source)]) + (s.oneShotCurve ? "  ONE SHOT" : "  ADSR"), top, juce::Justification::centredLeft, false);
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
    const int now = isEditable() ? pointAt (e.position) : -1;
    if (now != hoverPoint)
    {
        hoverPoint = now;
        repaint();
    }
}

void ModCurveView::mouseExit (const juce::MouseEvent&)
{
    if (hoverPoint >= 0)
    {
        hoverPoint = -1;
        repaint();
    }
}

void ModCurveView::mouseDown (const juce::MouseEvent& e)
{
    if (! isEditable())
        return;
    curve = ospProcessor.modulationCurve (curveIndex());
    before = curve;
    if (resetArea().contains (e.position))
    {
        ospProcessor.setModulationCurve (curveIndex(), source < 2 ? mod::defaultLfoCurve() : mod::defaultEnvCurve(), true);
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
// A source's controls

ModSourcePage::ModSourcePage (OspAudioProcessor& p, int s) : ospProcessor (p), source (s)
{
    auto& state = ospProcessor.parameters;
    const auto accent = modui::sourceColour (source);
    auto addKnob = [&] (const char* name, const char* caption, MiniKnob::Formatter f) {
        auto knob = std::make_unique<MiniKnob> (state, sourceParameter (source, name), caption, std::move (f));
        knob->setArcColour (accent);
        addAndMakeVisible (*knob);
        knobs.push_back (std::move (knob));
    };
    if (isLfo())
    {
        shape = std::make_unique<ValueSelector> (parameter (p, sourceParameter (source, "shape")), "SHAPE");
        mode = std::make_unique<ValueSelector> (parameter (p, sourceParameter (source, "mode")), "MODE");
        division = std::make_unique<ValueSelector> (parameter (p, sourceParameter (source, "division")), "DIVISION");
        timing = std::make_unique<SegmentedControl> (parameter (p, sourceParameter (source, "sync")), juce::StringArray { "HZ", "SYNC" });
        polarity = std::make_unique<SegmentedControl> (parameter (p, sourceParameter (source, "polarity")), juce::StringArray { "BIPOLAR", "UNIPOLAR" });
        scope = std::make_unique<SegmentedControl> (parameter (p, sourceParameter (source, "scope")), juce::StringArray { "GLOBAL", "POLY" });
        timing->setTooltip ("Free in Hz, or synced to the host's tempo");
        polarity->setTooltip ("Bipolar swings both ways around the setting; unipolar only adds");
        scope->setTooltip ("Global: one movement for the instrument. Poly: each note its own");
        for (auto* c : { static_cast<juce::Component*> (shape.get()), static_cast<juce::Component*> (mode.get()), static_cast<juce::Component*> (division.get()),
                         static_cast<juce::Component*> (timing.get()), static_cast<juce::Component*> (polarity.get()), static_cast<juce::Component*> (scope.get()) })
            addAndMakeVisible (c);
        for (auto* seg : { timing.get(), polarity.get(), scope.get() })
            seg->setAccent (accent);
        addKnob ("rate", "RATE", [] (double v) { return format::hertz (v); });
        addKnob ("phase", "PHASE", [] (double v) { return juce::String (juce::roundToInt (3.6 * v)) + juce::String::fromUTF8 ("\xc2\xb0"); });
    }
    else
    {
        mode = std::make_unique<ValueSelector> (parameter (p, sourceParameter (source, "mode")), "MODE");
        addAndMakeVisible (*mode);
        auto ms = [] (double v) { return format::milliseconds (v); };
        addKnob ("attack", "ATTACK", ms);
        addKnob ("decay", "DECAY", ms);
        addKnob ("sustain", "SUSTAIN", [] (double v) { return juce::String (juce::roundToInt (v)) + " %"; });
        addKnob ("release", "RELEASE", ms);
        addKnob ("curve", "CURVE", [] (double v) { return format::bipolar (v); });
        addKnob ("length", "LENGTH", ms);
    }
    refresh();
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
        knobs[0]->setVisible (! sync);   // RATE in Hz, or DIVISION when synced
        division->setVisible (sync);
    }
    else
    {
        for (std::size_t i = 0; i < 5; ++i)
            knobs[i]->setVisible (! oneShot);
        knobs[5]->setVisible (oneShot);
    }
    resized();
    repaint();
}

void ModSourcePage::resized()
{
    const float w = static_cast<float> (getWidth());
    auto at = [] (juce::Rectangle<float> r) { return r.getSmallestIntegerContainer(); };
    if (isLfo())
    {
        const float half = 0.5f * (w - 14.0f);
        shape->setBounds (at ({ 0.0f, 0.0f, half, 48.0f }));
        mode->setBounds (at ({ half + 14.0f, 0.0f, half, 48.0f }));
        const float third = (w - 20.0f) / 3.0f;
        timing->setBounds (at ({ 0.0f, 62.0f, third, 30.0f }));
        polarity->setBounds (at ({ third + 10.0f, 62.0f, third, 30.0f }));
        scope->setBounds (at ({ 2.0f * (third + 10.0f), 62.0f, third, 30.0f }));
        knobs[0]->setBounds (at (juce::Rectangle<float> (104.0f, 96.0f).withCentre ({ w / 3.0f, 160.0f })));
        division->setBounds (at (juce::Rectangle<float> (124.0f, 50.0f).withCentre ({ w / 3.0f, 160.0f })));
        knobs[1]->setBounds (at (juce::Rectangle<float> (104.0f, 96.0f).withCentre ({ 2.0f * w / 3.0f, 160.0f })));
        return;
    }
    mode->setBounds (at ({ 0.0f, 0.0f, 0.5f * (w - 14.0f), 48.0f }));
    const float cell = w / 5.0f;
    for (std::size_t i = 0; i < 5; ++i)
        knobs[i]->setBounds (at (juce::Rectangle<float> (cell, 96.0f).withCentre ({ cell * (static_cast<float> (i) + 0.5f), 124.0f })));
    knobs[5]->setBounds (at (juce::Rectangle<float> (104.0f, 96.0f).withCentre ({ 0.5f * w, 124.0f })));
}

void ModSourcePage::paint (juce::Graphics& g)
{
    if (isLfo())
        return;
    // An envelope belongs to each note: say so where its mode is chosen.
    const auto r = juce::Rectangle<float> (0.5f * (static_cast<float> (getWidth()) + 14.0f), 17.0f, 0.5f * (static_cast<float> (getWidth()) - 14.0f), 31.0f);
    g.setFont (type::popupLabel (12.0f));
    g.setColour (design::colour::textMicro);
    g.drawFittedText (shownOneShot ? "Each note plays the curve once" : "Each note has its own envelope;\nnote-off starts the release",
                      r.getSmallestIntegerContainer(), juce::Justification::centredLeft, 2);
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
// The bay

ModulationBay::Row::Row (ModulationBay& b, const OspAudioProcessor::ModRouteInfo& i)
    : bay (b), info (i), depth (b.ospProcessor, i.slot, modui::sourceColour (mod::sourceIndex (i.route.source)))
{
    addAndMakeVisible (depth);
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

juce::Rectangle<float> ModulationBay::Row::bypassArea() const
{
    return { static_cast<float> (getWidth()) - 52.0f, 0.5f * static_cast<float> (getHeight()) - 9.0f, 18.0f, 18.0f };
}

juce::Rectangle<float> ModulationBay::Row::removeArea() const
{
    return { static_cast<float> (getWidth()) - 26.0f, 0.5f * static_cast<float> (getHeight()) - 9.0f, 18.0f, 18.0f };
}

void ModulationBay::Row::resized()
{
    depth.setBounds (juce::Rectangle<float> (static_cast<float> (getWidth()) - 160.0f, 0.0f, 100.0f, static_cast<float> (getHeight())).getSmallestIntegerContainer());
}

void ModulationBay::Row::paint (juce::Graphics& g)
{
    using namespace design;
    const auto r = getLocalBounds().toFloat();
    const int s = mod::sourceIndex (info.route.source);
    const bool selectedRow = bay.selectedSlot == info.slot;
    const bool live = info.state == mod::RouteState::active;
    if (selectedRow || isMouseOver (true))
    {
        g.setColour (selectedRow ? colour::accentSoft.withAlpha (0.45f) : colour::text.withAlpha (0.04f));
        g.fillRoundedRectangle (r.reduced (1.0f, 2.0f), 7.0f);
    }
    g.setColour (colour::hairline);
    g.fillRect (juce::Rectangle<float> (8.0f, r.getBottom() - 1.0f, r.getWidth() - 16.0f, 1.0f));
    const float dim = live ? 1.0f : 0.45f;
    draw::led (g, { 15.0f, r.getCentreY() }, 8.0f, modui::sourceColour (s).withMultipliedAlpha (dim), 0.0f);
    g.setFont (type::popupLabel (12.5f));
    g.setColour (colour::text.withAlpha (0.86f * dim));
    g.drawText (sourceLabels[static_cast<std::size_t> (s)], juce::Rectangle<float> (26.0f, 0.0f, 44.0f, r.getHeight()), juce::Justification::centredLeft, false);
    g.setColour (colour::textMicro.withMultipliedAlpha (dim));
    g.drawText (juce::String::fromUTF8 ("\xe2\x86\x92"), juce::Rectangle<float> (68.0f, 0.0f, 14.0f, r.getHeight()), juce::Justification::centred, false);
    const auto destArea = juce::Rectangle<float> (86.0f, 0.0f, r.getWidth() - 86.0f - 166.0f, r.getHeight());
    g.setColour (info.state == mod::RouteState::scope ? colour::accent.withAlpha (0.8f) : colour::text.withAlpha (0.86f * dim));
    g.drawFittedText (mod::destInfo (info.route.dest).name, destArea.getSmallestIntegerContainer(), juce::Justification::centredLeft, 1, 0.8f);

    // Bypass: a small light (lit while the route plays), then remove.
    const auto bypass = bypassArea();
    const bool on = info.route.enabled;
    g.setColour (colour::hairline.darker (0.15f));
    g.drawEllipse (bypass.reduced (2.5f), 1.0f);
    if (on)
        draw::led (g, bypass.getCentre(), 7.0f, modui::sourceColour (s), 0.5f);
    const auto remove = removeArea().reduced (5.0f);
    const bool hot = isMouseOver() && removeArea().contains (getMouseXYRelative().toFloat());
    g.setColour (colour::text.withAlpha (hot ? 0.8f : 0.4f));
    g.drawLine (remove.getX(), remove.getY(), remove.getRight(), remove.getBottom(), 1.3f);
    g.drawLine (remove.getRight(), remove.getY(), remove.getX(), remove.getBottom(), 1.3f);
}

void ModulationBay::Row::mouseDown (const juce::MouseEvent& e)
{
    auto& p = bay.ospProcessor;
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
        juce::Component::SafePointer<ModulationBay> safe (&bay);
        // The row is rebuilt (deleted) by the change: act after this event.
        juce::MessageManager::callAsync ([safe, slot] {
            if (safe != nullptr)
            {
                safe->ospProcessor.removeModulationRoute (slot);
                if (safe->selectedSlot == slot)
                    safe->selectRoute (-1);
                safe->refresh();
            }
        });
        return;
    }
    bay.selectRoute (bay.selectedSlot == info.slot ? -1 : info.slot);
}

ModulationBay::ModulationBay (OspAudioProcessor& p) : ospProcessor (p), curveView (p)
{
    setTitle ("Modulation");
    addAndMakeVisible (curveView);
    for (int s = 0; s < 4; ++s)
    {
        pages[static_cast<std::size_t> (s)] = std::make_unique<ModSourcePage> (p, s);
        addChildComponent (*pages[static_cast<std::size_t> (s)]);
    }
    pages[0]->setVisible (true);
    routeViewport.setViewedComponent (&routeContent, false);
    routeViewport.setScrollBarsShown (true, false);
    routeViewport.setScrollBarThickness (7);
    addAndMakeVisible (routeViewport);
    refresh();
}

ModulationBay::~ModulationBay()
{
    rows.clear();
}

void ModulationBay::selectSource (int source)
{
    source = juce::jlimit (0, 3, source);
    if (source == selected)
        return;
    pages[static_cast<std::size_t> (selected)]->setVisible (false);
    selected = source;
    pages[static_cast<std::size_t> (selected)]->setVisible (true);
    pages[static_cast<std::size_t> (selected)]->refresh();
    curveView.setSource (source);
    repaint();
}

void ModulationBay::selectRoute (int slot)
{
    selectedSlot = slot;
    mod::Dest dest = mod::Dest::none;
    for (auto& row : rows)
    {
        if (row->info.slot == slot)
            dest = row->info.route.dest;
        row->repaint();
    }
    if (onRouteSelected != nullptr)
        onRouteSelected (dest);
}

juce::Rectangle<float> ModulationBay::tabArea (int source) const
{
    const float w = (static_cast<float> (getWidth()) - 36.0f - 3.0f * 8.0f) / 4.0f;
    return { 18.0f + static_cast<float> (source) * (w + 8.0f), 56.0f, w, 58.0f };
}

void ModulationBay::resized()
{
    const float w = static_cast<float> (getWidth()), h = static_cast<float> (getHeight());
    auto at = [] (juce::Rectangle<float> r) { return r.getSmallestIntegerContainer(); };
    header = { 22.0f, 14.0f, w - 44.0f, 30.0f };
    curveView.setBounds (at ({ 18.0f, 128.0f, w - 36.0f, 252.0f }));
    for (auto& page : pages)
        page->setBounds (at ({ 22.0f, 396.0f, w - 44.0f, 214.0f }));
    routingHeader = { 22.0f, 624.0f, w - 44.0f, 28.0f };
    addButton = { w - 22.0f - 74.0f, 624.0f, 74.0f, 28.0f };
    routeViewport.setBounds (at ({ 14.0f, 660.0f, w - 28.0f, std::max (40.0f, h - 660.0f - 16.0f) }));
    rebuildRows();
}

void ModulationBay::rebuildRows()
{
    const auto routes = ospProcessor.modulationRoutes();
    juce::String signature;
    for (const auto& r : routes)
        signature << r.slot << ":" << static_cast<int> (r.route.source) << ":" << static_cast<int> (r.route.dest) << ";";
    const int width = routeViewport.getWidth() - routeViewport.getScrollBarThickness() - 2;
    if (signature != shownRoutes || (! rows.empty() && rows.front()->getWidth() != width))
    {
        shownRoutes = signature;
        rows.clear();
        routeContent.removeAllChildren();
        int y = 0;
        for (const auto& r : routes)
        {
            auto row = std::make_unique<Row> (*this, r);
            row->setBounds (0, y, width, 38);
            routeContent.addAndMakeVisible (*row);
            rows.push_back (std::move (row));
            y += 38;
        }
        routeContent.setSize (std::max (10, width), std::max (y, 10));
        bool found = false;
        for (const auto& r : routes)
            found = found || r.slot == selectedSlot;
        if (! found && selectedSlot >= 0)
            selectRoute (-1);
        repaint();
        return;
    }
    // Same routes: keep the rows (a depth being dragged stays), follow their state.
    for (std::size_t i = 0; i < rows.size() && i < routes.size(); ++i)
    {
        auto& row = *rows[i];
        if (row.info.state != routes[i].state || row.info.route.enabled != routes[i].route.enabled)
        {
            row.info = routes[i];
            row.repaint();
        }
        row.info.route.depth = routes[i].route.depth;
    }
}

void ModulationBay::refresh()
{
    curveView.refresh();
    pages[static_cast<std::size_t> (selected)]->refresh();
    rebuildRows();
    // The tabs' small meters follow the sources.
    const auto view = ospProcessor.modulationView();
    for (int s = 0; s < 4; ++s)
    {
        const float v = view.value[static_cast<std::size_t> (s)];
        if (std::abs (v - shownValues[static_cast<std::size_t> (s)]) > 0.004f)
        {
            shownValues[static_cast<std::size_t> (s)] = v;
            repaint (tabArea (s).getSmallestIntegerContainer());
        }
    }
}

void ModulationBay::paint (juce::Graphics& g)
{
    using namespace design;
    const auto r = getLocalBounds().toFloat();
    // The expansion module's face: the instrument's panel, a shade cooler and quieter.
    draw::raised (g, r, layout::panelRadius, colour::panelTop.interpolatedWith (colour::housingTop, 0.35f), colour::panelBottom.interpolatedWith (colour::housingBottom, 0.35f));
    g.setColour (colour::text.withAlpha (0.9f));
    g.setFont (type::panelHeader());
    g.drawText ("MODULATION", header, juce::Justification::centredLeft, false);
    int active = 0;
    for (const auto& row : rows)
        active += row->info.state == mod::RouteState::active ? 1 : 0;
    g.setFont (type::popupLabel (12.5f));
    g.setColour (colour::textMicro);
    g.drawText (rows.empty() ? juce::String ("NO ROUTES") : juce::String (active) + " / " + juce::String (static_cast<int> (rows.size())) + " ACTIVE",
                header, juce::Justification::centredRight, false);

    for (int s = 0; s < 4; ++s)
        paintTab (g, s);

    g.setColour (colour::text.withAlpha (0.9f));
    g.setFont (type::panelHeader());
    g.drawText ("ROUTING", routingHeader, juce::Justification::centredLeft, false);
    draw::button (g, addButton, 7.0f, false, hoverAdd, colour::accent);
    g.setFont (type::popupLabel (13.0f));
    g.setColour (colour::text.withAlpha (0.86f));
    g.drawText ("+ ADD", addButton, juce::Justification::centred, false);
    g.setColour (colour::hairline);
    g.fillRect (juce::Rectangle<float> (22.0f, routingHeader.getBottom() + 4.0f, r.getWidth() - 44.0f, 1.0f));

    if (rows.empty())
    {
        g.setFont (type::popupLabel (13.0f));
        g.setColour (colour::textMicro);
        g.drawFittedText ("Drag a source's socket onto a knob,\nor choose + ADD.", routeViewport.getBounds().withHeight (80), juce::Justification::centred, 2);
    }
}

void ModulationBay::paintTab (juce::Graphics& g, int s)
{
    using namespace design;
    const auto t = tabArea (s);
    const auto c = modui::sourceColour (s);
    const bool on = s == selected;
    if (on)
    {
        // Chosen: pressed in, its colour along the foot.
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xffe2d9cc), 0.0f, t.getY(), juce::Colour (0xffebe4d9), 0.0f, t.getBottom(), false));
        g.fillRoundedRectangle (t, 9.0f);
        g.setColour (colour::hairline.darker (0.1f));
        g.drawRoundedRectangle (t.reduced (0.5f), 9.0f, 1.0f);
        g.setColour (c);
        g.fillRoundedRectangle (t.reduced (14.0f, 0.0f).withTop (t.getBottom() - 3.0f).withHeight (2.0f), 1.0f);
    }
    else
        draw::raised (g, t, 9.0f, s == hoverTab ? colour::panelTop.brighter (0.02f) : colour::panelTop, colour::panelBottom, 0.7f);

    // The name, then the patch socket (the drag handle): a ring, filled while routed.
    bool used = false;
    for (const auto& row : rows)
        used = used || (mod::sourceIndex (row->info.route.source) == s && row->info.state == mod::RouteState::active);
    g.setFont (type::controlLabel (0.93f));
    g.setColour (colour::text.withAlpha (on ? 0.95f : 0.78f));
    g.drawText (sourceLabels[static_cast<std::size_t> (s)], t.withTrimmedLeft (12.0f).withHeight (32.0f).translated (0.0f, 4.0f), juce::Justification::centredLeft, false);
    const auto socket = juce::Rectangle<float> (16.0f, 16.0f).withCentre ({ t.getRight() - 17.0f, t.getY() + 20.0f });
    g.setColour (juce::Colour (0x22302418));
    g.fillEllipse (socket.translated (0.0f, 1.0f));
    g.setColour (c.darker (0.15f));
    g.drawEllipse (socket.reduced (1.0f), 1.6f);
    g.setColour (used ? c : colour::panelBottom.darker (0.25f));
    g.fillEllipse (socket.reduced (5.0f));

    // A small meter of the source now (bipolar around its middle).
    const auto meter = juce::Rectangle<float> (t.getX() + 12.0f, t.getBottom() - 15.0f, t.getWidth() - 24.0f, 3.0f);
    g.setColour (colour::knobTrack.withAlpha (0.8f));
    g.fillRoundedRectangle (meter, 1.5f);
    const float v = juce::jlimit (-1.0f, 1.0f, shownValues[static_cast<std::size_t> (s)]);
    const bool bipolar = isBipolarSource (ospProcessor, s);
    const float from = bipolar ? meter.getCentreX() : meter.getX();
    const float to = bipolar ? meter.getCentreX() + 0.5f * v * meter.getWidth() : meter.getX() + std::max (0.0f, v) * meter.getWidth();
    g.setColour (c);
    g.fillRoundedRectangle (juce::Rectangle<float> (std::min (from, to), meter.getY(), std::abs (to - from), meter.getHeight()), 1.5f);
}

juce::PopupMenu ModulationBay::addMenu()
{
    // SOURCE -> DESTINATION without dragging (precise, hidden controls, accessibility).
    juce::PopupMenu menu;
    juce::Component::SafePointer<ModulationBay> safe (this);
    for (int s = 0; s < 4; ++s)
    {
        const auto source = static_cast<mod::Source> (s + 1);
        juce::PopupMenu sub;
        auto add = [&] (mod::Dest d) {
            sub.addItem (mod::destInfo (d).name, ospProcessor.canModulate (source, d), false, [safe, source, d] {
                if (safe == nullptr)
                    return;
                const int slot = safe->ospProcessor.addModulationRoute (source, d, 50.0f);
                safe->refresh();
                if (slot >= 0)
                    safe->selectRoute (slot);
            });
        };
        sub.addSectionHeader ("MACROS");
        for (auto d : { mod::Dest::life, mod::Dest::drive, mod::Dest::character, mod::Dest::movement, mod::Dest::space })
            add (d);
        sub.addSectionHeader ("CHARACTER");
        add (mod::Dest::cutoff);
        add (mod::Dest::resonance);
        sub.addSectionHeader ("AMP ENVELOPE");
        for (auto d : { mod::Dest::ampAttack, mod::Dest::ampDecay, mod::Dest::ampSustain, mod::Dest::ampRelease })
            add (d);
        for (int l = 0; l < OspAudioProcessor::numLayers; ++l)
        {
            sub.addSectionHeader ("LAYER " + OspAudioProcessor::layerName (l));
            for (auto first : { mod::Dest::levelA, mod::Dest::panA, mod::Dest::fineTuneA, mod::Dest::reimaginedA, mod::Dest::grainPositionA,
                                mod::Dest::grainDensityA, mod::Dest::grainSizeA, mod::Dest::grainSpreadA, mod::Dest::eqBellFrequencyA,
                                mod::Dest::eqBellGainA, mod::Dest::eqLowShelfGainA, mod::Dest::eqHighShelfGainA })
                add (offsetDest (first, l));
        }
        menu.addSubMenu (sourceLabels[static_cast<std::size_t> (s)], sub);
    }
    return menu;
}

void ModulationBay::mouseMove (const juce::MouseEvent& e)
{
    int tab = -1;
    for (int s = 0; s < 4; ++s)
        if (tabArea (s).contains (e.position))
            tab = s;
    const bool add = addButton.contains (e.position);
    if (tab != hoverTab || add != hoverAdd)
    {
        hoverTab = tab;
        hoverAdd = add;
        setMouseCursor (tab >= 0 ? juce::MouseCursor::DraggingHandCursor : (add ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor));
        repaint();
    }
}

void ModulationBay::mouseExit (const juce::MouseEvent&)
{
    if (hoverTab >= 0 || hoverAdd)
    {
        hoverTab = -1;
        hoverAdd = false;
        repaint();
    }
}

void ModulationBay::mouseDown (const juce::MouseEvent& e)
{
    pressAt = e.getPosition();
    dragSource = -1;
    for (int s = 0; s < 4; ++s)
        if (tabArea (s).contains (e.position))
            selectSource (s);
    if (addButton.contains (e.position))
        addMenu().showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withTargetScreenArea (localAreaToGlobal (addButton.getSmallestIntegerContainer())),
                                 nullptr);
}

void ModulationBay::mouseDrag (const juce::MouseEvent& e)
{
    // A tab dragged a few pixels becomes a cable from its socket.
    if (dragSource < 0)
    {
        if (e.getDistanceFromDragStart() < 5)
            return;
        for (int s = 0; s < 4; ++s)
            if (tabArea (s).contains (pressAt.toFloat()))
                dragSource = s;
        if (dragSource < 0)
            return;
    }
    setMouseCursor (juce::MouseCursor::DraggingHandCursor);
    if (onDragMove != nullptr)
        onDragMove (dragSource, e.getScreenPosition());
}

void ModulationBay::mouseUp (const juce::MouseEvent& e)
{
    if (dragSource >= 0 && onDragEnd != nullptr)
        onDragEnd (dragSource, e.getScreenPosition());
    dragSource = -1;
}

//==============================================================================
// The indicators

ModulationOverlay::ModulationOverlay (OspAudioProcessor& p, juce::Component& r) : ospProcessor (p), root (r)
{
    setInterceptsMouseClicks (true, false);
    setTitle ("Modulation indicators");
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
                // Just outside the knob's own value arc (see OspLookAndFeel::drawRotarySlider).
                if (static_cast<bool> (props["popup"]))
                    t.radius = 1.19f * 0.5f * side / 1.36f;
                else if (! static_cast<bool> (props["mini"]) && ! static_cast<bool> (props["noTicks"]))
                    t.radius = 1.25f * 0.5f * side / 1.42f;
                else
                    t.radius = 1.28f * 0.5f * side / 1.2f;
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

void ModulationOverlay::refresh()
{
    const auto before = targets.size();
    routes = ospProcessor.modulationRoutes();
    view = ospProcessor.modulationView();
    targets.clear();
    // Walk the instrument only when something could be drawn (a route, a drag, a highlight).
    if (! routes.empty() || dragSource >= 0 || highlight != mod::Dest::none || before > 0 || shownRings > 0)
        collect (root);
    // Repaint only around the rings (now and as last drawn): the instrument under the
    // overlay is never redrawn for a modulation marker that moved.
    std::vector<juce::Rectangle<int>> areas;
    for (const auto& t : targets)
    {
        const auto dests = modui::destinationsShownBy (t.parameterId);
        bool shown = dragSource >= 0;
        for (const auto& r : routes)
            shown = shown || std::find (dests.begin(), dests.end(), r.route.dest) != dests.end();
        if (shown)
            areas.push_back ((t.field ? t.area.expanded (6.0f) : juce::Rectangle<float> (2.0f * t.radius + 22.0f, 2.0f * t.radius + 22.0f).withCentre (t.centre))
                                 .getSmallestIntegerContainer());
    }
    for (const auto& a : shownAreas)
        repaint (a);
    for (const auto& a : areas)
        repaint (a);
    shownAreas = std::move (areas);
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
    // The front-most (collected last) control whose ring contains the point.
    for (auto it = targets.rbegin(); it != targets.rend(); ++it)
        if (it->slider != nullptr && (it->field ? it->area.expanded (3.0f).contains (where) : it->centre.getDistanceFrom (where) <= it->radius + 4.0f))
            return it->slider.getComponent();
    return nullptr;
}

float ModulationOverlay::positionOf (const Target& t, double offset, mod::Dest dest) const
{
    // The knob position (0..1) of base + offset, offset in the destination's own units
    // (span x contribution), mapped the way the engine applies it.
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

int ModulationOverlay::routeFor (const Target& t) const
{
    const auto dests = modui::destinationsShownBy (t.parameterId);
    int first = -1;
    for (const auto& r : routes)
        if (std::find (dests.begin(), dests.end(), r.route.dest) != dests.end())
        {
            if (mod::sourceIndex (r.route.source) == selectedSource)
                return r.slot;
            if (first < 0)
                first = r.slot;
        }
    return first;
}

const ModulationOverlay::Target* ModulationOverlay::ringAt (juce::Point<float> p) const
{
    for (auto it = targets.rbegin(); it != targets.rend(); ++it)
    {
        const float d = it->centre.getDistanceFrom (p);
        if (it->slider != nullptr && ! it->field && d >= it->radius - 4.0f && d <= it->radius + 5.0f && routeFor (*it) >= 0)
            return &*it;
    }
    return nullptr;
}

bool ModulationOverlay::hitTest (int x, int y)
{
    return dragSource < 0 && ringAt ({ static_cast<float> (x), static_cast<float> (y) }) != nullptr;
}

void ModulationOverlay::paint (juce::Graphics& g)
{
    using namespace design;
    int rings = 0;
    auto arc = [&g] (const Target& t, float radius, float from, float to, float width, juce::Colour c) {
        juce::Path p;
        const float a0 = t.start + juce::jlimit (0.0f, 1.0f, from) * (t.end - t.start);
        const float a1 = t.start + juce::jlimit (0.0f, 1.0f, to) * (t.end - t.start);
        if (std::abs (a1 - a0) < 0.02f)
            p.addCentredArc (t.centre.x, t.centre.y, radius, radius, 0.0f, a0 - 0.03f, a0 + 0.03f, true);
        else
            p.addCentredArc (t.centre.x, t.centre.y, radius, radius, 0.0f, std::min (a0, a1), std::max (a0, a1), true);
        g.setColour (c);
        g.strokePath (p, juce::PathStrokeType (width, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    };

    for (const auto& t : targets)
    {
        if (t.slider == nullptr)
            continue;
        auto* param = ospProcessor.parameters.getParameter (t.parameterId);
        if (param == nullptr)
            continue;
        const float base = param->getValue();

        // Assigning: where this source can go, quietly; the control under the pointer clearly.
        if (dragSource >= 0)
        {
            const auto dest = modui::destinationFor (t.parameterId, isPolySource (ospProcessor, dragSource));
            if (dest == mod::Dest::none || ! ospProcessor.canModulate (static_cast<mod::Source> (dragSource + 1), dest))
                continue;
            const auto c = modui::sourceColour (dragSource);
            if (t.field)
            {
                const bool over = t.area.expanded (3.0f).contains (dragPoint);
                g.setColour (c.withAlpha (over ? 0.95f : 0.5f));
                g.drawRoundedRectangle (t.area.expanded (2.0f), 6.0f, over ? 2.0f : 1.0f);
                ++rings;
                continue;
            }
            const bool hot = t.centre.getDistanceFrom (dragPoint) <= t.radius + 4.0f;
            g.setColour (c.withAlpha (hot ? 0.22f : 0.0f));
            g.drawEllipse (juce::Rectangle<float> (2.0f * t.radius + 8.0f, 2.0f * t.radius + 8.0f).withCentre (t.centre), 5.0f);
            g.setColour (c.withAlpha (hot ? 0.95f : 0.45f));
            g.drawEllipse (juce::Rectangle<float> (2.0f * t.radius, 2.0f * t.radius).withCentre (t.centre), hot ? 2.2f : 1.1f);
            ++rings;
            continue;
        }

        // The routes this control shows: their swept range, and where it is now.
        const auto dests = modui::destinationsShownBy (t.parameterId);
        float lo = base, hi = base, now = base;
        int count = 0, firstSource = -1, live = 0;
        bool mixed = false, nowKnown = false, selectedHere = false, highlighted = false;
        for (auto d : dests)
        {
            double down = 0.0, up = 0.0, current = 0.0;
            bool any = false;
            for (const auto& r : routes)
            {
                if (r.route.dest != d)
                    continue;
                highlighted = highlighted || d == highlight;
                const int s = mod::sourceIndex (r.route.source);
                ++count;
                if (firstSource < 0)
                    firstSource = s;
                mixed = mixed || s != firstSource;
                selectedHere = selectedHere || s == selectedSource;
                if (r.state != mod::RouteState::active)
                    continue;
                ++live;
                any = true;
                const double depth = r.route.depth;
                if (isBipolarSource (ospProcessor, s))
                {
                    down -= std::abs (depth);
                    up += std::abs (depth);
                }
                else
                {
                    down += std::min (0.0, depth);
                    up += std::max (0.0, depth);
                }
                // The value now: a global source always, a per-voice one from the newest note.
                if (! isPolySource (ospProcessor, s) || view.voice)
                {
                    current += depth * static_cast<double> (view.value[static_cast<std::size_t> (s)]);
                    nowKnown = true;
                }
            }
            if (! any)
                continue;
            lo = std::min (lo, positionOf (t, down, d));
            hi = std::max (hi, positionOf (t, up, d));
            if (nowKnown)
                now = positionOf (t, current, d);
        }
        if (count == 0)
            continue;
        ++rings;
        const auto c = mixed ? colour::textSecondary : modui::sourceColour (firstSource).darker (0.12f);
        if (highlighted)
        {
            g.setColour (c.withAlpha (0.2f));
            g.drawEllipse (juce::Rectangle<float> (2.0f * t.radius + 10.0f, 2.0f * t.radius + 10.0f).withCentre (t.centre), 6.0f);
        }
        if (t.field)
        {
            // A value field: its range as a thin bar under it, the value now as a tick.
            const auto bar = juce::Rectangle<float> (t.area.getX() + 3.0f, t.area.getBottom() + 2.0f, t.area.getWidth() - 6.0f, 2.0f);
            g.setColour (c.withAlpha (live == 0 ? 0.2f : 0.25f));
            g.fillRect (bar);
            if (live > 0)
            {
                g.setColour (c.withAlpha (0.9f));
                g.fillRect (juce::Rectangle<float> (bar.getX() + lo * bar.getWidth(), bar.getY(), (hi - lo) * bar.getWidth(), bar.getHeight()));
                if (nowKnown)
                    g.fillRect (juce::Rectangle<float> (bar.getX() + now * bar.getWidth() - 1.0f, bar.getY() - 2.0f, 2.0f, bar.getHeight() + 4.0f));
            }
            continue;
        }
        if (live == 0)
        {
            // Every route bypassed (or empty): a faint trace that something is assigned.
            arc (t, t.radius, 0.0f, 1.0f, 1.0f, c.withAlpha (0.22f));
            continue;
        }
        arc (t, t.radius, 0.0f, 1.0f, 1.0f, c.withAlpha (0.13f));
        arc (t, t.radius, lo, hi, selectedHere ? 2.6f : 2.0f, c.withAlpha (0.92f));
        if (nowKnown)
        {
            const float angle = t.start + now * (t.end - t.start);
            const auto at = t.centre.getPointOnCircumference (t.radius, angle);
            g.setColour (juce::Colours::white.withAlpha (0.9f));
            g.fillEllipse (juce::Rectangle<float> (7.0f, 7.0f).withCentre (at));
            g.setColour (c);
            g.fillEllipse (juce::Rectangle<float> (4.6f, 4.6f).withCentre (at));
        }
    }
    shownRings = rings;
}

void ModulationOverlay::mouseDown (const juce::MouseEvent& e)
{
    const auto* t = ringAt (e.position);
    if (t == nullptr)
        return;
    if (e.mods.isPopupMenu())
    {
        // The routes on this control, to remove one.
        juce::PopupMenu menu;
        const auto dests = modui::destinationsShownBy (t->parameterId);
        auto& p = ospProcessor;
        for (const auto& r : routes)
            if (std::find (dests.begin(), dests.end(), r.route.dest) != dests.end())
            {
                const int slot = r.slot;
                menu.addItem (juce::String ("Remove ") + sourceLabels[static_cast<std::size_t> (mod::sourceIndex (r.route.source))] + juce::String::fromUTF8 (" \xe2\x86\x92 ")
                                  + mod::destInfo (r.route.dest).name,
                              [&p, slot] { p.removeModulationRoute (slot); });
            }
        menu.showMenuAsync (juce::PopupMenu::Options().withDeletionCheck (*this));
        return;
    }
    editSlot = routeFor (*t);
    editParameter = editSlot >= 0 ? ospProcessor.parameters.getParameter (OspAudioProcessor::modRouteId (editSlot, "depth")) : nullptr;
    if (editParameter == nullptr)
        return;
    editFrom = editParameter->convertFrom0to1 (editParameter->getValue());
    ospProcessor.undoManager.beginNewTransaction ("Modulation depth");
    editParameter->beginChangeGesture();
}

void ModulationOverlay::mouseDrag (const juce::MouseEvent& e)
{
    if (editParameter == nullptr)
        return;
    // Up deepens, down turns it around; the same route the list shows.
    const float fine = e.mods.isAltDown() || e.mods.isShiftDown() ? 0.2f : 0.6f;
    const float depth = juce::jlimit (-100.0f, 100.0f, editFrom - fine * static_cast<float> (e.getDistanceFromDragStartY()));
    editParameter->setValueNotifyingHost (editParameter->convertTo0to1 (depth));
    repaint();
}

void ModulationOverlay::mouseUp (const juce::MouseEvent&)
{
    if (editParameter != nullptr)
        editParameter->endChangeGesture();
    editParameter = nullptr;
    editSlot = -1;
}

} // namespace osp::plugin
