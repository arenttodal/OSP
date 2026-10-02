#pragma once

#include "OspLookAndFeel.h"
#include "PluginProcessor.h"
#include "ShapingPopups.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>

namespace osp::plugin
{

/** Small A / B tabs in the display's top-left corner: which layer is being edited. */
class LayerTabs final : public juce::Component
{
public:
    std::function<void (int)> onSelect;
    void setSelected (int layer);
    int selected() const noexcept { return current; }
    /** Marks a layer that holds a sample (a small dot under its letter). */
    void setLoaded (int layer, bool loaded);

    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }

private:
    juce::Rectangle<float> tab (int layer) const;
    int current = 0;
    std::array<bool, 2> loaded {};
};

/** The A/B blend in the display's top-right corner: "A", a slim slider, "B". */
class BlendControl final : public juce::Component
{
public:
    explicit BlendControl (juce::AudioProcessorValueTreeState& state);
    void paint (juce::Graphics&) override;
    void resized() override;

    juce::Slider slider { juce::Slider::LinearHorizontal, juce::Slider::NoTextBox };

private:
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
};

/** ONE SHOT / GRANULAR for the edited layer, in the display's bottom-right corner. */
class SourceModeSwitch final : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit SourceModeSwitch (juce::RangedAudioParameter& parameter);
    std::function<void (int)> onChange;
    int mode() const noexcept { return current; }
    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    juce::Rectangle<float> option (int index) const;
    int current = 0;
    juce::ParameterAttachment attachment;
};

/** The granular controls over the waveform (edited layer, Granular mode only): POS SIZE DENS TUNE SPREAD. */
class GranularOverlay final : public juce::Component
{
public:
    GranularOverlay (juce::AudioProcessorValueTreeState& state, int layer);
    static juce::Point<int> preferredSize() { return { 268, 66 }; }
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    std::array<std::unique_ptr<MiniKnob>, 5> knobs;
};

} // namespace osp::plugin
