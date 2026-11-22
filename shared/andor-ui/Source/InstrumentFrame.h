#pragma once

#include "Design.h"
#include "ShapingPopups.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <functional>
#include <memory>

namespace osp::plugin
{

/** The on-screen keyboard: warm white keys, soft graphite black keys, a quiet coral for held notes. */
class OspKeyboard final : public juce::MidiKeyboardComponent
{
public:
    explicit OspKeyboard (juce::MidiKeyboardState& keyState) : MidiKeyboardComponent (keyState, horizontalKeyboard) {}

    /** A key's place in the edited layer's note map (NOTE MAP, unified engine Phase 5): a
        recorded note, filled from a neighbour (FILL KEYBOARD) or silent. None: an ordinary key
        (no map), drawn exactly as before. */
    struct KeyTint
    {
        enum class State { none, recorded, filled, silent };
        State state = State::none;
        int takes = 0;              ///< recorded: how many takes play in turn (round robin)
        juce::Colour colour;
        bool operator== (const KeyTint&) const = default;
    };
    std::function<KeyTint (int note)> tintOf;

    /** The keys, then the map's light strip along their top edge (a hardware keyboard's light
        guide: one short segment per key, white and black interleaved, over everything). */
    void paint (juce::Graphics&) override;

private:
    void drawLightStrip (juce::Graphics&);
    void drawWhiteNote (int note, juce::Graphics&, juce::Rectangle<float> area, bool isDown, bool isOver,
                        juce::Colour lineColour, juce::Colour textColour) override;
    void drawBlackNote (int note, juce::Graphics&, juce::Rectangle<float> area, bool isDown, bool isOver,
                        juce::Colour noteFillColour) override;
    juce::String getWhiteNoteText (int note) override;
};

/** The empty instrument: one full-width invitation to drop a sound (no empty A/B halves). */
class DropZone final : public juce::Component
{
public:
    DropZone();
    std::function<void()> onBrowse, onExample, onRecord;
    void setHighlight (bool on);
    /** Live capture (a host with liveCapture()): a third button, Record..., opens the CAPTURE page. */
    void setRecordShown (bool shown);
    juce::Button& recordButton() noexcept { return record; }
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    juce::TextButton browse { juce::String::fromUTF8 ("Browse\xe2\x80\xa6") }, example { "Load example" },
        record { juce::String::fromUTF8 ("Record\xe2\x80\xa6") };
    bool highlight = false;
};

/** While a sound is dragged over the instrument (fewer than three layers): where a new layer would go. */
class AddLayerTarget final : public juce::Component
{
public:
    void setLetter (const juce::String& letter);
    void setHighlight (bool on);
    void paint (juce::Graphics&) override;

private:
    juce::String letter = "B";
    bool highlight = false;
};

/** The header's menu: three round dots. */
struct HeaderMenuButton final : juce::Button
{
    HeaderMenuButton() : juce::Button ("Menu") {}
    void paintButton (juce::Graphics&, bool highlighted, bool down) override;
};

/**
    The ANDOR instrument's frame, shared by ANDOR OSP and every product built from the ANDOR
    Instrument Shell: the scaled reference canvas, the housing and its identity, the macro
    row, the keyboard and where everything sits. Extracted unchanged from ANDOR OSP's editor
    (the visual regression keeps the two identical); an editor composes it with its own
    windows and behaviour.
*/
namespace frame
{
    /** The header's identity: the mark in the display face's bold, the product in its light
        (set right after it), the tagline under them. ANDOR OSP: "ANDOR", "/OSP", "One Shot Performer". */
    struct Identity
    {
        juce::String mark { "ANDOR" };
        juce::String product { "/OSP" };
        juce::String tagline { "One Shot Performer" };
    };

    /** The instrument at the reference size, scaled to the window as one piece (an editor
        declares it before every child so it outlives them). */
    struct Content final : juce::Component
    {
        std::function<void (juce::Graphics&)> onPaint;
        void paint (juce::Graphics& g) override
        {
            if (onPaint != nullptr)
                onPaint (g);
        }
    };

