#include "GrainEditor.h"

#include "core/PitchMath.h"
#include "io/AudioFileIO.h"

#include <tuple>

namespace osp::andorlab
{

namespace
{
    juce::Colour voiceColour (int voice)
    {
        return plugin::palette::spectrum (static_cast<float> ((voice * 5) % 16) / 15.0f);
    }

    double baseOf (const grain::GrainParams& p, grain::DnaParam d)
    {
        switch (d)
        {
            case grain::DnaParam::size: return p.sizeSeconds;
            case grain::DnaParam::density: return p.density;
            case grain::DnaParam::position: return p.position;
            case grain::DnaParam::positionScatter: return p.positionScatter;
            case grain::DnaParam::pitchScatter: return p.pitchScatter;
            case grain::DnaParam::tone: return p.toneHz;
            case grain::DnaParam::spread: return p.spread;
        }
        return 0.0;
    }

    juce::String formatValue (grain::DnaParam d, double v)
    {
        switch (d)
        {
            case grain::DnaParam::size: return v < 1.0 ? juce::String (juce::roundToInt (v * 1000.0)) + " ms" : juce::String (v, 2) + " s";
            case grain::DnaParam::density: return juce::String (v, v < 10.0 ? 1 : 0) + " /s";
            case grain::DnaParam::pitchScatter: return juce::String (v, 2) + " st";
            case grain::DnaParam::tone: return v < 1000.0 ? juce::String (juce::roundToInt (v)) + " Hz" : juce::String (v / 1000.0, 1) + " kHz";
            case grain::DnaParam::position:
            case grain::DnaParam::positionScatter:
            case grain::DnaParam::spread: break;
        }
        return juce::String (juce::roundToInt (v * 100.0)) + " %";
    }
}

//==============================================================================
GrainWaveform::GrainWaveform (GrainProcessor& p) : processor (p)
{
    emptyText = "Drop a sample or an ANDOR Phrase Note Atlas (.andoratlas) here";
    setTitle ("Grain source");
}

void GrainWaveform::refresh()
{
    const auto& doc = processor.document();
    if (doc.source != nullptr)
        setOverview (doc.source->peakMin, doc.source->peakMax, doc.source->audio->numFrames(), doc.source->audio->sampleRate);
    else
        setOverview ({}, {}, 0, 48000.0);
}

void GrainWaveform::setGrains (std::vector<Dot> dots, std::vector<std::pair<float, float>> regions)
{
    grains = std::move (dots);
    activeRegions = std::move (regions);
    repaint();
}

void GrainWaveform::paintOverlay (juce::Graphics& g, juce::Rectangle<float> area)
{
    if (! hasAudio())
        return;
    const auto& doc = processor.document();
    const auto params = processor.currentParams();
    const double total = static_cast<double> (totalFrames());

    // Atlas regions (ANDOR Phrase notes): thin labelled bands.
    if (doc.atlas != nullptr)
        for (const auto& n : doc.atlas->notes)
        {
            if (! n.included)
                continue;
            const auto range = n.grainRange();
            const float x0 = xOf (static_cast<double> (range.start)), x1 = xOf (static_cast<double> (range.end));
            const auto colour = plugin::palette::spectrum (static_cast<float> (n.playKey() % 12) / 11.0f);
            g.setColour (colour.withAlpha (0.12f));
            g.fillRect (juce::Rectangle<float> (x0, area.getY() + 16.0f, std::max (1.0f, x1 - x0), area.getHeight() - 30.0f));
            g.setColour (colour.withAlpha (0.8f));
            g.fillRect (juce::Rectangle<float> (x0, area.getY() + 16.0f, std::max (1.0f, x1 - x0), 2.0f));
            if (x1 - x0 > 20.0f)
            {
                g.setFont (ui::label (10.5f));
                g.drawText (midiNoteName (n.playKey()), juce::Rectangle<float> (x0 + 2.0f, area.getY() + 1.0f, 60.0f, 14.0f), juce::Justification::centredLeft);
            }
        }

    // The sounding voices' regions, then POSITION and SCATTER inside the first one (or the file).
    std::pair<float, float> region { 0.0f, 1.0f };
    if (! activeRegions.empty())
        region = activeRegions.front();
    else if (doc.atlas != nullptr && params.policy != grain::SourcePolicy::wholeSource)
        region = { -1.0f, -1.0f };
    if (region.first >= 0.0f)
    {
        const double r0 = region.first * total, r1 = region.second * total;
        const double centre = r0 + params.position * (r1 - r0);
        const double half = 0.5 * params.positionScatter * (r1 - r0);
        g.setColour (design::colour::accent.withAlpha (0.10f));
        g.fillRect (juce::Rectangle<float> (xOf (centre - half), area.getY(), std::max (1.0f, xOf (centre + half) - xOf (centre - half)), area.getHeight()));
        g.setColour (design::colour::accent.withAlpha (0.85f));
        g.drawLine (xOf (centre), area.getY(), xOf (centre), area.getBottom(), 1.4f);
        g.setFont (ui::label (10.0f));
        g.drawText ("POSITION", juce::Rectangle<float> (xOf (centre) + 4.0f, area.getBottom() - 28.0f, 80.0f, 12.0f), juce::Justification::centredLeft);
    }
    // Grains: dots at their read position, scattered vertically by voice, sized by their window.
    for (const auto& d : grains)
    {
        const float x = xOf (d.position * total);
        const float lane = 0.2f + 0.6f * static_cast<float> ((d.voice * 37) % 16) / 15.0f;
        const float y = area.getY() + area.getHeight() * lane;
        const float radius = 1.5f + 3.5f * d.level;
        g.setColour (voiceColour (d.voice).withAlpha (0.25f + 0.65f * d.level));
        g.fillEllipse (x - radius, y - radius, 2.0f * radius, 2.0f * radius);
    }
    // The grain window's shape in the corner.
    auto box = juce::Rectangle<float> (area.getRight() - 96.0f, area.getY() + 4.0f, 88.0f, 34.0f);
    g.setColour (design::colour::wellText.withAlpha (0.6f));
    g.setFont (ui::label (9.5f));
    g.drawText ("WINDOW", box.removeFromTop (11.0f), juce::Justification::centredLeft);
    juce::Path w;
    for (int i = 0; i <= 40; ++i)
    {
        const double x = i / 40.0;
        double v = 0.0;
        switch (params.window)
        {
            case grain::GrainWindow::hann: v = 0.5 - 0.5 * std::cos (andor::twoPi * x); break;
            case grain::GrainWindow::soft: v = x < 0.25 ? 0.5 - 0.5 * std::cos (andor::pi * x / 0.25) : x > 0.75 ? 0.5 - 0.5 * std::cos (andor::pi * (1.0 - x) / 0.25) : 1.0; break;
            case grain::GrainWindow::percussive: v = std::min (1.0, x / 0.04) * std::exp (-5.0 * std::max (0.0, x - 0.04) / 0.96) * std::min (1.0, (1.0 - x) / 0.04); break;
        }
        const float px = box.getX() + box.getWidth() * static_cast<float> (x), py = box.getBottom() - box.getHeight() * static_cast<float> (v);
        i == 0 ? w.startNewSubPath (px, py) : w.lineTo (px, py);
    }
    g.setColour (design::colour::accent.withAlpha (0.8f));
    g.strokePath (w, juce::PathStrokeType (1.2f));
}

//==============================================================================
KeyDnaEditor::KeyDnaEditor (GrainProcessor& p) : processor (p)
{
    addAndMakeVisible (keys);
    keys.onNote = [this] (int key, bool down) { processor.audition (key, down); };
    keys.lookOf = [this] (int key)
    {
        KeyboardMap::KeyLook look;
        for (const auto& [k, eff] : voices)
            if (k == key)
                look.fill = design::colour::accentSoft;
        return look;
    };
    setRange (21, 108);
    setTitle ("KEY DNA curve editor");
}

void KeyDnaEditor::setParam (grain::DnaParam p)
{
    current = p;
    repaint();
}

void KeyDnaEditor::setVoices (std::vector<std::pair<int, std::array<float, grain::numDnaParams>>> v)
{
    if (v != voices)
    {
        voices = std::move (v);
        std::array<std::uint32_t, 4> mask {};
        for (const auto& [k, eff] : voices)
            mask[static_cast<std::size_t> (k >> 5)] |= 1u << (k & 31);
        keys.setSounding (mask);
        repaint();
    }
}

void KeyDnaEditor::setRange (int lowKey, int highKey)
{
    keys.setRange (lowKey, highKey);
    low = keys.lowKey();
    high = keys.highKey();
    repaint();
}

void KeyDnaEditor::resized()
{
    auto r = getLocalBounds();
    keys.setBounds (r.removeFromBottom (40).withTrimmedLeft (44));
}

juce::Rectangle<float> KeyDnaEditor::plot() const
{
    return getLocalBounds().toFloat().withTrimmedBottom (46.0f).withTrimmedLeft (44.0f).withTrimmedTop (18.0f).reduced (0.0f, 4.0f);
}

float KeyDnaEditor::xOfKey (float key) const
{
    // Keys line up with the keyboard below (white-key layout), fractional keys in between.
    const int k0 = juce::jlimit (low, high, static_cast<int> (std::floor (key)));
    const int k1 = std::min (high, k0 + 1);
    const float f = juce::jlimit (0.0f, 1.0f, key - static_cast<float> (k0));
    const float x0 = keys.keyBounds (k0).getCentreX(), x1 = keys.keyBounds (k1).getCentreX();
    return static_cast<float> (keys.getX()) + x0 + f * (x1 - x0);
}

float KeyDnaEditor::keyOfX (float x) const
{
    float best = static_cast<float> (low);
    float bestDistance = 1.0e9f;
    for (int k = low; k <= high; ++k)
    {
        const float d = std::abs (xOfKey (static_cast<float> (k)) - x);
        if (d < bestDistance)
        {
            bestDistance = d;
            best = static_cast<float> (k);
        }
    }
    return best;
}

float KeyDnaEditor::yOf (float value) const
{
    const auto r = plot();
    return r.getCentreY() - value * 0.5f * r.getHeight();
}

float KeyDnaEditor::valueOfY (float y) const
{
    const auto r = plot();
    return juce::jlimit (-1.0f, 1.0f, (r.getCentreY() - y) / (0.5f * r.getHeight()));
}

int KeyDnaEditor::pointAt (juce::Point<float> p) const
{
    const auto& c = processor.dnaSettings().curves[static_cast<std::size_t> (current)];
    for (int i = 0; i < c.count; ++i)
    {
        const auto& pt = c.points[static_cast<std::size_t> (i)];
        if (juce::Point<float> (xOfKey (pt.key), yOf (pt.value)).getDistanceFrom (p) < 8.0f)
            return i;
    }
    return -1;
}

void KeyDnaEditor::paint (juce::Graphics& g)
{
    const auto r = plot();
    ui::well (g, r.expanded (0.0f, 4.0f));
    const auto params = processor.currentParams();
    const auto table = processor.dnaTable();
    const auto p = current;
    const auto& curve = processor.dnaSettings().curves[static_cast<std::size_t> (p)];

    // Axis: +1 / 0 / -1 of the knob's half travel, labelled in the parameter's units at the base.
    g.setColour (design::colour::wellGrid);
    g.drawHorizontalLine (juce::roundToInt (r.getCentreY()), r.getX(), r.getRight());
    for (float v : { -1.0f, -0.5f, 0.5f, 1.0f })
        g.drawHorizontalLine (juce::roundToInt (yOf (v)), r.getX(), r.getRight());
    const double baseNorm = grain::toNormalised (p, baseOf (params, p));
    g.setFont (ui::value (9.5f));
    g.setColour (design::colour::textMicro);
    for (float v : { -1.0f, 0.0f, 1.0f })
        g.drawText (formatValue (p, grain::fromNormalised (p, baseNorm + 0.5 * v)), juce::Rectangle<float> (0.0f, yOf (v) - 7.0f, 42.0f, 14.0f), juce::Justification::centredRight);

    // Per-key individuality (the stable variation, scaled by VARIATION) as bars.
    for (int k = low; k <= high; ++k)
    {
        const float v = static_cast<float> (params.dna.variationAmount) * table.variation (k, p);
        const float x = xOfKey (static_cast<float> (k));
        const float y0 = yOf (static_cast<float> (params.dna.curveAmount) * table.curve (k, p));
        g.setColour (plugin::palette::lavender.withAlpha (0.55f));
        g.drawLine (x, y0, x, yOf (static_cast<float> (params.dna.curveAmount) * table.curve (k, p) + v), 2.0f);
    }
    // The curve (scaled by CURVE AMOUNT) and its points.
    juce::Path line;
    for (int k = low; k <= high; ++k)
    {
        const float x = xOfKey (static_cast<float> (k)), y = yOf (static_cast<float> (params.dna.curveAmount) * curve.evaluate (static_cast<float> (k)));
        k == low ? line.startNewSubPath (x, y) : line.lineTo (x, y);
    }
    g.setColour (plugin::palette::gold);
    g.strokePath (line, juce::PathStrokeType (2.0f));
    for (int i = 0; i < curve.count; ++i)
    {
        const auto& pt = curve.points[static_cast<std::size_t> (i)];
        const auto c = juce::Point<float> (xOfKey (pt.key), yOf (static_cast<float> (params.dna.curveAmount) * pt.value));
        g.setColour (i == dragging ? design::colour::accent : plugin::palette::gold.brighter (0.3f));
        g.fillEllipse (c.x - 4.5f, c.y - 4.5f, 9.0f, 9.0f);
    }
    // Sounding keys: their effective value now (DNA + trigger variation + modulation).
    for (const auto& [k, eff] : voices)
    {
        const float x = xOfKey (static_cast<float> (k));
        const float y = yOf (2.0f * (eff[static_cast<std::size_t> (p)] - static_cast<float> (baseNorm)));
        g.setColour (design::colour::accent);
        g.drawLine (x, r.getY(), x, r.getBottom(), 0.8f);
        g.fillEllipse (x - 5.0f, y - 5.0f, 10.0f, 10.0f);
    }
    // Header line: parameter, base, hovered key's profile.
    g.setColour (design::colour::textSecondary);
    g.setFont (ui::label (11.0f));
    juce::String header = juce::String (grain::dnaParamName (p)) + "   base " + formatValue (p, baseOf (params, p));
    if (hoverKey >= 0)
    {
        const double value = grain::fromNormalised (p, baseNorm + 0.5 * table.offset (hoverKey, p));
        header << "   " << midiNoteName (hoverKey) << ": " << formatValue (p, value) << "  (curve " << juce::String (params.dna.curveAmount * table.curve (hoverKey, p), 2)
               << ", individuality " << juce::String (params.dna.variationAmount * table.variation (hoverKey, p), 2) << ")";
    }
    g.drawText (header, juce::Rectangle<float> (r.getX(), 0.0f, r.getWidth(), 16.0f), juce::Justification::centredLeft);
    g.setColour (design::colour::textMicro);
    g.setFont (ui::value (9.5f));
    g.drawText ("click: add point  /  drag: move  /  double-click: remove  /  shift-drag: draw", r.reduced (8.0f, 4.0f).removeFromBottom (12.0f),
                juce::Justification::centredRight);
}

void KeyDnaEditor::mouseDown (const juce::MouseEvent& e)
{
    if (! plot().contains (e.position))
        return;
    dragging = pointAt (e.position);
    drawing = e.mods.isShiftDown();
    if (dragging < 0 && ! drawing)
    {
        auto curve = processor.dnaSettings().curves[static_cast<std::size_t> (current)];
        const float key = keyOfX (e.position.x);
        const float amount = std::max (0.05f, static_cast<float> (processor.currentParams().dna.curveAmount));
        if (curve.addPoint (key, valueOfY (e.position.y) / amount))
        {
            processor.setCurve (current, curve);
            dragging = pointAt (e.position);
        }
    }
    repaint();
}

void KeyDnaEditor::mouseDrag (const juce::MouseEvent& e)
{
    const float amount = std::max (0.05f, static_cast<float> (processor.currentParams().dna.curveAmount));
    auto curve = processor.dnaSettings().curves[static_cast<std::size_t> (current)];
    if (drawing)
    {
        curve.addPoint (keyOfX (e.position.x), valueOfY (e.position.y) / amount);
        processor.setCurve (current, curve);
    }
    else if (dragging >= 0 && dragging < curve.count)
    {
        curve.removePoint (dragging);
        curve.addPoint (keyOfX (e.position.x), valueOfY (e.position.y) / amount);
        processor.setCurve (current, curve);
        dragging = pointAt ({ xOfKey (keyOfX (e.position.x)), yOf (juce::jlimit (-1.0f, 1.0f, valueOfY (e.position.y) / amount) * amount) });
    }
    hoverKey = static_cast<int> (keyOfX (e.position.x));
    repaint();
}

void KeyDnaEditor::mouseUp (const juce::MouseEvent&)
{
    dragging = -1;
    drawing = false;
    repaint();
}

void KeyDnaEditor::mouseDoubleClick (const juce::MouseEvent& e)
{
    const int i = pointAt (e.position);
    if (i < 0)
        return;
    auto curve = processor.dnaSettings().curves[static_cast<std::size_t> (current)];
    curve.removePoint (i);
    processor.setCurve (current, curve);
    repaint();
}

void KeyDnaEditor::mouseMove (const juce::MouseEvent& e)
{
    const int key = plot().contains (e.position) ? static_cast<int> (keyOfX (e.position.x)) : -1;
    if (key != hoverKey)
    {
        hoverKey = key;
        repaint();
    }
}

void KeyDnaEditor::mouseExit (const juce::MouseEvent&)
{
    hoverKey = -1;
    repaint();
}

//==============================================================================
GrainEditor::Content::Content (GrainProcessor& p)
    : processor (p),
      wave (p),
      dna (p),
      sourceKey (ui::u8 ("SOURCE  ▾")),
      presetKey (ui::u8 ("PRESET  ▾")),
      quality (p.state, "quality", { "ECO", "STANDARD", "HIGH" }),
      window (p.state, "window", { "HANN", "SOFT", "PERC" }),
      size (p.state, "size", "Size", true, plugin::palette::amber),
      density (p.state, "density", "Density", true, plugin::palette::gold),
      position (p.state, "position", "Position", true, plugin::palette::coral),
      scatter (p.state, "scatter", "Scatter", true, plugin::palette::rose),
      pitch (p.state, "pitch", "Pitch", true, plugin::palette::lavender),
      spread (p.state, "spread", "Spread", true, plugin::palette::mineral),
      pitchScatter (p.state, "pitchScatter", "Pitch Scat."),
      reverse (p.state, "reverse", "Reverse"),
      jitter (p.state, "jitter", "Jitter"),
      level (p.state, "grainLevel", "Level"),
      master (p.state, "gain", "Master"),
      curveAmount (p.state, "dnaCurve", "Curve Amt", true, plugin::palette::gold),
      variation (p.state, "dnaVariation", "Variation", true, plugin::palette::lavender),
      individuality (p.state, "dnaIndividual", "Individual", true, plugin::palette::mineral),
      lfo1Rate (p.state, "lfo1Rate", "LFO 1"),
      lfo2Rate (p.state, "lfo2Rate", "LFO 2"),
      auxAttack (p.state, "auxAttack", "Aux A"),
      auxDecay (p.state, "auxDecay", "Aux D"),
      auxSustain (p.state, "auxSustain", "Aux S"),
      auxRelease (p.state, "auxRelease", "Aux R"),
      macro1 (p.state, "macro1", "Macro 1", true),
      macro2 (p.state, "macro2", "Macro 2", true),
      triggerVar (p.state, "triggerVar", "Trig. Var"),
      lfo1Shape (p.state, "lfo1Shape", "LFO 1 shape"),
      lfo2Shape (p.state, "lfo2Shape", "LFO 2 shape"),
      oscMix (p.state, "oscMix", "Osc / Grain", true, plugin::palette::amber),
      oscShape (p.state, "oscShape", "Shape", true, plugin::palette::gold),
      oscTune (p.state, "oscTune", "Osc Tune"),
      tone (p.state, "tone", "Tone", true, plugin::palette::coral),
      resonance (p.state, "resonance", "Resonance"),
      attack (p.state, "attack", "Attack"),
      decay (p.state, "decay", "Decay"),
      sustain (p.state, "sustain", "Sustain"),
      release (p.state, "release", "Release"),
      velocity (p.state, "velocity", "Velocity"),
      space (p.state, "space", "Space"),
      echo (p.state, "echo", "Echo"),
      policy (p.state, "policy", "Atlas source"),
      fixedNote (p.state, "fixedNote", "Fixed Note"),
      root (p.state, "root", "Root"),
      seed (p.state, "seed", "Seed")
{
    for (auto* c : std::initializer_list<juce::Component*> { &wave, &dna, &sourceKey, &presetKey, &quality, &window, &size, &density, &position,
                                                             &scatter, &pitch, &spread, &pitchScatter, &reverse, &jitter, &level, &master,
                                                             &curveAmount, &variation, &individuality, &newSeed, &randomise, &resetCurve, &zoom,
                                                             &lfo1Rate, &lfo2Rate, &auxAttack, &auxDecay, &auxSustain, &auxRelease, &macro1,
                                                             &macro2, &triggerVar, &lfo1Shape, &lfo2Shape, &oscMix, &oscShape, &oscTune, &tone,
                                                             &resonance, &attack, &decay, &sustain, &release, &velocity, &space, &echo, &policy,
                                                             &fixedNote, &root, &seed, &deterministic, &tabDna, &tabMod, &tabVoice })
        addAndMakeVisible (c);
    for (int i = 0; i < grain::numDnaParams; ++i)
    {
        auto key = std::make_unique<Key> (grain::dnaParamName (static_cast<grain::DnaParam> (i)));
        key->onClick = [this, i]
        {
            dna.setParam (static_cast<grain::DnaParam> (i));
            for (int k = 0; k < grain::numDnaParams; ++k)
                paramKeys[static_cast<std::size_t> (k)]->setActive (k == i);
        };
        addAndMakeVisible (*key);
        paramKeys.push_back (std::move (key));
    }
    paramKeys[0]->setActive (true);
    for (int s = 0; s < grain::numModSlots; ++s)
    {
        const auto n = juce::String (s + 1);
        modSources.push_back (std::make_unique<Choice> (p.state, "mod" + n + "Src", "Mod " + n + " source"));
        modDests.push_back (std::make_unique<Choice> (p.state, "mod" + n + "Dst", "to"));
        modAmounts.push_back (std::make_unique<Knob> (p.state, "mod" + n + "Amt", "Amount"));
        modAmounts.back()->slider().getProperties().set ("bipolar", true);
        addAndMakeVisible (*modSources.back());
        addAndMakeVisible (*modDests.back());
        addAndMakeVisible (*modAmounts.back());
    }
    sourceKey.onClick = [this] { showSourceMenu(); };
    presetKey.onClick = [this] { showPresetMenu(); };
    newSeed.onClick = [this] { processor.regenerateSeed(); dna.refresh(); };
    randomise.onClick = [this] { processor.randomiseCurve (dna.param()); dna.refresh(); };
    resetCurve.onClick = [this] { processor.resetCurve (dna.param()); dna.refresh(); };
    zoom.onClick = [this]
    {
        zoomed = ! zoomed;
        zoom.setActive (zoomed);
        dna.setRange (zoomed ? 48 : 21, zoomed ? 84 : 108);
    };
    tabDna.onClick = [this] { showTab (Tab::dna); };
    tabMod.onClick = [this] { showTab (Tab::modulation); };
    tabVoice.onClick = [this] { showTab (Tab::voice); };
    if (auto* param = p.state.getParameter ("deterministic"))
    {
        deterministicAttachment = std::make_unique<juce::ParameterAttachment> (*param, [this] (float v) { deterministic.setActive (v > 0.5f); });
        deterministicAttachment->sendInitialUpdate();
        deterministic.onClick = [this, param] { deterministicAttachment->setValueAsCompleteGesture (param->getValue() > 0.5f ? 0.0f : 1.0f); };
    }
    showTab (Tab::dna);
}

void GrainEditor::Content::showTab (Tab t)
{
    tab = t;
    const bool d = t == Tab::dna, m = t == Tab::modulation, v = t == Tab::voice;
    for (auto* c : std::initializer_list<juce::Component*> { &dna, &curveAmount, &variation, &individuality, &newSeed, &randomise, &resetCurve, &zoom })
        c->setVisible (d);
    for (auto& k : paramKeys)
        k->setVisible (d);
    for (std::size_t s = 0; s < modSources.size(); ++s)
    {
        modSources[s]->setVisible (m);
        modDests[s]->setVisible (m);
        modAmounts[s]->setVisible (m);
    }
    for (auto* c : std::initializer_list<juce::Component*> { &lfo1Rate, &lfo2Rate, &auxAttack, &auxDecay, &auxSustain, &auxRelease, &macro1, &macro2,
                                                             &triggerVar, &lfo1Shape, &lfo2Shape })
        c->setVisible (m);
    for (auto* c : std::initializer_list<juce::Component*> { &oscMix, &oscShape, &oscTune, &tone, &resonance, &attack, &decay, &sustain, &release,
                                                             &velocity, &space, &echo, &policy, &fixedNote, &root, &seed, &deterministic })
        c->setVisible (v);
    tabDna.setActive (d);
    tabMod.setActive (m);
    tabVoice.setActive (v);
    repaint();
}

void GrainEditor::Content::refresh()
{
    wave.refresh();
    dna.refresh();
    repaint();
}

void GrainEditor::Content::showSourceMenu()
{
    juce::PopupMenu menu;
    auto choose = [this] (const juce::String& title, const juce::String& patterns, std::function<void (const juce::File&)> then)
    {
        chooser = std::make_unique<juce::FileChooser> (title, juce::File(), patterns);
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [then] (const juce::FileChooser& fc)
                              {
                                  if (fc.getResult().existsAsFile())
                                      then (fc.getResult());
                              });
    };
    menu.addItem ("Load sample...", [this, choose] { choose ("Load a sample", "*.wav;*.wave;*.aif;*.aiff;*.aifc", [this] (const juce::File& f) { processor.loadSample (f); }); });
    menu.addItem ("Load Note Atlas from ANDOR Phrase...", [this, choose] { choose ("Load a Note Atlas", "*.andoratlas", [this] (const juce::File& f) { processor.loadAtlas (f); }); });
    menu.addSeparator();
    menu.addItem ("Example: sustained tone", [this] { processor.loadExample (GrainProcessor::Example::pad); });
    menu.addItem ("Example: sung phrase as a Note Atlas", [this] { processor.loadExample (GrainProcessor::Example::phraseAtlas); });
    menu.addItem ("Oscillator only (no source)", [this] { processor.clearSource(); });
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&sourceKey));
}

