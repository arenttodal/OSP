#include "PluginProcessor.h"
#include "AndorParameters.h"
#include "ValueFormat.h"

#include "LibraryPlaces.h"
#include "PluginEditor.h"

#include "audio/utility/TestSignals.h"
#include "core/PitchMath.h"
#include "io/AudioFileIO.h"
#include "io/ContentHash.h"
#include "io/NoteAtlasJson.h"
#include "library/Inbox.h"
#include "library/Classifier.h"
#include "analysis/phrase/PhraseEdits.h"

#include <algorithm>
#include <map>
#include <set>
#include <cstdio>
#include <cstdlib>

namespace osp::plugin
{

namespace
{
    /** CC 1-4's controllers at first: 2 (breath), 11 (expression), 16 and 17 (general purpose). */
    int defaultModController (int slot) noexcept
    {
        static constexpr std::array<int, mod::ccSlots> numbers { 2, 11, 16, 17 };
        return numbers[static_cast<std::size_t> (std::clamp (slot, 0, mod::ccSlots - 1))];
    }
}

namespace ids
{
    // OSP's own state trees (the parameter IDs are the shared ANDOR schema: AndorParameters.h).
    static const juce::Identifier instrument = "Instrument";
    static const juce::Identifier instrumentB = "InstrumentB";
    static const juce::Identifier instrumentC = "InstrumentC";
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
    // The ANDOR parameter schema (shared with every product built from the ANDOR Instrument
    // Shell): ANDOR OSP's own, frozen (tests/plugin/golden/osp-parameter-schema.txt).
    return createParameterLayout();
}

const juce::StringArray& OspAudioProcessor::layerStateNames()
{
    static const juce::StringArray names { "mute", "solo" };
    return names;
}

const juce::StringArray& OspAudioProcessor::reimaginedModeNames()
{
    static const juce::StringArray names { "mode",
                                           "kaleidoscope.focus", "kaleidoscope.spread",
                                           "tapeFrame.age", "tapeFrame.stability", "tapeFrame.frame",
                                           "toybox.motion", "toybox.digital", "toybox.play",
                                           "mosaic.detail", "mosaic.motion", "mosaic.model",
                                           "mirage.clock", "mirage.filter", "mirage.tone" };
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

/** Phase 5: an edit of a layer's Note Atlas (NOTE MAP). Undo and redo show the instrument of
    either state at once when it is still cached, and otherwise rebuild it from its atlas. */
class AtlasEditAction final : public juce::UndoableAction
{
public:
    AtlasEditAction (OspAudioProcessor& p, int l, std::uint64_t beforeLoadId, std::uint64_t afterLoadId,
                     std::shared_ptr<const atlas::NoteAtlas> beforeAtlas, std::shared_ptr<const atlas::NoteAtlas> afterAtlas)
        : processor (p), layer (l), beforeLoad (beforeLoadId), afterLoad (afterLoadId), before (std::move (beforeAtlas)), after (std::move (afterAtlas))
    {
    }
    bool perform() override
    {
        if (first)
        {
            first = false;   // the edit itself already published it
            return true;
        }
        return processor.showAtlasState (layer, afterLoad, after);
    }
    bool undo() override { return processor.showAtlasState (layer, beforeLoad, before); }
    int getSizeInUnits() override { return 1 + static_cast<int> (before != nullptr ? before->notes.size() : 0); }

private:
    OspAudioProcessor& processor;
    int layer;
    std::uint64_t beforeLoad, afterLoad;
    std::shared_ptr<const atlas::NoteAtlas> before, after;
    bool first = true;
};

/** Phase 5: a change of a layer's KEY DNA (the editor), its drawn curves included. */
class KeyDnaChangeAction final : public juce::UndoableAction
{
public:
    using Curves = std::array<std::optional<mod::Curve>, keydna::paramCount>;
    KeyDnaChangeAction (OspAudioProcessor& p, int l, keydna::Settings beforeSettings, Curves beforeCurves, keydna::Settings afterSettings, Curves afterCurves)
        : processor (p), layer (l), from (std::move (beforeSettings)), to (std::move (afterSettings)), fromCurves (beforeCurves), toCurves (afterCurves)
    {
    }
    bool perform() override { return apply (to, toCurves); }
    bool undo() override { return apply (from, fromCurves); }

private:
    bool apply (const keydna::Settings& settings, const Curves& curves)
    {
        {
            const std::lock_guard<std::mutex> lock (processor.keyDnaMutex);
            processor.keyDnaCurves[static_cast<std::size_t> (layer)] = curves;
        }
        processor.setKeyDnaSettings (layer, settings);
        return true;
    }
    OspAudioProcessor& processor;
    int layer;
    keydna::Settings from, to;
    Curves fromCurves, toCurves;
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
    echoParam = parameters.getRawParameterValue (ids::echo);
    driveParam = parameters.getRawParameterValue (ids::drive);
    arpEnabledParam = parameters.getRawParameterValue (ids::arpEnabled);
    arpPatternParam = parameters.getRawParameterValue (ids::arpPattern);
    arpRateParam = parameters.getRawParameterValue (ids::arpRate);
    arpGateParam = parameters.getRawParameterValue (ids::arpGate);
    arpOctavesParam = parameters.getRawParameterValue (ids::arpOctaves);
    arpSwingParam = parameters.getRawParameterValue (ids::arpSwing);
    arpVelocityParam = parameters.getRawParameterValue (ids::arpVelocity);
    for (int i = 0; i < 2; ++i)
    {
        auto& l = modLfoParams[static_cast<std::size_t> (i)];
        const char* names[] { "shape", "rate", "sync", "division", "phase", "polarity", "mode", "scope" };
        for (std::size_t k = 0; k < l.size(); ++k)
            l[k] = parameters.getRawParameterValue (modLfoId (i, names[k]));
        auto& e = modEnvParams[static_cast<std::size_t> (i)];
        const char* envNames[] { "mode", "attack", "decay", "sustain", "release", "curve", "length" };
        for (std::size_t k = 0; k < e.size(); ++k)
            e[k] = parameters.getRawParameterValue (modEnvId (i, envNames[k]));
    }
    for (int r = 0; r < mod::maxRoutes; ++r)
    {
        auto& rp = modRouteParams[static_cast<std::size_t> (r)];
        const char* names[] { "source", "dest", "depth", "enabled", "destExt" };
        for (std::size_t k = 0; k < rp.size(); ++k)
            rp[k] = parameters.getRawParameterValue (modRouteId (r, names[k]));
    }
    libraryHub = std::make_unique<LibraryService> (store.directory());
    auditionHub = std::make_unique<Audition> (preview, *libraryHub, store.directory(),
                                              [this] (const std::vector<Audition::LayerLoad>& sounds, const juce::String& label) {
                                                  return loadLibrarySounds (sounds, label);
                                              });
    // The Inbox once at startup (in the background), so what arrived while the host was closed
    // is waiting when the Library opens; the open Library window keeps checking.
    if (const auto folders = auditionHub->librarySettings().inboxFolders; ! folders.isEmpty())
    {
        std::vector<std::filesystem::path> paths;
        for (const auto& f : folders)
            paths.emplace_back (f.toStdString());
        libraryHub->postBackground ([paths] (library::Catalog& c) { library::reconcileInbox (c, paths, juce::Time::currentTimeMillis()); });
    }
    // Factory packs shipped with the plugin, when new or newer (user content is never touched).
    if (const auto factory = factoryPackSource(); factory.isDirectory())
        libraryHub->postBackground ([folder = std::filesystem::path (factory.getFullPathName().toStdString()), places = packPlaces()] (library::Catalog& c) {
            library::installFactoryPacks (c, folder, places);
        });
    parameters.state.addListener (this);   // META-MODULATION's links (ModLinks)
    refreshModLinks();
    modCurves = defaultModulationCurves();
    publishModulationCurves();
    for (int slot = 0; slot < mod::ccSlots; ++slot)
        modCcNumber[static_cast<std::size_t> (slot)].store (defaultModController (slot));
    lastModValues.fill (-1.0e9f);
    customPattern = RhythmicShaper::patternSteps (ShaperParams().pattern);   // CUSTOM starts as THREE
    publishCustomPattern();
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
        lp.mute = parameters.getRawParameterValue (layerParameterId (layer, "mute"));
        lp.solo = parameters.getRawParameterValue (layerParameterId (layer, "solo"));
        if (layer > 0)
            lp.reimagined = parameters.getRawParameterValue (reimaginedParameterId (layer));
        for (int i = 0; i < reimaginedModeNames().size(); ++i)
            lp.modes[static_cast<std::size_t> (i)] = parameters.getRawParameterValue (reimaginedModeParameterId (layer, reimaginedModeNames()[i]));
        lp.lastModes.fill (-1.0e9f);
        jassert (eqParameterNames().size() == static_cast<int> (lp.eq.size()));
        for (int i = 0; i < eqParameterNames().size(); ++i)
            lp.eq[static_cast<std::size_t> (i)] = parameters.getRawParameterValue (eqParameterId (layer, eqParameterNames()[i]));
        lp.lastEq.fill (-1.0e9f);
        for (int i = 0; i < dampenParameterNames().size(); ++i)
            lp.dampen[static_cast<std::size_t> (i)] = parameters.getRawParameterValue (dampenParameterId (layer, dampenParameterNames()[i]));
        lp.lastDampen.fill (-1.0e9f);
        lp.grainOptions = { parameters.getRawParameterValue (layerParameterId (layer, "granular.scatter")),
                            parameters.getRawParameterValue (layerParameterId (layer, "granular.pan")),
                            parameters.getRawParameterValue (layerParameterId (layer, "granular.window")) };
        lp.keyDnaAmount = parameters.getRawParameterValue (layerParameterId (layer, "keyDna.amount"));
        lp.synthesize = parameters.getRawParameterValue (layerParameterId (layer, "synthesize"));
    }
    grainBudgetParam = parameters.getRawParameterValue (ids::grainBudget);
    for (int layer = 0; layer < numLayers; ++layer)
    {
        juce::StringArray ids { reimaginedParameterId (layer) };
        for (const auto& name : reimaginedModeNames())
            ids.add (reimaginedModeParameterId (layer, name));
        for (const auto& pid : ids)
            if (auto* p = parameters.getParameter (pid))
            {
                reimaginedParameters.push_back (p);
                p->addListener (this);
            }
    }
    mixXParam = parameters.getRawParameterValue (ids::mixX);
    mixYParam = parameters.getRawParameterValue (ids::mixY);
    decayParam = parameters.getRawParameterValue (ids::decay);
    sustainLevelParam = parameters.getRawParameterValue (ids::sustainLevel);
    voiceModeParam = parameters.getRawParameterValue (ids::voiceMode);
    glideParam = parameters.getRawParameterValue (ids::glide);

    engineSettings.polyphony = 24;
    engineSettings.outputGainDb = -9.0;
    engine.prepare (48000.0, 512, engineSettings);

    startTimerHz (20);
}

OspAudioProcessor::~OspAudioProcessor()
{
    stopTimer();
    parameters.state.removeListener (this);
    auditionHub.reset();  // its decoding finishes first; it posts to the catalog thread
    libraryHub.reset();   // finishes the queued catalog work (short jobs)
    for (auto* p : reimaginedParameters)
        if (p != nullptr)
            p->removeListener (this);
    // A running load stage queues the next one, and a stage can outlast any fixed timeout
    // (a long file, a slow machine): stop new stages first, then wait until the pool is
    // really empty. A job must never outlive the members it reports into.
    closing = true;
    for (auto& layer : layers)
        if (layer.cancelPreparation != nullptr)
            *layer.cancelPreparation = true;   // an analysis being waited for stops at its next check
    while (! loaderPool.removeAllJobs (true, 10000) || loaderPool.getNumJobs() > 0)
    {
    }
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
    // The arpeggiator's merged MIDI: room for any realistic block, so the audio thread
    // never grows it (MidiBuffer stores 4 bytes of header plus the message per event).
    arpeggiator.prepare (sampleRate);
    arpMidi.ensureSize (static_cast<std::size_t> (Arpeggiator::maxEvents + 4096) * 16);
    previewMidi.ensureSize (4096 * 16);
    preview.prepare (sampleRate);
    previewKeysActive = false;
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

    const auto routing = perLayerReimagined.load() ? ReimaginedRouting::perLayer : ReimaginedRouting::legacyGlobal;
    if (force || routing != engineSettings.reimaginedRouting)
    {
        engineSettings.reimaginedRouting = routing;
        engine.setReimaginedRouting (routing);
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
    if (const auto generation = customGeneration.load (std::memory_order_acquire); generation != appliedCustomGeneration)
    {
        appliedCustomGeneration = generation;
        shapingChanged = true;
    }
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
    std::array<std::atomic<float>*, 8> macroParams { lifeParam, dynamicsParam, characterParam, motionParam, spaceParam, reimaginedParam, echoParam, driveParam };
    std::array<double, 8> values {};
    for (std::size_t i = 0; i < values.size(); ++i)
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
    // The mod wheel opens MOTION up, unless it is routed as a modulation source: then it does
    // what its routes say and nothing else (sessions without a wheel route are unchanged).
    bool wheelRouted = false;
    for (const auto& route : modSettings.routes)
        wheelRouted = wheelRouted || (route.source == mod::Source::modWheel && route.dest != mod::Dest::none);
    macros.motion = wheelRouted ? values[3] : values[3] + modWheel * (1.0 - values[3]);
    macros.space = values[4];
    macros.reimagined = values[5];
    macros.echo = values[6];
    macros.drive = values[7];
    engine.setMacros (macros);
    engineSettings.macros = macros;
    engine.setMpe (mpeParam->load() >= 0.5f);
    engineSettings.mono = voiceModeParam->load() >= 0.5f;
    engineSettings.glideSeconds = glideParam->load() / 1000.0;
    engine.setMono (engineSettings.mono);
    engine.setGlideSeconds (engineSettings.glideSeconds);
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
    engine.setMixSlots (keptSlotCount.load());
    // SOLO wins: with any layer soloed only the soloed ones are heard; else MUTE decides.
    bool anySolo = false;
    for (const auto& lp : layerParams)
        anySolo = anySolo || lp.solo->load() >= 0.5f;
    for (int layer = 0; layer < numLayers; ++layer)
    {
        auto& lp = layerParams[static_cast<std::size_t> (layer)];
        const bool audible = anySolo ? lp.solo->load() >= 0.5f : lp.mute->load() < 0.5f;
        {
            // The layer's EQ (before the rest of its settings, which carry it).
            std::array<float, 19> values {};
            for (std::size_t i = 0; i < values.size(); ++i)
                values[i] = lp.eq[i]->load();
            if (force || values != lp.lastEq)
            {
                lp.lastEq = values;
                lp.eqSettings = eqFromValues (values);
                engineSettings.layer[static_cast<std::size_t> (layer)].eq = lp.eqSettings;
                engine.setLayerEq (layer, lp.eqSettings);
            }
        }
        {
            // DAMPEN: its settings and its curve over the keys (published with the others).
            std::array<float, 5> values {};
            for (std::size_t i = 0; i < values.size(); ++i)
                values[i] = lp.dampen[i]->load();
            const auto generation = modCurveGeneration.load (std::memory_order_acquire);
            if (force || values != lp.lastDampen || generation != lp.dampenGeneration)
            {
                lp.lastDampen = values;
                lp.dampenGeneration = generation;
                auto& d = lp.dampenSettings;
                d.enabled = values[0] >= 0.5f;
                d.mode = values[1] >= 0.5f ? dampen::Mode::lowPass : dampen::Mode::softShelf;
                d.amount = std::clamp (values[2] / 100.0, 0.0, 1.0);
                d.shelfHz = values[3];
                d.minCutoffHz = values[4];
                const auto c = static_cast<std::size_t> (dampenCurveIndex (layer));
                for (std::size_t i = 0; i < d.curve.size(); ++i)
                    d.curve[i] = modCurveValues[c * mod::curvePoints + i].load (std::memory_order_relaxed);
                engine.setLayerDampen (layer, d);
            }
        }
        engine.setLayerPitchOffsetSemitones (layer, layers[static_cast<std::size_t> (layer)].rootShiftSemitones.load());
        std::array<float, 8> controls {};
        for (std::size_t i = 0; i < controls.size(); ++i)
            controls[i] = lp.controls[i]->load();
        const float ownReimagined = lp.reimagined != nullptr ? lp.reimagined->load() : -1.0f;
        std::array<float, 15> modes {};
        for (std::size_t i = 0; i < modes.size(); ++i)
            modes[i] = lp.modes[i]->load();
        if (force || controls != lp.lastControls || ! juce::exactlyEqual (ownReimagined, lp.lastReimagined) || modes != lp.lastModes
            || audible != lp.lastAudible)
        {
            lp.lastControls = controls;
            lp.lastAudible = audible;
            lp.lastReimagined = ownReimagined;
            lp.lastModes = modes;
            LayerSettings ls;
            ls.start = 0.01 * controls[0];
            ls.tuneSemitones = controls[1];
            ls.pan = 0.01 * controls[2];
            ls.levelDb = controls[3];
            ls.reverse = controls[5] >= 0.5f;
            ls.loop = controls[6] >= 0.5f;
            ls.follow = controls[7] >= 0.5f;
            ls.audible = audible;
            ls.eq = lp.eqSettings;
            ls.reimagined = ownReimagined < 0.0f ? -1.0 : 0.01 * ownReimagined;   // A follows the instrument's amount
            auto& rs = ls.reimaginedSettings;
            auto pick = [&modes] (std::size_t i) { return static_cast<int> (std::lround (modes[i])); };
            rs.mode = reimagined::modeFromIndex (pick (0));
            rs.kaleidoscope = { 0.01 * modes[1], 0.01 * modes[2] };
            rs.tapeFrame = { 0.01 * modes[3], 0.01 * modes[4], static_cast<TapeFrameLength> (std::clamp (pick (5), 0, 2)) };
            rs.toybox = { 0.01 * modes[6], 0.01 * modes[7], static_cast<ToyboxPlay> (std::clamp (pick (8), 0, 2)) };
            rs.mosaic = { 0.01 * modes[9], 0.01 * modes[10], static_cast<MosaicModel> (std::clamp (pick (11), 0, 1)) };
            rs.mirage = { 0.01 * modes[12], 0.01 * modes[13], static_cast<MirageTone> (std::clamp (pick (14), 0, 1)) };
            ls.synthesize = 0.01 * lp.synthesize->load();
            engineSettings.layer[static_cast<std::size_t> (layer)] = ls;
            engine.setLayerSettings (layer, ls);
        }
        std::array<float, 9> now { lp.mode->load(), lp.granular[0]->load(), lp.granular[1]->load(), lp.granular[2]->load(),
                                   lp.granular[3]->load(), lp.granular[4]->load(), lp.grainOptions[0]->load(), lp.grainOptions[1]->load(),
                                   lp.grainOptions[2]->load() };
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
            g.scatterSemitones = now[6];
            g.pan = 0.01 * now[7];
            g.window = static_cast<GrainWindow> (std::clamp (static_cast<int> (std::lround (now[8])), 0, 2));
            engineSettings.granular[static_cast<std::size_t> (layer)] = g;
            engine.setGranular (layer, g);
        }
        // KEY DNA AMOUNT: live (the voices smooth it); a plain number, every block.
        engine.setLayerKeyDnaAmount (layer, 0.01 * lp.keyDnaAmount->load());
        // SYNTHESIZE: live as well (the voices smooth it over ~20 ms).
        const double synthesize = 0.01 * lp.synthesize->load();
        engineSettings.layer[static_cast<std::size_t> (layer)].synthesize = synthesize;
        engine.setLayerSynthesize (layer, synthesize);
    }
    engineSettings.grainBudget = grainBudgetGrains (grainBudgetParam->load());
    engine.setGrainBudget (engineSettings.grainBudget);
    engineSettings.grainBudgetPolicy = boundedGrainBudget.load() ? GrainBudgetPolicy::bounded : GrainBudgetPolicy::shared;
    engine.setGrainBudgetPolicy (engineSettings.grainBudgetPolicy);
    applyModulation (force);
}

void OspAudioProcessor::applyModulation (bool force) noexcept
{
    // The modulation settings change rarely: they are gathered and compiled only when a
    // value or a curve moved (a plain copy into the engine, no allocation).
    std::array<float, numModValues> now {};
    std::size_t k = 0;
    for (const auto& l : modLfoParams)
        for (auto* p : l)
            now[k++] = p->load();
    for (const auto& e : modEnvParams)
        for (auto* p : e)
            now[k++] = p->load();
    for (const auto& r : modRouteParams)
        for (auto* p : r)
            now[k++] = p->load();
    for (const auto& t : modRouteTargets)
        now[k++] = static_cast<float> (t.load (std::memory_order_relaxed));
    const auto generation = modCurveGeneration.load (std::memory_order_acquire);
    const auto seed = static_cast<std::uint64_t> (std::max (1.0f, seedParam->load()));
    if (! force && now == lastModValues && generation == appliedModCurveGeneration && seed == modSettings.seed)
        return;
    lastModValues = now;
    auto& s = modSettings;
    s.seed = seed;
    for (std::size_t i = 0; i < 2; ++i)
    {
        const auto& l = modLfoParams[i];
        auto& lfo = s.lfo[i];
        lfo.shape = static_cast<mod::LfoShape> (std::clamp (juce::roundToInt (l[0]->load()), 0, mod::lfoShapeCount - 1));
        lfo.rateHz = l[1]->load();
        lfo.sync = l[2]->load() >= 0.5f;
        lfo.division = std::clamp (juce::roundToInt (l[3]->load()), 0, mod::syncDivisionCount - 1);
        lfo.phase = 0.01 * l[4]->load();
        lfo.bipolar = l[5]->load() < 0.5f;
        lfo.mode = static_cast<mod::LfoMode> (std::clamp (juce::roundToInt (l[6]->load()), 0, 2));
        lfo.scope = l[7]->load() >= 0.5f ? mod::Scope::poly : mod::Scope::global;
        const auto& e = modEnvParams[i];
        auto& env = s.env[i];
        env.oneShotCurve = e[0]->load() >= 0.5f;
        env.attackSeconds = 0.001 * e[1]->load();
        env.decaySeconds = 0.001 * e[2]->load();
        env.sustain = 0.01 * e[3]->load();
        env.releaseSeconds = 0.001 * e[4]->load();
        env.curve = 0.01 * e[5]->load();
        env.lengthSeconds = 0.001 * e[6]->load();
    }
    for (std::size_t r = 0; r < s.routes.size(); ++r)
    {
        const auto& rp = modRouteParams[r];
        auto& route = s.routes[r];
        route.source = static_cast<mod::Source> (std::clamp (juce::roundToInt (rp[0]->load()), 0, mod::sourceCount));
        route.dest = mod::routeDestFromChoices (std::clamp (juce::roundToInt (rp[1]->load()), 0, mod::hostDestCount - 1), juce::roundToInt (rp[4]->load()));
        route.depth = 0.01 * rp[2]->load();
        route.enabled = rp[3]->load() >= 0.5f;
        route.target = modRouteTargets[r].load (std::memory_order_relaxed);
    }
    if (generation != appliedModCurveGeneration || force)
    {
        appliedModCurveGeneration = generation;
        for (std::size_t c = 0; c < 6; ++c)   // (6.. are DAMPEN's: the layers read them)
        {
            auto& table = c < 2 ? s.lfoCurve[c] : c < 4 ? s.envCurve[c - 2] : c == 4 ? s.velCurve : s.noteCurve;
            for (std::size_t i = 0; i < table.size(); ++i)
                table[i] = modCurveValues[c * mod::curvePoints + i].load (std::memory_order_relaxed);
        }
    }
    s.velocityToAmp = velToAmp.load (std::memory_order_relaxed);
    engine.setModulation (s);
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
        ids::lifeCharacter, ids::lifeTakes, ids::lifeTakeOrder, ids::lifeTakesSeed,
        ids::spacePreDelay, ids::spaceSize, ids::spaceDamping, ids::spaceModulation, ids::spaceWidth, ids::spaceLowCut, ids::spaceHighCut,
        ids::echoType, ids::echoSync, ids::echoDivision, ids::echoTime, ids::echoFeedback, ids::echoTone, ids::echoAge, ids::echoStereo,
        ids::shaperCustom,
        ids::driveMode, ids::driveTone, ids::driveBody,
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
    s.lifeCharacter = static_cast<LifeCharacter> (index (33, 4));
    const int takes = index (34, PerformanceEngine::maxTakes);
    s.lifeTakes = takes == 0 ? 0 : takes + 1;   // endless, 2, 3 .. 16
    s.lifeTakeOrder = static_cast<LifeTakeOrder> (index (35, 2));
    s.lifeTakesSeed = static_cast<std::uint32_t> (std::max (0L, std::lround (v (36))));
    s.spacePreDelayMs = v (37);
    s.spaceSize = 0.01 * v (38);
    s.spaceDamping = 0.01 * v (39);
    s.spaceModulation = 0.01 * v (40);
    s.spaceWidth = 0.01 * v (41);
    s.spaceLowCutHz = v (42);
    s.spaceHighCutHz = v (43);
    s.echoType = static_cast<EchoType> (index (44, 2));
    s.echoSync = v (45) >= 0.5;
    s.echoDivision = index (46, shaping::echoDivisionCount);
    s.echoTimeMs = v (47);
    s.echoFeedback = 0.01 * v (48);
    s.echoTone = 0.01 * v (49);
    s.echoAge = 0.01 * v (50);
    s.echoStereo = static_cast<EchoStereo> (index (51, 3));
    s.shaper.custom = v (52) >= 0.5;
    s.driveMode = static_cast<DriveMode> (index (53, 3));
    s.driveTone = 0.01 * v (54);
    s.driveBody = 0.01 * v (55);
    for (std::size_t i = 0; i < s.shaper.customSteps.size(); ++i)
    {
        auto& step = s.shaper.customSteps[i];
        step.start = customStepValues[3 * i].load (std::memory_order_relaxed);
        step.end = customStepValues[3 * i + 1].load (std::memory_order_relaxed);
        step.shape = static_cast<StepShape> (std::clamp (static_cast<int> (customStepValues[3 * i + 2].load (std::memory_order_relaxed)), 0, 5));
    }
    return s;
}

//==============================================================================
// SHAPER CUSTOM
namespace
{
    class CustomPatternAction final : public juce::UndoableAction
    {
    public:
        CustomPatternAction (OspAudioProcessor& p, const ShaperPattern& before, const ShaperPattern& after) : processor (p), from (before), to (after) {}
        bool perform() override
        {
            processor.setShaperCustomPattern (to, false);
            return true;
        }
        bool undo() override
        {
            processor.setShaperCustomPattern (from, false);
            return true;
        }

