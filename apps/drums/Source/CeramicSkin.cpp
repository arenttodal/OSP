#include "CeramicSkin.h"

#include "OspLookAndFeel.h"

#include <cmath>
#include <map>

namespace osp::drumsplugin::ceramic
{

namespace
{
    // Raw teak (§3): base, light and dark grain; the displayed colours come out of the light.
    const juce::Colour teakBase { 0xffb97a45 };
    const juce::Colour grainLight { 0xffc4834a };
    const juce::Colour grainDark { 0xff7e4a24 };

    juce::Path rounded (juce::Rectangle<float> r, float radius)
    {
        juce::Path p;
        p.addRoundedRectangle (r, radius);
        return p;
    }

    juce::Font uiFont (float height, bool semibold = false)
    {
        return plugin::fonts::make (height, semibold ? plugin::fonts::Weight::semibold : plugin::fonts::Weight::medium, 0.03f);
    }
} // namespace

void contactShadow (juce::Graphics& g, juce::Rectangle<float> body, float radius, float height)
{
    // Two soft layers: a tight one where the slab meets the surface, a wide falloff below.
    const float h = juce::jlimit (0.3f, 2.5f, height);
    juce::DropShadow near (juce::Colours::black.withAlpha (0.16f * h), static_cast<int> (3.0f * h + 1.0f), { 0, static_cast<int> (std::round (1.0f * h)) });
    juce::DropShadow far (juce::Colour (0xff3a2a1c).withAlpha (0.10f * h), static_cast<int> (12.0f * h + 2.0f), { static_cast<int> (std::round (1.5f * h)), static_cast<int> (std::round (5.0f * h)) });
    const auto path = rounded (body, radius);
    far.drawForPath (g, path);
    near.drawForPath (g, path);
}

void teakCheek (juce::Graphics& g, juce::Rectangle<float> r)
{
    // Cached per size: straight grain along the length (deterministic, so it never shimmers).
    static std::map<std::pair<int, int>, juce::Image> cache;
    const int w = std::max (4, juce::roundToInt (r.getWidth()));
    const int h = std::max (4, juce::roundToInt (r.getHeight()));
    auto& img = cache[{ w, h }];
    if (! img.isValid())
    {
        img = juce::Image (juce::Image::ARGB, w, h, true);
        juce::Graphics t (img);
        const auto box = juce::Rectangle<float> (0.0f, 0.0f, static_cast<float> (w), static_cast<float> (h));
        juce::Path body = rounded (box, 3.0f);
        t.reduceClipRegion (body);
        // Base: lit from the top left, a little darker to the right.
        t.setGradientFill (juce::ColourGradient (colour::teakLight, 0.0f, 0.0f, colour::teakDark, static_cast<float> (w), 0.0f, false));
        t.fillAll();
        juce::Random random (0x7eac);
        for (int line = 0; line < w * 3; ++line)
        {
            const float x = random.nextFloat() * static_cast<float> (w);
            const float width = 0.4f + random.nextFloat() * 1.2f;
            const bool dark = random.nextFloat() < 0.55f;
            const float alpha = dark ? 0.05f + 0.16f * random.nextFloat() : 0.06f + 0.12f * random.nextFloat();
            t.setColour ((dark ? grainDark : grainLight).withAlpha (alpha));
            // Gentle waviness along the length (oiled, quarter-sawn).
            juce::Path streak;
            const float phase = random.nextFloat() * 6.28f, wave = 0.3f + random.nextFloat() * 0.9f;
            streak.startNewSubPath (x, 0.0f);
            for (float y = 0.0f; y <= static_cast<float> (h); y += 24.0f)
                streak.lineTo (x + wave * std::sin (phase + y * 0.011f), y);
            t.strokePath (streak, juce::PathStrokeType (width));
        }
        t.setColour (teakBase.withAlpha (0.08f));
        t.fillAll();
        // Rounded, oiled edges (1.1 mm bevel): light on the left, shade on the right; a satin
        // clear-coat sheen down the middle.
        t.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.20f), 0.0f, 0.0f, juce::Colours::transparentWhite, 4.0f, 0.0f, false));
        t.fillRect (box.withWidth (4.0f));
        t.setGradientFill (juce::ColourGradient (juce::Colours::transparentBlack, static_cast<float> (w) - 4.0f, 0.0f, juce::Colours::black.withAlpha (0.22f), static_cast<float> (w), 0.0f, false));
        t.fillRect (box.withLeft (static_cast<float> (w) - 4.0f));
        t.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.0f), static_cast<float> (w) * 0.25f, 0.0f, juce::Colours::white.withAlpha (0.07f), static_cast<float> (w) * 0.45f, 0.0f, false));
        t.fillRect (box.withTrimmedLeft (static_cast<float> (w) * 0.25f).withWidth (static_cast<float> (w) * 0.3f));
        t.setColour (juce::Colours::black.withAlpha (0.25f));
        t.strokePath (body, juce::PathStrokeType (0.8f));
    }
    contactShadow (g, r, 3.0f, 1.2f);
    g.setOpacity (1.0f);   // images composite at the current colour's alpha (the shadow's)
    g.drawImage (img, r);
}