    /** One of the six macros on the macro row: its name (a MacroLabel with settings behind
        it), its knob and the knob's attachment. */
    struct MacroKnob
    {
        juce::Slider slider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox };
        std::unique_ptr<MacroLabel> label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
        /** A USER macro (setUpUserMacro): its arc, accent and light take the USER palette
            (colour::userMacro) and its own rendered lens, so a CORE palette change leaves the
            USER bank as it is. */
        bool user = false;
    };
    static constexpr int macroCount = 6;
    using MacroRow = std::array<MacroKnob, macroCount>;

    /** What a macro shows and binds: the parameter (0..100 %), its name, its tooltip. */
    struct MacroSpec
    {
        juce::String parameterId, name, hint;
    };
    /** ANDOR OSP's macro row: LIFE, DRIVE, FILTER (the `character` parameter), MOVEMENT, SPACE, ECHO (data order). */
    std::array<MacroSpec, macroCount> ospMacroSpecs();
    /** Sets a macro up as ANDOR OSP's (identity colour `index` on its ring, the ivory sprite,
        the value bubble, a modulation drop target) and adds it to `content`. */
    void setUpMacro (MacroKnob& knob, int index, const MacroSpec& spec, juce::AudioProcessorValueTreeState& state, juce::Component& content,
                     bool opensPopup = true);
    /** setUpMacro for the USER bank's knob at that place: the same knob in the USER palette. */
    void setUpUserMacro (MacroKnob& knob, int index, const MacroSpec& spec, juce::AudioProcessorValueTreeState& state, juce::Component& content);

    /** The keyboard's range (A0..C8), naming (MIDI 60 = C4) and colours. */
    void setUpKeyboard (OspKeyboard& keyboard);

    /** The parts every ANDOR instrument places the same way (null: not shown). */
    struct Parts
    {
        juce::Component* presetBar = nullptr;
        juce::Component* savePreset = nullptr;
        juce::Component* volume = nullptr;
        juce::Component* menu = nullptr;
        MacroRow* macros = nullptr;
        juce::Component* modulationPanel = nullptr;
        juce::Component* arpPanel = nullptr;
        juce::Component* pitchWheel = nullptr;
        juce::Component* modWheel = nullptr;
        juce::MidiKeyboardComponent* keyboard = nullptr;
        juce::Component* arpControl = nullptr;
        juce::Component* advanced = nullptr;
        /** The keyboard's action keys (KeyboardUtilityActions: D, SETTINGS, INFO) in Advanced's
            place, with their shadow margin around it. */
        juce::Component* utilityActions = nullptr;
        juce::Component* headerMix = nullptr;
        /** The effects rack (opt-in) and how far it pushes what is under it (its height
            plus rackGap while a module is expanded; 0 without a rack: as before). */
        juce::Component* rack = nullptr;
        float rackShift = 0.0f;
        /** How much shorter the source cards are than the reference's (a product whose cards
            keep nothing under their knobs: EngineCard::setModifiersInWell); everything under
            them moves up by as much and the instrument is as much shorter. 0: as before. */
        float sourceTrim = 0.0f;
    };
    /** Places the parts at the reference's places (the keyboard row and footer move down by
        the arpeggiator editor's height while it shows). */
    void place (const Parts& parts, bool arpShown);
    /** The source area (the A/B/C cards or the drop zone), in reference pixels. */
    juce::Rectangle<int> sourceArea (float sourceTrim = 0.0f);
    /** The source area's column `index` of `columns` (the cards' places). */
    juce::Rectangle<int> sourceColumn (juce::Rectangle<int> area, int columns, int index);

    /** The housing, the identity, the keybed, the macro panel with its lights and the keys
        on the housing: everything the instrument paints under its components. `housingBase`:
        the instrument's height with no inline section open (draw::housing: what is above the
        housing's foot stays the same pixels whatever opens); 0: the content's own height. */
    void paint (juce::Graphics& g, juce::Component& content, const Identity& identity, bool arpShown, const MacroRow& macros,
                float rackShift = 0.0f, float sourceTrim = 0.0f, float housingBase = 0.0f,
                const std::vector<std::pair<const MacroKnob*, int>>* performanceKnobs = nullptr);
    /** A core macro's knob centre (x, reference pixels) in Edit Mode (data order: index 0..5). */
    float macroCentreX (int index);
    /** Performance Mode's macro strip: the knobs' places (name, knob) for `count` macros spread
        across the macro panel (no envelope panel), at the reference's knob size and lines. */
    std::vector<std::pair<juce::Rectangle<int>, juce::Rectangle<int>>> performanceMacroPlaces (int count, float sourceTrim);

