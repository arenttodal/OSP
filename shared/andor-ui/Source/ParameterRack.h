#pragma once

#include "EffectRack.h"
#include "InstrumentHost.h"
#include "RackControls.h"
#include "RackFaceplate.h"
#include "ShapingPopups.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <map>
#include <optional>

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
    /** The glass's tint (opt-in; a hardware faceplate's graphite, warm or cool). Unset: the
        approved #1c2528 to #192124, edge #343f43. */
    void setGlass (juce::Colour top, juce::Colour bottom, juce::Colour edge)
    {
        glassTop = top;
        glassBottom = bottom;
        glassEdge = edge;
        repaint();
    }

protected:
    /** Inside the frame (its 1 px edge excluded). */
    virtual void paintGraph (juce::Graphics&, juce::Rectangle<float> area) = 0;

private:
    juce::Colour glassTop { 0xff1c2528 }, glassBottom { 0xff192124 }, glassEdge { 0xff343f43 };
};

/**
    A product's own rack module (FILTER, MORPH: ShellProduct::productRacks), built from the
    approved rack's parts and in its envelope, so it is one design with MOVEMENT, ECHO and
    SPACE: the same 214 px rack (title row 32, body 172), the same selector row, ONE row of
    cells on the same lines (rackui::captionHeight, controlCentre, valueTop), the graph 160 px
    in the module's column. What does not fit one row is split into pages (and a page into
    sub-pages): an interface choice, never a parameter, and never a taller rack.

    The selector row holds four ranks, each told apart at a glance: the pages (primary: 11 px
    words, the shown one dark and underlined in the module's colour), the shown page's sub-pages
    (secondary: 10 px, lighter, a 1 px underline; set apart by space, no border), then, after a
    quiet rule, the page's
    contextual settings (captioned segmented switches or lists: fields, not words); the cells
    below are the sound's controls. A subclass builds a page's cells
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

    /** The boutique hardware faceplate (opt-in; ANDOR ANALOG's racks; ANDOR OSP's racks never
        set it): the module is drawn as a small instrument in the rack's slot - a ceramic
        chassis with a hairline bezel and four screws, an enamel title rail (its LED following
        the real ON, the title and a short subtitle, a product's part such as the preset
        browser, the overflow key, the power key), a narrow band of stripes at the body's left
        (the controls start after it) and the graph in a metal bezel on tinted glass. The
        heights, the selector row and the cells' lines are the standard rack's: only the paint
        and the left/right margins change. Interface only. */
    using Faceplate = rack::Faceplate;
    void setFaceplate (const Faceplate& f);
    bool hasFaceplate() const noexcept { return faceplate.has_value(); }
    const std::optional<Faceplate>& faceplateStyle() const noexcept { return faceplate; }
    /** Faceplate: the rail's own part (the preset browser), `width` wide, left of the overflow
        key, and the overflow key's action (null: no key). */
    void setRailComponent (std::unique_ptr<juce::Component> component, int width);
    juce::Component* railComponent() const noexcept { return railPart.get(); }
    void setOverflow (std::function<void (juce::Component& key)> onClick);
    juce::Component* overflowKey() const noexcept { return overflow.get(); }
    /** The graph IS the module (opt-in; ANDOR ANALOG's graphic EQ): it takes the body's whole
        width and height under the rail - no cells, no divider; its controls are its own overlays. */
    void setFullWidthGraph (bool full);
    /** The rail's save key (left of the overflow key): saves the settings as a preset. */
    void setSaveAction (std::function<void()> onClick);
    juce::Component* saveKey() const noexcept { return save.get(); }
    /** Faceplate: its power key (bound to the header switch's parameter), or null. */
    rackui::PowerKey* powerKey() const noexcept { return powerKeyPart.get(); }
    /** The chassis and the content's left edge (component coordinates; tests, subclasses). */
    juce::Rectangle<int> chassisBounds() const;
    int contentLeft() const;

    /** The standard rack (MOVEMENT / ECHO / SPACE's): body 172, the selector row 39 under the
        title, 8 px, the cells; the graph 4 px under the title, 160 high. */
    static constexpr int standardBody = 172, selectRow = 39, rowGap = 8, graphHeight = 160;
    /** Between two groups of cells (their divider in the middle): wider than a slot's gap, so
        each group reads as its own section. */
    static constexpr int groupGap = 28;
    /** The selector row's captions (TYPE, METHOD): one style for every rack. */
    static juce::Font selectorCaptionFont();

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
    /** A rack whose subject changes (a product's FX rack showing one effect, then another):
        its title and colour (the cells take the colour at their next build). */
    void setHeading (const juce::String& title);
    void setAccent (juce::Colour colour);
    GraphFrame* graph() const noexcept { return graphView.get(); }

    /** Faceplate: what its LED shows (default: the power key's ON, or lit without one). A
        rack without an ON of its own (a macro's effect) shows whether it plays. */
    virtual bool faceplateLit() const { return powerKeyPart == nullptr || powerKeyPart->isOn(); }

    /** Builds the shown page's selectors and cells (the sub-page: subPage()). */
    virtual void build (int page) = 0;
    /** What the cells depend on besides the pages (a mode): a change rebuilds them. */
    virtual int buildKey() const { return 0; }

    /** In build(): the page's compact selectors in the selector row (a small caption before),
        after the navigation and a quiet rule. `over`: instead, placed over the cell captioned so
        (a setting of that one control, in the row above it, away from the navigation). */
    rackui::SegmentedSwitch& selectorSegment (const juce::String& id, const juce::String& caption, std::vector<rackui::Position> positions, int width,
                                              const juce::String& tip, const juce::String& over = {});
    ValueSelector& selectorList (const juce::String& id, const juce::String& caption, int width, const juce::String& tip);
    /** In build(): a product's own field in the selector row (a preset browser), 27 px high. */
    juce::Component& selectorComponent (std::unique_ptr<juce::Component> control, const juce::String& caption, int width);
    /** In build(): the next cells form a group (a quiet divider before it; its caption, if any,
        small in the band over its first cell). `quiet`: a
        secondary group (configuration, not the sound's shaping): its captions and values a
        step lighter. */
    void group (const juce::String& caption, bool quiet = false);
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
        bool quiet = false;
    };
    struct Selector
    {
        juce::String caption;
        std::unique_ptr<juce::Component> control;
        juce::String over;   // (placed over this cell; empty: in the row)
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
    std::vector<Selector> selectors;
    std::vector<int> navRules;   // (x of the rules between the pages, the sub-pages and the selectors)
    std::unique_ptr<rackui::VerticalSwitch> power;
    std::unique_ptr<rackui::PowerKey> powerKeyPart;
    std::optional<Faceplate> faceplate;
    std::unique_ptr<juce::Component> railPart, overflow;
    std::unique_ptr<hardware::SaveKey> save;
    bool fullWidthGraph = false;
    int railPartWidth = 0;
    juce::String powerId;
    juce::Rectangle<int> railTitleArea;
    void paintFaceplate (juce::Graphics&);
    int contentRightMargin() const;
    juce::String powerWord;
    juce::Rectangle<int> powerWordArea, statusArea;
    std::unique_ptr<GraphFrame> graphView;
    std::vector<Placed> cells;
    std::vector<Group> groups;
};

} // namespace osp::plugin::rack
