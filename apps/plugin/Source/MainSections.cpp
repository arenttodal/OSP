#include "MainSections.h"

#include "EngineCard.h"

#include "engine/InstrumentEngine.h"

#include <cmath>

namespace osp::plugin
{

//==============================================================================
TriangleMix::TriangleMix (juce::AudioProcessorValueTreeState& state)
    : xAttachment (*state.getParameter ("mix.x"), [this] (float v) { x = v; repaint(); }),
      yAttachment (*state.getParameter ("mix.y"), [this] (float v) { y = v; repaint(); })
{
    xAttachment.sendInitialUpdate();
    yAttachment.sendInitialUpdate();
    setTooltip ("Three-layer mix: drag between A, B and C (double-click: all three equal)");
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
    setTitle ("Layer mix");
}

std::array<juce::Point<float>, 3> TriangleMix::corners() const
{
    auto r = getLocalBounds().toFloat().reduced (9.0f, 7.0f);
    const float w = std::min (r.getWidth(), r.getHeight() / 0.866f);
    const float h = w * 0.866f;
    r = r.withSizeKeepingCentre (w, h);
    return { juce::Point<float> (r.getX(), r.getBottom()), juce::Point<float> (r.getCentreX(), r.getY()),
             juce::Point<float> (r.getRight(), r.getBottom()) };
}

void TriangleMix::moveTo (juce::Point<float> where)
{
    const auto c = corners();
    const float w = c[2].x - c[0].x, h = c[0].y - c[1].y;
    // Into the triangle (the same clipping the engine applies), then back to x/y.
    const auto share = InstrumentEngine::triangleShares ((where.x - c[0].x) / std::max (1.0f, w), (c[0].y - where.y) / std::max (1.0f, h));
    xAttachment.setValueAsPartOfGesture (static_cast<float> (0.5 * share[1] + share[2]));
    yAttachment.setValueAsPartOfGesture (static_cast<float> (share[1]));
}

void TriangleMix::mouseDown (const juce::MouseEvent& e)
{
    dragging = true;
    xAttachment.beginGesture();
    yAttachment.beginGesture();
    moveTo (e.position);
}

void TriangleMix::mouseDrag (const juce::MouseEvent& e)
{
    if (dragging)
        moveTo (e.position);
}

void TriangleMix::mouseUp (const juce::MouseEvent&)
{
    if (! dragging)
        return;
    dragging = false;
    xAttachment.endGesture();
    yAttachment.endGesture();
}

void TriangleMix::mouseDoubleClick (const juce::MouseEvent&)
{
    xAttachment.setValueAsCompleteGesture (0.5f);
    yAttachment.setValueAsCompleteGesture (1.0f / 3.0f);
}

void TriangleMix::paint (juce::Graphics& g)
{
    using namespace palette;
    const auto c = corners();
    juce::Path shape;
    shape.addTriangle (c[0], c[1], c[2]);
    g.setColour (recessed);
    g.fillPath (shape);
    g.setColour (hairline.darker (0.08f));
    g.strokePath (shape, juce::PathStrokeType (1.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    const auto share = InstrumentEngine::triangleShares (x, y);
    const auto node = juce::Point<float> (c[0].x + x * (c[2].x - c[0].x), c[0].y - y * (c[0].y - c[1].y));
    // Threads to the corners, as strong as each layer's share; the node takes their colours.
    juce::Colour mix = layer (0).withAlpha (0.0f);
    for (std::size_t i = 0; i < 3; ++i)
    {
        g.setColour (layer (static_cast<int> (i)).withAlpha (0.15f + 0.7f * static_cast<float> (share[i])));
        g.drawLine ({ node, c[i] }, 1.2f);
        g.setColour (layer (static_cast<int> (i)));
        g.fillEllipse (c[i].x - 2.5f, c[i].y - 2.5f, 5.0f, 5.0f);
    }
    mix = layer (0).interpolatedWith (layer (1), static_cast<float> (share[1] / std::max (1.0e-6, share[0] + share[1])));
    mix = mix.interpolatedWith (layer (2), static_cast<float> (share[2]));
    juce::Path disc;
    disc.addEllipse (node.x - 6.0f, node.y - 6.0f, 12.0f, 12.0f);
    juce::DropShadow (juce::Colour (0x331e1c18), 3, { 0, 1 }).drawForPath (g, disc);
    g.setColour (raised);
    g.fillPath (disc);
    g.setColour (mix);
    g.strokePath (disc, juce::PathStrokeType (1.4f));
    g.setColour (accent);
    g.fillEllipse (node.x - 2.0f, node.y - 2.0f, 4.0f, 4.0f);

    g.setFont (fonts::make (10.0f, fonts::Weight::semibold));
    g.setColour (textDim);
    g.drawText ("A", juce::Rectangle<float> (c[0].x - 9.0f, c[0].y - 4.0f, 8.0f, 10.0f), juce::Justification::centredRight, false);
    g.drawText ("B", juce::Rectangle<float> (c[1].x - 4.0f, c[1].y - 8.0f, 8.0f, 8.0f).translated (7.0f, 2.0f), juce::Justification::centredLeft, false);
    g.drawText ("C", juce::Rectangle<float> (c[2].x + 2.0f, c[2].y - 4.0f, 8.0f, 10.0f), juce::Justification::centredLeft, false);
}

//==============================================================================
MixSection::MixSection (OspAudioProcessor& p) : processor (p), triangle (p.parameters)
{
    reimagined.getProperties().set ("blend", true);
    reimagined.getProperties().set ("thin", true);
    reimagined.getProperties().set ("leftColour", static_cast<juce::int64> (palette::hairline.darker (0.15f).getARGB()));
    reimagined.setTitle ("Original / Reimagined");
    reimagined.setTooltip ("Original <-> Reimagined: from the recording as it is to a transformed version of it");
    reimaginedAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (processor.parameters, "reimagined", reimagined);
    if (auto* param = processor.parameters.getParameter ("reimagined"))
        reimagined.setDoubleClickReturnValue (true, param->convertFrom0to1 (param->getDefaultValue()));
    addAndMakeVisible (reimagined);

    blend.getProperties().set ("blend", true);
    blend.setTitle ("Layer blend");
    blend.setTooltip ("Blend between the two layers (equal power: the middle is not quieter)");
    blendAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (processor.parameters, "ab.blend", blend);
    blend.setDoubleClickReturnValue (true, 0.5);
    addChildComponent (blend);
    addChildComponent (triangle);
}

void MixSection::setLayers (const std::array<bool, 3>& occupied)
{
    int n = 0;
    std::array<int, 3> which { 0, 1, 2 };
    for (int l = 0; l < 3; ++l)
        if (occupied[static_cast<std::size_t> (l)])
            which[static_cast<std::size_t> (n++)] = l;
    if (n == count && which == slots)
        return;
    count = n;
    slots = which;
    blend.getProperties().set ("leftColour", static_cast<juce::int64> (palette::layer (slots[0]).getARGB()));
    blend.getProperties().set ("rightColour", static_cast<juce::int64> (palette::layer (slots[1]).getARGB()));
    blend.setVisible (count == 2);
    triangle.setVisible (count == 3);
    resized();
    repaint();
}

void MixSection::resized()
{
    auto area = getLocalBounds().reduced (18, 7);
    captionArea = area.removeFromLeft (count >= 2 ? 176 : 0);
    if (count >= 2)
        area.removeFromLeft (14);
    // Both sliders share one span; captions and letters sit in the margins either side.
    const int margin = 96;
    auto columns = area.reduced (std::max (0, area.getWidth() / 12), 0);
    if (count == 2)
    {
        blendRow = columns.removeFromTop (columns.getHeight() / 2);
        reimaginedRow = columns;
        blend.setBounds (blendRow.reduced (margin, 0).withSizeKeepingCentre (blendRow.getWidth() - 2 * margin, 22));
    }
    else
    {
        blendRow = {};
        reimaginedRow = columns;
    }
    reimagined.setBounds (reimaginedRow.reduced (margin, 0).withSizeKeepingCentre (reimaginedRow.getWidth() - 2 * margin, 20));
    if (count == 3)
        triangle.setBounds (captionArea.removeFromLeft (64));
}

void MixSection::paint (juce::Graphics& g)
{
    using namespace palette;
    const auto r = getLocalBounds().toFloat().reduced (1.0f);
    juce::Path shape;
    shape.addRoundedRectangle (r, 10.0f);
    juce::DropShadow (juce::Colour (0x141e1c18), 3, { 0, 1 }).drawForPath (g, shape);
    g.setColour (raised);
    g.fillPath (shape);
    g.setColour (hairline);
    g.strokePath (shape, juce::PathStrokeType (1.0f));

    // ORIGINAL ... REIMAGINED around the global transformation (always visible: it is global).
    g.setFont (fonts::label (10.5f));
    g.setColour (textDim);
    const auto row = reimaginedRow.toFloat();
    g.drawText ("ORIGINAL", row.withWidth (90.0f), juce::Justification::centredRight, false);
    g.drawText ("REIMAGINED", row.withTrimmedLeft (row.getWidth() - 90.0f), juce::Justification::centredLeft, false);

    if (count == 2)
    {
        const auto b = blendRow.toFloat();
        g.setFont (fonts::make (13.0f, fonts::Weight::semibold));
        g.setColour (text);
        g.drawText (OspAudioProcessor::layerName (slots[0]), b.withWidth (84.0f), juce::Justification::centredRight, false);
        g.drawText (OspAudioProcessor::layerName (slots[1]), b.withTrimmedLeft (b.getWidth() - 84.0f), juce::Justification::centredLeft, false);
        g.setFont (fonts::make (14.0f, fonts::Weight::semibold, 0.06f));
        g.drawText (OspAudioProcessor::layerName (slots[0]) + " / " + OspAudioProcessor::layerName (slots[1]) + " BLEND",
                    captionArea.toFloat(), juce::Justification::centredLeft, false);
    }
    if (count == 3)
    {
        // How much of each is heard (power shares, adding up to 100).
        const auto share = InstrumentEngine::triangleShares (processor.parameterValue ("mix.x"), processor.parameterValue ("mix.y"));
        auto text = captionArea.toFloat().withTrimmedLeft (4.0f);
        g.setFont (fonts::make (14.0f, fonts::Weight::semibold, 0.06f));
        g.setColour (palette::text);
        g.drawText ("MIX", text.removeFromTop (text.getHeight() * 0.5f), juce::Justification::bottomLeft, false);
        g.setFont (fonts::make (11.0f, fonts::Weight::medium));
        auto line = text;
        for (std::size_t i = 0; i < 3; ++i)
        {
            const auto part = OspAudioProcessor::layerName (static_cast<int> (i)) + " " + juce::String (juce::roundToInt (100.0 * share[i]));
            const float w = juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), part) + 8.0f;
            g.setColour (layer (static_cast<int> (i)).darker (0.25f));
            g.drawText (part, line.removeFromLeft (w), juce::Justification::topLeft, false);
        }
    }
}

//==============================================================================
PresetBar::PresetBar (OspAudioProcessor& p) : processor (p)
{
    setTitle ("Preset");
    refresh();
}

void PresetBar::refresh()
{
    const auto n = processor.presetDisplayName();
    const bool f = processor.isFavourite();
    if (n != name || f != favourite)
    {
        name = n;
        favourite = f;
        repaint();
    }
}

juce::Rectangle<float> PresetBar::partBounds (Part part) const
{
    auto r = getLocalBounds().toFloat().reduced (1.0f);
    const float arrow = r.getHeight() + 4.0f;
    switch (part)
    {
        case Part::previous: return r.removeFromLeft (arrow);
        case Part::next: return r.removeFromRight (arrow);
        case Part::favourite: return r.withTrimmedRight (arrow).removeFromRight (r.getHeight());
        case Part::name: return r.withTrimmedLeft (arrow).withTrimmedRight (arrow + r.getHeight());
        case Part::none: break;
    }
    return {};
}

PresetBar::Part PresetBar::partAt (juce::Point<int> p) const
{
    for (auto part : { Part::previous, Part::next, Part::favourite, Part::name })
        if (partBounds (part).contains (p.toFloat()))
            return part;
    return Part::none;
}

void PresetBar::mouseUp (const juce::MouseEvent& e)
{
    switch (partAt (e.getPosition()))
    {
        case Part::previous: processor.stepPresetList (-1); break;
        case Part::next: processor.stepPresetList (1); break;
        case Part::favourite: processor.toggleFavourite(); break;
        case Part::name:
        {
            juce::PopupMenu menu;
            const auto list = processor.presetList();
            const auto current = processor.presetDisplayName();
            juce::Component::SafePointer<PresetBar> safe (this);
            menu.addSectionHeader ("Starting states");
            bool presetsHeader = false;
            for (const auto& entry : list)
            {
                if (entry.program < 0 && ! presetsHeader)
                {
                    menu.addSectionHeader ("Presets");
                    presetsHeader = true;
                }
                menu.addItem (entry.name, true, entry.name == current, [safe, entry] {
                    if (safe == nullptr)
                        return;
                    safe->processor.openPresetEntry (entry);
                    safe->refresh();
                    if (safe->onChange != nullptr)
                        safe->onChange();
                });
            }
            menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withDeletionCheck (*this));
            return;
        }
        case Part::none: return;
    }
    refresh();
    if (onChange != nullptr)
        onChange();
}

