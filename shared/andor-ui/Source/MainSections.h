#pragma once

#include "Design.h"
#include "OspLookAndFeel.h"
#include "InstrumentHost.h"
#include "ShapingPopups.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <functional>
#include <memory>

namespace osp::plugin
{

/**
    The mix's geometry, one for every mix control: A bottom left, B bottom right (the two
    layers' blend runs A -> B along the base), C on top. The stored state is unchanged:
    `ab.blend` (two layers, equal power) and `mix.x` / `mix.y` (three layers, the shares
    of InstrumentEngine::triangleShares). A point shows each layer's power share.
*/
namespace mixgeometry
{
    /** Power shares (A, B, C) of the three-layer state, and that state from shares. */
    std::array<double, 3> shares (double mixX, double mixY);
    juce::Point<float> stateFromShares (const std::array<double, 3>& share);
    /** The point for the shares in a triangle (corners A, B, C), and back (clipped into it). */
    juce::Point<float> pointFor (const std::array<double, 3>& share, const std::array<juce::Point<float>, 3>& corners);
    std::array<double, 3> sharesAt (juce::Point<float> point, const std::array<juce::Point<float>, 3>& corners);
    /** Two layers: B's power share for a blend, and the blend for B's share. */
    double blendShare (double blend);
    double blendForShare (double shareB);
}

/**
    Three layers' mix at popup size: a triangle with A (bottom left), B (bottom right) and
    C (top); the node's position gives each its constant-power share
    (InstrumentEngine::mixWeights). Bound to mix.x / mix.y.
*/
class TriangleMix final : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit TriangleMix (juce::AudioProcessorValueTreeState& state);
    /** When set, a click (no drag) calls this instead of moving the point (the small
        triangle opens the large one); dragging still moves it. */
    std::function<void()> onClick;
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

    /** The triangle's corners in this component (A, B, C). */
    std::array<juce::Point<float>, 3> corners() const;

private:
    void moveTo (juce::Point<float> where);
    float x = 0.5f, y = 1.0f / 3.0f;
    juce::ParameterAttachment xAttachment, yAttachment;
    bool dragging = false, pressed = false;
};

/**
    The master volume in the header: VOLUME and the value above a hairline track with a
    small cream thumb - a utility, quieter than any sound control. Bound to the `gain`
    parameter (its range, default and dB mapping unchanged); double-click returns to 0 dB.
*/
class VolumeSlider final : public juce::Slider
{
public:
    explicit VolumeSlider (juce::AudioProcessorValueTreeState& state);
    void paint (juce::Graphics&) override;
    static constexpr int thumbRadius = 7;

private:
    juce::AudioProcessorValueTreeState::SliderAttachment attachment;
};

/**
    MIX in the header, between the preset and VOLUME: one control that grows with the
    instrument. One source: nothing. Two: a hairline A -> B with a small node (the A/B
    blend). Three: the same line unfolds upwards into the triangle, C on top (mix.x /
    mix.y); the node keeps its place as it does (C starts at 0). While hovered or dragged
    a small readout gives each layer's share; double-click: equal shares. Neutral - the
    five macro colours are not used here. With three, a click on MIX opens the large mix.
*/
class HeaderMix final : public juce::Component, public juce::SettableTooltipClient, private juce::Timer
{
public:
    explicit HeaderMix (InstrumentHost& processor);
    /** Which slots the instrument shows (count 0..3); 1 hides the control. */
    void setLayers (const std::array<bool, 3>& occupied);
    /** The two ends' labels instead of the layers' letters (a product whose MIX sets a group
        against a layer, e.g. "A+B" and "C"). Empty: the letters, as in ANDOR OSP. */
    void setEndLabels (const juce::String& left, const juce::String& right)
    {
        endLabels = { left, right };
        repaint();
    }
    int layerCount() const noexcept { return count; }
    std::function<void()> onOpenMix;
    /** Whether a press on `c` is a press on the MIX opener (the editor's outside-click logic). */
    bool isMixOpener (const juce::Component* c) const noexcept { return count == 3 && design::showMixTriangle && c == this; }

    /** The corners at the current unfolding (C's height grows with it), for tests too. */
    std::array<juce::Point<float>, 3> corners() const;
    /** Where the node is drawn. */
    juce::Point<float> nodePosition() const;
    /** 0 = the A/B line, 1 = the triangle (animates between). */
    float unfolding() const noexcept { return morph; }
    /** Ends any running transition (tests and snapshots). */
    void finishAnimations();

