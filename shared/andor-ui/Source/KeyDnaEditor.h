#pragma once

#include "Design.h"
#include "InstrumentHost.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>

namespace osp::plugin
{

/**
    KEY DNA (unified engine Phase 5): how a layer's Granular voice differs from key to key, in
    the band over the instrument (the Library's place) with the real keyboard live below it
    and the profile drawn over the very keys it belongs to (08-gui-proposals.md, state 3).

    - The top row: the layer's KEY DNA on / off, AMOUNT (the host parameter
      layerX.keyDna.amount: live on sounding notes, automatable), INDIVIDUALITY (Smooth ..
      Individual), the SEED and RANDOMIZE, close.
    - One parameter at a time (POS SIZE DENS SPREAD TUNE SCATTER PAN): its curve over the
      keys (drag a point, double-click to add or remove one, drag the line to bend it, as the
      other curve editors), its DEPTH and VARIATION.
    - A key by hand: drag its bar (away from the curve) and the key plays that value; a stroke
      across keys draws each one it passes. Option-click (or right-click) returns a key to the
      curve and its variation.
    - Under every key, the profile the layer plays: the computed table (the same deterministic
      computation the processor publishes) at the current AMOUNT, held keys marked; below, the
      seven parameters at a glance.

    Drags are heard as they move and become one undo step when released. Depends only on
    InstrumentHost.
*/
class KeyDnaEditor final : public juce::Component, public juce::SettableTooltipClient
{
public:
    KeyDnaEditor (InstrumentHost& host, int layer);
    ~KeyDnaEditor() override;

    std::function<void()> onClose;
    /** Where a key is drawn, in this component's coordinates (the real keyboard's key; empty
        when it is not shown), and whether it is held. Unset: the keys spread evenly. */
    std::function<juce::Rectangle<float> (int note)> keyBounds;
    std::function<bool (int note)> isKeyDown;

    int layer() const noexcept { return layerIndex; }
    void refresh();

    keydna::Param parameter() const noexcept { return shownParam; }
    void showParameter (keydna::Param p);
    /** Tests: the controls by name ("power", "randomize", "reset", "param.<id>"), and the
        value fields ("amount", "individuality", "depth", "variation"). */
    juce::Rectangle<float> controlArea (const juce::String& name) const;
    juce::Rectangle<float> graphArea() const { return graph; }
    /** The x of a key's centre in the graph (the keyboard's when known). */
    float xForNote (double note) const;
    double noteForX (float x) const;
    /** The offset the layer plays at a key for the shown parameter, -1..1 of its half range (AMOUNT applied). */
    double shownOffset (int note) const;
    /** The shown parameter's key drawn by hand (its own value, not the curve's). */
    bool isDrawn (int note) const;
    /** The key a point in the graph stands for (the nearest shown key). */
    int keyAt (juce::Point<float> p) const;
    /** The unit value (-1..1, before AMOUNT) a height in the graph draws. */
    double unitAt (float y) const;
    void setValue (const juce::String& field, double value, bool final);
    double value (const juce::String& field) const;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    struct Field
    {
        juce::String name, caption;
        juce::Rectangle<float> area;
        double minimum = 0.0, maximum = 1.0, reset = 0.0;
    };
    juce::String fieldText (const Field&) const;
    const Field* fieldAt (juce::Point<float>) const;
    juce::Point<float> toScreen (float x, float y) const;
    juce::Point<float> fromScreen (juce::Point<float> p) const;
    int pointAt (juce::Point<float> p) const;
    bool nearCurve (juce::Point<float> p) const;
    void drawKeys (int from, double fromValue, int to, double toValue);
    void returnKeys (int key);   ///< one key (or all, -1) back to the curve, one undo step
    void recompute();
    void paintGraph (juce::Graphics&);
    void paintOverview (juce::Graphics&);
    void drawKey (juce::Graphics&, juce::Rectangle<float> r, const juce::String& text, bool on, bool hover, juce::Colour accent);

    InstrumentHost& host;
    const int layerIndex;
    juce::RangedAudioParameter* amountParameter = nullptr;
    keydna::Settings settings;
    keydna::Table table;
    keydna::Param shownParam = keydna::Param::position;
    mod::Curve curve;
    double amount = 1.0;
    std::array<bool, 128> held {};

    std::vector<Field> fields;
    juce::Rectangle<float> header, power, randomize, reset, closeArea, graph, overview, note;
    std::array<juce::Rectangle<float>, keydna::paramCount> tabs {};
    juce::String hoverControl;

    // A gesture.
    const Field* dragField = nullptr;
    double dragFrom = 0.0;
    juce::Point<float> dragStart;
    int dragPoint = -1, dragSegment = -1;
    float dragStartTension = 0.0f;
    bool dragging = false;
    // Drawing keys: armed by a press away from the curve, drawing once the pointer moves.
    int strokeKey = -1;
    double strokeValue = 0.0;
    bool drawArmed = false, drawingKeys = false;
};

} // namespace osp::plugin