    /** A popover's card size in reference pixels (a panel laid out at a smaller unit is
        scaled up, so every popover's shell reads alike). */
    juce::Point<float> popoverSize (const MiniPanel& popup);
    /** Where the modulation popovers (routes, larger curve) unfold: above the envelope
        panel (`panel`), at its right. */
    juce::Point<float> modulationPopoverOrigin (juce::Rectangle<float> panel, juce::Point<float> size);
    /** A macro's popover unfolds directly above its name (`label`): centred on it, its foot
        10 px above (kept inside the instrument by placePopover). */
    juce::Point<float> macroPopoverOrigin (juce::Rectangle<float> label, juce::Point<float> size);
    /** Advanced unfolds above its key (`key`), right-aligned with it. */
    juce::Point<float> advancedPopoverOrigin (juce::Rectangle<float> key, juce::Point<float> size, juce::Rectangle<float> clearOf = {});
    /** A wheel's popover (PITCH BEND, MOD WHEEL) unfolds above the wheel (`wheel`: its
        component, caption included, in the content), so the wheel stays in view and playable. */
    juce::Point<float> wheelPopoverOrigin (juce::Rectangle<float> wheel, juce::Point<float> size);
    /** Places a popover's card at `at` (reference pixels), kept inside the instrument. */
    void placePopover (MiniPanel& popup, const juce::Component& content, juce::Point<float> at);
    /** Opening: a short fade and a 3 px settle (110 ms), nothing more. */
    void animatePopoverOpening (MiniPanel& popup);

    /** The instrument's height in reference pixels (taller while the arpeggiator's editor shows). */
    float instrumentHeight (bool arpShown, float rackShift = 0.0f, float sourceTrim = 0.0f) noexcept;
    /** The last resort for a window shorter than the instrument: a slim vertical scroll bar at
        the editor's right edge and the instrument scrolled under it - never scaled down, never
        moved sideways, never a horizontal bar. Owned by an editor; hidden while all fits.

        The window itself always grows first (applyWindowShape asks for the full height). The bar
        appears only when that is impossible:
        - the screen has no more room (the window already stands at the screen's height): at once;
        - the host keeps a shorter window: only once the resize has been negotiated. A host may
          answer a resize with the old size first (a VST3 host reading the size it was told
          before, a host settling its own window); so the bar waits (`settleDelayMs`), asks for
          the full height once more, waits again, and only a window still short then is taken
          as the host's refusal. While it waits, the instrument stays at the top, unscrolled. */
    class Overflow final : private juce::ScrollBar::Listener,
                           private juce::Timer
    {
    public:
        explicit Overflow (juce::Component& editor);
        ~Overflow() override;
        /** (fit) The instrument is `contentHeight` editor pixels tall in a `visibleHeight` window. */
        void update (float contentHeight, float visibleHeight);
        /** How far the instrument is scrolled up (editor pixels). */
        float offset() const noexcept { return scrolled; }
        /** A wheel the instrument did not take (none of its knobs): scrolls while it overflows. */
        bool scrollBy (const juce::MouseWheelDetails& wheel);
        /** Scrolls (while it overflows) so editor pixels `top`..`bottom` of the instrument, as
            if unscrolled, are in view. False: nothing to scroll. */
        bool scrollToShow (float top, float bottom);
        bool overflowing() const noexcept { return bar.isVisible(); }
        /** A shorter window whose resize is still being negotiated (no bar yet). */
        bool settling() const noexcept { return isTimerRunning(); }
        /** The negotiation's next step now (what its timer does): ask for the full height again,
            or - asked once already - take the host's answer and show the bar. */
        void settleNow();
        juce::ScrollBar& scrollBar() noexcept { return bar; }
        /** The editor lays itself out again (its resized()). */
        std::function<void()> onScroll;

        static constexpr int settleDelayMs = 300;

    private:
        void scrollBarMoved (juce::ScrollBar*, double newRangeStart) override;
        void timerCallback() override { settleNow(); }
        void showBar();
        juce::Component& editor;
        juce::ScrollBar bar { true };
        float scrolled = 0.0f, content = 0.0f, visible = 0.0f;
        // The episode being negotiated (the instrument's and the window's heights) and how far.
        float episodeContent = -1.0f, episodeVisible = -1.0f;
        bool askedAgain = false, refused = false, asking = false;
    };

