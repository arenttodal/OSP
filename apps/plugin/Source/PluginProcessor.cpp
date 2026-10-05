#include "PluginProcessor.h"
#include "ValueFormat.h"

#include "PluginEditor.h"

#include "audio/utility/TestSignals.h"
#include "core/PitchMath.h"
#include "io/AudioFileIO.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>

namespace osp::plugin
{

namespace ids
{
    static const juce::String attack = "attack";
    static const juce::String release = "release";
    static const juce::String gain = "gain";
    static const juce::String velocityRange = "velocityRange";
    static const juce::String fineTune = "fineTune";
    static const juce::String bendRange = "bendRange";
    static const juce::String life = "life";
    static const juce::String dynamics = "dynamics";
    static const juce::String character = "character";
    static const juce::String motion = "motion";
    static const juce::String space = "space";
    static const juce::String reimagined = "reimagined";
    static const juce::String pitchCharacter = "pitchCharacter";
    static const juce::String sustain = "sustain";
    static const juce::String seed = "seed";
    static const juce::String mpe = "mpe";
    // Shaping system v1.0: what each macro does (popups). Never rename these IDs.
    static const juce::String lifeMode = "life.mode";
    static const juce::String lifePitch = "life.pitch";
    static const juce::String lifeTone = "life.tone";
    static const juce::String lifeAttack = "life.attack";
    static const juce::String dynamicsCurve = "dynamics.curve";
    static const juce::String dynamicsTone = "dynamics.tone";
    static const juce::String characterType = "character.type";
    static const juce::String characterMin = "character.min";
    static const juce::String characterMax = "character.max";
    static const juce::String characterResonance = "character.resonance";
    static const juce::String characterDrive = "character.drive";
    static const juce::String characterEnvAmount = "character.envAmount";
    static const juce::String characterEnvAttack = "character.envAttack";
    static const juce::String characterEnvDecay = "character.envDecay";
    static const juce::String movementMode = "movement.mode";
    // MOVEMENT v2: every mode's own settings (the old generic movement.paramA/B/C migrate in).
    static const juce::String driftSpeed = "movement.drift.speed";
    static const juce::String driftPitch = "movement.drift.pitch";
    static const juce::String driftTone = "movement.drift.tone";
    static const juce::String tapeWow = "movement.tape.wow";
    static const juce::String tapeFlutter = "movement.tape.flutter";
    static const juce::String tapeWear = "movement.tape.wear";
    static const juce::String chorusRate = "movement.chorus.rate";
    static const juce::String chorusWidth = "movement.chorus.width";
    static const juce::String chorusStereo = "movement.chorus.stereo";
    static const juce::String pulseRate = "movement.pulse.rate";
    static const juce::String pulseShape = "movement.pulse.shape";
    static const juce::String pulseStereo = "movement.pulse.stereo";
    static const juce::String shaperPattern = "movement.shaper.pattern";
    static const juce::String shaperRate = "movement.shaper.rate";
    static const juce::String shaperTarget = "movement.shaper.target";
    static const juce::String shaperSmooth = "movement.shaper.smooth";
    static const juce::String spaceType = "space.type";
    static const juce::String spaceDecay = "space.decay";
    // A/B layers (stable: never rename). Per layer: layerA.sourceMode, layerA.granular.position, ...
    static const juce::String blend = "ab.blend";
    static const juce::Identifier instrument = "Instrument";
    static const juce::Identifier instrumentB = "InstrumentB";
    static const juce::Identifier instrumentC = "InstrumentC";
    // Adaptive 1-3 layers (version hint 7): the three-layer mix position and the envelope's D and S.
    static const juce::String mixX = "mix.x";
    static const juce::String mixY = "mix.y";
    static const juce::String decay = "decay";
    static const juce::String sustainLevel = "sustainLevel";
}

namespace
{
    struct StartingState
    {
        const char* name;
        float life, dynamics, character, motion, space, reimagined, attackMs, releaseMs;
    };
    // Small and musical (spec §101): each changes how the engine treats the sample.
    constexpr StartingState startingStates[] = {
        // CHARACTER is the filter position now (100 = fully open).
        { "Natural", 45, 65, 90, 12, 10, 10, 2, 700 },
        { "Alive", 70, 70, 95, 30, 15, 20, 2, 500 },
        { "Floating", 35, 45, 70, 55, 45, 45, 120, 2500 },
        { "Broken", 85, 60, 80, 50, 20, 85, 2, 600 },
        { "Frozen", 10, 40, 75, 5, 30, 30, 250, 3000 },
        { "Dream", 40, 45, 60, 45, 60, 65, 300, 4000 },
        { "Wide", 40, 60, 90, 40, 55, 30, 10, 1200 },
    };
}

juce::AudioProcessorValueTreeState::ParameterLayout OspAudioProcessor::createLayout()
{
    using Range = juce::NormalisableRange<float>;
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    auto ms = juce::AudioParameterFloatAttributes().withLabel ("ms");
    auto db = juce::AudioParameterFloatAttributes().withLabel ("dB");

    Range attackRange (0.0f, 2000.0f, 0.1f);
    attackRange.setSkewForCentre (50.0f);
    Range releaseRange (20.0f, 15000.0f, 0.1f);
    releaseRange.setSkewForCentre (700.0f);

    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { ids::attack, 1 }, "Attack", attackRange, 2.0f, ms));
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { ids::release, 1 }, "Release", releaseRange, 700.0f, ms));
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { ids::gain, 1 }, "Output", Range (-36.0f, 12.0f, 0.1f), 0.0f, db));
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { ids::velocityRange, 1 }, "Velocity Range",
                                                             Range (0.0f, 48.0f, 0.1f), 30.0f, db));
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { ids::fineTune, 1 }, "Fine Tune",
                                                             Range (-100.0f, 100.0f, 0.1f), 0.0f,
                                                             juce::AudioParameterFloatAttributes().withLabel ("cents")));
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { ids::bendRange, 1 }, "Bend Range",
                                                             Range (0.0f, 24.0f, 1.0f), 2.0f,
                                                             juce::AudioParameterFloatAttributes().withLabel ("st")));

    // Musician-facing macros (spec §11, §12), 0..100 %.
    auto percent = juce::AudioParameterFloatAttributes().withLabel ("%");
    const Range unit (0.0f, 100.0f, 0.1f);
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { ids::life, 2 }, "Life", unit, 50.0f, percent));
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { ids::dynamics, 2 }, "Dynamics", unit, 65.0f, percent));
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { ids::character, 2 }, "Character", unit, 90.0f, percent));
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { ids::motion, 2 }, "Movement", unit, 12.0f, percent));
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { ids::space, 2 }, "Space", unit, 10.0f, percent));
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { ids::reimagined, 2 }, "Original / Reimagined", unit, 20.0f, percent));
    // Advanced (spec §13).
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { ids::pitchCharacter, 2 }, "Pitch Character",
                                                              juce::StringArray { "Tape", "Natural" }, 0));
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { ids::sustain, 2 }, "Sustain",
                                                              juce::StringArray { "Recording", "Endless" }, 1));
    layout.add (std::make_unique<juce::AudioParameterInt> (juce::ParameterID { ids::seed, 2 }, "Variation Seed", 1, 9999, 1));
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { ids::mpe, 3 }, "MPE", false));

    // Shaping system v1.0 (version hint 4): the popups behind the macros.
    const Shaping d;
    auto hz = juce::AudioParameterFloatAttributes().withLabel ("Hz");
    auto cents = juce::AudioParameterFloatAttributes().withLabel ("cents");
    auto seconds = juce::AudioParameterFloatAttributes().withLabel ("s");
    auto choice = [&layout] (const juce::String& id, const juce::String& name, juce::StringArray items, int def) {
        layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { id, 4 }, name, items, def));
    };
    auto number = [&layout] (const juce::String& id, const juce::String& name, Range range, float def, juce::AudioParameterFloatAttributes attr) {
        layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { id, 4 }, name, range, def, attr));
    };
    auto skewed = [] (float lo, float hi, float centre) {
        Range r (lo, hi, 0.0f);
        r.setSkewForCentre (centre);
        return r;
    };
    choice (ids::lifeMode, "Life Mode", { "Natural", "Loose", "Fray" }, 0);
    number (ids::lifePitch, "Life Pitch", skewed (0.0f, 15.0f, 4.0f), static_cast<float> (d.lifePitchCents), cents);
    number (ids::lifeTone, "Life Tone", unit, static_cast<float> (100.0 * d.lifeTone), percent);
    number (ids::lifeAttack, "Life Attack", unit, static_cast<float> (100.0 * d.lifeAttack), percent);
    choice (ids::dynamicsCurve, "Dynamics Curve", { "Soft", "Linear", "Hard" }, 1);
    number (ids::dynamicsTone, "Dynamics Tone", unit, static_cast<float> (100.0 * d.dynamicsTone), percent);
    choice (ids::characterType, "Character Filter", { "LP24", "LP12", "HP12", "BP12", "Tilt" }, 0);
    number (ids::characterMin, "Character Min", skewed (20.0f, 20000.0f, 632.0f), static_cast<float> (d.filterMinHz), hz);
    number (ids::characterMax, "Character Max", skewed (20.0f, 20000.0f, 632.0f), static_cast<float> (d.filterMaxHz), hz);
    number (ids::characterResonance, "Character Resonance", Range (0.0f, 90.0f, 0.0f), static_cast<float> (100.0 * d.resonance), percent);
    number (ids::characterDrive, "Character Drive", unit, static_cast<float> (100.0 * d.drive), percent);
    number (ids::characterEnvAmount, "Character Envelope", Range (-100.0f, 100.0f, 0.0f), static_cast<float> (100.0 * d.envAmount), percent);
    number (ids::characterEnvAttack, "Character Env Attack", skewed (0.0f, 2000.0f, 60.0f), static_cast<float> (1000.0 * d.envAttackSeconds), ms);
    number (ids::characterEnvDecay, "Character Env Decay", skewed (20.0f, 12000.0f, 700.0f), static_cast<float> (1000.0 * d.envDecaySeconds), ms);
    choice (ids::movementMode, "Movement Mode", { "Drift", "Tape", "Chorus", "Pulse", "Shaper" }, 0);
    choice (ids::spaceType, "Space Type", { "Room", "Chamber", "Plate", "Spring" }, 2);
    number (ids::spaceDecay, "Space Decay", skewed (0.2f, 8.0f, 1.8f), static_cast<float> (d.spaceDecaySeconds), seconds);

    // MOVEMENT v2 (version hint 6): every mode's own settings.
    auto movementNumber = [&layout, &unit, &percent] (const juce::String& id, const juce::String& name, double value) {
        layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { id, 6 }, name, unit, static_cast<float> (100.0 * value), percent));
    };
    movementNumber (ids::driftSpeed, "Drift Speed", d.driftSpeed);
    movementNumber (ids::driftPitch, "Drift Pitch", d.driftPitch);
    movementNumber (ids::driftTone, "Drift Tone", d.driftTone);
    movementNumber (ids::tapeWow, "Tape Wow", d.tapeWow);
    movementNumber (ids::tapeFlutter, "Tape Flutter", d.tapeFlutter);
    movementNumber (ids::tapeWear, "Tape Wear", d.tapeWear);
    movementNumber (ids::chorusRate, "Chorus Rate", d.chorusRate);
    movementNumber (ids::chorusWidth, "Chorus Width", d.chorusWidth);
    movementNumber (ids::chorusStereo, "Chorus Stereo", d.chorusStereo);
    movementNumber (ids::pulseRate, "Pulse Rate", d.pulseRate);
    movementNumber (ids::pulseShape, "Pulse Shape", d.pulseShape);
    movementNumber (ids::pulseStereo, "Pulse Stereo", d.pulseStereo);
    {
        juce::StringArray patterns;
        for (int i = 0; i < RhythmicShaper::patternCount; ++i)
            patterns.add (RhythmicShaper::patternName (i));
        layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { ids::shaperPattern, 6 }, "Shaper Pattern", patterns, d.shaper.pattern));
        layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { ids::shaperRate, 6 }, "Shaper Rate",
                                                                  juce::StringArray { "1/4", "1/8", "1/8T", "1/16", "1/16T", "1/32" }, static_cast<int> (d.shaper.rate)));
        layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { ids::shaperTarget, 6 }, "Shaper Target",
                                                                  juce::StringArray { "Vol", "Filter", "Both" }, static_cast<int> (d.shaper.target)));
        movementNumber (ids::shaperSmooth, "Shaper Smooth", d.shaper.smooth);
    }

    // A/B layers (version hint 5): the blend, then each layer's source mode and GRANULAR settings.
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { ids::blend, 5 }, "A/B Blend", Range (0.0f, 1.0f, 0.0f), 0.0f,
                                                             juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) {
                                                                 return v <= 0.0f ? juce::String ("A") : (v >= 1.0f ? juce::String ("B") : "A " + juce::String (juce::roundToInt (100.0f * (1.0f - v))) + " / B " + juce::String (juce::roundToInt (100.0f * v)));
                                                             })));
    const GranularParams g;
    for (int layer = 0; layer < numLayers; ++layer)
    {
        const auto name = "Layer " + layerName (layer) + " ";
        // Layer C arrived with the adaptive layers (version hint 7); A and B keep theirs.
        auto id = [layer] (const char* n) { return juce::ParameterID { layerParameterId (layer, n), layer < 2 ? 5 : 7 }; };
        layout.add (std::make_unique<juce::AudioParameterChoice> (id ("sourceMode"), name + "Source Mode", juce::StringArray { "One Shot", "Granular" }, 0));
        layout.add (std::make_unique<juce::AudioParameterFloat> (id ("granular.position"), name + "Grain Position", unit, static_cast<float> (100.0 * g.position), percent));
        layout.add (std::make_unique<juce::AudioParameterFloat> (id ("granular.size"), name + "Grain Size", skewed (20.0f, 400.0f, 120.0f), static_cast<float> (1000.0 * g.sizeSeconds), ms));
        layout.add (std::make_unique<juce::AudioParameterFloat> (id ("granular.density"), name + "Grain Density", skewed (4.0f, 40.0f, 14.0f), static_cast<float> (g.density),
                                                                 juce::AudioParameterFloatAttributes().withLabel ("/s")));
        layout.add (std::make_unique<juce::AudioParameterFloat> (id ("granular.tune"), name + "Grain Tune", Range (-12.0f, 12.0f, 0.0f), static_cast<float> (g.tuneSemitones),
                                                                 juce::AudioParameterFloatAttributes().withLabel ("st")));
        layout.add (std::make_unique<juce::AudioParameterFloat> (id ("granular.spread"), name + "Grain Spread", unit, static_cast<float> (100.0 * g.spread), percent));
    }

    // Adaptive 1-3 layers (version hint 7): every layer's START, TUNE, PAN, LEVEL and its
    // source modifiers; the three-layer mix; the envelope's decay and sustain.
    const LayerSettings defaults;
    for (int layer = 0; layer < numLayers; ++layer)
    {
        const auto name = "Layer " + layerName (layer) + " ";
        auto id = [layer] (const char* n) { return juce::ParameterID { layerParameterId (layer, n), 7 }; };
        layout.add (std::make_unique<juce::AudioParameterFloat> (id ("start"), name + "Start", unit, 0.0f,
                                                                 juce::AudioParameterFloatAttributes().withLabel ("%")
                                                                     .withStringFromValueFunction ([] (float v, int) { return juce::String (juce::roundToInt (v)) + " %"; })));
        layout.add (std::make_unique<juce::AudioParameterFloat> (id ("tune"), name + "Tune", Range (-24.0f, 24.0f, 0.01f), 0.0f,
                                                                 juce::AudioParameterFloatAttributes().withLabel ("st")
                                                                     .withStringFromValueFunction ([] (float v, int) { return format::semitones (v); })));
        layout.add (std::make_unique<juce::AudioParameterFloat> (id ("pan"), name + "Pan", Range (-100.0f, 100.0f, 0.1f), 0.0f,
                                                                 juce::AudioParameterFloatAttributes()
                                                                     .withStringFromValueFunction ([] (float v, int) { return format::pan (v); })
                                                                     .withValueFromStringFunction ([] (const juce::String& t) { return format::panFromText (t); })));
        Range levelRange (static_cast<float> (LayerSettings::minLevelDb), 6.0f, 0.1f);
        levelRange.setSkewForCentre (-12.0f);   // the knob's travel goes to the useful range
        layout.add (std::make_unique<juce::AudioParameterFloat> (id ("level"), name + "Level", levelRange, 0.0f,
                                                                 juce::AudioParameterFloatAttributes().withLabel ("dB")
                                                                     .withStringFromValueFunction ([] (float v, int) { return format::levelDb (v); })));
        layout.add (std::make_unique<juce::AudioParameterBool> (id ("link"), name + "Link", false));
        layout.add (std::make_unique<juce::AudioParameterBool> (id ("reverse"), name + "Reverse", defaults.reverse));
        layout.add (std::make_unique<juce::AudioParameterBool> (id ("loop"), name + "Loop", defaults.loop));
        layout.add (std::make_unique<juce::AudioParameterBool> (id ("follow"), name + "Follow", defaults.follow));
    }
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { ids::mixX, 7 }, "Mix X", Range (0.0f, 1.0f, 0.0f), 0.5f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { ids::mixY, 7 }, "Mix Y", Range (0.0f, 1.0f, 0.0f), 1.0f / 3.0f));
    {
        Range decayRange (1.0f, 20000.0f, 0.1f);
        decayRange.setSkewForCentre (600.0f);
        layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { ids::decay, 7 }, "Decay", decayRange, 600.0f, ms));
        layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { ids::sustainLevel, 7 }, "Sustain Level", unit, 100.0f, percent));
    }
    return layout;
}

