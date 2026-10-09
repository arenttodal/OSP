#pragma once

#include "Design.h"
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
    /** The four sources' restrained identities (0 LFO 1 sage, 1 LFO 2 blue, 2 ENV 1 amber, 3 ENV 2 rust). */
    juce::Colour sourceColour (int source);
    /** The same identity lifted for lines on the graphite displays. */
    juce::Colour sourceOnDark (int source);
    /** The destination a control stands for (by its parameter ID), for a source of this scope:
        the CHARACTER macro is the shared CHARACTER stage for a global source and the per-voice
        cutoff for a per-voice one. Dest::none when the control is not a destination. */
    mod::Dest destinationFor (const juce::String& parameterId, bool polySource);
    /** Every destination a control shows (CHARACTER: the macro and the cutoff). */
    std::vector<mod::Dest> destinationsShownBy (const juce::String& parameterId);
    /** The control's parameter ID for a destination (where to show its ring), or empty. */
    juce::String parameterFor (mod::Dest dest);
}

/**
    The modulation display: the selected source's waveform or envelope in a dark well, with a
    thin playhead from the audio thread's own state (the global LFO, or the newest sounding
    voice for a per-voice source). The LFO's CUSTOM shape and the envelope's ONE SHOT curve are
    edited here: drag a point, double-click to add or remove one, drag a segment to bend it.
*/
class ModCurveView final : public juce::Component
{
public:
    explicit ModCurveView (OspAudioProcessor& processor);
    void setSource (int source);
    /** Follows the parameters and the playhead (editor timer). */
    void refresh();
    bool isEditable() const;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

    juce::Rectangle<float> plotArea() const;
    juce::Rectangle<float> resetArea() const;

private:
    int curveIndex() const noexcept { return source; }   ///< the processor's curves: 0, 1 the LFOs' CUSTOM; 2, 3 the envelopes' ONE SHOT
    juce::Point<float> toScreen (float x, float y) const;
    juce::Point<float> fromScreen (juce::Point<float> p) const;
    int pointAt (juce::Point<float> p) const;
    void paintLfo (juce::Graphics&, juce::Rectangle<float> plot, juce::Colour colour);
    void paintEnvelope (juce::Graphics&, juce::Rectangle<float> plot, juce::Colour colour);
    void paintCurvePoints (juce::Graphics&, juce::Colour colour);

    OspAudioProcessor& ospProcessor;
    int source = 0;
    mod::Curve curve, before;
    int dragPoint = -1, dragSegment = -1, hoverPoint = -1;
    float dragStartTension = 0.0f;
    juce::Point<float> dragStart;
    bool dragging = false;
    // What was last drawn (repaint only on change).
    std::array<float, 24> shownSignature {};
    float shownPlayhead = -1.0f, shownValue = 0.0f;
};

/** One source's controls (LFO: shape, mode, timing, polarity, scope, rate or division, phase;
    ENV: mode, ADSR and curve, or the one-shot's length). Bound to the source's parameters. */
class ModSourcePage final : public juce::Component
{
public:
    ModSourcePage (OspAudioProcessor& processor, int source);
    void refresh();
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    OspAudioProcessor& ospProcessor;
    int source;
    bool isLfo() const noexcept { return source < 2; }
    std::unique_ptr<ValueSelector> shape, mode, division;
    std::unique_ptr<SegmentedControl> timing, polarity, scope;
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
    The modulation bay: an expansion module on the right of the instrument. From the top:
    MODULATION; the four sources as tabs (click: edit it; drag its handle onto a control: a
    route); the selected source's display and controls; the routing list (+ ADD for routes
    without dragging; per row the depth, a bypass light and remove).
*/
class ModulationBay final : public juce::Component
{
public:
    explicit ModulationBay (OspAudioProcessor& processor);
    ~ModulationBay() override;

    /** A source tab is being dragged (screen position) / was released (commit true: over the
        instrument, assign if possible). The editor finds the target. */
    std::function<void (int source, juce::Point<int> screen)> onDragMove;
    std::function<void (int source, juce::Point<int> screen)> onDragEnd;
    /** A route row was selected (its destination highlights in the instrument), or none. */
    std::function<void (mod::Dest)> onRouteSelected;

