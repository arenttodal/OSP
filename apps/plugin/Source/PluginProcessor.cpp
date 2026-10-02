#include "PluginProcessor.h"

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
    static const juce::Identifier instrument = "Instrument";
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
        { "Natural", 30, 50, 50, 25, 15, 10, 2, 250 },
        { "Alive", 60, 65, 50, 45, 20, 20, 2, 300 },
        { "Floating", 35, 35, 45, 70, 55, 45, 120, 1500 },
        { "Broken", 85, 55, 65, 60, 25, 85, 2, 400 },
        { "Frozen", 10, 30, 50, 5, 35, 30, 250, 2500 },
        { "Dream", 40, 40, 35, 60, 75, 65, 300, 3000 },
        { "Wide", 40, 50, 50, 50, 70, 30, 10, 800 },
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
    Range releaseRange (5.0f, 5000.0f, 0.1f);
    releaseRange.setSkewForCentre (300.0f);

    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { ids::attack, 1 }, "Attack", attackRange, 2.0f, ms));
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { ids::release, 1 }, "Release", releaseRange, 250.0f, ms));
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
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { ids::life, 2 }, "Life", unit, 35.0f, percent));
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { ids::dynamics, 2 }, "Dynamics", unit, 50.0f, percent));
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { ids::character, 2 }, "Character", unit, 50.0f, percent));
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { ids::motion, 2 }, "Motion", unit, 35.0f, percent));
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { ids::space, 2 }, "Space", unit, 20.0f, percent));
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { ids::reimagined, 2 }, "Original / Reimagined", unit, 20.0f, percent));
    // Advanced (spec §13).
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { ids::pitchCharacter, 2 }, "Pitch Character",
                                                              juce::StringArray { "Tape", "Natural" }, 0));
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { ids::sustain, 2 }, "Sustain",
                                                              juce::StringArray { "Recording", "Endless" }, 1));
    layout.add (std::make_unique<juce::AudioParameterInt> (juce::ParameterID { ids::seed, 2 }, "Variation Seed", 1, 9999, 1));
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { ids::mpe, 3 }, "MPE", false));
    return layout;
}

//==============================================================================
// Undo / redo actions (spec §56)

class InstrumentChangeAction final : public juce::UndoableAction
{
public:
    InstrumentChangeAction (OspAudioProcessor& p, std::uint64_t before, std::uint64_t after) : processor (p), beforeLoad (before), afterLoad (after) {}
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
        const auto it = processor.latestByLoad.find (loadId);
        if (it == processor.latestByLoad.end())
            return false;
        processor.republish (it->second);
        return true;
    }
    OspAudioProcessor& processor;
    std::uint64_t beforeLoad, afterLoad;
    bool first = true;
};

