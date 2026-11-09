#include "RackFaceplate.h"
#include "OspLookAndFeel.h"

namespace osp::plugin::rack::hardware
{

namespace
{
    /** V5 PATTERNED HARDWARE: one enamel machine - the body and the rail the same material
        (the rail a step deeper), the device's motif behind the controls' left, four small
        machined bolts, the graph's glass in a dark enamel bezel. */
    void paintEnamel (juce::Graphics& g, juce::Rectangle<int> local, int titleOffset, const Faceplate& f, const juce::String& heading, bool lit,
                      juce::Rectangle<int> graph, float subtitleLimit)
    {
        const auto face = local.toFloat();
        const float radius = bleedMeasures.radius;
        const float railBottom = static_cast<float> (titleOffset);
        juce::Path shape;
        shape.addRoundedRectangle (face, radius);
        {
            juce::Graphics::ScopedSaveState clip (g);
            g.reduceClipRegion (shape);
            // The body: matte enamel, a whisper of light from above, a satin grain.
            g.setGradientFill (juce::ColourGradient (f.body.brighter (0.05f), 0.0f, railBottom, f.body.darker (0.07f), 0.0f, face.getBottom(), false));
            g.fillRect (face);
            for (float gy = railBottom + 2.0f; gy < face.getBottom(); gy += 3.0f)
            {
                g.setColour ((static_cast<int> (gy) % 2 == 0 ? juce::Colours::white : juce::Colours::black).withAlpha (0.018f));
                g.fillRect (juce::Rectangle<float> (face.getX(), gy, face.getWidth(), 1.0f));
            }
            // The motif: the body's left (about a sixth of it), fading into the enamel.
            if (f.motif != nullptr)
            {
                const auto zone = juce::Rectangle<float> (face.getX(), railBottom + 1.0f, std::max (160.0f, 0.19f * face.getWidth()), face.getBottom() - railBottom - 1.0f);
                // (full strength over its first half, then fading out in narrow steps: no seam)
                const int steps = 12;
                const float solid = 0.5f * zone.getWidth(), stepW = (zone.getWidth() - solid) / static_cast<float> (steps);
                for (int i = -1; i < steps; ++i)
                {
                    const auto strip = i < 0 ? zone.withWidth (solid) : juce::Rectangle<float> (zone.getX() + solid + stepW * static_cast<float> (i), zone.getY(), stepW, zone.getHeight());
                    juce::Graphics::ScopedSaveState zoneClip (g);
                    g.reduceClipRegion (strip.getSmallestIntegerContainer());
                    const float opacity = i < 0 ? 1.0f : 1.0f - (static_cast<float> (i) + 0.5f) / static_cast<float> (steps);
                    g.beginTransparencyLayer (opacity);
                    f.motif (g, zone);
                    g.endTransparencyLayer();
                }
            }
            // The rail: the same enamel, deeper; a seam of light under it.
            const auto rail = juce::Rectangle<float> (face.getX(), face.getY(), face.getWidth(), railBottom - face.getY());
            g.setGradientFill (juce::ColourGradient (f.enamel.brighter (0.08f), 0.0f, rail.getY(), f.enamel.darker (0.08f), 0.0f, rail.getBottom(), false));
            g.fillRect (rail);
            g.setColour (juce::Colours::white.withAlpha (0.10f));
            g.fillRect (rail.withHeight (1.0f).translated (0.0f, 1.0f));
            g.setColour (juce::Colours::black.withAlpha (0.20f));
            g.fillRect (rail.withTop (rail.getBottom() - 1.0f));
            g.setColour (juce::Colours::white.withAlpha (0.12f));
            g.fillRect (rail.withTop (rail.getBottom()).withHeight (1.0f));
        }
        g.setColour (f.body.darker (0.45f));
        g.drawRoundedRectangle (face.reduced (0.5f), radius, 1.0f);
        g.setColour (juce::Colours::white.withAlpha (0.10f));
        g.drawRoundedRectangle (face.reduced (1.5f).withTrimmedTop (railBottom - face.getY()), radius - 1.0f, 0.8f);
        // Four bolts, machined, small, at one inset (the rail's corners clear of the LED and X).
        const float boltSize = 5.5f, inset = 8.5f;
        auto bolt = [&g, &f, boltSize] (juce::Point<float> c, bool onRail) {
            const auto base = onRail ? f.enamel : f.body;
            const auto r = juce::Rectangle<float> (boltSize, boltSize).withCentre (c);
            g.setColour (juce::Colours::black.withAlpha (0.28f));
            g.fillEllipse (r.translated (0.0f, 0.8f).expanded (0.4f));
            g.setGradientFill (juce::ColourGradient (base.brighter (0.55f), r.getX(), r.getY(), base.darker (0.55f), r.getRight(), r.getBottom(), false));
            g.fillEllipse (r);
            g.setColour (base.darker (0.75f).withAlpha (0.8f));
            g.drawEllipse (r, 0.6f);
            g.drawLine (c.x - 0.30f * boltSize, c.y + 0.18f * boltSize, c.x + 0.30f * boltSize, c.y - 0.18f * boltSize, 0.9f);
        };
        bolt ({ face.getX() + inset, face.getY() + inset }, true);
        bolt ({ face.getRight() - inset, face.getY() + inset }, true);
        bolt ({ face.getX() + inset, face.getBottom() - inset }, false);
        bolt ({ face.getRight() - inset, face.getBottom() - inset }, false);
        // The LED (what plays), the title, its descriptor.
        const float railMid = 0.5f * (face.getY() + railBottom);
        const juce::Point<float> led (face.getX() + 24.0f, railMid);
        g.setColour (juce::Colours::black.withAlpha (0.35f));
        g.fillEllipse (juce::Rectangle<float> (11.0f, 11.0f).withCentre (led));
        if (lit)
        {
            g.setGradientFill (juce::ColourGradient (f.glow.withAlpha (0.45f), led.x, led.y, f.glow.withAlpha (0.0f), led.x + 10.0f, led.y, true));
            g.fillEllipse (juce::Rectangle<float> (20.0f, 20.0f).withCentre (led));
        }
        g.setGradientFill (juce::ColourGradient (lit ? f.glow.brighter (0.4f) : f.enamel.darker (0.3f), led.x - 1.5f, led.y - 1.5f,
                                                 lit ? f.glow : f.enamel.darker (0.6f), led.x + 3.0f, led.y + 3.0f, true));
        g.fillEllipse (juce::Rectangle<float> (7.5f, 7.5f).withCentre (led));
        const auto titleFont = fonts::make (17.0f, fonts::Weight::bold, 0.08f);
        const float titleX = led.x + 15.0f;
        const float titleW = juce::GlyphArrangement::getStringWidth (titleFont, heading) + 4.0f;
        g.setColour (juce::Colour (0xfff7eedf));
        g.setFont (titleFont);
        g.drawText (heading, juce::Rectangle<float> (titleX, face.getY(), titleW + 4.0f, railBottom - face.getY()), juce::Justification::centredLeft, false);
        if (f.subtitle.isNotEmpty())
        {
            const float sx = titleX + titleW + 16.0f;
            if (subtitleLimit > sx + 40.0f)
            {
                g.setColour (juce::Colour (0xfff7eedf).withAlpha (0.7f));
                g.setFont (fonts::make (9.5f, fonts::Weight::medium, 0.3f));
                g.drawText (f.subtitle, juce::Rectangle<float> (sx, face.getY(), subtitleLimit - sx, railBottom - face.getY()).translated (0.0f, 0.5f), juce::Justification::centredLeft, true);
            }
        }
        // The graph's bezel: the enamel, deeper, around the glass.
        if (! graph.isEmpty())
        {
            const auto glass = graph.toFloat();
            const auto rim = glass.expanded (4.0f);
            g.setColour (juce::Colours::black.withAlpha (0.22f));
            g.fillRoundedRectangle (rim.translated (0.0f, 1.0f), 8.0f);
            g.setGradientFill (juce::ColourGradient (f.body.darker (0.30f), 0.0f, rim.getY(), f.body.darker (0.52f), 0.0f, rim.getBottom(), false));
            g.fillRoundedRectangle (rim, 8.0f);
            g.setColour (juce::Colours::white.withAlpha (0.14f));
            g.drawRoundedRectangle (rim.reduced (0.6f), 7.5f, 0.7f);
            g.setColour (juce::Colour (0xff15110d));
            g.fillRoundedRectangle (glass.expanded (1.0f), 6.0f);
        }
    }
}

void paint (juce::Graphics& g, juce::Rectangle<int> local, int titleOffset, const Faceplate& f, const juce::String& heading, bool lit,
            juce::Rectangle<int> graph, float subtitleLimit)
{
    if (f.enamelBody)
    {
        paintEnamel (g, local, titleOffset, f, heading, lit, graph, subtitleLimit);
        return;
    }
    const auto& m = measures (f);
    const bool bleed = f.fullBleed;
    const auto chassis = m.chassis (local).toFloat();
    const float chassisRadius = m.radius;
    const float railBottom = static_cast<float> (titleOffset);
    // The chassis: warm ceramic, a hairline bezel and a light inner edge (inset: its own soft
    // shadow; full bleed: the rack's shadow is the host's, outside these bounds).
    if (! bleed)
    {
        g.setColour (juce::Colour (0x26000000));
        g.fillRoundedRectangle (chassis.translated (0.0f, 1.6f), chassisRadius);
    }
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xfff7f0e5), 0.0f, chassis.getY(), juce::Colour (0xffece2d3), 0.0f, chassis.getBottom(), false));
    g.fillRoundedRectangle (chassis, chassisRadius);
    juce::Path shape;
    shape.addRoundedRectangle (chassis, chassisRadius);
    {
        juce::Graphics::ScopedSaveState clip (g);
        g.reduceClipRegion (shape);
        // The enamel rail.
        const auto rail = juce::Rectangle<float> (chassis.getX(), chassis.getY(), chassis.getWidth(), railBottom - chassis.getY());
        g.setGradientFill (juce::ColourGradient (f.enamel.brighter (0.10f), 0.0f, rail.getY(), f.enamel.darker (0.12f), 0.0f, rail.getBottom(), false));
        g.fillRect (rail);
        g.setColour (juce::Colours::white.withAlpha (0.10f));
        g.fillRect (rail.withHeight (1.0f).translated (0.0f, 1.0f));
        g.setColour (juce::Colours::black.withAlpha (0.22f));
        g.fillRect (rail.withTop (rail.getBottom() - 1.0f));
        g.setColour (juce::Colours::white.withAlpha (0.35f));
        g.fillRect (rail.withTop (rail.getBottom()).withHeight (1.0f));
        // The stripes: three bands sweeping from under the rail down the body's left edge
        // (full bleed: a narrow signature, 22 px at the top, leaning out of the face).
        const float x0 = chassis.getX(), top = railBottom, bottom = chassis.getBottom();
        for (int i = 0; i < 3; ++i)
        {
            const float w = bleed ? 5.0f : 11.0f, off = static_cast<float> (i) * (bleed ? 7.0f : 13.5f);
            const float topL = x0 + (bleed ? 3.0f : 10.0f) + off, botL = x0 + (bleed ? -9.0f : -12.0f) + off;
            juce::Path band;
            band.startNewSubPath (topL, top);
            band.lineTo (topL + w, top);
            band.quadraticTo (botL + w + 2.0f, top + 22.0f, botL + w, bottom);
            band.lineTo (botL, bottom);
            band.quadraticTo (botL + 2.0f, top + 22.0f, topL, top);
            band.closeSubPath();
            g.setColour (f.stripes[static_cast<std::size_t> (i)]);
            g.fillPath (band);
        }
    }
    // One outline (full bleed: the rack's only one).
    g.setColour (bleed ? juce::Colour (0xffbdb09c) : juce::Colour (0xffc9bca9));
    g.drawRoundedRectangle (chassis.reduced (0.5f), chassisRadius, 1.0f);
    if (! bleed)
    {
        g.setColour (juce::Colours::white.withAlpha (0.45f));
        g.drawRoundedRectangle (chassis.reduced (1.6f).withTrimmedTop (railBottom - chassis.getY()), chassisRadius - 1.5f, 0.8f);
    }
    // Screws: four in the corners (full bleed: two, at the foot, smaller and quieter).
    const float screwSize = bleed ? 5.0f : 6.4f, screwIn = bleed ? 6.5f : 7.5f;
    auto screw = [&g, screwSize] (juce::Point<float> c, bool onEnamel) {
        const auto r = juce::Rectangle<float> (screwSize, screwSize).withCentre (c);
        g.setGradientFill (juce::ColourGradient (juce::Colour (onEnamel ? 0xffcfc6b8 : 0xffe9e2d6), r.getX(), r.getY(), juce::Colour (0xff8f8678), r.getRight(), r.getBottom(), false));
        g.fillEllipse (r);
        g.setColour (juce::Colour (0x66000000));
        g.drawEllipse (r, 0.6f);
        g.setColour (juce::Colour (0xaa4d473f));
        g.drawLine (c.x - 0.31f * screwSize, c.y + 0.19f * screwSize, c.x + 0.31f * screwSize, c.y - 0.19f * screwSize, 0.9f);
    };
    if (! bleed)   // (full bleed: the rail's corners hold the LED and the X, no screws)
    {
        screw ({ chassis.getX() + screwIn, chassis.getY() + screwIn }, true);
        screw ({ chassis.getRight() - screwIn, chassis.getY() + screwIn }, true);
    }
    screw ({ chassis.getX() + screwIn, chassis.getBottom() - screwIn }, false);
    screw ({ chassis.getRight() - screwIn, chassis.getBottom() - screwIn }, false);
    // The LED (the effect's real ON), the title and its subtitle.
    const float railMid = 0.5f * (chassis.getY() + railBottom);
    const juce::Point<float> led (chassis.getX() + (bleed ? 22.0f : 25.0f), railMid);
    g.setColour (juce::Colours::black.withAlpha (0.35f));
    g.fillEllipse (juce::Rectangle<float> (11.0f, 11.0f).withCentre (led));
    if (lit)
    {
        g.setGradientFill (juce::ColourGradient (f.glow.withAlpha (0.45f), led.x, led.y, f.glow.withAlpha (0.0f), led.x + 10.0f, led.y, true));
        g.fillEllipse (juce::Rectangle<float> (20.0f, 20.0f).withCentre (led));
    }
    g.setGradientFill (juce::ColourGradient (lit ? f.glow.brighter (0.55f) : juce::Colour (0xff6f675d), led.x - 1.5f, led.y - 1.5f,
                                             lit ? f.glow : juce::Colour (0xff4a443d), led.x + 3.0f, led.y + 3.0f, true));
    g.fillEllipse (juce::Rectangle<float> (7.5f, 7.5f).withCentre (led));
    const auto titleFont = fonts::make (15.5f, fonts::Weight::bold, 0.09f).italicised();
    const float titleX = led.x + 15.0f;
    const float titleW = juce::GlyphArrangement::getStringWidth (titleFont, heading) + 4.0f;
    g.setColour (juce::Colour (0xfff7eedf));
    g.setFont (titleFont);
    g.drawText (heading, juce::Rectangle<float> (titleX, chassis.getY(), titleW + 4.0f, railBottom - chassis.getY()), juce::Justification::centredLeft, false);
    if (f.subtitle.isNotEmpty())
    {
        const float sx = titleX + titleW + 16.0f;
        const float limit = subtitleLimit;
        if (limit > sx + 40.0f)
        {
            g.setColour (juce::Colour (0xfff7eedf).withAlpha (0.66f));
            g.setFont (fonts::make (8.5f, fonts::Weight::medium, 0.34f));
            g.drawText (f.subtitle, juce::Rectangle<float> (sx, chassis.getY(), limit - sx, railBottom - chassis.getY()).translated (0.0f, 0.5f), juce::Justification::centredLeft, true);
        }
    }
    // The graph's bezel: warm metal around the glass.
    if (! graph.isEmpty())
    {
        const auto glass = graph.toFloat();
        const auto rim = glass.expanded (4.0f);
        g.setColour (juce::Colour (0x2a000000));
        g.fillRoundedRectangle (rim.translated (0.0f, 1.0f), 8.0f);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xffd2bf9b).interpolatedWith (f.enamel, 0.18f), 0.0f, rim.getY(),
                                                 juce::Colour (0xff8c7558).interpolatedWith (f.enamel, 0.25f), 0.0f, rim.getBottom(), false));
        g.fillRoundedRectangle (rim, 8.0f);
        g.setColour (juce::Colours::white.withAlpha (0.35f));
        g.drawRoundedRectangle (rim.reduced (0.6f), 7.5f, 0.7f);
        g.setColour (juce::Colour (0xff15110d));
        g.fillRoundedRectangle (glass.expanded (1.0f), 6.0f);
    }
}


