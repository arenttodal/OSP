#pragma once

#include "Design.h"
#include "MainSections.h"
#include "PluginProcessor.h"
#include "ShapingPopups.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <memory>
#include <vector>

namespace osp::plugin
{

namespace modui
{
    /** The sources' restrained identities (0 LFO 1 sage, 1 LFO 2 blue, 2 ENV 1 amber, 3 ENV 2 rust,
        4 MOD WHEEL warm graphite). Source indices are mod::sourceIndex (lfo1 = 0 .. modWheel = 4). */
    juce::Colour sourceColour (int source);
    /** The same identity lifted for lines on the graphite displays. */
    juce::Colour sourceOnDark (int source);
    const char* sourceLabel (int source);
    /** The destination a control stands for (by its parameter ID), for a source of this scope:
        the CHARACTER macro is the shared CHARACTER stage for a global source and the per-voice
        cutoff for a per-voice one. Dest::none when the control is not a destination. */
    mod::Dest destinationFor (const juce::String& parameterId, bool polySource);
    /** A source (0..4) acts per voice (the envelopes; a POLY LFO). */
    bool isPerVoice (const OspAudioProcessor& processor, int source);
    /** Every destination a control shows (CHARACTER: the macro and the cutoff). */
    std::vector<mod::Dest> destinationsShownBy (const juce::String& parameterId);
    /** The control's parameter ID for a destination (where to show its halo), or empty. */
    juce::String parameterFor (mod::Dest dest);
    /** A route's name: "ENV 1 → CHARACTER CUTOFF"; a depth route's "LFO 1 → DEPTH OF ENV 1 → ..."
        (withSource false: without its own source). */
    juce::String routeTitle (const OspAudioProcessor& processor, const OspAudioProcessor::ModRouteInfo& info, bool withSource = true);
    /** The panel's tabs: 0 AMP, 1 ENV 1, 2 ENV 2, 3 LFO 1, 4 LFO 2; their sources (-1 AMP). */
    constexpr int tabCount = 5;
    int sourceOfTab (int tab) noexcept;
    int tabOfSource (int source) noexcept;
}

/**
    The modulation display: the selected source's waveform or envelope in a dark well, with a
    thin playhead from the audio thread's own state (the global LFO, or the newest sounding
    voice for a per-voice source). The LFO's CUSTOM shape and the envelope's ONE SHOT curve are
    edited here: drag a point, double-click to add or remove one, drag a segment to bend it.
    Compact (the AMP / ENV / LFO panel): a slim label, no hints; the full editor opens in a
    popover. A right-click (or a click on the polarity tag) asks for the context menu.
*/
class ModCurveView final : public juce::Component
{
public:
    explicit ModCurveView (OspAudioProcessor& processor);
    void setSource (int source);
    int source() const noexcept { return sourceIndex; }
    void setCompact (bool shouldBeCompact);
    /** Follows the parameters and the playhead (editor timer). */
    void refresh();
    bool isEditable() const;

    /** Right-click / the polarity tag: the panel shows the source's menu. */
    std::function<void()> onContextMenu;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

    juce::Rectangle<float> plotArea() const;
    juce::Rectangle<float> resetArea() const;
    juce::Rectangle<float> polarityArea() const;

private:
    int curveIndex() const noexcept { return sourceIndex; }   ///< the processor's curves: 0, 1 the LFOs' CUSTOM; 2, 3 the envelopes' ONE SHOT
    juce::Point<float> toScreen (float x, float y) const;
    juce::Point<float> fromScreen (juce::Point<float> p) const;
    int pointAt (juce::Point<float> p) const;
    void paintLfo (juce::Graphics&, juce::Rectangle<float> plot, juce::Colour colour);
    void paintEnvelope (juce::Graphics&, juce::Rectangle<float> plot, juce::Colour colour);
    void paintCurvePoints (juce::Graphics&, juce::Colour colour);

