#include "ParameterRack.h"
#include "OspLookAndFeel.h"

namespace osp::plugin::rack
{

//==============================================================================
void GraphFrame::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat();
    g.setGradientFill (juce::ColourGradient (glassTop, 0.0f, r.getY(), glassBottom, 0.0f, r.getBottom(), false));
    g.fillRoundedRectangle (r, 5.0f);
    {
        juce::Graphics::ScopedSaveState state (g);
        g.reduceClipRegion (getLocalBounds().reduced (1));
        paintGraph (g, r.reduced (1.0f));
    }
    g.setColour (glassEdge);
    g.drawRoundedRectangle (r.reduced (0.5f), 5.0f, 1.0f);
}

//==============================================================================
/** Page words: low-profile, the shown one in the full text colour and underlined in the
    module's colour (the modulation panel's tabs' manner). Sub-pages are a rank below, and look
    it: a step smaller, lighter in both states, a hairline underline in the same place. */
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
            const auto shownColour = secondary ? rackui::palette::value : juce::Colour (0xff3e3d3a);
            const auto restColour = secondary ? rackui::palette::quietCaption : juce::Colour (0xff8a847b);
            g.setColour (on ? shownColour : (i == hover ? restColour.darker (0.25f) : restColour));
            g.drawText (names[i], t, juce::Justification::centredLeft, false);
            if (on)
            {
                g.setColour (secondary ? accent.withAlpha (0.8f) : accent);
                g.fillRoundedRectangle (t.withTrimmedRight (2.0f).withY (t.getCentreY() + 0.5f * font().getHeight() + 2.0f).withHeight (secondary ? 1.0f : 2.0f), secondary ? 0.5f : 1.0f);
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

namespace
{
    /** The faceplate's overflow key (three dots on the rail). */
    class OverflowKey final : public juce::Component, public juce::SettableTooltipClient
    {
    public:
        explicit OverflowKey (std::function<void (juce::Component&)> f) : action (std::move (f))
        {
            setMouseCursor (juce::MouseCursor::PointingHandCursor);
            setWantsKeyboardFocus (true);
            setMouseClickGrabsKeyboardFocus (false);
            setTitle ("More");
            setTooltip ("Presets and more");
        }
        void paint (juce::Graphics& g) override
        {
            const auto c = getLocalBounds().toFloat().getCentre();
            g.setColour (juce::Colour (0xfff3eadb).withAlpha (hover || hasKeyboardFocus (false) ? 1.0f : 0.78f));
            for (int i = -1; i <= 1; ++i)
                g.fillEllipse (juce::Rectangle<float> (3.0f, 3.0f).withCentre ({ c.x + 5.5f * static_cast<float> (i), c.y }));
        }
        void mouseEnter (const juce::MouseEvent&) override { hover = true; repaint(); }
        void mouseExit (const juce::MouseEvent&) override { hover = false; repaint(); }
        void mouseUp (const juce::MouseEvent& e) override
        {
            if (getLocalBounds().contains (e.getPosition()) && action != nullptr)
                action (*this);
        }
        bool keyPressed (const juce::KeyPress& k) override
        {
            if ((k == juce::KeyPress::spaceKey || k == juce::KeyPress::returnKey) && action != nullptr)
            {
                action (*this);
                return true;
            }
            return false;
        }
        std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override
        {
            return std::make_unique<juce::AccessibilityHandler> (*this, juce::AccessibilityRole::button,
                                                                  juce::AccessibilityActions().addAction (juce::AccessibilityActionType::press, [this] {
                                                                      if (action != nullptr)
                                                                          action (*this);
                                                                  }));
        }

    private:
        std::function<void (juce::Component&)> action;
        bool hover = false;
    };

    // The faceplate's measures (reference pixels, module coordinates).
    constexpr int chassisInsetX = 12, chassisTop = 3, chassisBottomGap = 3, motifWidth = 38, motifGap = 14;
    constexpr float chassisRadius = 7.0f;
}

void ParameterRack::setFaceplate (const Faceplate& f)
{
    faceplate = f;
    if (power != nullptr && powerId.isNotEmpty())
    {
        // The header switch becomes the power key (the same parameter).
        removeChildComponent (power.get());
        power.reset();
        if (auto* p = host.apvts().getParameter (powerId))
        {
            powerKeyPart = std::make_unique<rackui::PowerKey> (*p, &host.undo(), f.glow);
            powerKeyPart->onChange = [this] (bool) { repaint(); };
            addAndMakeVisible (*powerKeyPart);
        }
    }
    if (graphView != nullptr)
        graphView->setGlass (f.glassTop, f.glassBottom, f.glassTop.brighter (0.25f));
    if (getParentComponent() != nullptr)
        getParentComponent()->resized();
    layout();
    repaint();
}

void ParameterRack::setRailComponent (std::unique_ptr<juce::Component> component, int width)
{
    if (railPart != nullptr)
        removeChildComponent (railPart.get());
    railPart = std::move (component);
    railPartWidth = width;
    if (railPart != nullptr)
        addAndMakeVisible (*railPart);
}

void ParameterRack::setOverflow (std::function<void (juce::Component&)> onClick)
{
    if (overflow != nullptr)
        removeChildComponent (overflow.get());
    overflow.reset();
    if (onClick != nullptr)
    {
        overflow = std::make_unique<OverflowKey> (std::move (onClick));
        addAndMakeVisible (*overflow);
    }
}

juce::Rectangle<int> ParameterRack::chassisBounds() const
{
    return getLocalBounds().withTrimmedLeft (chassisInsetX).withTrimmedRight (chassisInsetX).withTrimmedTop (chassisTop).withTrimmedBottom (chassisBottomGap);
}

int ParameterRack::contentLeft() const
{
    return faceplate ? chassisInsetX + motifWidth + motifGap : rackui::Cell::contentInset;
}

int ParameterRack::contentRightMargin() const
{
    return faceplate ? chassisInsetX + 12 : rackui::Cell::contentInset;
}

juce::Font ParameterRack::selectorCaptionFont() { return fonts::make (9.5f, fonts::Weight::medium, 0.06f); }

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
    powerId = id;
    if (faceplate)
    {
        if (auto* p = host.apvts().getParameter (id))
        {
            powerKeyPart = std::make_unique<rackui::PowerKey> (*p, &host.undo(), faceplate->glow);
            powerKeyPart->setTooltip (tip);
            powerKeyPart->onChange = [this] (bool) { repaint(); };
            addAndMakeVisible (*powerKeyPart);
        }
        return;
    }
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
                                                          const juce::String& tip, const juce::String& over)
{
    auto seg = std::make_unique<rackui::SegmentedSwitch> (*host.apvts().getParameter (id), std::move (positions), &host.undo(), accentColour);
    seg->setTooltip (tip);
    seg->setTitle (caption);
    seg->setSize (width, 27);
    auto& ref = *seg;
    addAndMakeVisible (ref);
    selectors.push_back ({ caption, std::move (seg), over });
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
    selectors.push_back ({ caption, std::move (list), {} });
    return ref;
}