    private:
        OspAudioProcessor& processor;
        ShaperPattern from, to;
    };
}

namespace
{
    class ModCurveAction final : public juce::UndoableAction
    {
    public:
        ModCurveAction (OspAudioProcessor& p, int i, const mod::Curve& before, const mod::Curve& after) : processor (p), index (i), from (before), to (after) {}
        bool perform() override
        {
            processor.setModulationCurve (index, to, false);
            return true;
        }
        bool undo() override
        {
            processor.setModulationCurve (index, from, false);
            return true;
        }

    private:
        OspAudioProcessor& processor;
        int index;
        mod::Curve from, to;
    };
}

void OspAudioProcessor::setVelocityAppliesToAmp (bool on) noexcept
{
    velToAmp.store (on, std::memory_order_relaxed);
    modCurveGeneration.fetch_add (1, std::memory_order_release);   // the audio thread re-reads the settings
}

int OspAudioProcessor::modControllerNumber (int slot) const noexcept
{
    return slot >= 0 && slot < mod::ccSlots ? modCcNumber[static_cast<std::size_t> (slot)].load (std::memory_order_relaxed) : -1;
}

void OspAudioProcessor::setModControllerNumber (int slot, int controller) noexcept
{
    if (slot >= 0 && slot < mod::ccSlots)
        modCcNumber[static_cast<std::size_t> (slot)].store (std::clamp (controller, 0, 119), std::memory_order_relaxed);
}

mod::Curve OspAudioProcessor::modulationCurve (int index) const
{
    const juce::ScopedLock lock (customLock);
    return modCurves[static_cast<std::size_t> (std::clamp (index, 0, modCurveCount - 1))];
}

void OspAudioProcessor::setModulationCurve (int index, const mod::Curve& curve, bool undoable)
{
    index = std::clamp (index, 0, modCurveCount - 1);
    auto next = curve;
    next.normalise();
    const auto before = modulationCurve (index);
    if (before == next)
        return;
    if (undoable)
    {
        undoManager.perform (new ModCurveAction (*this, index, before, next));
        return;
    }
    {
        const juce::ScopedLock lock (customLock);
        modCurves[static_cast<std::size_t> (index)] = next;
    }
    publishModulationCurves();
}

void OspAudioProcessor::publishModulationCurves() noexcept
{
    ModCurves copy;
    {
        const juce::ScopedLock lock (customLock);
        copy = modCurves;
    }
    for (std::size_t c = 0; c < copy.size(); ++c)
    {
        const auto table = mod::compileCurve (copy[c]);
        for (std::size_t i = 0; i < table.size(); ++i)
            modCurveValues[c * mod::curvePoints + i].store (table[i], std::memory_order_relaxed);
    }
    modCurveGeneration.fetch_add (1, std::memory_order_release);
}

void OspAudioProcessor::refreshModLinks()
{
    for (int r = 0; r < mod::maxRoutes; ++r)
        modRouteTargets[static_cast<std::size_t> (r)].store (resolveModTarget (r), std::memory_order_relaxed);
}

void OspAudioProcessor::valueTreePropertyChanged (juce::ValueTree& tree, const juce::Identifier&)
{
    if (tree.hasType (modLinksTreeType()))
        refreshModLinks();
}

void OspAudioProcessor::valueTreeChildAdded (juce::ValueTree&, juce::ValueTree& child)
{
    if (child.hasType (modLinksTreeType()))
        refreshModLinks();
}

void OspAudioProcessor::valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree& child, int)
{
    if (child.hasType (modLinksTreeType()))
        refreshModLinks();
}

void OspAudioProcessor::valueTreeRedirected (juce::ValueTree&)
{
    refreshModLinks();   // a session or preset replaced the state
}

void OspAudioProcessor::migrateModulationDepths()
{
    // Before version 11 full depth moved a destination a fixed, smaller amount (LEVEL 24 dB,
    // TUNE 1 semitone, CUTOFF 4 octaves...); now it is the knob's whole range. Each depth is
    // scaled by old span / new span so the session sounds as it did. A depth route moves its
    // target's depth, so it scales with its target.
    const auto probe = modulationProbe();
    const double travel = std::abs (shaping::rangeOctaves (parameterValue (ids::characterMax)) - shaping::rangeOctaves (parameterValue (ids::characterMin)));
    auto factor = [travel] (mod::Dest d) {
        if (d == mod::Dest::none || d == mod::Dest::routeDepth)
            return 1.0;
        if (d == mod::Dest::cutoff)
            return travel > 1.0e-6 ? mod::legacySpan (d) / travel : 1.0;   // 4 octaves of the filter's travel
        return mod::legacySpan (d) / mod::destInfo (d).span;
    };
    for (int r = 0; r < mod::maxRoutes; ++r)
    {
        const auto& route = probe.routes[static_cast<std::size_t> (r)];
        if (route.source == mod::Source::none || route.dest == mod::Dest::none)
            continue;
        const double k = route.dest == mod::Dest::routeDepth
                             ? (route.target >= 0 ? factor (probe.routes[static_cast<std::size_t> (route.target)].dest) : 1.0)
                             : factor (route.dest);
        if (k != 1.0)
            setParameterValue (modRouteId (r, "depth"), static_cast<float> (parameterValue (modRouteId (r, "depth")) * k));
    }
}

ShaperPattern OspAudioProcessor::shaperCustomPattern() const
{
    const juce::ScopedLock lock (customLock);
    return customPattern;
}

void OspAudioProcessor::setShaperCustomPattern (const ShaperPattern& pattern, bool undoable)
{
    const auto before = shaperCustomPattern();
    if (before == pattern)
        return;
    if (undoable)
    {
        undoManager.perform (new CustomPatternAction (*this, before, pattern));
        return;
    }
    {
        const juce::ScopedLock lock (customLock);
        customPattern = pattern;
    }
    publishCustomPattern();
}