    OspAudioProcessor& ospProcessor;
    int sourceIndex = 0;
    bool compact = false;
    mod::Curve curve, before;
    int dragPoint = -1, dragSegment = -1, hoverPoint = -1;
    float dragStartTension = 0.0f;
    juce::Point<float> dragStart;
    bool dragging = false;
    // What was last drawn (repaint only on change).
    std::array<float, 24> shownSignature {};
    float shownPlayhead = -1.0f, shownValue = 0.0f;
};

/** One source's compact controls, in one row (the AMP envelope's knob row). LFO: SHAPE,
    MODE, RATE (its caption chooses Sync or Hz; the knob shows the division or the Hz),
    PHASE, SCOPE; polarity lives in the source's context menu. ENV: MODE, then ATTACK DECAY
    SUSTAIN RELEASE CURVE, or LENGTH for a one-shot curve. Bound to the source's parameters. */
class ModSourcePage final : public juce::Component
{
public:
    ModSourcePage (OspAudioProcessor& processor, int source);
    void refresh();
    void resized() override;
    /** The RATE caption's choice (tests, the panel). */
    juce::PopupMenu rateMenu();
    MiniKnob* rateKnob() const noexcept { return knobs.empty() ? nullptr : knobs[0].get(); }
    MiniKnob* divisionKnob() const noexcept { return division.get(); }

private:
    OspAudioProcessor& ospProcessor;
    int source;
    bool isLfo() const noexcept { return source < 2; }
    std::unique_ptr<ValueSelector> shape, mode, scope;
    std::unique_ptr<MiniKnob> division;
    std::vector<std::unique_ptr<MiniKnob>> knobs;   ///< LFO: rate, phase; ENV: attack, decay, sustain, release, curve, length
    bool shownSync = false, shownOneShot = false, laidOut = false;
};

/** A route's depth as a thin bipolar bar (drag; double-click: 0), bound to its slot's depth. */
class ModDepthBar final : public juce::Component, public juce::SettableTooltipClient
{
public:
    ModDepthBar (OspAudioProcessor& processor, int slot, juce::Colour colour);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

private:
    juce::Colour colour;
    float depth = 0.0f, dragFrom = 0.0f;
    juce::ParameterAttachment attachment;
};

/**
    The AMP envelope's panel, now shared: five quiet tabs (AMP, ENV 1, ENV 2, LFO 1, LFO 2)
    over the same display and knob row. AMP is the instrument's amplitude envelope exactly as
    before; the others are the modulation sources' editors. One shows at a time; which one is
    a view setting only (every source runs whichever is shown). A source's tab is also its
    drag handle: dragged a few pixels it becomes a cable onto a control (a route). Its route
    count (top right) opens the source's routes; an editable curve has an expand key for the
    larger editor.
*/
class ModulationPanel final : public juce::Component
{
public:
    explicit ModulationPanel (OspAudioProcessor& processor);

    std::function<void (int tab)> onTabChanged;
    /** A source tab dragged (screen position) / released there; the editor finds the target. */
    std::function<void (int source, juce::Point<int> screen)> onDragMove;
    std::function<void (int source, juce::Point<int> screen)> onDragEnd;
    std::function<void (int source)> onShowRoutes;    ///< the route count was clicked
    std::function<void (int source)> onExpandCurve;   ///< the expand key (an editable curve)

    void refresh();
    int tab() const noexcept { return currentTab; }
    void showTab (int tab);
    /** The shown source (-1: AMP). */
    int selectedSource() const noexcept { return modui::sourceOfTab (currentTab); }
    EnvelopePanel& ampEnvelope() noexcept { return amp; }
    ModCurveView& curveView() noexcept { return curve; }
    ModSourcePage& page (int source) noexcept { return *pages[static_cast<std::size_t> (juce::jlimit (0, 3, source))]; }
    juce::Rectangle<float> tabArea (int tab) const;
    /** A source tab's drag socket (empty for AMP). */
    juce::Rectangle<float> socketArea (int tab) const;
    juce::Rectangle<float> routesArea() const { return routesKey; }
    juce::Rectangle<float> expandArea() const { return expandKey; }
    /** The source's context menu (polarity for an LFO, the curve's tools, its routes). */
    juce::PopupMenu sourceMenu (int source);

