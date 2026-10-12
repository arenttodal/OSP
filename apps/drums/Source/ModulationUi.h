#pragma once

#include "CeramicSkin.h"
#include "DrumsProcessor.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <functional>

namespace osp::drumsplugin
{

/** A route's depth: a slim rail filled from its centre (no modulation) towards the value. */
class DepthSlider final : public juce::Slider
{
public:
    DepthSlider();
    void paint (juce::Graphics&) override;
};

/**
    The selected pad's modulation, one source at a time (the MODULATION card's ENV, LFO,
    VEL and CTRL tabs). Left: the source itself (its envelope, its LFO, VELOCITY's curve, or
    WHEEL / PRESSURE). Right: where it goes, three slots each with a target (PITCH, LEVEL,
    PAN, FILTER, START, DECAY) and a depth in the target's own words ("+12.0 st").

    Every change is one undo step (a drag: one step for the whole gesture).
*/
class PadModEditor final : public juce::Component
{
public:
    explicit PadModEditor (DrumsProcessor&);

    /** The page: ENV, LFO, VELOCITY, or the controllers (WHEEL or PRESSURE). */
    void setSource (drums::ModSource source);
    drums::ModSource source() const noexcept { return shown; }
    /** Back from the processor (another pad, an undo, a preset). */
    void refresh();

    void paint (juce::Graphics&) override;
    void resized() override;

    // For tests: the controls of the source shown.
    juce::ComboBox& targetBox (int slot) { return target[static_cast<std::size_t> (slot)]; }
    juce::Slider& depthSlider (int slot) { return depth[static_cast<std::size_t> (slot)]; }

private:
    void edit (const std::function<void (drums::PadModulation&)>& change, bool newTransaction);
    void showFor (drums::ModSource s);
    juce::Rectangle<int> sourceArea() const;
    juce::Rectangle<int> routesArea() const;
    juce::Rectangle<int> rowArea (int slot) const;
    void paintEnv (juce::Graphics&, juce::Rectangle<float>) const;
    void paintLfo (juce::Graphics&, juce::Rectangle<float>) const;
    void paintVelocity (juce::Graphics&, juce::Rectangle<float>) const;
    const drums::PadModulation& current() const { return proc.padModulation (proc.selectedPad); }

    DrumsProcessor& proc;
    drums::ModSource shown = drums::ModSource::env;
    bool updating = false;   ///< refresh() sets controls: no edits
    bool gesture = false;    ///< a drag started: its first change opens the undo step

    // ENV: attack, decay, sustain, release
    std::array<juce::Slider, 4> envKnob;
    // LFO
    juce::Slider rate;
    juce::ComboBox shape, division;
    ceramic::KeyButton sync { "SYNC", 0.6f }, retrig { "PER HIT", 0.6f };
    // VELOCITY
    std::array<ceramic::KeyButton, 3> velShape { ceramic::KeyButton { "SOFT", 0.6f }, ceramic::KeyButton { "LINEAR", 0.6f }, ceramic::KeyButton { "HARD", 0.6f } };
    // CTRL
    ceramic::KeyButton wheel { "WHEEL", 0.6f }, pressure { "PRESSURE", 0.6f };
    // Routes
    std::array<juce::ComboBox, drums::modSlotsPerSource> target;
    std::array<DepthSlider, drums::modSlotsPerSource> depth;
};

} // namespace osp::drumsplugin
