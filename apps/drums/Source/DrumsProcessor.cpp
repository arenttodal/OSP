#include "DrumsProcessor.h"

#include "DrumsEditor.h"

#include "core/PitchMath.h"
#include "io/ContentHash.h"

#include <filesystem>
#include <functional>
#include <set>

namespace osp::drumsplugin
{

namespace
{
    std::string bareHash (const std::string& h) { return h.rfind ("sha256:", 0) == 0 ? h.substr (7) : h; }

    const char* const kitIds[] = { "kit.volume", "kit.life", "kit.dynamics", "kit.character", "kit.drive", "kit.motion", "kit.echo", "kit.space", "kit.machine" };

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
    for (int p = 0; p < numPads; ++p)
        factoryPad (p);
    rawKeyboardMode = apvts.getRawParameterValue ("keyboardMode");
    for (std::size_t k = 0; k < rawKit.size(); ++k)
        rawKit[k] = apvts.getRawParameterValue (kitIds[k]);
    for (int p = 0; p < numPads; ++p)
    {
        auto& r = rawPad[index (p)];
        auto get = [this, p] (const char* name) { return apvts.getRawParameterValue (padParamId (p, name)); };
        r = { get ("gain"), get ("pan"), get ("tune"), get ("start"), get ("attack"), get ("decay"), get ("sustain"), get ("release"),
              get ("mute"), get ("solo"), get ("reverse") };
    }
    for (auto& r : retired)
        r.fill (nullptr);
    for (auto& l : lastLayer)
        l.store (-1, std::memory_order_relaxed);
    publishPadConfig();
    startTimerHz (20);
}

void DrumsProcessor::factoryPad (int pad)
{
    // The factory kit's pads: the GM layout's instruments (names, colours and voice modes).
    using C = drums::DrumCategory;
    const std::array<C, numPads> factory { C::kick, C::snare, C::closedHat, C::openHat, C::clap, C::tom, C::rim, C::percussion, C::cymbal };
    auto& info = pads[index (pad)];
    const auto defaults = drums::PadSettings::defaultsFor (factory[index (pad)]);
    info.name.clear();
    info.category = factory[index (pad)];
    info.chokeGroup = defaults.chokeGroup;
    info.voiceMode = defaults.voiceMode;
    info.trigger = defaults.trigger;
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
    // MACHINE (M9, experimental): 0 leaves every sound exactly as it was.
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "kit.machine", 3 }, "Machine",
                                                       NormalisableRange<float> (0.0f, 1.0f, 0.0001f), 0.0f));

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
        // State 2: the pad's own sample start, direction and amplitude envelope.
        const drums::PadSettings d;
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { padParamId (p, "start"), 2 }, "Pad" + n + " Start",
                                                           NormalisableRange<float> (0.0f, 1.0f, 0.0001f), static_cast<float> (d.start)));
        layout.add (std::make_unique<AudioParameterBool> (ParameterID { padParamId (p, "reverse"), 2 }, "Pad" + n + " Reverse", false));
        auto seconds = [] (float lo, float hi, float centre) {
            NormalisableRange<float> r (lo, hi, 0.0001f);
            r.setSkewForCentre (centre);
            return r;
        };
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { padParamId (p, "attack"), 2 }, "Pad" + n + " Attack",
                                                           seconds (0.0f, 2.0f, 0.05f), static_cast<float> (d.adsr.attackSeconds), AudioParameterFloatAttributes().withLabel ("s")));
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { padParamId (p, "decay"), 2 }, "Pad" + n + " Decay",
                                                           seconds (0.0f, 5.0f, 0.4f), static_cast<float> (d.adsr.decaySeconds), AudioParameterFloatAttributes().withLabel ("s")));
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { padParamId (p, "sustain"), 2 }, "Pad" + n + " Sustain",
                                                           NormalisableRange<float> (0.0f, 1.0f, 0.0001f), static_cast<float> (d.adsr.sustainLevel)));
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { padParamId (p, "release"), 2 }, "Pad" + n + " Release",
                                                           seconds (0.005f, 5.0f, 0.3f), static_cast<float> (d.adsr.releaseSeconds), AudioParameterFloatAttributes().withLabel ("s")));
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
    kit.machine = raw (apvts, "kit.machine");
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
        s.start = raw (apvts, padParamId (p, "start"));
        s.reverse = raw (apvts, padParamId (p, "reverse")) > 0.5f;
        s.adsr = { raw (apvts, padParamId (p, "attack")), raw (apvts, padParamId (p, "decay")),
                   raw (apvts, padParamId (p, "sustain")), raw (apvts, padParamId (p, "release")) };
    }
    engine.prepare (sampleRate, std::max (16, samplesPerBlock), kit, settings);
    for (int p = 0; p < numPads; ++p)
    {
        applySound (p, playing[index (p)]);
        pulled[index (p)] = Pulled {};
    }
    kitPulled = false;
    configSeen = ~0u;
    audioMap = map;
    wasPlaying = false;
    hostRate = sampleRate;
    previewNow = {};
    previewInUse.store (nullptr, std::memory_order_release);
}

// ---------------------------------------------------------------------------------------
// Audio thread
// ---------------------------------------------------------------------------------------

void DrumsProcessor::applySound (int pad, const PadSound* sound) noexcept
{
    std::array<const InstrumentModel*, drums::maxLayers> models {};
    drums::LayerPlan plan;
    if (sound != nullptr)
    {
        plan = sound->plan;
        plan.count = std::min ({ plan.count, drums::maxLayers, static_cast<int> (sound->sounds.size()) });
        for (int l = 0; l < plan.count; ++l)
            models[static_cast<std::size_t> (l)] = sound->sounds[static_cast<std::size_t> (l)]->model.get();
    }
    engine.setPadLayers (pad, models, plan);
}

bool DrumsProcessor::stillSounding (int pad, const PadSound& old) const noexcept
{
    // Sounds the current bundle also holds stay alive through it; only the others count.
    const auto* now = playing[index (pad)];
    for (const auto& s : old.sounds)
    {
        bool kept = false;
        if (now != nullptr)
            for (const auto& n : now->sounds)
                kept = kept || n.get() == s.get();
        if (! kept && engine.isModelInUse (s->model.get()))
            return true;
    }
    return false;
}

