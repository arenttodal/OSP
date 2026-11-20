#include "CustomPatternBrowser.h"

#include "AndorParameters.h"
#include "CustomPatternEditor.h"
#include "Design.h"
#include "OspLookAndFeel.h"
#include "io/ArpPatternJson.h"
#include "model/ArpFactoryPatterns.h"

#include <algorithm>
#include <set>

namespace osp::plugin
{

namespace
{
    namespace ap = arppattern;

    std::vector<ap::Document>& recentList()
    {
        static std::vector<ap::Document> recent;
        return recent;
    }

    /** The factory group's state, as the musician last left it (-1: not yet: open while the
        library is empty, closed once it has patterns of your own). */
    int& factoryRemembered()
    {
        static int open = -1;
        return open;
    }

    constexpr int maxRecent = 4;
    constexpr int maxListHeight = 336;
    constexpr int footerHeight = 30;

    int stepsOf (const ap::Pattern& p) { return (p.lengthTicks() + arp::ticksPerStep - 1) / arp::ticksPerStep; }
    juce::String stepsText (const ap::Document& d)
    {
        const int s = stepsOf (d.pattern);
        return juce::String (s) + (s == 1 ? " step" : " steps");
    }
}

void CustomPatternBrowser::noteUsed (const ap::Document& document)
{
    if (document.id.empty())
        return;
    auto& recent = recentList();
    recent.erase (std::remove_if (recent.begin(), recent.end(), [&document] (const ap::Document& d) { return d.id == document.id; }), recent.end());
    recent.insert (recent.begin(), document);
    if (static_cast<int> (recent.size()) > maxRecent)
        recent.resize (static_cast<std::size_t> (maxRecent));
}

void CustomPatternBrowser::clearRecentForTests()
{
    recentList().clear();
    factoryRemembered() = -1;
}

CustomPatternBrowser::CustomPatternBrowser (InstrumentHost& h, int card, std::function<void (bool)> editor)
    : MiniPanel ("CUSTOM PATTERNS", "CARD " + InstrumentHost::shownLayerName (juce::jlimit (0, 2, card))), host (h), cardIndex (juce::jlimit (0, 2, card)),
      openEditor (std::move (editor))
{
    rebuild();
}

void CustomPatternBrowser::rebuild()
{
    documents.clear();
    rows.clear();
    auto add = [this] (Kind kind, const juce::String& label, const juce::String& detail = {}, int document = -1) {
        rows.push_back ({ kind, label, detail, document });
    };
    auto keep = [this] (const ap::Document& d) {
        documents.push_back (d);
        return static_cast<int> (documents.size()) - 1;
    };

    // The card's own pattern.
    const auto* current = host.customArpDocument (cardIndex);
    add (Kind::heading, "ON CARD " + InstrumentHost::shownLayerName (cardIndex));
    if (current != nullptr)
        add (Kind::current, juce::String (current->name).isEmpty() ? juce::String ("Custom pattern") : juce::String (current->name), stepsText (*current), keep (*current));
    else
        add (Kind::none, "No pattern on this card yet");

    // The library and the factory: what the rest of the list already shows.
    std::vector<std::pair<juce::String, ap::Document>> library;
    std::set<std::string> listed;
    {
        auto files = CustomPatternEditor::libraryFolder().findChildFiles (juce::File::findFiles, false, juce::String ("*") + arpPatternExtension);
        files.sort();
        for (const auto& f : files)
        {
            std::string error;
            // (an unreadable or newer file is left out of the list, never an error here)
            if (auto d = readArpPattern (f.getFullPathName().toStdString(), error))
            {
                listed.insert (d->id);
                library.emplace_back (f.getFileNameWithoutExtension(), std::move (*d));
            }
        }
    }
    for (const auto& d : ap::factoryPatterns())
        listed.insert (d.id);
    if (current != nullptr)
        listed.insert (current->id);

    // RECENT: the other cards' patterns and the ones used this session, when nowhere else here.
    std::vector<std::pair<ap::Document, juce::String>> recent;
    for (int c = 0; c < 3; ++c)
        if (const auto* d = c != cardIndex ? host.customArpDocument (c) : nullptr; d != nullptr && listed.insert (d->id).second)
            recent.emplace_back (*d, "card " + InstrumentHost::shownLayerName (c));
    for (const auto& d : recentList())
        if (listed.insert (d.id).second)
            recent.emplace_back (d, stepsText (d));
    if (! recent.empty())
    {
        add (Kind::heading, "RECENT");
        for (auto& [d, detail] : recent)
            add (Kind::recent, juce::String (d.name), detail, keep (d));
    }

    add (Kind::heading, "YOUR PATTERNS");
    if (library.empty())
        add (Kind::none, "None saved yet (Save to Library in the editor)");
    for (auto& [fileName, d] : library)
        add (Kind::user, fileName, stepsText (d), keep (d));

    if (factoryRemembered() < 0)
        factoryOpen = library.empty();
    else
        factoryOpen = factoryRemembered() > 0;
    const auto& factory = ap::factoryPatterns();
    add (Kind::factoryHeading, "FACTORY PATTERNS", juce::String (static_cast<int> (factory.size())));
    if (factoryOpen)
        for (const auto& d : factory)
            add (Kind::factory, juce::String (d.name), stepsText (d), keep (d));

    add (Kind::newPattern, "+ New Pattern");
    scroll = juce::jlimit (0, std::max (0, contentHeight() - listArea.getHeight()), scroll);
}

int CustomPatternBrowser::rowHeight (const Row& r) const
{
    switch (r.kind)
    {
        case Kind::heading: return 22;
        case Kind::factoryHeading: return 28;
        case Kind::none: return 22;
        case Kind::newPattern: return 0;   // (the footer)
        default: return 24;
    }
}

int CustomPatternBrowser::contentHeight() const
{
    int h = 0;
    for (const auto& r : rows)
        h += rowHeight (r);
    return h;
}

juce::Point<int> CustomPatternBrowser::cardSize() const
{
    const int list = std::min (maxListHeight, contentHeight());
    return { width, popover::headerTop + list + popover::gap + footerHeight + popover::bottom };
}

void CustomPatternBrowser::layoutContent (juce::Rectangle<int> area)
{
    footerArea = area.removeFromBottom (footerHeight);
    area.removeFromBottom (popover::gap);
    listArea = area;
    scroll = juce::jlimit (0, std::max (0, contentHeight() - listArea.getHeight()), scroll);
}

juce::Rectangle<int> CustomPatternBrowser::rowArea (int index) const
{
    if (index < 0 || index >= rowCount())
        return {};
    if (rows[static_cast<std::size_t> (index)].kind == Kind::newPattern)
        return footerArea;
    int y = listArea.getY() - scroll;
    for (int i = 0; i < index; ++i)
        y += rowHeight (rows[static_cast<std::size_t> (i)]);
    return { listArea.getX(), y, listArea.getWidth(), rowHeight (rows[static_cast<std::size_t> (index)]) };
}

juce::Rectangle<int> CustomPatternBrowser::editArea() const
{
    const int i = find (Kind::current);
    if (i < 0)
        return {};
    return rowArea (i).removeFromRight (56).reduced (2, 3);
}

int CustomPatternBrowser::find (Kind kind, const juce::String& label) const
{
    for (int i = 0; i < rowCount(); ++i)
        if (rows[static_cast<std::size_t> (i)].kind == kind && (label.isEmpty() || rows[static_cast<std::size_t> (i)].label == label))
            return i;
    return -1;
}

int CustomPatternBrowser::rowAt (juce::Point<int> p) const
{
    if (footerArea.contains (p))
        return find (Kind::newPattern);
    if (! listArea.contains (p))
        return -1;
    for (int i = 0; i < rowCount(); ++i)
        if (rows[static_cast<std::size_t> (i)].kind != Kind::newPattern && rowArea (i).contains (p))
            return i;
    return -1;
}

void CustomPatternBrowser::choose (const ap::Document& document)
{
    host.setCustomArpDocument (cardIndex, &document);
    if (auto* p = host.apvts().getParameter (ids::arpCustom (host.cardArpNamespace(), cardIndex)); p != nullptr && p->getValue() < 0.5f)
    {
        host.undo().beginNewTransaction ("Custom pattern");
        p->beginChangeGesture();
        p->setValueNotifyingHost (1.0f);
        p->endChangeGesture();
    }
    noteUsed (document);
    if (onUserChoice != nullptr)
        onUserChoice();
    if (onClose != nullptr)
        onClose();
}

void CustomPatternBrowser::activate (int index, bool edit)
{
    if (index < 0 || index >= rowCount())
        return;
    const auto r = rows[static_cast<std::size_t> (index)];
    switch (r.kind)
    {
        case Kind::current:
            if (edit)
            {
                if (auto open = openEditor; open != nullptr)
                    open (false);   // (this panel may be gone after this)
                return;
            }
            choose (documents[static_cast<std::size_t> (r.document)]);
            return;
        case Kind::recent:
        case Kind::user:
        case Kind::factory:
            choose (documents[static_cast<std::size_t> (r.document)]);
            return;
        case Kind::factoryHeading:
            factoryOpen = ! factoryOpen;
            factoryRemembered() = factoryOpen ? 1 : 0;
            rebuild();
            if (onSizeChanged != nullptr)
                onSizeChanged();
            resized();
            repaint();
            return;
        case Kind::newPattern:
            if (auto open = openEditor; open != nullptr)
                open (true);
            return;
        case Kind::heading:
        case Kind::none:
            return;
    }
}

void CustomPatternBrowser::mouseDown (const juce::MouseEvent& e)
{
    const auto pos = e.getPosition();
    const int i = rowAt (pos);
    if (i < 0)
        return;
    activate (i, rows[static_cast<std::size_t> (i)].kind == Kind::current && editArea().contains (pos));
}

void CustomPatternBrowser::mouseMove (const juce::MouseEvent& e)
{
    const int i = rowAt (e.getPosition());
    const bool onEdit = editArea().contains (e.getPosition());
    if (i != hoverRow || onEdit != hoverEdit)
    {
        hoverRow = i;
        hoverEdit = onEdit;
        repaint();
    }
    const auto kind = i >= 0 ? rows[static_cast<std::size_t> (i)].kind : Kind::none;
    setMouseCursor (kind == Kind::heading || kind == Kind::none || i < 0 ? juce::MouseCursor::NormalCursor : juce::MouseCursor::PointingHandCursor);
}

void CustomPatternBrowser::mouseExit (const juce::MouseEvent&)
{
    hoverRow = -1;
    hoverEdit = false;
    repaint();
}

void CustomPatternBrowser::scrollBy (int pixels)
{
    const int next = juce::jlimit (0, std::max (0, contentHeight() - listArea.getHeight()), scroll + pixels);
    if (next != scroll)
    {
        scroll = next;
        repaint();
    }
}

void CustomPatternBrowser::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& wheel)
{
    // (scrolls the list; it never changes a pattern)
    wheelPending -= wheel.deltaY * 120.0f;
    const int px = static_cast<int> (wheelPending);
    wheelPending -= static_cast<float> (px);
    scrollBy (px);
}