    void refresh();
    int selectedSource() const noexcept { return selected; }
    void selectSource (int source);
    /** The routes as listed (tests, snapshots). */
    int routeRowCount() const noexcept { return static_cast<int> (rows.size()); }
    void selectRoute (int slot);
    int selectedRoute() const noexcept { return selectedSlot; }
    juce::Rectangle<float> tabArea (int source) const;
    juce::Rectangle<float> addArea() const { return addButton; }
    juce::PopupMenu addMenu();

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

private:
    struct Row final : juce::Component
    {
        Row (ModulationBay& bay, const OspAudioProcessor::ModRouteInfo& info);
        void paint (juce::Graphics&) override;
        void resized() override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseMove (const juce::MouseEvent&) override { repaint(); }
        void mouseExit (const juce::MouseEvent&) override { repaint(); }
        juce::Rectangle<float> bypassArea() const;
        juce::Rectangle<float> removeArea() const;
        ModulationBay& bay;
        OspAudioProcessor::ModRouteInfo info;
        ModDepthBar depth;
    };
    void rebuildRows();
    void paintTab (juce::Graphics&, int source);

    OspAudioProcessor& ospProcessor;
    int selected = 0;
    ModCurveView curveView;
    std::array<std::unique_ptr<ModSourcePage>, 4> pages;
    juce::Viewport routeViewport;
    juce::Component routeContent;
    std::vector<std::unique_ptr<Row>> rows;
    juce::String shownRoutes;   ///< the list's signature (rebuilt when routes change)
    int selectedSlot = -1;
    juce::Rectangle<float> header, routingHeader, addButton;
    std::array<float, 4> shownValues {};
    int dragSource = -1, hoverTab = -1;
    bool hoverAdd = false;
    juce::Point<int> pressAt;
};

/**
    The modulation indicators over the instrument: a thin ring outside a modulated control's
    own arc (the range the routes sweep, a small marker where it is now), in the source's
    colour (one source) or a quiet neutral (several). While a source is dragged the controls it
    can reach show a faint ring and the one under the pointer a stronger one. Dragging a ring
    edits the depth of the route it shows (the selected source's first). Lives on top of the
    instrument and takes clicks only on the rings.
*/
class ModulationOverlay final : public juce::Component
{
public:
    ModulationOverlay (OspAudioProcessor& processor, juce::Component& root);

    void refresh();
    /** Drag assignment: the source being dragged (-1: none) and where (root coordinates). */
    void setDrag (int source, juce::Point<float> where);
    /** The control under a point (root coordinates), the front-most, or nullptr. */
    juce::Slider* controlAt (juce::Point<float> where) const;
    void setSelectedSource (int source)
    {
        if (source != selectedSource)
        {
            selectedSource = source;
            repaint();
        }
    }
    void setHighlight (mod::Dest dest)
    {
        if (dest != highlight)
        {
            highlight = dest;
            repaint();
        }
    }
    /** Rings drawn now (tests). */
    int ringCount() const noexcept { return shownRings; }

    void paint (juce::Graphics&) override;
    bool hitTest (int x, int y) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    struct Target
    {
        juce::Component::SafePointer<juce::Slider> slider;
        juce::String parameterId;
        juce::Point<float> centre;
        float radius = 0.0f;
        float start = 0.0f, end = 0.0f;   ///< the knob's angles
        juce::Rectangle<float> area;      ///< in the root's coordinates
        bool field = false;               ///< a value field (EQ): an underline instead of a ring
    };
    void collect (juce::Component& c);
    float positionOf (const Target& t, double offset, mod::Dest dest) const;
    int routeFor (const Target& t) const;
    const Target* ringAt (juce::Point<float> p) const;

    OspAudioProcessor& ospProcessor;
    juce::Component& root;
    std::vector<Target> targets;
    std::vector<juce::Rectangle<int>> shownAreas;
    std::vector<OspAudioProcessor::ModRouteInfo> routes;
    InstrumentEngine::ModView view;
    int dragSource = -1, selectedSource = -1, shownRings = 0;
    juce::Point<float> dragPoint;
    mod::Dest highlight = mod::Dest::none;
    int editSlot = -1;
    float editFrom = 0.0f;
    juce::RangedAudioParameter* editParameter = nullptr;
};

} // namespace osp::plugin
