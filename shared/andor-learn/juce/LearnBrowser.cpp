#include "LearnBrowser.h"

#include "Design.h"
#include "OspLookAndFeel.h"

namespace andor::learn
{

namespace design = osp::plugin::design;
namespace fonts = osp::plugin::fonts;
namespace type = osp::plugin::type;

namespace
{
    constexpr float pad = 20.0f;
    constexpr float headerHeight = 74.0f;   // the title and the introduction
    constexpr float sectionHeight = 28.0f, rowHeight = 50.0f, footHeight = 14.0f;
    juce::String dot() { return juce::String::fromUTF8 (" \xc2\xb7 "); }
} // namespace

Browser::Browser()
{
    setWantsKeyboardFocus (true);
    setMouseClickGrabsKeyboardFocus (false);
    setTitle ("Learn ANDOR");
    setDescription ("Tutorials");
}

juce::String Browser::statusText (const Row& row)
{
    switch (row.status)
    {
        case Row::Status::inProgress:
            return "Resume" + dot() + "Step " + juce::String (row.resumeStep + 1) + " of " + juce::String (std::max (1, row.stepCount));
        case Row::Status::complete:
            return {};   // (a check)
        case Row::Status::notStarted:
            break;
    }
    return row.minutes > 0 ? juce::String (row.minutes) + " min" : juce::String();
}

void Browser::setRows (std::vector<Row> r)
{
    items = std::move (r);
    hover = -1;
    focused = items.empty() ? -1 : juce::jlimit (-1, static_cast<int> (items.size()) - 1, focused);
    scroll = 0.0f;
    layoutLines();
    const auto size = preferredSize (maxHeight);
    setSize (size.x, size.y);
    repaint();
}

void Browser::setMaximumHeight (int h)
{
    if (h == maxHeight)
        return;
    maxHeight = h;
    const auto size = preferredSize (maxHeight);
    setSize (size.x, size.y);
    scroll = juce::jlimit (0.0f, std::max (0.0f, contentHeight() - listArea().getHeight()), scroll);
    repaint();
}

void Browser::layoutLines()
{
    // START HERE (the featured Quick Start), then LESSONS: two quiet headings, never one per row.
    lines.clear();
    float y = 0.0f;
    const float x = 0.0f, w = static_cast<float> (cardWidth) - 2.0f * (pad - 8.0f);
    auto section = [&] (const juce::String& text) {
        lines.push_back ({ -1, text, { x, y, w, sectionHeight } });
        y += sectionHeight;
    };
    bool any = false;
    for (int i = 0; i < static_cast<int> (items.size()); ++i)
        if (items[static_cast<std::size_t> (i)].featured)
        {
            if (! any)
                section ("START HERE");
            any = true;
            lines.push_back ({ i, items[static_cast<std::size_t> (i)].title, { x, y, w, rowHeight } });
            y += rowHeight;
        }
    bool lessons = false;
    for (int i = 0; i < static_cast<int> (items.size()); ++i)
        if (! items[static_cast<std::size_t> (i)].featured)
        {
            if (! lessons)
            {
                if (any)
                    y += 6.0f;
                section ("LESSONS");
            }
            lessons = true;
            lines.push_back ({ i, items[static_cast<std::size_t> (i)].title, { x, y, w, rowHeight } });
            y += rowHeight;
        }
}

float Browser::contentHeight() const
{
    return lines.empty() ? 0.0f : lines.back().area.getBottom();
}

juce::Point<int> Browser::preferredSize (int limit) const
{
    const float full = headerHeight + contentHeight() + footHeight;
    float h = full;
    if (limit > 0)
        h = std::min (h, static_cast<float> (limit - 2 * margin));
    return { cardWidth + 2 * margin, juce::roundToInt (h) + 2 * margin };
}

juce::Rectangle<float> Browser::listArea() const
{
    const auto c = card().toFloat();
    return { c.getX() + pad - 8.0f, c.getY() + headerHeight, c.getWidth() - 2.0f * (pad - 8.0f), c.getHeight() - headerHeight - footHeight };
}

juce::Rectangle<float> Browser::rowArea (const juce::String& lessonId) const
{
    const auto list = listArea();
    for (const auto& l : lines)
        if (l.row >= 0 && items[static_cast<std::size_t> (l.row)].lessonId == lessonId)
            return l.area.translated (list.getX(), list.getY() - scroll);
    return {};
}

juce::Rectangle<float> Browser::closeArea() const
{
    const auto c = card().toFloat();
    return { c.getRight() - 34.0f, c.getY() + 14.0f, 22.0f, 22.0f };
}

int Browser::rowAt (juce::Point<float> p) const
{
    const auto list = listArea();
    if (! list.contains (p))
        return -1;
    for (const auto& l : lines)
        if (l.row >= 0 && l.area.translated (list.getX(), list.getY() - scroll).contains (p))
            return l.row;
    return -1;
}

void Browser::ensureVisible (int row)
{
    const auto list = listArea();
    for (const auto& l : lines)
        if (l.row == row)
        {
            if (l.area.getY() < scroll)
                scroll = l.area.getY();
            else if (l.area.getBottom() > scroll + list.getHeight())
                scroll = l.area.getBottom() - list.getHeight();
        }
    scroll = juce::jlimit (0.0f, std::max (0.0f, contentHeight() - list.getHeight()), scroll);
}

void Browser::paint (juce::Graphics& g)
{
    const auto r = card().toFloat();
    juce::Path shape;
    shape.addRoundedRectangle (r, 16.0f);
    design::CachedShadow (juce::Colour (0x26281c10), 20, { 0, 6 }).drawForPath (g, shape);
    design::CachedShadow (juce::Colour (0x1c1e1a16), 2, { 0, 1 }).drawForPath (g, shape);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xfff8f4ec), 0.0f, r.getY(), juce::Colour (0xffeee7db), 0.0f, r.getBottom(), false));
    g.fillPath (shape);
    g.setColour (juce::Colour (0xffd3cabc));
    g.strokePath (shape, juce::PathStrokeType (1.0f));
    g.setColour (juce::Colours::white.withAlpha (0.85f));
    g.drawHorizontalLine (juce::roundToInt (r.getY() + 1.0f), r.getX() + 16.0f, r.getRight() - 16.0f);

    // The title, a short introduction, the cross.
    g.setColour (design::colour::text.withAlpha (0.88f));
    g.setFont (type::popupTitle (13.0f));
    g.drawText ("LEARN ANDOR", juce::Rectangle<float> (r.getX() + pad, r.getY() + 16.0f, r.getWidth() - 80.0f, 20.0f), juce::Justification::centredLeft, false);
    g.setColour (design::colour::textSecondary);
    g.setFont (fonts::make (12.5f, fonts::Weight::regular, 0.0f));
    g.drawText ("Learn the instrument by using it.", juce::Rectangle<float> (r.getX() + pad, r.getY() + 40.0f, r.getWidth() - 2.0f * pad, 18.0f),
                juce::Justification::centredLeft, false);
    const auto c = closeArea().reduced (6.0f);
    g.setColour (design::colour::text.withAlpha (closeHover ? 0.85f : 0.45f));
    g.drawLine (c.getX(), c.getY(), c.getRight(), c.getBottom(), 1.2f);
    g.drawLine (c.getRight(), c.getY(), c.getX(), c.getBottom(), 1.2f);

    const auto list = listArea();
    juce::Graphics::ScopedSaveState clip (g);
    g.reduceClipRegion (list.getSmallestIntegerContainer());
    for (const auto& l : lines)
    {
        const auto area = l.area.translated (list.getX(), list.getY() - scroll);
        if (! area.intersects (list))
            continue;
        if (l.row < 0)
        {
            g.setColour (design::colour::textMicro);
            g.setFont (type::popupTitle (9.5f));
            g.drawText (l.text, area.withTrimmedLeft (8.0f).withTrimmedTop (8.0f), juce::Justification::centredLeft, false);
            continue;
        }
        const auto& item = items[static_cast<std::size_t> (l.row)];
        const auto box = area.reduced (0.0f, 2.0f);
        const bool selected = l.row == focused && hasKeyboardFocus (false);
        if (l.row == hover || selected)
        {
            g.setColour (juce::Colours::white.withAlpha (l.row == hover ? 0.62f : 0.5f));
            g.fillRoundedRectangle (box, 9.0f);
        }
        if (selected)
        {
            // The keyboard's place: a visible ring (focus is never shown by colour alone).
            g.setColour (design::colour::accent.withAlpha (0.75f));
            g.drawRoundedRectangle (box.reduced (0.5f), 9.0f, 1.2f);
        }
        auto text = box.reduced (10.0f, 6.0f);
        // Right: the time, Resume with its step, or a check.
        const auto status = statusText (item);
        auto right = text.removeFromRight (item.status == Row::Status::inProgress ? 124.0f : 56.0f);
        if (item.status == Row::Status::complete)
        {
            const auto mark = juce::Rectangle<float> (right.getRight() - 13.0f, right.getCentreY() - 6.5f, 13.0f, 13.0f);
            juce::Path tick;
            tick.startNewSubPath (mark.getX() + 1.5f, mark.getCentreY());
            tick.lineTo (mark.getX() + 5.0f, mark.getBottom() - 2.0f);
            tick.lineTo (mark.getRight() - 1.0f, mark.getY() + 2.0f);
            g.setColour (juce::Colour (0xff2fbf6c).darker (0.25f));
            g.strokePath (tick, juce::PathStrokeType (1.9f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
        else if (status.isNotEmpty())
        {
            g.setColour (item.status == Row::Status::inProgress ? design::colour::accent : design::colour::textMicro);
            g.setFont (type::popupLabel (10.5f));
            g.drawText (status, right, juce::Justification::centredRight, false);
        }
        const auto top = text.removeFromTop (text.getHeight() * 0.5f);
        g.setColour (design::colour::text);
        g.setFont (fonts::make (13.5f, fonts::Weight::medium, 0.0f));
        g.drawText (item.title, top, juce::Justification::bottomLeft, true);
        g.setColour (design::colour::textSecondary);
        g.setFont (fonts::make (11.5f, fonts::Weight::regular, 0.0f));
        g.drawText (item.description, text.withTrimmedTop (2.0f), juce::Justification::topLeft, true);
    }
    // More below (or above): a soft fade at the edge says so.
    if (contentHeight() > list.getHeight() + 0.5f)
    {
        const auto base = juce::Colour (0xffeee7db);
        if (scroll < contentHeight() - list.getHeight() - 0.5f)
        {
            const auto fade = list.withTop (list.getBottom() - 18.0f);
            g.setGradientFill (juce::ColourGradient (base.withAlpha (0.0f), 0.0f, fade.getY(), base, 0.0f, fade.getBottom(), false));
            g.fillRect (fade);
        }
        if (scroll > 0.5f)
        {
            const auto fade = list.withHeight (14.0f);
            g.setGradientFill (juce::ColourGradient (juce::Colour (0xfff5f0e6), 0.0f, fade.getY(), juce::Colour (0x00f5f0e6), 0.0f, fade.getBottom(), false));
            g.fillRect (fade);
        }
    }
}

void Browser::mouseMove (const juce::MouseEvent& e)
{
    const int row = rowAt (e.position);
    const bool overClose = closeArea().contains (e.position);
    if (row != hover || overClose != closeHover)
    {
        hover = row;
        closeHover = overClose;
        setMouseCursor (row >= 0 || overClose ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
        repaint();
    }
}

void Browser::mouseExit (const juce::MouseEvent&)
{
    hover = -1;
    closeHover = false;
    repaint();
}

void Browser::choose (int row)
{
    if (row < 0 || row >= static_cast<int> (items.size()))
        return;
    // A copy, after this event returns: choosing closes (deletes) the browser.
    const auto id = items[static_cast<std::size_t> (row)].lessonId;
    juce::Component::SafePointer<Browser> safe (this);
    juce::MessageManager::callAsync ([safe, id] {
        if (safe != nullptr && safe->onChoose != nullptr)
            safe->onChoose (id);
    });
}

void Browser::mouseUp (const juce::MouseEvent& e)
{
    if (closeArea().contains (e.position))
    {
        juce::Component::SafePointer<Browser> safe (this);
        juce::MessageManager::callAsync ([safe] {
            if (safe != nullptr && safe->onClose != nullptr)
                safe->onClose();
        });
        return;
    }
    choose (rowAt (e.position));
}

void Browser::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& wheel)
{
    const float room = std::max (0.0f, contentHeight() - listArea().getHeight());
    if (room <= 0.0f)
        return;
    scroll = juce::jlimit (0.0f, room, scroll - (wheel.isReversed ? -wheel.deltaY : wheel.deltaY) * 120.0f);
    repaint();
}

bool Browser::keyPressed (const juce::KeyPress& key)
{
    const int n = static_cast<int> (items.size());
    if (n == 0)
        return false;
    if (key == juce::KeyPress::downKey || key == juce::KeyPress::upKey)
    {
        // In the order shown (the featured row first).
        std::vector<int> order;
        for (const auto& l : lines)
            if (l.row >= 0)
                order.push_back (l.row);
        const auto at = std::find (order.begin(), order.end(), focused);
        int i = at == order.end() ? -1 : static_cast<int> (at - order.begin());
        i = juce::jlimit (0, static_cast<int> (order.size()) - 1, i < 0 ? 0 : i + (key == juce::KeyPress::downKey ? 1 : -1));
        focused = order[static_cast<std::size_t> (i)];
        ensureVisible (focused);
        repaint();
        return true;
    }
    if ((key == juce::KeyPress::returnKey || key == juce::KeyPress::spaceKey) && focused >= 0)
    {
        choose (focused);
        return true;
    }
    return false;
}

} // namespace andor::learn
