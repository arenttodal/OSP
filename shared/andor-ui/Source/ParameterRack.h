#pragma once

#include "EffectRack.h"
#include "InstrumentHost.h"
#include "RackControls.h"
#include "ShapingPopups.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <map>

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
    approved rack's parts and in its envelope, so it is one design with MOVEMENT, ECHO and
    SPACE: the same 214 px rack (title row 32, body 172), the same selector row, ONE row of
    cells on the same lines (rackui::captionHeight, controlCentre, valueTop), the graph 160 px
    in the module's column. What does not fit one row is split into pages (and a page into
    sub-pages): an interface choice, never a parameter, and never a taller rack.

    The selector row holds the page words, the shown page's sub-page words, then the page's own
    compact selectors (captioned segmented switches or lists). A subclass builds a page's cells
    and selectors in build(); the module rebuilds only when the page, the sub-page or buildKey()
    changes (a mode), so a value, a preset or automation never moves anything.
*/
class ParameterRack : public Module
{
public:
    ParameterRack (InstrumentHost& host, juce::String title, juce::Colour accent, float leftShare);
    ~ParameterRack() override;

    juce::String title() const override { return heading; }
    juce::Colour accent() const override { return accentColour; }
    int bodyHeight() const override { return standardBody; }
    void layoutTitleRow (juce::Rectangle<int> row) override;
    void refresh() override;
    void settle() override;
    void resized() override;
    void paint (juce::Graphics&) override;

    /** Tests: the captions in order, the cells' bounds, a control by its caption (a cell's or a
        selector's), the page and sub-page shown (and showing them), the graph's bounds, the
        header's switch, the status. */
    juce::String captions() const;
    std::vector<juce::Rectangle<int>> cellBounds() const;
    juce::Component* cellNamed (const juce::String& caption) const;
    int page() const noexcept { return currentPage; }
    int subPage() const noexcept;
    void showPage (int page);
    void showSubPage (int subPage);
    juce::Rectangle<int> graphBounds() const;
    rackui::VerticalSwitch* headerSwitch() const noexcept { return power.get(); }
    juce::String status() const { return headerStatus != nullptr ? headerStatus() : juce::String(); }

    /** The standard rack (MOVEMENT / ECHO / SPACE's): body 172, the selector row 39 under the
        title, 8 px, the cells; the graph 4 px under the title, 160 high. */
    static constexpr int standardBody = 172, selectRow = 39, rowGap = 8, graphHeight = 160;

protected:
    /** The pages (FILTER / VOICE / PAIR): low-profile words in the selector row, the shown one
        underlined; `memory` keeps the last page shown (interface only). */
    void setPages (juce::StringArray names, int* memory);
    /** A page's own sub-pages (VOICE: AMP / MOD / CHARACTER), shown after the pages' words. */
    void setSubPages (int page, juce::StringArray names, int* memory);
    /** The module's ON in its title row (the approved mini switch, ON up), and its word. */
    void setHeaderSwitch (const juce::String& parameterId, const juce::String& word, const juce::String& tip);
    /** A short status beside the title (an empty string: none). */
    std::function<juce::String()> headerStatus;
    void setGraph (std::unique_ptr<GraphFrame> graph);
    GraphFrame* graph() const noexcept { return graphView.get(); }

    /** Builds the shown page's selectors and cells (the sub-page: subPage()). */
    virtual void build (int page) = 0;
    /** What the cells depend on besides the pages (a mode): a change rebuilds them. */
    virtual int buildKey() const { return 0; }

    /** In build(): the page's compact selectors in the selector row (a small caption before). */
    rackui::SegmentedSwitch& selectorSegment (const juce::String& id, const juce::String& caption, std::vector<rackui::Position> positions, int width,
                                              const juce::String& tip);
    ValueSelector& selectorList (const juce::String& id, const juce::String& caption, int width, const juce::String& tip);
    /** In build(): the next cells form a group (a quiet divider before it; its caption, if any,
        small in the band over its cells). */
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
        int group = 0;
        rackui::Slot slot = rackui::Slot::knob;
    };
    struct Group
    {
        juce::String caption;
        juce::Rectangle<int> span;
    };
    class PageTabs;
    void add (std::unique_ptr<juce::Component> cell, rackui::Slot slot);
    int currentKey() const;
    void rebuild();
    void layout();
    int dividerX() const;

    juce::String heading;
    juce::Colour accentColour;
    float share;
    int currentPage = 0, builtKey = -1;
    int* pageMemory = nullptr;
    std::unique_ptr<PageTabs> pageTabs;
    std::map<int, std::pair<std::unique_ptr<PageTabs>, int*>> subTabs;
    std::vector<std::pair<juce::String, std::unique_ptr<juce::Component>>> selectors;
    std::unique_ptr<rackui::VerticalSwitch> power;
    juce::String powerWord;
    juce::Rectangle<int> powerWordArea, statusArea;
    std::unique_ptr<GraphFrame> graphView;
    std::vector<Placed> cells;
    std::vector<Group> groups;
};

} // namespace osp::plugin::rack
