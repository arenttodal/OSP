#pragma once

#include "OspLookAndFeel.h"
#include "PluginProcessor.h"
#include "ShapingPopups.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <functional>
#include <memory>

namespace osp::plugin
{

/**
    Three layers' mix: a small triangle with A (bottom left), B (top) and C (bottom right);
    the node's position gives each its constant-power share (InstrumentEngine::mixWeights).
    Bound to mix.x / mix.y; the readout says how much of each is heard.
*/
class TriangleMix final : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit TriangleMix (juce::AudioProcessorValueTreeState& state);
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
    bool dragging = false;
};

/**
    The band between the sources and the macros. Its height never changes: one layer shows
    only ORIGINAL <-> REIMAGINED; two add the A/B blend; three the mix triangle.
*/
class MixSection final : public juce::Component
{
public:
    explicit MixSection (OspAudioProcessor& processor);
    /** Which slots hold a sound (the blend's ends take their letters and colours). */
    void setLayers (const std::array<bool, 3>& occupied);
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    OspAudioProcessor& processor;
    int count = 0;
    std::array<int, 3> slots { 0, 1, 2 };
    juce::Slider reimagined { juce::Slider::LinearHorizontal, juce::Slider::NoTextBox };
    juce::Slider blend { juce::Slider::LinearHorizontal, juce::Slider::NoTextBox };
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> reimaginedAttachment, blendAttachment;
    TriangleMix triangle;
    juce::Rectangle<int> reimaginedRow, blendRow, captionArea;
};

/** ‹  PRESET NAME  ♡  › : the factory starting states, then the user's presets. */
class PresetBar final : public juce::Component
{
public:
    explicit PresetBar (OspAudioProcessor& processor);
    std::function<void()> onChange;   ///< a preset or starting state was opened
    void refresh();
    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }

private:
    enum class Part { none, previous, name, favourite, next };
    Part partAt (juce::Point<int>) const;
    juce::Rectangle<float> partBounds (Part) const;
    OspAudioProcessor& processor;
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

private:
    struct Shape
    {
        float attack, decay, sustain, release;   // ms, ms, 0..1, ms
    };
    Shape shape() const;
    /** Where the three handles are (attack peak, decay/sustain, release end). */
    std::array<juce::Point<float>, 3> handles() const;
    float timeToX (float ms, float from, float span) const;
    juce::AudioProcessorValueTreeState& state;
    std::array<std::unique_ptr<MiniKnob>, 4> knobs;
    juce::Rectangle<int> graph;
    int dragHandle = -1;
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

private:
    juce::Rectangle<float> slot() const;
    juce::String caption;
    bool springBack;
    std::function<void (float)> onMove;
    float value = 0.0f;   // pitch -1..1, mod 0..1
    float dragStart = 0.0f;
};

} // namespace osp::plugin
