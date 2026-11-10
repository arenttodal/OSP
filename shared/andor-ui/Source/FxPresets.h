#pragma once

#include "engine/fx/FxGraph.h"
#include "InstrumentHost.h"

#include <juce_audio_processors/juce_audio_processors.h>

namespace osp::plugin::fxpresets
{

/**
    FX presets (docs/FX_EFFECTS_LIBRARY.md): one effect's settings by name. A preset holds only
    that effect's own parameters (`<prefix>fx.<type>.<n>.<name>` by name, so it loads into any slot
    of its type); it never touches the instrument, the route, ON or another effect.

    Factory presets ship in code; user presets are files (`<the host's fxUserFolder>/FX
    Presets/<TYPE>/<name>.xml`; ANALOG: `<user data>/ANDOR/ANDOR Analog`). A CHAIN preset holds a route and the settings of every effect in
    it (`.../FX Chains/<name>.xml`): loading one sets the route and those effects only.
*/
struct Preset
{
    juce::String name;
    std::vector<std::pair<juce::String, float>> values;   ///< parameter name -> value (physical units)
};

const std::vector<Preset>& factory (fx::Type type);
juce::File userFolder (const osp::plugin::InstrumentHost& host, fx::Type type);
std::vector<Preset> user (const osp::plugin::InstrumentHost& host, fx::Type type);

/** Sets the slot's parameters to the preset (each with a host gesture; unnamed ones untouched). */
void apply (osp::plugin::InstrumentHost& host, fx::InstanceId id, const Preset& preset);
/** The slot's current settings as a preset (every parameter but ON). */
Preset capture (osp::plugin::InstrumentHost& host, fx::InstanceId id, const juce::String& name);
bool saveUser (const osp::plugin::InstrumentHost& host, fx::Type type, const Preset& preset);
bool removeUser (const osp::plugin::InstrumentHost& host, fx::Type type, const juce::String& name);
/** Whether the slot's parameters are still the preset's (a modified preset shows "*"). */
bool matches (osp::plugin::InstrumentHost& host, fx::InstanceId id, const Preset& preset);

/** A chain: the route and its effects' settings, to or from a file. */
juce::File chainFolder (const osp::plugin::InstrumentHost& host);
std::unique_ptr<juce::XmlElement> chainToXml (osp::plugin::InstrumentHost& host, const fx::Graph& route, const juce::String& name);
/** Reads a chain: the route (returned through `route`) and sets its effects' parameters. */
bool chainFromXml (osp::plugin::InstrumentHost& host, const juce::XmlElement& xml, fx::Graph& route);

} // namespace osp::plugin::fxpresets
