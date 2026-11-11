#pragma once

#include "engine/fx/FxGraph.h"
#include "engine/fx/FxSettings.h"

#include <juce_audio_processors/juce_audio_processors.h>

namespace osp::plugin::fxparams
{

/**
    The FX library's host parameters, shared by every product that plays the FX library
    (ANDOR ANALOG: `analog.fx.<type>.<n>.<name>`, hints 28 / 29; ANDOR OSP: `fx.<type>.<n>.<name>`,
    hint 30; appended after every older parameter, never reordered): one fixed set per pool slot
    (analog docs/FX_EFFECTS_LIBRARY.md). They exist whether or not their effect is in the route,
    so a host's automation and a session always find them; a slot out of the route keeps its
    values. `prefix` is the product's part before "fx." ("" for OSP, "analog." for ANALOG).
*/
struct Spec
{
    juce::String name;          ///< "mix", "band2.gain"
    juce::String title;         ///< "Mix", "Band 2 Gain"
    enum class Kind { real, choice, toggle } kind = Kind::real;
    float low = 0.0f, high = 1.0f, fallback = 0.0f, centre = -1.0f;   ///< centre: the skew's mid value (-1: linear)
    juce::String unit;          ///< "%", "dB", "Hz", "ms", "us", "s", "st", "deg", "x", "cents"
    juce::StringArray choices;
    std::function<void (fx::Settings&, int slot, float value)> write;
};

/** A type's parameters (empty for the original four: theirs are the instrument's own). */
const std::vector<Spec>& specsOf (fx::Type type);
/** The ID of a slot's parameter. */
juce::String idOf (const juce::String& prefix, fx::InstanceId id, const juce::String& name);
/** Appends them all: the V1 effects at `hint`, RETRO (appended in Analog's V2) at `retroHint`. */
void addTo (juce::AudioProcessorValueTreeState::ParameterLayout& layout, const juce::String& prefix, int hint, int retroHint);
/** A value as the parameter shows it ("-6.0 dB", "1.20 kHz", "350 ms"). */
juce::String format (const Spec& spec, float value);

/** Reads every slot's settings from the parameters (real-time safe once attached). */
class Reader
{
public:
    void attach (juce::AudioProcessorValueTreeState& state, const juce::String& prefix);
    void read (fx::Settings& settings) const noexcept;

private:
    struct Entry
    {
        std::atomic<float>* value = nullptr;
        const Spec* spec = nullptr;
        int slot = 0;
    };
    std::vector<Entry> entries;
};

} // namespace osp::plugin::fxparams