void PresetBar::paint (juce::Graphics& g)
{
    using namespace palette;
    const auto r = getLocalBounds().toFloat().reduced (1.0f);
    OspLookAndFeel::drawCard (g, r, 7.0f, false, false);
    const auto hover = isMouseOver() ? partAt (getMouseXYRelative()) : Part::none;
    for (auto part : { Part::previous, Part::next })
    {
        const auto b = partBounds (part);
        if (hover == part)
        {
            g.setColour (recessed);
            g.fillRoundedRectangle (b.reduced (2.0f), 5.0f);
        }
        icons::draw (g, part == Part::previous ? icons::Kind::chevronLeft : icons::Kind::chevronRight, b.reduced (b.getHeight() * 0.22f), text, 1.6f);
        g.setColour (hairline);
        g.drawVerticalLine (juce::roundToInt (part == Part::previous ? b.getRight() : b.getX()), r.getY() + 4.0f, r.getBottom() - 4.0f);
    }
    const auto heart = partBounds (Part::favourite);
    icons::draw (g, favourite ? icons::Kind::heartFilled : icons::Kind::heart, heart.reduced (heart.getHeight() * 0.25f),
                 favourite || hover == Part::favourite ? accent : textDim, 1.4f);
    g.setColour (hover == Part::name ? juce::Colours::black : text);
    g.setFont (fonts::make (14.5f, fonts::Weight::medium));
    g.drawText (name, partBounds (Part::name).reduced (10.0f, 0.0f), juce::Justification::centred, true);
}