const juce::StringArray& OspAudioProcessor::layerControlNames()
{
    static const juce::StringArray names { "start", "tune", "pan", "level", "link", "reverse", "loop", "follow" };
    return names;
}

//==============================================================================
// Undo / redo actions (spec §56)

class InstrumentChangeAction final : public juce::UndoableAction
{
public:
    InstrumentChangeAction (OspAudioProcessor& p, int l, std::uint64_t before, std::uint64_t after) : processor (p), layer (l), beforeLoad (before), afterLoad (after) {}
    bool perform() override
    {
        if (first)
        {
            first = false; // the load itself already published it
            return true;
        }
        return show (afterLoad);
    }
    bool undo() override { return show (beforeLoad); }

private:
    bool show (std::uint64_t loadId)
    {
        auto& byLoad = processor.layers[static_cast<std::size_t> (layer)].latestByLoad;
        const auto it = byLoad.find (loadId);
        if (it == byLoad.end())
            return false;
        processor.republish (it->second, layer);
        return true;
    }
    OspAudioProcessor& processor;
    int layer;
    std::uint64_t beforeLoad, afterLoad;
    bool first = true;
};

class RootChangeAction final : public juce::UndoableAction
{
public:
    RootChangeAction (OspAudioProcessor& p, int l, std::optional<double> before, std::optional<double> after) : processor (p), layer (l), from (before), to (after) {}
    bool perform() override
    {
        processor.setRootOverride (to, layer);
        return true;
    }
    bool undo() override
    {
        processor.setRootOverride (from, layer);
        return true;
    }

private:
    OspAudioProcessor& processor;
    int layer;
    std::optional<double> from, to;
};

OspAudioProcessor::OspAudioProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      parameters (*this, &undoManager, "OSP", createLayout()),
      store (SampleStore::defaultDirectory())
{
    attackParam = parameters.getRawParameterValue (ids::attack);
    releaseParam = parameters.getRawParameterValue (ids::release);
    gainParam = parameters.getRawParameterValue (ids::gain);
    velocityRangeParam = parameters.getRawParameterValue (ids::velocityRange);
    fineTuneParam = parameters.getRawParameterValue (ids::fineTune);
    bendRangeParam = parameters.getRawParameterValue (ids::bendRange);
    lifeParam = parameters.getRawParameterValue (ids::life);
    dynamicsParam = parameters.getRawParameterValue (ids::dynamics);
    characterParam = parameters.getRawParameterValue (ids::character);
    motionParam = parameters.getRawParameterValue (ids::motion);
    spaceParam = parameters.getRawParameterValue (ids::space);
    reimaginedParam = parameters.getRawParameterValue (ids::reimagined);
    pitchCharacterParam = parameters.getRawParameterValue (ids::pitchCharacter);
    sustainParam = parameters.getRawParameterValue (ids::sustain);
    seedParam = parameters.getRawParameterValue (ids::seed);
    mpeParam = parameters.getRawParameterValue (ids::mpe);
    for (int i = 0; i < numShapingParams; ++i)
        shapingParams[static_cast<std::size_t> (i)] = parameters.getRawParameterValue (shapingIds()[i]);
    lastShaping.fill (-1.0e9f);
    blendParam = parameters.getRawParameterValue (ids::blend);
    for (int layer = 0; layer < numLayers; ++layer)
    {
        auto& lp = layerParams[static_cast<std::size_t> (layer)];
        lp.mode = parameters.getRawParameterValue (layerParameterId (layer, granularNames()[0]));
        for (int i = 0; i < 5; ++i)
            lp.granular[static_cast<std::size_t> (i)] = parameters.getRawParameterValue (layerParameterId (layer, granularNames()[i + 1]));
        for (int i = 0; i < layerControlNames().size(); ++i)
            lp.controls[static_cast<std::size_t> (i)] = parameters.getRawParameterValue (layerParameterId (layer, layerControlNames()[i]));
    }
    mixXParam = parameters.getRawParameterValue (ids::mixX);
    mixYParam = parameters.getRawParameterValue (ids::mixY);
    decayParam = parameters.getRawParameterValue (ids::decay);
    sustainLevelParam = parameters.getRawParameterValue (ids::sustainLevel);

    engineSettings.polyphony = 24;
    engineSettings.outputGainDb = -9.0;
    engine.prepare (48000.0, 512, engineSettings);

    startTimerHz (20);
}