    // -- Modulation (the MIX as a destination; nothing changes on screen without a route) --
    /** A right-click: the MIX's modulation menu (the editor's overlay builds it). */
    std::function<void()> onModulationMenu;
    /** A source being dragged (0..4, -1: none), and whether it is over the MIX (its drop ring). */
    void setModulationDrag (int source, bool over);
    /** Whether a point (in the parent's coordinates) would drop a source on the MIX. */
    bool acceptsModulationDrop (juce::Point<float> inParent) const;
    /** Follows the mix as it plays and the routes moving it (the editor's timer). */
    void refreshModulation();
    /** Whether a route moves the MIX (the range and the live node are drawn). */
    bool showsModulation() const noexcept { return modulated; }
    /** Where the live (hollow) node is drawn, and the range bar's ends on the line. */
    juce::Point<float> liveNodePosition() const;
    juce::Range<float> modulationRange() const noexcept { return modRange; }

    void paint (juce::Graphics&) override;
    bool hitTest (int x, int y) override;
    void mouseEnter (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

    static constexpr float baseY = 60.0f, halfBase = 34.0f, apexHeight = 38.0f;

private:
    std::array<juce::String, 2> endLabels;

    void timerCallback() override;
    void dragTo (juce::Point<float> where);
    void showReadout (bool on);
    juce::Rectangle<float> captionArea() const;
    std::array<double, 3> shownShares() const;

    InstrumentHost& processor;
    juce::ParameterAttachment blendAttachment, xAttachment, yAttachment;
    float blend = 0.5f, mixX = 0.5f, mixY = 1.0f / 3.0f;
    int count = 0;
    std::array<int, 3> slots { 0, 1, 2 };
    float morph = 0.0f, morphTarget = 0.0f;       ///< line -> triangle
    float opacity = 0.0f, opacityTarget = 0.0f;   ///< fades in with the second layer
    float readout = 0.0f, readoutTarget = 0.0f;
    juce::uint32 readoutHoldUntil = 0;
    bool dragging = false, pressedCaption = false, hovering = false;
    // Modulation: the live mix, the range the routes can take it through (B's share along
    // the line, 0..1), the first route's source colour, and a source being dragged over it.
    bool modulated = false;
    float liveBlend = 0.5f, liveX = 0.5f, liveY = 1.0f / 3.0f;
    juce::Range<float> modRange;
    int modSource = -1, modDragSource = -1;
    bool modDragOver = false;
};

/** The large three-layer mix (opened from MIX in the header). */
std::unique_ptr<MiniPanel> createMixPopup (InstrumentHost& processor);

/** SAVE: the cassette key right of the preset bar (saves the patch as a preset). */
class SavePresetButton final : public juce::Button, public skin::KeyFace
{
public:
    SavePresetButton();
    void paintButton (juce::Graphics&, bool highlighted, bool down) override;
    void buttonStateChanged() override { skin::repaintKey (*this); }
    skin::KeyRole keyRole() const override { return skin::KeyRole::preset; }
    skin::KeyState keyState() const override { return isDown() ? skin::KeyState::pressed : isOver() ? skin::KeyState::hover : skin::KeyState::normal; }
};

/** ‹  PRESET NAME  ♡  › : the factory starting states, then the user's presets. */
class PresetBar final : public juce::Component, public skin::KeyFace
{
public:
    explicit PresetBar (InstrumentHost& processor);
    std::function<void()> onChange;   ///< a preset or starting state was opened
    std::function<void()> onSaveStartingState;   ///< "Save starting state..." (the editor asks for a name)
    std::function<void()> onOpenLibrary;         ///< "Open Library..." (the menu's first item)
    void refresh();
    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }
    skin::KeyRole keyRole() const override { return skin::KeyRole::preset; }
    skin::KeyState keyState() const override { return skin::KeyState::normal; }

private:
    enum class Part { none, previous, name, favourite, next };
    Part partAt (juce::Point<int>) const;
    juce::Rectangle<float> partBounds (Part) const;
    InstrumentHost& processor;
    juce::String name;
    bool favourite = false;
};

/** The instrument's one amplitude envelope: its shape (with draggable points) and A D S R. */
class EnvelopePanel final : public juce::Component
{
public:
    explicit EnvelopePanel (juce::AudioProcessorValueTreeState& state);
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

