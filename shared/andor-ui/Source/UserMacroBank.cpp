#include "UserMacroBank.h"

#include "Design.h"
#include "OspLookAndFeel.h"

namespace osp::plugin
{

namespace
{
    const juce::Colour sage { 0xff8fae8b };   // (the USER bank's muted sage)
}

//==============================================================================
MacroBankTab::Half::Half (MacroBankTab& o, int b, const juce::String& title) : owner (o), bank (b)
{
    setTitle (title);
    setWantsKeyboardFocus (true);
    setMouseClickGrabsKeyboardFocus (false);
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void MacroBankTab::Half::mouseUp (const juce::MouseEvent& e)
{
    if (getLocalBounds().contains (e.getPosition()) && ! e.mods.isPopupMenu())
        owner.choose (bank);
}

bool MacroBankTab::Half::keyPressed (const juce::KeyPress& key)
{
    if (key.getKeyCode() == juce::KeyPress::spaceKey || key.getKeyCode() == juce::KeyPress::returnKey)
    {
        owner.choose (bank);
        return true;
    }
    if (key.getKeyCode() == juce::KeyPress::leftKey || key.getKeyCode() == juce::KeyPress::rightKey)
    {
        owner.half (bank == 0 ? 1 : 0).grabKeyboardFocus();
        return true;
    }
    return false;
}

MacroBankTab::MacroBankTab()
{
    setTitle ("Macro bank");
    addAndMakeVisible (core);
    addAndMakeVisible (user);
}

void MacroBankTab::setBank (int b)
{
    b = b == 1 ? 1 : 0;
    if (b == current)
        return;
    current = b;
    repaint();
}

void MacroBankTab::choose (int b)
{
    if (b == current)
        return;
    current = b;
    repaint();
    if (onChange != nullptr)
        onChange (current);
}

void MacroBankTab::setPanel (juce::Rectangle<float> panelInParent, float panelRadius)
{
    panel = panelInParent;
    panelCorner = panelRadius;
    repaint();
}

juce::Rectangle<float> MacroBankTab::tabArea (int b) const
{
    const float left = targetSide + flare + (b == 0 ? 0.0f : tabWidth - overlap);
    return { left, targetAbove, tabWidth, rise };
}

juce::Rectangle<float> MacroBankTab::drawnArea() const
{
    return tabArea (0).getUnion (tabArea (1));
}

void MacroBankTab::resized()
{
    // Each tab's target: its half of the component, from above the tab into the panel.
    const int mid = juce::roundToInt (tabArea (1).getX() + 0.5f * overlap);
    core.setBounds (0, 0, mid, getHeight());
    user.setBounds (mid, 0, getWidth() - mid, getHeight());
}

juce::Path MacroBankTab::tabShape (int b, bool chosen) const
{
    // Rounded upper corners; the chosen tab flares into the panel's edge with soft fillets and
    // runs on into the panel (its join drawn by the panel's own surface: no line across it).
    // The other stops at the edge (the panel's edge passes in front of it).
    const auto r = tabArea (b);
    const float base = chosen ? panelTopY() + intoPanel : panelTopY();
    const float top = chosen ? r.getY() : r.getY() + 1.5f;   // (the one behind a touch lower)
    juce::Path p;
    if (chosen)
    {
        p.startNewSubPath (r.getX() - flare, base);
        p.lineTo (r.getX() - flare, panelTopY());
        p.quadraticTo (r.getX(), panelTopY(), r.getX(), panelTopY() - flare);
    }
    else
    {
        p.startNewSubPath (r.getX(), base);
    }
    p.lineTo (r.getX(), top + radius);
    p.quadraticTo (r.getX(), top, r.getX() + radius, top);
    p.lineTo (r.getRight() - radius, top);
    p.quadraticTo (r.getRight(), top, r.getRight(), top + radius);
    if (chosen)
    {
        p.lineTo (r.getRight(), panelTopY() - flare);
        p.quadraticTo (r.getRight(), panelTopY(), r.getRight() + flare, panelTopY());
        p.lineTo (r.getRight() + flare, base);
    }
    else
    {
        p.lineTo (r.getRight(), base);
    }
    p.closeSubPath();
    return p;
}

void MacroBankTab::paint (juce::Graphics& g)
{
    const float edge = panelTopY();
    const auto localPanel = panel.isEmpty() ? juce::Rectangle<float>()
                                            : panel.translated (-static_cast<float> (getX()), -static_cast<float> (getY()));
    auto outline = [&] (const juce::Path& shape, juce::Colour colour) {
        // The sides and the top only: never a line along the base.
        juce::Graphics::ScopedSaveState state (g);
        g.reduceClipRegion (juce::Rectangle<int> (0, 0, getWidth(), juce::roundToInt (edge)));
        g.setColour (colour);
        g.strokePath (shape, juce::PathStrokeType (1.0f));
    };
    // Behind: the other tab, recessed ceramic, quiet.
    const int other = 1 - current;
    {
        const auto shape = tabShape (other, false);
        g.setColour (design::colour::panelBottom.darker (0.07f));
        g.fillPath (shape);
        juce::ColourGradient shade (juce::Colours::black.withAlpha (0.0f), 0.0f, tabArea (other).getY(),
                                    juce::Colours::black.withAlpha (0.05f), 0.0f, edge, false);
        g.setGradientFill (shade);
        g.fillPath (shape);
        outline (shape, design::colour::hairline.withMultipliedAlpha (0.8f));
    }
    // In front: the chosen tab, cut from the panel (its surface raised by the tab's height, so
    // its top carries the panel's lit edge and its base meets the panel without a seam).
    {
        const auto shape = tabShape (current, true);
        {
            juce::Graphics::ScopedSaveState state (g);
            g.reduceClipRegion (juce::Rectangle<int> (0, 0, getWidth(), juce::roundToInt (edge)));
            design::CachedShadow (juce::Colour (0x1c302418), 2, { 0, 0 }).drawForPath (g, shape);
        }
        {
            juce::Graphics::ScopedSaveState state (g);
            g.reduceClipRegion (shape);
            if (! localPanel.isEmpty())
                design::draw::raised (g, localPanel.withY (tabArea (current).getY()), panelCorner, design::colour::panelTop, design::colour::panelBottom, 0.0f);
            else
            {
                g.setColour (design::colour::panelTop);
                g.fillPath (shape);
            }
        }
        outline (shape, design::colour::hairline.withMultipliedAlpha (0.55f));
    }
    // The words; the chosen one with a thin sage underline.
    g.setFont (type::popupLabel (8.5f));
    for (const int b : { 0, 1 })
    {
        const auto r = tabArea (b);
        auto& target = b == 0 ? core : user;
        const bool chosen = current == b, hover = target.isMouseOver();
        const auto words = r.withTrimmedTop (chosen ? 0.0f : 1.5f);
        g.setColour (chosen ? palette::text : palette::textDim.withMultipliedAlpha (hover ? 0.95f : 0.7f));
        g.drawText (b == 0 ? "CORE" : "USER", words.translated (0.0f, 0.5f), juce::Justification::centred, false);
        if (chosen)
        {
            const float w = juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), b == 0 ? "CORE" : "USER");
            g.setColour (sage);
            g.fillRect (juce::Rectangle<float> (w, 1.0f).withCentre ({ r.getCentreX(), r.getBottom() - 1.5f }));
        }
        if (target.hasKeyboardFocus (false))
        {
            g.setColour (sage.withAlpha (0.7f));
            g.drawRoundedRectangle (words.reduced (2.0f), 4.0f, 1.0f);
        }
    }
}

