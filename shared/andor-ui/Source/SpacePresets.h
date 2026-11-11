#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <vector>

namespace osp::plugin::spacepresets
{

/**
    SPACE's presets: named settings of SPACE's own controls, by category (ROOMS, HALLS,
    PLATES, SPRINGS for the rooms; ABYSS, VINTAGE, BLOOM for the other engines). A preset is
    nothing but values for the parameters SPACE already has: choosing one sets them (one undo
    step), and a session or a patch stores the parameters as ever, never a preset. So every
    session made before plays as it did, and what the bar shows is worked out from the values:
    the preset they match, the last one chosen with "*" once edited, or else the engine's own
    name for what is set (its variant or room type).

    The ones here are starting points: each is to be voiced by ear (docs/STATUS.md).
*/
struct Preset
{
    const char* category;
    const char* name;
    /** (parameter ID, value in the parameter's own units); every SPACE parameter not listed
        takes its default. */
    std::vector<std::pair<const char*, float>> values;
};

const std::vector<Preset>& all();
/** The categories in the menu's order. */
const juce::StringArray& categories();
/** Every parameter a preset sets (SPACE's own: not its amount, the SPACE macro). */
const juce::StringArray& parameterIds();

/** Sets the preset's values (every other SPACE parameter to its default) as one undo step. */
void apply (juce::AudioProcessorValueTreeState& state, const Preset& preset, juce::UndoManager* undo);
/** The preset the parameters match now (-1: none). */
int matching (const juce::AudioProcessorValueTreeState& state);
/** The preset is of the kind SPACE is set to now (its engine, and its room type or variant):
    an edited preset is still that preset ("NAME *") only while it is. */
bool sameKind (const juce::AudioProcessorValueTreeState& state, const Preset& preset);
/** What the presets bar shows: the matching preset's name, else the last one chosen
    (`last`, -1: none) with " *" while SPACE is still of its kind, else describe(). */
juce::String barText (const juce::AudioProcessorValueTreeState& state, int last);
/** What SPACE plays when it matches no preset: its engine and variant or room type ("HALL",
    "ABYSS DARK"). */
juce::String describe (const juce::AudioProcessorValueTreeState& state);

} // namespace osp::plugin::spacepresets