void OspAudioProcessor::publishCustomPattern() noexcept
{
    ShaperPattern copy;
    {
        const juce::ScopedLock lock (customLock);
        copy = customPattern;
    }
    for (std::size_t i = 0; i < copy.size(); ++i)
    {
        customStepValues[3 * i].store (std::clamp (copy[i].start, 0.0f, 1.0f), std::memory_order_relaxed);
        customStepValues[3 * i + 1].store (std::clamp (copy[i].end, 0.0f, 1.0f), std::memory_order_relaxed);
        customStepValues[3 * i + 2].store (static_cast<float> (copy[i].shape), std::memory_order_relaxed);
    }
    customGeneration.fetch_add (1, std::memory_order_release);
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

void OspAudioProcessor::swapKeyDnaIfPending() noexcept
{
    // A newly computed profile (message thread) replaces the layer's for notes started from
    // now. Sounding notes copied their key's values at note-on, so nothing reads an older
    // profile after this block: it may be freed as soon as this one is in use.
    for (int layer = 0; layer < numLayers; ++layer)
    {
        auto& exchange = keyDnaExchange[static_cast<std::size_t> (layer)];
        auto& playing = keyDnaPlaying[static_cast<std::size_t> (layer)];
        if (const auto* next = exchange.takePending())
        {
            playing = next;
            engine.setLayerKeyDnaProfile (layer, &next->table);
        }
        if (playing != nullptr)
        {
            exchange.publishOldestInUse (playing->generation);
            keyDnaInUse[static_cast<std::size_t> (layer)].store (playing->generation, std::memory_order_relaxed);
        }
    }
}

void OspAudioProcessor::setKeyDnaSettings (int layer, const keydna::Settings& settings)
{
    const auto l = resolve (layer);
    auto shape = settings;
    shape.amount = 1.0;   // AMOUNT is the host parameter, live
    auto profile = std::make_shared<keydna::Profile>();
    profile->table.compute (shape);   // all 128 keys, here (never on the audio thread)
    const std::lock_guard<std::mutex> lock (keyDnaMutex);
    keyDnaLayers[l] = shape;
    profile->generation = ++keyDnaGeneration;
    keyDnaExchange[l].publish (std::move (profile));
}

void OspAudioProcessor::beginKeyDnaGesture (std::size_t layer)
{
    auto& start = keyDnaGestureStart[layer];
    if (start.has_value())
        return;
    const std::lock_guard<std::mutex> lock (keyDnaMutex);
    start = KeyDnaState { keyDnaLayers[layer], keyDnaCurves[layer] };
}

void OspAudioProcessor::setKeyDnaSettings (int layer, const keydna::Settings& settings, const juce::String& name, bool undoable)
{
    // A drag publishes as it moves and becomes one undo step when it ends (undoable).
    const auto l = resolve (layer);
    beginKeyDnaGesture (l);
    if (! undoable)
    {
        setKeyDnaSettings (static_cast<int> (l), settings);
        return;
    }
    auto before = std::move (*keyDnaGestureStart[l]);
    keyDnaGestureStart[l].reset();
    std::array<std::optional<mod::Curve>, keydna::paramCount> after;
    {
        const std::lock_guard<std::mutex> lock (keyDnaMutex);
        after = keyDnaCurves[l];
    }
    undoManager.beginNewTransaction (name);
    undoManager.perform (new KeyDnaChangeAction (*this, static_cast<int> (l), std::move (before.settings), before.curves, settings, after));
}

mod::Curve OspAudioProcessor::keyDnaCurve (int layer, keydna::Param param) const
{
    const auto l = resolve (layer);
    keydna::Settings settings;
    {
        const std::lock_guard<std::mutex> lock (keyDnaMutex);
        if (const auto& drawn = keyDnaCurves[l][static_cast<std::size_t> (param)])
            return *drawn;
        settings = keyDnaLayers[l];
    }
    const auto& table = settings[param].curve;
    if (table == keydna::flatCurve())
        return InstrumentHost::keyDnaCurve (layer, param);
    // A curve saved without its points (KEY DNA before Phase 5): nine points along its table.
    mod::Curve curve;
    curve.count = 9;
    for (int i = 0; i < curve.count; ++i)
    {
        const auto x = static_cast<float> (i) / 8.0f;
        const auto at = std::min (mod::curvePoints - 1, static_cast<int> (std::lround (x * (mod::curvePoints - 1))));
        curve.points[static_cast<std::size_t> (i)] = { x, table[static_cast<std::size_t> (at)], 0.0f };
    }
    return curve;
}

void OspAudioProcessor::setKeyDnaCurve (int layer, keydna::Param param, const mod::Curve& curve, bool undoable)
{
    const auto l = resolve (layer);
    beginKeyDnaGesture (l);
    auto valid = curve;
    valid.normalise();
    auto settings = keyDnaSettings (static_cast<int> (l));
    settings[param].curve = keydna::curveFrom (valid);
    {
        const std::lock_guard<std::mutex> lock (keyDnaMutex);
        keyDnaCurves[l][static_cast<std::size_t> (param)] = valid;
    }
    setKeyDnaSettings (static_cast<int> (l), settings, "KEY DNA " + juce::String (keydna::paramName (param)) + " curve", undoable);
}

keydna::Settings OspAudioProcessor::keyDnaSettings (int layer) const
{
    const std::lock_guard<std::mutex> lock (keyDnaMutex);
    return keyDnaLayers[resolve (layer)];
}

std::size_t OspAudioProcessor::keyDnaProfilesAlive (int layer) const
{
    const std::lock_guard<std::mutex> lock (keyDnaMutex);
    return keyDnaExchange[resolve (layer)].ownedCount();
}

namespace
{
    // KEY DNA's numbers as round-trip-exact text (a reopened session computes the same profile).
    juce::String keyDnaNumber (double value)
    {
        char buffer[40];
        std::snprintf (buffer, sizeof (buffer), "%.17g", value);
        return buffer;
    }
    double keyDnaValue (const juce::var& v, double fallback)
    {
        const auto text = v.toString();
        return text.isEmpty() ? fallback : std::strtod (text.toRawUTF8(), nullptr);
    }
    juce::String encodeCurveTable (const mod::CurveTable& t)
    {
        // The compiled curve, exactly (what is heard): its 256 values, 9 significant digits.
        juce::StringArray values;
        for (float v : t)
        {
            char buffer[24];
            std::snprintf (buffer, sizeof (buffer), "%.9g", static_cast<double> (v));
            values.add (buffer);
        }
        return values.joinIntoString (" ");
    }
    std::optional<mod::CurveTable> decodeCurveTable (const juce::String& text)
    {
        const auto tokens = juce::StringArray::fromTokens (text, " ", "");
        if (tokens.size() != mod::curvePoints)
            return std::nullopt;
        mod::CurveTable t {};
        for (int i = 0; i < mod::curvePoints; ++i)
            t[static_cast<std::size_t> (i)] = std::clamp (std::strtof (tokens[i].toRawUTF8(), nullptr), 0.0f, 1.0f);
        return t;
    }
    // The editor's points ("x y tension" per point, ";" between): the curve as drawn. The
    // compiled table beside it stays what plays.
    juce::String encodeCurvePoints (const mod::Curve& c)
    {
        juce::StringArray points;
        for (int i = 0; i < c.count; ++i)
        {
            const auto& p = c.points[static_cast<std::size_t> (i)];
            char buffer[64];
            std::snprintf (buffer, sizeof (buffer), "%.9g %.9g %.9g", static_cast<double> (p.x), static_cast<double> (p.y), static_cast<double> (p.tension));
            points.add (buffer);
        }
        return points.joinIntoString (";");
    }
    // The keys drawn by hand ("key value" per key, ";" between; value -1..1, exact as float).
    juce::String encodeDrawnKeys (const std::array<float, 128>& drawn)
    {
        juce::StringArray keys;
        for (int k = 0; k < 128; ++k)
            if (const float v = drawn[static_cast<std::size_t> (k)]; v <= 1.0f)
            {
                char buffer[40];
                std::snprintf (buffer, sizeof (buffer), "%d %.9g", k, static_cast<double> (v));
                keys.add (buffer);
            }
        return keys.joinIntoString (";");
    }
    std::array<float, 128> decodeDrawnKeys (const juce::String& text)
    {
        auto drawn = keydna::undrawnKeys();
        for (const auto& item : juce::StringArray::fromTokens (text, ";", ""))
        {
            const auto values = juce::StringArray::fromTokens (item, " ", "");
            const int key = values.size() == 2 ? values[0].getIntValue() : -1;
            if (key >= 0 && key < 128)
                drawn[static_cast<std::size_t> (key)] = std::clamp (std::strtof (values[1].toRawUTF8(), nullptr), -1.0f, 1.0f);
        }
        return drawn;
    }
    std::optional<mod::Curve> decodeCurvePoints (const juce::String& text)
    {
        const auto points = juce::StringArray::fromTokens (text, ";", "");
        if (points.size() < 2 || points.size() > mod::maxCurvePoints)
            return std::nullopt;
        mod::Curve c;
        c.count = points.size();
        for (int i = 0; i < c.count; ++i)
        {
            const auto values = juce::StringArray::fromTokens (points[i], " ", "");
            if (values.size() != 3)
                return std::nullopt;
            c.points[static_cast<std::size_t> (i)] = { std::strtof (values[0].toRawUTF8(), nullptr), std::strtof (values[1].toRawUTF8(), nullptr),
                                                        std::strtof (values[2].toRawUTF8(), nullptr) };
        }
        c.normalise();
        return c;
    }
    constexpr int keyDnaSchemaVersion = 1;
}

juce::ValueTree OspAudioProcessor::keyDnaToTree (const std::array<keydna::Settings, 3>& layers)
{
    return keyDnaToTree (layers, KeyDnaCurves {});
}

juce::ValueTree OspAudioProcessor::keyDnaToTree (const std::array<keydna::Settings, 3>& layers, const KeyDnaCurves& curves)
{
    // KEY DNA schema 1: per layer its switch, individuality and seed, and per parameter
    // (position .. pan, by id) its curve depth, variation and curve (omitted when flat).
    juce::ValueTree tree ("KeyDna");
    tree.setProperty ("schemaVersion", keyDnaSchemaVersion, nullptr);
    const auto flat = keydna::flatCurve();
    for (int layer = 0; layer < numLayers; ++layer)
    {
        const auto& s = layers[static_cast<std::size_t> (layer)];
        juce::ValueTree l ("Layer");
        l.setProperty ("index", layer, nullptr);
        l.setProperty ("enabled", s.enabled, nullptr);
        l.setProperty ("individuality", keyDnaNumber (s.individuality), nullptr);
        l.setProperty ("seed", juce::String (s.seed), nullptr);
        for (int p = 0; p < keydna::paramCount; ++p)
        {
            const auto param = static_cast<keydna::Param> (p);
            const auto& ps = s[param];
            const auto& drawn = curves[static_cast<std::size_t> (layer)][static_cast<std::size_t> (p)];
            if (juce::exactlyEqual (ps.curveDepth, 0.0) && juce::exactlyEqual (ps.variation, 0.0) && ps.curve == flat && ! drawn.has_value() && ! ps.anyDrawn())
                continue;
            juce::ValueTree pt ("Param");
            pt.setProperty ("id", keydna::paramId (param), nullptr);
            pt.setProperty ("depth", keyDnaNumber (ps.curveDepth), nullptr);
            pt.setProperty ("variation", keyDnaNumber (ps.variation), nullptr);
            if (! (ps.curve == flat))
                pt.setProperty ("curve", encodeCurveTable (ps.curve), nullptr);
            if (drawn.has_value())
                pt.setProperty ("points", encodeCurvePoints (*drawn), nullptr);   // Phase 5: the editor's points
            if (ps.anyDrawn())
                pt.setProperty ("keys", encodeDrawnKeys (ps.drawn), nullptr);    // additive: keys drawn by hand
            l.appendChild (pt, nullptr);
        }
        tree.appendChild (l, nullptr);
    }
    return tree;
}

OspAudioProcessor::KeyDnaCurves OspAudioProcessor::keyDnaCurvesFromTree (const juce::ValueTree& tree)
{
    KeyDnaCurves curves {};
    if (! tree.isValid() || static_cast<int> (tree.getProperty ("schemaVersion", 0)) != keyDnaSchemaVersion)
        return curves;
    for (const auto& l : tree)
    {
        const int index = static_cast<int> (l.getProperty ("index", -1));
        if (! l.hasType ("Layer") || index < 0 || index >= numLayers)
            continue;
        for (const auto& pt : l)
            for (int p = 0; p < keydna::paramCount; ++p)
                if (pt.hasType ("Param") && pt.getProperty ("id").toString() == keydna::paramId (static_cast<keydna::Param> (p)) && pt.hasProperty ("points"))
                    curves[static_cast<std::size_t> (index)][static_cast<std::size_t> (p)] = decodeCurvePoints (pt.getProperty ("points").toString());
    }
    return curves;
}

std::array<keydna::Settings, 3> OspAudioProcessor::keyDnaFromTree (const juce::ValueTree& tree)
{
    // Absent (an older session) or from a newer schema this build does not know: no KEY DNA
    // (the original Granulator), never a guess.
    std::array<keydna::Settings, 3> layers {};
    if (! tree.isValid() || static_cast<int> (tree.getProperty ("schemaVersion", 0)) != keyDnaSchemaVersion)
        return layers;
    for (const auto& l : tree)
    {
        if (! l.hasType ("Layer"))
            continue;
        const int index = static_cast<int> (l.getProperty ("index", -1));
        if (index < 0 || index >= numLayers)
            continue;
        auto& s = layers[static_cast<std::size_t> (index)];
        s.enabled = static_cast<bool> (l.getProperty ("enabled", false));
        s.individuality = std::clamp (keyDnaValue (l.getProperty ("individuality"), 0.0), 0.0, 1.0);
        s.seed = std::strtoull (l.getProperty ("seed", "1").toString().toRawUTF8(), nullptr, 10);
        for (const auto& pt : l)
            for (int p = 0; p < keydna::paramCount; ++p)
            {
                const auto param = static_cast<keydna::Param> (p);
                if (! pt.hasType ("Param") || pt.getProperty ("id").toString() != keydna::paramId (param))
                    continue;
                auto& ps = s[param];
                ps.curveDepth = std::clamp (keyDnaValue (pt.getProperty ("depth"), 0.0), -1.0, 1.0);
                ps.variation = std::clamp (keyDnaValue (pt.getProperty ("variation"), 0.0), 0.0, 1.0);
                if (const auto curve = decodeCurveTable (pt.getProperty ("curve").toString()))
                    ps.curve = *curve;
                if (pt.hasProperty ("keys"))
                    ps.drawn = decodeDrawnKeys (pt.getProperty ("keys").toString());
            }
    }
    return layers;
}

void OspAudioProcessor::handleMidi (const juce::MidiMessage& m) noexcept
{
    const int channel = m.getChannel();
    // MPE (lower zone): channel 1 is the manager channel, 2..16 carry one note each.
    const bool memberChannel = engine.isMpe() && channel >= 2;
    // The CC 1-4 sources: the slots following this controller (Learn takes it first), and
    // PRESSURE (channel pressure; MPE members keep their own expression).
    if (m.isController() && m.getControllerNumber() < 120)
    {
        const int cc = m.getControllerNumber();
        if (const int learn = modCcLearn.exchange (-1, std::memory_order_acq_rel); learn >= 0 && learn < mod::ccSlots)
            modCcNumber[static_cast<std::size_t> (learn)].store (cc, std::memory_order_relaxed);
        for (int slot = 0; slot < mod::ccSlots; ++slot)
            if (modCcNumber[static_cast<std::size_t> (slot)].load (std::memory_order_relaxed) == cc)
                engine.setModController (slot, static_cast<float> (m.getControllerValue()) / 127.0f);
    }
    if ((m.isChannelPressure() || m.isAftertouch()) && ! memberChannel)
        engine.setModPressure (static_cast<float> (m.isChannelPressure() ? m.getChannelPressureValue() : m.getAfterTouchValue()) / 127.0f);
    if (m.isNoteOn())
    {
        engine.noteOn (m.getNoteNumber(), m.getVelocity(), channel);
        const int count = velocityCount.load (std::memory_order_relaxed);
        recentVelocity[static_cast<std::size_t> (count % velocityHistory)].store (m.getVelocity(), std::memory_order_relaxed);
        lastPlayedNote.store (m.getNoteNumber(), std::memory_order_relaxed);
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
    {
        modWheel = static_cast<float> (m.getControllerValue()) / 127.0f;
        engine.setModWheel (modWheel);   // the MOD WHEEL modulation source
        wheelShown.store (modWheel, std::memory_order_relaxed);
    }
    else if (m.isController() && m.getControllerNumber() >= 20 && m.getControllerNumber() <= 27)
        ccMacro[static_cast<std::size_t> (m.getControllerNumber() - 20)] = static_cast<float> (m.getControllerValue()) / 127.0f;   // 27: DRIVE
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

void OspAudioProcessor::routePreviewKeys (juce::MidiBuffer& midi) noexcept
{
    // Audio thread. While the Library auditions on the keyboard, notes go to the preview and
    // the instrument sees everything else; switching over stops the side that loses the keys.
    const bool on = previewKeysOn.load (std::memory_order_acquire);
    if (on == previewKeysActive && ! on)
        return;
    previewMidi.clear();
    if (on != previewKeysActive)
    {
        if (on)   // the instrument's (and the arpeggiator's) held notes end
            previewMidi.addEvent (juce::MidiMessage::allNotesOff (1), 0);
        else
            preview.allNotesOff();
        previewKeysActive = on;
    }
    if (! on)
        return;
    const auto mask = previewKeysMask.load (std::memory_order_relaxed);
    for (const auto m : midi)
    {
        const auto status = m.numBytes >= 3 ? (m.data[0] & 0xf0) : 0;
        if (status == 0x90 && m.data[2] > 0)
            preview.noteOn (m.data[1], static_cast<float> (m.data[2]) / 127.0f, mask);
        else if (status == 0x80 || (status == 0x90 && m.data[2] == 0))
            preview.noteOff (m.data[1]);
        else
            previewMidi.addEvent (m.data, m.numBytes, m.samplePosition);
    }
    midi.swapWith (previewMidi);
}

void OspAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int numSamples = buffer.getNumSamples();

    keyboardState.processNextMidiBuffer (midi, 0, numSamples, true);
    swapInstrumentIfPending();
    swapKeyDnaIfPending();
    routePreviewKeys (midi);

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
            {
                timing.bpm = *bpm;
                hostBpm.store (static_cast<float> (*bpm), std::memory_order_relaxed);
            }
            if (const auto signature = position->getTimeSignature())
            {
                timing.numerator = signature->numerator;
                timing.denominator = signature->denominator;
            }
            const bool transportRunning = position->getIsPlaying();
            if (transportRunning && ! hostWasPlaying)
            {
                engine.resetPerformance();
                arpeggiator.restartPattern();   // and the arpeggiator's pattern and RANDOM choices
            }
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
    const auto& notes = runArpeggiator (midi, numSamples, timing);

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

    for (const auto metadata : notes)
    {
        renderTo (std::clamp (metadata.samplePosition, 0, numSamples));
        handleMidi (metadata.getMessage());
    }
    renderTo (numSamples);
    // The Library's preview: after the instrument and its effects (dry), into the same output.
    if (outChannels > 0)
        preview.render (channels, outChannels, numSamples);

    for (int ch = outChannels; ch < buffer.getNumChannels(); ++ch)
        buffer.clear (ch, 0, numSamples);

    activeVoices.store (engine.musicalVoiceCount(), std::memory_order_relaxed);
}

const juce::MidiBuffer& OspAudioProcessor::runArpeggiator (const juce::MidiBuffer& midi, int numSamples, const HostTiming& timing) noexcept
{
    Arpeggiator::Settings settings;
    settings.enabled = arpEnabledParam->load() >= 0.5f;
    settings.pattern = static_cast<ArpPattern> (std::clamp (juce::roundToInt (arpPatternParam->load()), 0, arp::patternCount - 1));
    settings.rate = static_cast<ArpRate> (std::clamp (juce::roundToInt (arpRateParam->load()), 0, arp::rateCount - 1));
    settings.gate = arpGateParam->load() / 100.0;
    settings.octaves = std::clamp (juce::roundToInt (arpOctavesParam->load()), arp::minOctaves, arp::maxOctaves);
    settings.swing = arpSwingParam->load() / 100.0;
    settings.velocityScale = arpVelocityParam->load() / 100.0;
    applyArpModulation (settings);
    arpeggiator.setSeed (static_cast<std::uint64_t> (std::max (1.0f, seedParam->load())));
    arpeggiator.beginBlock (settings, timing, numSamples);
    // VEL and NOTE on the shared stages follow the player's keys, not the arpeggiator's.
    engine.setExternalInputNotes (settings.enabled);
    if (settings.enabled)
        for (const auto metadata : midi)
            if (const auto m = metadata.getMessage(); m.isNoteOn())
                engine.setInputNote (m.getNoteNumber(), m.getVelocity());

    auto feed = [this, numSamples] (const juce::MidiMessage& m, int samplePosition) {
        const int offset = std::clamp (samplePosition, 0, std::max (0, numSamples - 1));
        if (m.isNoteOn())
            arpeggiator.noteOn (offset, m.getNoteNumber(), m.getVelocity(), m.getChannel());
        else if (m.isNoteOff())
            arpeggiator.noteOff (offset, m.getNoteNumber(), m.getChannel());
        else if (m.isSustainPedalOn() || m.isSustainPedalOff())
            arpeggiator.sustainPedal (offset, m.isSustainPedalOn());
        else if (m.isAllNotesOff() || m.isAllSoundOff())
            arpeggiator.allNotesOff (offset);
        else
            return false;
        return true;
    };

    const bool owns = arpeggiator.isEnabled();
    if (! owns && arpeggiator.numEvents() == 0 && arpeggiator.soundingCount() == 0)
    {
        // Off: the held notes are only followed (to hand them over if it is switched on);
        // the MIDI the engine plays is exactly what arrived.
        for (const auto metadata : midi)
            feed (metadata.getMessage(), metadata.samplePosition);
        arpeggiator.endBlock();
        publishArpDisplay();
        return midi;
    }

    // On (or handing back): note events and the pedal go to the arpeggiator, everything
    // else (bends, wheels, pressure, CCs, All Notes Off) passes through at its sample.
    arpMidi.clear();
    for (const auto metadata : midi)
    {
        const auto m = metadata.getMessage();
        const bool noteEvent = feed (m, metadata.samplePosition);
        const bool consumed = owns && noteEvent && ! (m.isAllNotesOff() || m.isAllSoundOff());
        if (! consumed)
            arpMidi.addEvent (m, metadata.samplePosition);
    }
    arpeggiator.endBlock();
    for (int i = 0; i < arpeggiator.numEvents(); ++i)
    {
        const auto& e = arpeggiator.event (i);
        switch (e.kind)
        {
            case Arpeggiator::Event::Kind::noteOn: arpMidi.addEvent (juce::MidiMessage::noteOn (e.channel, e.note, e.velocity), e.offset); break;
            case Arpeggiator::Event::Kind::noteOff: arpMidi.addEvent (juce::MidiMessage::noteOff (e.channel, e.note), e.offset); break;
            case Arpeggiator::Event::Kind::sustainOn: arpMidi.addEvent (juce::MidiMessage::controllerEvent (e.channel, 64, 127), e.offset); break;
            case Arpeggiator::Event::Kind::sustainOff: arpMidi.addEvent (juce::MidiMessage::controllerEvent (e.channel, 64, 0), e.offset); break;
        }
    }
    // The engine plays arpMidi; the host's buffer is left as it came (the plugin sends no MIDI
    // out), and arpMidi keeps its own preallocated storage.
    publishArpDisplay();
    return arpMidi;
}

double OspAudioProcessor::steppedArpRatio (double multiplier) noexcept
{
    // The musical ratios ARP RATE's Stepped mode keeps to (the nearest, by octaves).
    static constexpr std::array<double, 11> ratios { 0.25, 1.0 / 3.0, 0.5, 2.0 / 3.0, 0.75, 1.0, 4.0 / 3.0, 1.5, 2.0, 3.0, 4.0 };
    const double octaves = std::log2 (std::max (multiplier, 1.0e-6));
    double best = 1.0, distance = 1.0e9;
    for (const double r : ratios)
        if (const double d = std::abs (std::log2 (r) - octaves); d < distance)
        {
            distance = d;
            best = r;
        }
    return best;
}

void OspAudioProcessor::applyArpModulation (Arpeggiator::Settings& settings) noexcept
{
    // The ARP destinations, from the engine's last control period (global sources; VEL and
    // NOTE follow the player's keys). Without a route every offset is 0: today's settings.
    using mod::Dest;
    const double rate = std::clamp (engine.arpModulation (Dest::arpRate), -2.0, 2.0);
    settings.rateStepped = arpRateStepped.load (std::memory_order_relaxed);
    settings.rateMultiplier = settings.rateStepped ? steppedArpRatio (std::exp2 (rate)) : std::exp2 (rate);
    settings.gate += engine.arpModulation (Dest::arpGate);
    settings.swing += engine.arpModulation (Dest::arpSwing);
    settings.velocityScale = std::clamp (settings.velocityScale + engine.arpModulation (Dest::arpVelocity), 0.0, 2.0);
    if (engine.arpModulated (Dest::arpOctaves))
    {
        // Quantised with hysteresis (a count changes once the value is well past half-way),
        // and the arpeggiator applies it at the start of its next cycle.
        const double wanted = settings.octaves + engine.arpModulation (Dest::arpOctaves);
        if (arpModOctaves < arp::minOctaves || std::abs (wanted - arpModOctaves) > 0.6)
            arpModOctaves = std::clamp (static_cast<int> (std::lround (wanted)), arp::minOctaves, arp::maxOctaves);
        settings.octaves = arpModOctaves;
        settings.octavesAtCycle = true;
    }
    else
    {
        arpModOctaves = 0;
    }
}

void OspAudioProcessor::publishArpDisplay() noexcept
{
    Arpeggiator::Display display;
    arpeggiator.display (display);
    for (int i = 0; i < Arpeggiator::displaySteps; ++i)
    {
        arpDisplayLow[static_cast<std::size_t> (i)].store (display.low[static_cast<std::size_t> (i)], std::memory_order_relaxed);
        arpDisplayHigh[static_cast<std::size_t> (i)].store (display.high[static_cast<std::size_t> (i)], std::memory_order_relaxed);
    }
    arpDisplayCurrent.store (display.current, std::memory_order_relaxed);
    arpDisplayActive.store (display.active, std::memory_order_release);
}

OspAudioProcessor::ArpView OspAudioProcessor::arpView() const noexcept
{
    ArpView view;
    view.active = arpDisplayActive.load (std::memory_order_acquire);
    view.current = arpDisplayCurrent.load (std::memory_order_relaxed);
    for (int i = 0; i < Arpeggiator::displaySteps; ++i)
    {
        view.low[static_cast<std::size_t> (i)] = arpDisplayLow[static_cast<std::size_t> (i)].load (std::memory_order_relaxed);
        view.high[static_cast<std::size_t> (i)] = arpDisplayHigh[static_cast<std::size_t> (i)].load (std::memory_order_relaxed);
    }
    return view;
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
    if (closing.load() || base == nullptr || base->model == nullptr || base->model->stage == InstrumentModel::Stage::complete)
        return;
    ++pendingLoads;
    loaderPool.addJob ([this, base = std::move (base), audio = std::move (audio), layer] {
        // A newer sample was dropped into this layer meanwhile: do not spend time on this one.
        if (! closing.load() && base->loadId == layers[static_cast<std::size_t> (layer)].latestLoadId.load())
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

std::shared_ptr<std::atomic<bool>> OspAudioProcessor::newPreparation (std::size_t index)
{
    // A new sound on the layer supersedes whatever was still being analysed or prepared for it.
    auto& slot = layers[index].cancelPreparation;
    if (slot != nullptr)
        *slot = true;
    slot = std::make_shared<std::atomic<bool>> (false);
    return slot;
}

void OspAudioProcessor::enqueueAtlasLoad (AtlasLoadRequest request, int layer)
{
    const auto index = resolve (layer);
    auto& target = layers[index];
    ++pendingLoads;
    target.state = LoadState::loading;
    const auto generation = nextGeneration.fetch_add (1);
    target.latestLoadId = generation;
    request.threads = atlasThreads;
    loaderPool.addJob ([this, request = std::move (request), generation, layer, cancel = newPreparation (index)] {
        auto result = loadInstrumentAtlas (request, store, generation);
        // Stage B is queued before Stage A is reported, so pendingLoads never reads 0 in between.
        if (result.instrument != nullptr && result.atlasPending != nullptr)
            enqueueAtlasEnhance (result.instrument, std::move (result.atlasPending), layer, cancel);
        pushResult (std::move (result), layer);
        --pendingLoads;
    });
}

void OspAudioProcessor::enqueueAtlasEnhance (std::shared_ptr<const LoadedInstrument> base, std::shared_ptr<const atlasset::PreparedAtlas> pending,
                                             int layer, std::shared_ptr<std::atomic<bool>> cancel)
{
    if (closing.load() || base == nullptr || pending == nullptr)
        return;
    ++pendingLoads;
    loaderPool.addJob ([this, base = std::move (base), pending = std::move (pending), layer, cancel = std::move (cancel)] {
        // A newer sound on this layer meanwhile (or cancelled, or closing): keep Stage A.
        if (! closing.load() && ! cancel->load() && base->loadId == layers[static_cast<std::size_t> (layer)].latestLoadId.load())
        {
            auto result = enhanceInstrumentAtlas (*base, *pending, nextGeneration.fetch_add (1), cancel.get(), atlasThreads);
            if (result.instrument != nullptr)
                pushResult (std::move (result), layer);
            else if (result.error != "cancelled")
            {
                // Stage A keeps playing; the musician hears why its sustain did not arrive.
                LoadResult note;
                note.instrument = base;
                note.warnings.push_back ("sustain and release could not be prepared: " + result.error);
                pushResult (std::move (note), layer);
            }
        }
        --pendingLoads;
    });
}

void OspAudioProcessor::loadAtlas (const atlas::NoteAtlas& atlas, int layer, const std::string& atlasFolder)
{
    AtlasLoadRequest request;
    request.atlas = atlas;
    request.atlasFolder = atlasFolder;
    request.capacitySeconds = atlasCapacityFor (static_cast<int> (resolve (layer)));
    userLoads.insert (nextGeneration.load());
    enqueueAtlasLoad (std::move (request), static_cast<int> (resolve (layer)));
}

void OspAudioProcessor::analyseRecording (const juce::File& file, int layer, const phrase::AnalysisSettings& settings)
{
    const auto index = resolve (layer);
    AnalyseRequest request;
    request.file = file;
    request.settings = settings;
    request.capacitySeconds = atlasCapacityFor (static_cast<int> (index));
    request.threads = atlasThreads;
    auto& target = layers[index];
    ++pendingLoads;
    target.state = LoadState::loading;
    const auto generation = nextGeneration.fetch_add (1);
    target.latestLoadId = generation;
    target.analysisInFlight = generation;
    target.editQueue.clear();   // edits of the atlas being replaced
    target.editInFlight = 0;
    userLoads.insert (generation);
    loaderPool.addJob ([this, request = std::move (request), generation, l = static_cast<int> (index), cancel = newPreparation (index)] {
        auto result = loadAnalysedRecording (request, store, analysisQueue, analysisCache, generation, cancel.get());
        result.requestId = generation;
        if (result.instrument != nullptr && result.atlasPending != nullptr)
            enqueueAtlasEnhance (result.instrument, std::move (result.atlasPending), l, cancel);
        else if (result.instrument != nullptr && result.instrument->set == nullptr)
            enqueueRefine (result.instrument, result.audio, l);   // kept as one sample: its usual stages
        pushResult (std::move (result), l);   // a cancelled one too: the layer stops showing ANALYSING
        --pendingLoads;
    });
}

void OspAudioProcessor::cancelAnalysis (int layer)
{
    if (auto& flag = layers[resolve (layer)].cancelPreparation; flag != nullptr)
        *flag = true;
}

std::shared_ptr<const AtlasStatus> OspAudioProcessor::atlasStatus (int layer) const
{
    const auto instrument = currentInstrument (layer);
    return instrument != nullptr ? instrument->atlasStatus : nullptr;
}

double OspAudioProcessor::atlasCapacityFor (int layer) const
{
    double others = 0.0;
    for (int l = 0; l < numLayers; ++l)
        if (l != layer)
            if (const auto status = atlasStatus (l); status != nullptr)
                others += status->regionSeconds;
    return std::clamp (atlasSessionSeconds - others, 0.0, atlasLayerSeconds);
}

std::vector<std::string> OspAudioProcessor::missingSources (int layer) const
{
    return layers[resolve (layer)].missingSources;
}

//==============================================================================
// Phase 5: MAP NOTES and the NOTE MAP

bool OspAudioProcessor::canMapNotes (int layer) const
{
    const auto instrument = currentInstrument (layer);
    return instrument != nullptr && instrument->set == nullptr && instrument->model != nullptr && ! instrument->storedPath.empty()
           && loadState (layer) != LoadState::loading;
}

int OspAudioProcessor::mapNotesPrompt (int layer) const
{
    const auto& l = layers[resolve (layer)];
    const auto instrument = currentInstrument (layer);
    if (l.promptLoad == 0 || instrument == nullptr || instrument->loadId != l.promptLoad || ! canMapNotes (layer))
        return 0;
    return l.promptNotes;
}

void OspAudioProcessor::dismissMapNotesPrompt (int layer)
{
    auto& l = layers[resolve (layer)];
    l.promptLoad = 0;
    l.promptNotes = 0;
}

void OspAudioProcessor::mapNotes (int layer, phrase::Detection detection)
{
    const auto index = static_cast<int> (resolve (layer));
    dismissMapNotesPrompt (index);
    const auto instrument = currentInstrument (index);
    if (instrument == nullptr)
        return;
    if (instrument->atlas != nullptr)
    {
        // Again, with another DETECTION: the musician's corrections survive, conflicts are told.
        auto report = std::make_shared<RedetectReport>();
        const juce::String label = juce::String ("Detect notes (") + juce::String (phrase::toString (detection)).toUpperCase() + ")";
        queueEdit (index, label, [detection, report] (atlas::NoteAtlas& a, const AudioData& audio) {
            if (a.sources.empty())
                return false;
            phrase::AnalysisSettings settings;
            settings.detection = detection;
            settings.a4Hz = a.analysis.a4Hz > 0.0 ? a.analysis.a4Hz : 440.0;
            auto fresh = phrase::analysePhrase (audio, a.sources.front(), settings);
            if (! fresh.ok)
                return false;
            auto merged = phrase::edit::mergeReanalysisReported (a, fresh.atlas);
            // The atlas keeps its own identity and sources (paths found since), the new analysis's record.
            merged.atlas.atlasId = a.atlasId;
            merged.atlas.sources = a.sources;
            merged.atlas.mapping = a.mapping;
            a = std::move (merged.atlas);
            report->keptCorrected = merged.keptCorrected;
            report->conflicts = static_cast<int> (merged.conflicts.size());
            for (const auto& c : merged.conflicts)
                report->conflictNoteIds.push_back (c.noteId);
            juce::String summary;
            if (merged.keptCorrected > 0)
                summary << merged.keptCorrected << (merged.keptCorrected == 1 ? " correction kept" : " corrections kept");
            if (! merged.conflicts.empty())
                summary << (summary.isEmpty() ? "" : "; ") << static_cast<int> (merged.conflicts.size())
                        << (merged.conflicts.size() == 1 ? " place where" : " places where") << " the new detection disagrees";
            report->summary = summary;
            return true;
        }, report);
        return;
    }
    if (! canMapNotes (index))
        return;
    juce::File file (juce::String::fromUTF8 (instrument->storedPath.c_str()));
    if (! file.existsAsFile())
        file = juce::File (juce::String::fromUTF8 (instrument->originalPath.c_str()));
    if (! file.existsAsFile())
    {
        showMessage ("MAP NOTES: the recording is not in the sample store any more");
        return;
    }
    phrase::AnalysisSettings settings;
    settings.detection = detection;
    analyseRecording (file, index, settings);
    userLoadNames[layers[static_cast<std::size_t> (index)].latestLoadId.load()] = "Map notes " + layerName (index);
}

void OspAudioProcessor::editAtlas (int layer, const juce::String& name, AtlasEdit edit)
{
    queueEdit (static_cast<int> (resolve (layer)), name, std::move (edit), nullptr);
}

void OspAudioProcessor::queueEdit (int layer, const juce::String& name, AtlasEdit edit, std::shared_ptr<RedetectReport> report)
{
    if (edit == nullptr || layerAtlas (layer) == nullptr)
        return;
    auto& target = layers[static_cast<std::size_t> (layer)];
    // One at a time: the next edits the published result of this one. Bounded: a burst of
    // edits faster than preparation keeps the last 32.
    if (target.editInFlight != 0 || target.analysisInFlight != 0)
    {
        target.editQueue.push_back ({ name, std::move (edit), std::move (report) });
        while (target.editQueue.size() > 32)
            target.editQueue.pop_front();
        return;
    }
    startEdit (layer, name, std::move (edit), std::move (report));
}

void OspAudioProcessor::startEdit (int layer, const juce::String& name, AtlasEdit edit, std::shared_ptr<RedetectReport> report)
{
    const auto index = static_cast<std::size_t> (layer);
    auto& target = layers[index];
    const auto instrument = currentInstrument (layer);
    if (instrument == nullptr || instrument->atlas == nullptr)
        return;
    const auto loadId = nextGeneration.fetch_add (1);
    target.latestLoadId = loadId;
    target.editInFlight = loadId;
    EditRecord record;
    record.layer = layer;
    record.name = name;
    record.beforeLoad = instrument->loadId;
    record.before = instrument->atlas;
    record.report = std::move (report);
    editLoads[loadId] = record;
    while (editLoads.size() > 64)
        editLoads.erase (editLoads.begin());

    AtlasLoadRequest request;
    request.atlas = *instrument->atlas;
    request.capacitySeconds = std::max (atlasCapacityFor (layer), instrument->atlasStatus != nullptr ? instrument->atlasStatus->regionSeconds : 0.0);
    request.threads = atlasThreads;
    request.preloaded = target.atlasAudio;
    ++pendingLoads;
    loaderPool.addJob ([this, request = std::move (request), edit = std::move (edit), loadId, layer, cancel = newPreparation (index)]() mutable {
        LoadResult result;
        result.requestId = loadId;
        // The recording's audio: the layer's decoded copy, else from the store (as a load does).
        const AudioData* audio = nullptr;
        if (! request.atlas.sources.empty())
            for (const auto& s : request.preloaded)
                if (s.assetId == request.atlas.sources.front().assetId)
                    audio = s.audio.get();
        std::shared_ptr<const AudioData> decoded;
        if (audio == nullptr && ! request.atlas.sources.empty())
        {
            const auto& src = request.atlas.sources.front();
            const auto hash = src.contentHash.rfind ("sha256:", 0) == 0 ? src.contentHash.substr (7) : src.contentHash;
            if (auto stored = store.find (hash))
                if (auto loaded = io::loadAudioFile (std::filesystem::path (stored->getFullPathName().toStdString())); loaded.ok)
                {
                    decoded = std::make_shared<const AudioData> (std::move (loaded.audio));
                    audio = decoded.get();
                    request.preloaded.push_back ({ src.assetId, decoded });
                }
        }
        bool changed = false;
        if (audio == nullptr)
            result.error = "the recording's audio is not in the sample store";
        else
        {
            try
            {
                changed = edit (request.atlas, *audio);
                atlas::repair (request.atlas);
            }
            catch (...)
            {
                result.error = "the edit could not be applied";
            }
        }
        if (result.error.empty() && ! changed)
            result.error = "unchanged";
        if (result.error.empty() && ! cancel->load())
        {
            result = loadInstrumentAtlas (request, store, loadId);
            result.requestId = loadId;
            if (result.instrument != nullptr && result.atlasPending != nullptr)
                enqueueAtlasEnhance (result.instrument, std::move (result.atlasPending), layer, cancel);
        }
        else if (result.error.empty())
            result.error = "cancelled";
        pushResult (std::move (result), layer);
        --pendingLoads;
    });
}

bool OspAudioProcessor::showAtlasState (int layer, std::uint64_t loadId, std::shared_ptr<const atlas::NoteAtlas> atlas)
{
    const auto index = static_cast<std::size_t> (resolve (layer));
    auto& target = layers[index];
    target.editQueue.clear();   // edits asked for the state being left behind
    if (const auto it = target.latestByLoad.find (loadId);
        it != target.latestByLoad.end() && it->second != nullptr && (it->second->atlasStatus == nullptr || ! it->second->atlasStatus->playableOnly))
    {
        if (target.cancelPreparation != nullptr)
            *target.cancelPreparation = true;   // an edit still preparing is superseded
        target.latestLoadId = loadId;
        target.editInFlight = 0;
        republish (it->second, static_cast<int> (index));
        return true;
    }
    if (atlas == nullptr)
        return false;
    // Not cached any more (or still unfinished): rebuilt from its atlas, without a new undo step.
    target.editInFlight = 0;
    restoreAtlas (static_cast<int> (index), std::move (atlas));
    return true;
}

void OspAudioProcessor::restoreAtlas (int layer, std::shared_ptr<const atlas::NoteAtlas> atlas)
{
    auto& target = layers[static_cast<std::size_t> (layer)];
    const auto previous = target.editInFlight;
    startEdit (layer, "Restore notes", [atlas] (atlas::NoteAtlas& a, const AudioData&) {
        a = *atlas;
        return true;
    });
    if (target.editInFlight != 0 && target.editInFlight != previous)
        editLoads[target.editInFlight].restore = true;
}

OspAudioProcessor::AtlasActivity OspAudioProcessor::atlasActivity (int layer) const noexcept
{
    const auto& target = layers[resolve (layer)];
    if (target.analysisInFlight != 0)
        return AtlasActivity::analysing;
    if (target.editInFlight != 0)
        return AtlasActivity::preparing;
    const auto instrument = currentInstrument (layer);
    if (instrument != nullptr && instrument->atlasStatus != nullptr && instrument->atlasStatus->playableOnly && pendingLoads.load() > 0)
        return AtlasActivity::enhancing;
    return AtlasActivity::none;
}

void OspAudioProcessor::auditionAtlasRegion (int layer, std::int64_t start, std::int64_t end)
{
    const auto index = resolve (layer);
    const auto& target = layers[index];
    const auto atlas = layerAtlas (static_cast<int> (index));
    if (atlas == nullptr || atlas->sources.empty() || auditionHub == nullptr)
        return;
    const AudioData* audio = nullptr;
    for (const auto& s : target.atlasAudio)
        if (s.assetId == atlas->sources.front().assetId)
            audio = s.audio.get();
    if (audio == nullptr || audio->isEmpty())
        return;
    start = std::clamp<std::int64_t> (start, 0, audio->numFrames());
    end = std::clamp<std::int64_t> (end, start, audio->numFrames());
    if (end - start < 16)
        return;
    auto slice = std::make_shared<AudioData> (AudioData::allocate (audio->numChannels(), end - start, audio->sampleRate));
    for (int c = 0; c < audio->numChannels(); ++c)
        std::copy (audio->channels[static_cast<std::size_t> (c)].begin() + start, audio->channels[static_cast<std::size_t> (c)].begin() + end,
                   slice->channels[static_cast<std::size_t> (c)].begin());
    // A short fade at each end: a cut in the middle of a note never clicks.
    const auto fade = std::min<std::int64_t> ((end - start) / 4, static_cast<std::int64_t> (0.004 * audio->sampleRate));
    for (auto& ch : slice->channels)
        for (std::int64_t i = 0; i < fade; ++i)
        {
            const auto g = static_cast<float> (i) / static_cast<float> (fade);
            ch[static_cast<std::size_t> (i)] *= g;
            ch[ch.size() - 1 - static_cast<std::size_t> (i)] *= g;
        }
    auditionHub->previewAudio (std::move (slice), "Note " + layerName (static_cast<int> (index)));
}

void OspAudioProcessor::playAsOneSample (int layer)
{
    const auto index = static_cast<int> (resolve (layer));
    const auto atlas = layerAtlas (index);
    if (atlas == nullptr || atlas->sources.empty())
        return;
    const auto& src = atlas->sources.front();
    const auto hash = src.contentHash.rfind ("sha256:", 0) == 0 ? src.contentHash.substr (7) : src.contentHash;
    auto stored = store.find (hash);
    if (! stored)
    {
        showMessage ("The recording is not in the sample store any more");
        return;
    }
    replaceLayer ({ *stored }, index);
    userLoadNames[layers[static_cast<std::size_t> (index)].latestLoadId.load()] = "Play as one sample " + layerName (index);
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
    // An atlas's places are its own (its notes, not inference): corrected in the atlas.
    if (instrument == nullptr || instrument->set == nullptr || instrument->atlas != nullptr)
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

void OspAudioProcessor::publishEmpty (std::size_t index)
{
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
    target.missingSources.clear();
    target.state = LoadState::empty;
}

void OspAudioProcessor::clearLayer (int layer)
{
    const auto index = resolve (layer);
    publishEmpty (index);
    setRootOverride (std::nullopt, static_cast<int> (index));
    // An empty slot is neither muted nor soloed (a solo left on it would silence the others).
    for (const auto& name : layerStateNames())
        if (auto* p = parameters.getParameter (layerParameterId (static_cast<int> (index), name)))
            p->setValueNotifyingHost (p->getDefaultValue());
}

//==============================================================================
// Adaptive layers: which slots hold a sound, adding and removing layers

bool OspAudioProcessor::isLayerOccupied (int layer) const
{
    const auto index = static_cast<std::size_t> (std::clamp (layer, 0, numLayers - 1));
    return layers[index].state.load() == LoadState::loading || currentInstrument (static_cast<int> (index)) != nullptr;
}

int OspAudioProcessor::firstFreeLayer() const
{
    for (int layer = 0; layer < numLayers; ++layer)
        if (! isLayerOccupied (layer))
            return layer;
    return -1;
}

int OspAudioProcessor::addLayers (const juce::Array<juce::File>& files, int firstLayer)
{
    int loaded = 0;
    const bool pairBefore = occupiedLayerCount() >= 2;   // an A/B mix the musician has already set
    for (const auto& file : files)
    {
        const int layer = firstLayer >= 0 && loaded == 0 && ! isLayerOccupied (firstLayer) ? firstLayer : firstFreeLayer();
        if (layer < 0)
            break;
        // A kept slot (clear all samples, a starting state) keeps its settings for its new sound.
        const bool kept = layer < keptSlotCount.load();
        if (! kept)
            resetLayerControls (layer);
        loadFile (file, layer);
        if (! kept)
            makeLayerAudible (layer, pairBefore);
        ++loaded;
    }
    if (loaded < files.size())
        showMessage (juce::String (files.size()) + " sounds dropped: ANDOR/OSP plays up to three, so " + juce::String (files.size() - loaded)
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
    const bool kept = layer < keptSlotCount.load();
    const bool pairBefore = occupiedLayerCount() >= 2;
    if (! kept)
        resetLayerControls (layer);
    loadFiles (files, layer);
    if (! kept)
        makeLayerAudible (layer, pairBefore);
    return layer;
}

void OspAudioProcessor::resetLayerControls (int layer)
{
    for (const auto& name : layerStateNames())   // a new sound is heard: not muted, not soloed
        if (auto* p = parameters.getParameter (layerParameterId (layer, name)))
            p->setValueNotifyingHost (p->getDefaultValue());
    for (const auto& name : granularNames())
        if (auto* p = parameters.getParameter (layerParameterId (layer, name)))
            p->setValueNotifyingHost (p->getDefaultValue());
    for (const auto& name : layerControlNames())
        if (auto* p = parameters.getParameter (layerParameterId (layer, name)))
            p->setValueNotifyingHost (p->getDefaultValue());
    if (layer > 0)   // a free slot's Reimagined returns to the instrument's (A's)
        setParameterValue (reimaginedParameterId (layer), parameterValue (reimaginedParameterId (0)));
    for (const auto& name : reimaginedModeNames())   // and its mode to KALEIDOSCOPE
        if (auto* p = parameters.getParameter (reimaginedModeParameterId (layer, name)))
            p->setValueNotifyingHost (p->getDefaultValue());
    setRootOverride (std::nullopt, layer);
}

OspAudioProcessor::LayerSnapshot OspAudioProcessor::captureLayer (int layer) const
{
    LayerSnapshot snapshot;
    snapshot.instrument = currentInstrument (layer);
    snapshot.rootOverride = rootOverride (layer);
    for (const auto* names : { &granularNames(), &layerControlNames(), &layerStateNames() })
        for (const auto& name : *names)
            snapshot.values.set (name, parameterValue (layerParameterId (layer, name)));
    snapshot.values.set ("reimagined", parameterValue (reimaginedParameterId (layer)));
    for (const auto& name : reimaginedModeNames())
        snapshot.values.set ("reimagined." + name, parameterValue (reimaginedModeParameterId (layer, name)));
    const auto& slot = layers[static_cast<std::size_t> (layer)];
    snapshot.latestByLoad = slot.latestByLoad;
    snapshot.lastPublishedLoad = slot.lastPublishedLoad;
    return snapshot;
}

void OspAudioProcessor::applyLayer (int layer, const LayerSnapshot& snapshot)
{
    auto& slot = layers[static_cast<std::size_t> (layer)];
    for (const auto* names : { &granularNames(), &layerControlNames(), &layerStateNames() })
        for (const auto& name : *names)
            setParameterValue (layerParameterId (layer, name), snapshot.values.getWithDefault (name, parameterValue (layerParameterId (layer, name))));
    setParameterValue (reimaginedParameterId (layer), snapshot.values.getWithDefault ("reimagined", parameterValue (reimaginedParameterId (layer))));
    for (const auto& name : reimaginedModeNames())
        setParameterValue (reimaginedModeParameterId (layer, name),
                           snapshot.values.getWithDefault ("reimagined." + name, parameterValue (reimaginedModeParameterId (layer, name))));
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
    const bool keptEmpty = isKeptEmptySlot (layer);
    if (layer < 0 || layer >= numLayers || pendingLoads.load() > 0 || ! (isLayerOccupied (layer) || keptEmpty))
        return false;
    const int slots = slotCount();
    // Three -> two: the two that stay keep their balance (their shares, renormalised, as
    // the equal-power blend), never a reset to the middle.
    std::optional<float> projectedBlend;
    if (slots == 3)
    {
        const auto share = InstrumentEngine::triangleShares (parameterValue (ids::mixX), parameterValue (ids::mixY));
        std::array<double, 2> remaining {};
        int n = 0;
        for (int l = 0; l < 3; ++l)
            if (l != layer)
                remaining[static_cast<std::size_t> (n++)] = share[static_cast<std::size_t> (l)];
        const double total = remaining[0] + remaining[1];
        projectedBlend = total > 1.0e-9 ? static_cast<float> (std::asin (std::sqrt (remaining[1] / total)) * 2.0 / juce::MathConstants<double>::pi) : 0.5f;
    }
    std::array<LayerSnapshot, numLayers> before;
    for (int l = 0; l < numLayers; ++l)
        before[static_cast<std::size_t> (l)] = captureLayer (l);
    if (! keptEmpty)
    {
        removedLayer = std::make_unique<RemovedLayer>();
        removedLayer->index = layer;
        removedLayer->snapshot = before[static_cast<std::size_t> (layer)];
    }
    // Compact: the layers above move down, so the instrument is always A, A+B or A+B+C.
    // Kept slots move with their settings even without a sound.
    int last = layer;
    for (int l = layer + 1; l < numLayers; ++l)
        if (before[static_cast<std::size_t> (l)].instrument != nullptr || l < slots)
        {
            applyLayer (last, before[static_cast<std::size_t> (l)]);
            last = l;
        }
    clearLayer (last);
    resetLayerControls (last);
    if (layer < keptSlotCount.load())
        keptSlotCount = keptSlotCount.load() - 1;
    if (projectedBlend)
        setParameterValue (ids::blend, *projectedBlend);
    if (last != layer)
        undoManager.clearUndoHistory();   // load undo steps name slots that have moved
    if (editLayer() >= std::max (1, slotCount()))
        setEditLayer (std::max (0, slotCount() - 1));
    showMessage (keptEmpty ? "Removed slot " + layerName (layer)
                           : "Removed layer " + layerName (layer) + juce::String::fromUTF8 (" \xc2\xb7 Restore it from the menu"));
    return true;
}

int OspAudioProcessor::slotCount() const
{
    int highest = 0;
    for (int l = 0; l < numLayers; ++l)
        if (isLayerOccupied (l))
            highest = l + 1;
    return std::max (highest, keptSlotCount.load());
}

void OspAudioProcessor::clearAllSamples()
{
    if (pendingLoads.load() > 0)
        return;
    const int slots = slotCount();
    if (slots == 0)
        return;
    keptSlotCount = slots;
    for (int l = 0; l < numLayers; ++l)
        if (isLayerOccupied (l))
            clearLayer (l);
    removedLayer.reset();
    undoManager.clearUndoHistory();
    showMessage ("Samples cleared: drop new sounds into " + juce::String (slots == 1 ? "A" : (slots == 2 ? "A and B" : "A, B and C"))
                 + juce::String::fromUTF8 (" \xe2\x80\x94 every setting is kept"));
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
    if (keptSlotCount.load() > 0)
        setKeptSlots (keptSlotCount.load() + 1);
    if (index < count)
        undoManager.clearUndoHistory();
    removedLayer.reset();
    setEditLayer (index);
    return true;
}

void OspAudioProcessor::convertLegacyReimaginedToPerLayer()
{
    // The amounts stay as they are (each layer already has its own; sessions from before
    // the layers gave every layer the one amount on load), so nothing on screen moves.
    perLayerReimagined = true;
}

void OspAudioProcessor::parameterGestureChanged (int parameterIndex, bool gestureIsStarting)
{
    // A musician's edit of a layer's REIMAGINED (knob, double-click reset, wheel, LINK
    // following it, a host's control surface) - never automation playback, which sends no
    // gestures. Only that makes a legacy patch per-layer.
    if (! gestureIsStarting || perLayerReimagined.load())
        return;
    for (auto* p : reimaginedParameters)
        if (p != nullptr && p->getParameterIndex() == parameterIndex)
            convertLegacyReimaginedToPerLayer();
}

//==============================================================================
// Header preset navigation

std::vector<OspAudioProcessor::PresetEntry> OspAudioProcessor::presetList() const
{
    std::vector<PresetEntry> list;
    for (int i = 0; i < static_cast<int> (std::size (startingStates)); ++i)
        list.push_back ({ startingStates[i].name, i, {}, true });
    for (const auto& file : findFiles (startingStateFolder(), startingStateExtension))
        list.push_back ({ file.getFileNameWithoutExtension(), -1, file, true });
    for (const auto& file : findFiles (presetFolder(), presetExtension))
        list.push_back ({ file.getFileNameWithoutExtension(), -1, file, false });
    return list;
}

juce::String OspAudioProcessor::presetDisplayName() const
{
    if (! presetIsProgram && lastPresetFile != juce::File())
        return lastPresetFile.getFileNameWithoutExtension();
    if (! presetIsProgram && presetNameOverride.isNotEmpty())
        return presetNameOverride;
    return startingStates[std::clamp (currentProgram, 0, static_cast<int> (std::size (startingStates)) - 1)].name;
}

void OspAudioProcessor::openPresetEntry (const PresetEntry& entry)
{
    if (entry.program >= 0)
    {
        rememberPreviousState ("Before " + entry.name);
        setCurrentProgram (entry.program);
        presetIsProgram = true;
    }
    else if (entry.startingState)
        loadStartingState (entry.file);
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
            || (! presetIsProgram && list[static_cast<std::size_t> (i)].file != juce::File()
                && (list[static_cast<std::size_t> (i)].file == lastPresetFile || list[static_cast<std::size_t> (i)].file == lastStartingStateFile)))
            current = i;
    const int count = static_cast<int> (list.size());
    const int next = current < 0 ? 0 : ((current + delta) % count + count) % count;
    openPresetEntry (list[static_cast<std::size_t> (next)]);
}

namespace
{
    juce::File favouritesFile() { return OspAudioProcessor::presetFolder().getChildFile ("Favourites.txt"); }
}

const juce::StringArray& OspAudioProcessor::favourites() const
{
    // Read once (the header asks often); written through on every change.
    if (! favouritesLoaded)
    {
        favouriteNames = juce::StringArray::fromLines (favouritesFile().loadFileAsString());
        favouriteNames.removeEmptyStrings();
        favouritesLoaded = true;
    }
    return favouriteNames;
}

bool OspAudioProcessor::isFavourite() const
{
    return favourites().contains (presetDisplayName());
}

void OspAudioProcessor::toggleFavourite()
{
    const auto name = presetDisplayName();
    setFavouriteName (name, ! isFavouriteName (name));
}

bool OspAudioProcessor::isFavouriteName (const juce::String& name) const
{
    return favourites().contains (name);
}

void OspAudioProcessor::setFavouriteName (const juce::String& name, bool favourite)
{
    // Favourites are plain names in a text file beside the presets (shareable, editable).
    auto lines = favourites();
    if (favourite == lines.contains (name))
        return;
    if (favourite)
        lines.add (name);
    else
        lines.removeString (name);
    favouriteNames = lines;
    favouritesFile().getParentDirectory().createDirectory();
    favouritesFile().replaceWithText (lines.joinIntoString ("\n") + "\n");
}

juce::StringArray OspAudioProcessor::factoryStartingStates()
{
    juce::StringArray names;
    for (const auto& state : startingStates)
        names.add (state.name);
    return names;
}

std::vector<std::string> OspAudioProcessor::soundsInUse() const
{
    std::vector<std::string> hashes;
    for (int layer = 0; layer < numLayers; ++layer)
        if (const auto instrument = currentInstrument (layer))
        {
            if (! instrument->contentHash.empty())
                hashes.push_back (instrument->contentHash);
            for (const auto& member : instrument->memberFiles)
                hashes.push_back (member.contentHash);
        }
    return hashes;
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
    // DYNAMICS' SOFT / LINEAR / HARD (kept for compatibility) sets the VEL curve when moved.
    if (const int dyn = juce::roundToInt (parameterValue (ids::dynamicsCurve)); dyn != shownDynamicsCurve)
    {
        shownDynamicsCurve = dyn;
        static constexpr std::array<mod::CurvePreset, 3> presets { mod::CurvePreset::soft, mod::CurvePreset::linear, mod::CurvePreset::hard };
        setModulationCurve (4, mod::presetCurve (presets[static_cast<std::size_t> (std::clamp (dyn, 0, 2))]), false);
    }
    std::deque<Finished> results;
    {
        const std::lock_guard<std::mutex> lock (resultsMutex);
        results.swap (finishedLoads);
    }
    preview.collectGarbage();
    if (auditionHub != nullptr)
        auditionHub->deliver();
    if (libraryHub != nullptr)
        libraryHub->deliver();

    std::array<bool, numLayers> published {};
    std::array<bool, numLayers> touched {};
    std::array<bool, numLayers> lastFailed {};
    std::array<bool, numLayers> dispatch {};
    std::array<std::shared_ptr<const atlas::NoteAtlas>, numLayers> rebuild {};
    for (auto& finished : results)
    {
        const auto index = static_cast<std::size_t> (std::clamp (finished.layer, 0, numLayers - 1));
        auto& layer = layers[index];
        auto& result = finished.result;
        // Phase 5: MAP NOTES analyses and NOTE MAP edits (a load id, or the failed request's).
        const auto resultLoad = result.instrument != nullptr ? result.instrument->loadId : result.requestId;
        if (resultLoad != 0 && resultLoad == layer.analysisInFlight)
        {
            layer.analysisInFlight = 0;
            dispatch[index] = true;
        }
        const auto editIt = resultLoad != 0 ? editLoads.find (resultLoad) : editLoads.end();
        const bool isEdit = editIt != editLoads.end();
        if (isEdit && layer.editInFlight == resultLoad)
        {
            layer.editInFlight = 0;   // Stage A is in (or it failed): the next edit may start
            dispatch[index] = true;
        }
        if (result.instrument == nullptr && (isEdit || result.error == "cancelled"))
        {
            // An edit that changed nothing, was superseded or failed: the instrument playing stays.
            if (layer.latestLoadId.load() == resultLoad)
                if (const auto current = currentInstrument (static_cast<int> (index)); current != nullptr)
                {
                    layer.latestLoadId = current->loadId;
                    // Its Stage B was stopped for the edit: prepared again.
                    if (isEdit && current->atlas != nullptr && current->atlasStatus != nullptr && current->atlasStatus->playableOnly)
                        rebuild[index] = current->atlas;
                }
            if (isEdit)
                editLoads.erase (editIt);
            if (result.error != "unchanged" && result.error != "cancelled")
            {
                const std::lock_guard<std::mutex> lock (messageMutex);
                lastMessage = "Edit not applied: " + juce::String (result.error);
            }
            continue;
        }
        if (! result.atlasAudio.empty())
        {
            // The decoded recording(s), for edits and audition (two kept: undo across a reload).
            for (auto& audio : result.atlasAudio)
            {
                auto& cache = layer.atlasAudio;
                cache.erase (std::remove_if (cache.begin(), cache.end(), [&] (const auto& a) { return a.assetId == audio.assetId; }), cache.end());
                cache.insert (cache.begin(), std::move (audio));
                if (cache.size() > 2)
                    cache.resize (2);
            }
        }
        touched[index] = true;
        lastFailed[index] = result.instrument == nullptr;
        if (result.instrument == nullptr || result.instrument->atlas != nullptr)
            layer.missingSources = result.missingSources;   // an atlas's (a failed load names what it lacked)
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
                if (loadId != layer.lastPublishedLoad && isEdit)
                {
                    // A NOTE MAP edit: the same sound (not a Library entry), one undo step.
                    const auto& record = editIt->second;
                    if (! record.restore)
                    {
                        undoManager.beginNewTransaction (record.name);
                        undoManager.perform (new AtlasEditAction (*this, static_cast<int> (index), record.beforeLoad, loadId, record.before, result.instrument->atlas));
                    }
                    if (record.report != nullptr)
                        layer.redetect = *record.report;
                    layer.lastPublishedLoad = loadId;
                }
                else if (loadId != layer.lastPublishedLoad)
                {
                    // A new sound on this layer: into the Library (its history when the user chose it).
                    libraryHub->soundLoaded (*result.instrument, static_cast<int> (index), userLoads.count (loadId) > 0);
                    // A new sample (not a later stage of the same one): undoable if the user asked for it.
                    if (userLoads.count (loadId) > 0 && layer.lastPublishedLoad != 0)
                    {
                        const auto named = userLoadNames.find (loadId);
                        undoManager.beginNewTransaction (named != userLoadNames.end() ? named->second : "Load sample " + layerName (static_cast<int> (index)));
                        undoManager.perform (new InstrumentChangeAction (*this, static_cast<int> (index), layer.lastPublishedLoad, loadId));
                    }
                    if (result.instrument->atlas == nullptr || ! userLoadNames.count (loadId))
                        layer.redetect = {};
                    // A dropped phrase asks at once whether to map its notes (Phase 5 review). A
                    // recalled session, an atlas, a set or "Play as one sample" asks nothing.
                    layer.promptLoad = 0;
                    layer.promptNotes = 0;
                    if (userLoads.count (loadId) > 0 && ! userLoadNames.count (loadId) && result.instrument->atlas == nullptr
                        && result.instrument->set == nullptr && result.instrument->durationSeconds >= 1.0)
                    {
                        const auto& pitch = result.instrument->analysis.pitch;
                        const int notes = library::heldNotes (pitch.trackHz, pitch.trackConfidence);
                        if (notes >= 2)
                        {
                            layer.promptLoad = loadId;
                            layer.promptNotes = notes;
                        }
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

        // A later stage of the same load (sustain prepared, registers added) says nothing new:
        // it keeps that load's message ("kept as one sample", doubts, a partial build).
        const auto loadId = result.instrument != nullptr ? result.instrument->loadId : std::uint64_t { 0 };
        if (message.isEmpty() && loadId != 0 && loadId == layer.messageLoad)
            continue;
        layer.messageLoad = loadId;
        const std::lock_guard<std::mutex> lock (messageMutex);
        lastMessage = message;
    }

    for (int index = 0; index < numLayers; ++index)
    {
        auto& layer = layers[static_cast<std::size_t> (index)];
        // Phase 5: the next NOTE MAP edit once the one before is playable (it edits that result).
        if (rebuild[static_cast<std::size_t> (index)] != nullptr && layer.editInFlight == 0 && layer.analysisInFlight == 0)
            restoreAtlas (index, rebuild[static_cast<std::size_t> (index)]);
        if (dispatch[static_cast<std::size_t> (index)] && layer.editInFlight == 0 && layer.analysisInFlight == 0 && ! layer.editQueue.empty())
        {
            auto next = std::move (layer.editQueue.front());
            layer.editQueue.pop_front();
            startEdit (index, next.name, std::move (next.edit), std::move (next.report));
        }
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

    {
        const std::lock_guard<std::mutex> lock (keyDnaMutex);
        for (auto& exchange : keyDnaExchange)
            exchange.collectGarbage();
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
    stateTree.setProperty ("arpExpanded", arpEditorOpen.load(), nullptr);   // UI only, never audio
    stateTree.setProperty ("modTab", modTab.load(), nullptr);              // UI only, never audio
    stateTree.setProperty ("modTabs", 6, nullptr);                         // ... in the six-tab order
    stateTree.setProperty ("program", currentProgram, nullptr);

    stateTree.setProperty ("editLayer", editLayer(), nullptr);
    stateTree.setProperty ("keptSlots", keptSlotCount.load(), nullptr);
    stateTree.setProperty ("reimaginedRouting", perLayerReimagined.load() ? "perLayer" : "legacyGlobal", nullptr);
    stateTree.setProperty ("shaperCustom", encodeShaperPattern (shaperCustomPattern()), nullptr);
    {
        ModCurves curves;
        {
            const juce::ScopedLock lock (customLock);
            curves = modCurves;
        }
        stateTree.setProperty ("modCurves", encodeModulationCurves (curves), nullptr);
    }
    {
        juce::StringArray numbers;
        for (int slot = 0; slot < mod::ccSlots; ++slot)
            numbers.add (juce::String (modControllerNumber (slot)));
        stateTree.setProperty ("modControllers", numbers.joinIntoString (","), nullptr);
    }
    stateTree.setProperty ("velToAmp", velocityAppliesToAmp(), nullptr);
    stateTree.setProperty ("arpRateStepped", arpRateIsStepped(), nullptr);
    stateTree.setProperty ("grainBudgetPolicy", boundedGrainBudget.load() ? "bounded" : "shared", nullptr);
    {
        std::array<keydna::Settings, 3> dna;
        KeyDnaCurves curves;
        {
            const std::lock_guard<std::mutex> lock (keyDnaMutex);
            dna = keyDnaLayers;
            curves = keyDnaCurves;
        }
        stateTree.removeChild (stateTree.getChildWithName ("KeyDna"), nullptr);
        stateTree.appendChild (keyDnaToTree (dna, curves), nullptr);
    }
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
    if (instrument != nullptr && instrument->atlas != nullptr)
    {
        // A Note Atlas: its JSON (own schemaVersion). The recordings stay in the sample store,
        // named by content hash; the session holds no audio.
        juce::ValueTree atlasTree ("Atlas");
        atlasTree.setProperty ("json", juce::String (io::noteAtlasToString (*instrument->atlas)), nullptr);
        for (const auto& source : instrument->atlasSources)
        {
            juce::ValueTree m ("Source");
            m.setProperty ("contentHash", juce::String (source.contentHash), nullptr);
            m.setProperty ("filename", juce::String (source.filename), nullptr);
            atlasTree.appendChild (m, nullptr);
        }
        tree.appendChild (atlasTree, nullptr);
    }
    else if (instrument != nullptr && instrument->set != nullptr)
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

void OspAudioProcessor::applyStateXml (const juce::XmlElement& xml, bool settingsOnly)
{
    auto stateTree = juce::ValueTree::fromXml (xml);
    if (! settingsOnly)
    {
        uiScaleFactor = std::clamp (static_cast<float> (stateTree.getProperty ("uiScale", 1.0f)), 0.8f, 2.0f);
        advancedPanelOpen = static_cast<bool> (stateTree.getProperty ("advancedOpen", false));
        arpEditorOpen = static_cast<bool> (stateTree.getProperty ("arpExpanded", false));
        {
            // Before the six tabs (AMP, ENV 1, ENV 2, LFO 1, LFO 2): ENV 2 opens ENV, the LFOs move by one.
            static constexpr std::array<int, 5> fromFive { 0, 1, 1, 2, 3 };
            const int saved = static_cast<int> (stateTree.getProperty ("modTab", 0));
            modTab = static_cast<int> (stateTree.getProperty ("modTabs", 5)) == 6 ? std::clamp (saved, 0, 5)
                                                                                   : fromFive[static_cast<std::size_t> (std::clamp (saved, 0, 4))];
            stateTree.removeProperty ("modTabs", nullptr);
        }
        currentProgram = static_cast<int> (stateTree.getProperty ("program", 0));
        setEditLayer (static_cast<int> (stateTree.getProperty ("editLayer", 0)));
    }
    setKeptSlots (static_cast<int> (stateTree.getProperty ("keptSlots", 0)));
    const std::array<juce::ValueTree, numLayers> layerTrees { stateTree.getChildWithName (ids::instrument),
                                                              stateTree.getChildWithName (ids::instrumentB),
                                                              stateTree.getChildWithName (ids::instrumentC) };
    for (const auto& tree : layerTrees)
        stateTree.removeChild (tree, nullptr);
    const auto keyDnaTree = stateTree.getChildWithName ("KeyDna");
    stateTree.removeChild (keyDnaTree, nullptr);
    const int savedVersion = static_cast<int> (stateTree.getProperty ("stateVersion", 1));
    // Made before per-layer Reimagined (no routing saved): the legacy shared stage, exactly.
    perLayerReimagined = savedVersion >= 8 && stateTree.getProperty ("reimaginedRouting", "legacyGlobal").toString() == "perLayer";
    stateTree.removeProperty ("reimaginedRouting", nullptr);
    // SHAPER CUSTOM: the session's own steps, or (older sessions) CUSTOM's starting point.
    setShaperCustomPattern (decodeShaperPattern (stateTree["shaperCustom"].toString()).value_or (RhythmicShaper::patternSteps (ShaperParams().pattern)), false);
    stateTree.removeProperty ("shaperCustom", nullptr);
    // MODULATION's curves: the session's own, or (older sessions) the defaults. Before VEL
    // (c1 or none): the VEL curve is the session's DYNAMICS curve, applied to AMP, so it
    // responds exactly as it did.
    {
        const auto text = stateTree["modCurves"].toString();
        auto curves = decodeModulationCurves (text);
        if (! curves)
            curves = defaultModulationCurves();
        // The session's DYNAMICS curve (its parameters are applied below; one it lacks keeps
        // the current value).
        float savedDyn = parameterValue (ids::dynamicsCurve);
        for (const auto& child : stateTree)
            if (child.getProperty ("id").toString() == ids::dynamicsCurve && child.hasProperty ("value"))
                savedDyn = static_cast<float> (child.getProperty ("value"));
        const int dyn = std::clamp (juce::roundToInt (savedDyn), 0, 2);
        shownDynamicsCurve = dyn;
        if (! text.startsWith ("c2:") && ! text.startsWith ("c3:"))
        {
            static constexpr std::array<mod::CurvePreset, 3> presets { mod::CurvePreset::soft, mod::CurvePreset::linear, mod::CurvePreset::hard };
            (*curves)[4] = mod::presetCurve (presets[static_cast<std::size_t> (dyn)]);
        }
        setVelocityAppliesToAmp (! stateTree.hasProperty ("velToAmp") || static_cast<bool> (stateTree["velToAmp"]));
        stateTree.removeProperty ("velToAmp", nullptr);
        // The grain budget's policy: the session's; before v14 Phase 2's shared budget (exactly
        // as they sounded), from v14 on bounded.
        if (stateTree.hasProperty ("grainBudgetPolicy"))
            boundedGrainBudget = stateTree["grainBudgetPolicy"].toString() != "shared";
        else
            boundedGrainBudget = savedVersion >= 14;
        stateTree.removeProperty ("grainBudgetPolicy", nullptr);
        // ARP RATE's modulation: smooth unless the session chose Stepped.
        setArpRateStepped (static_cast<bool> (stateTree.getProperty ("arpRateStepped", false)));
        stateTree.removeProperty ("arpRateStepped", nullptr);
        {
            const juce::ScopedLock lock (customLock);
            modCurves = *curves;
        }
        publishModulationCurves();
        stateTree.removeProperty ("modCurves", nullptr);
    }
    // The CC 1-4 sources' controllers (older sessions: the defaults).
    {
        const auto numbers = juce::StringArray::fromTokens (stateTree["modControllers"].toString(), ",", "");
        for (int slot = 0; slot < mod::ccSlots; ++slot)
            setModControllerNumber (slot, slot < numbers.size() && numbers[slot].containsOnly ("0123456789") && numbers[slot].isNotEmpty()
                                              ? numbers[slot].getIntValue() : defaultModController (slot));
        stateTree.removeProperty ("modControllers", nullptr);
    }
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
    juce::StringArray savedIds;
    for (const auto& child : stateTree)
        if (child.hasProperty ("id"))
            savedIds.add (child["id"].toString());
    parameters.replaceState (stateTree);
    // A parameter the session does not contain (it is older than the parameter) starts from
    // its default: APVTS would otherwise keep whatever this instance had (e.g. Mono).
    for (auto* parameter : getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (parameter))
            if (! savedIds.contains (ranged->getParameterID()))
                ranged->setValueNotifyingHost (ranged->getDefaultValue());
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

    if (savedVersion < 7)
    {
        // Before per-layer Reimagined there was one amount: every layer keeps it.
        const float amount = parameterValue (reimaginedParameterId (0));
        for (int layer = 1; layer < numLayers; ++layer)
            setParameterValue (reimaginedParameterId (layer), amount);
    }

    if (savedVersion < 9)
    {
        // Before the REIMAGINED modes every layer played KALEIDOSCOPE (the one algorithm) at
        // its neutral FOCUS / SPREAD, and every other mode starts from its defaults.
        for (int layer = 0; layer < numLayers; ++layer)
            for (const auto& name : reimaginedModeNames())
                if (auto* p = parameters.getParameter (reimaginedModeParameterId (layer, name)))
                    p->setValueNotifyingHost (p->getDefaultValue());
    }

    if (savedVersion < 10)
    {
        // Before SPACE v2 the rooms had no EQ: open it, so nothing is filtered that never
        // was (CHAMBER opens as HALL, which took its place). ECHO did not exist: off.
        setParameterValue (ids::spaceLowCut, 20.0f);
        setParameterValue (ids::spaceHighCut, 20000.0f);
        setParameterValue (ids::echo, 0.0f);
    }
    if (savedVersion < 11)
        migrateModulationDepths();
    if (savedVersion < 13)
    {
        // Before the grain budget every grain played: older sessions and presets keep that
        // (Unlimited), while new sessions start from the benchmarked budget.
        setParameterValue (ids::grainBudget, 0.0f);
    }
    // KEY DNA: the session's (absent before 13: none). Always published, so a session without
    // it also clears the previous one's.
    {
        const auto dna = keyDnaFromTree (keyDnaTree);
        {
            const std::lock_guard<std::mutex> lock (keyDnaMutex);
            keyDnaCurves = keyDnaCurvesFromTree (keyDnaTree);
        }
        keyDnaGestureStart = {};
        for (int layer = 0; layer < numLayers; ++layer)
            setKeyDnaSettings (layer, dna[static_cast<std::size_t> (layer)]);
    }

    if (settingsOnly)
        return;
    for (int layer = 0; layer < numLayers; ++layer)
    {
        const auto& tree = layerTrees[static_cast<std::size_t> (layer)];
        if (tree.isValid() && (tree.hasProperty ("contentHash") || tree.getChildWithName ("Set").isValid() || tree.getChildWithName ("Atlas").isValid()))
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

    if (const auto atlasTree = tree.getChildWithName ("Atlas"); atlasTree.isValid())
    {
        // The layer is emptied first: until every recording is found, it must not look (or
        // sound) restored with the previous sound. A failed load leaves it empty, failed, with
        // its missing sources named.
        const auto index = resolve (layer);
        publishEmpty (index);
        std::string error;
        auto atlas = io::noteAtlasFromString (atlasTree["json"].toString().toStdString(), error);
        if (! atlas)
        {
            LoadResult failed;
            failed.error = "the session's Note Atlas cannot be read: " + error;
            layers[index].latestLoadId = nextGeneration.fetch_add (1);
            pushResult (std::move (failed), layer);
            return;
        }
        AtlasLoadRequest request;
        request.atlas = std::move (*atlas);
        enqueueAtlasLoad (std::move (request), layer);
        return;
    }

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
    presetNameOverride.clear();
    lastStartingStateFile = juce::File();
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
    // The factory starting states were made with the shared Reimagined stage: they keep it.
    perLayerReimagined = false;
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

std::vector<std::string> OspAudioProcessor::soundHashesInState (const juce::XmlElement& state)
{
    std::vector<std::string> hashes;
    for (const auto* id : { &ids::instrument, &ids::instrumentB, &ids::instrumentC })
        if (const auto* layer = state.getChildByName (juce::Identifier (*id).toString()))
        {
            if (const auto hash = layer->getStringAttribute ("contentHash"); hash.isNotEmpty())
                hashes.push_back (hash.toStdString());
            if (const auto* set = layer->getChildByName ("Set"))
                for (const auto* member : set->getChildWithTagNameIterator ("Member"))
                    if (const auto hash = member->getStringAttribute ("contentHash"); hash.isNotEmpty())
                        hashes.push_back (hash.toStdString());
            if (const auto* atlas = layer->getChildByName ("Atlas"))
                for (const auto* source : atlas->getChildWithTagNameIterator ("Source"))
                    if (const auto hash = source->getStringAttribute ("contentHash"); hash.isNotEmpty())
                        hashes.push_back (hash.toStdString());
        }
    std::sort (hashes.begin(), hashes.end());
    hashes.erase (std::unique (hashes.begin(), hashes.end()), hashes.end());
    return hashes;
}

bool OspAudioProcessor::savePreset (const juce::File& file)
{
    const auto xml = createStateXml();
    file.getParentDirectory().createDirectory();
    if (xml == nullptr || ! xml->writeTo (file))
        return false;
    lastPresetFile = file;
    libraryHub->presetSaved (file, library::AssetType::preset, soundHashesInState (*xml), slotCount(), stateVersion);
    return true;
}

bool OspAudioProcessor::loadPreset (const juce::File& file)
{
    const auto xml = juce::XmlDocument::parse (file);
    if (xml == nullptr || ! xml->hasTagName (parameters.state.getType()))
        return false;
    rememberPreviousState ("Before " + file.getFileNameWithoutExtension());
    applyStateXml (*xml);
    lastPresetFile = file;
    lastStartingStateFile = juce::File();
    presetNameOverride.clear();
    presetIsProgram = false;
    return true;
}

void OspAudioProcessor::resetSettings()
{
    for (auto* parameter : getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (parameter))
            ranged->setValueNotifyingHost (ranged->getDefaultValue());
    if (const auto links = modLinks(); links.isValid())
        parameters.state.removeChild (links, &undoManager);   // no routes, no links
    perLayerReimagined = true;   // a new patch
    // The sounds still loaded are heard: two meet in the middle of the blend, three at the
    // centre of the triangle, and every layer starts as Reimagined as A.
    const int slots = slotCount();
    if (slots >= 2)
        makeLayerAudible (slots - 1);
    for (int layer = 0; layer < numLayers; ++layer)
        setRootOverride (std::nullopt, layer);
    presetNameOverride = "Reset";
    presetIsProgram = false;
    lastPresetFile = juce::File();
    lastStartingStateFile = juce::File();
}

void OspAudioProcessor::initPatch()
{
    if (pendingLoads.load() > 0)
        return;
    for (int layer = 0; layer < numLayers; ++layer)
        if (isLayerOccupied (layer))
            clearLayer (layer);
    keptSlotCount = 0;
    removedLayer.reset();
    undoManager.clearUndoHistory();
    setEditLayer (0);
    resetSettings();
    presetNameOverride = "INIT";
}

bool OspAudioProcessor::saveStartingState (const juce::File& file)
{
    auto xml = createStateXml();
    if (xml == nullptr)
        return false;
    // Settings only: no sounds (their trees), no window state; the slots it was made with.
    for (const auto* id : { &ids::instrument, &ids::instrumentB, &ids::instrumentC })
        if (auto* child = xml->getChildByName (juce::Identifier (*id).toString()))
            xml->removeChildElement (child, true);
    for (const auto* property : { "uiScale", "advancedOpen", "arpExpanded", "editLayer", "program" })
        xml->removeAttribute (property);
    xml->setAttribute ("keptSlots", slotCount());
    xml->setAttribute ("startingState", 1);
    file.getParentDirectory().createDirectory();
    if (! xml->writeTo (file))
        return false;
    libraryHub->presetSaved (file, library::AssetType::templateState, {}, slotCount(), stateVersion);
    presetNameOverride = file.getFileNameWithoutExtension();
    presetIsProgram = false;
    lastPresetFile = juce::File();
    lastStartingStateFile = file;
    return true;
}

bool OspAudioProcessor::loadStartingState (const juce::File& file)
{
    const auto xml = juce::XmlDocument::parse (file);
    if (xml == nullptr || ! xml->hasTagName (parameters.state.getType()))
        return false;
    // Settings and the state's slots (empty ones wait for sounds); the sounds already here
    // keep playing - more sounds than the state had slots all stay (slotCount).
    rememberPreviousState ("Before " + file.getFileNameWithoutExtension());
    applyStateXml (*xml, true);
    presetNameOverride = file.getFileNameWithoutExtension();
    presetIsProgram = false;
    lastPresetFile = juce::File();
    lastStartingStateFile = file;
    return true;
}

namespace
{
    /** Documents/OSP: the user's presets, starting states and instruments (OSP_DOCUMENTS_DIR
        overrides it, so tests never touch a real user's folders). */
    juce::File documentsRoot()
    {
        if (const char* dir = std::getenv ("OSP_DOCUMENTS_DIR"); dir != nullptr && *dir != 0)
            return juce::File (juce::String::fromUTF8 (dir));
        return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("OSP");
    }
}

juce::File OspAudioProcessor::startingStateFolder()
{
    return documentsRoot().getChildFile ("Starting States");
}

juce::File OspAudioProcessor::presetFolder()
{
    return documentsRoot().getChildFile ("Presets");
}

juce::File OspAudioProcessor::instrumentFolder()
{
    return documentsRoot().getChildFile ("Instruments");
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

bool OspAudioProcessor::exportInstrument (const juce::File& file, juce::String& error, const ExportOptions& options)
{
    std::vector<LoadedInstrument::MemberFile> members;
    for (int layer = 0; layer < numLayers; ++layer)
        if (const auto instrument = currentInstrument (layer))
        {
            if (instrument->atlas != nullptr)
                members.insert (members.end(), instrument->atlasSources.begin(), instrument->atlasSources.end());
            else if (instrument->memberFiles.empty())
                members.push_back ({ instrument->contentHash, instrument->filename, instrument->originalPath });
            else
                members.insert (members.end(), instrument->memberFiles.begin(), instrument->memberFiles.end());
        }
    if (members.empty())
    {
        error = "nothing to export: load a sound first";
        return false;
    }

    // Every sound's rights, from the Library (unknown when it has no record).
    auto rights = std::make_shared<std::map<std::string, std::string>>();
    {
        std::vector<std::string> hashes;
        for (const auto& m : members)
            hashes.push_back (m.contentHash);
        libraryHub->runAndWait ([rights, hashes] (library::Catalog& c) {
            for (const auto& h : hashes)
                (*rights)[h] = c.provenanceOf (h);
        });
    }
    auto rightsOf = [&rights] (const std::string& hash) {
        const auto it = rights->find (hash);
        return it != rights->end() ? it->second : std::string ("unknown");
    };
    if (options.forDistribution)
    {
        juce::StringArray unknown;
        std::vector<std::string> unknownHashes;
        for (const auto& m : members)
            if (rightsOf (m.contentHash) == "unknown")
            {
                unknown.addIfNotAlreadyThere (juce::String (m.filename));
                unknownHashes.push_back (m.contentHash);
            }
        if (! unknown.isEmpty() && ! options.confirmUnknownRights)
        {
            error = "these sounds have no known rights: " + unknown.joinIntoString (", ") + ". Confirm that you may share them to export";
            return false;
        }
        // The user's confirmation is recorded (only then does unknown become cleared).
        for (const auto& h : unknownHashes)
            (*rights)[h] = "cleared";
        if (! unknownHashes.empty())
            libraryHub->post ([unknownHashes] (library::Catalog& c) {
                for (const auto& h : unknownHashes)
                    c.setProvenance (h, "cleared");
            });
    }

    juce::ZipFile::Builder zip;
    auto manifest = std::make_unique<juce::DynamicObject>();
    manifest->setProperty ("schemaVersion", 2);
    manifest->setProperty ("format", "OSP portable instrument");
    manifest->setProperty ("engineVersion", JucePlugin_VersionString);
    manifest->setProperty ("stateVersion", stateVersion);
    manifest->setProperty ("name", options.name.isNotEmpty() ? options.name : file.getFileNameWithoutExtension());
    manifest->setProperty ("category", options.category);
    manifest->setProperty ("notes", options.notes);
    manifest->setProperty ("creator", options.creator);
    juce::Array<juce::var> tags;
    for (const auto& t : options.tags)
        tags.add (t);
    manifest->setProperty ("tags", tags);
    juce::Array<juce::var> files;
    std::set<std::string> packed;
    for (const auto& m : members)
    {
        if (! packed.insert (m.contentHash).second)
            continue;   // the same sound on two layers: once
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
        entry->setProperty ("sha256", hash);
        entry->setProperty ("bytes", stored->getSize());
        entry->setProperty ("provenance", juce::String (rightsOf (m.contentHash)));
        files.add (juce::var (entry.release()));
    }
    manifest->setProperty ("sources", files);
    const auto manifestText = juce::JSON::toString (juce::var (manifest.release()));
    zip.addEntry (new juce::MemoryInputStream (manifestText.toRawUTF8(), manifestText.getNumBytesAsUTF8(), true), 9, "manifest.json",
                  juce::Time::getCurrentTime());
    const auto xml = createStateXml();
    const auto preset = xml->toString();
    zip.addEntry (new juce::MemoryInputStream (preset.toRawUTF8(), preset.getNumBytesAsUTF8(), true), 9, "preset.xml", juce::Time::getCurrentTime());

    // Written beside the target and moved into place: an existing file is replaced only by a
    // complete package, and the package is read back before that.
    const auto temp = file.getSiblingFile (file.getFileName() + ".partial");
    {
        juce::FileOutputStream out (temp);
        if (! out.openedOk() || ! (out.setPosition (0) && out.truncate().wasOk()) || ! zip.writeToStream (out, nullptr))
        {
            temp.deleteFile();
            error = "cannot write " + file.getFullPathName();
            return false;
        }
    }
    {
        juce::ZipFile check (temp);
        if (check.getEntry ("preset.xml") == nullptr || check.getEntry ("manifest.json") == nullptr || check.getNumEntries() < 2 + static_cast<int> (packed.size()))
        {
            temp.deleteFile();
            error = "the package could not be verified after writing";
            return false;
        }
    }
    if (! temp.moveFileTo (file))
    {
        temp.deleteFile();
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
    // The manifest (if any) is checked before anything is written: a newer format is refused,
    // and manifest 2 names every sound with its size (a truncated package changes nothing).
    if (const auto* manifestEntry = zip.getEntry ("manifest.json"))
    {
        std::unique_ptr<juce::InputStream> in (zip.createStreamForEntry (*manifestEntry));
        const auto manifest = in != nullptr ? juce::JSON::parse (in->readEntireStreamAsString()) : juce::var();
        const int version = static_cast<int> (manifest.getProperty ("schemaVersion", 1));
        if (version > 2)
        {
            error = file.getFileName() + " was made by a newer version of the plugin";
            return false;
        }
        if (const auto* sources = manifest.getProperty ("sources", juce::var()).getArray(); sources != nullptr && version >= 2)
            for (const auto& source : *sources)
            {
                const auto stored = source.getProperty ("stored", "").toString();
                const auto* entry = zip.getEntry (stored);
                const auto bytes = static_cast<juce::int64> (source.getProperty ("bytes", -1));
                if (entry == nullptr || (bytes >= 0 && entry->uncompressedSize != bytes))
                {
                    error = "the package is incomplete: " + source.getProperty ("filename", stored).toString() + " is missing or damaged";
                    return false;
                }
            }
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
        // A package is untrusted: an entry is one plain file name inside its folder (no
        // "..", no further folders, no absolute path), so nothing lands outside the store.
        const auto leaf = name.fromFirstOccurrenceOf ("/", false, false);
        if (leaf.isEmpty() || leaf.containsAnyOf ("/\\:") || leaf.contains ("..") || leaf.startsWithChar ('.'))
            continue;
        const auto target = store.directory().getChildFile (leaf);
        if (target.getParentDirectory() != store.directory() || target.existsAsFile())
            continue;
        std::unique_ptr<juce::InputStream> in (zip.createStreamForEntry (i));
        const auto temp = target.getSiblingFile (target.getFileName() + ".partial");
        {
            juce::FileOutputStream out (temp);
            if (in == nullptr || ! out.openedOk())
            {
                error = "cannot extract " + name;
                return false;
            }
            out.writeFromInputStream (*in, -1);
        }
        // A sound is stored under its content hash: its bytes must match its name.
        if (name.startsWith ("source/"))
        {
            const auto hash = io::sha256OfFile (temp.getFullPathName().toStdString());
            if (! hash || juce::String (*hash) != target.getFileNameWithoutExtension())
            {
                temp.deleteFile();
                error = "the package's sound " + leaf + " is damaged (its content does not match its name)";
                return false;
            }
        }
        if (! temp.moveFileTo (target))
        {
            temp.deleteFile();
            error = "cannot extract " + name;
            return false;
        }
    }
    std::unique_ptr<juce::InputStream> presetStream (zip.createStreamForEntry (*zip.getEntry ("preset.xml")));
    const auto xml = presetStream != nullptr ? juce::XmlDocument::parse (presetStream->readEntireStreamAsString()) : nullptr;
    if (xml == nullptr || ! xml->hasTagName (parameters.state.getType()))
    {
        error = "the instrument's settings are unreadable";
        return false;
    }
    rememberPreviousState ("Before " + file.getFileNameWithoutExtension());
    applyStateXml (*xml);
    return true;
}

bool OspAudioProcessor::loadLibrarySounds (const std::vector<Audition::LayerLoad>& sounds, const juce::String& label)
{
    if (sounds.empty())
        return false;
    for (const auto& s : sounds)
        if (s.layer < 0 || s.layer >= numLayers || ! s.file.existsAsFile())
            return false;
    rememberPreviousState (label);
    const bool pairBefore = occupiedLayerCount() >= 2;
    for (const auto& s : sounds)
    {
        LoadRequest request;
        request.file = s.file;
        request.filename = s.filename.toStdString();
        request.originalPath = s.originalPath.toStdString();
        // An occupied layer keeps its controls (a new sound: its own root); a new layer starts
        // neutral and is heard in the mix, as a dropped sound would be.
        const bool added = ! isLayerOccupied (s.layer) && s.layer >= keptSlotCount.load();
        setRootOverride (s.rootMidi, s.layer);   // the root the Library knows, else the analysis
        if (added)
            resetLayerControls (s.layer);
        userLoads.insert (nextGeneration.load());
        enqueueLoad (std::move (request), s.layer);
        if (added)
            makeLayerAudible (s.layer, pairBefore);
    }
    return true;
}

void OspAudioProcessor::rememberPreviousState (const juce::String& what)
{
    previousState = createStateXml();
    previousStateLabel = what;
}

bool OspAudioProcessor::restorePreviousState()
{
    if (previousState == nullptr)
        return false;
    // A swap: restoring again brings back what was just replaced.
    auto restore = std::move (previousState);
    rememberPreviousState ("Before restoring");
    applyStateXml (*restore);
    lastPresetFile = juce::File();
    lastStartingStateFile = juce::File();
    presetNameOverride = "Restored";
    presetIsProgram = false;
    return true;
}

juce::File OspAudioProcessor::libraryTrashFolder()
{
    return juce::File (juce::String (library::Catalog::defaultFile().parent_path().string())).getChildFile ("Trash");
}

std::optional<juce::File> OspAudioProcessor::renamePresetFile (const juce::File& file, const juce::String& newName, juce::String& error)
{
    const auto clean = juce::File::createLegalFileName (newName.trim());
    if (! file.existsAsFile() || clean.isEmpty())
    {
        error = clean.isEmpty() ? "a name is needed" : file.getFileName() + " is not there any more";
        return std::nullopt;
    }
    const auto target = file.getSiblingFile (clean + file.getFileExtension());
    if (target.exists() && target != file)
    {
        error = "there is already a " + target.getFileName();
        return std::nullopt;
    }
    if (! file.moveFileTo (target))
    {
        error = "cannot rename " + file.getFileName();
        return std::nullopt;
    }
    libraryHub->post ([from = file.getFullPathName().toStdString(), to = target.getFullPathName().toStdString(), name = clean.toStdString()] (library::Catalog& c) {
        if (const auto id = c.presetWithFile (from))
            c.movePreset (*id, to, name);
    });
    if (lastPresetFile == file)
        lastPresetFile = target;
    if (lastStartingStateFile == file)
        lastStartingStateFile = target;
    return target;
}

std::optional<juce::File> OspAudioProcessor::duplicatePresetFile (const juce::File& file, juce::String& error)
{
    const auto xml = juce::XmlDocument::parse (file);
    if (xml == nullptr || ! xml->hasTagName (parameters.state.getType()))
    {
        error = file.getFileName() + " is not a preset";
        return std::nullopt;
    }
    const auto copy = file.getParentDirectory().getNonexistentChildFile (file.getFileNameWithoutExtension() + " copy", file.getFileExtension(), false);
    if (! file.copyFileTo (copy))
    {
        error = "cannot copy " + file.getFileName();
        return std::nullopt;
    }
    const bool isTemplate = file.hasFileExtension (startingStateExtension);
    libraryHub->presetSaved (copy, isTemplate ? library::AssetType::templateState : library::AssetType::preset, soundHashesInState (*xml),
                             static_cast<int> (xml->getIntAttribute ("keptSlots", 1)), stateVersion);
    return copy;
}

std::optional<juce::File> OspAudioProcessor::trashPresetFile (const juce::File& file, juce::String& error)
{
    if (! file.existsAsFile())
    {
        error = file.getFileName() + " is not there any more";
        return std::nullopt;
    }
    const auto trash = libraryTrashFolder();
    trash.createDirectory();
    const auto stamp = juce::Time::getCurrentTime().formatted ("%Y-%m-%d %H%M%S");
    const auto target = trash.getNonexistentChildFile (stamp + " " + file.getFileNameWithoutExtension(), file.getFileExtension(), false);
    if (! file.moveFileTo (target))
    {
        error = "cannot move " + file.getFileName() + " to the Library's trash";
        return std::nullopt;
    }
    // The record remembers where it was (restore puts it back), and leaves the results.
    const auto from = file.getFullPathName().toStdString(), to = target.getFullPathName().toStdString();
    libraryHub->runAndWait ([from, to] (library::Catalog& c) {
        if (const auto id = c.presetWithFile (from))
        {
            c.movePreset (*id, to, "", from);
            c.trash (*id);
        }
    });
    if (lastPresetFile == file)
        lastPresetFile = juce::File();
    return target;
}

std::optional<juce::File> OspAudioProcessor::restorePresetFile (const juce::File& trashed, juce::String& error)
{
    if (! trashed.existsAsFile())
    {
        error = trashed.getFileName() + " is not in the trash any more";
        return std::nullopt;
    }
    auto from = std::make_shared<std::string>();
    auto id = std::make_shared<std::string>();
    libraryHub->runAndWait ([from, id, path = trashed.getFullPathName().toStdString()] (library::Catalog& c) {
        if (const auto found = c.presetWithFile (path, true))
        {
            *id = *found;
            *from = c.trashedFrom (*found);
        }
    });
    // Back where it came from (or beside it, if that name is taken now); without a record, into
    // the presets folder under its own name.
    auto original = from->empty() ? presetFolder().getChildFile (trashed.getFileName().fromFirstOccurrenceOf (" ", false, false).fromFirstOccurrenceOf (" ", false, false))
                                  : juce::File (juce::String (*from));
    if (original.exists())
        original = original.getParentDirectory().getNonexistentChildFile (original.getFileNameWithoutExtension() + " (restored)", original.getFileExtension(), false);
    original.getParentDirectory().createDirectory();
    if (! trashed.moveFileTo (original))
    {
        error = "cannot restore " + trashed.getFileName();
        return std::nullopt;
    }
    if (! id->empty())
        libraryHub->post ([assetId = *id, to = original.getFullPathName().toStdString()] (library::Catalog& c) {
            c.movePreset (assetId, to, "");
            c.restore (assetId);
        });
    return original;
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
