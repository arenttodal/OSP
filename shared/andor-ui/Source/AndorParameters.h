#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

/**
    The ANDOR parameter schema's IDs (ANDOR OSP's, frozen: automation and sessions depend on
    them; never rename, re-range or reorder). InstrumentHost::createParameterLayout() builds
    the whole schema; tests/plugin/golden/osp-parameter-schema.txt pins it.
*/
namespace osp::plugin::ids
{
    inline const juce::String attack = "attack";
    inline const juce::String release = "release";
    inline const juce::String gain = "gain";
    inline const juce::String velocityRange = "velocityRange";
    inline const juce::String fineTune = "fineTune";
    inline const juce::String bendRange = "bendRange";
    inline const juce::String life = "life";
    inline const juce::String dynamics = "dynamics";
    inline const juce::String character = "character";
    inline const juce::String motion = "motion";
    inline const juce::String space = "space";
    inline const juce::String reimagined = "reimagined";
    inline const juce::String pitchCharacter = "pitchCharacter";
    inline const juce::String sustain = "sustain";
    inline const juce::String seed = "seed";
    inline const juce::String mpe = "mpe";
    inline const juce::String voiceMode = "voiceMode";   // Poly / Mono (version hint 8)
    inline const juce::String glide = "glide";
    // Shaping system v1.0: what each macro does (popups). Never rename these IDs.
    inline const juce::String lifeMode = "life.mode";
    inline const juce::String lifePitch = "life.pitch";
    inline const juce::String lifeTone = "life.tone";
    inline const juce::String lifeAttack = "life.attack";
    inline const juce::String lifeCharacter = "life.character";   // version hint 10
    inline const juce::String lifeTakes = "life.takes";
    inline const juce::String lifeTakeOrder = "life.takeOrder";
    inline const juce::String lifeTakesSeed = "life.takesSeed";
    inline const juce::String dynamicsCurve = "dynamics.curve";
    inline const juce::String dynamicsTone = "dynamics.tone";
    inline const juce::String characterType = "character.type";
    inline const juce::String characterMin = "character.min";
    inline const juce::String characterMax = "character.max";
    inline const juce::String characterResonance = "character.resonance";
    inline const juce::String characterDrive = "character.drive";
    inline const juce::String characterEnvAmount = "character.envAmount";
    inline const juce::String characterEnvAttack = "character.envAttack";
    inline const juce::String characterEnvDecay = "character.envDecay";
    inline const juce::String movementMode = "movement.mode";
    // MOVEMENT v2: every mode's own settings (the old generic movement.paramA/B/C migrate in).
    inline const juce::String driftSpeed = "movement.drift.speed";
    inline const juce::String driftPitch = "movement.drift.pitch";
    inline const juce::String driftTone = "movement.drift.tone";
    inline const juce::String tapeWow = "movement.tape.wow";
    inline const juce::String tapeFlutter = "movement.tape.flutter";
    inline const juce::String tapeWear = "movement.tape.wear";
    inline const juce::String chorusRate = "movement.chorus.rate";
    inline const juce::String chorusWidth = "movement.chorus.width";
    inline const juce::String chorusStereo = "movement.chorus.stereo";
    inline const juce::String pulseRate = "movement.pulse.rate";
    inline const juce::String pulseShape = "movement.pulse.shape";
    inline const juce::String pulseStereo = "movement.pulse.stereo";
    inline const juce::String shaperPattern = "movement.shaper.pattern";
    inline const juce::String shaperRate = "movement.shaper.rate";
    inline const juce::String shaperTarget = "movement.shaper.target";
    inline const juce::String shaperSmooth = "movement.shaper.smooth";
    inline const juce::String spaceType = "space.type";
    inline const juce::String spaceDecay = "space.decay";
    // SPACE v2, ECHO and SHAPER CUSTOM (version hint 12).
    inline const juce::String spacePreDelay = "space.preDelay";
    inline const juce::String spaceSize = "space.size";
    inline const juce::String spaceDamping = "space.damping";
    inline const juce::String spaceModulation = "space.modulation";
    inline const juce::String spaceWidth = "space.width";
    inline const juce::String spaceLowCut = "space.lowCut";
    inline const juce::String spaceHighCut = "space.highCut";
    inline const juce::String echo = "echo";
    inline const juce::String echoType = "echo.type";
    inline const juce::String echoSync = "echo.sync";
    inline const juce::String echoDivision = "echo.division";
    inline const juce::String echoTime = "echo.time";
    inline const juce::String echoFeedback = "echo.feedback";
    inline const juce::String echoTone = "echo.tone";
    inline const juce::String echoAge = "echo.age";
    inline const juce::String echoStereo = "echo.stereo";
    inline const juce::String shaperCustom = "movement.shaper.custom";
    // DRIVE (version hint 14): the macro that took DYNAMICS' place on the panel. DYNAMICS keeps
    // its own IDs (and lives in Advanced now); never reuse one for the other.
    inline const juce::String drive = "drive";
    inline const juce::String driveMode = "drive.mode";
    inline const juce::String driveTone = "drive.tone";
    inline const juce::String driveBody = "drive.body";
    // ARPEGGIATOR (version hint 15): note events ahead of the voice engine. Off by default.
    inline const juce::String arpEnabled = "arp.enabled";
    inline const juce::String arpPattern = "arp.pattern";
    inline const juce::String arpRate = "arp.rate";
    inline const juce::String arpGate = "arp.gate";
    inline const juce::String arpOctaves = "arp.octaves";
    inline const juce::String arpSwing = "arp.swing";   // version hint 16
    inline const juce::String arpVelocity = "arp.velocity";   // version hint 19
    // A/B layers (stable: never rename). Per layer: layerA.sourceMode, layerA.granular.position, ...
    inline const juce::String blend = "ab.blend";
    // Adaptive 1-3 layers (version hint 7): the three-layer mix position and the envelope's D and S.
    inline const juce::String mixX = "mix.x";
    inline const juce::String mixY = "mix.y";
    inline const juce::String decay = "decay";
    inline const juce::String sustainLevel = "sustainLevel";
    // Unified engine, Phase 2 (version hint 20): grains at once across A, B and C. Per layer:
    // layerA.granular.scatter / .pan / .window and layerA.keyDna.amount.
    inline const juce::String grainBudget = "grainBudget";
}