void ParameterRack::setHeading (const juce::String& t)
{
    heading = t;
    if (auto* parent = getParentComponent())
        parent->repaint();
}

void ParameterRack::setAccent (juce::Colour c)
{
    accentColour = c;
    builtKey = -1;
    if (auto* parent = getParentComponent())
        parent->repaint();
}

juce::Component& ParameterRack::selectorComponent (std::unique_ptr<juce::Component> control, const juce::String& caption, int width)
{
    control->setSize (width, 27);
    control->setTitle (caption);
    auto& ref = *control;
    addAndMakeVisible (ref);
    selectors.push_back ({ caption, std::move (control), {} });
    return ref;
}

void ParameterRack::setGraph (std::unique_ptr<GraphFrame> g)
{
    if (graphView != nullptr)
        removeChildComponent (graphView.get());
    graphView = std::move (g);
    if (graphView != nullptr && faceplate)
        graphView->setGlass (faceplate->glassTop, faceplate->glassBottom, faceplate->glassTop.brighter (0.25f));
    addAndMakeVisible (*graphView);
}

void ParameterRack::group (const juce::String& caption, bool quiet)
{
    groups.push_back ({ caption, {}, quiet });
}

namespace
{
    juce::String captionOf (const juce::Component& c)
    {
        if (auto* k = dynamic_cast<const MiniKnob*> (&c))
            return k->captionText();
        if (auto* cell = dynamic_cast<const rackui::Cell*> (&c))
            return cell->captionText();
        return {};
    }
}

