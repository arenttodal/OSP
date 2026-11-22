#include "PerformanceComponents.h"

#include "Design.h"
#include "OspLookAndFeel.h"

namespace osp::plugin::performance
{

//==============================================================================
ModeKey::ModeKey() : juce::Button ("Enter Performance Mode")
{
    setWantsKeyboardFocus (true);
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
    setPerformance (false);
}

void ModeKey::setPerformance (bool p)
{
    performance = p;
    // The key names the action it offers (its icon too).
    const juce::String action = p ? "Edit Instrument" : "Enter Performance Mode";
    setButtonText (action);
    setTitle (action);
    setTooltip (action);
    setDescription (p ? "Show every control again (the sound does not change)"
                      : "Hide the editing controls: sources, your chosen macros and the keyboard (the sound does not change)");
    repaint();
}

void ModeKey::paintButton (juce::Graphics& g, bool over, bool down)
{
    // A header key as SAVE and FX ROUTING (their face, icon size and ink).
    using namespace design;
    const auto r = getLocalBounds().toFloat();
    const float k = r.getHeight() / 48.0f;
    if (! skin::drawnBySurface())   // ceramic: the housing draws the key
        draw::raised (g, r.reduced (0.5f), 9.0f * k, colour::cardTop, colour::cardBottom, 0.9f);
    const float s = 27.0f * k / 24.0f;
    const auto o = r.getCentre() - juce::Point<float> (12.0f * s, 12.0f * s) + juce::Point<float> (0.0f, down ? 0.5f : 0.0f);
    auto at = [&] (float x, float y) { return o + juce::Point<float> (x * s, y * s); };
    juce::Path p;
    if (! performance)
    {
        // A gauge: a 240-degree dial, its needle up-right, its hub.
        const auto c = at (12.0f, 13.5f);
        const float radius = 8.0f * s;
        p.addCentredArc (c.x, c.y, radius, radius, 0.0f, -juce::MathConstants<float>::pi * 2.0f / 3.0f, juce::MathConstants<float>::pi * 2.0f / 3.0f, true);
        const float needle = juce::MathConstants<float>::pi * 0.22f;
        p.startNewSubPath (c);
        p.lineTo (c + juce::Point<float> (std::sin (needle), -std::cos (needle)) * (radius - 2.0f * s));
        for (int i = -2; i <= 2; ++i)
        {
            const float a = static_cast<float> (i) * juce::MathConstants<float>::pi / 3.0f;
            const juce::Point<float> dir (std::sin (a), -std::cos (a));
            p.startNewSubPath (c + dir * (radius - 2.6f * s));
            p.lineTo (c + dir * (radius - 1.4f * s));
        }
        g.setColour (colour::text.withAlpha (over || down ? 0.85f : 0.6f));
        g.strokePath (p, juce::PathStrokeType (1.5f * k, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.fillEllipse (juce::Rectangle<float> (3.4f * s, 3.4f * s).withCentre (c));
    }
    else
    {
        // A pencil, writing down-left.
        p.startNewSubPath (at (6.0f, 18.0f));
        p.lineTo (at (6.0f, 15.2f));
        p.lineTo (at (15.5f, 5.7f));
        p.lineTo (at (18.3f, 8.5f));
        p.lineTo (at (8.8f, 18.0f));
        p.closeSubPath();
        p.startNewSubPath (at (13.6f, 7.6f));
        p.lineTo (at (16.4f, 10.4f));
        p.startNewSubPath (at (5.0f, 19.5f));
        p.lineTo (at (19.0f, 19.5f));
        g.setColour (colour::text.withAlpha (over || down ? 0.85f : 0.6f));
        g.strokePath (p, juce::PathStrokeType (1.5f * k, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
    if (hasKeyboardFocus (false))
    {
        g.setColour (juce::Colour (0xff88a8af));
        g.drawRoundedRectangle (r.reduced (1.5f), 9.0f * k, 1.5f);
    }
}

//==============================================================================
MacroFlap::MacroFlap() : juce::Button ("Choose visible macros")
{
    setTitle ("Choose visible macros");
    setTooltip ("Choose visible macros");
    setWantsKeyboardFocus (true);
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

juce::Rectangle<float> MacroFlap::tab() const
{
    // The drawn tab: 34 x 18, at the component's top right (the rest is hit area).
    const auto r = getLocalBounds().toFloat();
    return juce::Rectangle<float> (34.0f, 18.0f).withPosition (r.getRight() - 34.0f, r.getY());
}

void MacroFlap::paintButton (juce::Graphics& g, bool over, bool down)
{
    // A small tab folded over the housing's top edge: the housing's cream, a hairline, a soft
    // drop; the sliders icon in the quiet ink. No accent.
    using namespace design;
    const auto t = tab();
    juce::Path shape;
    shape.addRoundedRectangle (t.getX(), t.getY(), t.getWidth(), t.getHeight(), 5.0f, 5.0f, false, false, true, true);
    design::CachedShadow (juce::Colour (0x221e1a16), 4, { 0, 1 }).drawForPath (g, shape);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xfff6f1e8), 0.0f, t.getY(), juce::Colour (0xffe9e1d4), 0.0f, t.getBottom(), false));
    g.fillPath (shape);
    g.setColour (juce::Colour (0xffcfc5b6));
    g.strokePath (shape, juce::PathStrokeType (1.0f));
    // Three sliders.
    const auto ink = colour::text.withAlpha (over || down ? 0.78f : 0.5f);
    const auto icon = t.withSizeKeepingCentre (14.0f, 9.0f).translated (0.0f, down ? 0.5f : 0.0f);
    const float knobs[] = { 0.7f, 0.3f, 0.55f };
    for (int i = 0; i < 3; ++i)
    {
        const float y = icon.getY() + icon.getHeight() * static_cast<float> (i) / 2.0f;
        g.setColour (ink.withMultipliedAlpha (0.7f));
        g.fillRect (juce::Rectangle<float> (icon.getX(), y - 0.5f, icon.getWidth(), 1.0f));
        g.setColour (ink);
        g.fillEllipse (juce::Rectangle<float> (3.4f, 3.4f).withCentre ({ icon.getX() + icon.getWidth() * knobs[i], y }));
    }
    if (hasKeyboardFocus (false))
    {
        g.setColour (juce::Colour (0xff88a8af));
        g.drawRoundedRectangle (t.expanded (1.0f), 6.0f, 1.2f);
    }
}

//==============================================================================
namespace
{
    constexpr int rowHeight = 26, pickerWidth = 300, footerHeight = 30;
}

/** One macro in the picker: its tick, its name (USER: its state), and - when chosen - up / down. */
class MacroPicker::Row final : public juce::Component
{
public:
    Row (MacroPicker& o, PickerEntry e) : owner (o), entry (std::move (e))
    {
        setTitle (entry.name);
        setWantsKeyboardFocus (true);
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    }

    bool chosen() const { return owner.selection.contains (entry.id); }
    const PickerEntry& item() const noexcept { return entry; }

    void paint (juce::Graphics& g) override
    {
        using namespace design;
        const auto r = getLocalBounds().toFloat();
        if (isMouseOver (true))
        {
            g.setColour (colour::text.withAlpha (0.05f));
            g.fillRoundedRectangle (r.reduced (0.0f, 1.0f), 5.0f);
        }
        const bool on = chosen();
        const bool full = ! on && owner.selection.size() >= maxMacros;
        // The tick.
        const auto box = juce::Rectangle<float> (14.0f, 14.0f).withCentre ({ r.getX() + 13.0f, r.getCentreY() });
        g.setColour (on ? colour::text.withAlpha (0.78f) : juce::Colours::transparentBlack);
        g.fillRoundedRectangle (box, 3.0f);
        g.setColour (colour::text.withAlpha (on ? 0.78f : (full ? 0.2f : 0.38f)));
        g.drawRoundedRectangle (box, 3.0f, 1.0f);
        if (on)
        {
            juce::Path tick;
            tick.startNewSubPath (box.getX() + 3.4f, box.getCentreY() + 0.2f);
            tick.lineTo (box.getX() + 6.0f, box.getBottom() - 3.6f);
            tick.lineTo (box.getRight() - 3.2f, box.getY() + 3.6f);
            g.setColour (juce::Colour (0xfff8f4ec));
            g.strokePath (tick, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
        // Its name, and a USER macro's state.
        auto text = r.withTrimmedLeft (28.0f).withTrimmedRight (on ? 44.0f : 6.0f);
        const bool inert = entry.user && ! entry.assigned;
        g.setColour (colour::text.withAlpha (inert || full ? 0.45f : 0.85f));
        g.setFont (fonts::make (12.0f, fonts::Weight::medium, 0.03f));
        const float nameWidth = std::min (text.getWidth(), juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), entry.name) + 4.0f);
        g.drawText (entry.name, text.removeFromLeft (nameWidth), juce::Justification::centredLeft, true);
        if (entry.user)
        {
            g.setColour (colour::textMicro);
            g.setFont (fonts::make (9.5f, fonts::Weight::regular, 0.06f));
            g.drawText (inert ? juce::String ("Assign in Edit Mode") : juce::String ("USER"), text.withTrimmedLeft (8.0f), juce::Justification::centredLeft, true);
        }
        // Up / down (chosen only).
        if (on)
        {
            const int index = owner.selection.indexOf (entry.id);
            auto arrow = [&] (juce::Rectangle<float> a, bool up, bool enabled) {
                juce::Path p;
                const auto c = a.getCentre();
                if (up)
                    p.addTriangle (c.x - 4.0f, c.y + 2.0f, c.x + 4.0f, c.y + 2.0f, c.x, c.y - 2.5f);
                else
                    p.addTriangle (c.x - 4.0f, c.y - 2.0f, c.x + 4.0f, c.y - 2.0f, c.x, c.y + 2.5f);
                g.setColour (colour::text.withAlpha (enabled ? 0.6f : 0.15f));
                g.fillPath (p);
            };
            arrow (upArea(), true, index > 0);
            arrow (downArea(), false, index < owner.selection.size() - 1);
        }
        if (hasKeyboardFocus (false))
        {
            g.setColour (juce::Colour (0xff88a8af));
            g.drawRoundedRectangle (r.reduced (0.5f, 1.5f), 5.0f, 1.0f);
        }
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (! e.mouseWasClicked())
            return;
        if (chosen() && upArea().contains (e.position))
            owner.move (entry.id, -1);
        else if (chosen() && downArea().contains (e.position))
            owner.move (entry.id, 1);
        else
            owner.toggle (entry.id);
    }
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }
    bool keyPressed (const juce::KeyPress& key) override
    {
        if (key == juce::KeyPress::spaceKey || key == juce::KeyPress::returnKey)
        {
            owner.toggle (entry.id);
            return true;
        }
        if (chosen() && (key == juce::KeyPress (juce::KeyPress::upKey, juce::ModifierKeys::altModifier, 0)
                         || key == juce::KeyPress (juce::KeyPress::downKey, juce::ModifierKeys::altModifier, 0)))
        {
            owner.move (entry.id, key.getKeyCode() == juce::KeyPress::upKey ? -1 : 1);
            return true;
        }
        return false;
    }

private:
    juce::Rectangle<float> upArea() const { return { static_cast<float> (getWidth()) - 42.0f, 0.0f, 20.0f, static_cast<float> (getHeight()) }; }
    juce::Rectangle<float> downArea() const { return { static_cast<float> (getWidth()) - 21.0f, 0.0f, 20.0f, static_cast<float> (getHeight()) }; }

    MacroPicker& owner;
    PickerEntry entry;
};

MacroPicker::MacroPicker (std::vector<PickerEntry> e, juce::StringArray chosenIds, juce::StringArray defaultIds)
    : MiniPanel ("Performance Macros"), entries (std::move (e)), selection (std::move (chosenIds)), defaults (std::move (defaultIds))
{
    setTitle ("Performance Macros");
    setWantsKeyboardFocus (true);
    rebuild();
}

MacroPicker::~MacroPicker() = default;

juce::Point<int> MacroPicker::cardSize() const
{
    return { pickerWidth, popover::headerTop + static_cast<int> (entries.size()) * rowHeight + footerHeight + popover::bottom };
}

void MacroPicker::rebuild()
{
    // One row per macro, made once (a row is never deleted while it handles its own click).
    rows.clear();
    for (const auto& e : entries)
    {
        rows.push_back (std::make_unique<Row> (*this, e));
        addAndMakeVisible (*rows.back());
    }
    resized();
}

void MacroPicker::layoutContent (juce::Rectangle<int> area)
{
    // The chosen macros first, in their order; then the others (core, then USER), as offered.
    std::vector<Row*> ordered;
    for (const auto& id : selection)
        for (auto& row : rows)
            if (row->item().id == id)
                ordered.push_back (row.get());
    for (auto& row : rows)
        if (! selection.contains (row->item().id))
            ordered.push_back (row.get());
    for (auto* row : ordered)
        row->setBounds (area.removeFromTop (rowHeight));
    resetArea = area.removeFromTop (footerHeight).withTrimmedTop (6);
}

void MacroPicker::paint (juce::Graphics& g)
{
    MiniPanel::paint (g);
    using namespace design;
    const auto c = card();
    // "N of 8", right of the title.
    g.setColour (colour::textMicro);
    g.setFont (fonts::make (11.0f, fonts::Weight::regular, 0.04f));
    g.drawText (juce::String (selection.size()) + " of " + juce::String (maxMacros),
                juce::Rectangle<int> (c.getRight() - 90, c.getY() + 7, 78, 20), juce::Justification::centredRight, false);
    // A hairline over the footer, and Reset to Default.
    g.setColour (juce::Colour (0xffd8cfc1));
    g.fillRect (juce::Rectangle<int> (resetArea.getX(), resetArea.getY() - 3, resetArea.getWidth(), 1));
    g.setColour (colour::textSecondary.withAlpha (selection == defaults ? 0.4f : 0.85f));
    g.setFont (fonts::make (11.5f, fonts::Weight::medium, 0.04f));
    g.drawText ("Reset to Default", resetArea, juce::Justification::centredLeft, false);
}

void MacroPicker::mouseUp (const juce::MouseEvent& e)
{
    if (e.mouseWasClicked() && resetArea.contains (e.getPosition()))
        reset();
}

bool MacroPicker::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress::escapeKey && onClose != nullptr)
    {
        onClose();
        return true;
    }
    return false;
}

void MacroPicker::toggle (const juce::String& id)
{
    if (selection.contains (id))
    {
        if (selection.size() <= 1)
            return;   // (never none)
        selection.removeString (id);
    }
    else
    {
        if (selection.size() >= maxMacros)
            return;   // (at most eight)
        selection.add (id);
    }
    changed (false);
}

void MacroPicker::move (const juce::String& id, int delta)
{
    const int from = selection.indexOf (id);
    const int to = from + delta;
    if (from < 0 || to < 0 || to >= selection.size())
        return;
    selection.move (from, to);
    changed (false);
}

void MacroPicker::reset()
{
    selection = defaults;
    changed (true);
}

void MacroPicker::changed (bool wasReset)
{
    resized();
    repaint();
    for (auto& row : rows)
        row->repaint();
    if (onChange != nullptr)
        onChange (selection, wasReset);
}

} // namespace osp::plugin::performance
