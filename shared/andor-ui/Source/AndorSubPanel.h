#pragma once

#include <bitset>
#include <functional>

#include "InstrumentHost.h"
#include "ShapingPopups.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>

namespace osp::plugin
{

/**
    SUB's popover, V7 (docs/SUB_POPUP_V7.md): WAVE and a compact OCT on one line; one dark display
    (the wave's shape, the key response, the playable range on a keyboard); GRIT with a small
    valve that warms with it, LEVEL, and VOICES (POLY / MONO) over MONO NOTE (LOWEST / LATEST).
    The response (FLAT / TAPER, FROM, dB / OCT) is shown by the display only (no controls). Interface only: every control is one of SUB's parameters.
*/
namespace subview
{
    /** The relative loss SUB applies to `key` (dB, >= 0): SubOsc::keyGain without LEVEL. */
    double relativeLossDb (int key, bool taper, int taperStart, double slopeDbPerOctave) noexcept;
    bool isBlack (int key) noexcept;
    /** The keys the keyboard shows: C1..C8 when the range fits; else shifted, else 0..127. */
    struct Viewport
    {
        int first = 24, last = 108;
        bool operator== (const Viewport&) const = default;
    };
    Viewport fit (int lowKey, int highKey) noexcept;
    /** The keyboard's note names: the parameters' own convention (MIDI 60 = C4). */
    juce::String noteName (int key);
    /** SUB's parameter IDs (InstrumentHost::subFields(), then subVoicingFields()): each product
        names its own (ANDOR OSP "sub.<field>", ANDOR ANALOG "analog.sub.<field>"). */
    struct Ids
    {
        juce::String enabled, wave, octave, grit, level, lowKey, highKey, response, taperStart, taperSlope, voices, priority;
        static Ids from (const std::function<juce::String (const char* field)>& idOf);
    };
} // namespace subview

/**
    The dark display: one component (so the curve, its handles and the keys stay aligned at any
    scale), painted in layers: the well, a faint grid, the selected wave's shape (a still
    picture, not a meter), the playable range's tint, the response (bright where keys sound,
    faint where they would not), its two handles, the keyboard with the range's ends.

    Edits (each one host gesture, undoable): the range's ends and its span on the keyboard
    (arrow keys: the low end; with Shift, the high one). The response curve is a picture only. Everything it shows is read from the
    parameters, so knobs, automation, presets and undo move it at once.
*/
class SubResponseDisplay final : public juce::Component, public juce::SettableTooltipClient, private juce::Timer
{
public:
    /** Whether a key is down now (the instrument's keyboard state: played, from the host or the
        on-screen keys). Set by the panel; the keys held are lit on the keyboard, with their
        place on the curve, so the range and the response read against what is playing. */
    std::function<bool (int key)> isKeyDown;
    /** The keys shown as held (tests). */
    const std::bitset<128>& heldKeys() const noexcept { return held; }
    /** Reads the keys held now (the timer's work; tests call it directly). */
    void pollHeldKeys();
    SubResponseDisplay (InstrumentHost& host, juce::Colour accent, subview::Ids ids);
    ~SubResponseDisplay() override;

    enum class Handle { none, low, high, span, from, slope };

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress&) override;
    void focusGained (FocusChangeType) override { repaint(); }
    void focusLost (FocusChangeType) override { repaint(); }

    // State as drawn (tests, screenshots).
    int lowKey() const;
    int highKey() const;
    int taperStart() const;
    double slope() const;
    bool taper() const;
    int wave() const;
    subview::Viewport viewport() const;
    /** The dB the graph's height spans (a stepped rule, frozen while a handle is dragged). */
    double scaleDb() const;
    juce::Rectangle<float> graphArea() const noexcept { return graph; }
    juce::Rectangle<float> keybedArea() const noexcept { return bed; }
    /** A key on the keyboard (local coordinates; empty outside the viewport). */
    juce::Rectangle<float> keyArea (int key) const;
    /** The curve's height at a key (local y). */
    float curveY (int key) const;
    juce::Point<float> handlePosition (Handle) const;
    Handle handleAt (juce::Point<float>) const;
    Handle dragged() const noexcept { return drag; }
    /** The transient readout while a handle is dragged (empty otherwise). */
    juce::String readout() const;

private:
    struct Geometry;
    float keyCentre (int key) const;
    float lossToY (double lossDb, double scale) const;
    double yToLoss (float y, double scale) const;
    int keyAtX (float x) const;
    void beginDrag (Handle, juce::Point<float>);
    void endDrag();
    void rebuildWave();
    double currentScale() const;
    juce::RangedAudioParameter& parameter (const juce::String& id) const;

