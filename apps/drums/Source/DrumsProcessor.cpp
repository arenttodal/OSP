#include "DrumsProcessor.h"

#include "DrumsEditor.h"

#include "core/PitchMath.h"

namespace osp::drumsplugin
{

namespace
{
    const char* const kitIds[] = { "kit.volume", "kit.life", "kit.dynamics", "kit.character", "kit.drive", "kit.motion", "kit.echo", "kit.space" };

    float raw (juce::AudioProcessorValueTreeState& state, const juce::String& id)
    {
        if (auto* p = state.getRawParameterValue (id))
            return p->load (std::memory_order_relaxed);
        return 0.0f;
    }

    juce::String categoryLabel (drums::DrumCategory c)
    {
        switch (c)
        {
            case drums::DrumCategory::kick: return "Kick";
            case drums::DrumCategory::snare: return "Snare";
            case drums::DrumCategory::clap: return "Clap";
            case drums::DrumCategory::closedHat: return "Closed Hat";
            case drums::DrumCategory::openHat: return "Open Hat";
            case drums::DrumCategory::tom: return "Tom";
            case drums::DrumCategory::rim: return "Rim";
            case drums::DrumCategory::cymbal: return "Cymbal";
            case drums::DrumCategory::percussion: return "Percussion";
            case drums::DrumCategory::unknown: break;
        }
        return {};
    }
} // namespace

DrumsProcessor::DrumsProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "Parameters", createLayout())
{
    // The factory kit's pads: the GM layout's instruments (names, colours and voice modes).
    using C = drums::DrumCategory;
    const std::array<C, numPads> factory { C::kick, C::snare, C::closedHat, C::openHat, C::clap, C::tom, C::rim, C::percussion, C::cymbal };
    for (int p = 0; p < numPads; ++p)
    {
        auto& info = pads[index (p)];
        const auto defaults = drums::PadSettings::defaultsFor (factory[index (p)]);
        info.category = factory[index (p)];
        info.chokeGroup = defaults.chokeGroup;
        info.voiceMode = defaults.voiceMode;
        info.trigger = defaults.trigger;
    }
    for (auto& r : retired)
        r.fill (nullptr);
    publishPadConfig();
    startTimerHz (20);
}

DrumsProcessor::~DrumsProcessor()
{
    stopTimer();
    loaderPool.removeAllJobs (true, 10000);
}

juce::AudioProcessorValueTreeState::ParameterLayout DrumsProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "keyboardMode", 1 }, "Keyboard Mode", StringArray { "PAD", "EXPANDED" }, 0));

    const drums::KitSettings kit;
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "kit.volume", 1 }, "Volume", NormalisableRange<float> (-60.0f, 6.0f, 0.01f),
                                                       static_cast<float> (kit.volumeDb), AudioParameterFloatAttributes().withLabel ("dB")));
    auto macro = [&] (const char* id, const char* name, double value) {
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, name, NormalisableRange<float> (0.0f, 1.0f, 0.0001f), static_cast<float> (value)));
    };
    macro ("kit.life", "Life", kit.macros.life);
    macro ("kit.dynamics", "Dynamics", kit.macros.dynamics);
    macro ("kit.character", "Character", kit.macros.character);
    macro ("kit.drive", "Drive", kit.macros.drive);
    macro ("kit.motion", "Movement", kit.macros.motion);
    macro ("kit.echo", "Echo", kit.macros.echo);
    macro ("kit.space", "Space", kit.macros.space);

    for (int p = 0; p < numPads; ++p)
    {
        const auto n = " " + String (p + 1);
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { padParamId (p, "gain"), 1 }, "Pad" + n + " Gain",
                                                           NormalisableRange<float> (-60.0f, 12.0f, 0.01f), 0.0f, AudioParameterFloatAttributes().withLabel ("dB")));
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { padParamId (p, "pan"), 1 }, "Pad" + n + " Pan",
                                                           NormalisableRange<float> (-1.0f, 1.0f, 0.001f), 0.0f));
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { padParamId (p, "tune"), 1 }, "Pad" + n + " Tune",
                                                           NormalisableRange<float> (-24.0f, 24.0f, 0.01f), 0.0f, AudioParameterFloatAttributes().withLabel ("st")));
        layout.add (std::make_unique<AudioParameterBool> (ParameterID { padParamId (p, "mute"), 1 }, "Pad" + n + " Mute", false));
        layout.add (std::make_unique<AudioParameterBool> (ParameterID { padParamId (p, "solo"), 1 }, "Pad" + n + " Solo", false));
    }
    return layout;
}