    juce::Rectangle<int> graphBounds() const { return graph; }
    /** Draw the "AMP ENVELOPE" title (off when a tab row above names it). */
    void setShowsTitle (bool shows) { showsTitle = shows; repaint(); }
    /** A short word on the picture while the sound does not follow the envelope as drawn (a
        product's: "GATE", say); the curve is drawn faint under it. Empty (the default): the
        picture exactly as before. */
    void setNote (const juce::String& text)
    {
        if (text != note)
        {
            note = text;
            repaint();
        }
    }

    /** Where a knob is now with its modulation (its parameter's 0..1; below 0: unknown, the set
        value is drawn). Set by the editor from the halos' own reading (ModulationOverlay::
        livePosition). Null (the default): the picture shows the set values only. */
    std::function<float (const juce::String& parameterId)> livePosition;
    /** Asked by the editor's timer after the halos refreshed: repaints the picture when the
        modulated shape moved. While nothing modulates A, D, S or R the picture is exactly the
        set one. */
    void refreshLive();

private:
    struct Shape
    {
        float attack, decay, sustain, release;   // ms, ms, 0..1, ms
        bool operator== (const Shape&) const = default;
    };
    Shape shape() const;
    /** The shape the voices play now (each value moved by its modulation), else shape(). */
    Shape liveShape() const;
    /** Where the three handles are (attack peak, decay/sustain, release end). */
    std::array<juce::Point<float>, 3> handles() const { return handlesOf (shape()); }
    std::array<juce::Point<float>, 3> handlesOf (const Shape&) const;
    Shape shownLive {};
    float timeToX (float ms, float from, float span) const;
    juce::AudioProcessorValueTreeState& state;
    std::array<std::unique_ptr<MiniKnob>, 4> knobs;
    juce::Rectangle<int> graph;
    bool showsTitle = true;
    juce::String note;
    int dragHandle = -1;
};

/**
    A macro's own on / off, as a tiny lamp beside its name (opt-in: ShellProduct::macroSwitches;
    ANDOR ANALOG's MORPH). A ring with a dot: muted while off, softly lit in the macro's colour
    while on. A real button (click, Return or Space; one host gesture, undoable) with a larger
    invisible target than it shows. It switches a bool parameter and nothing else.
*/
class MacroSwitchLamp final : public juce::Component, public juce::SettableTooltipClient
{
public:
    MacroSwitchLamp (juce::RangedAudioParameter& parameter, juce::Colour colour, juce::UndoManager* undo);
    /** The switch moved (any source: a click, automation, a preset): the editor dims the knob. */
    std::function<void (bool on)> onStateChange;
    bool isOn() const noexcept { return on; }
    void toggle();
    /** The lamp's visible ring (local coordinates; the component is its larger click target). */
    juce::Rectangle<float> ringArea() const;
    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }
    bool keyPressed (const juce::KeyPress&) override;
    void focusGained (FocusChangeType) override { repaint(); }
    void focusLost (FocusChangeType) override { repaint(); }

private:
    juce::Colour colour;
    juce::ParameterAttachment attachment;
    bool on = false;
};

/** A performance wheel beside the keyboard: PITCH springs back to the centre, MOD stays. */
class Wheel final : public juce::Component, public juce::SettableTooltipClient
{
public:
    Wheel (juce::String caption, bool springBack, std::function<void (float)> onMove);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    /** Shows a value set elsewhere (the processor's mod wheel); ignored while dragged. */
    void setValue (float newValue);
    /** The wheel as a modulation source (MOD): a drag socket beside its caption. Dragging the
        caption or socket carries a cable (screen positions); dragging the wheel still turns it. */
    void setAssignable (juce::Colour socketColour);
    std::function<void (juce::Point<int>)> onSourceDrag, onSourceDrop;
    juce::Rectangle<float> socketArea() const;
    /** The socket's centre fills while the wheel feeds a route. */
    void setInUse (bool used);
    void mouseMove (const juce::MouseEvent&) override;

private:
    juce::Rectangle<float> slot() const;
    bool dragging = false;
    juce::String caption;
    bool springBack;
    std::function<void (float)> onMove;
    float value = 0.0f;   // pitch -1..1, mod 0..1
    float dragStart = 0.0f;
    bool assignable = false, cabling = false, overSocket = false, inUse = false;
    juce::Colour socket;
};

} // namespace osp::plugin