    InstrumentHost& host;
    juce::Colour accent;
    const subview::Ids ids;   ///< (before the attachments: they are made from it)
    juce::ParameterAttachment lowA, highA, startA, slopeA, responseA, waveA, enabledA;
    juce::Rectangle<float> graph, bed, labels;
    juce::Path wavePath;
    Handle drag = Handle::none, hover = Handle::none;
    // A drag keeps the picture still under the pointer: the viewport and the dB scale it began with.
    subview::Viewport frozenView;
    double frozenScale = 6.0;
    int spanLow0 = 0, spanHigh0 = 0, spanKey0 = 0;
    bool coincident = false;   ///< a press on two ends drawn together: the drag's direction picks one
    juce::Point<float> pressAt;
    std::bitset<128> held;
    void timerCallback() override { pollHeldKeys(); }
};

/**
    The valve beside GRIT: a small drawn glass valve whose filaments and glow follow GRIT (and
    dim with SUB off). A picture of the setting, never a meter: it changes only when the
    parameters do. Not a model of a tube: GRIT is a soft saturation stage.
*/
class SubGritValve final : public juce::Component
{
public:
    SubGritValve();
    /** `grit` 0..1; `enabled` SUB's switch. */
    void setAmount (float grit, bool enabled);
    /** The glow drawn (0..1): pow (grit, 0.8), dimmed while SUB is off. */
    float glow() const noexcept { return glowNow; }
    bool dimmed() const noexcept { return ! on; }
    void paint (juce::Graphics&) override;

private:
    float gritNow = 0.0f, glowNow = 0.0f;
    bool on = true;
};

class SubPanel final : public MiniPanel
{
public:
    /** `isKeyDown`: whether a key is held now (the instrument's keyboard state, before the
        arpeggiator); the display lights those keys. Empty: nothing is lit. */
    SubPanel (InstrumentHost& host, subview::Ids ids, std::function<bool (int key)> isKeyDown = {});
    ~SubPanel() override;

    static constexpr int width = 440;
    bool compact() const override { return true; }
    juce::Point<int> cardSize() const override;
    void paint (juce::Graphics&) override;
    void refreshContent() override;

    SubResponseDisplay& display() noexcept { return *responseDisplay; }
    SubGritValve& valve() noexcept { return *gritValve; }
    SegmentedControl& waveControl() noexcept { return *waveSegments; }
    SegmentedControl& octaveControl() noexcept { return *octaveSegments; }
    SegmentedControl& voicesControl() noexcept { return *voicesSegments; }
    SegmentedControl& priorityControl() noexcept { return *prioritySegments; }
    MiniKnob& gritKnob() noexcept { return *grit; }
    MiniKnob& levelKnob() noexcept { return *level; }

protected:
    void layoutContent (juce::Rectangle<int> area) override;

private:
    void updateDependents();
    InstrumentHost& host;
    juce::Colour accent;
    const subview::Ids ids;
    std::unique_ptr<SegmentedControl> waveSegments, octaveSegments, voicesSegments, prioritySegments;
    std::unique_ptr<SubResponseDisplay> responseDisplay;
    std::unique_ptr<SubGritValve> gritValve;
    std::unique_ptr<MiniKnob> grit, level;
    std::unique_ptr<juce::ParameterAttachment> gritA, enabledA, voicesA;
    juce::Rectangle<int> waveCaption, octCaption, voicesCaption, priorityCaption;
    juce::Rectangle<float> topDivider, leftDivider, rightDivider;
};

} // namespace osp::plugin