void housing (juce::Graphics& g, juce::Rectangle<float> r)
{
    contactShadow (g, r, 7.0f, 1.6f);
    g.setGradientFill (juce::ColourGradient (colour::housingTop, r.getX(), r.getY(), colour::housingBottom, r.getX(), r.getBottom(), false));
    g.fillRoundedRectangle (r, 7.0f);
    // The rounded 2 mm edge: light along the top-left, shade along the bottom-right.
    g.setColour (juce::Colours::white.withAlpha (0.55f));
    g.drawRoundedRectangle (r.reduced (1.0f).translated (0.0f, 0.5f), 6.0f, 1.0f);
    g.setColour (juce::Colour (0xff8c7f70).withAlpha (0.35f));
    g.drawRoundedRectangle (r.reduced (0.5f), 7.0f, 1.0f);
}

void card (juce::Graphics& g, juce::Rectangle<float> r, float radius, float height)
{
    contactShadow (g, r, radius, height);
    juce::ColourGradient face (colour::cardTop, r.getX(), r.getY(), colour::cardBottom, r.getX(), r.getBottom(), false);
    face.addColour (0.45, colour::cardMiddle);
    g.setGradientFill (face);
    g.fillRoundedRectangle (r, radius);
    // Soft 0.55 mm edge: a fine highlight inside the top edge, a faint shade at the bottom.
    g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.9f), r.getX(), r.getY(), juce::Colours::white.withAlpha (0.0f), r.getX(), r.getY() + 6.0f, false));
    g.drawRoundedRectangle (r.reduced (0.75f), radius, 1.2f);
    g.setColour (juce::Colour (0xffb9ad9b).withAlpha (0.45f));
    g.drawRoundedRectangle (r.reduced (0.25f), radius, 0.8f);
    // Faint satin sheen across the upper third.
    g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.25f), r.getX(), r.getY(), juce::Colours::white.withAlpha (0.0f), r.getX(), r.getY() + r.getHeight() * 0.35f, false));
    g.fillRoundedRectangle (r.reduced (1.5f), radius - 1.0f);
}

void panel (juce::Graphics& g, juce::Rectangle<float> r, float radius)
{
    contactShadow (g, r, radius, 0.7f);
    g.setGradientFill (juce::ColourGradient (colour::panelTop, r.getX(), r.getY(), colour::panelBottom, r.getX(), r.getBottom(), false));
    g.fillRoundedRectangle (r, radius);
    g.setColour (juce::Colours::white.withAlpha (0.7f));
    g.drawRoundedRectangle (r.reduced (0.75f).translated (0.0f, 0.4f), radius, 1.0f);
    g.setColour (juce::Colour (0xffb9ad9b).withAlpha (0.35f));
    g.drawRoundedRectangle (r.reduced (0.25f), radius, 0.8f);
}

void recess (juce::Graphics& g, juce::Rectangle<float> r, float radius, juce::Colour floor)
{
    // The lip (0.4 mm): light on the lower edge, the floor 1.2 mm down shaded from the top.
    g.setColour (juce::Colours::white.withAlpha (0.6f));
    g.drawRoundedRectangle (r.expanded (0.5f).translated (0.0f, 0.8f), radius + 0.5f, 1.0f);
    g.setColour (floor);
    g.fillRoundedRectangle (r, radius);
    g.saveState();
    g.reduceClipRegion (rounded (r, radius));
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff6a5a48).withAlpha (0.22f), r.getX(), r.getY(), juce::Colours::transparentBlack, r.getX(), r.getY() + 7.0f, false));
    g.fillRect (r.withHeight (7.0f));
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff6a5a48).withAlpha (0.10f), r.getX(), r.getY(), juce::Colours::transparentBlack, r.getX() + 5.0f, r.getY(), false));
    g.fillRect (r.withWidth (5.0f));
    g.restoreState();
    g.setColour (juce::Colour (0xff9c8f7f).withAlpha (0.45f));
    g.drawRoundedRectangle (r.reduced (0.25f), radius, 0.8f);
}