bool DrumsProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono();
}

void DrumsProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    // The engine's settings come from the parameters; everything else is pushed below.
    drums::KitSettings kit;
    kit.keyboardMode = keyboardMode();
    kit.keyMap = map;
    kit.volumeDb = raw (apvts, "kit.volume");
    kit.macros.life = raw (apvts, "kit.life");
    kit.macros.dynamics = raw (apvts, "kit.dynamics");
    kit.macros.character = raw (apvts, "kit.character");
    kit.macros.drive = raw (apvts, "kit.drive");
    kit.macros.motion = raw (apvts, "kit.motion");
    kit.macros.echo = raw (apvts, "kit.echo");
    kit.macros.space = raw (apvts, "kit.space");
    std::array<drums::PadSettings, numPads> settings {};
    for (int p = 0; p < numPads; ++p)
    {
        auto& s = settings[index (p)];
        const auto& info = pads[index (p)];
        s = drums::PadSettings::defaultsFor (info.category);
        s.chokeGroup = info.chokeGroup;
        s.voiceMode = info.voiceMode;
        s.trigger = info.trigger;
        s.gainDb = raw (apvts, padParamId (p, "gain"));
        s.pan = raw (apvts, padParamId (p, "pan"));
        s.tuneSemitones = raw (apvts, padParamId (p, "tune"));
        s.mute = raw (apvts, padParamId (p, "mute")) > 0.5f;
        s.solo = raw (apvts, padParamId (p, "solo")) > 0.5f;
    }
    engine.prepare (sampleRate, std::max (16, samplesPerBlock), kit, settings);
    for (int p = 0; p < numPads; ++p)
    {
        if (playing[index (p)] != nullptr)
            engine.setPadModel (p, playing[index (p)]->model.get());
        pulled[index (p)] = Pulled {};
    }
    kitPulled = false;
    configSeen = ~0u;
    audioMap = map;
    wasPlaying = false;
}

// ---------------------------------------------------------------------------------------
// Audio thread
// ---------------------------------------------------------------------------------------

void DrumsProcessor::takeNewSounds() noexcept
{
    for (int p = 0; p < numPads; ++p)
    {
        const auto i = index (p);
        const LoadedInstrument* fresh = exchange[i].takePending();
        const bool clear = clearRequest[i].exchange (false, std::memory_order_acq_rel);
        if (fresh != nullptr || clear)
        {
            if (playing[i] != nullptr)
            {
                // Held hits finish on the old sound; with no room left, the oldest one ends.
                auto& slots = retired[i];
                auto* free = std::find (slots.begin(), slots.end(), nullptr);
                if (free == slots.end())
                {
                    engine.killVoicesUsing (slots.front()->model.get());
                    std::move (slots.begin() + 1, slots.end(), slots.begin());
                    free = slots.end() - 1;
                }
                *free = playing[i];
            }
            playing[i] = clear && fresh == nullptr ? nullptr : fresh;
            engine.setPadModel (p, playing[i] != nullptr ? playing[i]->model.get() : nullptr);
        }
        std::uint64_t oldest = playing[i] != nullptr ? playing[i]->generation : std::numeric_limits<std::uint64_t>::max();
        for (auto& r : retired[i])
        {
            if (r != nullptr && ! engine.isModelInUse (r->model.get()))
                r = nullptr;
            if (r != nullptr)
                oldest = std::min (oldest, r->generation);
        }
        exchange[i].publishOldestInUse (oldest);
    }
}

