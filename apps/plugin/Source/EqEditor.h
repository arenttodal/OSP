#pragma once

#include "Design.h"
#include "PluginProcessor.h"

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
class EqButton final : public juce::Button
{
public:
    explicit EqButton (int layer);
    void setActive (bool on) { if (active != on) { active = on; repaint(); } }
    bool isActive() const noexcept { return active; }
    void paintButton (juce::Graphics&, bool highlighted, bool down) override;

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
    EqValueField (OspAudioProcessor& processor, const juce::String& parameterId);
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
    A layer's EQ, drawn over its waveform (the waveform stays faintly visible underneath):
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
    EqEditor (OspAudioProcessor& processor, int layer);

    std::function<void()> onClose;
    /** Follows the parameters and the modulation (editor timer); repaints only on change. */
    void refresh();

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

    OspAudioProcessor& ospProcessor;
    int layer;
    eq::Settings shown;
    int selected = -1, hover = -1, dragBand = -1;
    float dragFromX = 0.0f, dragFromY = 0.0f;
    double dragFromHz = 1000.0, dragFromDb = 0.0;
    std::array<double, 4> shownModulation {};
    std::unique_ptr<EqValueField> frequencyField, gainField, qField;
    juce::Rectangle<float> titleArea, nameArea, slopeArea, bandSwitchArea, resetArea, closeArea;
    std::array<double, 4> modulationOffsets() const;   ///< bell octaves, bell dB, low shelf dB, high shelf dB (global LFOs now)
    eq::Settings effective() const;                     ///< what is heard now (the settings plus modulation)
};

} // namespace osp::plugin
