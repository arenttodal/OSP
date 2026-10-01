#include "PluginProcessor.h"

#include "PluginEditor.h"

#include "audio/utility/TestSignals.h"
#include "core/PitchMath.h"
#include "io/AudioFileIO.h"

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
    static const juce::Identifier instrument = "Instrument";
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
    return layout;
}

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

    samplerSettings.polyphony = 24;
    samplerSettings.outputGainDb = -9.0;
    sampler.prepare (48000.0, 512, samplerSettings);

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
    // Not the audio thread: allocation is allowed here.
    sampler.prepare (sampleRate, samplesPerBlock, samplerSettings);
    sampler.setSource (playing != nullptr ? &playing->playback : nullptr);
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
        AdsrSettings adsr = samplerSettings.adsr;
        adsr.attackSeconds = attack * 0.001;
        adsr.releaseSeconds = release * 0.001;
        sampler.setEnvelope (adsr);
        lastAttack = attack;
        lastRelease = release;
    }

    const float gain = gainParam->load();
    if (force || changed (gain, lastGain))
    {
        sampler.setOutputGainDb (samplerSettings.outputGainDb + gain);
        lastGain = gain;
    }

    const float velocityRange = velocityRangeParam->load();
    if (force || changed (velocityRange, lastVelocityRange))
    {
        sampler.setVelocityRangeDb (velocityRange);
        lastVelocityRange = velocityRange;
    }

    sampler.setPitchOffsetSemitones (pitchBendSemitones + fineTuneParam->load() / 100.0 + rootShiftSemitones.load());
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
                sampler.killVoicesUsing (&(*oldest)->playback);
                slot = oldest;
            }
            *slot = playing;
        }
        playing = next;
        sampler.setSource (&playing->playback);
    }

    std::uint64_t oldest = playing != nullptr ? playing->generation : 0;
    for (auto& r : retired)
    {
        if (r == nullptr)
            continue;
        if (! sampler.isSourceInUse (&r->playback))
            r = nullptr;
        else
            oldest = std::min (oldest, r->generation);
    }
    if (playing != nullptr)
        exchange.publishOldestInUse (oldest);
}

void OspAudioProcessor::handleMidi (const juce::MidiMessage& m) noexcept
{
    if (m.isNoteOn())
        sampler.noteOn (m.getNoteNumber(), m.getVelocity());
    else if (m.isNoteOff())
        sampler.noteOff (m.getNoteNumber());
    else if (m.isSustainPedalOn() || m.isSustainPedalOff())
        sampler.setSustainPedal (m.isSustainPedalOn());
    else if (m.isAllNotesOff() || m.isAllSoundOff())
        sampler.allNotesOff();
    else if (m.isPitchWheel())
    {
        const double normalised = (m.getPitchWheelValue() - 8192) / 8192.0;
        pitchBendSemitones = normalised * bendRangeParam->load();
        sampler.setPitchOffsetSemitones (pitchBendSemitones + fineTuneParam->load() / 100.0 + rootShiftSemitones.load());
    }
}

void OspAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int numSamples = buffer.getNumSamples();

    keyboardState.processNextMidiBuffer (midi, 0, numSamples, true);
    swapInstrumentIfPending();
    applyParameters (false);

    const int outChannels = std::min (buffer.getNumChannels(), 2);
    auto* const* channels = buffer.getArrayOfWritePointers();

    int position = 0;
    auto renderTo = [&] (int end) {
        if (end <= position || outChannels <= 0)
            return;
        float* segment[2] = { channels[0] + position, channels[outChannels > 1 ? 1 : 0] + position };
        sampler.render (segment, outChannels, end - position);
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

    activeVoices.store (sampler.activeVoiceCount(), std::memory_order_relaxed);
}

//==============================================================================
// Loading (message thread + loader thread)

void OspAudioProcessor::enqueueLoad (LoadRequest request)
{
    ++pendingLoads;
    state = LoadState::loading;
    const auto generation = nextGeneration.fetch_add (1);
    loaderPool.addJob ([this, request = std::move (request), generation] {
        auto result = loadInstrument (request, store, generation);
        {
            const std::lock_guard<std::mutex> lock (resultsMutex);
            finishedLoads.push_back (std::move (result));
        }
        --pendingLoads;
    });
}

void OspAudioProcessor::loadFile (const juce::File& file)
{
    LoadRequest request;
    request.file = file;
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
    if (idle)
    {
        // A failed load keeps the previous instrument playable; the error is shown as a message.
        const bool haveInstrument = currentInstrument() != nullptr;
        state = haveInstrument ? LoadState::ready : (lastLoadFailed ? LoadState::failed : LoadState::empty);
    }

    const std::lock_guard<std::mutex> lock (modelMutex);
    exchange.collectGarbage();
}

std::shared_ptr<const LoadedInstrument> OspAudioProcessor::currentInstrument() const
{
    const std::lock_guard<std::mutex> lock (modelMutex);
    return exchange.latestModel();
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
    auto stateTree = parameters.copyState();
    stateTree.setProperty ("stateVersion", stateVersion, nullptr);

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
    stateTree.appendChild (instrumentTree, nullptr);

    if (auto xml = stateTree.createXml())
        copyXmlToBinary (*xml, destData);
}

void OspAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    const auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName (parameters.state.getType()))
        return;

    auto stateTree = juce::ValueTree::fromXml (*xml);
    const auto instrumentTree = stateTree.getChildWithName (ids::instrument);
    stateTree.removeChild (instrumentTree, nullptr);
    parameters.replaceState (stateTree);

    if (instrumentTree.hasProperty ("rootOverride"))
        setRootOverride (exactValue (instrumentTree["rootOverride"], 60.0));
    else
        setRootOverride (std::nullopt);

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

juce::AudioProcessorEditor* OspAudioProcessor::createEditor()
{
    return new OspAudioProcessorEditor (*this);
}

} // namespace osp::plugin

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new osp::plugin::OspAudioProcessor();
}