void ParameterRack::add (std::unique_ptr<juce::Component> cell, rackui::Slot slot)
{
    addAndMakeVisible (*cell);
    if (groups.empty())
        groups.push_back ({ {}, {}, false });
    if (groups.back().quiet)
    {
        if (auto* k = dynamic_cast<MiniKnob*> (cell.get()))
            k->setQuiet (true);
        else if (auto* c = dynamic_cast<rackui::Cell*> (cell.get()))
            c->setQuiet (true);
    }
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
        removeChildComponent (s.control.get());
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
    if (faceplate)
    {
        // The rail, from the right (left of the host's collapse key): power, overflow, the
        // product's part (the preset browser); the status in what is left.
        row = row.withSizeKeepingCentre (row.getWidth(), 26);
        if (powerKeyPart != nullptr)
        {
            powerKeyPart->setBounds (row.removeFromRight (24).withSizeKeepingCentre (24, 24));
            row.removeFromRight (8);
        }
        if (overflow != nullptr)
        {
            overflow->setBounds (row.removeFromRight (26).withSizeKeepingCentre (26, 22));
            row.removeFromRight (8);
        }
        if (railPart != nullptr)
        {
            railPart->setBounds (row.removeFromRight (railPartWidth).withSizeKeepingCentre (railPartWidth, 24));
            row.removeFromRight (14);
        }
        statusArea = row.withTrimmedLeft (4);
        return;
    }
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
    return contentLeft() + juce::roundToInt (share * static_cast<float> (getWidth() - contentLeft() - contentRightMargin())) + rackui::Cell::columnGap;
}

void ParameterRack::layout()
{
    const int width = getWidth(), x0 = contentLeft(), top = titleOffset();
    const int divider = dividerX();
    // The selector row: the pages, the shown page's sub-pages, its selectors (caption before),
    // a quiet rule between each rank.
    int x = x0;
    const int fieldY = top + (selectRow - 27) / 2;
    navRules.clear();
    auto rule = [this, &x] {
        x += 14;
        navRules.push_back (x);
        x += 15;
    };
    if (pageTabs != nullptr)
    {
        pageTabs->setBounds (x, fieldY, pageTabs->preferredWidth(), 27);
        x = pageTabs->getRight();
    }
    if (const auto it = subTabs.find (currentPage); it != subTabs.end())
    {
        // The sub-pages: set apart from the pages by space alone (no border).
        x += 28;
        it->second.first->setBounds (x, fieldY, it->second.first->preferredWidth(), 27);
        x = it->second.first->getRight();
    }
    const auto captionFont = selectorCaptionFont();
    auto captionWidth = [&captionFont] (const juce::String& c) { return c.isEmpty() ? 0 : juce::roundToInt (juce::GlyphArrangement::getStringWidth (captionFont, c)) + 6; };
    bool first = true;
    for (auto& s : selectors)
    {
        if (s.over.isNotEmpty())
            continue;
        if (first && x > x0)
            rule();
        first = false;
        x += captionWidth (s.caption);
        s.control->setTopLeftPosition (x, fieldY);
        x = s.control->getRight() + 14;
    }
    jassert (x - 14 <= divider - rackui::Cell::columnGap);
    // One row of cells on the standard lines; one slot family for the page.
    const bool dense = static_cast<int> (cells.size()) >= rackui::denseFrom;
    for (auto& g : groups)
        g.span = {};
    const int y = top + selectRow + rowGap;
    int cx = x0, lastGroup = -1;
    for (auto& p : cells)
    {
        if (lastGroup >= 0 && p.group != lastGroup)
            cx += groupGap - rackui::slotGap (dense);   // a group's gap (its divider in the middle)
        const int w = rackui::slotWidth (p.slot, dense);
        p.cell->setBounds (cx, y, w, rackui::cellHeight);
        auto& span = groups[static_cast<std::size_t> (p.group)].span;
        span = span.isEmpty() ? p.cell->getBounds() : span.getUnion (p.cell->getBounds());
        cx += w + rackui::slotGap (dense);
        lastGroup = p.group;
    }
    jassert (cx - rackui::slotGap (dense) <= divider - rackui::Cell::columnGap);
    // A selector of one control: over that cell, centred (in the row, away from the navigation).
    for (auto& s : selectors)
    {
        if (s.over.isEmpty())
            continue;
        for (auto& p : cells)
            if (captionOf (*p.cell) == s.over)
            {
                const int w = captionWidth (s.caption) + s.control->getWidth();
                const int left = juce::jmin (p.cell->getBounds().getCentreX() - w / 2, divider - rackui::Cell::columnGap - w);
                s.control->setTopLeftPosition (left + captionWidth (s.caption), fieldY);
            }
    }
    if (graphView != nullptr)
    {
        const int pictureX = divider + 1 + rackui::Cell::columnGap;
        if (faceplate)
            graphView->setBounds (pictureX, top + 7, width - contentRightMargin() - pictureX, graphHeight - 4);   // (room for its bezel)
        else
            graphView->setBounds (pictureX, top + 4, width - x0 - pictureX, graphHeight);
    }
}

