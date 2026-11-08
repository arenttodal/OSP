#include "ParameterRack.h"
#include "OspLookAndFeel.h"

namespace osp::plugin::rack
{

//==============================================================================
void GraphFrame::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat();
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff1c2528), 0.0f, r.getY(), juce::Colour (0xff192124), 0.0f, r.getBottom(), false));
    g.fillRoundedRectangle (r, 5.0f);
    {
        juce::Graphics::ScopedSaveState state (g);
        g.reduceClipRegion (getLocalBounds().reduced (1));
        paintGraph (g, r.reduced (1.0f));
    }
    g.setColour (juce::Colour (0xff343f43));
    g.drawRoundedRectangle (r.reduced (0.5f), 5.0f, 1.0f);
}

//==============================================================================
/** Page words: low-profile, the shown one in the full text colour and underlined in the
    module's colour (the modulation panel's tabs' manner); sub-pages a step smaller. */
class ParameterRack::PageTabs final : public juce::Component
{
public:
    PageTabs (juce::StringArray n, juce::Colour a, bool small, std::function<int()> get, std::function<void (int)> set)
        : names (std::move (n)), accent (a), secondary (small), shownIndex (std::move (get)), choose (std::move (set))
    {
        setWantsKeyboardFocus (true);
        setMouseClickGrabsKeyboardFocus (false);
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    }
    juce::Font font() const { return fonts::make (secondary ? 10.0f : 11.0f, fonts::Weight::medium, 0.06f); }
    float gap() const { return secondary ? 16.0f : 22.0f; }
    int preferredWidth() const
    {
        float w = 0.0f;
        for (const auto& n : names)
            w += juce::GlyphArrangement::getStringWidth (font(), n) + gap();
        return juce::roundToInt (w - gap() + 2.0f);
    }
    juce::Rectangle<float> tab (int i) const
    {
        float x = 0.0f;
        for (int k = 0; k < i; ++k)
            x += juce::GlyphArrangement::getStringWidth (font(), names[k]) + gap();
        return { x, 0.0f, juce::GlyphArrangement::getStringWidth (font(), names[i]) + 2.0f, static_cast<float> (getHeight()) };
    }
    const juce::StringArray& words() const noexcept { return names; }
    void paint (juce::Graphics& g) override
    {
        g.setFont (font());
        const int shown = shownIndex();
        for (int i = 0; i < names.size(); ++i)
        {
            const auto t = tab (i);
            const bool on = i == shown;
            g.setColour (on ? juce::Colour (0xff3e3d3a) : juce::Colour (0xff8a847b).withAlpha (i == hover ? 1.0f : 0.9f));
            g.drawText (names[i], t, juce::Justification::centredLeft, false);
            if (on)
            {
                g.setColour (accent);
                g.fillRoundedRectangle (t.withTrimmedRight (2.0f).withY (t.getCentreY() + 0.5f * font().getHeight() + 2.0f).withHeight (secondary ? 1.5f : 2.0f), 1.0f);
            }
        }
        if (hasKeyboardFocus (false))
        {
            g.setColour (rackui::palette::focus);
            g.drawRoundedRectangle (getLocalBounds().toFloat().expanded (2.0f), 4.0f, 2.0f);
        }
    }
    void mouseDown (const juce::MouseEvent& e) override
    {
        for (int i = 0; i < names.size(); ++i)
            if (tab (i).expanded (0.5f * gap(), 0.0f).contains (e.position))
                choose (i);
    }
    void mouseMove (const juce::MouseEvent& e) override
    {
        int now = -1;
        for (int i = 0; i < names.size(); ++i)
            if (tab (i).contains (e.position))
                now = i;
        if (now != hover)
        {
            hover = now;
            repaint();
        }
    }
    void mouseExit (const juce::MouseEvent&) override
    {
        hover = -1;
        repaint();
    }
    bool keyPressed (const juce::KeyPress& key) override
    {
        if (key == juce::KeyPress::leftKey)
            choose (std::max (0, shownIndex() - 1));
        else if (key == juce::KeyPress::rightKey)
            choose (std::min (names.size() - 1, shownIndex() + 1));
        else
            return false;
        return true;
    }
    void focusGained (FocusChangeType) override { repaint(); }
    void focusLost (FocusChangeType) override { repaint(); }

private:
    juce::StringArray names;
    juce::Colour accent;
    bool secondary;
    std::function<int()> shownIndex;
    std::function<void (int)> choose;
    int hover = -1;
};

//==============================================================================
ParameterRack::ParameterRack (InstrumentHost& h, juce::String t, juce::Colour a, float leftShare)
    : host (h), heading (std::move (t)), accentColour (a), share (leftShare)
{
}

ParameterRack::~ParameterRack() = default;

