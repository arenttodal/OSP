#pragma once

#include "CeramicSkin.h"
#include "Design.h"
#include "InstrumentHost.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <memory>

namespace osp::plugin
{

namespace equi
{
    /** The five bands' restrained identities: HP muted blue, LOW SHELF amber, BELL sage,
        HIGH SHELF soft rust, LP desaturated violet. */
    juce::Colour bandColour (eq::Band band);
}

/** "EQ" in a source card's header: opens the layer's EQ over its waveform (a view setting);
    a small light while the layer's EQ is heard. Opening never switches the EQ on, closing
    never off. */
class EqButton final : public juce::Button, public skin::KeyFace
{
public:
    explicit EqButton (int layer);
    void setActive (bool on) { if (active != on) { active = on; repaint(); } }
    bool isActive() const noexcept { return active; }
    void paintButton (juce::Graphics&, bool highlighted, bool down) override;
    void buttonStateChanged() override { skin::repaintKey (*this); }
    skin::KeyRole keyRole() const override { return skin::KeyRole::eq; }
    skin::KeyState keyState() const override
    {
        return getToggleState() || isDown() ? skin::KeyState::pressed : isOver() ? skin::KeyState::hover : skin::KeyState::normal;
    }

private:
    int layer;
    bool active = false;
};

/**
    A value in the EQ's band inspector (frequency, gain, Q): its text, dragged up / down to
    change it (Shift: fine), double-click to type a value. A slider underneath (bound to its
    parameter by a SliderAttachment: automation, undo and the host see one gesture), so it is
    also a modulation drop target (its "paramId").
*/
class EqValueField final : public juce::Slider
{
public:
    EqValueField (InstrumentHost& processor, const juce::String& parameterId);
    void paint (juce::Graphics&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void setColour (juce::Colour c) { accent = c; repaint(); }
    /** Shows a text editor for typing (tests call it directly). */
    void beginTyping();
    void commitTyping (const juce::String& text);

private:
    juce::RangedAudioParameter* parameter = nullptr;
    juce::Colour accent { 0xffe9dcc8 };
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    std::unique_ptr<juce::TextEditor> editor;
};

/**
    A layer's DAMPEN, the EQ overlay's second page: the damping over the keys (C0 .. C8 across,
    no damping at the top, full at the bottom), the last played key marked; drag a point,
    double-click to add or remove one, drag between points to bend the segment. The top row:
    DAMPEN's switch, the EQ tab, the mode (Shelf | Low-pass), AMOUNT, the shelf's
    frequency or the low-pass's minimum cutoff, the curve's presets, close.
*/
class DampenEditor final : public juce::Component, public juce::SettableTooltipClient
{
public:
    DampenEditor (InstrumentHost& processor, int layer);

    std::function<void()> onClose, onShowEq;
    void refresh();

    juce::Rectangle<float> graphArea() const;
    juce::Rectangle<float> powerArea() const { return { 12.0f, 10.0f, 22.0f, 22.0f }; }
    juce::Rectangle<float> eqTabArea() const { return eqTab; }
    juce::Rectangle<float> modeArea() const { return mode; }
    juce::Rectangle<float> presetArea() const { return presets; }
    juce::Point<float> pointPosition (int index) const;
    /** The damping drawn at a key (0..1, the curve as the processor holds it). */
    double dampingAt (int note) const;
    void applyPreset (int preset);   ///< 0 the default, then mod::CurvePreset values + 1

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

private:
    juce::RangedAudioParameter* parameter (const char* name) const;
    bool enabled() const;
    bool lowPass() const;
    int pointAt (juce::Point<float> p) const;
    juce::Point<float> toScreen (float x, float y) const;
    juce::Point<float> fromScreen (juce::Point<float> p) const;
    void layoutFields();

    InstrumentHost& ospProcessor;
    int layer;
    mod::Curve curve, before;
    int dragPoint = -1, dragSegment = -1, hoverPoint = -1;
    float dragStartTension = 0.0f;
    juce::Point<float> dragStart;
    bool dragging = false;
    std::array<float, 6> shownSignature {};
    int shownNote = -2;
    std::unique_ptr<EqValueField> amountField, frequencyField, cutoffField;
    juce::Rectangle<float> title, eqTab, mode, presets, closeArea, amountCaption, valueCaption;
    bool hotTab = false, hotPresets = false;
    bool twoRows = false;   ///< narrow (three cards): the mode and values on a second row under the tabs
};

/**
    A layer's SYNTH page (SYNTHESIZE, Phase S2), opened from the SYNTH chip over the waveform
    on the display's graphite: the oscillator's own wave drawn large (as it sounds after its
    tone filter, so WAVE, TONE, OCTAVE and DETUNE are seen), and below it the few choices:
    WAVE (saw, square, triangle, sine), OCTAVE (-2 .. +1), DETUNE (cents), TONE, SHAPE (Held |
    Follow) and PITCH (Keys | Recording). The top row: the page's name, AMOUNT (the chip's
    value) and close. Every control is a host parameter (automation, undo, sessions).
*/
class SynthEditor final : public juce::Component, public juce::SettableTooltipClient, private juce::Timer
{
public:
    SynthEditor (InstrumentHost& processor, int layer);
    ~SynthEditor() override;

    std::function<void()> onClose;
    /** Re-reads the parameters and redraws the wave when they changed. */
    void refresh();