void GrainEditor::Content::showPresetMenu()
{
    juce::PopupMenu menu;
    menu.addItem ("Save preset...", [this]
                  {
                      chooser = std::make_unique<juce::FileChooser> ("Save ANDOR Grain preset", GrainProcessor::presetFolder(), "*.andorgrain");
                      chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting, [this] (const juce::FileChooser& fc)
                                            {
                                                std::string error;
                                                if (fc.getResult() != juce::File())
                                                    processor.savePreset (fc.getResult().withFileExtension (GrainProcessor::presetExtension), error);
                                            });
                  });
    menu.addItem ("Load preset...", [this]
                  {
                      chooser = std::make_unique<juce::FileChooser> ("Load ANDOR Grain preset", GrainProcessor::presetFolder(), "*.andorgrain");
                      chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this] (const juce::FileChooser& fc)
                                            {
                                                std::string error;
                                                if (fc.getResult().existsAsFile())
                                                    processor.loadPreset (fc.getResult(), error);
                                            });
                  });
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&presetKey));
}

void GrainEditor::Content::resized()
{
    sourceKey.setBounds (560, 34, 150, 34);
    presetKey.setBounds (720, 34, 120, 34);
    quality.setBounds (862, 48, 230, 26);
    master.setBounds (1124, 18, 82, 84);
    wave.setBounds (30, 108, 1180, 206);

    // GRAIN panel.
    int x = 48;
    for (auto* k : { &size, &density, &position, &scatter, &pitch, &spread })
    {
        k->setBounds (x, 348, 104, 112);
        x += 114;
    }
    x = 752;
    for (auto* k : { &pitchScatter, &reverse, &jitter, &level })
    {
        k->setBounds (x, 344, 84, 70);
        x += 96;
    }
    window.setBounds (756, 434, 300, 26);

    tabDna.setBounds (36, 488, 140, 28);
    tabMod.setBounds (182, 488, 140, 28);
    tabVoice.setBounds (328, 488, 140, 28);

    // KEY DNA tab.
    for (std::size_t i = 0; i < paramKeys.size(); ++i)
        paramKeys[i]->setBounds (48 + static_cast<int> (i) * 116, 532, 110, 26);
    dna.setBounds (48, 566, 900, 232);
    curveAmount.setBounds (966, 532, 76, 92);
    variation.setBounds (1046, 532, 76, 92);
    individuality.setBounds (1126, 532, 76, 92);
    newSeed.setBounds (966, 664, 114, 28);
    randomise.setBounds (1086, 664, 118, 28);
    resetCurve.setBounds (966, 700, 114, 28);
    zoom.setBounds (1086, 700, 118, 28);

    // MODULATION tab: 8 slots in two columns, sources on the right.
    for (int s = 0; s < grain::numModSlots; ++s)
    {
        const int col = s / 4, row = s % 4;
        const int sx = 48 + col * 400, sy = 534 + row * 64;
        modSources[static_cast<std::size_t> (s)]->setBounds (sx, sy, 140, 44);
        modDests[static_cast<std::size_t> (s)]->setBounds (sx + 150, sy, 140, 44);
        modAmounts[static_cast<std::size_t> (s)]->setBounds (sx + 300, sy - 6, 70, 62);
    }
    lfo1Rate.setBounds (858, 530, 70, 70);
    lfo1Shape.setBounds (934, 548, 100, 40);
    lfo2Rate.setBounds (858, 610, 70, 70);
    lfo2Shape.setBounds (934, 628, 100, 40);
    macro1.setBounds (1050, 526, 76, 92);
    macro2.setBounds (1130, 526, 76, 92);
    x = 858;
    for (auto* k : { &auxAttack, &auxDecay, &auxSustain, &auxRelease })
    {
        k->setBounds (x, 702, 66, 70);
        x += 70;
    }
    triggerVar.setBounds (1140, 702, 66, 70);

    // OSC + VOICE tab.
    x = 48;
    for (auto* k : { &oscMix, &oscShape, &tone })
    {
        k->setBounds (x, 534, 100, 108);
        x += 110;
    }
    x = 48;
    for (auto* k : { &oscTune, &resonance, &velocity })
    {
        k->setBounds (x, 664, 90, 72);
        x += 110;
    }
    x = 420;
    for (auto* k : { &attack, &decay, &sustain, &release })
    {
        k->setBounds (x, 540, 80, 80);
        x += 88;
    }
    x = 420;
    for (auto* k : { &space, &echo })
    {
        k->setBounds (x, 664, 80, 72);
        x += 88;
    }
    policy.setBounds (820, 540, 180, 44);
    fixedNote.setBounds (820, 600, 80, 72);
    root.setBounds (910, 600, 80, 72);
    seed.setBounds (1000, 600, 80, 72);
    deterministic.setBounds (1090, 618, 120, 30);
}

