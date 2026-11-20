#pragma once

#include "InstrumentHost.h"
#include "ShapingPopups.h"

#include "model/ArpPatternModel.h"

#include <functional>
#include <vector>

namespace osp::plugin
{

/**
    Custom ARP Patterns (docs/research/custom-arp, R2): the one place a card's custom pattern is
    chosen, opened from the ARP style list's single "Custom Patterns ›" row.

    It is for choosing, not for managing files: the card's current pattern (with Edit), the
    patterns used recently in this session that are nowhere else in the list (other cards',
    applied ones not in the library), your library (~/Documents/ANDOR/Patterns), the nine factory
    patterns (a collapsible group) and + New Pattern.

    Choosing a pattern puts it on the card, turns that card's CUSTOM on (one undo step) and
    closes. Opening and closing it alone changes nothing: it only reads.
*/
class CustomPatternBrowser final : public MiniPanel
{
public:
    /** `openEditor (fresh)`: the card's editor (fresh: + New Pattern; else Edit). Called after
        the browser has asked to close; the owner opens the editor asynchronously. */
    CustomPatternBrowser (InstrumentHost& host, int card, std::function<void (bool fresh)> openEditor);

    static constexpr int width = 300;
    /** ANDOR Learn (opt-in, null by default): a pattern was chosen by a click on its row (or
        activate(), which is what the click does), after the card took it and before the browser
        asks to close. */
    std::function<void()> onUserChoice;
    juce::Point<int> cardSize() const override;
    bool compact() const override { return true; }
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    enum class Kind
    {
        heading,       ///< CURRENT, RECENT, YOUR PATTERNS
        current,       ///< the card's pattern (choose it again; Edit on its right)
        none,          ///< a quiet line: nothing in this group
        recent,
        user,
        factoryHeading, ///< ▸ / ▾ FACTORY PATTERNS (n)
        factory,
        newPattern     ///< the footer
    };
    struct Row
    {
        Kind kind = Kind::none;
        juce::String label, detail;
        int document = -1;   ///< into documents (choosable rows)
    };

    // -- tests -------------------------------------------------------------------------------
    int rowCount() const noexcept { return static_cast<int> (rows.size()); }
    const Row& row (int index) const { return rows[static_cast<std::size_t> (index)]; }
    /** The first row of a kind with this label (-1: none). */
    int find (Kind kind, const juce::String& label = {}) const;
    /** What a click on the row does (Edit: `edit` on the current row). */
    void activate (int index, bool edit = false);
    bool factoryExpanded() const noexcept { return factoryOpen; }
    juce::Rectangle<int> rowArea (int index) const;
    juce::Rectangle<int> editArea() const;

    /** A pattern the musician used (applied from the editor, chosen here): offered under RECENT
        while it is nowhere else in the list. In memory, for this session (process-wide). */
    static void noteUsed (const arppattern::Document& document);
    static void clearRecentForTests();

protected:
    void layoutContent (juce::Rectangle<int> area) override;

private:
    void rebuild();
    void choose (const arppattern::Document& document);
    int rowAt (juce::Point<int> p) const;
    int rowHeight (const Row& r) const;
    int contentHeight() const;
    void scrollBy (int pixels);

    InstrumentHost& host;
    int cardIndex = 0;
    std::function<void (bool)> openEditor;
    std::vector<arppattern::Document> documents;
    std::vector<Row> rows;
    juce::Rectangle<int> listArea, footerArea;
    int scroll = 0, hoverRow = -1;
    bool hoverEdit = false;
    bool factoryOpen = false;
    float wheelPending = 0.0f;
};

} // namespace osp::plugin