void ParameterRack::setPages (juce::StringArray names, int* memory)
{
    pageMemory = memory;
    currentPage = memory != nullptr ? juce::jlimit (0, names.size() - 1, *memory) : 0;
    pageTabs = std::make_unique<PageTabs> (std::move (names), accentColour, false, [this] { return currentPage; }, [this] (int p) { showPage (p); });
    pageTabs->setTitle ("Pages");
    addAndMakeVisible (*pageTabs);
}

void ParameterRack::setSubPages (int page, juce::StringArray names, int* memory)
{
    auto tabs = std::make_unique<PageTabs> (std::move (names), accentColour, true, [this] { return subPage(); }, [this] (int s) { showSubPage (s); });
    tabs->setTitle ("Sub-pages");
    addChildComponent (*tabs);
    subTabs[page] = { std::move (tabs), memory };
}

int ParameterRack::subPage() const noexcept
{
    const auto it = subTabs.find (currentPage);
    if (it == subTabs.end() || it->second.second == nullptr)
        return 0;
    return juce::jlimit (0, it->second.first->words().size() - 1, *it->second.second);
}

void ParameterRack::showPage (int p)
{
    if (p == currentPage)
        return;
    currentPage = p;
    if (pageMemory != nullptr)
        *pageMemory = p;
    rebuild();
}

void ParameterRack::showSubPage (int s)
{
    const auto it = subTabs.find (currentPage);
    if (it == subTabs.end() || it->second.second == nullptr || *it->second.second == s)
        return;
    *it->second.second = s;
    rebuild();
}

void ParameterRack::setHeaderSwitch (const juce::String& id, const juce::String& word, const juce::String& tip)
{
    if (auto* p = host.apvts().getParameter (id))
    {
        power = std::make_unique<rackui::VerticalSwitch> (*p, std::vector<rackui::Position> { { "ON", "On", 1.0f }, { "OFF", "Off", 0.0f } }, &host.undo(),
                                                          rackui::VerticalSwitch::Style::mini);
        power->setTitle (heading + " on");
        power->setTooltip (tip);
        power->onChange = [this] (int) { repaint(); };
        powerWord = word;
        addAndMakeVisible (*power);
    }
}

rackui::SegmentedSwitch& ParameterRack::selectorSegment (const juce::String& id, const juce::String& caption, std::vector<rackui::Position> positions, int width,
                                                          const juce::String& tip)
{
    auto seg = std::make_unique<rackui::SegmentedSwitch> (*host.apvts().getParameter (id), std::move (positions), &host.undo(), accentColour);
    seg->setTooltip (tip);
    seg->setTitle (caption);
    seg->setSize (width, 27);
    auto& ref = *seg;
    addAndMakeVisible (ref);
    selectors.emplace_back (caption, std::move (seg));
    return ref;
}

ValueSelector& ParameterRack::selectorList (const juce::String& id, const juce::String& caption, int width, const juce::String& tip)
{
    auto list = std::make_unique<ValueSelector> (*host.apvts().getParameter (id), juce::String());
    list->setRackStyle (ValueSelector::RackStyle::option);
    list->setTooltip (tip);
    list->setTitle (caption);
    list->setSize (width, 27);
    auto& ref = *list;
    addAndMakeVisible (ref);
    selectors.emplace_back (caption, std::move (list));
    return ref;
}

void ParameterRack::setGraph (std::unique_ptr<GraphFrame> g)
{
    if (graphView != nullptr)
        removeChildComponent (graphView.get());
    graphView = std::move (g);
    addAndMakeVisible (*graphView);
}

void ParameterRack::group (const juce::String& caption)
{
    groups.push_back ({ caption, {} });
}

void ParameterRack::add (std::unique_ptr<juce::Component> cell, rackui::Slot slot)
{
    addAndMakeVisible (*cell);
    if (groups.empty())
        groups.push_back ({ {}, {} });
    cells.push_back ({ std::move (cell), static_cast<int> (groups.size()) - 1, slot });
}

MiniKnob& ParameterRack::knob (const juce::String& id, const juce::String& caption, MiniKnob::Formatter f)
{
    auto k = std::make_unique<MiniKnob> (host.apvts(), id, caption, std::move (f));
    k->setArcColour (accentColour);
    k->setRackMetrics (true);
    auto& ref = *k;
    add (std::move (k), rackui::Slot::knob);
    return ref;
}

MiniKnob& ParameterRack::steppedKnob (const juce::String& id, const juce::String& caption, MiniKnob::Formatter f)
{
    auto& k = knob (id, caption, std::move (f));
    k.setDetents (true, accentColour);
    return k;
}