juce::Rectangle<float> display (juce::Graphics& g, juce::Rectangle<float> r, float radius, juce::Colour well)
{
    // 1.6 px ceramic lip, 0.6 mm deep, around the unchanged dark well.
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xffd9d0c2), r.getX(), r.getY(), juce::Colour (0xfffbf6ee), r.getX(), r.getBottom(), false));
    g.fillRoundedRectangle (r, radius + 1.6f);
    const auto inner = r.reduced (1.6f);
    g.setGradientFill (juce::ColourGradient (well.brighter (0.06f), inner.getX(), inner.getY(), well.darker (0.15f), inner.getX(), inner.getBottom(), false));
    g.fillRoundedRectangle (inner, radius);
    g.setColour (colour::wellRim);
    g.drawRoundedRectangle (inner.reduced (0.25f), radius, 0.8f);
    // Inner shade under the top lip.
    g.saveState();
    g.reduceClipRegion (rounded (inner, radius));
    g.setGradientFill (juce::ColourGradient (juce::Colours::black.withAlpha (0.35f), inner.getX(), inner.getY(), juce::Colours::transparentBlack, inner.getX(), inner.getY() + 6.0f, false));
    g.fillRect (inner.withHeight (6.0f));
    g.restoreState();
    return inner;
}

void lens (juce::Graphics& g, juce::Point<float> c, float d, juce::Colour col, bool lit)
{
    const auto r = juce::Rectangle<float> (d, d).withCentre (c);
    // Seat (a recessed slot) then the dome.
    g.setColour (juce::Colours::black.withAlpha (0.18f));
    g.fillEllipse (r.expanded (1.2f).translated (0.0f, 0.4f));
    if (lit)
    {
        // Emission 1.4: a halo around the lens.
        g.setGradientFill (juce::ColourGradient (col.withAlpha (0.45f), c.x, c.y, col.withAlpha (0.0f), c.x + d * 1.4f, c.y, true));
        g.fillEllipse (r.expanded (d * 0.9f));
    }
    const auto face = lit ? col.withMultipliedSaturation (1.25f).brighter (0.35f) : col.withMultipliedSaturation (0.55f).interpolatedWith (colour::ledOff, 0.62f);
    g.setGradientFill (juce::ColourGradient (face.brighter (lit ? 0.5f : 0.25f), c.x - d * 0.2f, c.y - d * 0.25f, face.darker (lit ? 0.1f : 0.35f), c.x + d * 0.3f, c.y + d * 0.45f, true));
    g.fillEllipse (r);
    g.setColour (juce::Colours::white.withAlpha (lit ? 0.85f : 0.45f));
    g.fillEllipse (juce::Rectangle<float> (d * 0.32f, d * 0.22f).withCentre ({ c.x - d * 0.12f, c.y - d * 0.22f }));
    g.setColour (juce::Colours::black.withAlpha (0.25f));
    g.drawEllipse (r, 0.6f);
}

void badge (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& text, juce::Colour face, juce::Colour ink, float fontHeight)
{
    contactShadow (g, r, 3.0f, 0.5f);
    g.setGradientFill (juce::ColourGradient (face.brighter (0.12f), r.getX(), r.getY(), face.darker (0.08f), r.getX(), r.getBottom(), false));
    g.fillRoundedRectangle (r, 3.5f);
    g.setColour (juce::Colours::white.withAlpha (0.35f));
    g.drawRoundedRectangle (r.reduced (0.6f).translated (0.0f, 0.3f), 3.0f, 0.8f);
    g.setColour (ink);
    g.setFont (plugin::fonts::make (fontHeight, plugin::fonts::Weight::semibold));
    g.drawText (text, r, juce::Justification::centred);
}

void tabUnderline (juce::Graphics& g, float x, float y, float width)
{
    g.setColour (colour::accent);
    g.fillRoundedRectangle (juce::Rectangle<float> (x, y, width, 2.0f), 1.0f);
}

// ---------------------------------------------------------------------------------------