void DrumsProcessor::pullKeyMap() noexcept
{
    const auto g1 = configGeneration.load (std::memory_order_acquire);
    if (g1 == configSeen || (g1 & 1u) != 0)
        return;
    drums::KeyMap m;
    std::array<int, numPads> choke {}, voice {}, trigger {};
    for (int p = 0; p < numPads; ++p)
    {
        const auto i = index (p);
        m.padNote[i] = aNote[i].load (std::memory_order_relaxed);
        m.zone[i] = drums::Zone { aLow[i].load (std::memory_order_relaxed), aHigh[i].load (std::memory_order_relaxed),
                                  aRoot[i].load (std::memory_order_relaxed) };
        choke[i] = aChoke[i].load (std::memory_order_relaxed);
        voice[i] = aVoice[i].load (std::memory_order_relaxed);
        trigger[i] = aTrigger[i].load (std::memory_order_relaxed);
    }
    m.allowOverlap = aOverlap.load (std::memory_order_relaxed);
    std::atomic_thread_fence (std::memory_order_acquire);
    if (configGeneration.load (std::memory_order_relaxed) != g1)
        return;   // written meanwhile: next block
    configSeen = g1;
    audioMap = m;
    engine.setKeyMap (audioMap);
    for (int p = 0; p < numPads; ++p)
    {
        const auto i = index (p);
        pulled[i].choke = choke[i];
        pulled[i].voice = voice[i];
        pulled[i].trigger = trigger[i];
        auto s = engine.padSettings (p);
        s.chokeGroup = choke[i];
        s.voiceMode = voice[i] == 1 ? drums::VoiceMode::monoRetrigger : drums::VoiceMode::poly;
        s.trigger = trigger[i] == 1 ? drums::TriggerMode::gate : drums::TriggerMode::oneShot;
        engine.setPadSettings (p, s);
    }
}

void DrumsProcessor::pullSettings() noexcept
{
    engine.setKeyboardMode (raw (apvts, "keyboardMode") > 0.5f ? drums::KeyboardMode::expanded : drums::KeyboardMode::pad);

    std::array<double, 8> kit {};
    for (std::size_t k = 0; k < kit.size(); ++k)
        kit[k] = raw (apvts, kitIds[k]);
    if (! kitPulled || kit != pulledKit)
    {
        pulledKit = kit;
        kitPulled = true;
        engine.setVolumeDb (kit[0]);
        Macros m;
        m.life = kit[1];
        m.dynamics = kit[2];
        m.character = kit[3];
        m.drive = kit[4];
        m.motion = kit[5];
        m.echo = kit[6];
        m.space = kit[7];
        m.reimagined = 0.0;
        engine.setKitMacros (m);
    }
    for (int p = 0; p < numPads; ++p)
    {
        auto& c = pulled[index (p)];
        const double gain = raw (apvts, padParamId (p, "gain"));
        const double pan = raw (apvts, padParamId (p, "pan"));
        const double tune = raw (apvts, padParamId (p, "tune"));
        const bool mute = raw (apvts, padParamId (p, "mute")) > 0.5f;
        const bool solo = raw (apvts, padParamId (p, "solo")) > 0.5f;
        if (gain == c.gain && pan == c.pan && tune == c.tune && mute == c.mute && solo == c.solo)
            continue;
        c.gain = gain;
        c.pan = pan;
        c.tune = tune;
        c.mute = mute;
        c.solo = solo;
        auto s = engine.padSettings (p);
        s.gainDb = gain;
        s.pan = pan;
        s.tuneSemitones = tune;
        s.mute = mute;
        s.solo = solo;
        engine.setPadSettings (p, s);
    }
}

void DrumsProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int numSamples = buffer.getNumSamples();
    takeNewSounds();
    pullKeyMap();
    pullSettings();

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
            // A bounce from the same position performs identically: the pads' performance
            // memory restarts whenever the transport starts.
            if (timing.playing && ! wasPlaying)
                engine.resetPerformance();
            wasPlaying = timing.playing;
        }
    engine.setHostTiming (timing);

    // Mouse auditions land at the block's start.
    {
        const auto scope = auditionFifo.read (auditionFifo.getNumReady());
        for (int i = 0; i < scope.blockSize1; ++i)
        {
            const auto& a = auditions[static_cast<std::size_t> (scope.startIndex1 + i)];
            a.velocity > 0 ? engine.triggerPad (a.pad, a.velocity) : engine.releasePad (a.pad);
        }
        for (int i = 0; i < scope.blockSize2; ++i)
        {
            const auto& a = auditions[static_cast<std::size_t> (scope.startIndex2 + i)];
            a.velocity > 0 ? engine.triggerPad (a.pad, a.velocity) : engine.releasePad (a.pad);
        }
    }

    // Sample-accurate MIDI: render up to each event, then apply it.
    std::array<float*, 2> out { buffer.getWritePointer (0), buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : nullptr };
    const int channels = buffer.getNumChannels() > 1 ? 2 : 1;
    int position = 0;
    auto renderTo = [&] (int end) {
        if (end <= position)
            return;
        std::array<float*, 2> ch { out[0] + position, out[1] != nullptr ? out[1] + position : nullptr };
        engine.render (ch.data(), channels, end - position);
        position = end;
    };
    for (const auto metadata : midi)
    {
        const auto message = metadata.getMessage();
        renderTo (juce::jlimit (0, numSamples, metadata.samplePosition));
        if (message.isNoteOn())
            engine.noteOn (message.getNoteNumber(), message.getVelocity(), message.getChannel());
        else if (message.isNoteOff())
            engine.noteOff (message.getNoteNumber(), message.getChannel());
        else if (message.isAllNotesOff() || message.isAllSoundOff())
            engine.allNotesOff();
    }
    renderTo (numSamples);
    for (int c = 2; c < buffer.getNumChannels(); ++c)
        buffer.clear (c, 0, numSamples);
    midi.clear();
}

// ---------------------------------------------------------------------------------------
// Message thread
// ---------------------------------------------------------------------------------------

void DrumsProcessor::publishPadConfig()
{
    // Seqlock: odd while writing; the audio thread takes only a stable, even generation.
    configGeneration.fetch_add (1, std::memory_order_acq_rel);
    std::atomic_thread_fence (std::memory_order_release);
    for (int p = 0; p < numPads; ++p)
    {
        const auto i = index (p);
        aNote[i].store (map.padNote[i], std::memory_order_relaxed);
        aLow[i].store (map.zone[i].low, std::memory_order_relaxed);
        aHigh[i].store (map.zone[i].high, std::memory_order_relaxed);
        aRoot[i].store (map.zone[i].root, std::memory_order_relaxed);
        aChoke[i].store (pads[i].chokeGroup, std::memory_order_relaxed);
        aVoice[i].store (pads[i].voiceMode == drums::VoiceMode::monoRetrigger ? 1 : 0, std::memory_order_relaxed);
        aTrigger[i].store (pads[i].trigger == drums::TriggerMode::gate ? 1 : 0, std::memory_order_relaxed);
    }
    aOverlap.store (map.allowOverlap, std::memory_order_relaxed);
    configGeneration.fetch_add (1, std::memory_order_release);
    sendChangeMessage();
}

void DrumsProcessor::setKeyMap (const drums::KeyMap& newMap)
{
    map = newMap;
    publishPadConfig();
}

drums::KeyboardMode DrumsProcessor::keyboardMode() const
{
    return raw (const_cast<juce::AudioProcessorValueTreeState&> (apvts), "keyboardMode") > 0.5f ? drums::KeyboardMode::expanded
                                                                                                 : drums::KeyboardMode::pad;
}