    void resized() override;
    /** The tab row's mouse (the strip forwards it; tests call it directly), in panel coordinates. */
    void tabMouseDown (const juce::MouseEvent&);
    void tabMouseDrag (const juce::MouseEvent&);
    void tabMouseUp (const juce::MouseEvent&);
    void tabMouseMove (juce::Point<float> at);

private:
    /** The tab row over the panel's top (the AMP envelope fills the panel under it). */
    struct Strip final : juce::Component, juce::SettableTooltipClient
    {
        explicit Strip (ModulationPanel& p) : panel (p) {}
        void paint (juce::Graphics& g) override { panel.paintTabs (g); }
        void mouseDown (const juce::MouseEvent& e) override { panel.tabMouseDown (e); }
        void mouseDrag (const juce::MouseEvent& e) override { panel.tabMouseDrag (e); }
        void mouseUp (const juce::MouseEvent& e) override { panel.tabMouseUp (e); }
        void mouseMove (const juce::MouseEvent& e) override { panel.tabMouseMove (e.position); }
        void mouseExit (const juce::MouseEvent&) override { panel.tabMouseMove ({ -100.0f, -100.0f }); }
        ModulationPanel& panel;
    };
    void paintTabs (juce::Graphics&);
    static void paintSocket (juce::Graphics&, juce::Point<float> centre, juce::Colour colour, float strength, bool inUse);
    int routeCount (int source, bool activeOnly) const;
    void layoutTabs();

    OspAudioProcessor& ospProcessor;
    EnvelopePanel amp;
    ModCurveView curve;
    std::array<std::unique_ptr<ModSourcePage>, 4> pages;
    Strip strip { *this };
    int currentTab = 0, hoverTab = -1, pressTab = -1, dragSource = -1;
    bool hoverRoutes = false, hoverExpand = false;
    std::array<juce::Rectangle<float>, modui::tabCount> tabs {};
    std::array<juce::Rectangle<float>, modui::tabCount> sockets {};   ///< a source tab's drag socket (AMP: none)
    int hoverSocket = -1;
    bool pressedSocket = false;
    juce::Rectangle<float> routesKey, expandKey;
    std::array<int, 8> shownRouteCounts {};
    bool shownEditable = false;
    juce::Point<float> pressAt;
};

/** A source's routes (the panel's route count): destination, depth, bypass and remove per
    route, + ADD for one without dragging. A click on a row selects that route (its halo
    and its control are highlighted). A compact popover above the panel. */
class ModRoutesPopup final : public MiniPanel
{
public:
    ModRoutesPopup (OspAudioProcessor& processor, int source);
    std::function<void (int slot)> onSelectRoute;
    void setSelectedSlot (int slot);
    int source() const noexcept { return sourceIndex; }
    int rowCount() const noexcept { return static_cast<int> (rows.size()); }
    juce::PopupMenu addMenu();

    juce::Point<int> cardSize() const override;
    bool compact() const override { return true; }
    void refreshContent() override;
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;

protected:
    void layoutContent (juce::Rectangle<int> area) override;

private:
    struct Row final : juce::Component
    {
        Row (ModRoutesPopup& owner, const OspAudioProcessor::ModRouteInfo& info);
        void paint (juce::Graphics&) override;
        void resized() override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseMove (const juce::MouseEvent&) override { repaint(); }
        void mouseExit (const juce::MouseEvent&) override { repaint(); }
        juce::Rectangle<float> bypassArea() const;
        juce::Rectangle<float> removeArea() const;
        ModRoutesPopup& owner;
        OspAudioProcessor::ModRouteInfo info;
        juce::String title;
        ModDepthBar depth;
    };
    void rebuild();