    /** Where each choice sits (tests and the screenshots): "wave", "octave", "shape", "pitch". */
    juce::Rectangle<float> choiceArea (const juce::String& name) const;
    /** Picks part `index` of a choice, as a click there does (one undo step). */
    void choose (const juce::String& name, int index);
    juce::Rectangle<float> waveArea() const;
    bool isTwoRows() const noexcept { return twoRows; }
    /** The drawn wave (normalised samples), as painted. */
    const std::vector<float>& shownWave() const noexcept { return wave; }

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

private:
    void timerCallback() override { refresh(); }
    std::array<float, 7> signature() const;
    void computeWave();
    int choiceCount (const juce::String& name) const;

    InstrumentHost& ospProcessor;
    int layer;
    std::unique_ptr<EqValueField> amountField, detuneField, toneField;
    juce::Rectangle<float> title, closeArea, amountCaption;
    juce::Rectangle<float> waveChoice, octaveChoice, shapeChoice, pitchChoice, detuneArea, toneArea;
    std::array<float, 7> shown {};
    std::vector<float> wave;
    juce::String hot;
    bool twoRows = false;
};

/**
    Live capture's page (InstrumentHost::liveCapture): in a card's well (its REC key) or over an
    empty instrument. What arrives at the input scrolls by as a level picture; RECORD starts a
    take for this layer and STOP keeps it (a minute at most); GRAB LAST 10 / 20 / 30 / 60 s keeps
    what was just played. A kept take becomes the layer's instrument, its notes mapped. Without
    an input the page says how to connect one.
*/
class CaptureEditor final : public juce::Component, public juce::SettableTooltipClient, private juce::Timer
{
public:
    CaptureEditor (InstrumentHost& processor, int layer);
    ~CaptureEditor() override;

    /** The page closes (its close cross, or a take was kept). */
    std::function<void()> onClose;
    static constexpr std::array<int, 4> grabChoices { 10, 20, 30, 60 };

    /** Where each part sits (tests and the screenshots). */
    juce::Rectangle<float> recordArea() const noexcept { return recordButton; }
    juce::Rectangle<float> discardArea() const noexcept { return discardButton; }
    juce::Rectangle<float> grabArea (int choice) const noexcept;
    juce::Rectangle<float> pictureArea() const noexcept { return picture; }
    /** Reads the input again (the page's timer; tests). */
    void refresh();
    const InstrumentHost::CaptureView& shownView() const noexcept { return view; }

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

private:
    void timerCallback() override { refresh(); }
    juce::String partAt (juce::Point<float>) const;

    InstrumentHost& host;
    int layer;
    InstrumentHost::CaptureView view;
    std::array<float, 360> history {};   ///< the input's level, 30 a second (12 s), newest last
    juce::Rectangle<float> title, closeArea, meter, picture, recordButton, discardButton, grabRow;
    juce::String hot;
};

/**
    A layer's EQ, opened over its waveform on the display's opaque graphite (graph first):
    a logarithmic frequency grid (20 Hz - 20 kHz) and +-18 dB, the EQ's exact response (the
    same section designs the audio runs), and the five bands. A band that is off is a small,
    quiet marker on the 0 dB line; pressing or dragging it switches it on. Dragging a band
    moves its frequency across and (bell, shelves) its gain up and down; the mouse wheel (or
    the inspector) sets its Q; a double-click resets its gain (HP / LP: its frequency). The
    top row: the EQ's own switch and the selected band's values, its switch and reset.
*/
class EqEditor final : public juce::Component, public juce::SettableTooltipClient
{
public:
    EqEditor (InstrumentHost& processor, int layer);

    std::function<void()> onClose;
    /** Follows the parameters and the modulation (editor timer); repaints only on change. */
    void refresh();
    /** EQ | DAMPEN: the overlay's two pages (a view setting). */
    void showDampen (bool show);
    bool dampenShown() const noexcept { return dampenPage != nullptr && dampenPage->isVisible(); }
    DampenEditor* dampenEditor() noexcept { return dampenPage.get(); }
    juce::Rectangle<float> dampenTabArea() const { return dampenTab; }

    int selectedBand() const noexcept { return selected; }
    void selectBand (int band);
    /** The band's node / marker centre (tests drive the mouse there). */
    juce::Point<float> nodePosition (int band) const;
    juce::Rectangle<float> graphArea() const;
    juce::Rectangle<float> powerArea() const;
    float xForHz (double hz) const;
    double hzForX (float x) const;
    float yForDb (double db) const;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    juce::RangedAudioParameter* bandParameter (int band, const char* name) const;
    void setValue (juce::RangedAudioParameter* p, float value);
    void switchBand (int band, bool on);
    int bandAt (juce::Point<float> p) const;
    void rebuildInspector();
    double sampleRate() const;

    InstrumentHost& ospProcessor;
    int layer;
    eq::Settings shown;
    int selected = -1, hover = -1, dragBand = -1;
    bool hotTab = false;
    bool twoRows = false;   ///< narrow (three cards): the band's values on a second row under the tabs
    float dragFromX = 0.0f, dragFromY = 0.0f;
    double dragFromHz = 1000.0, dragFromDb = 0.0;
    std::array<double, 4> shownModulation {};
    std::unique_ptr<EqValueField> frequencyField, gainField, qField;
    juce::Rectangle<float> titleArea, nameArea, slopeArea, bandSwitchArea, resetArea, closeArea, dampenTab;
    std::unique_ptr<DampenEditor> dampenPage;
    std::array<double, 4> modulationOffsets() const;   ///< bell octaves, bell dB, low shelf dB, high shelf dB (global LFOs now)
    eq::Settings effective() const;                     ///< what is heard now (the settings plus modulation)
};

} // namespace osp::plugin