void DrumsProcessor::setKeyboardMode (drums::KeyboardMode mode)
{
    if (auto* p = apvts.getParameter ("keyboardMode"))
        p->setValueNotifyingHost (mode == drums::KeyboardMode::expanded ? 1.0f : 0.0f);
}

juce::String DrumsProcessor::padDisplayName (int pad) const
{
    const auto& info = pads[index (pad)];
    if (! info.name.empty())
        return juce::String::fromUTF8 (info.name.c_str());
    const auto label = categoryLabel (info.category);
    return label.isNotEmpty() ? label : "Pad " + juce::String (pad + 1);
}

void DrumsProcessor::setPadName (int pad, const juce::String& name)
{
    pads[index (pad)].name = name.trim().toStdString();
    sendChangeMessage();
}

void DrumsProcessor::setPadCategory (int pad, drums::DrumCategory category, bool applyCategoryDefaults)
{
    auto& info = pads[index (pad)];
    info.category = category;
    if (applyCategoryDefaults)
    {
        const auto d = drums::PadSettings::defaultsFor (category);
        info.chokeGroup = d.chokeGroup;
        info.voiceMode = d.voiceMode;
        info.trigger = d.trigger;
    }
    publishPadConfig();
}

void DrumsProcessor::setPadChoke (int pad, int group)
{
    pads[index (pad)].chokeGroup = juce::jlimit (0, 8, group);
    publishPadConfig();
}

void DrumsProcessor::setPadVoiceMode (int pad, drums::VoiceMode mode)
{
    pads[index (pad)].voiceMode = mode;
    publishPadConfig();
}

void DrumsProcessor::setPadTrigger (int pad, drums::TriggerMode mode)
{
    pads[index (pad)].trigger = mode;
    publishPadConfig();
}

void DrumsProcessor::auditionPad (int pad, int velocity)
{
    const auto scope = auditionFifo.write (1);
    if (scope.blockSize1 > 0)
        auditions[static_cast<std::size_t> (scope.startIndex1)] = Audition { pad, juce::jlimit (1, 127, velocity) };
}

void DrumsProcessor::releaseAuditionedPad (int pad)
{
    const auto scope = auditionFifo.write (1);
    if (scope.blockSize1 > 0)
        auditions[static_cast<std::size_t> (scope.startIndex1)] = Audition { pad, 0 };
}

void DrumsProcessor::loadPad (int pad, const juce::File& file)
{
    plugin::LoadRequest request;
    request.file = file;
    startLoad (pad, std::move (request));
}

void DrumsProcessor::startLoad (int pad, plugin::LoadRequest request)
{
    const auto generation = nextGeneration.fetch_add (1);
    latestRequest[index (pad)] = generation;
    pads[index (pad)].loading = true;
    pads[index (pad)].error.clear();
    ++pendingLoads;
    loaderPool.addJob ([this, request = std::move (request), generation, pad] {
        auto result = plugin::loadInstrument (request, store, generation);
        {
            const std::lock_guard<std::mutex> lock (resultsMutex);
            finished.push_back ({ pad, generation, std::move (result) });
        }
        --pendingLoads;
    });
    sendChangeMessage();
}

void DrumsProcessor::clearPad (int pad)
{
    auto& info = pads[index (pad)];
    latestRequest[index (pad)] = nextGeneration.fetch_add (1);   // a load still running is dropped
    info.sound.reset();
    info.loading = false;
    info.error.clear();
    info.missingFilename.clear();
    clearRequest[index (pad)].store (true, std::memory_order_release);
    sendChangeMessage();
}

void DrumsProcessor::publishFinishedLoads()
{
    std::vector<Finished> results;
    {
        const std::lock_guard<std::mutex> lock (resultsMutex);
        results.swap (finished);
    }
    for (auto& f : results)
    {
        const auto i = index (f.pad);
        if (f.generation != latestRequest[i])
            continue;   // superseded by a newer request for this pad
        auto& info = pads[i];
        info.loading = false;
        if (f.result.instrument == nullptr)
        {
            info.error = f.result.error;
            continue;
        }
        info.error.clear();
        info.missingFilename.clear();
        info.sound = f.result.instrument;
        exchange[i].publish (f.result.instrument);
    }
    if (! results.empty())
        sendChangeMessage();
}