//==============================================================================
namespace
{
    constexpr float attackMax = 2000.0f, decayMax = 20000.0f, releaseMax = 15000.0f;
    /** A compressed time axis: short times get room, long ones still fit. */
    float timeShare (float ms, float maximum) { return std::log1p (std::max (0.0f, ms) / 20.0f) / std::log1p (maximum / 20.0f); }
    float timeFromShare (float share, float maximum) { return 20.0f * std::expm1 (std::clamp (share, 0.0f, 1.0f) * std::log1p (maximum / 20.0f)); }
}

EnvelopePanel::EnvelopePanel (juce::AudioProcessorValueTreeState& s) : state (s)
{
    auto ms = [] (double v) { return format::milliseconds (v); };
    const std::array<std::pair<const char*, const char*>, 4> ids { { { "attack", "A" }, { "decay", "D" }, { "sustainLevel", "S" }, { "release", "R" } } };
    for (std::size_t i = 0; i < knobs.size(); ++i)
    {
        MiniKnob::Formatter f = i == 2 ? MiniKnob::Formatter ([] (double v) { return juce::String (juce::roundToInt (v)) + " %"; }) : MiniKnob::Formatter (ms);
        knobs[i] = std::make_unique<MiniKnob> (state, ids[i].first, ids[i].second, std::move (f));
        addAndMakeVisible (*knobs[i]);
    }
    setTitle ("Amplitude envelope");
}

