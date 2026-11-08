#pragma once

#include "EffectRack.h"
#include "InstrumentHost.h"
#include "RackControls.h"
#include "ShapingPopups.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace osp::plugin::rack
{

/**
    The graph of a product's rack module: the approved dark frame (#1c2528 to #192124, radius 5,
    edge #343f43), the module drawing inside it. A product subclasses it and paints what its
    sound is (a computed response, a measured state); `refresh` is called on the editor's timer.
*/
class GraphFrame : public juce::Component
{
public:
    GraphFrame() { setInterceptsMouseClicks (false, false); }
    void paint (juce::Graphics&) final;
    virtual void refresh() {}

protected:
    /** Inside the frame (its 1 px edge excluded). */
    virtual void paintGraph (juce::Graphics&, juce::Rectangle<float> area) = 0;
};

/**
    A product's own rack module (FILTER, MORPH: ShellProduct::productRacks), built from the
    approved rack's parts so it is one design with MOVEMENT, ECHO and SPACE: the title row
    (the host draws the title) with an optional ON switch and a short status, a selector row
    (an optional page selector - an interface choice, never a parameter - and captioned
    segmented selectors), then rows of cells in groups (knobs, stepped rates, lists, segmented
    and vertical switches) on the approved slot widths, the graph at the right in a fixed
    column. Every control is bound to a host parameter.

    A subclass builds a page's cells in build(); the module rebuilds only when buildKey()
    changes (a page or a mode), so a value, a preset or automation never moves anything.
*/
class ParameterRack : public Module
{
public:
    ParameterRack (InstrumentHost& host, juce::String title, juce::Colour accent, float leftShare, int rows);
    ~ParameterRack() override;

    juce::String title() const override { return heading; }
    juce::Colour accent() const override { return accentColour; }
    int bodyHeight() const override;
    void layoutTitleRow (juce::Rectangle<int> row) override;
    void refresh() override;
    void settle() override;
    void resized() override;
    void paint (juce::Graphics&) override;

    /** Tests: the captions in order, the cells' bounds, a cell's control by its caption, the
        page shown (and showing one), the graph's bounds, the header's switch. */
    juce::String captions() const;
    std::vector<juce::Rectangle<int>> cellBounds() const;
    juce::Component* cellNamed (const juce::String& caption) const;
    int page() const noexcept { return currentPage; }
    void showPage (int page);
    juce::Rectangle<int> graphBounds() const;
    rackui::VerticalSwitch* headerSwitch() const noexcept { return power.get(); }
    juce::String status() const { return headerStatus != nullptr ? headerStatus() : juce::String(); }

    /** Approved rows: the selector row 39, 8 px, then each row a 12 px group band and a cell. */
    static constexpr int selectRow = 39, rowGap = 8, groupBand = 12, bottomMargin = 10;

protected:
    /** The page selector (FILTER / VOICE / PAIR): low-profile words in the selector row, the
        shown one underlined; `memory` keeps the last page shown (interface only). */
    void setPages (juce::StringArray names, int* memory);
    /** The module's ON in its title row (the approved mini switch, ON up), and its word. */
    void setHeaderSwitch (const juce::String& parameterId, const juce::String& word, const juce::String& tip);
    /** A short status beside the title (an empty string: none). */
    std::function<juce::String()> headerStatus;
    /** A captioned segmented selector in the selector row (MORPH's MODE, CARRIER). */
    rackui::SegmentedSwitch& selectorSegment (const juce::String& id, const juce::String& caption, std::vector<rackui::Position> positions, int width,
                                              const juce::String& tip);
    void setGraph (std::unique_ptr<GraphFrame> graph);
    GraphFrame* graph() const noexcept { return graphView.get(); }

    /** Builds `page`'s cells (row(), group(), the cells). */
    virtual void build (int page) = 0;
    /** What the cells depend on besides the page (a mode): a change rebuilds them. */
    virtual int buildKey() const { return 0; }

    /** The next cells go in the next row (the first row is implicit). */
    void row();
    /** The next cells form a group with this small caption over them (a divider before it). */
    void group (const juce::String& caption);
    MiniKnob& knob (const juce::String& id, const juce::String& caption, MiniKnob::Formatter f);
    MiniKnob& steppedKnob (const juce::String& id, const juce::String& caption, MiniKnob::Formatter f);
    ValueSelector& listCell (const juce::String& id, const juce::String& caption, const juce::String& tip);
    rackui::SegmentedSwitch& segmentCell (const juce::String& id, const juce::String& caption, std::vector<rackui::Position> positions, const juce::String& tip,
                                          rackui::Slot slot = rackui::Slot::wide);
    rackui::VerticalSwitch& switchCell (const juce::String& id, const juce::String& caption, std::vector<rackui::Position> positions, const juce::String& tip);

    InstrumentHost& host;

private:
    struct Placed
    {
        std::unique_ptr<juce::Component> cell;
        int row = 0, group = 0;
        rackui::Slot slot = rackui::Slot::knob;
    };
    struct Group
    {
        juce::String caption;
        int row = 0;
        juce::Rectangle<int> span;
    };
    class PageTabs;
    void add (std::unique_ptr<juce::Component> cell, rackui::Slot slot);
    void rebuild();
    void layout();

    juce::String heading;
    juce::Colour accentColour;
    float share;
    int rows;
    int currentPage = 0, builtKey = -1, buildingRow = 0;
    int* pageMemory = nullptr;
    std::unique_ptr<PageTabs> pageTabs;
    std::vector<std::pair<juce::String, std::unique_ptr<juce::Component>>> selectors;
    std::unique_ptr<rackui::VerticalSwitch> power;
    juce::String powerWord;
    juce::Rectangle<int> powerWordArea, statusArea;
    std::unique_ptr<GraphFrame> graphView;
    std::vector<Placed> cells;
    std::vector<Group> groups;
};

} // namespace osp::plugin::rack
