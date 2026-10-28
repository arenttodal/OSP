#include "OspPopups.h"

#include "PluginProcessor.h"

namespace osp::plugin
{

const juce::StringArray& popupParameterIds (MacroPopup macro)
{
    static const juce::StringArray life { "life.mode", "life.pitch", "life.tone", "life.attack" };
    static const juce::StringArray drive { "drive.mode", "drive.tone", "drive.body" };
    static const juce::StringArray character { "character.type", "character.min", "character.max", "character.resonance",
                                               "character.drive", "character.envAmount", "character.envAttack", "character.envDecay" };
    static const juce::StringArray movement { "movement.mode", "movement.drift.speed", "movement.drift.pitch", "movement.drift.tone",
                                              "movement.tape.wow", "movement.tape.flutter", "movement.tape.wear",
                                              "movement.chorus.rate", "movement.chorus.width", "movement.chorus.stereo",
                                              "movement.pulse.rate", "movement.pulse.shape", "movement.pulse.stereo",
                                              "movement.shaper.pattern", "movement.shaper.rate", "movement.shaper.target",
                                              "movement.shaper.smooth", "movement.shaper.custom" };
    static const juce::StringArray space { "space.type", "space.decay", "space.preDelay", "space.size", "space.damping",
                                           "space.modulation", "space.width", "space.lowCut", "space.highCut" };
    static const juce::StringArray echo { "echo.type", "echo.sync", "echo.division", "echo.time", "echo.feedback",
                                          "echo.tone", "echo.age", "echo.stereo" };
    switch (macro)
    {
        case MacroPopup::life: return life;
        case MacroPopup::drive: return drive;
        case MacroPopup::character: return character;
        case MacroPopup::movement: return movement;
        case MacroPopup::echo: return echo;
        case MacroPopup::space: break;
    }
    return space;
}

} // namespace osp::plugin