ValueSelector& ParameterRack::listCell (const juce::String& id, const juce::String& caption, const juce::String& tip)
{
    auto selector = std::make_unique<ValueSelector> (*host.apvts().getParameter (id), juce::String());
    selector->setTooltip (tip);
    selector->setRackStyle (ValueSelector::RackStyle::option);
    auto& ref = *selector;
    add (std::make_unique<rackui::Cell> (caption, std::move (selector), rackui::Cell::Kind::list), rackui::Slot::knob);
    return ref;
}

rackui::SegmentedSwitch& ParameterRack::segmentCell (const juce::String& id, const juce::String& caption, std::vector<rackui::Position> positions, const juce::String& tip,
                                                     rackui::Slot slot)
{
    auto seg = std::make_unique<rackui::SegmentedSwitch> (*host.apvts().getParameter (id), std::move (positions), &host.undo(), accentColour);
    seg->setTooltip (tip);
    seg->setTitle (caption);
    auto& ref = *seg;
    add (std::make_unique<rackui::Cell> (caption, std::move (seg), rackui::Cell::Kind::segments), slot);
    return ref;
}

rackui::VerticalSwitch& ParameterRack::switchCell (const juce::String& id, const juce::String& caption, std::vector<rackui::Position> positions, const juce::String& tip)
{
    const bool three = positions.size() == 3;
    auto key = std::make_unique<rackui::VerticalSwitch> (*host.apvts().getParameter (id), std::move (positions), &host.undo());
    key->setTooltip (tip);
    key->setTitle (caption);
    auto& ref = *key;
    add (std::make_unique<rackui::Cell> (caption, std::move (key), three ? rackui::Cell::Kind::switch3 : rackui::Cell::Kind::switch2),
         three ? rackui::Slot::wide : rackui::Slot::slim);
    return ref;
}

int ParameterRack::currentKey() const
{
    return (currentPage * 16 + subPage()) * 1000 + buildKey();
}

void ParameterRack::rebuild()
{
    for (auto& c : cells)
        removeChildComponent (c.cell.get());
    cells.clear();
    for (auto& s : selectors)
        removeChildComponent (s.second.get());
    selectors.clear();
    groups.clear();
    builtKey = currentKey();
    build (currentPage);
    for (auto& [page, tabs] : subTabs)
        tabs.first->setVisible (page == currentPage);
    layout();
    repaint();
}

void ParameterRack::refresh()
{
    if (currentKey() != builtKey)
        rebuild();
    if (graphView != nullptr)
        graphView->refresh();
    if (headerStatus != nullptr)
        repaint (statusArea);
}

void ParameterRack::settle()
{
    refresh();
    if (graphView != nullptr)
        graphView->repaint();
}

void ParameterRack::resized()
{
    if (builtKey < 0)
        rebuild();
    else
        layout();
}

void ParameterRack::layoutTitleRow (juce::Rectangle<int> row)
{
    // ... [status]  ON (switch)  (collapse): as MOVEMENT's lane power.
    row = row.withSizeKeepingCentre (row.getWidth(), 30);
    if (power != nullptr)
    {
        power->setBounds (row.removeFromRight (16).withSizeKeepingCentre (16, 26));
        row.removeFromRight (7);
        powerWordArea = row.removeFromRight (14);
        row.removeFromRight (12);
    }
    statusArea = row.withTrimmedLeft (4);
}

int ParameterRack::dividerX() const
{
    return rackui::Cell::contentInset + juce::roundToInt (share * static_cast<float> (getWidth() - 2 * rackui::Cell::contentInset)) + rackui::Cell::columnGap;
}

void ParameterRack::layout()
{
    const int width = getWidth(), x0 = rackui::Cell::contentInset, top = titleOffset();
    const int divider = dividerX();
    // The selector row: the pages, the shown page's sub-pages, its selectors (caption before).
    int x = x0;
    const int fieldY = top + (selectRow - 27) / 2;
    if (pageTabs != nullptr)
    {
        pageTabs->setBounds (x, fieldY, pageTabs->preferredWidth(), 27);
        x = pageTabs->getRight() + 22;
    }
    if (const auto it = subTabs.find (currentPage); it != subTabs.end())
    {
        it->second.first->setBounds (x, fieldY, it->second.first->preferredWidth(), 27);
        x = it->second.first->getRight() + 22;
    }
    const auto captionFont = fonts::make (9.0f, fonts::Weight::medium, 0.06f);
    for (auto& [caption, control] : selectors)
    {
        if (caption.isNotEmpty())
            x += juce::roundToInt (juce::GlyphArrangement::getStringWidth (captionFont, caption)) + 7;
        control->setTopLeftPosition (x, fieldY);
        x = control->getRight() + 16;
    }
    jassert (x - 16 <= divider - rackui::Cell::columnGap);
    // One row of cells on the standard lines; one slot family for the page.
    const bool dense = static_cast<int> (cells.size()) >= rackui::denseFrom;
    for (auto& g : groups)
        g.span = {};
    const int y = top + selectRow + rowGap;
    int cx = x0, lastGroup = -1;
    for (auto& p : cells)
    {
        if (lastGroup >= 0 && p.group != lastGroup)
            cx += 20 - rackui::slotGap (dense);   // a group's gap (its divider in the middle)
        const int w = rackui::slotWidth (p.slot, dense);
        p.cell->setBounds (cx, y, w, rackui::cellHeight);
        auto& span = groups[static_cast<std::size_t> (p.group)].span;
        span = span.isEmpty() ? p.cell->getBounds() : span.getUnion (p.cell->getBounds());
        cx += w + rackui::slotGap (dense);
        lastGroup = p.group;
    }
    jassert (cx - rackui::slotGap (dense) <= divider - rackui::Cell::columnGap);
    if (graphView != nullptr)
    {
        const int pictureX = divider + 1 + rackui::Cell::columnGap;
        graphView->setBounds (pictureX, top + 4, width - x0 - pictureX, graphHeight);
    }
}