class RootChangeAction final : public juce::UndoableAction
{
public:
    RootChangeAction (OspAudioProcessor& p, std::optional<double> before, std::optional<double> after) : processor (p), from (before), to (after) {}
    bool perform() override
    {
        processor.setRootOverride (to);
        return true;
    }
    bool undo() override
    {
        processor.setRootOverride (from);
        return true;
    }

private:
    OspAudioProcessor& processor;
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
    if (playing != nullptr && playing->set != nullptr)
        engine.setInstrumentSet (playing->set.get());
    else
        engine.setModel (playing != nullptr ? playing->model.get() : nullptr);
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
    if (force || changed (attack, lastAttack) || changed (release, lastRelease))
    {
        AdsrSettings adsr = engineSettings.adsr;
        adsr.attackSeconds = attack * 0.001;
        adsr.releaseSeconds = release * 0.001;
        engine.setEnvelope (adsr);
        lastAttack = attack;
        lastRelease = release;
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

    engine.setPitchOffsetSemitones (pitchBendSemitones + fineTuneParam->load() / 100.0 + rootShiftSemitones.load());
}

void OspAudioProcessor::swapInstrumentIfPending() noexcept
{
    if (const auto* next = exchange.takePending())
    {
        if (playing != nullptr)
        {
            // Keep the old instrument alive while voices still play it.
            auto slot = std::find (retired.begin(), retired.end(), nullptr);
            if (slot == retired.end())
            {
                // Too many overlapping swaps: silence the oldest retired instrument.
                auto oldest = std::min_element (retired.begin(), retired.end(),
                                                [] (auto* a, auto* b) { return a->generation < b->generation; });
                if ((*oldest)->set != nullptr)
                    engine.killVoicesUsing ((*oldest)->set.get());
                else
                    engine.killVoicesUsing ((*oldest)->model.get());
                slot = oldest;
            }
            *slot = playing;
        }
        playing = next;
        if (playing->set != nullptr)
            engine.setInstrumentSet (playing->set.get());
        else
            engine.setModel (playing->model.get());
    }

    std::uint64_t oldest = playing != nullptr ? playing->generation : 0;
    for (auto& r : retired)
    {
        if (r == nullptr)
            continue;
        if (r->set != nullptr ? ! engine.isSetInUse (r->set.get()) : ! engine.isModelInUse (r->model.get()))
            r = nullptr;
        else
            oldest = std::min (oldest, r->generation);
    }
    if (playing != nullptr)
        exchange.publishOldestInUse (oldest);
}

void OspAudioProcessor::handleMidi (const juce::MidiMessage& m) noexcept
{
    const int channel = m.getChannel();
    // MPE (lower zone): channel 1 is the manager channel, 2..16 carry one note each.
    const bool memberChannel = engine.isMpe() && channel >= 2;
    if (m.isNoteOn())
        engine.noteOn (m.getNoteNumber(), m.getVelocity(), channel);
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
        engine.setPitchOffsetSemitones (pitchBendSemitones + fineTuneParam->load() / 100.0 + rootShiftSemitones.load());
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
    if (auto* head = getPlayHead())
        if (const auto position = head->getPosition())
        {
            const bool transportRunning = position->getIsPlaying();
            if (transportRunning && ! hostWasPlaying)
                engine.resetPerformance();
            hostWasPlaying = transportRunning;
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

    activeVoices.store (engine.activeVoiceCount(), std::memory_order_relaxed);
}

//==============================================================================
// Loading (message thread + loader thread)

void OspAudioProcessor::enqueueLoad (LoadRequest request)
{
    ++pendingLoads;
    state = LoadState::loading;
    const auto generation = nextGeneration.fetch_add (1);
    latestLoadId = generation;
    loaderPool.addJob ([this, request = std::move (request), generation] {
        auto result = loadInstrument (request, store, generation);
        // Queue the next model stage before reporting, so pendingLoads never reads 0 in between.
        if (result.instrument != nullptr)
            enqueueRefine (result.instrument, result.audio);
        {
            const std::lock_guard<std::mutex> lock (resultsMutex);
            finishedLoads.push_back (std::move (result));
        }
        --pendingLoads;
    });
}

void OspAudioProcessor::enqueueRefine (std::shared_ptr<const LoadedInstrument> base, std::shared_ptr<const AudioData> audio)
{
    if (base == nullptr || base->model == nullptr || base->model->stage == InstrumentModel::Stage::complete)
        return;
    ++pendingLoads;
    loaderPool.addJob ([this, base = std::move (base), audio = std::move (audio)] {
        // A newer sample was dropped meanwhile: do not spend time on this one.
        if (base->loadId == latestLoadId.load())
        {
            auto result = refineInstrument (*base, audio, nextGeneration.fetch_add (1));
            if (result.instrument != nullptr)
                enqueueRefine (result.instrument, result.audio);
            result.audio.reset();
            const std::lock_guard<std::mutex> lock (resultsMutex);
            finishedLoads.push_back (std::move (result));
        }
        --pendingLoads;
    });
}

void OspAudioProcessor::pushResult (LoadResult result)
{
    const std::lock_guard<std::mutex> lock (resultsMutex);
    finishedLoads.push_back (std::move (result));
}

void OspAudioProcessor::enqueueSetLoad (SetLoadRequest request)
{
    ++pendingLoads;
    state = LoadState::loading;
    const auto generation = nextGeneration.fetch_add (1);
    latestLoadId = generation;
    loaderPool.addJob ([this, request = std::move (request), generation] {
        pushResult (loadInstrumentSet (request, store, generation));
        --pendingLoads;
    });
}

void OspAudioProcessor::loadFiles (const juce::Array<juce::File>& files)
{
    if (files.size() == 1)
    {
        loadFile (files.getFirst());
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
    enqueueSetLoad (std::move (request));
}

void OspAudioProcessor::reassignSample (const std::string& filename, SampleRole role, int layer, std::optional<double> rootMidi)
{
    const auto instrument = currentInstrument();
    if (instrument == nullptr || instrument->set == nullptr)
        return;
    auto assignments = instrument->assignments;
    assignments.erase (std::remove_if (assignments.begin(), assignments.end(), [&] (const SetAssignment& a) { return a.filename == filename; }),
                       assignments.end());
    assignments.push_back ({ filename, role, rootMidi, layer });
    ++pendingLoads;
    loaderPool.addJob ([this, instrument, assignments, generation = nextGeneration.fetch_add (1)] {
        pushResult (reassignInstrumentSet (*instrument, assignments, generation));
        --pendingLoads;
    });
}

void OspAudioProcessor::loadFile (const juce::File& file)
{
    LoadRequest request;
    request.file = file;
    userLoads.insert (nextGeneration.load());
    enqueueLoad (std::move (request));
}

void OspAudioProcessor::loadExample()
{
    // A synthetic vowel stands in for the factory examples until legally owned ones exist.
    const auto file = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("OSP Example Vowel A3.wav");
    if (! file.existsAsFile())
    {
        auto audio = testsignals::vowel (midiToHz (57), 5.0, 48000.0, 1);
        std::string error;
        io::writeAudioFile (std::filesystem::path (file.getFullPathName().toStdString()), audio, io::SampleFormat::pcm24, error);
    }
    loadFile (file);
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

void OspAudioProcessor::timerCallback()
{
    std::deque<LoadResult> results;
    {
        const std::lock_guard<std::mutex> lock (resultsMutex);
        results.swap (finishedLoads);
    }

    bool published = false;
    for (auto& result : results)
    {
        juce::String message;
        if (result.instrument != nullptr)
        {
            const std::lock_guard<std::mutex> lock (modelMutex);
            // Ignore results superseded by a newer request that finished first.
            const auto latest = exchange.latestModel();
            if (latest == nullptr || latest->generation < result.instrument->generation)
            {
                exchange.publish (result.instrument);
                published = true;
                const auto loadId = result.instrument->loadId;
                latestByLoad[loadId] = result.instrument;
                if (loadId != lastPublishedLoad)
                {
                    // A new sample (not a later stage of the same one): undoable if the user asked for it.
                    if (userLoads.count (loadId) > 0 && lastPublishedLoad != 0)
                    {
                        undoManager.beginNewTransaction ("Load sample");
                        undoManager.perform (new InstrumentChangeAction (*this, lastPublishedLoad, loadId));
                    }
                    lastPublishedLoad = loadId;
                }
                // Keep undo history bounded (the instruments hold audio).
                while (latestByLoad.size() > 8)
                    latestByLoad.erase (latestByLoad.begin());
            }
            if (! result.warnings.empty())
                message = juce::String (result.warnings.front());
        }
        else
            message = juce::String (result.error);

        const std::lock_guard<std::mutex> lock (messageMutex);
        lastMessage = message;
    }

    if (published)
        setRootOverride (rootOverride()); // re-derive the shift for the new analysis root

    if (! results.empty())
        lastLoadFailed = results.back().instrument == nullptr;

    bool idle = false;
    {
        // A worker pushes its result before decrementing pendingLoads, so this is race-free.
        const std::lock_guard<std::mutex> lock (resultsMutex);
        idle = finishedLoads.empty() && pendingLoads.load() == 0;
    }
    const auto current = currentInstrument();
    if (idle)
    {
        // A failed load keeps the previous instrument playable; the error is shown as a message.
        state = current != nullptr ? LoadState::ready : (lastLoadFailed ? LoadState::failed : LoadState::empty);
    }
    else if (current != nullptr && current->loadId == latestLoadId.load())
    {
        // Playable as soon as the first stage is in (spec §62); later stages refine it.
        state = LoadState::ready;
    }

    const std::lock_guard<std::mutex> lock (modelMutex);
    exchange.collectGarbage();
}

std::shared_ptr<const LoadedInstrument> OspAudioProcessor::currentInstrument() const
{
    const std::lock_guard<std::mutex> lock (modelMutex);
    return exchange.latestModel();
}

juce::String OspAudioProcessor::stageMessage() const
{
    const auto instrument = currentInstrument();
    if (instrument == nullptr || instrument->model == nullptr || pendingLoads.load() == 0)
        return "Ready";
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
// Root

void OspAudioProcessor::setRootOverride (std::optional<double> midi)
{
    hasRootOverride = midi.has_value();
    if (midi)
        rootOverrideMidi = *midi;

    const auto instrument = currentInstrument();
    const double analysisRoot = instrument != nullptr ? instrument->analysisRootMidi : 60.0;
    rootShiftSemitones = midi ? analysisRoot - *midi : 0.0;
}

std::optional<double> OspAudioProcessor::rootOverride() const
{
    if (hasRootOverride.load())
        return rootOverrideMidi.load();
    return std::nullopt;
}

double OspAudioProcessor::effectiveRootMidi() const
{
    if (const auto overrideMidi = rootOverride())
        return *overrideMidi;
    const auto instrument = currentInstrument();
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
    stateTree.setProperty ("program", currentProgram, nullptr);

    stateTree.removeChild (stateTree.getChildWithName (ids::instrument), nullptr);
    juce::ValueTree instrumentTree (ids::instrument);
    if (const auto instrument = currentInstrument())
    {
        instrumentTree.setProperty ("contentHash", juce::String (instrument->contentHash), nullptr);
        instrumentTree.setProperty ("filename", juce::String (instrument->filename), nullptr);
        instrumentTree.setProperty ("originalPath", juce::String (instrument->originalPath), nullptr);
        instrumentTree.setProperty ("playbackRootMidi", exactString (instrument->analysisRootMidi), nullptr);
        instrumentTree.setProperty ("rootOrigin", juce::String (instrument->rootOrigin), nullptr);
        instrumentTree.setProperty ("startSeconds", exactString (instrument->startSeconds), nullptr);
        instrumentTree.setProperty ("playbackGainDb", exactString (instrument->playbackGainDb), nullptr);
    }
    if (const auto overrideMidi = rootOverride())
        instrumentTree.setProperty ("rootOverride", exactString (*overrideMidi), nullptr);
    if (const auto instrument = currentInstrument(); instrument != nullptr && instrument->set != nullptr)
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
        instrumentTree.appendChild (setTree, nullptr);
    }
    stateTree.appendChild (instrumentTree, nullptr);
    return stateTree.createXml();
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
    currentProgram = static_cast<int> (stateTree.getProperty ("program", 0));
    const auto instrumentTree = stateTree.getChildWithName (ids::instrument);
    stateTree.removeChild (instrumentTree, nullptr);
    const int savedVersion = static_cast<int> (stateTree.getProperty ("stateVersion", 1));
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

    if (instrumentTree.hasProperty ("rootOverride"))
        setRootOverride (exactValue (instrumentTree["rootOverride"], 60.0));
    else
        setRootOverride (std::nullopt);

    const auto setTree = instrumentTree.getChildWithName ("Set");
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
        enqueueSetLoad (std::move (request));
        return;
    }

    const auto hash = instrumentTree["contentHash"].toString();
    if (hash.isNotEmpty())
    {
        LoadRequest request;
        request.expectedHash = hash.toStdString();
        request.filename = instrumentTree["filename"].toString().toStdString();
        request.originalPath = instrumentTree["originalPath"].toString().toStdString();
        if (instrumentTree.hasProperty ("playbackRootMidi"))
        {
            LoadRequest::SavedPlayback saved;
            saved.rootMidi = exactValue (instrumentTree["playbackRootMidi"], 60.0);
            saved.rootOrigin = instrumentTree["rootOrigin"].toString().toStdString();
            saved.startSeconds = exactValue (instrumentTree["startSeconds"], 0.0);
            saved.gainDb = exactValue (instrumentTree["playbackGainDb"], 0.0);
            request.savedPlayback = saved;
        }
        enqueueLoad (std::move (request));
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

void OspAudioProcessor::changeRootOverride (std::optional<double> midi)
{
    undoManager.beginNewTransaction ("Root");
    undoManager.perform (new RootChangeAction (*this, rootOverride(), midi));
}

void OspAudioProcessor::republish (std::shared_ptr<const LoadedInstrument> instrument)
{
    if (instrument == nullptr)
        return;
    // A copy with a fresh generation keeps ModelExchange's ordering intact; the models
    // and audio are shared, nothing is rebuilt.
    auto copy = std::make_shared<LoadedInstrument> (*instrument);
    copy->generation = nextGeneration.fetch_add (1);
    {
        const std::lock_guard<std::mutex> lock (modelMutex);
        exchange.publish (copy);
    }
    lastPublishedLoad = copy->loadId;
    setRootOverride (rootOverride());
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
    const auto instrument = currentInstrument();
    if (instrument == nullptr)
    {
        error = "nothing to export: load a sound first";
        return false;
    }
    std::vector<LoadedInstrument::MemberFile> members = instrument->memberFiles;
    if (members.empty())
        members.push_back ({ instrument->contentHash, instrument->filename, instrument->originalPath });

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