OspAudioProcessor::~OspAudioProcessor()
{
    stopTimer();
    loaderPool.removeAllJobs (true, 10000);
}

bool OspAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono();
}

void OspAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    // Not the audio thread: allocation is allowed here. Start from the current macro
    // values so smoothing never begins from a previous session's state.
    lastMacroParam.fill (-1.0f);
    applyParameters (true);
    engine.prepare (sampleRate, samplesPerBlock, engineSettings);
    for (int layer = 0; layer < numLayers; ++layer)
    {
        const auto* playing = layers[static_cast<std::size_t> (layer)].playing;
        if (playing != nullptr && playing->set != nullptr)
            engine.setInstrumentSet (playing->set.get(), layer);
        else
            engine.setModel (playing != nullptr ? playing->model.get() : nullptr, layer);
    }
    keyboardState.reset();
    pitchBendSemitones = 0.0;
    applyParameters (true);
}

namespace
{
    bool changed (float a, float b) noexcept { return std::abs (a - b) > 1.0e-6f; }
}

void OspAudioProcessor::applyParameters (bool force) noexcept
{
    const float attack = attackParam->load();
    const float release = releaseParam->load();
    const float decay = decayParam->load();
    const float sustainLevel = sustainLevelParam->load();
    if (force || changed (attack, lastAttack) || changed (release, lastRelease) || changed (decay, lastDecay) || changed (sustainLevel, lastSustainLevel))
    {
        // The instrument's one amplitude envelope (every layer's voices share it).
        AdsrSettings adsr = engineSettings.adsr;
        adsr.attackSeconds = attack * 0.001;
        adsr.decaySeconds = decay * 0.001;
        adsr.sustainLevel = 0.01 * sustainLevel;
        adsr.releaseSeconds = release * 0.001;
        engineSettings.adsr = adsr;
        engine.setEnvelope (adsr);
        lastAttack = attack;
        lastRelease = release;
        lastDecay = decay;
        lastSustainLevel = sustainLevel;
    }

    const float gain = gainParam->load();
    if (force || changed (gain, lastGain))
    {
        engine.setOutputGainDb (engineSettings.outputGainDb + gain);
        lastGain = gain;
    }

    const float velocityRange = velocityRangeParam->load();
    if (force || changed (velocityRange, lastVelocityRange))
    {
        engine.setVelocityRangeDb (velocityRange);
        lastVelocityRange = velocityRange;
    }

    bool shapingChanged = force;
    for (std::size_t i = 0; i < shapingParams.size(); ++i)
    {
        const float v = shapingParams[i]->load();
        if (changed (v, lastShaping[i]))
        {
            lastShaping[i] = v;
            shapingChanged = true;
        }
    }
    if (shapingChanged)
    {
        engineSettings.shaping = shapingFromParameters();
        engine.setShaping (engineSettings.shaping);
    }

    // Macro = host parameter, unless a MIDI CC (20-25) moved it more recently.
    std::array<std::atomic<float>*, 6> macroParams { lifeParam, dynamicsParam, characterParam, motionParam, spaceParam, reimaginedParam };
    std::array<double, 6> values {};
    for (std::size_t i = 0; i < 6; ++i)
    {
        const float p = macroParams[i]->load() * 0.01f;
        if (changed (p, lastMacroParam[i]))
        {
            lastMacroParam[i] = p;
            ccMacro[i] = -1.0f;
        }
        values[i] = ccMacro[i] >= 0.0f ? ccMacro[i] : p;
    }
    Macros macros;
    macros.life = values[0];
    macros.dynamics = values[1];
    macros.character = values[2];
    macros.motion = values[3] + modWheel * (1.0 - values[3]); // mod wheel opens MOTION up
    macros.space = values[4];
    macros.reimagined = values[5];
    engine.setMacros (macros);
    engineSettings.macros = macros;
    engine.setMpe (mpeParam->load() >= 0.5f);
    engine.setPitchCharacter (pitchCharacterParam->load() >= 0.5f ? PitchCharacter::natural : PitchCharacter::tape);
    engine.setContinuation (sustainParam->load() >= 0.5f ? ContinuationStrategy::multiLoopMovement : ContinuationStrategy::off);
    engine.setSeed (static_cast<std::uint64_t> (std::max (1.0f, seedParam->load())));

    engine.setPitchOffsetSemitones (pitchBendSemitones + fineTuneParam->load() / 100.0);

    // Layers: the mix, root corrections, layer controls, source modes and granular settings.
    // Also kept in engineSettings: prepare() must start from the current values, never ramp to them.
    engineSettings.blend = blendParam->load();
    engine.setBlend (engineSettings.blend);
    engineSettings.mixX = mixXParam->load();
    engineSettings.mixY = mixYParam->load();
    engine.setMixPosition (engineSettings.mixX, engineSettings.mixY);
    for (int layer = 0; layer < numLayers; ++layer)
    {
        auto& lp = layerParams[static_cast<std::size_t> (layer)];
        engine.setLayerPitchOffsetSemitones (layer, layers[static_cast<std::size_t> (layer)].rootShiftSemitones.load());
        std::array<float, 8> controls {};
        for (std::size_t i = 0; i < controls.size(); ++i)
            controls[i] = lp.controls[i]->load();
        if (force || controls != lp.lastControls)
        {
            lp.lastControls = controls;
            LayerSettings ls;
            ls.start = 0.01 * controls[0];
            ls.tuneSemitones = controls[1];
            ls.pan = 0.01 * controls[2];
            ls.levelDb = controls[3];
            ls.reverse = controls[5] >= 0.5f;
            ls.loop = controls[6] >= 0.5f;
            ls.follow = controls[7] >= 0.5f;
            engineSettings.layer[static_cast<std::size_t> (layer)] = ls;
            engine.setLayerSettings (layer, ls);
        }
        std::array<float, 6> now { lp.mode->load(), lp.granular[0]->load(), lp.granular[1]->load(), lp.granular[2]->load(),
                                   lp.granular[3]->load(), lp.granular[4]->load() };
        if (force || now != lp.last)
        {
            lp.last = now;
            const auto mode = now[0] >= 0.5f ? SourceMode::granular : SourceMode::oneShot;
            engineSettings.sourceMode[static_cast<std::size_t> (layer)] = mode;
            engine.setSourceMode (layer, mode);
            GranularParams g;
            g.position = 0.01 * now[1];
            g.sizeSeconds = 0.001 * now[2];
            g.density = now[3];
            g.tuneSemitones = now[4];
            g.spread = 0.01 * now[5];
            engineSettings.granular[static_cast<std::size_t> (layer)] = g;
            engine.setGranular (layer, g);
        }
    }
}

const juce::StringArray& OspAudioProcessor::granularNames()
{
    static const juce::StringArray names { "sourceMode", "granular.position", "granular.size", "granular.density", "granular.tune", "granular.spread" };
    return names;
}

const juce::StringArray& OspAudioProcessor::shapingIds()
{
    static const juce::StringArray list {
        ids::lifeMode, ids::lifePitch, ids::lifeTone, ids::lifeAttack,
        ids::dynamicsCurve, ids::dynamicsTone,
        ids::characterType, ids::characterMin, ids::characterMax, ids::characterResonance, ids::characterDrive,
        ids::characterEnvAmount, ids::characterEnvAttack, ids::characterEnvDecay,
        ids::movementMode,
        ids::spaceType, ids::spaceDecay,
        ids::driftSpeed, ids::driftPitch, ids::driftTone, ids::tapeWow, ids::tapeFlutter, ids::tapeWear,
        ids::chorusRate, ids::chorusWidth, ids::chorusStereo, ids::pulseRate, ids::pulseShape, ids::pulseStereo,
        ids::shaperPattern, ids::shaperRate, ids::shaperTarget, ids::shaperSmooth,
    };
    jassert (list.size() == numShapingParams);
    return list;
}

Shaping OspAudioProcessor::shapingFromParameters() const noexcept
{
    auto v = [this] (int i) { return static_cast<double> (shapingParams[static_cast<std::size_t> (i)]->load()); };
    auto index = [&v] (int i, int count) { return std::clamp (static_cast<int> (std::lround (v (i))), 0, count - 1); };
    Shaping s;
    s.lifeMode = static_cast<LifeMode> (index (0, 3));
    s.lifePitchCents = v (1);
    s.lifeTone = 0.01 * v (2);
    s.lifeAttack = 0.01 * v (3);
    s.velocityCurve = static_cast<VelocityCurve> (index (4, 3));
    s.dynamicsTone = 0.01 * v (5);
    s.filterType = static_cast<FilterType> (index (6, 5));   // LP24..Tilt; `off` is not offered
    s.filterMinHz = v (7);
    s.filterMaxHz = v (8);
    s.resonance = 0.01 * v (9);
    s.drive = 0.01 * v (10);
    s.envAmount = 0.01 * v (11);
    s.envAttackSeconds = 0.001 * v (12);
    s.envDecaySeconds = 0.001 * v (13);
    s.movementMode = static_cast<MovementMode> (index (14, 5));
    s.spaceType = static_cast<SpaceType> (index (15, 4));
    s.spaceDecaySeconds = v (16);
    s.driftSpeed = 0.01 * v (17);
    s.driftPitch = 0.01 * v (18);
    s.driftTone = 0.01 * v (19);
    s.tapeWow = 0.01 * v (20);
    s.tapeFlutter = 0.01 * v (21);
    s.tapeWear = 0.01 * v (22);
    s.chorusRate = 0.01 * v (23);
    s.chorusWidth = 0.01 * v (24);
    s.chorusStereo = 0.01 * v (25);
    s.pulseRate = 0.01 * v (26);
    s.pulseShape = 0.01 * v (27);
    s.pulseStereo = 0.01 * v (28);
    s.shaper.pattern = index (29, RhythmicShaper::patternCount);
    s.shaper.rate = static_cast<ShaperRate> (index (30, 6));
    s.shaper.target = static_cast<ShaperTarget> (index (31, 3));
    s.shaper.smooth = 0.01 * v (32);
    return s;
}