EnvelopePanel::Shape EnvelopePanel::shape() const
{
    auto v = [this] (const char* id) {
        auto* p = state.getParameter (id);
        return p != nullptr ? p->convertFrom0to1 (p->getValue()) : 0.0f;
    };
    return { v ("attack"), v ("decay"), 0.01f * v ("sustainLevel"), v ("release") };
}

std::array<juce::Point<float>, 3> EnvelopePanel::handles() const
{
    const auto s = shape();
    const auto r = graph.toFloat().reduced (8.0f, 8.0f);
    const float w = r.getWidth();
    const float peakX = r.getX() + 0.27f * w * timeShare (s.attack, attackMax);
    const float decayX = peakX + 0.27f * w * timeShare (s.decay, decayMax);
    const float sustainY = r.getBottom() - s.sustain * r.getHeight();
    const float holdEnd = decayX + 0.16f * w;
    const float releaseX = holdEnd + 0.28f * w * timeShare (s.release, releaseMax);
    return { juce::Point<float> (peakX, r.getY()), juce::Point<float> (decayX, sustainY), juce::Point<float> (releaseX, r.getBottom()) };
}

void EnvelopePanel::resized()
{
    auto area = getLocalBounds();
    area.removeFromTop (20);
    graph = area.removeFromTop (std::max (36, area.getHeight() - 62));
    area.removeFromTop (4);
    const int w = area.getWidth() / 4;
    for (auto& k : knobs)
        k->setBounds (area.removeFromLeft (w).withSizeKeepingCentre (std::min (w, 64), area.getHeight()));
}