    OspAudioProcessor& ospProcessor;
    int sourceIndex;
    int selectedSlot = -1;
    std::vector<std::unique_ptr<Row>> rows;
    juce::String shownRoutes;
    juce::Rectangle<float> addKey, listArea;
};

/** The larger curve editor (an LFO's CUSTOM shape, an envelope's ONE SHOT curve): the same
    display with its points, hints and RESET, in a popover; the instrument never grows. */
class ModCurvePopup final : public MiniPanel
{
public:
    ModCurvePopup (OspAudioProcessor& processor, int source);
    juce::Point<int> cardSize() const override { return { 640, 330 }; }
    bool compact() const override { return true; }
    void refreshContent() override { view.refresh(); }
    ModCurveView& curveView() noexcept { return view; }

protected:
    void layoutContent (juce::Rectangle<int> area) override { view.setBounds (area); }

private:
    ModCurveView view;
};

/**
    The modulation halos over the instrument. A modulated control gets a second ring well
    outside its own: the selected route's sweep (from the base value, in the destination's
    own mapping, clamped where the parameter ends) in the source's colour, other routes on the
    same control as faint thin arcs just outside it, and a small marker where it is now.
    Hovering the ring reveals the full guide track of the knob's sweep, the route's endpoint
    handle and a short readout (route, depth, base, range); dragging it (up / down, Shift
    fine; Option-drag on the knob itself) changes that route's depth only, never the base
    value. A right-click offers the route's actions. While a source is dragged, the controls
    it can reach show a faint ring and the one under the pointer a strong one. Takes clicks
    only on the rings (and the knob with Option held).
*/
class ModulationOverlay final : public juce::Component
{
public:
    ModulationOverlay (OspAudioProcessor& processor, juce::Component& root);

    /** A route or source chosen here (a halo pressed, "Select"): the editor's one selection. */
    std::function<void (int source, int slot)> onSelect;
    std::function<void (int source)> onShowRoutes;

    void refresh();
    /** Drag assignment: the source being dragged (-1: none) and where (root coordinates). */
    void setDrag (int source, juce::Point<float> where);
    /** The control under a point (root coordinates), the front-most, or nullptr. */
    juce::Slider* controlAt (juce::Point<float> where) const;
    /** The editor's selection: the source shown in the panel and the chosen route (-1: none). */
    void setSelection (int source, int slot);
    /** Rings drawn now (tests). */
    int ringCount() const noexcept { return shownRings; }
    /** The halo of a control (root coordinates): its radius and centre (tests drive the
        mouse there), or a zero radius when it has none. */
    juce::Point<float> haloCentre (const juce::String& parameterId) const;
    float haloRadius (const juce::String& parameterId) const;
    /** The route a control's halo edits now (-1: none). */
    int haloRoute (const juce::String& parameterId) const;
    /** The readout the halo shows (route, depth, base, range). */
    juce::StringArray haloReadout (const juce::String& parameterId) const;
    bool isHovering() const noexcept { return hover >= 0; }
    bool isEditing() const noexcept { return editSlot >= 0; }
    /** A destination's right-click menu: its routes (select, bypass, depth to 0, remove),
        "Assign <source>" for every source the rules allow, "Remove All Assignments". */
    juce::PopupMenu contextMenu (const juce::String& parameterId);
    /** Assigns through the processor's one path (source 0..4); selects the route, or shows why
        it could not be made. Returns the route's slot, -1 when refused. */
    int assign (int source, mod::Dest dest);
    /** A short message by the controls (a refused assignment), for a few seconds. */
    void showNotice (const juce::String& text);
    juce::String currentNotice() const;

    /** META-MODULATION. What a point is over (root coordinates): a halo's route - its ring
        band, the route's depth - several routes where their arcs lie together (a chooser),
        or the control itself (its value). */
    struct HaloHit
    {
        int target = -1;          ///< the control (index among the collected targets)
        juce::String parameterId;
        int slot = -1;            ///< the route whose depth the ring stands for
        std::vector<int> routes;  ///< more than one route there: ask which
        bool body = false;        ///< the control itself
        bool halo() const noexcept { return slot >= 0 || ! routes.empty(); }
    };
    HaloHit hitAt (juce::Point<float> where) const;
    /** A source dropped there: on a halo it modulates that route's depth (a chooser when the
        arcs lie together), on the control its value; never one for the other. The route's
        slot (made or selected), -1 when nothing was made (refused, or the chooser is open). */
    int drop (int source, juce::Point<float> where);
    /** `source` (0..4) on the depth of the route in `slot`, through the processor's one path. */
    int assignDepth (int source, int slot);
    /** A halo's right-click: the route, its depth, MODULATE THIS DEPTH (every source the rules
        allow), EXISTING DEPTH MODULATION, Remove Depth Modulation. */
    juce::PopupMenu depthMenu (int slot);
    /** Which route's depth (arcs lying together). */
    juce::PopupMenu depthChooser (int source, const std::vector<int>& slots);
    /** The depth routes of a route. */
    std::vector<const OspAudioProcessor::ModRouteInfo*> depthRoutesOf (int slot) const;
    /** A route's depth now (moved by its depth routes, as the engine takes it) and the range
        its depth routes can take it through; base depth when none. */
    double effectiveDepth (int slot) const;
    juce::Range<double> effectiveDepthRange (int slot) const;
    /** While a source is dragged: what a drop there would make (or why not). */
    juce::String dragTip() const;