void CustomPatternBrowser::paint (juce::Graphics& g)
{
    MiniPanel::paint (g);
    using namespace design;
    const auto* current = host.customArpDocument (cardIndex);
    const bool customOn = host.parameterValue (ids::arpCustom (host.cardArpNamespace(), cardIndex)) >= 0.5f;

    // CARD X on the title row's right.
    g.setFont (type::popupLabel (10.0f));
    g.setColour (colour::textMicro);
    g.drawText (subtitle, card().toFloat().withTrimmedRight (14.0f).withHeight (34.0f).withTrimmedTop (7.0f).withHeight (20.0f), juce::Justification::centredRight, false);

    {
        juce::Graphics::ScopedSaveState clip (g);
        g.reduceClipRegion (listArea.expanded (2, 0));
        for (int i = 0; i < rowCount(); ++i)
        {
            const auto& r = rows[static_cast<std::size_t> (i)];
            if (r.kind == Kind::newPattern)
                continue;
            const auto area = rowArea (i).toFloat();
            if (area.getBottom() < static_cast<float> (listArea.getY()) || area.getY() > static_cast<float> (listArea.getBottom()))
                continue;
            switch (r.kind)
            {
                case Kind::heading:
                    g.setFont (type::popupLabel (popover::captionFont));
                    g.setColour (colour::textMicro);
                    g.drawText (r.label, area.withTrimmedLeft (8.0f).withTrimmedTop (6.0f), juce::Justification::centredLeft, false);
                    break;
                case Kind::none:
                    g.setFont (type::annotation (11.0f));
                    g.setColour (colour::textMicro);
                    g.drawText (r.label, area.withTrimmedLeft (10.0f), juce::Justification::centredLeft, true);
                    break;
                case Kind::factoryHeading:
                {
                    if (i == hoverRow)
                    {
                        g.setColour (colour::text.withAlpha (0.05f));
                        g.fillRoundedRectangle (area.reduced (0.0f, 2.0f), 5.0f);
                    }
                    g.setColour (colour::hairline);
                    g.fillRect (area.withHeight (1.0f).reduced (6.0f, 0.0f));
                    // The disclosure triangle, then the caption and its count.
                    juce::Path tri;
                    const auto c = juce::Point<float> (area.getX() + 13.0f, area.getCentreY() + 1.0f);
                    if (factoryOpen)
                        tri.addTriangle (c.x - 3.5f, c.y - 2.0f, c.x + 3.5f, c.y - 2.0f, c.x, c.y + 2.5f);
                    else
                        tri.addTriangle (c.x - 2.0f, c.y - 3.5f, c.x - 2.0f, c.y + 3.5f, c.x + 2.5f, c.y);
                    g.setColour (colour::textSecondary);
                    g.fillPath (tri);
                    g.setFont (type::popupLabel (popover::captionFont));
                    g.setColour (colour::textSecondary);
                    g.drawText (r.label, area.withTrimmedLeft (22.0f).withTrimmedTop (1.0f), juce::Justification::centredLeft, false);
                    g.setColour (colour::textMicro);
                    g.drawText (r.detail, area.withTrimmedRight (10.0f).withTrimmedTop (1.0f), juce::Justification::centredRight, false);
                    break;
                }
                default:
                {
                    // A pattern: its name, quiet details on the right; the card's playing
                    // pattern marked with the accent.
                    const bool playing = customOn && current != nullptr && r.document >= 0 && documents[static_cast<std::size_t> (r.document)].id == current->id;
                    auto text = area;
                    if (r.kind == Kind::current)
                        text = text.withTrimmedRight (60.0f);
                    if (playing)
                    {
                        g.setColour (colour::accent.withAlpha (0.16f));
                        g.fillRoundedRectangle (area.reduced (0.0f, 1.0f), 5.0f);
                    }
                    else if (i == hoverRow && ! hoverEdit)
                    {
                        g.setColour (colour::text.withAlpha (0.06f));
                        g.fillRoundedRectangle (area.reduced (0.0f, 1.0f), 5.0f);
                    }
                    if (playing)
                    {
                        g.setColour (colour::accent);
                        g.fillEllipse (juce::Rectangle<float> (5.0f, 5.0f).withCentre ({ area.getX() + 9.0f, area.getCentreY() }));
                    }
                    g.setFont (type::popupLabel (12.5f));
                    g.setColour (colour::text.withAlpha (i == hoverRow || playing ? 0.95f : 0.8f));
                    g.drawText (r.label, text.withTrimmedLeft (18.0f).withTrimmedRight (64.0f), juce::Justification::centredLeft, true);
                    g.setFont (type::popupLabel (10.0f));
                    g.setColour (colour::textMicro);
                    g.drawText (r.detail, text.withTrimmedRight (8.0f), juce::Justification::centredRight, false);
                    if (r.kind == Kind::current)
                    {
                        // Edit: a quiet key with a pencil.
                        const auto key = editArea().toFloat();
                        g.setGradientFill (juce::ColourGradient (colour::buttonTop, 0.0f, key.getY(), colour::buttonBottom, 0.0f, key.getBottom(), false));
                        g.fillRoundedRectangle (key, 5.0f);
                        g.setColour (hoverEdit ? colour::divider.darker (0.15f) : colour::edgeShade);
                        g.drawRoundedRectangle (key.reduced (0.5f), 5.0f, 1.0f);
                        const auto icon = juce::Rectangle<float> (10.0f, 10.0f).withCentre ({ key.getX() + 13.0f, key.getCentreY() });
                        juce::Path pencil;
                        pencil.startNewSubPath (icon.getX() + 1.0f, icon.getBottom() - 1.0f);
                        pencil.lineTo (icon.getX() + 1.5f, icon.getBottom() - 3.5f);
                        pencil.lineTo (icon.getRight() - 2.5f, icon.getY() + 0.5f);
                        pencil.lineTo (icon.getRight() - 0.5f, icon.getY() + 2.5f);
                        pencil.lineTo (icon.getX() + 3.5f, icon.getBottom() - 1.5f);
                        pencil.closeSubPath();
                        g.setColour (colour::textSecondary);
                        g.strokePath (pencil, juce::PathStrokeType (1.1f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
                        g.setFont (type::popupLabel (11.0f));
                        g.setColour (colour::text.withAlpha (0.85f));
                        g.drawText ("Edit", key.withTrimmedLeft (21.0f), juce::Justification::centredLeft, false);
                    }
                    break;
                }
            }
        }
    }

    // A scroll track when the list is longer than its place.
    if (contentHeight() > listArea.getHeight())
    {
        const auto track = juce::Rectangle<float> (static_cast<float> (listArea.getRight()) + 4.0f, static_cast<float> (listArea.getY()) + 2.0f, 3.0f,
                                                   static_cast<float> (listArea.getHeight()) - 4.0f);
        g.setColour (colour::hairline.withAlpha (0.8f));
        g.fillRoundedRectangle (track, 1.5f);
        const float share = static_cast<float> (listArea.getHeight()) / static_cast<float> (contentHeight());
        const float top = static_cast<float> (scroll) / static_cast<float> (contentHeight());
        g.setColour (colour::textSecondary.withAlpha (0.5f));
        g.fillRoundedRectangle (track.withY (track.getY() + top * track.getHeight()).withHeight (share * track.getHeight()), 1.5f);
    }

    // + New Pattern: the footer, a neutral key across the panel.
    const auto key = footerArea.toFloat().reduced (0.5f);
    const int newRow = find (Kind::newPattern);
    g.setGradientFill (juce::ColourGradient (colour::buttonTop, 0.0f, key.getY(), colour::buttonBottom, 0.0f, key.getBottom(), false));
    g.fillRoundedRectangle (key, 6.0f);
    g.setColour (hoverRow == newRow ? colour::divider.darker (0.15f) : colour::edgeShade);
    g.drawRoundedRectangle (key, 6.0f, 1.0f);
    g.setFont (type::popupLabel (12.5f));
    g.setColour (colour::text.withAlpha (0.88f));
    g.drawText ("+  New Pattern", key, juce::Justification::centred, false);
}

} // namespace osp::plugin
