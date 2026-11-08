#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <vector>

namespace osp::plugin::rackui
{

/**
    The effects racks' approved ceramic controls (ANDOR_APPROVED_EFFECTS spec 2.1 - 2.3; the
    geometry and colours are the approved rack HTML's, in reference pixels). Opt-in: only the
    racks build them, so ANDOR OSP's own panels are untouched.

    Every control is bound to a host parameter: the parameter is the truth, the control draws
    where it is and changes it only as one complete, undoable gesture (never while painting).
*/

/** One position of a switch: its label, its spoken name and the parameter's plain value. */
struct Position
{
    juce::String label, spoken;
    float value = 0.0f;
};

/** The approved colours (rack HTML), shared by the controls and the racks' cells. */
namespace palette
{
    const juce::Colour caption { 0xff74716c }, value { 0xff4b4945 };
    const juce::Colour switchLabel { 0xff79736e }, switchLabelTwo { 0xff7c7871 }, switchActive { 0xff4b697a };
    const juce::Colour trackEdge { 0xffc7bfb2 }, trackDark { 0xff18191a }, trackMid { 0xff393535 };
    const juce::Colour actuatorHi { 0xfffffdf4 }, actuatorMid { 0xffdad2c6 }, actuatorLo { 0xffb6aea4 }, actuatorEdge { 0xffddd5cb };
    const juce::Colour miniBody { 0xff322f2d }, miniEdge { 0xffb3aca2 }, miniThumbHi { 0xfffaf6ed }, miniThumbLo { 0xffd6cfc4 };
    const juce::Colour segmentFill { 0xffeee9e0 }, segmentEdge { 0xffc8bdb0 }, segmentDivider { 0xffcfc6ba }, segmentText { 0xff79736d };
    const juce::Colour segmentActive { 0xffccdee4 }, segmentActiveText { 0xff476a7d };
    const juce::Colour laneFill { 0xffeeeae3 }, laneEdge { 0xffcec6bb }, laneText { 0xff77716a };
    const juce::Colour laneActiveFill { 0xffd9e5ea }, laneActiveEdge { 0xff8cabbc }, laneActiveText { 0xff3b5868 };
    const juce::Colour ledLive { 0xff6798b2 }, ledOff { 0xffb8b2a9 };
    const juce::Colour focus { 0xff90adbf };
    /** A module's active segment: MOVEMENT's approved tint, the same lightness and
        saturation in another module's hue (ECHO teal, SPACE violet, FILTER green, MORPH copper). */
    juce::Colour activeFill (juce::Colour accent);
    juce::Colour activeText (juce::Colour accent);
}

/**
    The ceramic miniature slide switch: a dark recessed slot and a raised warm-ivory
    actuator whose height is the value. Two positions: the labels above and below (FREE /
    SYNC, ON / OFF); three: the labels to the right, on the actuator's levels (MONO / PING /
    WIDE). Exactly two or three detents. A click on the slot or a label selects; a drag
    snaps; arrow keys step, Home / End go to the ends.

    `mini`: the rack header's lane switch (16 x 26, its ON label beside it, drawn by the header).
*/
class VerticalSwitch final : public juce::Component, public juce::SettableTooltipClient
{
public:
    enum class Style
    {
        slot,
        mini
    };
    VerticalSwitch (juce::RangedAudioParameter& parameter, std::vector<Position> positions, juce::UndoManager* undo, Style style = Style::slot);

    int positionIndex() const noexcept { return shown; }
    int positionCount() const noexcept { return static_cast<int> (positions.size()); }
    /** Selects a position as the user would (one undoable gesture). */
    void choose (int index);
    std::function<void (int index)> onChange;

