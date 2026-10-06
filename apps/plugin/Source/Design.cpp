#include "Design.h"

namespace osp::plugin::design::draw
{

void housing (juce::Graphics& g, juce::Rectangle<float> bounds)
{
    using namespace colour;
    // The surround: a warm surface, a touch darker at the top left.
    g.setGradientFill (juce::ColourGradient (backdropTop, bounds.getX(), bounds.getY(), backdropBottom, bounds.getRight(), bounds.getBottom(), false));
    g.fillRect (bounds);

    const auto r = layout::housing;
    juce::Path body;
    body.addRoundedRectangle (r, layout::housingRadius);
    // One broad, quiet shadow (a physical object resting on the surface), then a close one.
    juce::DropShadow (juce::Colour (0x2a3a2a1c), 26, { 0, 10 }).drawForPath (g, body);
    juce::DropShadow (juce::Colour (0x22302418), 4, { 0, 2 }).drawForPath (g, body);
    // Powder-coated body: barely brighter at the top, warmer at the foot.
    g.setGradientFill (juce::ColourGradient (housingTop, 0.0f, r.getY(), housingBottom, 0.0f, r.getBottom(), false));
    g.fillPath (body);
    // A faint lit rim along the top and left, a darker one at the foot.
    {
        juce::Graphics::ScopedSaveState state (g);
        g.reduceClipRegion (body);
        g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.55f), 0.0f, r.getY(), juce::Colours::white.withAlpha (0.0f), 0.0f, r.getY() + 6.0f, false));
        g.fillRect (r.withHeight (6.0f));
        g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.25f), r.getX(), 0.0f, juce::Colours::white.withAlpha (0.0f), r.getX() + 5.0f, 0.0f, false));
        g.fillRect (r.withWidth (5.0f));
    }
    g.setColour (housingRim.withAlpha (0.55f));
    g.strokePath (body, juce::PathStrokeType (1.0f));
}

void raised (juce::Graphics& g, juce::Rectangle<float> r, float radius, juce::Colour top, juce::Colour bottom, float shadow)
{
    using namespace colour;
    juce::Path shape;
    shape.addRoundedRectangle (r, radius);
    if (shadow > 0.0f)
    {
        juce::DropShadow (juce::Colour (0x1e3a2a1c).withMultipliedAlpha (shadow), juce::roundToInt (9.0f * shadow), { 0, juce::roundToInt (3.0f * shadow) }).drawForPath (g, shape);
        juce::DropShadow (juce::Colour (0x1c302418).withMultipliedAlpha (shadow), 2, { 0, 1 }).drawForPath (g, shape);
    }
    g.setGradientFill (juce::ColourGradient (top, 0.0f, r.getY(), bottom, 0.0f, r.getBottom(), false));
    g.fillPath (shape);
    // Edges: lit along the top, shaded along the bottom (the hairline sits in between).
    juce::Graphics::ScopedSaveState state (g);
    g.reduceClipRegion (shape);
    g.setColour (edgeLight.withAlpha (0.9f));
    juce::Path topEdge;
    topEdge.addRoundedRectangle (r.translated (0.0f, 1.0f), radius);
    g.strokePath (topEdge, juce::PathStrokeType (1.2f));
    g.setColour (edgeShade.withAlpha (0.35f));
    juce::Path bottomEdge;
    bottomEdge.addRoundedRectangle (r.translated (0.0f, -1.0f), radius);
    g.strokePath (bottomEdge, juce::PathStrokeType (1.0f));
    g.setColour (hairline.withAlpha (0.8f));
    g.strokePath (shape, juce::PathStrokeType (0.8f));
}

