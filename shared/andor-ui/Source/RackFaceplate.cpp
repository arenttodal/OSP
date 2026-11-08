#include "RackFaceplate.h"
#include "OspLookAndFeel.h"

namespace osp::plugin::rack::hardware
{

void paint (juce::Graphics& g, juce::Rectangle<int> local, int titleOffset, const Faceplate& f, const juce::String& heading, bool lit,
            juce::Rectangle<int> graph, float subtitleLimit)
{
    const auto chassis = hardware::chassis (local).toFloat();
    const float railBottom = static_cast<float> (titleOffset);
    // The chassis: a soft shadow, warm ceramic, a hairline bezel and a light inner edge.
    g.setColour (juce::Colour (0x26000000));
    g.fillRoundedRectangle (chassis.translated (0.0f, 1.6f), chassisRadius);
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
        // The stripes: three bands sweeping from under the rail down the body's left edge.
        const float x0 = chassis.getX(), top = railBottom, bottom = chassis.getBottom();
        for (int i = 0; i < 3; ++i)
        {
            const float w = 11.0f, off = static_cast<float> (i) * 13.5f;
            const float topL = x0 + 10.0f + off, botL = x0 - 12.0f + off;
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
    g.setColour (juce::Colour (0xffc9bca9));
    g.drawRoundedRectangle (chassis.reduced (0.5f), chassisRadius, 1.0f);
    g.setColour (juce::Colours::white.withAlpha (0.45f));
    g.drawRoundedRectangle (chassis.reduced (1.6f).withTrimmedTop (railBottom - chassis.getY()), chassisRadius - 1.5f, 0.8f);
    // Screws: four, in the corners.
    auto screw = [&g] (juce::Point<float> c, bool onEnamel) {
        const auto r = juce::Rectangle<float> (6.4f, 6.4f).withCentre (c);
        g.setGradientFill (juce::ColourGradient (juce::Colour (onEnamel ? 0xffcfc6b8 : 0xffe9e2d6), r.getX(), r.getY(), juce::Colour (0xff8f8678), r.getRight(), r.getBottom(), false));
        g.fillEllipse (r);
        g.setColour (juce::Colour (0x66000000));
        g.drawEllipse (r, 0.6f);
        g.setColour (juce::Colour (0xaa4d473f));
        g.drawLine (c.x - 2.0f, c.y + 1.2f, c.x + 2.0f, c.y - 1.2f, 0.9f);
    };
    screw ({ chassis.getX() + 7.5f, chassis.getY() + 7.5f }, true);
    screw ({ chassis.getRight() - 7.5f, chassis.getY() + 7.5f }, true);
    screw ({ chassis.getX() + 7.5f, chassis.getBottom() - 7.5f }, false);
    screw ({ chassis.getRight() - 7.5f, chassis.getBottom() - 7.5f }, false);
    // The LED (the effect's real ON), the title and its subtitle.
    const float railMid = 0.5f * (chassis.getY() + railBottom);
    const juce::Point<float> led (chassis.getX() + 25.0f, railMid);
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