//==============================================================================
MacroLens::MacroLens()
{
    setTitle ("Macro assignments");
    setMouseCursor (juce::MouseCursor::DraggingHandCursor);
    setWantsKeyboardFocus (true);
    setMouseClickGrabsKeyboardFocus (false);
}

void MacroLens::mouseDown (const juce::MouseEvent& e)
{
    pressed = ! e.mods.isPopupMenu();
    dragging = false;
}

void MacroLens::mouseDrag (const juce::MouseEvent& e)
{
    if (! pressed)
        return;
    if (! dragging && e.getDistanceFromDragStart() < 4)
        return;
    if (! dragging)
    {
        dragging = true;
        grabKeyboardFocus();   // (Escape cancels)
    }
    if (onDragMove != nullptr)
        onDragMove (e.getScreenPosition());
}

void MacroLens::mouseUp (const juce::MouseEvent& e)
{
    if (! pressed)
        return;
    pressed = false;
    if (dragging)
    {
        dragging = false;
        if (onDragEnd != nullptr)
            onDragEnd (e.getScreenPosition(), false);
    }
    else if (getLocalBounds().contains (e.getPosition()) && onClick != nullptr)
        onClick();
}

void MacroLens::cancelDrag()
{
    if (! dragging)
        return;
    dragging = pressed = false;
    if (onDragEnd != nullptr)
        onDragEnd ({}, true);
}

