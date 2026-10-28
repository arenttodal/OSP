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

private:
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
    std::function<void()> onBrowse, onExample;
    void setHighlight (bool on);
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    juce::TextButton browse { juce::String::fromUTF8 ("Browse\xe2\x80\xa6") }, example { "Load example" };
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
    };
    static constexpr int macroCount = 6;
    using MacroRow = std::array<MacroKnob, macroCount>;

    /** What a macro shows and binds: the parameter (0..100 %), its name, its tooltip. */
    struct MacroSpec
    {
        juce::String parameterId, name, hint;
    };
    /** ANDOR OSP's macro row: LIFE, DRIVE, CHARACTER, MOVEMENT, SPACE, ECHO (data order). */
    std::array<MacroSpec, macroCount> ospMacroSpecs();
    /** Sets a macro up as ANDOR OSP's (identity colour `index` on its ring, the ivory sprite,
        the value bubble, a modulation drop target) and adds it to `content`. */
    void setUpMacro (MacroKnob& knob, int index, const MacroSpec& spec, juce::AudioProcessorValueTreeState& state, juce::Component& content,
                     bool opensPopup = true);

    /** The keyboard's range (A0..C8), naming (MIDI 60 = C4) and colours. */
    void setUpKeyboard (OspKeyboard& keyboard);
    /** The footer's status line (its font and colour). */
    void setUpStatusLabel (juce::Label& label);

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
        juce::Component* status = nullptr;
        juce::Component* arpControl = nullptr;
        juce::Component* advanced = nullptr;
        juce::Component* headerMix = nullptr;
    };
    /** Places the parts at the reference's places (the keyboard row and footer move down by
        the arpeggiator editor's height while it shows). */
    void place (const Parts& parts, bool arpShown);
    /** The source area (the A/B/C cards or the drop zone), in reference pixels. */
    juce::Rectangle<int> sourceArea();
    /** The source area's column `index` of `columns` (the cards' places). */
    juce::Rectangle<int> sourceColumn (juce::Rectangle<int> area, int columns, int index);

    /** The housing, the identity, the keybed, the macro panel with its lights and the keys
        on the housing: everything the instrument paints under its components. */
    void paint (juce::Graphics& g, juce::Component& content, const Identity& identity, bool arpShown, const MacroRow& macros);

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
    juce::Point<float> advancedPopoverOrigin (juce::Rectangle<float> key, juce::Point<float> size);
    /** Places a popover's card at `at` (reference pixels), kept inside the instrument. */
    void placePopover (MiniPanel& popup, const juce::Component& content, juce::Point<float> at);
    /** Opening: a short fade and a 3 px settle (110 ms), nothing more. */
    void animatePopoverOpening (MiniPanel& popup);

    /** The instrument's height in reference pixels (taller while the arpeggiator's editor shows). */
    float instrumentHeight (bool arpShown) noexcept;
    /** Scales `content` (laid out at the reference size) uniformly into `editor`, centred:
        if a host keeps another window shape, the instrument still shows whole. */
    void fit (juce::Component& editor, juce::Component& content, float height);
    /** The window keeps the instrument's proportions (869..1810 px wide); `resize` also takes
        the height that goes with the current width. */
    void applyWindowShape (juce::AudioProcessorEditor& editor, float height, bool resize);
}

} // namespace osp::plugin