void ParameterRack::paint (juce::Graphics& g)
{
    // The divider (as the effects racks').
    g.setColour (juce::Colour (0xffcbc4b9));
    g.fillRect (juce::Rectangle<float> (static_cast<float> (dividerX()), static_cast<float> (titleOffset() + 3), 1.0f, static_cast<float> (getHeight() - titleOffset() - 5)));
    // The selectors' captions (inline, before each).
    g.setFont (fonts::make (9.0f, fonts::Weight::medium, 0.06f));
    g.setColour (rackui::palette::caption);
    for (auto& [caption, control] : selectors)
        if (caption.isNotEmpty())
            g.drawText (caption, juce::Rectangle<int> (control->getX() - 200, control->getY(), 193, control->getHeight()), juce::Justification::centredRight, false);
    // The groups: a quiet divider between them; a caption (if any) small in the band over the cells.
    for (std::size_t i = 0; i < groups.size(); ++i)
    {
        const auto& gr = groups[i];
        if (gr.span.isEmpty())
            continue;
        if (gr.caption.isNotEmpty())
        {
            g.setColour (juce::Colour (0xff8c857b));
            g.setFont (fonts::make (8.5f, fonts::Weight::medium, 0.08f));
            g.drawText (gr.caption, juce::Rectangle<int> (gr.span.getX() + 4, gr.span.getY() - 11, gr.span.getWidth(), 10), juce::Justification::centredLeft, false);
        }
        if (i > 0 && ! groups[i - 1].span.isEmpty())
        {
            const float x = 0.5f * static_cast<float> (groups[i - 1].span.getRight() + gr.span.getX());
            g.setColour (juce::Colour (0xffddd5c9));
            g.fillRect (juce::Rectangle<float> (x, static_cast<float> (gr.span.getY() + 4), 1.0f, static_cast<float> (gr.span.getHeight() - 16)));
        }
    }
    if (power != nullptr)
    {
        g.setColour (juce::Colour (0xff777069));
        g.setFont (fonts::make (9.0f, fonts::Weight::regular, 0.03f));
        g.drawText (powerWord, powerWordArea, juce::Justification::centred, false);
    }
    if (headerStatus != nullptr)
        if (const auto s = headerStatus(); s.isNotEmpty())
        {
            g.setColour (juce::Colour (0xff8a847b));
            g.setFont (fonts::make (10.0f, fonts::Weight::regular, 0.02f));
            g.drawText (s, statusArea, juce::Justification::centredLeft, true);
        }
}

juce::String ParameterRack::captions() const
{
    juce::StringArray c;
    for (const auto& p : cells)
    {
        if (auto* k = dynamic_cast<MiniKnob*> (p.cell.get()))
            c.add (k->captionText());
        else if (auto* cell = dynamic_cast<rackui::Cell*> (p.cell.get()))
            c.add (cell->captionText());
    }
    return c.joinIntoString (" ");
}

std::vector<juce::Rectangle<int>> ParameterRack::cellBounds() const
{
    std::vector<juce::Rectangle<int>> b;
    for (const auto& p : cells)
        b.push_back (p.cell->getBounds());
    return b;
}

juce::Component* ParameterRack::cellNamed (const juce::String& caption) const
{
    for (const auto& p : cells)
    {
        if (auto* k = dynamic_cast<MiniKnob*> (p.cell.get()); k != nullptr && k->captionText() == caption)
            return k;
        if (auto* c = dynamic_cast<rackui::Cell*> (p.cell.get()); c != nullptr && c->captionText() == caption)
            return &c->key();
    }
    for (const auto& [name, control] : selectors)
        if (name == caption)
            return control.get();
    return nullptr;
}

juce::Rectangle<int> ParameterRack::graphBounds() const
{
    return graphView != nullptr ? graphView->getBounds() : juce::Rectangle<int>();
}

} // namespace osp::plugin::rack