bool MacroLens::keyPressed (const juce::KeyPress& key)
{
    if (key.getKeyCode() == juce::KeyPress::escapeKey && dragging)
    {
        cancelDrag();
        return true;
    }
    if ((key.getKeyCode() == juce::KeyPress::returnKey || key.getKeyCode() == juce::KeyPress::spaceKey) && onClick != nullptr)
    {
        onClick();
        return true;
    }
    return false;
}

//==============================================================================
void MacroAssignOverlay::beginDrag (juce::Colour colour, const juce::String& macroName)
{
    current = Mode::drag;
    accent = colour;
    macro = macroName;
    pending.clear();
    setMouseCursor (juce::MouseCursor::NormalCursor);
    repaint();
}

void MacroAssignOverlay::beginPick (juce::Colour colour, const juce::String& macroName, juce::Rectangle<float> bannerAnchor)
{
    current = Mode::pick;
    accent = colour;
    macro = macroName;
    pending.clear();
    // The banner: a small card above the anchor (the macro panel's top edge), never over it.
    const float w = 300.0f, h = 26.0f;
    banner = juce::Rectangle<float> (w, h).withCentre ({ bannerAnchor.getCentreX(), bannerAnchor.getY() - h * 0.5f - 6.0f });
    if (banner.getY() < 2.0f)
        banner.setY (2.0f);
    setMouseCursor (juce::MouseCursor::CrosshairCursor);
    repaint();
}

void MacroAssignOverlay::show (std::vector<juce::String> ids, juce::Colour colour)
{
    mapped = std::move (ids);
    accent = colour;
    if (current == Mode::idle || current == Mode::show)
        current = mapped.empty() ? Mode::idle : Mode::show;
    repaint();
}

void MacroAssignOverlay::stop()
{
    current = Mode::idle;
    pending.clear();
    mapped.clear();
    banner = {};
    repaint();
}

juce::String MacroAssignOverlay::bannerText() const
{
    return current == Mode::pick ? "Assign to " + macro + juce::String::fromUTF8 (" \xc2\xb7 Click a control \xc2\xb7 Esc") : juce::String();
}

juce::String MacroAssignOverlay::controlAt (juce::Point<float> where) const
{
    // The front-most (the last collected) eligible control under the point.
    for (auto it = controls.rbegin(); it != controls.rend(); ++it)
        if (it->area.contains (where))
            return it->parameterId;
    return {};
}

void MacroAssignOverlay::pointAt (juce::Point<float> where)
{
    const auto now = controlAt (where);
    if (now != pending)
    {
        pending = now;
        repaint();
    }
}

bool MacroAssignOverlay::hitTest (int x, int y)
{
    if (current != Mode::pick)
        return false;
    const juce::Point<float> p { static_cast<float> (x), static_cast<float> (y) };
    return banner.contains (p) || controlAt (p).isNotEmpty();
}

void MacroAssignOverlay::mouseMove (const juce::MouseEvent& e)
{
    pointAt (e.position);
}

void MacroAssignOverlay::mouseExit (const juce::MouseEvent&)
{
    if (current == Mode::pick && pending.isNotEmpty())
    {
        pending.clear();
        repaint();
    }
}

void MacroAssignOverlay::mouseUp (const juce::MouseEvent& e)
{
    if (current != Mode::pick)
        return;
    if (banner.contains (e.position))
    {
        if (onCancel != nullptr)
            onCancel();
        return;
    }
    const auto id = controlAt (e.position);
    if (id.isNotEmpty() && onPicked != nullptr)
        onPicked (id);
}