void KeyButton::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    auto r = getLocalBounds().toFloat().reduced (1.5f, 1.5f);
    r.removeFromBottom (1.5f);
    const bool on = getToggleState();
    const bool enabled = isEnabled();
    const bool sunk = down || on;
    // Height by role: the contact shadow and the travel shrink when the key is down.
    const float height = role * (sunk ? 0.42f : 1.0f);
    contactShadow (g, r, corner, height);
    const auto face = on ? colour::keyOn : highlighted && enabled ? colour::keyNormal.interpolatedWith (juce::Colours::white, 0.16f) : colour::keyNormal;
    const auto body = r.translated (0.0f, sunk ? 0.6f : 0.0f);
    g.setGradientFill (juce::ColourGradient (face.brighter (0.03f), body.getX(), body.getY(), face.darker (0.035f), body.getX(), body.getBottom(), false));
    g.fillRoundedRectangle (body, corner);
    g.setColour (juce::Colours::white.withAlpha (sunk ? 0.35f : 0.8f));
    g.drawRoundedRectangle (body.reduced (0.8f).translated (0.0f, 0.3f), corner - 0.5f, 0.9f);
    g.setColour (juce::Colour (0xffa99c8a).withAlpha (on ? 0.55f : 0.42f));
    g.drawRoundedRectangle (body.reduced (0.3f), corner, 0.8f);
    auto ink = on ? colour::accent : colour::text;
    if (! enabled)
        ink = ink.withAlpha (0.35f);
    if (icon)
        icon (g, body.reduced (body.getHeight() * 0.22f), ink);
    else
    {
        g.setColour (ink);
        g.setFont (uiFont (fontHeight));
        g.drawFittedText (getButtonText(), body.toNearestInt().reduced (4, 0), juce::Justification::centred, 1, 1.0f);
    }
}