void well (juce::Graphics& g, juce::Rectangle<float> r, float radius, juce::Colour fill)
{
    juce::Path shape;
    shape.addRoundedRectangle (r, radius);
    // The rim catches light from above on the card around it.
    juce::Path lip;
    lip.addRoundedRectangle (r.expanded (1.0f), radius + 1.0f);
    g.setColour (juce::Colours::white.withAlpha (0.55f));
    g.strokePath (lip.createPathWithRoundedCorners (0.0f), juce::PathStrokeType (1.0f), juce::AffineTransform::translation (0.0f, 1.0f));
    g.setColour (fill);
    g.fillPath (shape);
    juce::Graphics::ScopedSaveState state (g);
    g.reduceClipRegion (shape);
    // Recessed: an inner shadow under the top edge, a faint reflection near the bottom.
    g.setGradientFill (juce::ColourGradient (juce::Colours::black.withAlpha (0.45f), 0.0f, r.getY(), juce::Colours::black.withAlpha (0.0f), 0.0f, r.getY() + 10.0f, false));
    g.fillRect (r.withHeight (10.0f));
    g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.0f), 0.0f, r.getBottom() - 16.0f, juce::Colours::white.withAlpha (0.035f), 0.0f, r.getBottom(), false));
    g.fillRect (r.withTop (r.getBottom() - 16.0f));
    g.setColour (colour::wellRim.withAlpha (0.9f));
    g.strokePath (shape, juce::PathStrokeType (1.2f));
}

void button (juce::Graphics& g, juce::Rectangle<float> r, float radius, bool active, bool hover, juce::Colour activeColour)
{
    using namespace colour;
    juce::Path shape;
    shape.addRoundedRectangle (r, radius);
    juce::DropShadow (juce::Colour (0x24302418), 3, { 0, 1 }).drawForPath (g, shape);
    if (active)
    {
        // On: a warm, barely tinted face with an accent rim (colour marks the state, it does
        // not fill the key).
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xfffbefe5), 0.0f, r.getY(), juce::Colour (0xfff3ddcd), 0.0f, r.getBottom(), false));
        g.fillPath (shape);
        g.setColour (activeColour.withAlpha (0.18f));
        g.strokePath (shape, juce::PathStrokeType (3.0f));
        g.setColour (activeColour.withAlpha (0.95f));
        g.strokePath (shape, juce::PathStrokeType (1.5f));
        return;
    }
    g.setGradientFill (juce::ColourGradient (hover ? buttonTop.brighter (0.2f) : buttonTop, 0.0f, r.getY(), buttonBottom, 0.0f, r.getBottom(), false));
    g.fillPath (shape);
    juce::Graphics::ScopedSaveState state (g);
    g.reduceClipRegion (shape);
    juce::Path lit;
    lit.addRoundedRectangle (r.translated (0.0f, 1.0f), radius);
    g.setColour (juce::Colours::white.withAlpha (0.85f));
    g.strokePath (lit, juce::PathStrokeType (1.0f));
    g.setColour (juce::Colour (0xffcbc2b4));
    g.strokePath (shape, juce::PathStrokeType (1.0f));
}