void DrumsProcessor::takeNewSounds() noexcept
{
    for (int p = 0; p < numPads; ++p)
    {
        const auto i = index (p);
        if (const PadSound* fresh = exchange[i].takePending())
        {
            if (playing[i] != nullptr)
            {
                // Held hits finish on the old sounds; with no room left, the oldest bundle's
                // sounds that are gone from the new one end.
                auto& slots = retired[i];
                auto* free = std::find (slots.begin(), slots.end(), nullptr);
                if (free == slots.end())
                {
                    for (const auto& s : slots.front()->sounds)
                    {
                        bool kept = false;
                        for (const auto& n : fresh->sounds)
                            kept = kept || n.get() == s.get();
                        if (! kept)
                            engine.killVoicesUsing (s->model.get());
                    }
                    std::move (slots.begin() + 1, slots.end(), slots.begin());
                    free = slots.end() - 1;
                }
                *free = playing[i];
            }
            playing[i] = fresh;
            applySound (p, fresh);
        }
        std::uint64_t oldest = playing[i] != nullptr ? playing[i]->generation : std::numeric_limits<std::uint64_t>::max();
        for (auto& r : retired[i])
        {
            if (r != nullptr && ! stillSounding (p, *r))
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
    auto v = [] (const std::atomic<float>* a) { return static_cast<double> (a->load (std::memory_order_relaxed)); };
    engine.setKeyboardMode (v (rawKeyboardMode) > 0.5 ? drums::KeyboardMode::expanded : drums::KeyboardMode::pad);

    std::array<double, 9> kit {};
    for (std::size_t k = 0; k < kit.size(); ++k)
        kit[k] = v (rawKit[k]);
    if (! kitPulled || kit != pulledKit)
    {
        // MACHINE first: the macros below are applied on top of its character.
        if (! kitPulled || kit[8] < pulledKit[8] || kit[8] > pulledKit[8])
            engine.setMachine (kit[8]);
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
        Pulled now = c;
        const auto& r = rawPad[index (p)];
        now.gain = v (r.gain);
        now.pan = v (r.pan);
        now.tune = v (r.tune);
        now.start = v (r.start);
        now.attack = v (r.attack);
        now.decay = v (r.decay);
        now.sustain = v (r.sustain);
        now.release = v (r.release);
        now.mute = v (r.mute) > 0.5;
        now.solo = v (r.solo) > 0.5;
        now.reverse = v (r.reverse) > 0.5;
        if (now == c)
            continue;
        c = now;
        auto s = engine.padSettings (p);
        s.gainDb = now.gain;
        s.pan = now.pan;
        s.tuneSemitones = now.tune;
        s.mute = now.mute;
        s.solo = now.solo;
        s.start = now.start;
        s.reverse = now.reverse;
        s.adsr = { now.attack, now.decay, now.sustain, now.release };
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
        auto audition = [this] (const Audition& a) {
            if (a.velocity <= 0)
            {
                engine.releasePad (a.pad);
                return;
            }
            const auto* sound = playing[index (a.pad)];
            int engineLayer = -1;
            if (a.layer >= 0 && sound != nullptr)
                for (int l = 0; l < sound->plan.count; ++l)
                    if (sound->uiIndex[static_cast<std::size_t> (l)] == a.layer)
                        engineLayer = l;
            if (a.layer >= 0 && engineLayer < 0)
                return;   // a missing layer: nothing to play
            engineLayer >= 0 ? engine.triggerPadLayer (a.pad, engineLayer, a.velocity) : engine.triggerPad (a.pad, a.velocity);
        };
        const auto scope = auditionFifo.read (auditionFifo.getNumReady());
        for (int i = 0; i < scope.blockSize1; ++i)
            audition (auditions[static_cast<std::size_t> (scope.startIndex1 + i)]);
        for (int i = 0; i < scope.blockSize2; ++i)
            audition (auditions[static_cast<std::size_t> (scope.startIndex2 + i)]);
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
        {
            const int note = message.getNoteNumber();
            engine.noteOn (note, message.getVelocity(), message.getChannel());
            heldBits[static_cast<std::size_t> (note >> 5)].fetch_or (1u << (note & 31), std::memory_order_relaxed);
            lastNote.store (note, std::memory_order_relaxed);
            if (learnArmed.load (std::memory_order_relaxed))
            {
                const auto scope = learnFifo.write (1);
                if (scope.blockSize1 > 0)
                    learnNotes[static_cast<std::size_t> (scope.startIndex1)] = note;
            }
        }
        else if (message.isNoteOff())
        {
            const int note = message.getNoteNumber();
            engine.noteOff (note, message.getChannel());
            heldBits[static_cast<std::size_t> (note >> 5)].fetch_and (~(1u << (note & 31)), std::memory_order_relaxed);
        }
        else if (message.isSustainPedalOn())
            engine.setSustainPedal (true);
        else if (message.isSustainPedalOff())
            engine.setSustainPedal (false);
        else if (message.isAllNotesOff() || message.isAllSoundOff())
        {
            engine.setSustainPedal (false);
            engine.allNotesOff();
            for (auto& b : heldBits)
                b.store (0, std::memory_order_relaxed);
        }
    }
    renderTo (numSamples);
    renderPreview (buffer, numSamples);
    {
        // Display meter: the block's peak per side, kept until the editor reads it.
        const float pl = buffer.getMagnitude (0, 0, numSamples);
        const float pr = buffer.getNumChannels() > 1 ? buffer.getMagnitude (1, 0, numSamples) : pl;
        if (pl > outPeakL.load (std::memory_order_relaxed))
            outPeakL.store (pl, std::memory_order_relaxed);
        if (pr > outPeakR.load (std::memory_order_relaxed))
            outPeakR.store (pr, std::memory_order_relaxed);
    }
    for (int p = 0; p < numPads; ++p)
    {
        const auto* sound = playing[index (p)];
        const int l = engine.lastLayerPlayed (p);
        lastLayer[index (p)].store (sound != nullptr && l >= 0 && l < sound->plan.count ? sound->uiIndex[static_cast<std::size_t> (l)] : -1,
                                    std::memory_order_relaxed);
    }
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
    auto next = arrangement();
    next.names[index (pad)] = name.trim().toStdString();
    applyEdit (next, "Rename pad");
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
        auditions[static_cast<std::size_t> (scope.startIndex1)] = Audition { pad, juce::jlimit (1, 127, velocity), -1 };
}

void DrumsProcessor::auditionLayer (int pad, int layer, int velocity)
{
    const auto scope = auditionFifo.write (1);
    if (scope.blockSize1 > 0)
        auditions[static_cast<std::size_t> (scope.startIndex1)] = Audition { pad, juce::jlimit (1, 127, velocity), std::max (0, layer) };
}

void DrumsProcessor::releaseAuditionedPad (int pad)
{
    const auto scope = auditionFifo.write (1);
    if (scope.blockSize1 > 0)
        auditions[static_cast<std::size_t> (scope.startIndex1)] = Audition { pad, 0, -1 };
}

DrumsProcessor::Arrangement DrumsProcessor::arrangement() const
{
    Arrangement a;
    a.map = map;
    a.order = order;
    for (int p = 0; p < numPads; ++p)
        a.names[index (p)] = pads[index (p)].name;
    return a;
}

void DrumsProcessor::setArrangement (const Arrangement& a)
{
    map = a.map;
    order = a.order;
    for (int p = 0; p < numPads; ++p)
        pads[index (p)].name = a.names[index (p)];
    publishPadConfig();
}

namespace
{
    /** One user edit of the arrangement: before and after, nothing else (cheap and exact). */
    class ArrangementAction final : public juce::UndoableAction
    {
    public:
        using Arrangement = DrumsProcessor::Arrangement;
        ArrangementAction (Arrangement b, Arrangement a, std::function<void (const Arrangement&)> applyFn)
            : before (std::move (b)), after (std::move (a)), apply (std::move (applyFn))
        {
        }
        bool perform() override
        {
            apply (after);
            return true;
        }
        bool undo() override
        {
            apply (before);
            return true;
        }
        int getSizeInUnits() override { return 1; }

    private:
        Arrangement before, after;
        std::function<void (const Arrangement&)> apply;
    };
} // namespace

void DrumsProcessor::applyEdit (const Arrangement& next, const juce::String& actionName)
{
    const auto current = arrangement();
    if (next == current)
        return;
    undo.beginNewTransaction (actionName);
    undo.perform (new ArrangementAction (current, next, [this] (const Arrangement& a) { setArrangement (a); }), actionName);
}

int DrumsProcessor::slotOfPad (int pad) const noexcept
{
    for (int s = 0; s < numPads; ++s)
        if (order[index (s)] == pad)
            return s;
    return pad;
}

void DrumsProcessor::swapPadSlots (int slotA, int slotB)
{
    if (slotA == slotB || slotA < 0 || slotB < 0 || slotA >= numPads || slotB >= numPads)
        return;
    auto next = arrangement();
    std::swap (next.order[index (slotA)], next.order[index (slotB)]);
    applyEdit (next, "Swap pads");
}

void DrumsProcessor::setLearnArmed (bool armed)
{
    if (armed)
        takeLearnedNotes();   // nothing stale from before
    learnArmed.store (armed, std::memory_order_relaxed);
}

std::vector<int> DrumsProcessor::takeLearnedNotes()
{
    std::vector<int> notes;
    const auto scope = learnFifo.read (learnFifo.getNumReady());
    for (int i = 0; i < scope.blockSize1; ++i)
        notes.push_back (learnNotes[static_cast<std::size_t> (scope.startIndex1 + i)]);
    for (int i = 0; i < scope.blockSize2; ++i)
        notes.push_back (learnNotes[static_cast<std::size_t> (scope.startIndex2 + i)]);
    return notes;
}

bool DrumsProcessor::isNoteHeld (int note) const noexcept
{
    if (note < 0 || note > 127)
        return false;
    return ((heldBits[static_cast<std::size_t> (note >> 5)].load (std::memory_order_relaxed) >> (note & 31)) & 1u) != 0;
}

void DrumsProcessor::loadPad (int pad, const juce::File& file)
{
    addPadLayers (pad, juce::Array<juce::File> { file }, LayerDrop::replace);
}

void DrumsProcessor::addPadLayers (int pad, const juce::Array<juce::File>& files, LayerDrop how)
{
    std::vector<plugin::LoadRequest> requests;
    for (const auto& f : files)
        if (static_cast<int> (requests.size()) < drums::maxLayers)
        {
            plugin::LoadRequest r;
            r.file = f;
            requests.push_back (std::move (r));
        }
    if (requests.empty())
        return;
    Batch batch;
    batch.how = how;
    startBatch (pad, std::move (batch), std::move (requests));
}

void DrumsProcessor::startBatch (int pad, Batch batch, std::vector<plugin::LoadRequest> requests)
{
    const auto i = index (pad);
    const auto id = nextGeneration.fetch_add (1);
    latestRequest[i] = id;   // a batch still loading for this pad is dropped
    for (auto it = batches.begin(); it != batches.end();)
        it = it->second.pad == pad ? batches.erase (it) : std::next (it);
    batch.pad = pad;
    batch.remaining = static_cast<int> (requests.size());
    batch.loaded.assign (requests.size(), nullptr);
    pads[i].loading = true;
    pads[i].error.clear();
    auto& stored = batches[id] = std::move (batch);
    if (requests.empty())
    {
        finishBatch (id, stored);
        batches.erase (id);
        sendChangeMessage();
        return;
    }
    for (std::size_t k = 0; k < requests.size(); ++k)
    {
        ++pendingLoads;
        loaderPool.addJob ([this, request = std::move (requests[k]), id, pad, slot = static_cast<int> (k)] {
            auto result = plugin::loadInstrument (request, store, id);
            {
                const std::lock_guard<std::mutex> lock (resultsMutex);
                finished.push_back ({ pad, id, slot, std::move (result) });
            }
            --pendingLoads;
        });
    }
    sendChangeMessage();
}

namespace
{
    /** The level each file was recorded at, against the loudest of them (dB, <= 0): the
        loader levels every file, velocity layers want their own dynamics back. */
    void restoreRecordedLevels (PadLayers& layers)
    {
        double loudest = -1.0e9;
        for (const auto& l : layers.layers)
            if (l.sound != nullptr)
                loudest = std::max (loudest, -l.sound->playbackGainDb);
        for (auto& l : layers.layers)
            if (l.sound != nullptr)
                l.params.gainDb = std::clamp (-l.sound->playbackGainDb - loudest, -60.0, 0.0);
    }

    double recordedLevel (const PadLayer& l)
    {
        return l.sound != nullptr ? l.sound->analysis.envelope.maxRmsDbfs : -1.0e9;
    }
} // namespace

void DrumsProcessor::finishBatch (std::uint64_t, Batch& batch)
{
    const int pad = batch.pad;
    auto& info = pads[index (pad)];
    info.loading = false;
    if (batch.recall)
    {
        setPadLayers (pad, batch.result);
        info.error = batch.error;
        return;
    }
    std::vector<std::shared_ptr<const LoadedInstrument>> loaded;
    for (auto& l : batch.loaded)
        if (l != nullptr)
            loaded.push_back (l);
    if (batch.relink)
    {
        // Each found sound fills every missing layer with its content (never another one).
        auto next = info.layers;
        for (const auto& sound : loaded)
            for (auto& l : next.layers)
                if (l.sound == nullptr && l.missing.has_value() && bareHash (l.missing->expectedHash) == bareHash (sound->contentHash))
                {
                    l.sound = sound;
                    l.missing.reset();
                }
        info.error.clear();
        setPadLayers (pad, next);
        return;
    }
    if (batch.targetLayer >= 0 && ! loaded.empty())
    {
        auto next = info.layers;
        if (batch.targetLayer < static_cast<int> (next.layers.size()))
        {
            auto& l = next.layers[static_cast<std::size_t> (batch.targetLayer)];
            l.sound = loaded.front();
            l.missing.reset();
            info.error.clear();
            applyLayerEdit (pad, next, "Relink sample");
        }
        return;
    }
    if (loaded.empty())
    {
        info.error = batch.error.empty() ? std::string ("The file could not be loaded") : batch.error;
        sendChangeMessage();
        return;
    }
    info.error.clear();
    PadLayers next = batch.how == LayerDrop::replace ? PadLayers {} : info.layers;
    for (auto& sound : loaded)
        if (static_cast<int> (next.layers.size()) < drums::maxLayers)
        {
            PadLayer layer;
            layer.sound = std::move (sound);
            next.layers.push_back (std::move (layer));
        }
    const int count = static_cast<int> (next.layers.size());
    drums::LayerPlan ranges;
    switch (batch.how)
    {
        case LayerDrop::replace:
        case LayerDrop::roundRobin:
            next.mode = drums::LayerMode::roundRobin;
            for (auto& l : next.layers)
            {
                l.params.velocityLow = 1;
                l.params.velocityHigh = 127;
            }
            break;
        case LayerDrop::stack:
            next.mode = drums::LayerMode::stack;
            for (auto& l : next.layers)
            {
                l.params.velocityLow = 1;
                l.params.velocityHigh = 127;
            }
            break;
        case LayerDrop::velocity:
        {
            // Softest recording first; each its own equal share of the velocity range, at
            // the level it was recorded at against the loudest.
            next.mode = drums::LayerMode::velocity;
            std::stable_sort (next.layers.begin(), next.layers.end(),
                              [] (const PadLayer& a, const PadLayer& b) { return recordedLevel (a) < recordedLevel (b); });
            drums::spreadVelocityRanges (ranges, 0, count);
            for (int l = 0; l < count; ++l)
            {
                next.layers[static_cast<std::size_t> (l)].params.velocityLow = ranges.layer[static_cast<std::size_t> (l)].velocityLow;
                next.layers[static_cast<std::size_t> (l)].params.velocityHigh = ranges.layer[static_cast<std::size_t> (l)].velocityHigh;
            }
            restoreRecordedLevels (next);
            break;
        }
    }
    applyLayerEdit (pad, next, batch.how == LayerDrop::replace ? (count > 1 ? "Load samples" : "Load sample") : "Add layers");
}

void DrumsProcessor::clearPad (int pad)
{
    auto& info = pads[index (pad)];
    latestRequest[index (pad)] = nextGeneration.fetch_add (1);   // a load still running is dropped
    info.loading = false;
    info.error.clear();
    applyLayerEdit (pad, PadLayers {}, "Clear pad");
    info.missingFilename.clear();
    sendChangeMessage();
}

void DrumsProcessor::setPadLayers (int pad, const PadLayers& layers)
{
    pads[index (pad)].layers = layers;
    auto& selected = selectedLayer[index (pad)];
    selected = juce::jlimit (0, std::max (0, static_cast<int> (layers.layers.size()) - 1), selected);
    publishLayers (pad);
}

void DrumsProcessor::publishLayers (int pad)
{
    auto& info = pads[index (pad)];
    auto bundle = std::make_shared<PadSound>();
    bundle->generation = nextGeneration.fetch_add (1);
    bundle->plan.mode = info.layers.mode;
    bundle->plan.order = info.layers.order;
    info.sound.reset();
    info.missingFilename.clear();
    for (int l = 0; l < static_cast<int> (info.layers.layers.size()); ++l)
    {
        const auto& layer = info.layers.layers[static_cast<std::size_t> (l)];
        if (layer.sound == nullptr)
        {
            if (info.missingFilename.empty())
                info.missingFilename = layer.fileName();
            continue;
        }
        const int n = bundle->plan.count;
        if (n >= drums::maxLayers)
            break;
        if (info.sound == nullptr)
            info.sound = layer.sound;
        bundle->uiIndex[static_cast<std::size_t> (n)] = l;
        bundle->plan.layer[static_cast<std::size_t> (n)] = layer.params;
        bundle->sounds.push_back (layer.sound);
        bundle->plan.count = n + 1;
    }
    exchange[index (pad)].publish (std::move (bundle));
    sendChangeMessage();
}

namespace
{
    /** One undoable change of one pad's layers (before and after; sounds are shared). */
    class LayerAction final : public juce::UndoableAction
    {
    public:
        LayerAction (int padIndex, PadLayers b, PadLayers a, std::function<void (int, const PadLayers&)> applyFn)
            : pad (padIndex), before (std::move (b)), after (std::move (a)), apply (std::move (applyFn))
        {
        }
        bool perform() override
        {
            apply (pad, after);
            return true;
        }
        bool undo() override
        {
            apply (pad, before);
            return true;
        }
        int getSizeInUnits() override { return 1; }

    private:
        int pad;
        PadLayers before, after;
        std::function<void (int, const PadLayers&)> apply;
    };
} // namespace

void DrumsProcessor::applyLayerEdit (int pad, const PadLayers& next, const juce::String& actionName, bool newTransaction)
{
    const auto& current = pads[index (pad)].layers;
    if (next == current)
        return;
    if (newTransaction)
        undo.beginNewTransaction (actionName);
    undo.perform (new LayerAction (pad, current, next, [this] (int p, const PadLayers& l) { setPadLayers (p, l); }), actionName);
}

void DrumsProcessor::setLayerParams (int pad, int layer, const drums::LayerParams& params, bool newTransaction)
{
    auto next = padLayers (pad);
    if (layer < 0 || layer >= static_cast<int> (next.layers.size()))
        return;
    auto p = params;
    p.gainDb = juce::jlimit (-60.0, 12.0, p.gainDb);
    p.start = juce::jlimit (0.0, 1.0, p.start);
    p.end = juce::jlimit (p.start, 1.0, p.end);
    p.velocityLow = juce::jlimit (1, 127, p.velocityLow);
    p.velocityHigh = juce::jlimit (p.velocityLow, 127, p.velocityHigh);
    next.layers[static_cast<std::size_t> (layer)].params = p;
    applyLayerEdit (pad, next, "Edit layer", newTransaction);
}

void DrumsProcessor::setLayerMode (int pad, drums::LayerMode mode)
{
    auto next = padLayers (pad);
    if (next.mode == mode)
        return;
    const int count = static_cast<int> (next.layers.size());
    bool allFull = true;
    for (const auto& l : next.layers)
        allFull = allFull && l.params.velocityLow == 1 && l.params.velocityHigh == 127;
    if (mode == drums::LayerMode::velocity && allFull)
    {
        // Velocity layers need ranges: equal shares in layer order (softest first).
        drums::LayerPlan ranges;
        drums::spreadVelocityRanges (ranges, 0, count);
        for (int l = 0; l < count; ++l)
        {
            next.layers[static_cast<std::size_t> (l)].params.velocityLow = ranges.layer[static_cast<std::size_t> (l)].velocityLow;
            next.layers[static_cast<std::size_t> (l)].params.velocityHigh = ranges.layer[static_cast<std::size_t> (l)].velocityHigh;
        }
    }
    else if (next.mode == drums::LayerMode::velocity)
        for (auto& l : next.layers)   // round robin and stack play every layer at every velocity
        {
            l.params.velocityLow = 1;
            l.params.velocityHigh = 127;
        }
    next.mode = mode;
    applyLayerEdit (pad, next, "Layer mode");
}

void DrumsProcessor::setLayerOrder (int pad, drums::RoundRobinOrder newOrder)
{
    auto next = padLayers (pad);
    next.order = newOrder;
    applyLayerEdit (pad, next, "Round robin order");
}

void DrumsProcessor::renameLayer (int pad, int layer, const juce::String& name)
{
    auto next = padLayers (pad);
    if (layer < 0 || layer >= static_cast<int> (next.layers.size()))
        return;
    auto& l = next.layers[static_cast<std::size_t> (layer)];
    l.name = name.trim().toStdString();
    if (l.name == l.fileName())
        l.name.clear();
    applyLayerEdit (pad, next, "Rename layer");
}

void DrumsProcessor::removeLayer (int pad, int layer)
{
    auto next = padLayers (pad);
    if (layer < 0 || layer >= static_cast<int> (next.layers.size()))
        return;
    next.layers.erase (next.layers.begin() + layer);
    applyLayerEdit (pad, next, "Remove layer");
}

void DrumsProcessor::duplicateLayer (int pad, int layer)
{
    auto next = padLayers (pad);
    if (layer < 0 || layer >= static_cast<int> (next.layers.size()) || static_cast<int> (next.layers.size()) >= drums::maxLayers)
        return;
    next.layers.insert (next.layers.begin() + layer + 1, next.layers[static_cast<std::size_t> (layer)]);
    applyLayerEdit (pad, next, "Duplicate layer");
    selectedLayer[index (pad)] = layer + 1;
}

void DrumsProcessor::moveLayer (int pad, int from, int to)
{
    auto next = padLayers (pad);
    const int n = static_cast<int> (next.layers.size());
    if (from < 0 || from >= n || to < 0 || to >= n || from == to)
        return;
    auto moved = next.layers[static_cast<std::size_t> (from)];
    next.layers.erase (next.layers.begin() + from);
    next.layers.insert (next.layers.begin() + to, std::move (moved));
    applyLayerEdit (pad, next, "Reorder layers");
    selectedLayer[index (pad)] = to;
}

// ---------------------------------------------------------------------------------------
// Pads: duplicate
// ---------------------------------------------------------------------------------------

namespace
{
    /** Any undoable step given as two functions. */
    class FunctionAction final : public juce::UndoableAction
    {
    public:
        FunctionAction (std::function<void()> doFn, std::function<void()> undoFn) : doIt (std::move (doFn)), undoIt (std::move (undoFn)) {}
        bool perform() override
        {
            doIt();
            return true;
        }
        bool undo() override
        {
            undoIt();
            return true;
        }
        int getSizeInUnits() override { return 10; }

    private:
        std::function<void()> doIt, undoIt;
    };

    // Per-pad parameters a duplicate copies (SOLO stays: soloing a copy is never wanted).
    const char* const copiedPadParams[] = { "gain", "pan", "tune", "mute", "start", "reverse", "attack", "decay", "sustain", "release" };
} // namespace

int DrumsProcessor::firstEmptyPad() const
{
    for (int slot = 0; slot < numPads; ++slot)
    {
        const int p = order[index (slot)];
        if (pads[index (p)].layers.layers.empty() && ! pads[index (p)].loading && (slot < visiblePads() || slot == numPads - 1))
            return p;
    }
    return -1;
}

bool DrumsProcessor::duplicatePad (int from, int to)
{
    if (from == to || from < 0 || to < 0 || from >= numPads || to >= numPads || pads[index (from)].layers.layers.empty())
        return false;
    struct Snapshot
    {
        PadInfo info;
        std::array<float, std::size (copiedPadParams)> params {};
    };
    auto take = [this] (int p) {
        Snapshot snap;
        snap.info = pads[index (p)];
        for (std::size_t k = 0; k < std::size (copiedPadParams); ++k)
            if (auto* param = apvts.getParameter (padParamId (p, copiedPadParams[k])))
                snap.params[k] = param->getValue();
        return snap;
    };
    auto put = [this] (int p, const Snapshot& snap) {
        auto& info = pads[index (p)];
        info.name = snap.info.name;
        info.category = snap.info.category;
        info.chokeGroup = snap.info.chokeGroup;
        info.voiceMode = snap.info.voiceMode;
        info.trigger = snap.info.trigger;
        info.error.clear();
        latestRequest[index (p)] = nextGeneration.fetch_add (1);   // a load still running is dropped
        info.loading = false;
        for (std::size_t k = 0; k < std::size (copiedPadParams); ++k)
            if (auto* param = apvts.getParameter (padParamId (p, copiedPadParams[k])))
                param->setValueNotifyingHost (snap.params[k]);
        selectedLayer[index (p)] = 0;
        setPadLayers (p, snap.info.layers);
        publishPadConfig();
    };
    const auto before = take (to);
    auto after = take (from);
    if (! after.info.name.empty())
        after.info.name += " copy";
    undo.beginNewTransaction ("Duplicate pad");
    undo.perform (new FunctionAction ([put, to, after] { put (to, after); }, [put, to, before] { put (to, before); }), "Duplicate pad");
    return true;
}

// ---------------------------------------------------------------------------------------
// Kits
// ---------------------------------------------------------------------------------------

void DrumsProcessor::resetKit()
{
    for (auto* param : getParameters())
        if (auto* p = dynamic_cast<juce::RangedAudioParameter*> (param))
            p->setValueNotifyingHost (p->getDefaultValue());
    for (int p = 0; p < numPads; ++p)
    {
        latestRequest[index (p)] = nextGeneration.fetch_add (1);
        factoryPad (p);
        pads[index (p)].loading = false;
        pads[index (p)].error.clear();
        selectedLayer[index (p)] = 0;
        setPadLayers (p, PadLayers {});
    }
    map = drums::KeyMap::defaults();
    order = { 0, 1, 2, 3, 4, 5, 6, 7, 8 };
    kitName = "New Kit";
    selectedPad = 0;
    kitFile = juce::File();
    publishPadConfig();
}

void DrumsProcessor::newKit()
{
    std::shared_ptr<juce::XmlElement> before (createStateXml (stateTag).release());
    undo.beginNewTransaction ("New kit");
    undo.perform (new FunctionAction ([this] { resetKit(); }, [this, before] { restoreStateXml (*before); }), "New kit");
    sendChangeMessage();
}

bool DrumsProcessor::saveKit (const juce::File& file, juce::String& error)
{
    auto xml = createStateXml (kitTag);
    xml->setAttribute ("kitSchemaVersion", kitSchemaVersion);
    xml->setAttribute ("name", juce::String::fromUTF8 (kitName.c_str()));
    xml->setAttribute ("saved", juce::Time::getCurrentTime().toISO8601 (true));
    const auto target = file.hasFileExtension (kitExtension) ? file : file.withFileExtension (kitExtension);
    if (! xml->writeTo (target))
    {
        error = "Could not write " + target.getFullPathName();
        return false;
    }
    kitFile = target;
    return true;
}

bool DrumsProcessor::loadKit (const juce::File& file, juce::String& error)
{
    const auto xml = juce::XmlDocument::parse (file);
    if (xml == nullptr || ! xml->hasTagName (kitTag))
    {
        // Not a drum kit (an OSP preset or session is never read as one: spec 10.5).
        error = file.getFileName() + " is not an ANDOR Drums kit";
        return false;
    }
    if (xml->getIntAttribute ("kitSchemaVersion", 0) > kitSchemaVersion)
    {
        error = file.getFileName() + " was saved by a newer ANDOR Drums";
        return false;
    }
    std::shared_ptr<juce::XmlElement> before (createStateXml (stateTag).release());
    std::shared_ptr<juce::XmlElement> after (new juce::XmlElement (*xml));
    const auto base = file.getParentDirectory();
    undo.beginNewTransaction ("Open kit");
    undo.perform (new FunctionAction ([this, after, base] { restoreStateXml (*after, base); },
                                      [this, before] { restoreStateXml (*before); }), "Open kit");
    kitFile = file;
    sendChangeMessage();
    return true;
}

juce::File DrumsProcessor::exportPortableKit (const juce::File& parentDirectory, juce::String& error)
{
    auto name = juce::File::createLegalFileName (juce::String::fromUTF8 (kitName.c_str())).trim();
    if (name.isEmpty())
        name = "Kit";
    const auto folder = parentDirectory.getNonexistentChildFile (name, "", false);
    const auto samples = folder.getChildFile ("Samples");
    if (! samples.createDirectory())
    {
        error = "Could not create " + samples.getFullPathName();
        return {};
    }
    // One copy per sound (by content); same names for different sounds get the hash's start.
    std::map<std::string, juce::String> copied;   // content hash -> relative path
    std::set<juce::String> usedNames;
    juce::StringArray failed;
    auto soundPath = [&] (const LoadedInstrument& sound) -> juce::String {
        if (auto it = copied.find (sound.contentHash); it != copied.end())
            return it->second;
        juce::String fileName = juce::String::fromUTF8 (sound.filename.c_str());
        if (fileName.isEmpty() || usedNames.count (fileName.toLowerCase()) > 0)
            fileName = juce::String (sound.contentHash).fromFirstOccurrenceOf (":", false, false).substring (0, 8) + "-" + fileName;
        usedNames.insert (fileName.toLowerCase());
        juce::File source (juce::File::isAbsolutePath (sound.storedPath) ? juce::String::fromUTF8 (sound.storedPath.c_str()) : juce::String());
        if (! source.existsAsFile() && juce::File::isAbsolutePath (sound.originalPath))
            source = juce::File (juce::String::fromUTF8 (sound.originalPath.c_str()));
        if (! source.existsAsFile() || ! source.copyFileTo (samples.getChildFile (fileName)))
            failed.add (juce::String::fromUTF8 (sound.filename.c_str()));
        const auto relative = "Samples/" + fileName;
        copied[sound.contentHash] = relative;
        return relative;
    };
    auto xml = createStateXml (kitTag, soundPath);
    xml->setAttribute ("kitSchemaVersion", kitSchemaVersion);
    xml->setAttribute ("name", juce::String::fromUTF8 (kitName.c_str()));
    xml->setAttribute ("portable", true);
    xml->setAttribute ("saved", juce::Time::getCurrentTime().toISO8601 (true));
    const auto kit = folder.getChildFile (name + kitExtension);
    if (! xml->writeTo (kit))
    {
        error = "Could not write " + kit.getFullPathName();
        return {};
    }
    const int missing = missingLayerCount();
    if (! failed.isEmpty() || missing > 0)
        error = (failed.isEmpty() ? juce::String() : "Could not copy: " + failed.joinIntoString (", ") + ". ")
                + (missing > 0 ? juce::String (missing) + " missing sample(s) were not included." : juce::String());
    return kit;
}

// ---------------------------------------------------------------------------------------
// Missing samples
// ---------------------------------------------------------------------------------------

int DrumsProcessor::missingLayerCount() const
{
    int n = 0;
    for (const auto& info : pads)
        for (const auto& l : info.layers.layers)
            n += l.sound == nullptr ? 1 : 0;
    return n;
}

void DrumsProcessor::relinkLayer (int pad, int layer, const juce::File& file)
{
    if (layer < 0 || layer >= static_cast<int> (padLayers (pad).layers.size()))
        return;
    plugin::LoadRequest r;
    r.file = file;
    Batch batch;
    batch.targetLayer = layer;
    startBatch (pad, std::move (batch), { r });
}

void DrumsProcessor::findMissingSamples (const juce::File& folder)
{
    struct Want
    {
        int pad;
        std::string hash, filename;
    };
    std::vector<Want> wants;
    for (int p = 0; p < numPads; ++p)
        for (const auto& l : pads[index (p)].layers.layers)
            if (l.sound == nullptr && l.missing.has_value() && ! l.missing->expectedHash.empty())
                wants.push_back ({ p, bareHash (l.missing->expectedHash), l.missing->filename });
    relinkReport = wants.empty() ? "No missing samples to look for." : "Searching " + folder.getFullPathName() + "...";
    if (wants.empty())
    {
        sendChangeMessage();
        return;
    }
    ++pendingLoads;
    loaderPool.addJob ([this, folder, wants] {
        // Same name first (cheap); then, for what is still missing, every audio file
        // (renamed files). Only an exact content match counts.
        std::vector<juce::File> audio;
        for (const auto& entry : juce::RangedDirectoryIterator (folder, true, "*.wav;*.aif;*.aiff;*.flac;*.caf", juce::File::findFiles))
            if (audio.size() < 20000)
                audio.push_back (entry.getFile());
        std::map<std::string, std::string> nameOf;   // wanted hash -> its file name
        for (const auto& w : wants)
            nameOf.emplace (w.hash, w.filename);
        std::map<std::string, juce::File> byHash;   // wanted hash -> the file with that content
        int hashed = 0;
        for (const bool byName : { true, false })
            for (const auto& [hash, filename] : nameOf)
            {
                if (byHash.count (hash) > 0)
                    continue;
                for (const auto& f : audio)
                {
                    if (byName != (f.getFileName() == juce::String::fromUTF8 (filename.c_str())))
                        continue;
                    if (! byName && hashed > 3000)
                        break;   // a huge folder: beyond this, names only
                    ++hashed;
                    if (io::sha256OfFile (std::filesystem::path (f.getFullPathName().toStdString())).value_or (std::string()) == hash)
                    {
                        byHash[hash] = f;
                        break;
                    }
                }
            }
        std::vector<Found> resolved;
        for (const auto& w : wants)
            if (auto it = byHash.find (w.hash); it != byHash.end())
                resolved.push_back ({ w.pad, "sha256:" + w.hash, it->second });
        {
            const std::lock_guard<std::mutex> lock (resultsMutex);
            for (auto& r : resolved)
                found.push_back (r);
            foundReport = "Found " + juce::String (static_cast<int> (byHash.size())) + " of " + juce::String (static_cast<int> (nameOf.size())) + " missing samples.";
        }
        --pendingLoads;
    });
}

// ---------------------------------------------------------------------------------------
// Break slicing
// ---------------------------------------------------------------------------------------

void DrumsProcessor::importBreak (const juce::File& file)
{
    if (importState.open())
        closeImport();   // one plan at a time
    breakState = {};
    breakState.file = file;
    breakState.loading = true;
    breakRequest = nextGeneration.fetch_add (1);
    const auto request = breakRequest;
    const auto settings = breakState.settings;
    ++pendingLoads;
    loaderPool.addJob ([this, file, request, settings] {
        plugin::LoadRequest r;
        r.file = file;
        BreakLoaded loaded;
        loaded.request = request;
        loaded.result = plugin::loadInstrument (r, store, request);
        if (loaded.result.audio != nullptr)
        {
            loaded.slices = drums::sliceBreak (*loaded.result.audio, settings);
            classifySlices (loaded.slices, *loaded.result.audio);
        }
        {
            const std::lock_guard<std::mutex> lock (resultsMutex);
            breakLoaded.push_back (std::move (loaded));
        }
        --pendingLoads;
    });
    sendChangeMessage();
}

void DrumsProcessor::closeBreak()
{
    breakRequest = nextGeneration.fetch_add (1);   // a load still running is dropped
    breakState = {};
    sendChangeMessage();
}

void DrumsProcessor::setBreakSlices (const std::vector<drums::BreakSlice>& slices)
{
    breakState.slices = slices;
    if (breakState.audio != nullptr)
        breakState.bpm = drums::estimateTempo (slices, breakState.audio->sampleRate);
    sendChangeMessage();
}

void DrumsProcessor::editBreakSlices (const std::vector<drums::BreakSlice>& slices, const juce::String& actionName, bool newTransaction)
{
    if (slices == breakState.slices)
        return;
    const auto before = breakState.slices;
    const auto request = breakRequest;
    if (newTransaction)
        undo.beginNewTransaction (actionName);
    // Only while the same break is open (a closed or replaced session ignores old steps).
    undo.perform (new FunctionAction ([this, slices, request] { if (request == breakRequest) setBreakSlices (slices); },
                                      [this, before, request] { if (request == breakRequest) setBreakSlices (before); }),
                  actionName);
}

void DrumsProcessor::resliceBreak (const drums::SliceSettings& settings)
{
    if (breakState.audio == nullptr)
        return;
    breakState.settings = settings;
    auto next = drums::sliceBreak (*breakState.audio, settings);
    classifySlices (next, *breakState.audio);
    // Keep the plan of slices that survive (the same onset within 10 ms).
    const auto tolerance = static_cast<std::int64_t> (0.01 * breakState.audio->sampleRate);
    for (auto& n : next)
        for (const auto& old : breakState.slices)
            if (std::abs (old.onset - n.onset) <= tolerance)
            {
                n.pad = old.pad;
                n.category = old.category;
                n.confidence = old.confidence;
            }
    editBreakSlices (next, "Slice settings");
}

void DrumsProcessor::classifySlices (std::vector<drums::BreakSlice>& slices, const AudioData& audio)
{
    for (auto& sl : slices)
    {
        const auto g = drums::classifyFeatures (drums::drumFeatures (audio, sl.start, sl.end));
        sl.category = g.confidence >= 0.45 ? g.category : drums::DrumCategory::unknown;
        sl.confidence = g.confidence;
        sl.layered = g.layered;
    }
}

void DrumsProcessor::suggestBreakPlan (bool forNewKit)
{
    if (breakState.audio == nullptr)
        return;
    // Alike slices of one category become one pad's layers; each group goes to a pad of its
    // category (the kit's, or the factory's for a new kit), then to free pads; the rest stay
    // in the tray.
    std::vector<drums::GroupItem> items;
    for (const auto& sl : breakState.slices)
    {
        drums::GroupItem it;
        it.name = "break slice";
        it.features = drums::drumFeatures (*breakState.audio, sl.start, sl.end);
        it.guess.category = sl.category;
        it.guess.confidence = sl.confidence;
        items.push_back (it);
    }
    const auto groups = drums::groupItems (items);
    std::array<drums::DrumCategory, drums::maxPads> categories {};
    std::vector<int> freePads, allPads;
    for (int slot = 0; slot < numPads; ++slot)
    {
        const int p = order[index (slot)];
        allPads.push_back (p);
        if (forNewKit || pads[index (p)].layers.layers.empty())
            freePads.push_back (p);
    }
    for (int p = 0; p < numPads; ++p)
        categories[index (p)] = pads[index (p)].category;
    if (forNewKit)
    {
        using C = drums::DrumCategory;
        const std::array<C, numPads> factory { C::kick, C::snare, C::closedHat, C::openHat, C::clap, C::tom, C::rim, C::percussion, C::cymbal };
        categories = factory;
    }
    const auto target = drums::mapGroupsToPads (groups, items, categories, freePads.empty() ? allPads : freePads);
    for (auto& sl : breakState.slices)
        sl.pad = -1;
    for (std::size_t g = 0; g < groups.size(); ++g)
        for (int i : groups[g])
            breakState.slices[static_cast<std::size_t> (i)].pad = target[g];
    sendChangeMessage();
}

void DrumsProcessor::auditionSlice (int i)
{
    if (breakState.audio == nullptr || i < 0 || i >= static_cast<int> (breakState.slices.size()))
        return;
    const auto& s = breakState.slices[static_cast<std::size_t> (i)];
    if (std::find (previewKeep.begin(), previewKeep.end(), breakState.audio) == previewKeep.end())
        previewKeep.push_back (breakState.audio);
    const auto scope = previewFifo.write (1);
    if (scope.blockSize1 > 0)
        previews[static_cast<std::size_t> (scope.startIndex1)] = Preview { breakState.audio.get(), s.start, s.end };
}

void DrumsProcessor::renderPreview (juce::AudioBuffer<float>& buffer, int numSamples) noexcept
{
    {
        const auto scope = previewFifo.read (previewFifo.getNumReady());
        auto take = [this] (const Preview& p) {
            previewNow = p;
            previewPosition = static_cast<double> (p.start);
            previewFade = 0;
        };
        for (int i = 0; i < scope.blockSize1; ++i)
            take (previews[static_cast<std::size_t> (scope.startIndex1 + i)]);
        for (int i = 0; i < scope.blockSize2; ++i)
            take (previews[static_cast<std::size_t> (scope.startIndex2 + i)]);
    }
    previewInUse.store (previewNow.audio, std::memory_order_release);
    const auto* audio = previewNow.audio;
    if (audio == nullptr || audio->channels.empty() || hostRate <= 0.0)
        return;
    // Linear interpolation at the break's own speed, a 2 ms fade in and out, -6 dB.
    const double step = audio->sampleRate / hostRate;
    const int fade = std::max (1, static_cast<int> (0.002 * hostRate));
    const auto end = std::min<std::int64_t> (previewNow.end, audio->numFrames() - 1);
    for (int i = 0; i < numSamples; ++i)
    {
        if (previewPosition >= static_cast<double> (end))
        {
            previewNow = {};
            previewInUse.store (nullptr, std::memory_order_release);
            return;
        }
        const auto k = static_cast<std::size_t> (previewPosition);
        const auto frac = static_cast<float> (previewPosition - static_cast<double> (k));
        const double left = (static_cast<double> (end) - previewPosition) / step;
        const float gain = 0.5f * std::min ({ 1.0f, static_cast<float> (++previewFade) / static_cast<float> (fade), static_cast<float> (left / fade) });
        for (int c = 0; c < std::min (2, buffer.getNumChannels()); ++c)
        {
            const auto& ch = audio->channels[static_cast<std::size_t> (std::min (c, audio->numChannels() - 1))];
            const float v = ch[k] + (ch[std::min (k + 1, ch.size() - 1)] - ch[k]) * frac;
            buffer.addSample (c, i, v * gain);
        }
        previewPosition += step;
    }
}

PadLayer DrumsProcessor::sliceLayer (const drums::BreakSlice& s, int number) const
{
    PadLayer layer;
    layer.sound = breakState.sound;
    layer.name = "Slice " + std::to_string (number);
    if (breakState.sound != nullptr && breakState.audio != nullptr)
    {
        // The voice reads START/END as fractions of what follows the sound's analysed start.
        const double rate = breakState.audio->sampleRate;
        const double startFrame = breakState.sound->startSeconds * rate;
        const double span = std::max (1.0, static_cast<double> (breakState.audio->numFrames()) - startFrame);
        layer.params.start = std::clamp ((static_cast<double> (s.start) - startFrame) / span, 0.0, 1.0);
        layer.params.end = std::clamp ((static_cast<double> (s.end) - startFrame) / span, layer.params.start, 1.0);
    }
    return layer;
}

void DrumsProcessor::applyBreakNow (BreakApply how, juce::String& report)
{
    if (how == BreakApply::newKit)
    {
        const auto keepName = juce::File (juce::String::fromUTF8 (breakState.sound->filename.c_str())).getFileNameWithoutExtension();
        resetKit();
        kitName = keepName.toStdString();
    }
    std::array<std::vector<int>, numPads> perPad;
    for (int i = 0; i < static_cast<int> (breakState.slices.size()); ++i)
    {
        const int pad = breakState.slices[static_cast<std::size_t> (i)].pad;
        if (pad >= 0 && pad < numPads)
            perPad[index (pad)].push_back (i);
    }
    int placed = 0, skipped = 0;
    for (int p = 0; p < numPads; ++p)
    {
        const auto& list = perPad[index (p)];
        if (list.empty())
            continue;
        auto& info = pads[index (p)];
        const bool empty = info.layers.layers.empty();
        if (how == BreakApply::emptyPads && ! empty)
        {
            skipped += static_cast<int> (list.size());
            continue;
        }
        auto next = empty ? PadLayers {} : info.layers;
        next.mode = drums::LayerMode::roundRobin;
        // Hits of very different strength (ghost notes and accents) become velocity layers,
        // softest first, so a ghost note stays a ghost note; alike hits are round robins.
        auto ordered = list;
        double weakest = 1.0, strongest = 0.0;
        for (int i : list)
        {
            weakest = std::min (weakest, breakState.slices[static_cast<std::size_t> (i)].strength);
            strongest = std::max (strongest, breakState.slices[static_cast<std::size_t> (i)].strength);
        }
        const bool dynamic = empty && list.size() > 1 && strongest - weakest >= 0.25;
        if (dynamic)
            std::stable_sort (ordered.begin(), ordered.end(), [this] (int a, int b) {
                return breakState.slices[static_cast<std::size_t> (a)].strength < breakState.slices[static_cast<std::size_t> (b)].strength;
            });
        for (int i : ordered)
            if (static_cast<int> (next.layers.size()) < drums::maxLayers)
            {
                next.layers.push_back (sliceLayer (breakState.slices[static_cast<std::size_t> (i)], i + 1));
                ++placed;
            }
        if (dynamic)
        {
            next.mode = drums::LayerMode::velocity;
            drums::LayerPlan ranges;
            drums::spreadVelocityRanges (ranges, 0, static_cast<int> (next.layers.size()));
            for (std::size_t l = 0; l < next.layers.size(); ++l)
            {
                next.layers[l].params.velocityLow = ranges.layer[l].velocityLow;
                next.layers[l].params.velocityHigh = ranges.layer[l].velocityHigh;
            }
        }
        // A pad that was empty takes its slices' category when they agree.
        if (empty)
        {
            const auto c = breakState.slices[static_cast<std::size_t> (list.front())].category;
            bool same = c != drums::DrumCategory::unknown;
            for (int i : list)
                same = same && breakState.slices[static_cast<std::size_t> (i)].category == c;
            if (same)
            {
                info.category = c;
                const auto d = drums::PadSettings::defaultsFor (c);
                info.chokeGroup = d.chokeGroup;
                info.voiceMode = d.voiceMode;
                info.trigger = d.trigger;
            }
        }
        latestRequest[index (p)] = nextGeneration.fetch_add (1);
        info.loading = false;
        info.error.clear();
        selectedLayer[index (p)] = 0;
        setPadLayers (p, next);
    }
    publishPadConfig();
    // The groove: every hit's original timing and its pad (spec 7.13).
    groove = sessionGroove();
    report = juce::String (placed) + " slice" + (placed == 1 ? "" : "s") + " placed";
    if (skipped > 0)
        report << ", " << skipped << " left in the tray (their pads are not empty)";
    report << ".";
}

juce::String DrumsProcessor::applyBreak (BreakApply how)
{
    if (breakState.sound == nullptr || breakState.audio == nullptr)
        return "No break to apply.";
    auto report = std::make_shared<juce::String>();
    std::shared_ptr<juce::XmlElement> before (createStateXml (stateTag).release());
    auto after = std::make_shared<std::shared_ptr<juce::XmlElement>>();
    const auto grooveBefore = groove;
    auto grooveAfter = std::make_shared<Groove>();
    undo.beginNewTransaction ("Apply break");
    undo.perform (new FunctionAction (
                      [this, how, after, grooveAfter, report] {
                          if (*after == nullptr)
                          {
                              applyBreakNow (how, *report);
                              *after = std::shared_ptr<juce::XmlElement> (createStateXml (stateTag).release());
                              *grooveAfter = groove;
                          }
                          else
                          {
                              restoreStateXml (**after);
                              groove = *grooveAfter;
                          }
                      },
                      [this, before, grooveBefore] {
                          restoreStateXml (*before);
                          groove = grooveBefore;
                      }),
                  "Apply break");
    breakRequest = nextGeneration.fetch_add (1);
    breakState = {};
    sendChangeMessage();
    return *report;
}

DrumsProcessor::Groove DrumsProcessor::sessionGroove() const
{
    Groove g;
    if (breakState.sound == nullptr || breakState.audio == nullptr || breakState.slices.empty())
        return g;
    g.filename = breakState.sound->filename;
    g.hash = breakState.sound->contentHash;
    g.bpm = breakState.bpm > 0.0 ? breakState.bpm : 120.0;
    const auto first = breakState.slices.front().onset;
    for (const auto& sl : breakState.slices)
        g.hits.push_back ({ static_cast<double> (sl.onset - first) / breakState.audio->sampleRate, sl.pad, sl.strength });
    return g;
}

bool DrumsProcessor::writeGrooveMidi (const juce::File& file, juce::String& error, const Groove* source) const
{
    const auto& g = source != nullptr ? *source : groove;
    if (g.hits.empty())
    {
        error = "There is no groove yet: slice a break and apply it first.";
        return false;
    }
    constexpr int ticks = 960;
    juce::MidiMessageSequence track;
    track.addEvent (juce::MidiMessage::tempoMetaEvent (juce::roundToInt (60.0e6 / g.bpm)), 0.0);
    track.addEvent (juce::MidiMessage::timeSignatureMetaEvent (4, 4), 0.0);
    track.addEvent (juce::MidiMessage::textMetaEvent (3, juce::String::fromUTF8 (g.filename.c_str())), 0.0);
    const double perSecond = g.bpm / 60.0 * ticks;
    for (const auto& h : g.hits)
    {
        if (h.pad < 0 || h.pad >= numPads || map.padNote[index (h.pad)] < 0)
            continue;
        const int note = map.padNote[index (h.pad)];
        const auto velocity = static_cast<juce::uint8> (juce::jlimit (1, 127, juce::roundToInt (30.0 + 97.0 * h.strength)));
        const double at = std::round (h.seconds * perSecond);
        track.addEvent (juce::MidiMessage::noteOn (10, note, velocity), at);
        track.addEvent (juce::MidiMessage::noteOff (10, note), at + ticks / 8.0);
    }
    track.updateMatchedPairs();
    juce::MidiFile midi;
    midi.setTicksPerQuarterNote (ticks);
    midi.addTrack (track);
    juce::FileOutputStream out (file);
    if (! out.openedOk())
    {
        error = "Could not write " + file.getFullPathName();
        return false;
    }
    out.setPosition (0);
    out.truncate();
    if (! midi.writeTo (out))
    {
        error = "Could not write the MIDI file";
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------------------
// Batch import
// ---------------------------------------------------------------------------------------

void DrumsProcessor::importFiles (const juce::Array<juce::File>& files)
{
    if (breakState.open())
        closeBreak();   // one plan at a time
    importState = {};
    importRequest = nextGeneration.fetch_add (1);
    const auto request = importRequest;
    for (int i = 0; i < files.size(); ++i)
    {
        ImportItem item;
        item.file = files[i];
        importState.items.push_back (item);
        ++importState.remaining;
        ++pendingLoads;
        loaderPool.addJob ([this, file = files[i], request, i] {
            plugin::LoadRequest r;
            r.file = file;
            ImportLoaded loaded;
            loaded.request = request;
            loaded.index = i;
            loaded.result = plugin::loadInstrument (r, store, request);
            if (loaded.result.audio != nullptr)
                loaded.features = drums::drumFeatures (*loaded.result.audio);
            {
                const std::lock_guard<std::mutex> lock (resultsMutex);
                importLoaded.push_back (std::move (loaded));
            }
            --pendingLoads;
        });
    }
    sendChangeMessage();
}

void DrumsProcessor::closeImport()
{
    importRequest = nextGeneration.fetch_add (1);
    importState = {};
    sendChangeMessage();
}

std::vector<DrumsProcessor::ImportGroup> DrumsProcessor::suggestImportGroups() const
{
    std::vector<drums::GroupItem> items;
    std::vector<int> sessionIndex;
    for (int i = 0; i < static_cast<int> (importState.items.size()); ++i)
    {
        const auto& it = importState.items[static_cast<std::size_t> (i)];
        if (it.sound == nullptr || it.duplicateOf >= 0)
            continue;   // listed, not placed
        drums::GroupItem g;
        g.name = it.file.getFileName().toStdString();
        g.features = it.features;
        g.guess = it.guess;
        items.push_back (g);
        sessionIndex.push_back (i);
    }
    const auto groups = drums::groupItems (items);
    std::array<drums::DrumCategory, drums::maxPads> categories {};
    bool kitEmpty = true;
    for (int p = 0; p < numPads; ++p)
    {
        categories[index (p)] = pads[index (p)].category;
        kitEmpty = kitEmpty && pads[index (p)].layers.layers.empty();
    }
    std::vector<int> freePads;
    for (int slot = 0; slot < numPads; ++slot)
    {
        const int p = order[index (slot)];
        if (kitEmpty || pads[index (p)].layers.layers.empty())
            freePads.push_back (p);
    }
    const auto target = drums::mapGroupsToPads (groups, items, categories, freePads);
    std::vector<ImportGroup> out;
    for (std::size_t g = 0; g < groups.size(); ++g)
    {
        ImportGroup group;
        for (int k : groups[g])
            group.items.push_back (sessionIndex[static_cast<std::size_t> (k)]);
        group.category = items[static_cast<std::size_t> (groups[g].front())].guess.category;
        group.pad = target[g];
        out.push_back (group);
    }
    return out;
}

void DrumsProcessor::setImportGroups (const std::vector<ImportGroup>& groups)
{
    importState.groups = groups;
    sendChangeMessage();
}

void DrumsProcessor::editImportGroups (const std::vector<ImportGroup>& groups, const juce::String& actionName)
{
    const auto before = importState.groups;
    const auto request = importRequest;
    undo.beginNewTransaction (actionName);
    undo.perform (new FunctionAction ([this, groups, request] { if (request == importRequest) setImportGroups (groups); },
                                      [this, before, request] { if (request == importRequest) setImportGroups (before); }),
                  actionName);
}

void DrumsProcessor::resetImportSuggestions()
{
    editImportGroups (suggestImportGroups(), "Reset suggestions");
}

void DrumsProcessor::auditionImportItem (int i)
{
    if (i < 0 || i >= static_cast<int> (importState.items.size()))
        return;
    const auto& audio = importState.items[static_cast<std::size_t> (i)].audio;
    if (audio == nullptr)
        return;
    if (std::find (previewKeep.begin(), previewKeep.end(), audio) == previewKeep.end())
        previewKeep.push_back (audio);
    const auto scope = previewFifo.write (1);
    if (scope.blockSize1 > 0)
        previews[static_cast<std::size_t> (scope.startIndex1)] = Preview { audio.get(), 0, std::min<std::int64_t> (audio->numFrames(), static_cast<std::int64_t> (4.0 * audio->sampleRate)) };
}

juce::String DrumsProcessor::applyImport()
{
    std::vector<ImportGroup> placed, kept;
    for (const auto& g : importState.groups)
        (g.pad >= 0 && g.pad < numPads ? placed : kept).push_back (g);
    if (placed.empty())
        return "No group has a pad yet.";
    std::shared_ptr<juce::XmlElement> before (createStateXml (stateTag).release());
    auto after = std::make_shared<std::shared_ptr<juce::XmlElement>>();
    const auto items = importState.items;
    int files = 0;
    for (const auto& g : placed)
        files += static_cast<int> (g.items.size());
    undo.beginNewTransaction ("Import samples");
    undo.perform (new FunctionAction (
                      [this, placed, items, after] {
                          if (*after != nullptr)
                          {
                              restoreStateXml (**after);
                              return;
                          }
                          for (const auto& g : placed)
                          {
                              PadLayers next;
                              for (int i : g.items)
                                  if (items[static_cast<std::size_t> (i)].sound != nullptr && static_cast<int> (next.layers.size()) < drums::maxLayers)
                                  {
                                      PadLayer layer;
                                      layer.sound = items[static_cast<std::size_t> (i)].sound;
                                      next.layers.push_back (layer);
                                  }
                              auto& info = pads[index (g.pad)];
                              if (g.category != drums::DrumCategory::unknown)
                              {
                                  info.category = g.category;
                                  const auto d = drums::PadSettings::defaultsFor (g.category);
                                  info.chokeGroup = d.chokeGroup;
                                  info.voiceMode = d.voiceMode;
                                  info.trigger = d.trigger;
                              }
                              info.name.clear();
                              latestRequest[index (g.pad)] = nextGeneration.fetch_add (1);
                              info.loading = false;
                              info.error.clear();
                              selectedLayer[index (g.pad)] = 0;
                              setPadLayers (g.pad, next);
                          }
                          publishPadConfig();
                          *after = std::shared_ptr<juce::XmlElement> (createStateXml (stateTag).release());
                      },
                      [this, before] { restoreStateXml (*before); }),
                  "Import samples");
    juce::String report = juce::String (files) + " file" + (files == 1 ? "" : "s") + " on " + juce::String (static_cast<int> (placed.size())) + " pad"
                          + (placed.size() == 1 ? "" : "s");
    if (! kept.empty())
    {
        report << "; " << static_cast<int> (kept.size()) << " group" << (kept.size() == 1 ? "" : "s") << " still in the tray";
        importState.groups = kept;   // the rest stays in the preview (never discarded)
    }
    else
        closeImport();
    sendChangeMessage();
    return report + ".";
}

void DrumsProcessor::publishFinishedLoads()
{
    std::vector<Finished> results;
    std::vector<Found> located;
    std::vector<BreakLoaded> breaks;
    std::vector<ImportLoaded> imports;
    juce::String report;
    {
        const std::lock_guard<std::mutex> lock (resultsMutex);
        results.swap (finished);
        located.swap (found);
        breaks.swap (breakLoaded);
        imports.swap (importLoaded);
        report.swapWith (foundReport);
    }
    for (auto& l : imports)
    {
        if (l.request != importRequest || l.index < 0 || l.index >= static_cast<int> (importState.items.size()))
            continue;
        auto& item = importState.items[static_cast<std::size_t> (l.index)];
        item.sound = l.result.instrument;
        item.audio = l.result.audio;
        item.features = l.features;
        item.error = item.sound == nullptr ? (l.result.error.empty() ? std::string ("could not be loaded") : l.result.error) : std::string();
        if (item.sound != nullptr)
        {
            item.guess = drums::classify (item.file.getFileName().toStdString(), item.features);
            previewKeep.push_back (item.audio);
        }
        if (--importState.remaining == 0)
        {
            // The same content twice is listed once more, never placed twice.
            for (std::size_t i = 0; i < importState.items.size(); ++i)
                for (std::size_t j = 0; j < i; ++j)
                    if (importState.items[i].sound != nullptr && importState.items[j].sound != nullptr && importState.items[j].duplicateOf < 0
                        && importState.items[i].sound->contentHash == importState.items[j].sound->contentHash)
                    {
                        importState.items[i].duplicateOf = static_cast<int> (j);
                        break;
                    }
            importState.groups = suggestImportGroups();
        }
        sendChangeMessage();
    }
    for (auto& b : breaks)
        if (b.request == breakRequest)
        {
            breakState.loading = false;
            if (b.result.instrument == nullptr || b.result.audio == nullptr)
            {
                breakState.error = b.result.error.empty() ? std::string ("The break could not be loaded") : b.result.error;
                continue;
            }
            breakState.sound = b.result.instrument;
            breakState.audio = b.result.audio;
            previewKeep.push_back (breakState.audio);
            breakState.slices = std::move (b.slices);
            breakState.bpm = drums::estimateTempo (breakState.slices, breakState.audio->sampleRate);
            suggestBreakPlan();
            sendChangeMessage();
        }
    if (report.isNotEmpty())
    {
        relinkReport = report;
        sendChangeMessage();
    }
    // Missing samples found by content: load them per pad, filling the layers they belong to.
    std::map<int, std::vector<plugin::LoadRequest>> perPad;
    for (const auto& f : located)
    {
        auto& list = perPad[f.pad];
        bool already = false;
        for (const auto& r : list)
            already = already || r.expectedHash == f.hash;
        if (already)
            continue;
        plugin::LoadRequest r;
        r.file = f.file;
        r.expectedHash = f.hash;
        r.originalPath = f.file.getFullPathName().toStdString();
        r.filename = f.file.getFileName().toStdString();
        list.push_back (std::move (r));
    }
    for (auto& [pad, requests] : perPad)
    {
        Batch batch;
        batch.relink = true;
        startBatch (pad, std::move (batch), std::move (requests));
    }
    for (auto& f : results)
    {
        const auto it = batches.find (f.generation);
        if (it == batches.end())
            continue;   // superseded by a newer request for this pad (or the pad was cleared)
        auto& batch = it->second;
        if (f.generation != latestRequest[index (batch.pad)])
        {
            batches.erase (it);
            continue;
        }
        const auto slot = static_cast<std::size_t> (f.slot);
        if (f.result.instrument == nullptr && batch.error.empty())
            batch.error = f.result.error;
        if (batch.recall)
        {
            if (f.result.instrument != nullptr && slot < batch.result.layers.size())
            {
                batch.result.layers[slot].sound = f.result.instrument;
                batch.result.layers[slot].missing.reset();
            }
        }
        else if (slot < batch.loaded.size())
            batch.loaded[slot] = f.result.instrument;
        if (--batch.remaining <= 0)
        {
            finishBatch (f.generation, batch);
            batches.erase (f.generation);
        }
    }
    if (! results.empty())
        sendChangeMessage();
}

void DrumsProcessor::timerCallback()
{
    publishFinishedLoads();
    for (auto& e : exchange)
        e.collectGarbage();
    // Preview audio no longer queued, playing or open is let go.
    if (previewFifo.getNumReady() == 0)
    {
        const auto* inUse = previewInUse.load (std::memory_order_acquire);
        previewKeep.erase (std::remove_if (previewKeep.begin(), previewKeep.end(),
                                           [this, inUse] (const auto& a) {
                                               bool importing = false;
                                               for (const auto& it : importState.items)
                                                   importing = importing || it.audio == a;
                                               return a.get() != inUse && a != breakState.audio && ! importing;
                                           }),
                           previewKeep.end());
    }
}

bool DrumsProcessor::waitForLoads (int timeoutMs)
{
    const auto end = juce::Time::getMillisecondCounter() + static_cast<juce::uint32> (timeoutMs);
    while (juce::Time::getMillisecondCounter() < end)
    {
        bool idle = false;
        {
            const std::lock_guard<std::mutex> lock (resultsMutex);
            idle = finished.empty() && found.empty() && breakLoaded.empty() && importLoaded.empty() && pendingLoads.load() == 0;
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

namespace
{
    /** A sound's identity and its exact playback model, so a reopened project sounds
        identical even if analysis changes later (as OSP does). */
    void writeSound (juce::XmlElement& x, const LoadedInstrument& s)
    {
        x.setAttribute ("hash", juce::String (s.contentHash));
        x.setAttribute ("filename", juce::String::fromUTF8 (s.filename.c_str()));
        x.setAttribute ("originalPath", juce::String::fromUTF8 (s.originalPath.c_str()));
        x.setAttribute ("rootMidi", juce::String (s.analysisRootMidi, 17));
        x.setAttribute ("rootOrigin", juce::String (s.rootOrigin));
        x.setAttribute ("startSeconds", juce::String (s.startSeconds, 17));
        x.setAttribute ("gainDb", juce::String (s.playbackGainDb, 17));
    }

    /** A missing sound: written back exactly as it was read (still findable later). */
    void writeRequest (juce::XmlElement& x, const plugin::LoadRequest& r)
    {
        x.setAttribute ("hash", juce::String (r.expectedHash));
        x.setAttribute ("filename", juce::String::fromUTF8 (r.filename.c_str()));
        x.setAttribute ("originalPath", juce::String::fromUTF8 (r.originalPath.c_str()));
        if (r.savedPlayback.has_value())
        {
            x.setAttribute ("rootMidi", juce::String (r.savedPlayback->rootMidi, 17));
            x.setAttribute ("rootOrigin", juce::String (r.savedPlayback->rootOrigin));
            x.setAttribute ("startSeconds", juce::String (r.savedPlayback->startSeconds, 17));
            x.setAttribute ("gainDb", juce::String (r.savedPlayback->gainDb, 17));
        }
    }

    plugin::LoadRequest readRequest (const juce::XmlElement& x)
    {
        plugin::LoadRequest request;
        request.expectedHash = x.getStringAttribute ("hash").toStdString();
        request.originalPath = x.getStringAttribute ("originalPath").toStdString();
        request.filename = x.getStringAttribute ("filename").toStdString();
        if (x.hasAttribute ("rootMidi"))
        {
            plugin::LoadRequest::SavedPlayback saved;
            saved.rootMidi = x.getStringAttribute ("rootMidi", "60").getDoubleValue();
            saved.rootOrigin = x.getStringAttribute ("rootOrigin").toStdString();
            saved.startSeconds = x.getStringAttribute ("startSeconds", "0").getDoubleValue();
            saved.gainDb = x.getStringAttribute ("gainDb", "0").getDoubleValue();
            request.savedPlayback = saved;
        }
        return request;
    }
} // namespace

void DrumsProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    copyXmlToBinary (*createStateXml (stateTag), destData);
}

std::unique_ptr<juce::XmlElement> DrumsProcessor::createStateXml (const char* tag, std::function<juce::String (const LoadedInstrument&)> soundPath)
{
    auto rootPtr = std::make_unique<juce::XmlElement> (tag);
    auto& root = *rootPtr;
    root.setAttribute ("schemaVersion", stateVersion);
    root.setAttribute ("product", JucePlugin_Name);
    if (auto params = apvts.copyState().createXml())
        root.addChildElement (params.release());
    // A portable kit: each sound also by its path inside the kit; no private absolute paths.
    auto writeLayerSound = [&soundPath] (juce::XmlElement& x, const LoadedInstrument& sound) {
        writeSound (x, sound);
        if (soundPath)
        {
            x.setAttribute ("kitPath", soundPath (sound));
            x.setAttribute ("originalPath", juce::String());
        }
    };

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
        // Layers (state 3). The first loaded layer is also written as <Sound>, so a build
        // from before layers still opens the kit with one sample per pad.
        if (info.sound != nullptr)
            writeLayerSound (*x->createNewChildElement ("Sound"), *info.sound);
        if (! info.layers.layers.empty())
        {
            auto* layersXml = x->createNewChildElement ("Layers");
            layersXml->setAttribute ("mode", drums::toString (info.layers.mode));
            layersXml->setAttribute ("order", drums::toString (info.layers.order));
            for (const auto& layer : info.layers.layers)
            {
                auto* l = layersXml->createNewChildElement ("Layer");
                if (layer.sound != nullptr)
                    writeLayerSound (*l, *layer.sound);
                else if (layer.missing.has_value())
                    writeRequest (*l, *layer.missing);
                l->setAttribute ("name", juce::String::fromUTF8 (layer.name.c_str()));
                l->setAttribute ("level", juce::String (layer.params.gainDb, 17));
                l->setAttribute ("start", juce::String (layer.params.start, 17));
                l->setAttribute ("end", juce::String (layer.params.end, 17));
                l->setAttribute ("velocityLow", layer.params.velocityLow);
                l->setAttribute ("velocityHigh", layer.params.velocityHigh);
            }
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

    juce::StringArray slots;
    for (int slot = 0; slot < numPads; ++slot)
        slots.add (juce::String (order[index (slot)] + 1));
    root.createNewChildElement ("Order")->setAttribute ("slots", slots.joinIntoString (","));

    if (! groove.hits.empty())
    {
        auto* g = root.createNewChildElement ("Groove");
        g->setAttribute ("filename", juce::String::fromUTF8 (groove.filename.c_str()));
        g->setAttribute ("hash", juce::String (groove.hash));
        g->setAttribute ("bpm", groove.bpm);
        for (const auto& h : groove.hits)
        {
            auto* x = g->createNewChildElement ("Hit");
            x->setAttribute ("seconds", juce::String (h.seconds, 6));
            x->setAttribute ("pad", h.pad + 1);
            x->setAttribute ("strength", juce::String (h.strength, 4));
        }
    }

    auto* view = root.createNewChildElement ("View");
    view->setAttribute ("layout", layoutName (padLayout));
    view->setAttribute ("kitName", juce::String::fromUTF8 (kitName.c_str()));
    view->setAttribute ("selectedPad", selectedPad + 1);
    return rootPtr;
}

void DrumsProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    const auto root = getXmlFromBinary (data, sizeInBytes);
    if (root == nullptr || ! root->hasTagName (stateTag))
        return;   // not an ANDOR Drums session: keep the current kit
    restoreStateXml (*root);
    undo.clearUndoHistory();   // a recalled kit starts a fresh history
}

void DrumsProcessor::restoreStateXml (const juce::XmlElement& rootRef, const juce::File& baseDirectory)
{
    const auto* root = &rootRef;
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
            // The pad's layers (state 3), or its one <Sound> (states 1 and 2: one layer).
            Batch batch;
            batch.recall = true;
            std::vector<plugin::LoadRequest> requests;
            auto addLayer = [&] (const juce::XmlElement& l, bool withSettings) {
                if (static_cast<int> (requests.size()) >= drums::maxLayers || l.getStringAttribute ("filename").isEmpty())
                    return;
                PadLayer layer;
                layer.missing = readRequest (l);   // until it loads
                // A portable kit: the copy inside the kit, when the original is not here.
                const auto kitPath = l.getStringAttribute ("kitPath");
                if (kitPath.isNotEmpty() && baseDirectory != juce::File())
                {
                    const auto inKit = baseDirectory.getChildFile (kitPath);
                    const auto& original = layer.missing->originalPath;
                    const bool originalHere = juce::File::isAbsolutePath (original) && juce::File (original).existsAsFile();
                    if (inKit.existsAsFile() && ! originalHere)
                        layer.missing->originalPath = inKit.getFullPathName().toStdString();
                }
                if (! juce::File::isAbsolutePath (layer.missing->originalPath))
                    layer.missing->originalPath.clear();
                if (withSettings)
                {
                    layer.name = l.getStringAttribute ("name").toStdString();
                    drums::LayerParams lp;
                    lp.gainDb = juce::jlimit (-60.0, 12.0, l.getStringAttribute ("level", "0").getDoubleValue());
                    lp.start = juce::jlimit (0.0, 1.0, l.getStringAttribute ("start", "0").getDoubleValue());
                    lp.end = juce::jlimit (lp.start, 1.0, l.getStringAttribute ("end", "1").getDoubleValue());
                    lp.velocityLow = juce::jlimit (1, 127, l.getIntAttribute ("velocityLow", 1));
                    lp.velocityHigh = juce::jlimit (lp.velocityLow, 127, l.getIntAttribute ("velocityHigh", 127));
                    layer.params = lp;
                }
                requests.push_back (*layer.missing);
                batch.result.layers.push_back (std::move (layer));
            };
            if (auto* layersXml = x->getChildByName ("Layers"))
            {
                batch.result.mode = drums::layerModeFromString (layersXml->getStringAttribute ("mode").toStdString());
                batch.result.order = drums::roundRobinOrderFromString (layersXml->getStringAttribute ("order").toStdString());
                for (auto* l : layersXml->getChildWithTagNameIterator ("Layer"))
                    addLayer (*l, true);
            }
            else if (auto* sound = x->getChildByName ("Sound"))
                addLayer (*sound, false);
            else if (auto* missing = x->getChildByName ("Missing"))   // state 2's missing sample: kept by name
            {
                PadLayer layer;
                plugin::LoadRequest r;
                r.filename = missing->getStringAttribute ("filename").toStdString();
                layer.missing = r;
                batch.result.layers.push_back (std::move (layer));
            }
            latestRequest[index (p)] = nextGeneration.fetch_add (1);   // a load still running is dropped
            info.loading = false;
            info.error.clear();
            if (requests.empty())
                setPadLayers (p, batch.result);   // empty, or only a sample known to be missing
            else
            {
                info.missingFilename = requests.front().filename;   // until the load succeeds
                startBatch (p, std::move (batch), std::move (requests));
            }
        }

    order = { 0, 1, 2, 3, 4, 5, 6, 7, 8 };   // state 1 had no order: pads in identity order
    if (auto* orderXml = root->getChildByName ("Order"))
    {
        juce::StringArray slots;
        slots.addTokens (orderXml->getStringAttribute ("slots"), ",", "");
        std::array<int, numPads> restoredOrder {};
        std::array<bool, numPads> seen {};
        bool valid = slots.size() == numPads;
        for (int slot = 0; valid && slot < numPads; ++slot)
        {
            const int p = slots[slot].getIntValue() - 1;
            valid = p >= 0 && p < numPads && ! seen[index (p)];
            if (valid)
            {
                seen[index (p)] = true;
                restoredOrder[index (slot)] = p;
            }
        }
        if (valid)
            order = restoredOrder;
    }

    groove = {};
    if (auto* g = root->getChildByName ("Groove"))
    {
        groove.filename = g->getStringAttribute ("filename").toStdString();
        groove.hash = g->getStringAttribute ("hash").toStdString();
        groove.bpm = juce::jlimit (20.0, 400.0, g->getDoubleAttribute ("bpm", 120.0));
        for (auto* x : g->getChildWithTagNameIterator ("Hit"))
            groove.hits.push_back ({ x->getDoubleAttribute ("seconds"), x->getIntAttribute ("pad", 0) - 1, juce::jlimit (0.0, 1.0, x->getDoubleAttribute ("strength", 1.0)) });
    }

    if (auto* view = root->getChildByName ("View"))
    {
        const auto layout = view->getStringAttribute ("layout");
        padLayout = layout == "3x3" ? PadLayout::grid9 : layout == "2x4" ? PadLayout::grid8 : PadLayout::row8;
        kitName = view->getStringAttribute ("kitName", "New Kit").toStdString();
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