OverflowKey::OverflowKey (std::function<void (juce::Component&)> f) : action (std::move (f))
{
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
    setWantsKeyboardFocus (true);
    setMouseClickGrabsKeyboardFocus (false);
    setTitle ("More");
    setTooltip ("Presets and more");
}

void OverflowKey::paint (juce::Graphics& g)
{
    const auto c = getLocalBounds().toFloat().getCentre();
    g.setColour (juce::Colour (0xfff3eadb).withAlpha (hover || hasKeyboardFocus (false) ? 1.0f : 0.78f));
    for (int i = -1; i <= 1; ++i)
        g.fillEllipse (juce::Rectangle<float> (3.0f, 3.0f).withCentre ({ c.x + 5.5f * static_cast<float> (i), c.y }));
}

void OverflowKey::mouseEnter (const juce::MouseEvent&)
{
    hover = true;
    repaint();
}

void OverflowKey::mouseExit (const juce::MouseEvent&)
{
    hover = false;
    repaint();
}

void OverflowKey::mouseUp (const juce::MouseEvent& e)
{
    if (getLocalBounds().contains (e.getPosition()) && action != nullptr)
        action (*this);
}

bool OverflowKey::keyPressed (const juce::KeyPress& k)
{
    if ((k == juce::KeyPress::spaceKey || k == juce::KeyPress::returnKey) && action != nullptr)
    {
        action (*this);
        return true;
    }
    return false;
}

std::unique_ptr<juce::AccessibilityHandler> OverflowKey::createAccessibilityHandler()
{
    return std::make_unique<juce::AccessibilityHandler> (*this, juce::AccessibilityRole::button,
                                                          juce::AccessibilityActions().addAction (juce::AccessibilityActionType::press, [this] {
                                                              if (action != nullptr)
                                                                  action (*this);
                                                          }));
}

} // namespace osp::plugin::rack::hardware