void GrainEditor::Content::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    g.fillAll (design::draw::surround());
    design::draw::housing (g, bounds.reduced (6.0f));
    ui::logoBlock (g, { 40.0f, 30.0f, 420.0f, 56.0f }, "Grain", ui::u8 ("KEY DNA GRANULAR HYBRID  ·  RESEARCH PROTOTYPE"));
    g.setFont (ui::label (11.0f));
    g.setColour (design::colour::textSecondary);
    g.drawText ("QUALITY", juce::Rectangle<float> (866.0f, 32.0f, 120.0f, 14.0f), juce::Justification::centredLeft);
    g.setColour (design::colour::text);
    g.setFont (ui::value (11.0f));
    g.drawFittedText (statusText, juce::Rectangle<int> (866, 78, 250, 16), juce::Justification::centredLeft, 1);

    const auto& doc = processor.document();
    juce::String source = doc.source != nullptr ? juce::String (doc.source->fileName)
                        : doc.sourceMissing ? "Missing: " + juce::String (doc.missingName) : juce::String ("No source: oscillator only");
    if (doc.source != nullptr)
    {
        source << ui::u8 ("  ·  ") << juce::String (doc.source->audio->durationSeconds(), 1) << " s";
        if (doc.atlas != nullptr)
            source << ui::u8 ("  ·  Note Atlas: ") << static_cast<int> (doc.atlas->notes.size()) << " notes from " << juce::String (doc.atlas->createdBy);
        else
            source << ui::u8 ("  ·  root ") << midiNoteName (juce::roundToInt (processor.currentParams().wholeRootMidi));
    }
    g.setColour (design::colour::textSecondary);
    g.setFont (ui::label (11.5f));
    g.drawText (source, juce::Rectangle<float> (32.0f, 90.0f, 900.0f, 16.0f), juce::Justification::centredLeft);

    ui::panel (g, { 30.0f, 322.0f, 1180.0f, 152.0f }, "GRAIN");
    g.setFont (ui::label (11.0f));
    g.setColour (design::colour::textSecondary);
    g.drawText ("WINDOW", juce::Rectangle<float> (758.0f, 418.0f, 100.0f, 14.0f), juce::Justification::centredLeft);
    ui::panel (g, { 30.0f, 480.0f, 1180.0f, 324.0f });
    if (tab == Tab::dna)
    {
        g.setColour (design::colour::textSecondary);
        g.setFont (ui::label (11.0f));
        g.drawText ("SEED " + juce::String (juce::roundToInt (processor.currentParams().dna.seed)), juce::Rectangle<float> (968.0f, 640.0f, 200.0f, 16.0f),
                    juce::Justification::centredLeft);
        g.setFont (ui::value (10.5f));
        g.setColour (design::colour::textMicro);
        g.drawFittedText ("Each key keeps its profile: the drawn curve, plus a stable individuality from the seed. "
                          "Smooth keeps neighbours related; Individual lets them differ.",
                          juce::Rectangle<int> (966, 740, 240, 52), juce::Justification::topLeft, 4);
    }
    if (tab == Tab::voice)
    {
        g.setColour (design::colour::textSecondary);
        g.setFont (ui::label (11.0f));
        g.drawText ("AMP ENVELOPE", juce::Rectangle<float> (422.0f, 524.0f, 200.0f, 14.0f), juce::Justification::centredLeft);
        g.drawText ("SOURCE AND DETERMINISM", juce::Rectangle<float> (822.0f, 524.0f, 300.0f, 14.0f), juce::Justification::centredLeft);
    }
    g.setColour (design::colour::textMicro);
    g.setFont (ui::value (11.0f));
    g.drawText ("voices " + juce::String (processor.grainsDisplay.voices.load()) + ui::u8 ("  ·  grains ") + juce::String (processor.grainsDisplay.grains.load())
                    + " / " + juce::String (grain::limitsFor (processor.currentParams().quality).grainBudget) + ui::u8 ("  ·  ANDOR Grain " JucePlugin_VersionString),
                juce::Rectangle<float> (36.0f, 816.0f, 900.0f, 14.0f), juce::Justification::centredLeft);
}