void DrumsProcessor::timerCallback()
{
    publishFinishedLoads();
    for (auto& e : exchange)
        e.collectGarbage();
}

bool DrumsProcessor::waitForLoads (int timeoutMs)
{
    const auto end = juce::Time::getMillisecondCounter() + static_cast<juce::uint32> (timeoutMs);
    while (juce::Time::getMillisecondCounter() < end)
    {
        bool idle = false;
        {
            const std::lock_guard<std::mutex> lock (resultsMutex);
            idle = finished.empty() && pendingLoads.load() == 0;
        }
        publishFinishedLoads();
        if (idle)
            return true;
        juce::Thread::sleep (5);
    }
    return false;
}

// ---------------------------------------------------------------------------------------
// State: <AndorDrums schemaVersion product> parameters, pads (sounds by content hash), key
// map, view. Never an OSP session: OSP's root tag is different and is ignored here.
// ---------------------------------------------------------------------------------------

void DrumsProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    juce::XmlElement root (stateTag);
    root.setAttribute ("schemaVersion", stateVersion);
    root.setAttribute ("product", JucePlugin_Name);
    if (auto params = apvts.copyState().createXml())
        root.addChildElement (params.release());

    auto* padsXml = root.createNewChildElement ("Pads");
    for (int p = 0; p < numPads; ++p)
    {
        const auto& info = pads[index (p)];
        auto* x = padsXml->createNewChildElement ("Pad");
        x->setAttribute ("id", p + 1);
        x->setAttribute ("name", juce::String::fromUTF8 (info.name.c_str()));
        x->setAttribute ("category", drums::toString (info.category));
        x->setAttribute ("choke", info.chokeGroup);
        x->setAttribute ("voice", drums::toString (info.voiceMode));
        x->setAttribute ("trigger", drums::toString (info.trigger));
        if (info.sound != nullptr)
        {
            const auto& s = *info.sound;
            auto* sound = x->createNewChildElement ("Sound");
            sound->setAttribute ("hash", juce::String (s.contentHash));
            sound->setAttribute ("filename", juce::String::fromUTF8 (s.filename.c_str()));
            sound->setAttribute ("originalPath", juce::String::fromUTF8 (s.originalPath.c_str()));
            // The exact playback model, so a reopened project sounds identical even if
            // analysis changes later (as OSP does).
            sound->setAttribute ("rootMidi", juce::String (s.analysisRootMidi, 17));
            sound->setAttribute ("rootOrigin", juce::String (s.rootOrigin));
            sound->setAttribute ("startSeconds", juce::String (s.startSeconds, 17));
            sound->setAttribute ("gainDb", juce::String (s.playbackGainDb, 17));
        }
        else if (! info.missingFilename.empty())
        {
            auto* sound = x->createNewChildElement ("Missing");
            sound->setAttribute ("filename", juce::String::fromUTF8 (info.missingFilename.c_str()));
        }
    }

    auto* mapXml = root.createNewChildElement ("KeyMap");
    mapXml->setAttribute ("allowOverlap", map.allowOverlap);
    for (int p = 0; p < numPads; ++p)
    {
        auto* x = mapXml->createNewChildElement ("Pad");
        x->setAttribute ("id", p + 1);
        x->setAttribute ("note", map.padNote[index (p)]);
        x->setAttribute ("low", map.zone[index (p)].low);
        x->setAttribute ("high", map.zone[index (p)].high);
        x->setAttribute ("root", map.zone[index (p)].root);
    }

    auto* view = root.createNewChildElement ("View");
    view->setAttribute ("layout", nineLayout ? "3x3" : "2x4");
    view->setAttribute ("selectedPad", selectedPad + 1);
    copyXmlToBinary (root, destData);
}

void DrumsProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    const auto root = getXmlFromBinary (data, sizeInBytes);
    if (root == nullptr || ! root->hasTagName (stateTag))
        return;   // not an ANDOR Drums session: keep the current kit

    if (auto* params = root->getChildByName (apvts.state.getType()))
        apvts.replaceState (juce::ValueTree::fromXml (*params));

    drums::KeyMap restored = drums::KeyMap::defaults();
    if (auto* mapXml = root->getChildByName ("KeyMap"))
    {
        restored.allowOverlap = mapXml->getBoolAttribute ("allowOverlap", false);
        for (auto* x : mapXml->getChildWithTagNameIterator ("Pad"))
        {
            const int p = x->getIntAttribute ("id") - 1;
            if (p < 0 || p >= numPads)
                continue;
            const auto i = index (p);
            restored.padNote[i] = juce::jlimit (-1, 127, x->getIntAttribute ("note", restored.padNote[i]));
            drums::Zone z { x->getIntAttribute ("low", -1), x->getIntAttribute ("high", -1), x->getIntAttribute ("root", -1) };
            if (! z.enabled())
                z = drums::Zone {};
            else if (z.root >= 0 && ! z.contains (z.root))
                z.root = drums::noNote;
            restored.zone[i] = z;
        }
    }
    map = restored;

    if (auto* padsXml = root->getChildByName ("Pads"))
        for (auto* x : padsXml->getChildWithTagNameIterator ("Pad"))
        {
            const int p = x->getIntAttribute ("id") - 1;
            if (p < 0 || p >= numPads)
                continue;
            auto& info = pads[index (p)];
            info.name = x->getStringAttribute ("name").toStdString();
            info.category = drums::categoryFromString (x->getStringAttribute ("category").toStdString());
            info.chokeGroup = juce::jlimit (0, 8, x->getIntAttribute ("choke", 0));
            info.voiceMode = x->getStringAttribute ("voice") == "mono" ? drums::VoiceMode::monoRetrigger : drums::VoiceMode::poly;
            info.trigger = x->getStringAttribute ("trigger") == "gate" ? drums::TriggerMode::gate : drums::TriggerMode::oneShot;
            info.missingFilename.clear();
            if (auto* sound = x->getChildByName ("Sound"))
            {
                plugin::LoadRequest request;
                request.expectedHash = sound->getStringAttribute ("hash").toStdString();
                request.originalPath = sound->getStringAttribute ("originalPath").toStdString();
                request.filename = sound->getStringAttribute ("filename").toStdString();
                plugin::LoadRequest::SavedPlayback saved;
                saved.rootMidi = sound->getStringAttribute ("rootMidi", "60").getDoubleValue();
                saved.rootOrigin = sound->getStringAttribute ("rootOrigin").toStdString();
                saved.startSeconds = sound->getStringAttribute ("startSeconds", "0").getDoubleValue();
                saved.gainDb = sound->getStringAttribute ("gainDb", "0").getDoubleValue();
                request.savedPlayback = saved;
                info.missingFilename = request.filename;   // until the load succeeds
                startLoad (p, std::move (request));
            }
            else
            {
                if (auto* missing = x->getChildByName ("Missing"))
                    info.missingFilename = missing->getStringAttribute ("filename").toStdString();
                if (info.sound != nullptr)
                    clearPad (p);
            }
        }

    if (auto* view = root->getChildByName ("View"))
    {
        nineLayout = view->getStringAttribute ("layout") == "3x3";
        selectedPad = juce::jlimit (0, numPads - 1, view->getIntAttribute ("selectedPad", 1) - 1);
    }
    publishPadConfig();
}

juce::AudioProcessorEditor* DrumsProcessor::createEditor()
{
    return new DrumsEditor (*this);
}

} // namespace osp::drumsplugin

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new osp::drumsplugin::DrumsProcessor();
}