void OspAudioProcessor::swapInstrumentIfPending() noexcept
{
    for (int index = 0; index < numLayers; ++index)
    {
        auto& layer = layers[static_cast<std::size_t> (index)];
        if (const auto* next = layer.exchange.takePending())
        {
            if (layer.playing != nullptr)
            {
                // Keep the old instrument alive while voices still play it.
                auto slot = std::find (layer.retired.begin(), layer.retired.end(), nullptr);
                if (slot == layer.retired.end())
                {
                    // Too many overlapping swaps: silence the oldest retired instrument.
                    auto oldest = std::min_element (layer.retired.begin(), layer.retired.end(),
                                                    [] (auto* a, auto* b) { return a->generation < b->generation; });
                    if ((*oldest)->set != nullptr)
                        engine.killVoicesUsing ((*oldest)->set.get());
                    else if ((*oldest)->model != nullptr)
                        engine.killVoicesUsing ((*oldest)->model.get());
                    slot = oldest;
                }
                *slot = layer.playing;
            }
            layer.playing = next;
            if (next->set != nullptr)
                engine.setInstrumentSet (next->set.get(), index);
            else
                engine.setModel (next->model.get(), index);   // an emptied layer has no model: silence
        }

        std::uint64_t oldest = layer.playing != nullptr ? layer.playing->generation : 0;
        for (auto& r : layer.retired)
        {
            if (r == nullptr)
                continue;
            const bool inUse = r->set != nullptr ? engine.isSetInUse (r->set.get()) : (r->model != nullptr && engine.isModelInUse (r->model.get()));
            if (! inUse)
                r = nullptr;
            else
                oldest = std::min (oldest, r->generation);
        }
        if (layer.playing != nullptr)
            layer.exchange.publishOldestInUse (oldest);
    }
}

void OspAudioProcessor::handleMidi (const juce::MidiMessage& m) noexcept
{
    const int channel = m.getChannel();
    // MPE (lower zone): channel 1 is the manager channel, 2..16 carry one note each.
    const bool memberChannel = engine.isMpe() && channel >= 2;
    if (m.isNoteOn())
    {
        engine.noteOn (m.getNoteNumber(), m.getVelocity(), channel);
        const int count = velocityCount.load (std::memory_order_relaxed);
        recentVelocity[static_cast<std::size_t> (count % velocityHistory)].store (m.getVelocity(), std::memory_order_relaxed);
        velocityCount.store (count + 1, std::memory_order_release);
    }
    else if (m.isNoteOff())
        engine.noteOff (m.getNoteNumber(), channel);
    else if (m.isChannelPressure())
        engine.setChannelPressure (memberChannel ? channel : 1, m.getChannelPressureValue() / 127.0);
    else if (m.isAftertouch())
        engine.setChannelPressure (memberChannel ? channel : 1, m.getAfterTouchValue() / 127.0);
    else if (m.isController() && m.getControllerNumber() == 74)
        engine.setChannelTimbre (memberChannel ? channel : 1, m.getControllerValue() / 127.0);
    else if (m.isController() && m.getControllerNumber() == 1)
        modWheel = static_cast<float> (m.getControllerValue()) / 127.0f;
    else if (m.isController() && m.getControllerNumber() >= 20 && m.getControllerNumber() <= 25)
        ccMacro[static_cast<std::size_t> (m.getControllerNumber() - 20)] = static_cast<float> (m.getControllerValue()) / 127.0f;
    else if (m.isPitchWheel() && memberChannel)
        engine.setChannelPitchBend (channel, (m.getPitchWheelValue() - 8192) / 8192.0 * 48.0); // MPE default: +/- 48 st
    else if (m.isSustainPedalOn() || m.isSustainPedalOff())
        engine.setSustainPedal (m.isSustainPedalOn());
    else if (m.isAllNotesOff() || m.isAllSoundOff())
        engine.allNotesOff();
    else if (m.isPitchWheel())
    {
        const double normalised = (m.getPitchWheelValue() - 8192) / 8192.0;
        pitchBendSemitones = normalised * bendRangeParam->load();
        engine.setPitchOffsetSemitones (pitchBendSemitones + fineTuneParam->load() / 100.0);
    }
}

void OspAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int numSamples = buffer.getNumSamples();

    keyboardState.processNextMidiBuffer (midi, 0, numSamples, true);
    swapInstrumentIfPending();

    // A bounce or playback from the same position must perform identically (spec §33):
    // performance memory and the note counter restart whenever the transport starts.
    // One canonical musical-time snapshot per block (MOVEMENT's SHAPER syncs to it).
    HostTiming timing;
    if (auto* head = getPlayHead())
        if (const auto position = head->getPosition())
        {
            timing.playing = position->getIsPlaying();
            if (const auto ppq = position->getPpqPosition())
            {
                timing.valid = true;
                timing.ppq = *ppq;
            }
            if (const auto bpm = position->getBpm())
                timing.bpm = *bpm;
            if (const auto signature = position->getTimeSignature())
            {
                timing.numerator = signature->numerator;
                timing.denominator = signature->denominator;
            }
            const bool transportRunning = position->getIsPlaying();
            if (transportRunning && ! hostWasPlaying)
                engine.resetPerformance();
            hostWasPlaying = transportRunning;
        }
    engine.setHostTiming (timing);
    {
        // The on-screen wheels act like the controller's.
        const float pitch = screenPitch.load(), mod = screenMod.load();
        if (changed (pitch, lastScreenPitch))
        {
            lastScreenPitch = pitch;
            handleMidi (juce::MidiMessage::pitchWheel (1, juce::jlimit (0, 16383, 8192 + juce::roundToInt (pitch * 8191.0f))));
        }
        if (changed (mod, lastScreenMod))
        {
            lastScreenMod = mod;
            handleMidi (juce::MidiMessage::controllerEvent (1, 1, juce::jlimit (0, 127, juce::roundToInt (mod * 127.0f))));
        }
    }
    applyParameters (false);

    const int outChannels = std::min (buffer.getNumChannels(), 2);
    auto* const* channels = buffer.getArrayOfWritePointers();

    int position = 0;
    auto renderTo = [&] (int end) {
        if (end <= position || outChannels <= 0)
            return;
        float* segment[2] = { channels[0] + position, channels[outChannels > 1 ? 1 : 0] + position };
        engine.render (segment, outChannels, end - position);
        position = end;
    };

    for (const auto metadata : midi)
    {
        renderTo (std::clamp (metadata.samplePosition, 0, numSamples));
        handleMidi (metadata.getMessage());
    }
    renderTo (numSamples);

    for (int ch = outChannels; ch < buffer.getNumChannels(); ++ch)
        buffer.clear (ch, 0, numSamples);

    activeVoices.store (engine.musicalVoiceCount(), std::memory_order_relaxed);
}

//==============================================================================
// Loading (message thread + loader thread)

void OspAudioProcessor::enqueueLoad (LoadRequest request, int layer)
{
    auto& target = layers[resolve (layer)];
    ++pendingLoads;
    target.state = LoadState::loading;
    const auto generation = nextGeneration.fetch_add (1);
    target.latestLoadId = generation;
    loaderPool.addJob ([this, request = std::move (request), generation, layer] {
        auto result = loadInstrument (request, store, generation);
        // Queue the next model stage before reporting, so pendingLoads never reads 0 in between.
        if (result.instrument != nullptr)
            enqueueRefine (result.instrument, result.audio, layer);
        pushResult (std::move (result), layer);
        --pendingLoads;
    });
}

void OspAudioProcessor::enqueueRefine (std::shared_ptr<const LoadedInstrument> base, std::shared_ptr<const AudioData> audio, int layer)
{
    if (base == nullptr || base->model == nullptr || base->model->stage == InstrumentModel::Stage::complete)
        return;
    ++pendingLoads;
    loaderPool.addJob ([this, base = std::move (base), audio = std::move (audio), layer] {
        // A newer sample was dropped into this layer meanwhile: do not spend time on this one.
        if (base->loadId == layers[static_cast<std::size_t> (layer)].latestLoadId.load())
        {
            auto result = refineInstrument (*base, audio, nextGeneration.fetch_add (1));
            if (result.instrument != nullptr)
                enqueueRefine (result.instrument, result.audio, layer);
            result.audio.reset();
            pushResult (std::move (result), layer);
        }
        --pendingLoads;
    });
}

void OspAudioProcessor::pushResult (LoadResult result, int layer)
{
    const std::lock_guard<std::mutex> lock (resultsMutex);
    finishedLoads.push_back ({ layer, std::move (result) });
}

void OspAudioProcessor::enqueueSetLoad (SetLoadRequest request, int layer)
{
    auto& target = layers[resolve (layer)];
    ++pendingLoads;
    target.state = LoadState::loading;
    const auto generation = nextGeneration.fetch_add (1);
    target.latestLoadId = generation;
    loaderPool.addJob ([this, request = std::move (request), generation, layer] {
        pushResult (loadInstrumentSet (request, store, generation), layer);
        --pendingLoads;
    });
}

void OspAudioProcessor::loadFiles (const juce::Array<juce::File>& files, int layer)
{
    if (files.size() == 1)
    {
        loadFile (files.getFirst(), layer);
        return;
    }
    SetLoadRequest request;
    for (const auto& f : files)
    {
        LoadRequest r;
        r.file = f;
        request.files.push_back (std::move (r));
    }
    userLoads.insert (nextGeneration.load());
    enqueueSetLoad (std::move (request), static_cast<int> (resolve (layer)));
}

void OspAudioProcessor::reassignSample (const std::string& filename, SampleRole role, int velocityLayer, std::optional<double> rootMidi)
{
    const int layer = static_cast<int> (resolve (-1));
    const auto instrument = currentInstrument (layer);
    if (instrument == nullptr || instrument->set == nullptr)
        return;
    auto assignments = instrument->assignments;
    assignments.erase (std::remove_if (assignments.begin(), assignments.end(), [&] (const SetAssignment& a) { return a.filename == filename; }),
                       assignments.end());
    assignments.push_back ({ filename, role, rootMidi, velocityLayer });
    ++pendingLoads;
    loaderPool.addJob ([this, instrument, assignments, generation = nextGeneration.fetch_add (1), layer] {
        pushResult (reassignInstrumentSet (*instrument, assignments, generation), layer);
        --pendingLoads;
    });
}