void EnvelopePanel::paint (juce::Graphics& g)
{
    using namespace palette;
    g.setColour (text);
    g.setFont (fonts::label (11.5f));
    g.drawText ("AMP ENVELOPE", getLocalBounds().removeFromTop (16), juce::Justification::centredLeft, false);

    const auto well = graph.toFloat();
    g.setColour (graphite);
    g.fillRoundedRectangle (well, 7.0f);
    const auto r = well.reduced (8.0f, 8.0f);
    const auto h = handles();
    const float holdEnd = h[1].x + 0.16f * r.getWidth();
    juce::Path curve;
    curve.startNewSubPath (r.getX(), r.getBottom());
    curve.lineTo (h[0]);
    // Decay and release fall exponentially: drawn as curves.
    curve.quadraticTo (h[0].x + 0.15f * (h[1].x - h[0].x), h[1].y, h[1].x, h[1].y);
    curve.lineTo (holdEnd, h[1].y);
    curve.quadraticTo (holdEnd + 0.15f * (h[2].x - holdEnd), h[2].y, h[2].x, h[2].y);
    juce::Path fill (curve);
    fill.lineTo (r.getX(), r.getBottom());
    fill.closeSubPath();
    g.setGradientFill (juce::ColourGradient (accent.withAlpha (0.35f), 0.0f, r.getY(), accent.withAlpha (0.02f), 0.0f, r.getBottom(), false));
    g.fillPath (fill);
    g.setColour (accent);
    g.strokePath (curve, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    for (int i = 0; i < 3; ++i)
    {
        const auto p = h[static_cast<std::size_t> (i)];
        g.setColour (i == dragHandle ? accent : raised);
        g.fillEllipse (p.x - 4.0f, p.y - 4.0f, 8.0f, 8.0f);
        g.setColour (graphite);
        g.drawEllipse (p.x - 4.0f, p.y - 4.0f, 8.0f, 8.0f, 1.0f);
    }
}

void EnvelopePanel::mouseDown (const juce::MouseEvent& e)
{
    dragHandle = -1;
    const auto h = handles();
    float best = 14.0f;
    for (int i = 0; i < 3; ++i)
    {
        const float d = h[static_cast<std::size_t> (i)].getDistanceFrom (e.position);
        if (d < best)
        {
            best = d;
            dragHandle = i;
        }
    }
    const std::array<std::vector<const char*>, 3> ids { { { "attack" }, { "decay", "sustainLevel" }, { "release" } } };
    if (dragHandle >= 0)
        for (const auto* id : ids[static_cast<std::size_t> (dragHandle)])
            if (auto* p = state.getParameter (id))
                p->beginChangeGesture();
    repaint();
}

void EnvelopePanel::mouseDrag (const juce::MouseEvent& e)
{
    if (dragHandle < 0)
        return;
    const auto r = graph.toFloat().reduced (8.0f, 8.0f);
    const auto h = handles();
    const float w = std::max (1.0f, r.getWidth());
    auto set = [this] (const char* id, float value) {
        if (auto* p = state.getParameter (id))
            p->setValueNotifyingHost (p->convertTo0to1 (p->getNormalisableRange().snapToLegalValue (value)));
    };
    if (dragHandle == 0)
        set ("attack", timeFromShare ((e.position.x - r.getX()) / (0.27f * w), attackMax));
    else if (dragHandle == 1)
    {
        set ("decay", timeFromShare ((e.position.x - h[0].x) / (0.27f * w), decayMax));
        set ("sustainLevel", 100.0f * std::clamp ((r.getBottom() - e.position.y) / r.getHeight(), 0.0f, 1.0f));
    }
    else
        set ("release", timeFromShare ((e.position.x - (h[1].x + 0.16f * w)) / (0.28f * w), releaseMax));
    repaint();
}

void EnvelopePanel::mouseUp (const juce::MouseEvent&)
{
    const std::array<std::vector<const char*>, 3> ids { { { "attack" }, { "decay", "sustainLevel" }, { "release" } } };
    if (dragHandle >= 0)
        for (const auto* id : ids[static_cast<std::size_t> (dragHandle)])
            if (auto* p = state.getParameter (id))
                p->endChangeGesture();
    dragHandle = -1;
    repaint();
}

//==============================================================================
Wheel::Wheel (juce::String c, bool spring, std::function<void (float)> move) : caption (std::move (c)), springBack (spring), onMove (std::move (move))
{
    setTitle (caption);
    setTooltip (springBack ? "Pitch bend (springs back)" : "Modulation: opens MOVEMENT up");
    setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
}

juce::Rectangle<float> Wheel::slot() const
{
    return getLocalBounds().toFloat().withTrimmedBottom (16.0f).reduced (2.0f, 2.0f);
}

void Wheel::mouseDown (const juce::MouseEvent&)
{
    dragStart = value;
}

void Wheel::mouseDrag (const juce::MouseEvent& e)
{
    const float travel = slot().getHeight() * (springBack ? 0.5f : 1.0f);
    value = std::clamp (dragStart - static_cast<float> (e.getDistanceFromDragStartY()) / std::max (1.0f, travel), springBack ? -1.0f : 0.0f, 1.0f);
    if (onMove != nullptr)
        onMove (value);
    repaint();
}

void Wheel::mouseUp (const juce::MouseEvent&)
{
    if (springBack)
    {
        value = 0.0f;
        if (onMove != nullptr)
            onMove (value);
        repaint();
    }
}

void Wheel::paint (juce::Graphics& g)
{
    using namespace palette;
    const auto s = slot();
    g.setColour (graphite);
    g.fillRoundedRectangle (s, 5.0f);
    // How far it is turned: a soft coloured column from the rest position.
    const float rest = springBack ? s.getCentreY() : s.getBottom() - 4.0f;
    const float range = springBack ? 0.5f * (s.getHeight() - 8.0f) : s.getHeight() - 8.0f;
    const float pos = rest - value * range;
    g.setColour ((springBack ? mineral : accent).withAlpha (0.55f));
    g.fillRoundedRectangle (juce::Rectangle<float> (s.getX() + 3.0f, std::min (rest, pos), s.getWidth() - 6.0f, std::abs (rest - pos)), 2.0f);
    g.setColour (raised);
    g.fillRoundedRectangle (juce::Rectangle<float> (s.getX() + 3.0f, pos - 2.5f, s.getWidth() - 6.0f, 5.0f), 2.0f);
    g.setColour (textDim);
    g.setFont (fonts::label (9.5f));
    g.drawText (caption, getLocalBounds().removeFromBottom (14), juce::Justification::centred, false);
}

} // namespace osp::plugin