void MacroAssignOverlay::paint (juce::Graphics& g)
{
    if (current == Mode::idle)
        return;
    auto ringOf = [] (juce::Rectangle<float> area) {
        const float side = std::min (area.getWidth(), area.getHeight());
        return juce::Rectangle<float> (side + 8.0f, side + 8.0f).withCentre (area.getCentre());
    };
    auto wide = [] (juce::Rectangle<float> area) {
        return area.getWidth() > 1.6f * area.getHeight() || area.getHeight() > 1.6f * area.getWidth();
    };
    auto drawName = [&g] (const juce::String& name, juce::Rectangle<float> under) {
        g.setFont (type::popupLabel (10.0f));
        const auto label = juce::Rectangle<float> (160.0f, 14.0f).withCentre ({ under.getCentreX(), under.getBottom() + 9.0f });
        g.setColour (juce::Colour (0xff1a1e1f).withAlpha (0.85f));
        g.fillRoundedRectangle (label.withSizeKeepingCentre (juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), name) + 12.0f, 14.0f), 3.0f);
        g.setColour (juce::Colour (0xfff0ece4));
        g.drawText (name, label, juce::Justification::centred, false);
    };
    if (current == Mode::drag || current == Mode::pick)
    {
        // Every control it can move: a faint ring; the one under the pointer: a strong one and
        // a pending arc, with its name.
        for (const auto& c : controls)
        {
            const bool hot = c.parameterId == pending;
            g.setColour (accent.withAlpha (hot ? 0.95f : 0.28f));
            if (wide (c.area))
            {
                // A fader or a strip: its own outline, not a circle.
                g.drawRoundedRectangle (c.area.expanded (4.0f), 5.0f, hot ? 2.0f : 1.0f);
                if (hot)
                    drawName (c.name, c.area.expanded (4.0f));
                continue;
            }
            g.drawEllipse (ringOf (c.area), hot ? 2.0f : 1.0f);
            if (hot)
            {
                const auto r = ringOf (c.area).expanded (4.0f);
                juce::Path arc;
                arc.addCentredArc (r.getCentreX(), r.getCentreY(), 0.5f * r.getWidth(), 0.5f * r.getHeight(), 0.0f,
                                   -juce::MathConstants<float>::pi * 0.75f, juce::MathConstants<float>::pi * 0.75f, true);
                g.strokePath (arc, juce::PathStrokeType (1.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
                drawName (c.name, r);
            }
        }
    }
    if (current == Mode::show)
    {
        // The controls the macro moves: a thin arc in its colour, outside the knob's own marks.
        for (const auto& c : controls)
            if (std::find (mapped.begin(), mapped.end(), c.parameterId) != mapped.end())
            {
                if (wide (c.area))
                {
                    g.setColour (accent.withAlpha (0.75f));
                    g.drawRoundedRectangle (c.area.expanded (4.0f), 5.0f, 1.2f);
                    continue;
                }
                const auto r = ringOf (c.area);
                juce::Path arc;
                arc.addCentredArc (r.getCentreX(), r.getCentreY(), 0.5f * r.getWidth(), 0.5f * r.getHeight(), 0.0f,
                                   -juce::MathConstants<float>::pi * 0.75f, juce::MathConstants<float>::pi * 0.75f, true);
                g.setColour (accent.withAlpha (0.75f));
                g.strokePath (arc, juce::PathStrokeType (1.2f));
            }
    }
    if (current == Mode::pick && ! banner.isEmpty())
    {
        // The pick banner: the popovers' small card language.
        g.setColour (juce::Colours::black.withAlpha (0.12f));
        g.fillRoundedRectangle (banner.translated (0.0f, 2.0f), 6.0f);
        g.setColour (palette::ivory);
        g.fillRoundedRectangle (banner, 6.0f);
        g.setColour (design::colour::hairline);
        g.drawRoundedRectangle (banner.reduced (0.5f), 6.0f, 1.0f);
        g.setColour (accent);
        g.fillEllipse (juce::Rectangle<float> (7.0f, 7.0f).withCentre ({ banner.getX() + 14.0f, banner.getCentreY() }));
        g.setColour (palette::text);
        g.setFont (type::popupLabel (11.0f));
        g.drawText (bannerText(), banner.withTrimmedLeft (24.0f).withTrimmedRight (8.0f), juce::Justification::centredLeft, true);
    }
}

} // namespace osp::plugin