    /** The slot (component coordinates) and where a position's actuator sits / label reads. */
    juce::Rectangle<float> trackBounds() const;
    juce::Rectangle<float> actuatorBounds (int index) const;
    juce::Rectangle<float> labelBounds (int index) const;
    /** The size the approved layout gives it (reference pixels). */
    static juce::Point<int> preferredSize (int positions, Style style);

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress&) override;
    void focusGained (FocusChangeType) override { repaint(); }
    void focusLost (FocusChangeType) override { repaint(); }
    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override;

private:
    int indexForValue (float plain) const;
    int indexAt (juce::Point<float> p) const;
    juce::RangedAudioParameter& parameter;
    std::vector<Position> positions;
    juce::UndoManager* undoManager;
    Style style;
    int shown = 0;
    juce::ParameterAttachment attachment;
};

/**
    The ceramic segmented switch for a few short exclusive choices (SHAPER's VOL / FILTER /
    BOTH, LADDER / OTA, IMPRINT / FUSE / WEAVE): one cream key, engraved dividers, the chosen
    segment a subtle tint of the module's colour, slightly pressed in.
*/
class SegmentedSwitch final : public juce::Component, public juce::SettableTooltipClient
{
public:
    SegmentedSwitch (juce::RangedAudioParameter& parameter, std::vector<Position> positions, juce::UndoManager* undo, juce::Colour accent);
    int positionIndex() const noexcept { return shown; }
    void choose (int index);
    juce::Rectangle<float> segmentBounds (int index) const;
    std::function<void (int index)> onChange;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress&) override;
    void focusGained (FocusChangeType) override { repaint(); }
    void focusLost (FocusChangeType) override { repaint(); }
    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override;

private:
    int indexForValue (float plain) const;
    juce::RangedAudioParameter& parameter;
    std::vector<Position> positions;
    juce::UndoManager* undoManager;
    juce::Colour fill, text;
    int shown = 0;
    juce::ParameterAttachment attachment;
};

/**
    MOVEMENT's lane key (A, B): a small M/S-like square key with its lane's own light. It
    selects the lane to edit and never switches a lane on or off.
*/
class LaneKey final : public juce::Button
{
public:
    explicit LaneKey (const juce::String& letter);
    std::function<bool()> lit;
    /** A right-click (or Shift+F10): the lanes' menu. */
    std::function<void()> onMenu;
    void paintButton (juce::Graphics&, bool highlighted, bool down) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress&) override;
    static constexpr int width = 40, height = 23, gap = 2;
};

/** The approved slot widths: a knob or a list 104, a two-position switch 95, a segmented or
    three-position switch 119, 7 px apart; the dense family (eight or more controls) 89 / 85 /
    100, 5 px apart. Knobs never shrink (59 px in both). */
enum class Slot
{
    knob,
    slim,
    wide,
    twoKnobs   ///< two knobs' width (a segmented switch with long labels: MORPH ONLY / LAYERED)
};
int slotWidth (Slot slot, bool dense) noexcept;
inline int slotGap (bool dense) noexcept { return dense ? 5 : 7; }
constexpr int denseFrom = 8, cellHeight = 112;

/** A rack cell that is not a knob: its caption (as a rack knob's) over its control, placed as
    the approved rack places it - a compact list 25 px high 40 px down, a segmented switch 27 px
    42 px down, a vertical switch centred in its 79 px band 29 px down. */
class Cell final : public juce::Component
{
public:
    enum class Kind
    {
        list,
        segments,
        switch2,
        switch3
    };
    Cell (juce::String caption, std::unique_ptr<juce::Component> control, Kind kind);
    const juce::String& captionText() const noexcept { return caption; }
    juce::Component& key() noexcept { return *control; }
    Kind cellKind() const noexcept { return kind; }
    Slot slot() const noexcept;
    void resized() override;
    void paint (juce::Graphics&) override;

private:
    juce::String caption;
    std::unique_ptr<juce::Component> control;
    Kind kind;
};

/** The detent marks of a stepped rate knob (the approved rate study): a short tick per
    step around the knob, the chosen one longer, in the module's colour. */
void paintDetents (juce::Graphics& g, juce::Rectangle<float> knob, int steps, int chosen, juce::Colour accent);

} // namespace osp::plugin::rackui