    /** A popover just opened in a window shorter than the instrument (it scrolls): scrolls once
        so the whole popover is in view - one opened low (the Custom Patterns browser under the
        arpeggiator) would otherwise run below the window's edge. Call it on opening only, never
        on a reposition, so the musician's own scrolling is left alone. Nothing while all fits. */
    void revealPopover (juce::Component& editor, const juce::Component& content, const juce::Component& popup, Overflow& overflow);

    /** For tests (no screen to ask headless): the screen's room for the editor's height, in
        pixels, as if it stood on such a screen. 0: none (the default; a real screen is asked). */
    void overrideScreenRoom (int pixels) noexcept;

    /** Places `content` (laid out at the reference size) in `editor`. Its scale is the window's
        width over the reference width - the musician's zoom - and nothing else: opening a rack,
        the arpeggiator or a strip never shrinks or re-centres it. The window as tall as the
        instrument (within a pixel): exactly as it always was. Shorter: the instrument at the
        top-left at that scale, scrolled by `overflow` (none: cut at the bottom). Taller (a host
        that kept an old height): at the top-left, the surround under it. */
    void fit (juce::Component& editor, juce::Component& content, float height, Overflow* overflow = nullptr);
    /** The window's width is the musician's alone (the corner, 869..1810 px): nothing opened or
        closed ever changes it, and no aspect ratio is imposed that a host could answer with a
        new width. `resize` takes the height that goes with that width - taller with the
        arpeggiator, a rack or a strip shown - but never more than the screen holds: then the
        window stops there and the instrument scrolls (Overflow), at the same scale. */
    void applyWindowShape (juce::AudioProcessorEditor& editor, float height, bool resize);
    /** The window's height for `width` and an instrument `height` tall (reference pixels): the
        height that goes with the width, or the screen's room (pixels for the editor; 0 or less:
        no screen to ask) if that is less. */
    int fittedWindowHeight (int width, int screenRoom, float height) noexcept;

    /** Owned by an editor: the first time it is shown on a screen, its window is fitted to that
        screen (applyWindowShape) - a session reopened with a tall layout (the arpeggiator or a
        rack open) never opens taller than the screen; its width stays. Off screen it does nothing. */
    class ScreenFit final : private juce::ComponentListener
    {
    public:
        ScreenFit (juce::AudioProcessorEditor& editor, std::function<float()> instrumentHeight);
        ~ScreenFit() override;

        /** The editor's window shape (its constrainer, installed here), for the corner and the
            host alike: the width 869..1810 px; a change of width (the corner, a host's resize)
            takes the height that goes with it - the instrument's proportions, so dragging the
            corner zooms the whole instrument evenly; a change of height alone is answered with
            the instrument's full height too (never capped at an old height; never shorter
            than the instrument by choice - the window grows, it does not scroll). No fixed
            aspect ratio: a taller instrument can never be answered with another width. */
        class Shape final : public juce::ComponentBoundsConstrainer
        {
        public:
            Shape (juce::AudioProcessorEditor& editor, std::function<float()> instrumentHeight);
            void checkBounds (juce::Rectangle<int>& bounds, const juce::Rectangle<int>& previous, const juce::Rectangle<int>& limits, bool top, bool left,
                              bool bottom, bool right) override;

        private:
            juce::AudioProcessorEditor& editor;
            std::function<float()> height;
        };

    private:
        void componentParentHierarchyChanged (juce::Component&) override { check(); }
        void componentVisibilityChanged (juce::Component&) override { check(); }
        void check();

        juce::AudioProcessorEditor& editor;
        std::function<float()> height;
        Shape shape;
        bool done = false;
    };
}

} // namespace osp::plugin