    void paint (juce::Graphics&) override;
    bool hitTest (int x, int y) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    struct Target
    {
        juce::Component::SafePointer<juce::Slider> slider;
        juce::String parameterId;
        juce::Point<float> centre;
        float radius = 0.0f;              ///< the halo's ring
        float bodyRadius = 0.0f;          ///< the knob's own reach (Option-drag)
        float start = 0.0f, end = 0.0f;   ///< the knob's angles
        juce::Rectangle<float> area;      ///< in the root's coordinates
        bool field = false;               ///< a value field (EQ): an underline instead of a ring
        juce::Rectangle<float> avoid;     ///< a caption the ring breaks around (root coordinates)
    };
    struct Sweep
    {
        float lo = 0.0f, hi = 0.0f, base = 0.0f, handle = 0.0f, now = 0.0f;
        bool nowKnown = false;
        float effective = 0.0f;   ///< where the depth routes take the handle now
        int metaSource = -1;      ///< the first depth route's source (its accent), -1: none
    };
    void collect (juce::Component& c);
    float positionOf (const Target& t, double offset, mod::Dest dest) const;
    std::vector<const OspAudioProcessor::ModRouteInfo*> routesOf (const Target& t) const;
    const OspAudioProcessor::ModRouteInfo* emphasised (const Target& t) const;
    Sweep sweepOf (const Target& t, const OspAudioProcessor::ModRouteInfo& route) const;
    int targetAt (juce::Point<float> p, bool includeBody, bool anyControl = false) const;
    const OspAudioProcessor::ModRouteInfo* routeInSlot (int slot) const;
    /** A source's value now as a destination hears it (a shared stage: the global envelope). */
    double valueNow (int source, mod::Dest dest, bool& known) const;
    juce::Range<double> sourceRange (int source) const;
    /** The depth route being edited on this control's halo (the selection), or nullptr. */
    const OspAudioProcessor::ModRouteInfo* selectedDepthRoute (const Target& t) const;
    juce::Rectangle<float> dragTipArea() const;
    void paintDragTarget (juce::Graphics&, const Target& t, int index, const HaloHit& hit);
    juce::String notice;
    juce::uint32 noticeUntil = 0;
    juce::Rectangle<float> noticeAnchor;
    juce::Rectangle<float> noticeArea() const;
    const Target* find (const juce::String& parameterId) const;
    juce::Rectangle<float> readoutArea (const Target& t) const;
    juce::Rectangle<int> repaintArea (const Target& t) const;
    void paintHalo (juce::Graphics&, const Target& t, int index);

    OspAudioProcessor& ospProcessor;
    juce::Component& root;
    std::vector<Target> targets;
    std::vector<juce::Rectangle<int>> shownAreas;
    std::vector<OspAudioProcessor::ModRouteInfo> routes;
    InstrumentEngine::ModView view;
    int dragSource = -1, selectedSource = -1, selectedSlot = -1, shownRings = 0;
    int hover = -1;
    juce::Point<float> dragPoint;
    int editSlot = -1, editTarget = -1;
    float editFrom = 0.0f;
    juce::RangedAudioParameter* editParameter = nullptr;
};

} // namespace osp::plugin