void CheckKey::paintButton (juce::Graphics& g, bool highlighted, bool)
{
    auto bounds = getLocalBounds().toFloat();
    const float side = bounds.getHeight();
    auto box = bounds.removeFromLeft (side).reduced (side * 0.18f);
    const bool on = getToggleState();
    contactShadow (g, box, 3.0f, on ? 0.3f : 0.6f);
    g.setColour (on ? colour::keyOn : highlighted ? colour::keyNormal.brighter (0.1f) : colour::keyNormal);
    g.fillRoundedRectangle (box, 3.0f);
    g.setColour (juce::Colour (0xffa99c8a).withAlpha (0.6f));
    g.drawRoundedRectangle (box.reduced (0.3f), 3.0f, 0.8f);
    if (on)
    {
        juce::Path tick;
        tick.startNewSubPath (box.getX() + box.getWidth() * 0.24f, box.getCentreY());
        tick.lineTo (box.getX() + box.getWidth() * 0.43f, box.getBottom() - box.getHeight() * 0.27f);
        tick.lineTo (box.getRight() - box.getWidth() * 0.22f, box.getY() + box.getHeight() * 0.27f);
        g.setColour (colour::accent);
        g.strokePath (tick, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
    g.setColour (isEnabled() ? colour::textSecondary : colour::textMicro);
    g.setFont (uiFont (13.0f));
    g.drawText (getButtonText(), bounds.withTrimmedLeft (6.0f), juce::Justification::centredLeft);
}

void PillSwitch::paintButton (juce::Graphics& g, bool highlighted, bool)
{
    auto r = getLocalBounds().toFloat().withSizeKeepingCentre (std::min (44.0f, static_cast<float> (getWidth())), 20.0f);
    const bool on = getToggleState();
    recess (g, r, r.getHeight() * 0.5f, on ? colour::accent.withAlpha (0.85f) : colour::keybedFloor.darker (0.04f));
    const float d = r.getHeight() - 4.0f;
    const auto knob = juce::Rectangle<float> (d, d).withCentre ({ on ? r.getRight() - 2.0f - d * 0.5f : r.getX() + 2.0f + d * 0.5f, r.getCentreY() });
    contactShadow (g, knob, d * 0.5f, 0.6f);
    g.setGradientFill (juce::ColourGradient (juce::Colours::white, knob.getX(), knob.getY(), colour::keyNormal.darker (highlighted ? 0.0f : 0.05f), knob.getX(), knob.getBottom(), false));
    g.fillEllipse (knob);
    g.setColour (juce::Colour (0xffa99c8a).withAlpha (0.6f));
    g.drawEllipse (knob, 0.7f);
}

Fader::Fader()
{
    setSliderStyle (juce::Slider::LinearHorizontal);
    setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
}

void Fader::paint (juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat();
    const float pos = static_cast<float> (valueToProportionOfLength (getValue()));
    const float capW = 14.0f, capH = std::min (22.0f, b.getHeight());
    const float x0 = b.getX() + capW * 0.5f, x1 = b.getRight() - capW * 0.5f;
    const float cx = x0 + (x1 - x0) * pos;
    // T3 rail: a fine brushed-metal guide, 3.2 px, standing 0.6 mm.
    const auto rail = juce::Rectangle<float> (x0, b.getCentreY() - 1.6f, x1 - x0, 3.2f);
    g.setColour (juce::Colours::black.withAlpha (0.12f));
    g.fillRoundedRectangle (rail.translated (0.4f, 0.9f), 1.6f);
    g.setGradientFill (juce::ColourGradient (colour::faderRail.brighter (0.25f), rail.getX(), rail.getY(), colour::faderRail.darker (0.2f), rail.getX(), rail.getBottom(), false));
    g.fillRoundedRectangle (rail, 1.6f);
    // The value: a 1.2 px accent fill to the cap.
    g.setColour (colour::accent);
    g.fillRoundedRectangle (juce::Rectangle<float> (x0, b.getCentreY() - 0.6f, cx - x0, 1.2f), 0.6f);
    // C1 cap: warm ivory, three grip grooves across the travel; lifts on hover / drag.
    const bool lifted = isMouseOverOrDragging();
    auto cap = juce::Rectangle<float> (capW, capH).withCentre ({ cx, b.getCentreY() });
    contactShadow (g, cap, 4.0f, lifted ? 1.4f : 1.0f);
    if (lifted)
        cap = cap.translated (0.0f, -0.5f);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xfff4f0ea), cap.getX(), cap.getY(), colour::faderCap, cap.getX(), cap.getBottom(), false));
    g.fillRoundedRectangle (cap, 4.0f);
    g.setColour (juce::Colours::white.withAlpha (lifted ? 0.9f : 0.7f));
    g.drawRoundedRectangle (cap.reduced (0.7f), 3.5f, 0.8f);
    g.setColour (juce::Colour (0xff8f877b).withAlpha (0.55f));
    g.drawRoundedRectangle (cap.reduced (0.2f), 4.0f, 0.7f);
    for (int i = -1; i <= 1; ++i)
    {
        const float gx = cap.getCentreX() + static_cast<float> (i) * 3.0f;
        g.setColour (juce::Colour (0xff8f877b).withAlpha (0.6f));
        g.fillRect (juce::Rectangle<float> (gx - 0.4f, cap.getY() + 5.0f, 0.8f, cap.getHeight() - 10.0f));
        g.setColour (juce::Colours::white.withAlpha (0.7f));
        g.fillRect (juce::Rectangle<float> (gx + 0.4f, cap.getY() + 5.0f, 0.6f, cap.getHeight() - 10.0f));
    }
}

void Meter::setLevels (float left, float right)
{
    // Instant rise, ~300 ms fall at 30 Hz.
    l = std::max (left, l * 0.82f);
    r = std::max (right, r * 0.82f);
    repaint();
}

void Meter::paint (juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat();
    recess (g, b, 3.0f, juce::Colour (0xff2a2d2e));
    auto inner = b.reduced (2.5f);
    const float w = (inner.getWidth() - 2.0f) * 0.5f;
    for (int ch = 0; ch < 2; ++ch)
    {
        const float level = ch == 0 ? l : r;
        const float db = level > 1.0e-6f ? 20.0f * std::log10 (level) : -120.0f;
        const float frac = juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 66.0f);
        const auto bar = juce::Rectangle<float> (inner.getX() + static_cast<float> (ch) * (w + 2.0f), inner.getY(), w, inner.getHeight());
        const auto lit = bar.withTop (bar.getBottom() - bar.getHeight() * frac);
        g.setGradientFill (juce::ColourGradient (colour::accent.brighter (0.2f), bar.getX(), bar.getY(), juce::Colour (0xffc4561f), bar.getX(), bar.getBottom(), false));
        g.fillRect (lit);
    }
}

// ---------------------------------------------------------------------------------------