void OspAudioProcessor::loadFile (const juce::File& file, int layer)
{
    LoadRequest request;
    request.file = file;
    userLoads.insert (nextGeneration.load());
    enqueueLoad (std::move (request), static_cast<int> (resolve (layer)));
}

void OspAudioProcessor::loadExample (int layer)
{
    // A synthetic vowel stands in for the factory examples until legally owned ones exist.
    const auto file = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("OSP Example Vowel A3.wav");
    if (! file.existsAsFile())
    {
        auto audio = testsignals::vowel (midiToHz (57), 5.0, 48000.0, 1);
        std::string error;
        io::writeAudioFile (std::filesystem::path (file.getFullPathName().toStdString()), audio, io::SampleFormat::pcm24, error);
    }
    loadFile (file, layer);
}

void OspAudioProcessor::clearLayer (int layer)
{
    const auto index = resolve (layer);
    auto& target = layers[index];
    // An instrument without a model: the layer plays nothing (voices on the old one finish).
    auto empty = std::make_shared<LoadedInstrument>();
    empty->generation = nextGeneration.fetch_add (1);
    empty->loadId = empty->generation;
    target.latestLoadId = empty->loadId;
    {
        const std::lock_guard<std::mutex> lock (modelMutex);
        target.exchange.publish (empty);
    }
    target.lastPublishedLoad = empty->loadId;
    target.lastLoadFailed = false;
    target.state = LoadState::empty;
    setRootOverride (std::nullopt, static_cast<int> (index));
}

//==============================================================================
// Adaptive layers: which slots hold a sound, adding and removing layers

bool OspAudioProcessor::isLayerOccupied (int layer) const
{
    const auto index = static_cast<std::size_t> (std::clamp (layer, 0, numLayers - 1));
    return layers[index].state.load() == LoadState::loading || currentInstrument (static_cast<int> (index)) != nullptr;
}

int OspAudioProcessor::occupiedLayerCount() const
{
    int count = 0;
    for (int layer = 0; layer < numLayers; ++layer)
        count += isLayerOccupied (layer) ? 1 : 0;
    return count;
}

int OspAudioProcessor::firstFreeLayer() const
{
    for (int layer = 0; layer < numLayers; ++layer)
        if (! isLayerOccupied (layer))
            return layer;
    return -1;
}

void OspAudioProcessor::setParameterValue (const juce::String& id, float value)
{
    if (auto* p = parameters.getParameter (id))
        p->setValueNotifyingHost (p->convertTo0to1 (value));
}

float OspAudioProcessor::parameterValue (const juce::String& id) const
{
    if (auto* p = parameters.getParameter (id))
        return p->convertFrom0to1 (p->getValue());
    return 0.0f;
}

void OspAudioProcessor::makeLayerAudible (int layer)
{
    // A layer the musician just added is heard: two layers meet in the middle of the
    // blend, three at the centre of the triangle.
    const int count = occupiedLayerCount();
    if (count == 2 && layer >= 1)
        setParameterValue (ids::blend, 0.5f);
    else if (count == 3)
    {
        setParameterValue (ids::mixX, 0.5f);
        setParameterValue (ids::mixY, 1.0f / 3.0f);
    }
}

int OspAudioProcessor::addLayers (const juce::Array<juce::File>& files, int firstLayer)
{
    int loaded = 0;
    for (const auto& file : files)
    {
        const int layer = firstLayer >= 0 && loaded == 0 && ! isLayerOccupied (firstLayer) ? firstLayer : firstFreeLayer();
        if (layer < 0)
            break;
        resetLayerControls (layer);
        loadFile (file, layer);
        makeLayerAudible (layer);
        ++loaded;
    }
    if (loaded < files.size())
        showMessage (juce::String (files.size()) + " sounds dropped: OSP plays up to three, so " + juce::String (files.size() - loaded)
                     + (files.size() - loaded == 1 ? " was" : " were") + " left out. Drop a folder to make one multi-sample layer.");
    return loaded;
}

void OspAudioProcessor::replaceLayer (const juce::Array<juce::File>& files, int layer)
{
    if (files.isEmpty())
        return;
    const int index = static_cast<int> (resolve (layer));
    setRootOverride (std::nullopt, index);
    loadFiles (files, index);
}

int OspAudioProcessor::addLayerSet (const juce::Array<juce::File>& files)
{
    const int layer = firstFreeLayer();
    if (layer < 0 || files.isEmpty())
        return -1;
    resetLayerControls (layer);
    loadFiles (files, layer);
    makeLayerAudible (layer);
    return layer;
}

void OspAudioProcessor::resetLayerControls (int layer)
{
    for (const auto& name : granularNames())
        if (auto* p = parameters.getParameter (layerParameterId (layer, name)))
            p->setValueNotifyingHost (p->getDefaultValue());
    for (const auto& name : layerControlNames())
        if (auto* p = parameters.getParameter (layerParameterId (layer, name)))
            p->setValueNotifyingHost (p->getDefaultValue());
    setRootOverride (std::nullopt, layer);
}

OspAudioProcessor::LayerSnapshot OspAudioProcessor::captureLayer (int layer) const
{
    LayerSnapshot snapshot;
    snapshot.instrument = currentInstrument (layer);
    snapshot.rootOverride = rootOverride (layer);
    for (const auto* names : { &granularNames(), &layerControlNames() })
        for (const auto& name : *names)
            snapshot.values.set (name, parameterValue (layerParameterId (layer, name)));
    const auto& slot = layers[static_cast<std::size_t> (layer)];
    snapshot.latestByLoad = slot.latestByLoad;
    snapshot.lastPublishedLoad = slot.lastPublishedLoad;
    return snapshot;
}

void OspAudioProcessor::applyLayer (int layer, const LayerSnapshot& snapshot)
{
    auto& slot = layers[static_cast<std::size_t> (layer)];
    for (const auto* names : { &granularNames(), &layerControlNames() })
        for (const auto& name : *names)
            setParameterValue (layerParameterId (layer, name), snapshot.values.getWithDefault (name, parameterValue (layerParameterId (layer, name))));
    if (snapshot.instrument == nullptr)
    {
        clearLayer (layer);
        return;
    }
    slot.latestByLoad = snapshot.latestByLoad;
    slot.latestLoadId = snapshot.instrument->loadId;
    slot.autoPositionLoad = snapshot.instrument->loadId;   // POS was already chosen
    setRootOverride (snapshot.rootOverride, layer);
    republish (snapshot.instrument, layer);
    slot.lastPublishedLoad = snapshot.lastPublishedLoad;
    slot.lastLoadFailed = false;
    slot.state = LoadState::ready;
}

bool OspAudioProcessor::removeLayer (int layer)
{
    if (layer < 0 || layer >= numLayers || pendingLoads.load() > 0 || ! isLayerOccupied (layer))
        return false;
    std::array<LayerSnapshot, numLayers> before;
    for (int l = 0; l < numLayers; ++l)
        before[static_cast<std::size_t> (l)] = captureLayer (l);
    removedLayer = std::make_unique<RemovedLayer>();
    removedLayer->index = layer;
    removedLayer->snapshot = before[static_cast<std::size_t> (layer)];
    // Compact: the layers above move down, so the instrument is always A, A+B or A+B+C.
    int last = layer;
    for (int l = layer + 1; l < numLayers; ++l)
        if (before[static_cast<std::size_t> (l)].instrument != nullptr)
        {
            applyLayer (last, before[static_cast<std::size_t> (l)]);
            last = l;
        }
    clearLayer (last);
    resetLayerControls (last);
    if (last != layer)
        undoManager.clearUndoHistory();   // load undo steps name slots that have moved
    if (editLayer() >= occupiedLayerCount())
        setEditLayer (std::max (0, occupiedLayerCount() - 1));
    showMessage ("Removed layer " + layerName (layer) + juce::String::fromUTF8 (" Â· Restore it from the menu"));
    return true;
}

bool OspAudioProcessor::canRestoreRemovedLayer() const
{
    return removedLayer != nullptr && removedLayer->snapshot.instrument != nullptr && firstFreeLayer() >= 0 && pendingLoads.load() == 0;
}

bool OspAudioProcessor::restoreRemovedLayer()
{
    if (! canRestoreRemovedLayer())
        return false;
    const int count = occupiedLayerCount();
    const int index = std::clamp (removedLayer->index, 0, count);
    // Make room: the layers from `index` up move one slot up again.
    for (int l = count - 1; l >= index; --l)
        applyLayer (l + 1, captureLayer (l));
    applyLayer (index, removedLayer->snapshot);
    if (index < count)
        undoManager.clearUndoHistory();
    removedLayer.reset();
    setEditLayer (index);
    return true;
}

void OspAudioProcessor::applyLinkedDelta (int layer, const juce::String& control, float delta)
{
    if (std::abs (delta) < 1.0e-9f || ! isLayerLinked (layer) || ! layerControlNames().contains (control))
        return;
    for (int other = 0; other < numLayers; ++other)
    {
        if (other == layer || ! isLayerOccupied (other) || ! isLayerLinked (other))
            continue;
        if (auto* p = parameters.getParameter (layerParameterId (other, control)))
        {
            const auto range = p->getNormalisableRange();
            const float next = range.snapToLegalValue (std::clamp (p->convertFrom0to1 (p->getValue()) + delta, range.start, range.end));
            p->beginChangeGesture();
            p->setValueNotifyingHost (p->convertTo0to1 (next));
            p->endChangeGesture();
        }
    }
}

//==============================================================================
// Header preset navigation

std::vector<OspAudioProcessor::PresetEntry> OspAudioProcessor::presetList() const
{
    std::vector<PresetEntry> list;
    for (int i = 0; i < static_cast<int> (std::size (startingStates)); ++i)
        list.push_back ({ startingStates[i].name, i, {} });
    for (const auto& file : findFiles (presetFolder(), presetExtension))
        list.push_back ({ file.getFileNameWithoutExtension(), -1, file });
    return list;
}

juce::String OspAudioProcessor::presetDisplayName() const
{
    if (! presetIsProgram && lastPresetFile != juce::File())
        return lastPresetFile.getFileNameWithoutExtension();
    return startingStates[std::clamp (currentProgram, 0, static_cast<int> (std::size (startingStates)) - 1)].name;
}