//==============================================================================
GrainEditor::GrainEditor (GrainProcessor& p) : AudioProcessorEditor (p), processor (p), content (p), scaler (content, designWidth, designHeight)
{
    setLookAndFeel (&look);
    addAndMakeVisible (scaler);
    setResizable (true, true);
    constrainer.setFixedAspectRatio (static_cast<double> (designWidth) / designHeight);
    constrainer.setSizeLimits (designWidth * 6 / 10, designHeight * 6 / 10, designWidth * 2, designHeight * 2);
    setConstrainer (&constrainer);
    setSize (designWidth, designHeight);
    processor.onDocumentChanged = [this] { content.refresh(); };
    content.refresh();
    startTimerHz (30);
}

GrainEditor::~GrainEditor()
{
    processor.onDocumentChanged = nullptr;
    setLookAndFeel (nullptr);
}

void GrainEditor::resized()
{
    scaler.setBounds (getLocalBounds());
}

void GrainEditor::timerCallback()
{
    const int count = juce::jlimit (0, 256, processor.grainsDisplay.count.load (std::memory_order_acquire));
    std::vector<GrainWaveform::Dot> dots;
    dots.reserve (static_cast<std::size_t> (count));
    for (int i = 0; i < count; ++i)
        dots.push_back ({ processor.grainsDisplay.a[static_cast<std::size_t> (i)].load(), processor.grainsDisplay.b[static_cast<std::size_t> (i)].load(),
                          processor.grainsDisplay.c[static_cast<std::size_t> (i)].load() });
    const int voices = juce::jlimit (0, 16, processor.voicesDisplay.count.load (std::memory_order_acquire));
    std::vector<std::pair<float, float>> regions;
    std::vector<std::pair<int, std::array<float, grain::numDnaParams>>> effective;
    for (int i = 0; i < voices; ++i)
    {
        regions.emplace_back (processor.voicesDisplay.regionStart[static_cast<std::size_t> (i)].load(), processor.voicesDisplay.regionEnd[static_cast<std::size_t> (i)].load());
        std::array<float, grain::numDnaParams> e {};
        for (int p = 0; p < grain::numDnaParams; ++p)
            e[static_cast<std::size_t> (p)] = processor.voicesDisplay.effective[static_cast<std::size_t> (i)][static_cast<std::size_t> (p)].load();
        effective.emplace_back (processor.voicesDisplay.keys[static_cast<std::size_t> (i)].load(), e);
    }
    content.wave.setGrains (std::move (dots), std::move (regions));
    content.dna.setVoices (std::move (effective));
    const auto text = processor.isBusy() ? processor.status() + "  " + juce::String (juce::roundToInt (100.0f * processor.progress())) + " %" : processor.status();
    if (text != content.statusText)
    {
        content.statusText = text;
        content.repaint();
    }
}

bool GrainEditor::isInterestedInFileDrag (const juce::StringArray& files)
{
    return files.size() >= 1 && (io::isSupportedAudioExtension (files[0].toStdString()) || files[0].endsWithIgnoreCase (".andoratlas"));
}

void GrainEditor::filesDropped (const juce::StringArray& files, int, int)
{
    if (files.isEmpty())
        return;
    const juce::File f (files[0]);
    if (f.hasFileExtension (".andoratlas"))
        processor.loadAtlas (f);
    else
        processor.loadSample (f);
}

} // namespace osp::andorlab