void ParameterRack::paintFaceplate (juce::Graphics& g)
{
    const auto& f = *faceplate;
    const auto chassis = chassisBounds().toFloat();
    const float railBottom = static_cast<float> (titleOffset());
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
    const bool lit = powerKeyPart == nullptr || powerKeyPart->isOn();
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
    railTitleArea = juce::Rectangle<float> (titleX, chassis.getY(), titleW, railBottom - chassis.getY()).toNearestInt();
    juce::ignoreUnused (railTitleArea);
    if (f.subtitle.isNotEmpty())
    {
        const float sx = titleX + titleW + 16.0f;
        const float limit = (railPart != nullptr ? static_cast<float> (railPart->getX()) : chassis.getRight() - 120.0f) - 12.0f;
        if (limit > sx + 40.0f)
        {
            g.setColour (juce::Colour (0xfff7eedf).withAlpha (0.66f));
            g.setFont (fonts::make (8.5f, fonts::Weight::medium, 0.34f));
            g.drawText (f.subtitle, juce::Rectangle<float> (sx, chassis.getY(), limit - sx, railBottom - chassis.getY()).translated (0.0f, 0.5f), juce::Justification::centredLeft, true);
        }
    }
    // The graph's bezel: warm metal around the glass.
    if (graphView != nullptr)
    {
        const auto glass = graphView->getBounds().toFloat();
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

void ParameterRack::paint (juce::Graphics& g)
{
    if (faceplate)
        paintFaceplate (g);
    // The divider (as the effects racks').
    g.setColour (juce::Colour (0xffcbc4b9));
    g.fillRect (juce::Rectangle<float> (static_cast<float> (dividerX()), static_cast<float> (titleOffset() + 3), 1.0f, static_cast<float> (getHeight() - titleOffset() - 5)));
    // The selectors' captions (inline, before each).
    g.setFont (selectorCaptionFont());
    g.setColour (rackui::palette::caption);
    for (auto& s : selectors)
        if (s.caption.isNotEmpty())
            g.drawText (s.caption, juce::Rectangle<int> (s.control->getX() - 200, s.control->getY(), 193, s.control->getHeight()), juce::Justification::centredRight, false);
    // The rules between the ranks of the selector row (pages | sub-pages | settings).
    g.setColour (juce::Colour (0xffd8d0c4));
    for (const int rx : navRules)
        g.fillRect (juce::Rectangle<float> (static_cast<float> (rx), static_cast<float> (titleOffset() + selectRow / 2 - 7), 1.0f, 14.0f));
    // The groups: a quiet divider between them; a caption (if any) small in the band over the cells.
    for (std::size_t i = 0; i < groups.size(); ++i)
    {
        const auto& gr = groups[i];
        if (gr.span.isEmpty())
            continue;
        if (gr.caption.isNotEmpty())
        {
            // A quiet caption over the group's first cell (no rule: the divider and the gap
            // already say where it ends).
            g.setColour (gr.quiet ? rackui::palette::quietCaption : juce::Colour (0xff8c857b));
            g.setFont (fonts::make (9.0f, fonts::Weight::medium, 0.08f));
            g.drawText (gr.caption, juce::Rectangle<int> (gr.span.getX() + 4, gr.span.getY() - 12, gr.span.getWidth() - 8, 11), juce::Justification::centredLeft, true);
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
            g.setColour (faceplate ? juce::Colour (0xfff7eedf).withAlpha (0.75f) : juce::Colour (0xff8a847b));
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
    for (const auto& s : selectors)
        if (s.caption == caption)
            return s.control.get();
    return nullptr;
}

juce::Rectangle<int> ParameterRack::graphBounds() const
{
    return graphView != nullptr ? graphView->getBounds() : juce::Rectangle<int>();
}

} // namespace osp::plugin::rack