void OspAudioProcessor::openPresetEntry (const PresetEntry& entry)
{
    if (entry.program >= 0)
    {
        setCurrentProgram (entry.program);
        presetIsProgram = true;
    }
    else if (loadPreset (entry.file))
        presetIsProgram = false;
}

void OspAudioProcessor::stepPresetList (int delta)
{
    const auto list = presetList();
    if (list.empty())
        return;
    int current = -1;
    for (int i = 0; i < static_cast<int> (list.size()); ++i)
        if ((presetIsProgram && list[static_cast<std::size_t> (i)].program == currentProgram)
            || (! presetIsProgram && list[static_cast<std::size_t> (i)].file == lastPresetFile))
            current = i;
    const int count = static_cast<int> (list.size());
    const int next = current < 0 ? 0 : ((current + delta) % count + count) % count;
    openPresetEntry (list[static_cast<std::size_t> (next)]);
}

namespace
{
    juce::File favouritesFile() { return OspAudioProcessor::presetFolder().getChildFile ("Favourites.txt"); }
}

bool OspAudioProcessor::isFavourite() const
{
    const auto lines = juce::StringArray::fromLines (favouritesFile().loadFileAsString());
    return lines.contains (presetDisplayName());
}

void OspAudioProcessor::toggleFavourite()
{
    // Favourites are plain names in a text file beside the presets (shareable, editable).
    auto lines = juce::StringArray::fromLines (favouritesFile().loadFileAsString());
    lines.removeEmptyStrings();
    const auto name = presetDisplayName();
    if (lines.contains (name))
        lines.removeString (name);
    else
        lines.add (name);
    favouritesFile().getParentDirectory().createDirectory();
    favouritesFile().replaceWithText (lines.joinIntoString ("\n") + "\n");
}

bool OspAudioProcessor::waitForLoads (int timeoutMs)
{
    const auto deadline = juce::Time::getMillisecondCounter() + static_cast<juce::uint32> (timeoutMs);
    while (pendingLoads.load() > 0)
    {
        if (juce::Time::getMillisecondCounter() > deadline)
            return false;
        juce::Thread::sleep (5);
    }
    return true;
}

void OspAudioProcessor::suggestGranularPosition (int index, const LoadedInstrument& instrument)
{
    // Granular POS starts in the recording's stable sustain once the analysis knows it,
    // unless the musician has moved POS for this sample.
    auto& layer = layers[static_cast<std::size_t> (index)];
    if (instrument.model == nullptr || layer.autoPositionLoad == instrument.loadId)
        return;
    const auto& cont = instrument.model->original.continuation;
    const auto* source = instrument.model->original.source.get();
    if (! cont.canSustain || source == nullptr || source->numFrames() <= 0)
        return;
    auto* param = parameters.getParameter (layerParameterId (index, "granular.position"));
    if (param == nullptr)
        return;
    const float current = param->convertFrom0to1 (param->getValue());
    const float untouched = param->convertFrom0to1 (param->getDefaultValue());
    if (std::abs (current - untouched) > 0.05f && std::abs (current - layer.autoPosition) > 0.05f)
        return;
    const double centre = 0.5 * (cont.sustainStartFrame + cont.sustainEndFrame) / static_cast<double> (source->numFrames());
    layer.autoPosition = static_cast<float> (std::clamp (100.0 * centre, 0.0, 100.0));
    layer.autoPositionLoad = instrument.loadId;
    param->setValueNotifyingHost (param->convertTo0to1 (layer.autoPosition));
}

void OspAudioProcessor::timerCallback()
{
    std::deque<Finished> results;
    {
        const std::lock_guard<std::mutex> lock (resultsMutex);
        results.swap (finishedLoads);
    }

    std::array<bool, numLayers> published {};
    std::array<bool, numLayers> touched {};
    std::array<bool, numLayers> lastFailed {};
    for (auto& finished : results)
    {
        const auto index = static_cast<std::size_t> (std::clamp (finished.layer, 0, numLayers - 1));
        auto& layer = layers[index];
        auto& result = finished.result;
        touched[index] = true;
        lastFailed[index] = result.instrument == nullptr;
        juce::String message;
        if (result.instrument != nullptr)
        {
            const std::lock_guard<std::mutex> lock (modelMutex);
            // Ignore results superseded by a newer request that finished first.
            const auto latest = layer.exchange.latestModel();
            if (latest == nullptr || latest->generation < result.instrument->generation)
            {
                layer.exchange.publish (result.instrument);
                published[index] = true;
                const auto loadId = result.instrument->loadId;
                layer.latestByLoad[loadId] = result.instrument;
                if (loadId != layer.lastPublishedLoad)
                {
                    // A new sample (not a later stage of the same one): undoable if the user asked for it.
                    if (userLoads.count (loadId) > 0 && layer.lastPublishedLoad != 0)
                    {
                        undoManager.beginNewTransaction ("Load sample " + layerName (static_cast<int> (index)));
                        undoManager.perform (new InstrumentChangeAction (*this, static_cast<int> (index), layer.lastPublishedLoad, loadId));
                    }
                    layer.lastPublishedLoad = loadId;
                }
                // Keep undo history bounded (the instruments hold audio).
                while (layer.latestByLoad.size() > 8)
                    layer.latestByLoad.erase (layer.latestByLoad.begin());
            }
            if (! result.warnings.empty())
                message = juce::String (result.warnings.front());
        }
        else
            message = juce::String (result.error);

        const std::lock_guard<std::mutex> lock (messageMutex);
        lastMessage = message;
    }

    for (int index = 0; index < numLayers; ++index)
    {
        auto& layer = layers[static_cast<std::size_t> (index)];
        if (published[static_cast<std::size_t> (index)])
        {
            setRootOverride (rootOverride (index), index); // re-derive the shift for the new analysis root
            if (const auto instrument = currentInstrument (index); instrument != nullptr && userLoads.count (instrument->loadId) > 0)
                suggestGranularPosition (index, *instrument);
        }
        if (touched[static_cast<std::size_t> (index)])
            layer.lastLoadFailed = lastFailed[static_cast<std::size_t> (index)];
    }

    bool idle = false;
    {
        // A worker pushes its result before decrementing pendingLoads, so this is race-free.
        const std::lock_guard<std::mutex> lock (resultsMutex);
        idle = finishedLoads.empty() && pendingLoads.load() == 0;
    }
    for (int index = 0; index < numLayers; ++index)
    {
        auto& layer = layers[static_cast<std::size_t> (index)];
        const auto current = currentInstrument (index);
        if (idle)
        {
            // A failed load keeps the previous instrument playable; the error is shown as a message.
            layer.state = current != nullptr ? LoadState::ready : (layer.lastLoadFailed ? LoadState::failed : LoadState::empty);
        }
        else if (current != nullptr && current->loadId == layer.latestLoadId.load())
        {
            // Playable as soon as the first stage is in (spec §62); later stages refine it.
            layer.state = LoadState::ready;
        }
    }

    const std::lock_guard<std::mutex> lock (modelMutex);
    for (auto& layer : layers)
        layer.exchange.collectGarbage();
}

std::shared_ptr<const LoadedInstrument> OspAudioProcessor::currentInstrument (int layer) const
{
    const std::lock_guard<std::mutex> lock (modelMutex);
    auto latest = layers[resolve (layer)].exchange.latestModel();
    return latest != nullptr && latest->model != nullptr ? latest : nullptr;   // an emptied layer reads as empty
}

juce::String OspAudioProcessor::stageMessage() const
{
    const auto instrument = currentInstrument();
    if (instrument == nullptr || instrument->model == nullptr)
        return "Ready";
    if (pendingLoads.load() == 0)
    {
        // Natural pitch needs register anchors, which long recordings skip (memory).
        const bool natural = pitchCharacterParam->load() >= 0.5f;
        if (natural && instrument->set == nullptr && instrument->model->stage == InstrumentModel::Stage::complete
            && instrument->model->anchors.empty())
            return juce::String::fromUTF8 ("Ready \xc2\xb7 Natural pitch needs a recording under a minute: playing as Tape");
        return "Ready";
    }
    switch (instrument->model->stage)
    {
        case InstrumentModel::Stage::provisional: return juce::String::fromUTF8 ("Playable \xc2\xb7 building sustain\xe2\x80\xa6");
        case InstrumentModel::Stage::continued: return juce::String::fromUTF8 ("Playable \xc2\xb7 preparing registers\xe2\x80\xa6");
        case InstrumentModel::Stage::complete: break;
    }
    return "Ready";
}

juce::String OspAudioProcessor::statusMessage() const
{
    const std::lock_guard<std::mutex> lock (messageMutex);
    return lastMessage;
}

//==============================================================================
// Root (per layer)

void OspAudioProcessor::setRootOverride (std::optional<double> midi, int layer)
{
    auto& target = layers[resolve (layer)];
    target.hasRootOverride = midi.has_value();
    if (midi)
        target.rootOverrideMidi = *midi;

    const auto instrument = currentInstrument (static_cast<int> (resolve (layer)));
    const double analysisRoot = instrument != nullptr ? instrument->analysisRootMidi : 60.0;
    target.rootShiftSemitones = midi ? analysisRoot - *midi : 0.0;
}

std::optional<double> OspAudioProcessor::rootOverride (int layer) const
{
    const auto& target = layers[resolve (layer)];
    if (target.hasRootOverride.load())
        return target.rootOverrideMidi.load();
    return std::nullopt;
}

double OspAudioProcessor::effectiveRootMidi (int layer) const
{
    if (const auto overrideMidi = rootOverride (layer))
        return *overrideMidi;
    const auto instrument = currentInstrument (layer);
    return instrument != nullptr ? instrument->analysisRootMidi : 60.0;
}

//==============================================================================
// State

namespace
{
    // Doubles are stored as round-trip-exact text: recall must reproduce the same audio.
    juce::String exactString (double value)
    {
        char buffer[40];
        std::snprintf (buffer, sizeof (buffer), "%.17g", value);
        return buffer;
    }

    double exactValue (const juce::var& v, double fallback)
    {
        const auto text = v.toString();
        return text.isEmpty() ? fallback : std::strtod (text.toRawUTF8(), nullptr);
    }
}

void OspAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (const auto xml = createStateXml())
        copyXmlToBinary (*xml, destData);
}