void iconUndo (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c, bool redo)
{
    const auto s = std::min (r.getWidth(), r.getHeight());
    const auto b = r.withSizeKeepingCentre (s, s);
    juce::Path arc;
    arc.addCentredArc (b.getCentreX(), b.getCentreY() + s * 0.08f, s * 0.32f, s * 0.32f, 0.0f, redo ? -1.9f : 1.9f, redo ? 1.2f : -1.2f, true);
    g.setColour (c);
    g.strokePath (arc, juce::PathStrokeType (1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    const auto tip = arc.getPointAlongPath (0.0f);
    juce::Path head;
    const float dir = redo ? 1.0f : -1.0f;
    head.addTriangle (tip.x - dir * s * 0.02f, tip.y - s * 0.17f, tip.x - dir * s * 0.02f, tip.y + s * 0.13f, tip.x + dir * s * 0.18f, tip.y - s * 0.02f);
    g.fillPath (head);
}

void iconChevron (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c, bool right)
{
    const auto s = std::min (r.getWidth(), r.getHeight()) * 0.5f;
    const auto m = r.getCentre();
    juce::Path p;
    const float d = right ? 1.0f : -1.0f;
    p.startNewSubPath (m.x - d * s * 0.3f, m.y - s * 0.55f);
    p.lineTo (m.x + d * s * 0.3f, m.y);
    p.lineTo (m.x - d * s * 0.3f, m.y + s * 0.55f);
    g.setColour (c);
    g.strokePath (p, juce::PathStrokeType (1.5f, juce::PathStrokeType::mitered, juce::PathStrokeType::rounded));
}

void iconPlay (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c)
{
    const auto s = std::min (r.getWidth(), r.getHeight());
    const auto m = r.getCentre();
    juce::Path p;
    p.addTriangle (m.x - s * 0.28f, m.y - s * 0.34f, m.x - s * 0.28f, m.y + s * 0.34f, m.x + s * 0.34f, m.y);
    g.setColour (c);
    g.fillPath (p);
}

void iconMenu (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c)
{
    g.setColour (c);
    const float w = std::min (r.getWidth(), 14.0f);
    for (int i = -1; i <= 1; ++i)
        g.fillRoundedRectangle (juce::Rectangle<float> (w, 1.4f).withCentre ({ r.getCentreX(), r.getCentreY() + static_cast<float> (i) * 4.0f }), 0.7f);
}

void iconPower (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c)
{
    const auto s = std::min (r.getWidth(), r.getHeight());
    const auto m = r.getCentre();
    juce::Path arc;
    arc.addCentredArc (m.x, m.y + s * 0.04f, s * 0.36f, s * 0.36f, 0.0f, 0.65f, 6.28f - 0.65f, true);
    g.setColour (c);
    g.strokePath (arc, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.drawLine (m.x, m.y - s * 0.42f, m.x, m.y - s * 0.02f, 1.6f);
}

void iconKeys (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c)
{
    const auto s = std::min (r.getWidth(), r.getHeight());
    const auto b = r.withSizeKeepingCentre (s, s * 0.8f);
    g.setColour (c);
    g.drawRoundedRectangle (b, 1.5f, 1.3f);
    for (int i = 1; i < 4; ++i)
    {
        const float x = b.getX() + b.getWidth() * static_cast<float> (i) / 4.0f;
        g.fillRect (juce::Rectangle<float> (x - 0.5f, b.getY(), 1.0f, b.getHeight()));
        g.fillRect (juce::Rectangle<float> (x - 1.8f, b.getY(), 3.6f, b.getHeight() * 0.55f));
    }
}

void iconGear (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c)
{
    const auto s = std::min (r.getWidth(), r.getHeight());
    const auto m = r.getCentre();
    juce::Path p;
    for (int i = 0; i < 8; ++i)
    {
        const float a = static_cast<float> (i) * juce::MathConstants<float>::twoPi / 8.0f;
        p.addRectangle (juce::Rectangle<float> (s * 0.16f, s * 0.2f).withCentre ({ m.x + std::sin (a) * s * 0.38f, m.y - std::cos (a) * s * 0.38f }));
    }
    p.addEllipse (juce::Rectangle<float> (s * 0.62f, s * 0.62f).withCentre (m));
    g.setColour (c);
    g.fillPath (p);
    g.setColour (colour::keyNormal);
    g.fillEllipse (juce::Rectangle<float> (s * 0.24f, s * 0.24f).withCentre (m));
}

} // namespace osp::drumsplugin::ceramic