void knob (juce::Graphics& g, juce::Point<float> c, float r, float position, const KnobStyle& style)
{
    using namespace colour;
    position = juce::jlimit (0.0f, 1.0f, position);
    auto at = [c] (float radius, float angle) { return c + juce::Point<float> (std::sin (angle), -std::cos (angle)) * radius; };
    const float angle = style.startAngle + position * (style.endAngle - style.startAngle);

    // Ticks: small dark dots around the travel.
    if (style.ticks)
        for (int i = 0; i < style.tickCount; ++i)
        {
            const float a = style.startAngle + (style.endAngle - style.startAngle) * static_cast<float> (i) / static_cast<float> (style.tickCount - 1);
            const auto p = at (style.tickRadius * r, a);
            const float d = std::max (1.6f, 0.07f * r);
            g.setColour (tick.withAlpha (style.enabled ? 0.85f : 0.35f));
            g.fillEllipse (p.x - 0.5f * d, p.y - 0.5f * d, d, d);
        }

    // Track and value arc, hugging the rim.
    const float arcRadius = style.arcRadius * r, arcWidth = std::max (2.4f, 0.12f * r);
    {
        // The track: a soft grey ring, shaded on its inner edge.
        juce::Path track;
        track.addCentredArc (c.x, c.y, arcRadius, arcRadius, 0.0f, style.startAngle, style.endAngle, true);
        g.setColour (juce::Colour (0xffc3b9aa));
        g.strokePath (track, juce::PathStrokeType (arcWidth, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (juce::Colours::white.withAlpha (0.35f));
        juce::Path lit;
        lit.addCentredArc (c.x, c.y, arcRadius + 0.3f * arcWidth, arcRadius + 0.3f * arcWidth, 0.0f, style.startAngle, style.endAngle, true);
        g.strokePath (lit, juce::PathStrokeType (0.35f * arcWidth));
        const float from = style.bipolar ? 0.5f * (style.startAngle + style.endAngle) : style.startAngle;
        if (std::abs (angle - from) > 0.01f && style.enabled)
        {
            // The value arc deepens towards where it starts and is brightest at the value.
            juce::Path value;
            value.addCentredArc (c.x, c.y, arcRadius, arcRadius, 0.0f, std::min (from, angle), std::max (from, angle), true);
            const auto p0 = at (arcRadius, from), p1 = at (arcRadius, angle);
            g.setGradientFill (juce::ColourGradient (style.arc.darker (0.55f), p0.x, p0.y, style.arc.brighter (0.12f), p1.x, p1.y, false));
            g.strokePath (value, juce::PathStrokeType (arcWidth, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
    }

    // Body: contact shadow, bevelled rim (lit top-left, shaded bottom-right), lit cap.
    const auto body = juce::Rectangle<float> (2.0f * r, 2.0f * r).withCentre (c);
    {
        juce::Path disc;
        disc.addEllipse (body);
        juce::DropShadow (juce::Colour (0x46302418), juce::roundToInt (0.22f * r) + 2, { 0, juce::roundToInt (0.09f * r) + 1 }).drawForPath (g, disc);
    }
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xfffdfaf4), c.x - 0.7f * r, c.y - 0.8f * r, knobRim, c.x + 0.6f * r, c.y + 0.9f * r, false));
    g.fillEllipse (body);
    const float capR = 0.88f * r;
    const auto cap = juce::Rectangle<float> (2.0f * capR, 2.0f * capR).withCentre (c);
    // The groove between rim and cap.
    g.setColour (juce::Colour (0x40665a48));
    g.drawEllipse (cap.expanded (0.6f), std::max (0.8f, 0.03f * r));
    juce::ColourGradient capFill (knobCapTop, c.x - 0.35f * r, c.y - 0.55f * r, knobCapBottom, c.x + 0.5f * r, c.y + 0.85f * r, true);
    g.setGradientFill (capFill);
    g.fillEllipse (cap);
    // A soft highlight across the top of the cap.
    g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.55f), c.x, cap.getY(), juce::Colours::white.withAlpha (0.0f), c.x, c.y, false));
    g.fillEllipse (cap.reduced (0.08f * r).withTrimmedBottom (0.7f * r));

    // Pointer: a dark groove from near the centre to the cap's edge.
    const auto p0 = at (style.pointerFrom * r, angle), p1 = at (style.pointerTo * r, angle);
    g.setColour (style.pointer.withAlpha (style.enabled ? 1.0f : 0.4f));
    g.drawLine ({ p0, p1 }, std::max (2.2f, 0.11f * r));
}

void led (juce::Graphics& g, juce::Point<float> centre, float diameter, juce::Colour c, float glow)
{
    const float r = 0.5f * diameter;
    if (glow > 0.0f)
    {
        juce::ColourGradient halo (c.withAlpha (0.35f * glow), centre.x, centre.y, c.withAlpha (0.0f), centre.x + 2.2f * r, centre.y, true);
        g.setGradientFill (halo);
        g.fillEllipse (juce::Rectangle<float> (4.4f * r, 4.4f * r).withCentre (centre));
    }
    // A small glossy bead: dark rim, bright centre, a highlight up and to the left.
    juce::ColourGradient body (c.brighter (0.5f), centre.x - 0.3f * r, centre.y - 0.35f * r, c.darker (0.55f), centre.x + 0.8f * r, centre.y + 0.9f * r, true);
    g.setGradientFill (body);
    g.fillEllipse (juce::Rectangle<float> (diameter, diameter).withCentre (centre));
    g.setColour (juce::Colours::white.withAlpha (0.55f));
    g.fillEllipse (juce::Rectangle<float> (0.42f * diameter, 0.32f * diameter).withCentre (centre + juce::Point<float> (-0.18f * diameter, -0.22f * diameter)));
}

} // namespace osp::plugin::design::draw