std::unique_ptr<juce::XmlElement> OspAudioProcessor::createStateXml()
{
    auto stateTree = parameters.copyState();
    stateTree.setProperty ("stateVersion", stateVersion, nullptr);
    stateTree.setProperty ("uiScale", uiScaleFactor.load(), nullptr);
    stateTree.setProperty ("advancedOpen", advancedPanelOpen.load(), nullptr);
    stateTree.setProperty ("program", currentProgram, nullptr);

    stateTree.setProperty ("editLayer", editLayer(), nullptr);
    for (int layer = 0; layer < numLayers; ++layer)
    {
        const auto tree = instrumentTree (layer);
        stateTree.removeChild (stateTree.getChildWithName (tree.getType()), nullptr);
        stateTree.appendChild (tree, nullptr);
    }
    return stateTree.createXml();
}

juce::ValueTree OspAudioProcessor::instrumentTree (int layer)
{
    // Layer A keeps the original "Instrument" tree, so older sessions recall into A.
    juce::ValueTree tree (layer == 0 ? ids::instrument : (layer == 1 ? ids::instrumentB : ids::instrumentC));
    const auto instrument = currentInstrument (layer);
    if (instrument != nullptr)
    {
        tree.setProperty ("contentHash", juce::String (instrument->contentHash), nullptr);
        tree.setProperty ("filename", juce::String (instrument->filename), nullptr);
        tree.setProperty ("originalPath", juce::String (instrument->originalPath), nullptr);
        tree.setProperty ("playbackRootMidi", exactString (instrument->analysisRootMidi), nullptr);
        tree.setProperty ("rootOrigin", juce::String (instrument->rootOrigin), nullptr);
        tree.setProperty ("startSeconds", exactString (instrument->startSeconds), nullptr);
        tree.setProperty ("playbackGainDb", exactString (instrument->playbackGainDb), nullptr);
    }
    if (const auto overrideMidi = rootOverride (layer))
        tree.setProperty ("rootOverride", exactString (*overrideMidi), nullptr);
    if (instrument != nullptr && instrument->set != nullptr)
    {
        // Multi-sample sessions: every member file plus the user's corrections. The set is
        // re-inferred from the (cached) analyses on recall.
        juce::ValueTree setTree ("Set");
        for (const auto& member : instrument->memberFiles)
        {
            juce::ValueTree m ("Member");
            m.setProperty ("contentHash", juce::String (member.contentHash), nullptr);
            m.setProperty ("filename", juce::String (member.filename), nullptr);
            m.setProperty ("originalPath", juce::String (member.originalPath), nullptr);
            setTree.appendChild (m, nullptr);
        }
        for (const auto& a : instrument->assignments)
        {
            juce::ValueTree t ("Assignment");
            t.setProperty ("filename", juce::String (a.filename), nullptr);
            t.setProperty ("role", juce::String (toString (a.role)), nullptr);
            t.setProperty ("layer", a.layer.value_or (0), nullptr);
            if (a.rootMidi)
                t.setProperty ("rootMidi", exactString (*a.rootMidi), nullptr);
            setTree.appendChild (t, nullptr);
        }
        tree.appendChild (setTree, nullptr);
    }
    return tree;
}


void OspAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    const auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName (parameters.state.getType()))
        return;
    applyStateXml (*xml);
}

void OspAudioProcessor::applyStateXml (const juce::XmlElement& xml)
{
    auto stateTree = juce::ValueTree::fromXml (xml);
    uiScaleFactor = std::clamp (static_cast<float> (stateTree.getProperty ("uiScale", 1.0f)), 0.8f, 2.0f);
    advancedPanelOpen = static_cast<bool> (stateTree.getProperty ("advancedOpen", false));
    currentProgram = static_cast<int> (stateTree.getProperty ("program", 0));
    setEditLayer (static_cast<int> (stateTree.getProperty ("editLayer", 0)));
    const std::array<juce::ValueTree, numLayers> layerTrees { stateTree.getChildWithName (ids::instrument),
                                                              stateTree.getChildWithName (ids::instrumentB),
                                                              stateTree.getChildWithName (ids::instrumentC) };
    for (const auto& tree : layerTrees)
        stateTree.removeChild (tree, nullptr);
    const int savedVersion = static_cast<int> (stateTree.getProperty ("stateVersion", 1));
    // Before MOVEMENT v2 the three movement knobs were shared by every mode.
    std::array<std::optional<float>, 3> genericMovement;
    int savedMovementMode = 0;
    for (const auto& child : stateTree)
    {
        const auto id = child["id"].toString();
        if (id == "movement.paramA" || id == "movement.paramB" || id == "movement.paramC")
            genericMovement[static_cast<std::size_t> (id.getLastCharacter() - 'A')] = static_cast<float> (child["value"]);
        else if (id == ids::movementMode)
            savedMovementMode = static_cast<int> (child["value"]);
    }
    parameters.replaceState (stateTree);
    if (savedVersion < 2)
    {
        // v1 sessions were made with the plain sampler: neutral engine settings keep them
        // sounding the same (no variation, velocity = volume, no added sustain or space).
        auto set = [this] (const juce::String& id, float value) {
            if (auto* p = parameters.getParameter (id))
                p->setValueNotifyingHost (p->convertTo0to1 (value));
        };
        set (ids::life, 0.0f);
        set (ids::dynamics, 0.0f);
        set (ids::character, 50.0f);
        set (ids::motion, 0.0f);
        set (ids::space, 0.0f);
        set (ids::reimagined, 0.0f);
        set (ids::pitchCharacter, 0.0f);
        set (ids::sustain, 0.0f);
    }
    if (savedVersion < 3)
    {
        // Before v3 CHARACTER was a tone tilt (neutral at 50); it is now the filter's
        // position, so older sessions open it fully. Their popup settings are the defaults.
        for (const auto& id : shapingIds())
            if (auto* p = parameters.getParameter (id))
                p->setValueNotifyingHost (p->getDefaultValue());
        if (auto* p = parameters.getParameter (ids::character))
            p->setValueNotifyingHost (1.0f);
        if (savedVersion < 2)
        {
            // ...and the plain-sampler sessions get a clean open filter (no drive, resonance,
            // envelope or velocity colour), as close to v1's sound as the filter allows.
            auto set = [this] (const juce::String& id, float value) {
                if (auto* p = parameters.getParameter (id))
                    p->setValueNotifyingHost (p->convertTo0to1 (value));
            };
            set (ids::characterDrive, 0.0f);
            set (ids::characterResonance, 0.0f);
            set (ids::characterEnvAmount, 0.0f);
            set (ids::dynamicsTone, 0.0f);
            set (ids::characterMax, 20000.0f);
        }
    }

    if (savedVersion < 5)
    {
        // MOVEMENT v2: the shared knobs belonged to the mode that was selected; every other
        // mode starts from its defaults. (Versions before 3 had no movement settings.)
        for (const auto* id : { &ids::driftSpeed, &ids::driftPitch, &ids::driftTone, &ids::tapeWow, &ids::tapeFlutter, &ids::tapeWear,
                                &ids::chorusRate, &ids::chorusWidth, &ids::chorusStereo, &ids::pulseRate, &ids::pulseShape, &ids::pulseStereo,
                                &ids::shaperPattern, &ids::shaperRate, &ids::shaperTarget, &ids::shaperSmooth })
            if (auto* p = parameters.getParameter (*id))
                p->setValueNotifyingHost (p->getDefaultValue());
        if (savedVersion >= 3)
        {
            static const std::array<std::array<const juce::String*, 3>, 4> slots { {
                { &ids::driftSpeed, &ids::driftPitch, &ids::driftTone },
                { &ids::tapeWow, &ids::tapeFlutter, &ids::tapeWear },
                { &ids::chorusRate, &ids::chorusWidth, &ids::chorusStereo },
                { &ids::pulseRate, &ids::pulseShape, &ids::pulseStereo } } };
            const auto& target = slots[static_cast<std::size_t> (std::clamp (savedMovementMode, 0, 3))];
            for (std::size_t i = 0; i < 3; ++i)
                if (genericMovement[i].has_value())
                    if (auto* p = parameters.getParameter (*target[i]))
                        p->setValueNotifyingHost (p->convertTo0to1 (*genericMovement[i]));
        }
    }
    if (savedVersion < 4)
    {
        // Before the A/B layers: the session is layer A, heard alone, in One Shot.
        auto reset = [this] (const juce::String& id) {
            if (auto* p = parameters.getParameter (id))
                p->setValueNotifyingHost (p->getDefaultValue());
        };
        reset (ids::blend);
        for (int layer = 0; layer < numLayers; ++layer)
            for (const auto& name : granularNames())
                reset (layerParameterId (layer, name));
    }

    if (savedVersion < 6)
    {
        // Before the adaptive layers: no layer C, no layer controls (neutral: START 0,
        // TUNE 0, centre, 0 dB, forwards, FOLLOW on), the envelope was attack-release only.
        // The global Sustain becomes every layer's LOOP, and Sustain itself goes back to
        // Endless so the two can never disagree.
        auto reset = [this] (const juce::String& id) {
            if (auto* p = parameters.getParameter (id))
                p->setValueNotifyingHost (p->getDefaultValue());
        };
        const bool endless = parameters.getParameter (ids::sustain)->getValue() >= 0.5f;
        for (const auto& name : granularNames())
            reset (layerParameterId (2, name));
        for (int layer = 0; layer < numLayers; ++layer)
            for (const auto& name : layerControlNames())
                reset (layerParameterId (layer, name));
        for (int layer = 0; layer < numLayers; ++layer)
            if (auto* loop = parameters.getParameter (layerParameterId (layer, "loop")))
                loop->setValueNotifyingHost (endless ? 1.0f : 0.0f);
        if (auto* sustain = parameters.getParameter (ids::sustain))
            sustain->setValueNotifyingHost (1.0f);
        for (const auto* id : { &ids::mixX, &ids::mixY, &ids::decay, &ids::sustainLevel })
            reset (*id);
    }

    for (int layer = 0; layer < numLayers; ++layer)
    {
        const auto& tree = layerTrees[static_cast<std::size_t> (layer)];
        if (tree.isValid() && (tree.hasProperty ("contentHash") || tree.getChildWithName ("Set").isValid()))
            recallInstrument (tree, layer);
        else
        {
            if (tree.isValid())
                recallInstrument (tree, layer);   // a root override without a sample (older sessions)
            if (currentInstrument (layer) != nullptr)
                clearLayer (layer);   // the recalled instrument has no such layer
        }
    }
    removedLayer.reset();
}

