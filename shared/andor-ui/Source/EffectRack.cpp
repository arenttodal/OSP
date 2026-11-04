#include "EffectRack.h"
#include "OspLookAndFeel.h"

namespace osp::plugin::rack
{

using namespace osp::plugin;

namespace
{
    void chevron (juce::Graphics& g, juce::Point<float> c, float w, float h, bool up, juce::Colour colour, float thickness)
    {
        juce::Path p;
        const float dy = up ? -0.5f * h : 0.5f * h;
        p.startNewSubPath (c.x - w, c.y - dy);
        p.lineTo (c.x, c.y + dy);
        p.lineTo (c.x + w, c.y - dy);
        g.setColour (colour);
        g.strokePath (p, juce::PathStrokeType (thickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
}

juce::String name (Kind kind)
{
    switch (kind)
    {
        case Kind::movement: return "MOVEMENT";
        case Kind::echo: return "ECHO";
        case Kind::space: return "SPACE";
    }
    return {};
}

std::optional<Kind> kindForMacro (const juce::String& parameterId)
{
    if (parameterId == "motion")
        return Kind::movement;
    if (parameterId == "echo")
        return Kind::echo;
    if (parameterId == "space")
        return Kind::space;
    return std::nullopt;
}

//==============================================================================
ExpandButton::ExpandButton (Kind kind) : juce::Button (name (kind) + " rack"), which (kind)
{
    setWantsKeyboardFocus (true);
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
    setExpanded (false);
}

void ExpandButton::setExpanded (bool expanded)
{
    open = expanded;
    setTooltip (open ? "Collapse the " + name (which) + " rack (the settings stay as they are)"
                     : "Expand " + name (which) + " into the rack: all its settings and a larger picture");
    setToggleState (open, juce::dontSendNotification);
    repaint();
}

void ExpandButton::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    using namespace design;
    // A small key in the macro row's quiet language: two rack rails over a chevron (down to
    // expand, up when open); lit in the macro's identity while its module is open.
    const auto r = getLocalBounds().toFloat().reduced (1.5f);
    draw::button (g, r, 4.0f, open, highlighted || down || hasKeyboardFocus (true), accent);
    const auto ink = open ? accent.darker (0.35f) : colour::text.withAlpha (highlighted ? 0.95f : 0.7f);
    const float cx = r.getCentreX(), w = r.getWidth() * 0.28f;
    g.setColour (ink);
    g.fillRoundedRectangle (juce::Rectangle<float> (cx - w, r.getY() + r.getHeight() * 0.27f, 2.0f * w, 1.4f), 0.7f);
    g.fillRoundedRectangle (juce::Rectangle<float> (cx - w, r.getY() + r.getHeight() * 0.42f, 2.0f * w, 1.4f), 0.7f);
    chevron (g, { cx, r.getY() + r.getHeight() * 0.68f }, w * 0.8f, r.getHeight() * 0.16f, open, ink, 1.4f);
}

//==============================================================================
/** The module's collapse key: a small round button with an up chevron (the arpeggiator's). */
class Host::CollapseButton final : public juce::Button
{
public:
    explicit CollapseButton (Kind kind) : juce::Button ("Collapse " + name (kind))
    {
        setWantsKeyboardFocus (true);
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
        setTooltip ("Collapse " + name (kind) + " (the settings stay as they are)");
    }
    void paintButton (juce::Graphics& g, bool highlighted, bool down) override
    {
        using namespace design;
        const auto c = getLocalBounds().toFloat().reduced (1.0f);
        g.setColour (juce::Colour (0x1a302418));
        g.fillEllipse (c.translated (0.0f, 1.0f));
        g.setGradientFill (juce::ColourGradient (colour::buttonTop, 0.0f, c.getY(), colour::buttonBottom, 0.0f, c.getBottom(), false));
        g.fillEllipse (c);
        const bool lit = highlighted || down || hasKeyboardFocus (true);
        g.setColour (lit ? colour::text.withAlpha (0.45f) : colour::hairline.darker (0.12f));
        g.drawEllipse (c.reduced (0.5f), 1.0f);
        chevron (g, c.getCentre(), 5.0f, 3.2f, true, colour::text.withAlpha (lit ? 1.0f : 0.8f), 1.6f);
    }
};

Host::Host (Factory f) : factory (std::move (f))
{
    setOpaque (false);
}

Host::~Host() = default;

void Host::setShown (Kind kind, bool shown)
{
    if (shown == isShown (kind))
        return;
    if (! shown)
    {
        slots.erase (std::remove_if (slots.begin(), slots.end(), [kind] (const Slot& s) { return s.kind == kind; }), slots.end());
        changed();
        return;
    }
    auto made = factory != nullptr ? factory (kind) : nullptr;
    if (made == nullptr)
        return;
    if (! stacking)
        slots.clear();
    Slot slot;
    slot.kind = kind;
    slot.module = std::move (made);
    slot.module->onHeightChanged = [this] { changed(); };
    slot.collapse = std::make_unique<CollapseButton> (kind);
    slot.collapse->onClick = [this, kind] { setShown (kind, false); };
    addAndMakeVisible (*slot.module);
    addAndMakeVisible (*slot.collapse);
    slots.push_back (std::move (slot));
    changed();
}

bool Host::isShown (Kind kind) const noexcept
{
    return std::any_of (slots.begin(), slots.end(), [kind] (const Slot& s) { return s.kind == kind; });
}

std::vector<Kind> Host::shownKinds() const
{
    std::vector<Kind> kinds;
    for (const auto& s : slots)
        kinds.push_back (s.kind);
    return kinds;
}

Module* Host::module (Kind kind) const noexcept
{
    for (const auto& s : slots)
        if (s.kind == kind)
            return s.module.get();
    return nullptr;
}

float Host::height() const noexcept
{
    if (slots.empty())
        return 0.0f;
    int h = 0;
    for (const auto& s : slots)
        h += titleHeight + s.module->bodyHeight();
    h += bottomMargin + divider * (static_cast<int> (slots.size()) - 1);
    return static_cast<float> (h);
}

void Host::changed()
{
    setVisible (! slots.empty());
    resized();
    repaint();
    if (onLayoutChanged != nullptr)
        onLayoutChanged();
}

void Host::refresh()
{
    for (auto& s : slots)
        s.module->refresh();
}

void Host::settle()
{
    for (auto& s : slots)
        s.module->settle();
}

void Host::resized()
{
    int y = 0;
    for (std::size_t i = 0; i < slots.size(); ++i)
    {
        auto& s = slots[i];
        if (i > 0)
            y += divider;
        auto title = juce::Rectangle<int> (0, y, getWidth(), titleHeight).reduced (22, 0);
        s.collapse->setBounds (title.removeFromRight (26).withSizeKeepingCentre (26, 26).translated (0, 2));
        s.titleArea = title.removeFromLeft (160);
        title.removeFromRight (12);
        s.module->layoutTitleRow (title.translated (0, 2).withTrimmedLeft (8));
        y += titleHeight;
        s.module->setBounds (0, y, getWidth(), s.module->bodyHeight());
        y += s.module->bodyHeight();
    }
}

void Host::paint (juce::Graphics& g)
{
    using namespace design;
    const auto r = getLocalBounds().toFloat();
    if (! skin::drawnBySurface())   // ceramic: the housing draws the panel (its shadow falls there)
        draw::raised (g, r, layout::panelRadius, colour::panelTop, colour::panelBottom);
    for (std::size_t i = 0; i < slots.size(); ++i)
    {
        const auto& s = slots[i];
        if (i > 0)
        {
            g.setColour (colour::divider);
            g.fillRect (juce::Rectangle<float> (22.0f, static_cast<float> (s.titleArea.getY()) - 0.5f * divider - 0.5f, r.getWidth() - 44.0f, 1.0f));
        }
        // The module's name in the panels' header type, a small light in its identity.
        const auto title = s.titleArea.toFloat().translated (0.0f, 2.0f);
        draw::led (g, { title.getX() + 4.5f, title.getCentreY() }, 8.0f, s.module->accent(), 0.5f);
        g.setColour (colour::text.withAlpha (0.9f));
        g.setFont (type::panelHeader());
        g.drawText (s.module->title(), title.withTrimmedLeft (16.0f), juce::Justification::centredLeft, false);
    }
}

} // namespace osp::plugin::rack