void OspAudioProcessor::recallInstrument (const juce::ValueTree& tree, int layer)
{
    if (tree.hasProperty ("rootOverride"))
        setRootOverride (exactValue (tree["rootOverride"], 60.0), layer);
    else
        setRootOverride (std::nullopt, layer);

    const auto setTree = tree.getChildWithName ("Set");
    if (setTree.isValid() && setTree.getNumChildren() > 0)
    {
        SetLoadRequest request;
        for (const auto& child : setTree)
        {
            if (child.hasType ("Member"))
            {
                LoadRequest r;
                r.expectedHash = child["contentHash"].toString().toStdString();
                r.filename = child["filename"].toString().toStdString();
                r.originalPath = child["originalPath"].toString().toStdString();
                request.files.push_back (std::move (r));
            }
            else if (child.hasType ("Assignment"))
            {
                SetAssignment a;
                a.filename = child["filename"].toString().toStdString();
                const auto role = child["role"].toString();
                a.role = role == "pitch" ? SampleRole::pitchAnchor
                       : role == "velocity" ? SampleRole::velocityLayer
                       : role == "articulation" ? SampleRole::articulation : SampleRole::roundRobin;
                a.layer = static_cast<int> (child["layer"]);
                if (child.hasProperty ("rootMidi"))
                    a.rootMidi = exactValue (child["rootMidi"], 60.0);
                request.assignments.push_back (a);
            }
        }
        enqueueSetLoad (std::move (request), layer);
        return;
    }

    const auto hash = tree["contentHash"].toString();
    if (hash.isNotEmpty())
    {
        LoadRequest request;
        request.expectedHash = hash.toStdString();
        request.filename = tree["filename"].toString().toStdString();
        request.originalPath = tree["originalPath"].toString().toStdString();
        if (tree.hasProperty ("playbackRootMidi"))
        {
            LoadRequest::SavedPlayback saved;
            saved.rootMidi = exactValue (tree["playbackRootMidi"], 60.0);
            saved.rootOrigin = tree["rootOrigin"].toString().toStdString();
            saved.startSeconds = exactValue (tree["startSeconds"], 0.0);
            saved.gainDb = exactValue (tree["playbackGainDb"], 0.0);
            request.savedPlayback = saved;
        }
        enqueueLoad (std::move (request), layer);
    }
}

int OspAudioProcessor::getNumPrograms()
{
    return static_cast<int> (std::size (startingStates));
}

const juce::String OspAudioProcessor::getProgramName (int index)
{
    return index >= 0 && index < getNumPrograms() ? juce::String (startingStates[index].name) : juce::String();
}

void OspAudioProcessor::setCurrentProgram (int index)
{
    if (index < 0 || index >= getNumPrograms())
        return;
    currentProgram = index;
    presetIsProgram = true;
    const auto& s = startingStates[index];
    auto set = [this] (const juce::String& id, float value) {
        if (auto* p = parameters.getParameter (id))
            p->setValueNotifyingHost (p->convertTo0to1 (value));
    };
    set (ids::life, s.life);
    set (ids::dynamics, s.dynamics);
    set (ids::character, s.character);
    set (ids::motion, s.motion);
    set (ids::space, s.space);
    set (ids::reimagined, s.reimagined);
    set (ids::attack, s.attackMs);
    set (ids::release, s.releaseMs);
}

//==============================================================================
// Undo / redo of sample loads and root changes (spec §56)

void OspAudioProcessor::changeRootOverride (std::optional<double> midi, int layer)
{
    const int index = static_cast<int> (resolve (layer));
    undoManager.beginNewTransaction ("Root " + layerName (index));
    undoManager.perform (new RootChangeAction (*this, index, rootOverride (index), midi));
}

void OspAudioProcessor::republish (std::shared_ptr<const LoadedInstrument> instrument, int layer)
{
    if (instrument == nullptr)
        return;
    // A copy with a fresh generation keeps ModelExchange's ordering intact; the models
    // and audio are shared, nothing is rebuilt.
    auto copy = std::make_shared<LoadedInstrument> (*instrument);
    copy->generation = nextGeneration.fetch_add (1);
    auto& target = layers[resolve (layer)];
    {
        const std::lock_guard<std::mutex> lock (modelMutex);
        target.exchange.publish (copy);
    }
    target.lastPublishedLoad = copy->loadId;
    setRootOverride (rootOverride (layer), layer);
}

//==============================================================================
// Presets and portable instruments

bool OspAudioProcessor::savePreset (const juce::File& file)
{
    const auto xml = createStateXml();
    file.getParentDirectory().createDirectory();
    if (xml == nullptr || ! xml->writeTo (file))
        return false;
    lastPresetFile = file;
    return true;
}

bool OspAudioProcessor::loadPreset (const juce::File& file)
{
    const auto xml = juce::XmlDocument::parse (file);
    if (xml == nullptr || ! xml->hasTagName (parameters.state.getType()))
        return false;
    applyStateXml (*xml);
    lastPresetFile = file;
    presetIsProgram = false;
    return true;
}

juce::File OspAudioProcessor::presetFolder()
{
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("OSP/Presets");
}

juce::File OspAudioProcessor::instrumentFolder()
{
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("OSP/Instruments");
}

juce::Array<juce::File> OspAudioProcessor::findFiles (const juce::File& folder, const juce::String& extension)
{
    juce::Array<juce::File> files;
    if (folder.isDirectory())
        for (const auto& entry : juce::RangedDirectoryIterator (folder, true, "*" + extension, juce::File::findFiles))
            if (! entry.isHidden())
                files.add (entry.getFile());
    std::sort (files.begin(), files.end(), [&folder] (const juce::File& a, const juce::File& b) {
        return a.getRelativePathFrom (folder).compareNatural (b.getRelativePathFrom (folder)) < 0;
    });
    return files;
}

bool OspAudioProcessor::stepPreset (int delta, const juce::File& root)
{
    const auto folder = lastPresetFile.existsAsFile() && ! lastPresetFile.isAChildOf (root) ? lastPresetFile.getParentDirectory() : root;
    const auto files = findFiles (folder, presetExtension);
    if (files.isEmpty())
        return false;
    const int current = files.indexOf (lastPresetFile);
    const int count = files.size();
    const int next = current < 0 ? (delta >= 0 ? 0 : count - 1) : ((current + delta) % count + count) % count;
    return loadPreset (files[next]);
}

bool OspAudioProcessor::exportInstrument (const juce::File& file, juce::String& error)
{
    std::vector<LoadedInstrument::MemberFile> members;
    for (int layer = 0; layer < numLayers; ++layer)
        if (const auto instrument = currentInstrument (layer))
        {
            if (instrument->memberFiles.empty())
                members.push_back ({ instrument->contentHash, instrument->filename, instrument->originalPath });
            else
                members.insert (members.end(), instrument->memberFiles.begin(), instrument->memberFiles.end());
        }
    if (members.empty())
    {
        error = "nothing to export: load a sound first";
        return false;
    }

    juce::ZipFile::Builder zip;
    auto manifest = std::make_unique<juce::DynamicObject>();
    manifest->setProperty ("schemaVersion", 1);
    manifest->setProperty ("format", "OSP portable instrument");
    manifest->setProperty ("engineVersion", JucePlugin_VersionString);
    manifest->setProperty ("stateVersion", stateVersion);
    juce::Array<juce::var> files;
    for (const auto& m : members)
    {
        const auto hash = juce::String (m.contentHash).fromFirstOccurrenceOf ("sha256:", false, false);
        const auto stored = store.find (hash.toStdString());
        if (! stored)
        {
            error = "the sample " + juce::String (m.filename) + " is missing from the sample store";
            return false;
        }
        zip.addFile (*stored, 0, "source/" + stored->getFileName());
        const auto analysis = store.analysisCacheFor (hash.toStdString());
        if (analysis.existsAsFile())
            zip.addFile (analysis, 9, "analysis/" + analysis.getFileName());
        auto entry = std::make_unique<juce::DynamicObject>();
        entry->setProperty ("contentHash", juce::String (m.contentHash));
        entry->setProperty ("filename", juce::String (m.filename));
        entry->setProperty ("stored", "source/" + stored->getFileName());
        files.add (juce::var (entry.release()));
    }
    manifest->setProperty ("sources", files);
    const auto manifestText = juce::JSON::toString (juce::var (manifest.release()));
    zip.addEntry (new juce::MemoryInputStream (manifestText.toRawUTF8(), manifestText.getNumBytesAsUTF8(), true), 9, "manifest.json",
                  juce::Time::getCurrentTime());
    const auto xml = createStateXml();
    const auto preset = xml->toString();
    zip.addEntry (new juce::MemoryInputStream (preset.toRawUTF8(), preset.getNumBytesAsUTF8(), true), 9, "preset.xml", juce::Time::getCurrentTime());

    file.deleteFile();
    juce::FileOutputStream out (file);
    if (! out.openedOk() || ! zip.writeToStream (out, nullptr))
    {
        error = "cannot write " + file.getFullPathName();
        return false;
    }
    return true;
}

bool OspAudioProcessor::importInstrument (const juce::File& file, juce::String& error)
{
    juce::ZipFile zip (file);
    if (zip.getNumEntries() == 0 || zip.getEntry ("preset.xml") == nullptr)
    {
        error = file.getFileName() + " is not an OSP instrument";
        return false;
    }
    // Sources and analyses go into the managed store under their hash names; the preset
    // then recalls them from the store exactly like a reopened session.
    store.directory().createDirectory();
    for (int i = 0; i < zip.getNumEntries(); ++i)
    {
        const auto* entry = zip.getEntry (i);
        const auto name = entry->filename;
        if (! name.startsWith ("source/") && ! name.startsWith ("analysis/"))
            continue;
        const auto target = store.directory().getChildFile (name.fromFirstOccurrenceOf ("/", false, false));
        if (target.existsAsFile() || target.getFileName().isEmpty() || target.getFileName().contains (".."))
            continue;
        std::unique_ptr<juce::InputStream> in (zip.createStreamForEntry (i));
        juce::FileOutputStream out (target);
        if (in == nullptr || ! out.openedOk())
        {
            error = "cannot extract " + name;
            return false;
        }
        out.writeFromInputStream (*in, -1);
    }
    std::unique_ptr<juce::InputStream> presetStream (zip.createStreamForEntry (*zip.getEntry ("preset.xml")));
    const auto xml = presetStream != nullptr ? juce::XmlDocument::parse (presetStream->readEntireStreamAsString()) : nullptr;
    if (xml == nullptr || ! xml->hasTagName (parameters.state.getType()))
    {
        error = "the instrument's settings are unreadable";
        return false;
    }
    applyStateXml (*xml);
    return true;
}

juce::AudioProcessorEditor* OspAudioProcessor::createEditor()
{
    return new OspAudioProcessorEditor (*this);
}

} // namespace osp::plugin

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new osp::plugin::OspAudioProcessor();
}
