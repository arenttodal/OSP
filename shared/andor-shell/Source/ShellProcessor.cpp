#include "ShellProcessor.h"
#include "ShellEditor.h"

#include "AndorParameters.h"
#include "Binding.h"

#include <algorithm>
#include <cstdlib>

namespace andor::shell
{

using osp::plugin::InstrumentHost;
using osp::plugin::LoadedInstrument;

namespace
{
    /** CC 1-4's controllers at first, as in ANDOR OSP: 2 (breath), 11 (expression), 16, 17. */
    constexpr std::array<int, osp::mod::ccSlots> defaultControllers { 2, 11, 16, 17 };

    juce::File folderFromEnvironment (const char* variable)
    {
        if (const char* path = std::getenv (variable); path != nullptr && *path != 0)
            return juce::File (juce::String::fromUTF8 (path));
        return {};
    }

    struct CurveAction final : juce::UndoableAction
    {
        CurveAction (ShellProcessor& p, int i, osp::mod::Curve a, osp::mod::Curve b) : processor (p), index (i), from (a), to (b) {}
        bool perform() override { processor.setModulationCurve (index, to, false); return true; }
        bool undo() override { processor.setModulationCurve (index, from, false); return true; }
        ShellProcessor& processor;
        int index;
        osp::mod::Curve from, to;
    };

    struct ShaperAction final : juce::UndoableAction
    {
        ShaperAction (ShellProcessor& p, const osp::ShaperPattern& a, const osp::ShaperPattern& b) : processor (p), from (a), to (b) {}
        bool perform() override { processor.setShaperCustomPattern (to, false); return true; }
        bool undo() override { processor.setShaperCustomPattern (from, false); return true; }
        ShellProcessor& processor;
        osp::ShaperPattern from, to;
    };

    const juce::Identifier sourcesType ("Sources"), layerType ("Layer"), viewType ("View"), curvesType ("Curves"), shaperType ("Shaper");

    /** ANDOR OSP's schema, then the product's own parameters (ProductInfo::addParameters). */
    juce::AudioProcessorValueTreeState::ParameterLayout productParameterLayout()
    {
        // The product's own modulation destinations come after ANDOR OSP's in the routes'
        // destination list, which the layout below is made with.
        osp::mod::setProductDestinations (productInfo().modulationDestinations);
        auto layout = InstrumentHost::createParameterLayout();
        if (productInfo().addParameters != nullptr)
            productInfo().addParameters (layout);
        return layout;
    }

    /** A small, stable fingerprint of a picture (the card repaints when it changes). */
    std::uint64_t fingerprint (const std::vector<float>& values)
    {
        std::uint64_t h = 1469598103934665603ull;
        for (const float v : values)
        {
            h ^= static_cast<std::uint64_t> (juce::roundToInt (v * 4096.0f) + 65536);
            h *= 1099511628211ull;
        }
        return h;
    }
}

//==============================================================================
ShellProcessor::ShellProcessor (std::unique_ptr<Engine> productEngine)
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      parameters (*this, &undoManager, productInfo().stateType, productParameterLayout()),
      engine (std::move (productEngine))
{
    jassert (engine != nullptr);
    description = engine->describe();
    bound = engine->boundParameters();
    generatorName = engine->generatorName();
    arpBound = osp::plugin::binding::anyBound ({ osp::plugin::ids::arpEnabled }, bound);
    arpEnabledParam = parameters.getRawParameterValue (osp::plugin::ids::arpEnabled);
    arpPatternParam = parameters.getRawParameterValue (osp::plugin::ids::arpPattern);
    arpRateParam = parameters.getRawParameterValue (osp::plugin::ids::arpRate);
    arpGateParam = parameters.getRawParameterValue (osp::plugin::ids::arpGate);
    arpOctavesParam = parameters.getRawParameterValue (osp::plugin::ids::arpOctaves);
    arpSwingParam = parameters.getRawParameterValue (osp::plugin::ids::arpSwing);
    arpVelocityParam = parameters.getRawParameterValue (osp::plugin::ids::arpVelocity);
    seedParam = parameters.getRawParameterValue (osp::plugin::ids::seed);
    modBound = osp::plugin::binding::anyBound ({ modRouteId (0, "source") }, bound);
    for (int i = 0; i < 2; ++i)
    {
        int k = 0;
        for (const char* name : { "shape", "rate", "sync", "division", "phase", "polarity", "mode", "scope" })
            modLfoParams[static_cast<std::size_t> (8 * i + k++)] = parameters.getRawParameterValue (modLfoId (i, name));
        k = 0;
        for (const char* name : { "mode", "attack", "decay", "sustain", "release", "curve", "length" })
            modEnvParams[static_cast<std::size_t> (7 * i + k++)] = parameters.getRawParameterValue (modEnvId (i, name));
    }
    for (int r = 0; r < osp::mod::maxRoutes; ++r)
    {
        int k = 0;
        for (const char* name : { "source", "dest", "depth", "enabled" })
            modRouteParams[static_cast<std::size_t> (4 * r + k++)] = parameters.getRawParameterValue (modRouteId (r, name));
        modRouteTargets[static_cast<std::size_t> (r)].store (-1);
    }
    macros = productInfo().macros;
    for (std::size_t i = 0; i < ccNumbers.size(); ++i)
        ccNumbers[i].store (defaultControllers[i]);
    publishShaperPattern();
    for (int i = 0; i < modCurveCount; ++i)
        publishCurve (i);
    store = std::make_unique<osp::plugin::SampleStore> (sampleStoreFolder());
    if (productInfo().installFactoryPresets != nullptr)
        productInfo().installFactoryPresets (*this, presetFolder().getChildFile ("Factory"));
    startTimerHz (20);
}

ShellProcessor::~ShellProcessor()
{
    closing = true;
    stopTimer();
    loaderPool.removeAllJobs (true, 30000);
}

void ShellProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    engine->prepare (sampleRate, samplesPerBlock, parameters);
    // The arpeggiator's MIDI: room for any realistic block, so the audio thread never grows it.
    arpeggiator.prepare (sampleRate);
    arpMidi.ensureSize (static_cast<std::size_t> (osp::Arpeggiator::maxEvents + 4096) * 16);
    shaperGenerationSeen = 0;   // the prepared engine is given the pattern, curves and modulation again
    curveGenerationSeen.fill (0);
    modDirty = true;
    setLatencySamples (engine->latencySamples());
    // A prepared engine plays the sources it is given from now on: hand it the current ones.
    for (int l = 0; l < numLayers; ++l)
    {
        auto& layer = layers[static_cast<std::size_t> (l)];
        if (layer.instrument != nullptr)
            layer.exchange.publish (layer.instrument);
    }
}

void ShellProcessor::releaseResources()
{
    engine->release();
}

bool ShellProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono();
}

void ShellProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    // The sources' hand-over: a newly published one replaces the layer's (the engine stops
    // reading the old one before setSource returns), so older ones may be freed.
    for (int l = 0; l < numLayers; ++l)
    {
        auto& layer = layers[static_cast<std::size_t> (l)];
        if (layer.clearRequested.exchange (false, std::memory_order_acq_rel))
        {
            engine->setSource (l, nullptr);
            layer.playing = nullptr;
        }
        if (const auto* next = layer.exchange.takePending())
        {
            engine->setSource (l, next);
            layer.playing = next;
            layer.exchange.publishOldestInUse (next->generation);
        }
    }

    keyboardState.processNextMidiBuffer (midi, 0, buffer.getNumSamples(), true);
    for (const auto metadata : midi)
    {
        const auto m = metadata.getMessage();
        if (m.isNoteOn())
        {
            const int count = velocityCount.load (std::memory_order_relaxed);
            recentVelocity[static_cast<std::size_t> (count % velocityHistory)].store (m.getVelocity(), std::memory_order_relaxed);
            velocityCount.store (count + 1, std::memory_order_release);
            lastNote.store (m.getNoteNumber(), std::memory_order_relaxed);
        }
        else if (m.isPitchWheel())
            performance.pitchWheel = static_cast<float> (m.getPitchWheelValue() - 8192) / 8192.0f;
        else if (m.isControllerOfType (1))
            performance.modWheel = static_cast<float> (m.getControllerValue()) / 127.0f;
        // CC 1-4's Learn: the next controller that moves is taken for the slot.
        if (m.isController() && m.getControllerNumber() < 120)
            if (const int learn = ccLearn.exchange (-1, std::memory_order_acq_rel); learn >= 0 && learn < osp::mod::ccSlots)
                ccNumbers[static_cast<std::size_t> (learn)].store (m.getControllerNumber(), std::memory_order_relaxed);
    }
    // The on-screen wheels win when they move.
    if (const float p = screenPitch.load (std::memory_order_relaxed); ! juce::exactlyEqual (p, lastScreenPitch))
        performance.pitchWheel = lastScreenPitch = p;
    if (const float m = screenMod.load (std::memory_order_relaxed); ! juce::exactlyEqual (m, lastScreenMod))
        performance.modWheel = lastScreenMod = m;
    wheelShown.store (performance.modWheel, std::memory_order_relaxed);

    // The host's transport (tempo-synced movement and echoes): once per block.
    performance.hasPosition = false;
    performance.playing = false;
    if (auto* head = getPlayHead())
        if (const auto position = head->getPosition())
        {
            performance.playing = position->getIsPlaying();
            if (const auto bpm = position->getBpm(); bpm.hasValue() && *bpm > 1.0)
                performance.bpm = *bpm;
            if (const auto ppq = position->getPpqPosition(); ppq.hasValue())
            {
                performance.ppq = *ppq;
                performance.hasPosition = true;
            }
            if (const auto signature = position->getTimeSignature(); signature.hasValue())
            {
                performance.numerator = signature->numerator;
                performance.denominator = signature->denominator;
            }
        }

    tempo.store (static_cast<float> (performance.bpm), std::memory_order_relaxed);
    // A bounce from the same position plays the same arpeggio: its pattern and RANDOM's
    // choices start over whenever the transport starts.
    if (performance.playing && ! hostWasPlaying)
        arpeggiator.restartPattern();
    hostWasPlaying = performance.playing;

    // SHAPER CUSTOM: a changed pattern reaches the engine before the block.
    if (const auto generation = shaperGeneration.load (std::memory_order_acquire); generation != shaperGenerationSeen)
    {
        shaperGenerationSeen = generation;
        osp::ShaperPattern pattern;
        for (std::size_t i = 0; i < pattern.size(); ++i)
        {
            pattern[i].start = shaperSteps[3 * i].load (std::memory_order_relaxed);
            pattern[i].end = shaperSteps[3 * i + 1].load (std::memory_order_relaxed);
            pattern[i].shape = static_cast<osp::StepShape> (std::clamp (static_cast<int> (shaperSteps[3 * i + 2].load (std::memory_order_relaxed)), 0, 5));
        }
        engine->setShaperPattern (pattern);
    }
    for (int c = 0; c < modCurveCount; ++c)
    {
        const auto index = static_cast<std::size_t> (c);
        if (const auto generation = curveGeneration[index].load (std::memory_order_acquire); generation != curveGenerationSeen[index])
        {
            curveGenerationSeen[index] = generation;
            const auto& shared = curveShared[index];
            osp::mod::Curve curve;
            curve.count = std::clamp (static_cast<int> (shared[curveValues - 1].load (std::memory_order_relaxed)), 0, osp::mod::maxCurvePoints);
            for (std::size_t i = 0; i < static_cast<std::size_t> (curve.count); ++i)
                curve.points[i] = { shared[3 * i].load (std::memory_order_relaxed), shared[3 * i + 1].load (std::memory_order_relaxed),
                                    shared[3 * i + 2].load (std::memory_order_relaxed) };
            engine->setModulationCurve (c, curve);
            if (c < 6)   // LFO 1-2, ENV 1-2, VEL, NOTE (the rest are DAMPEN's)
            {
                auto& table = c < 2 ? modSettings.lfoCurve[index] : c < 4 ? modSettings.envCurve[index - 2] : c == 4 ? modSettings.velCurve : modSettings.noteCurve;
                table = osp::mod::compileCurve (curve);
                modDirty = true;
            }
        }
    }
    if (modBound)
        applyModulation();

    buffer.clear();
    if (! arpBound)
    {
        engine->process (buffer, midi, performance);
        return;
    }
    osp::HostTiming timing;
    timing.valid = performance.hasPosition;
    timing.playing = performance.playing;
    timing.bpm = performance.bpm;
    timing.ppq = performance.ppq;
    timing.numerator = performance.numerator;
    timing.denominator = performance.denominator;
    const auto& notes = runArpeggiator (midi, buffer.getNumSamples(), timing);
    engine->setPlayedKeys (arpeggiator.isEnabled(), midi);
    engine->process (buffer, notes, performance);
}

void ShellProcessor::applyModulation() noexcept
{
    // As ANDOR OSP's processor gathers it (OspAudioProcessor::applyModulation): rarely
    // changed, so compared first and handed over only when something moved.
    std::array<float, modValueCount> now {};
    std::size_t k = 0;
    for (auto* p : modLfoParams)
        now[k++] = p->load();
    for (auto* p : modEnvParams)
        now[k++] = p->load();
    for (auto* p : modRouteParams)
        now[k++] = p->load();
    for (const auto& t : modRouteTargets)
        now[k++] = static_cast<float> (t.load (std::memory_order_relaxed));
    for (const auto& c : ccNumbers)
        now[k++] = static_cast<float> (c.load (std::memory_order_relaxed));
    now[k++] = std::max (1.0f, seedParam->load());
    now[k++] = velToAmp.load (std::memory_order_relaxed) ? 1.0f : 0.0f;
    if (! modDirty && now == lastModValues)
        return;
    modDirty = false;
    lastModValues = now;
    auto& s = modSettings;
    for (std::size_t i = 0; i < 2; ++i)
    {
        auto l = [this, i] (std::size_t n) { return modLfoParams[8 * i + n]->load(); };
        auto& lfo = s.lfo[i];
        lfo.shape = static_cast<osp::mod::LfoShape> (std::clamp (juce::roundToInt (l (0)), 0, osp::mod::lfoShapeCount - 1));
        lfo.rateHz = l (1);
        lfo.sync = l (2) >= 0.5f;
        lfo.division = std::clamp (juce::roundToInt (l (3)), 0, osp::mod::syncDivisionCount - 1);
        lfo.phase = 0.01 * l (4);
        lfo.bipolar = l (5) < 0.5f;
        lfo.mode = static_cast<osp::mod::LfoMode> (std::clamp (juce::roundToInt (l (6)), 0, 2));
        lfo.scope = l (7) >= 0.5f ? osp::mod::Scope::poly : osp::mod::Scope::global;
        auto e = [this, i] (std::size_t n) { return static_cast<double> (modEnvParams[7 * i + n]->load()); };
        auto& env = s.env[i];
        env.oneShotCurve = e (0) >= 0.5;
        env.attackSeconds = 0.001 * e (1);
        env.decaySeconds = 0.001 * e (2);
        env.sustain = 0.01 * e (3);
        env.releaseSeconds = 0.001 * e (4);
        env.curve = 0.01 * e (5);
        env.lengthSeconds = 0.001 * e (6);
    }
    for (std::size_t r = 0; r < s.routes.size(); ++r)
    {
        auto& route = s.routes[r];
        route.source = static_cast<osp::mod::Source> (std::clamp (juce::roundToInt (modRouteParams[4 * r]->load()), 0, osp::mod::sourceCount));
        route.dest = osp::mod::destFromChoice (juce::roundToInt (modRouteParams[4 * r + 1]->load()));
        route.depth = 0.01 * modRouteParams[4 * r + 2]->load();
        route.enabled = modRouteParams[4 * r + 3]->load() >= 0.5f;
        route.target = modRouteTargets[r].load (std::memory_order_relaxed);
    }
    s.seed = static_cast<std::uint64_t> (std::max (1.0f, seedParam->load()));
    s.velocityToAmp = velToAmp.load (std::memory_order_relaxed);
    for (std::size_t i = 0; i < modControllers.size(); ++i)
        modControllers[i] = ccNumbers[i].load (std::memory_order_relaxed);
    engine->setModulation (s, modControllers);
}

void ShellProcessor::refreshModTargets()
{
    // A depth route's target, by its identity (message thread: the links live in the state).
    for (int r = 0; r < osp::mod::maxRoutes; ++r)
        modRouteTargets[static_cast<std::size_t> (r)].store (resolveModTarget (r), std::memory_order_relaxed);
}

double ShellProcessor::steppedArpRatio (double multiplier) noexcept
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

const juce::MidiBuffer& ShellProcessor::runArpeggiator (const juce::MidiBuffer& midi, int numSamples, const osp::HostTiming& timing) noexcept
{
    // ANDOR OSP's arpeggiator as its processor runs it (OspAudioProcessor::runArpeggiator).
    using namespace osp;
    Arpeggiator::Settings settings;
    settings.enabled = arpEnabledParam->load() >= 0.5f;
    settings.pattern = static_cast<ArpPattern> (std::clamp (juce::roundToInt (arpPatternParam->load()), 0, arp::patternCount - 1));
    settings.rate = static_cast<ArpRate> (std::clamp (juce::roundToInt (arpRateParam->load()), 0, arp::rateCount - 1));
    settings.gate = arpGateParam->load() / 100.0;
    settings.octaves = std::clamp (juce::roundToInt (arpOctavesParam->load()), arp::minOctaves, arp::maxOctaves);
    settings.swing = arpSwingParam->load() / 100.0;
    settings.velocityScale = arpVelocityParam->load() / 100.0;
    settings.rateStepped = arpStepped.load (std::memory_order_relaxed);
    {
        // The ARP destinations, from the engine's last control period (ANDOR OSP's
        // applyArpModulation). Without a route every offset is 0: the settings as they are.
        using osp::mod::Dest;
        auto offset = [this] (Dest d) {
            double v = 0.0;
            return engine->arpModulation (d, v) ? v : 0.0;
        };
        const double rate = std::clamp (offset (Dest::arpRate), -2.0, 2.0);
        settings.rateMultiplier = settings.rateStepped ? steppedArpRatio (std::exp2 (rate)) : std::exp2 (rate);
        settings.gate += offset (Dest::arpGate);
        settings.swing += offset (Dest::arpSwing);
        settings.velocityScale = std::clamp (settings.velocityScale + offset (Dest::arpVelocity), 0.0, 2.0);
        if (double octaves = 0.0; engine->arpModulation (Dest::arpOctaves, octaves))
        {
            // Quantised with hysteresis; the arpeggiator applies it at its next cycle.
            const double wanted = settings.octaves + octaves;
            if (arpModOctaves < arp::minOctaves || std::abs (wanted - arpModOctaves) > 0.6)
                arpModOctaves = std::clamp (static_cast<int> (std::lround (wanted)), arp::minOctaves, arp::maxOctaves);
            settings.octaves = arpModOctaves;
            settings.octavesAtCycle = true;
        }
        else
            arpModOctaves = 0;
    }
    arpeggiator.setSeed (static_cast<std::uint64_t> (std::max (1.0f, seedParam->load())));
    arpeggiator.beginBlock (settings, timing, numSamples);

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
        // the engine plays exactly the MIDI that arrived.
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
    publishArpDisplay();
    return arpMidi;
}

void ShellProcessor::publishArpDisplay() noexcept
{
    osp::Arpeggiator::Display display;
    arpeggiator.display (display);
    for (std::size_t i = 0; i < arpDisplayLow.size(); ++i)
    {
        arpDisplayLow[i].store (display.low[i], std::memory_order_relaxed);
        arpDisplayHigh[i].store (display.high[i], std::memory_order_relaxed);
    }
    arpDisplayCurrent.store (display.current, std::memory_order_relaxed);
    arpDisplayActive.store (display.active, std::memory_order_release);
}

InstrumentHost::ArpView ShellProcessor::arpView() const noexcept
{
    ArpView view;
    view.active = arpDisplayActive.load (std::memory_order_acquire);
    view.current = arpDisplayCurrent.load (std::memory_order_relaxed);
    for (std::size_t i = 0; i < arpDisplayLow.size(); ++i)
    {
        view.low[i] = arpDisplayLow[i].load (std::memory_order_relaxed);
        view.high[i] = arpDisplayHigh[i].load (std::memory_order_relaxed);
    }
    return view;
}

juce::AudioProcessorEditor* ShellProcessor::createEditor()
{
    return new ShellEditor (*this);
}

//==============================================================================
// Sources

InstrumentHost::LoadState ShellProcessor::loadState (int layer) const noexcept
{
    if (isGenerator())
    {
        const int r = resolve (layer);
        if (r == hybridLayer && hasHybridLayer())
            return layers[static_cast<std::size_t> (r)].state.load();
        return r == 0 ? LoadState::ready : LoadState::empty;
    }
    return layers[static_cast<std::size_t> (resolve (layer))].state.load();
}

std::shared_ptr<const LoadedInstrument> ShellProcessor::currentInstrument (int layer) const
{
    return layers[static_cast<std::size_t> (resolve (layer))].instrument;
}

bool ShellProcessor::isLayerOccupied (int layer) const
{
    if (isGenerator() && ! (hasHybridLayer() && layer == hybridLayer))
        return layer == 0;
    const auto& slot = layers[static_cast<std::size_t> (std::clamp (layer, 0, numLayers - 1))];
    return slot.state.load() == LoadState::loading || slot.instrument != nullptr;
}

const osp::InstrumentEngine::GrainSnapshot& ShellProcessor::grainSnapshot (int layer) const noexcept
{
    const int l = std::clamp (layer, 0, numLayers - 1);
    if (const auto* grains = engine->grainSnapshot (l))
        return *grains;
    return noGrains[static_cast<std::size_t> (l)];
}

bool ShellProcessor::generatedSource (int layer, GeneratedSource& out) const
{
    if (! isGenerator() || layer != 0)
        return false;
    out.name = generatorName;
    out.cycle.clear();
    engine->sourcePicture (layer, parameters, out.cycle);
    if (out.cycle.empty())
        out.cycle.assign (2, 0.0f);   // a flat line: the card still shows the generator, not a drop target
    out.revision = fingerprint (out.cycle);
    return true;
}

int ShellProcessor::slotCount() const
{
    if (isGenerator())
        return hasHybridLayer() && isLayerOccupied (hybridLayer) ? 2 : 1;
    int count = 0;
    for (int l = 0; l < numLayers; ++l)
        if (isLayerOccupied (l))
            count = l + 1;
    return count;
}

int ShellProcessor::addLayers (const juce::Array<juce::File>& files)
{
    if (hasHybridLayer())
    {
        // The generator's one recording: the first file goes to layer B (replacing its sound).
        if (files.isEmpty())
            return 0;
        if (files.size() > 1)
        {
            const std::lock_guard<std::mutex> lock (messageMutex);
            lastMessage = productInfo().hybridSampleName + " takes one recording: " + files[0].getFileName() + " was loaded";
        }
        loadFile (files[0], hybridLayer);
        return 1;
    }
    if (isGenerator())
    {
        // Nothing to load: the instrument makes its own sound.
        if (! files.isEmpty())
        {
            const std::lock_guard<std::mutex> lock (messageMutex);
            lastMessage = productInfo().name + " makes its own sound: it plays no recordings";
        }
        return 0;
    }
    int loaded = 0;
    const bool pairBefore = occupiedLayerCount() >= 2;   // an A/B mix the musician has already set
    for (const auto& file : files)
    {
        int free = -1;
        for (int l = 0; l < numLayers && free < 0; ++l)
            if (! isLayerOccupied (l))
                free = l;
        if (free < 0)
        {
            const std::lock_guard<std::mutex> lock (messageMutex);
            lastMessage = "Three layers are the most: " + file.getFileName() + " was not loaded";
            break;
        }
        // A new layer is heard in the mix at once (ANDOR OSP's rule; a free slot's settings
        // are at their defaults: removeLayer and INIT reset them).
        loadFile (file, free);
        makeLayerAudible (free, pairBefore);
        ++loaded;
    }
    return loaded;
}

void ShellProcessor::loadFile (const juce::File& file, int layer)
{
    if (hasHybridLayer())
    {
        juce::ignoreUnused (layer);
        osp::plugin::LoadRequest request;
        request.file = file;
        enqueueLoad (std::move (request), hybridLayer);
        return;
    }
    if (isGenerator())
    {
        addLayers ({ file });
        return;
    }
    osp::plugin::LoadRequest request;
    request.file = file;
    enqueueLoad (std::move (request), resolve (layer));
}

void ShellProcessor::enqueueLoad (osp::plugin::LoadRequest request, int layer)
{
    auto& target = layers[static_cast<std::size_t> (layer)];
    ++pendingLoads;
    target.state = LoadState::loading;
    const auto generation = nextGeneration.fetch_add (1);
    target.latestLoadId = generation;
    // Import, hashing and analysis on the loader thread, never the audio thread.
    loaderPool.addJob ([this, request = std::move (request), generation, layer] {
        auto result = osp::plugin::loadInstrument (request, *store, generation);
        // The next model stage is queued before this one reports, so pendingLoads never reads 0 between them.
        if (result.instrument != nullptr)
            enqueueRefine (result.instrument, result.audio, layer);
        pushResult (std::move (result), layer);
        --pendingLoads;
    });
}

void ShellProcessor::enqueueRefine (std::shared_ptr<const LoadedInstrument> base, std::shared_ptr<const osp::AudioData> audio, int layer)
{
    // The same stages as ANDOR OSP (continuation, then register anchors): the cards show what
    // they add (the sustain loop), and an engine may use them.
    if (closing.load() || base == nullptr || base->model == nullptr || base->model->stage == osp::InstrumentModel::Stage::complete)
        return;
    ++pendingLoads;
    loaderPool.addJob ([this, base = std::move (base), audio = std::move (audio), layer] {
        // A newer sound was dropped into this layer meanwhile: do not spend time on this one.
        if (! closing.load() && base->loadId == layers[static_cast<std::size_t> (layer)].latestLoadId.load())
        {
            auto result = osp::plugin::refineInstrument (*base, audio, nextGeneration.fetch_add (1));
            if (result.instrument != nullptr)
                enqueueRefine (result.instrument, result.audio, layer);
            result.audio.reset();
            pushResult (std::move (result), layer);
        }
        --pendingLoads;
    });
}

void ShellProcessor::pushResult (osp::plugin::LoadResult result, int layer)
{
    const std::lock_guard<std::mutex> lock (resultsMutex);
    finished.push_back ({ layer, std::move (result) });
}

void ShellProcessor::clearLayer (int layer)
{
    auto& slot = layers[static_cast<std::size_t> (layer)];
    slot.instrument = nullptr;
    slot.latestLoadId = nextGeneration.fetch_add (1);   // a load still running for it is dropped
    slot.state = LoadState::empty;
    slot.clearRequested.store (true, std::memory_order_release);
}

void ShellProcessor::removeLayer (int layer)
{
    if (hasHybridLayer() && layer == hybridLayer)
    {
        clearLayer (hybridLayer);   // (its settings stay: the next recording plays with them)
        engine->sourcePublished (hybridLayer, nullptr);
        return;
    }
    if (isGenerator())
        return;
    if (layer < 0 || layer >= numLayers || pendingLoads.load() > 0)
        return;
    // The layers above move down with their sounds and settings (every "layerX." parameter;
    // A's REIMAGINED is the instrument's `reimagined`).
    auto idFor = [] (int l, const juce::String& rest) { return rest == "reimagined" ? reimaginedParameterId (l) : layerParameterId (l, rest); };
    juce::StringArray names;
    for (auto* p : getParameters())
        if (auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*> (p); withId != nullptr && withId->paramID.startsWith ("layerA."))
            names.add (withId->paramID.fromFirstOccurrenceOf (".", false, false));
    names.add ("reimagined");
    const int slots = slotCount();
    for (int l = layer; l + 1 < slots; ++l)
    {
        for (const auto& name : names)
            setParameterValue (idFor (l, name), parameterValue (idFor (l + 1, name)));
        auto& to = layers[static_cast<std::size_t> (l)];
        const auto& from = layers[static_cast<std::size_t> (l + 1)];
        to.instrument = from.instrument;
        to.state = from.state.load();
        if (to.instrument != nullptr)
            to.exchange.publish (to.instrument);
        else
            to.clearRequested.store (true, std::memory_order_release);
    }
    clearLayer (std::max (layer, slots - 1));
    for (const auto& name : names)
        if (auto* p = parameters.getParameter (idFor (std::max (layer, slots - 1), name)))
            p->setValueNotifyingHost (p->getDefaultValue());
}

bool ShellProcessor::waitForLoads (int timeoutMs)
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

void ShellProcessor::timerCallback()
{
    std::deque<Finished> done;
    {
        const std::lock_guard<std::mutex> lock (resultsMutex);
        done.swap (finished);
    }
    for (auto& item : done)
    {
        auto& slot = layers[static_cast<std::size_t> (item.layer)];
        if (item.result.instrument == nullptr || item.result.instrument->loadId != slot.latestLoadId.load())
        {
            if (item.result.instrument == nullptr && slot.state.load() == LoadState::loading)
            {
                // Fail beautifully: odd input is reported, nothing else changes.
                slot.state = slot.instrument != nullptr ? LoadState::ready : LoadState::failed;
                const std::lock_guard<std::mutex> lock (messageMutex);
                lastMessage = juce::String (item.result.error);
            }
            continue;
        }
        slot.instrument = item.result.instrument;
        slot.state = LoadState::ready;
        slot.exchange.publish (slot.instrument);
        engine->sourcePublished (item.layer, slot.instrument);
    }
    for (auto& slot : layers)
        slot.exchange.collectGarbage();
    if (modBound)
        refreshModTargets();
}

juce::String ShellProcessor::statusMessage() const
{
    const std::lock_guard<std::mutex> lock (messageMutex);
    return lastMessage;
}

//==============================================================================
// State: the parameters, the sources (by content hash and path), the curves and the view.

std::unique_ptr<juce::XmlElement> ShellProcessor::stateXml() const
{
    auto xml = std::make_unique<juce::XmlElement> (productInfo().stateType);
    xml->setAttribute ("schemaVersion", stateVersion);
    xml->setAttribute ("product", productInfo().slug);
    xml->setAttribute ("productVersion", productInfo().stateVersion);
    // copyState() flushes the parameters; the shell's tree is only read here.
    if (auto params = const_cast<juce::AudioProcessorValueTreeState&> (parameters).copyState().createXml())
        xml->addChildElement (params.release());
    auto* sources = xml->createNewChildElement (sourcesType.toString());
    for (int l = 0; l < numLayers; ++l)
        if (const auto instrument = layers[static_cast<std::size_t> (l)].instrument)
        {
            auto* e = sources->createNewChildElement (layerType.toString());
            e->setAttribute ("index", l);
            e->setAttribute ("hash", juce::String (instrument->contentHash));
            e->setAttribute ("filename", juce::String (instrument->filename));
            e->setAttribute ("path", juce::String (instrument->originalPath));
        }
    {
        const juce::ScopedLock lock (curveLock);
        xml->createNewChildElement (curvesType.toString())->setAttribute ("value", encodeModulationCurves (curves));
    }
    xml->createNewChildElement (shaperType.toString())->setAttribute ("value", encodeShaperPattern (shaperCustomPattern()));
    auto* view = xml->createNewChildElement (viewType.toString());
    view->setAttribute ("arpOpen", arpOpen.load() ? 1 : 0);
    view->setAttribute ("shaperLarge", shaperLarge.load() ? 1 : 0);
    view->setAttribute ("modTab", modTab.load());
    view->setAttribute ("editLayer", editLayer());
    view->setAttribute ("velToAmp", velToAmp.load() ? 1 : 0);
    view->setAttribute ("arpRateStepped", arpStepped.load() ? 1 : 0);
    juce::StringArray controllers;
    for (const auto& c : ccNumbers)
        controllers.add (juce::String (c.load()));
    view->setAttribute ("controllers", controllers.joinIntoString (","));
    return xml;
}

bool ShellProcessor::applyStateXml (const juce::XmlElement& xml)
{
    // Another product's (or ANDOR OSP's) state is never read as this one's.
    if (! xml.hasTagName (productInfo().stateType) || xml.getStringAttribute ("product") != productInfo().slug)
        return false;
    if (auto* params = xml.getChildByName (parameters.state.getType()))
        parameters.replaceState (juce::ValueTree::fromXml (*params));
    // An older version's state: the product moves its settings to where it reads them now.
    if (const int from = xml.getIntAttribute ("productVersion", 1); from < productInfo().stateVersion && productInfo().migrateState != nullptr)
        productInfo().migrateState (*this, from);
    for (int l = 0; l < numLayers; ++l)
        clearLayer (l);
    if (auto* sources = xml.getChildByName (sourcesType))
        for (auto* e : sources->getChildWithTagNameIterator (layerType.toString()))
        {
            const int index = e->getIntAttribute ("index", -1);
            if (index < 0 || index >= numLayers)
                continue;
            if (isGenerator() && ! (hasHybridLayer() && index == hybridLayer))
                continue;   // (never saved by a generator; ignored if a file says otherwise)
            osp::plugin::LoadRequest request;
            request.expectedHash = e->getStringAttribute ("hash").toStdString();
            request.originalPath = e->getStringAttribute ("path").toStdString();
            request.filename = e->getStringAttribute ("filename").toStdString();
            request.file = juce::File (e->getStringAttribute ("path"));
            enqueueLoad (std::move (request), index);
        }
    if (auto* c = xml.getChildByName (curvesType))
        if (const auto decoded = decodeModulationCurves (c->getStringAttribute ("value")))
        {
            {
                const juce::ScopedLock lock (curveLock);
                curves = *decoded;
            }
            for (int i = 0; i < modCurveCount; ++i)
                publishCurve (i);
        }
    {
        const auto* shaper = xml.getChildByName (shaperType);
        const auto decoded = shaper != nullptr ? decodeShaperPattern (shaper->getStringAttribute ("value")) : std::nullopt;
        setShaperCustomPattern (decoded.value_or (osp::RhythmicShaper::patternSteps (osp::ShaperParams().pattern)), false);
    }
    if (auto* view = xml.getChildByName (viewType))
    {
        arpOpen = view->getIntAttribute ("arpOpen") != 0;
        shaperLarge = view->getIntAttribute ("shaperLarge") != 0;
        setModPanelTab (view->getIntAttribute ("modTab"));
        setEditLayer (view->getIntAttribute ("editLayer"));
        velToAmp = view->getIntAttribute ("velToAmp", 1) != 0;
        arpStepped = view->getIntAttribute ("arpRateStepped") != 0;
        const auto controllers = juce::StringArray::fromTokens (view->getStringAttribute ("controllers"), ",", "");
        for (int i = 0; i < std::min (controllers.size(), osp::mod::ccSlots); ++i)
            setModControllerNumber (i, controllers[i].getIntValue());
    }
    if (modBound)
        refreshModTargets();
    return true;
}

void ShellProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = stateXml())
        copyXmlToBinary (*xml, destData);
}

void ShellProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        applyStateXml (*xml);
}

//==============================================================================
// Presets

juce::File ShellProcessor::documentsFolder() const
{
    if (const auto overridden = folderFromEnvironment ("ANDOR_DOCUMENTS_DIR"); overridden != juce::File())
        return overridden.getChildFile (productInfo().presetFolder);
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile (productInfo().presetFolder);
}

juce::File ShellProcessor::sampleStoreFolder() const
{
    if (const auto overridden = folderFromEnvironment ("ANDOR_SAMPLE_STORE"); overridden != juce::File())
        return overridden;
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile (productInfo().presetFolder).getChildFile ("Samples");
}

bool ShellProcessor::savePreset (const juce::File& file)
{
    auto xml = stateXml();
    if (xml == nullptr || ! file.getParentDirectory().createDirectory())
        return false;
    // Never over a file that is not ours: a preset keeps the product's extension.
    const auto target = file.hasFileExtension (productInfo().presetExtension) ? file : file.withFileExtension (productInfo().presetExtension);
    if (! xml->writeTo (target))
        return false;
    lastPresetFile = target;
    return true;
}

bool ShellProcessor::loadPreset (const juce::File& file)
{
    const auto xml = juce::XmlDocument::parse (file);
    if (xml == nullptr)
        return false;
    rememberPreviousState (file.getFileNameWithoutExtension());
    if (! applyStateXml (*xml))
        return false;
    lastPresetFile = file;
    undoManager.clearUndoHistory();
    return true;
}

void ShellProcessor::rememberPreviousState (const juce::String& label)
{
    previousState = stateXml();
    previousStateLabel = label;
}

bool ShellProcessor::restorePreviousState()
{
    if (previousState == nullptr)
        return false;
    const auto state = std::move (previousState);
    previousStateLabel.clear();
    return applyStateXml (*state);
}

std::vector<InstrumentHost::PresetEntry> ShellProcessor::presetList() const
{
    std::vector<PresetEntry> list;
    auto files = presetFolder().findChildFiles (juce::File::findFiles, true, "*" + productInfo().presetExtension);
    files.sort();
    for (const auto& f : files)
    {
        PresetEntry e;
        e.name = f.getFileNameWithoutExtension();
        e.file = f;
        list.push_back (e);
    }
    return list;
}

juce::String ShellProcessor::presetDisplayName() const
{
    return lastPresetFile == juce::File() ? juce::String ("Init") : lastPresetFile.getFileNameWithoutExtension();
}

void ShellProcessor::stepPresetList (int delta)
{
    const auto list = presetList();
    if (list.empty())
        return;
    int index = -1;
    for (std::size_t i = 0; i < list.size(); ++i)
        if (list[i].file == lastPresetFile)
            index = static_cast<int> (i);
    const int n = static_cast<int> (list.size());
    const int next = index < 0 ? (delta > 0 ? 0 : n - 1) : ((index + delta) % n + n) % n;
    loadPreset (list[static_cast<std::size_t> (next)].file);
}

void ShellProcessor::openPresetEntry (const PresetEntry& entry)
{
    if (entry.file.existsAsFile())
        loadPreset (entry.file);
}

const juce::StringArray& ShellProcessor::favourites() const
{
    if (! favouritesLoaded)
    {
        favouriteNames = juce::StringArray::fromLines (favouritesFile().loadFileAsString());
        favouriteNames.removeEmptyStrings();
        favouritesLoaded = true;
    }
    return favouriteNames;
}

bool ShellProcessor::isFavourite() const
{
    return favourites().contains (presetDisplayName());
}

void ShellProcessor::toggleFavourite()
{
    favourites();
    const auto name = presetDisplayName();
    if (favouriteNames.contains (name))
        favouriteNames.removeString (name);
    else
        favouriteNames.add (name);
    favouritesFile().getParentDirectory().createDirectory();
    favouritesFile().replaceWithText (favouriteNames.joinIntoString ("\n"));
}

void ShellProcessor::resetSettings()
{
    rememberPreviousState ("Reset");
    for (auto* p : getParameters())
        p->setValueNotifyingHost (p->getDefaultValue());
    {
        const juce::ScopedLock lock (curveLock);
        curves = defaultModulationCurves();
    }
    for (int i = 0; i < modCurveCount; ++i)
        publishCurve (i);
    if (auto links = parameters.state.getChildWithName (modLinksTreeType()); links.isValid())
        parameters.state.removeChild (links, nullptr);
    setShaperCustomPattern (osp::RhythmicShaper::patternSteps (osp::ShaperParams().pattern), false);
}

void ShellProcessor::initPatch()
{
    resetSettings();
    previousStateLabel = "Init";
    for (int l = 0; l < numLayers; ++l)
        clearLayer (l);
    lastPresetFile = juce::File();
    undoManager.clearUndoHistory();
}

//==============================================================================
// Modulation curves and controllers

osp::mod::Curve ShellProcessor::modulationCurve (int index) const
{
    const juce::ScopedLock lock (curveLock);
    return curves[static_cast<std::size_t> (std::clamp (index, 0, modCurveCount - 1))];
}

void ShellProcessor::setModulationCurve (int index, const osp::mod::Curve& curve, bool undoable)
{
    index = std::clamp (index, 0, modCurveCount - 1);
    auto next = curve;
    next.normalise();
    const auto before = modulationCurve (index);
    if (before == next)
        return;
    if (undoable)
    {
        undoManager.perform (new CurveAction (*this, index, before, next));
        return;
    }
    {
        const juce::ScopedLock lock (curveLock);
        curves[static_cast<std::size_t> (index)] = next;
    }
    publishCurve (index);
}

void ShellProcessor::publishCurve (int index) noexcept
{
    const auto curve = modulationCurve (index);
    auto& shared = curveShared[static_cast<std::size_t> (index)];
    for (std::size_t i = 0; i < static_cast<std::size_t> (osp::mod::maxCurvePoints); ++i)
    {
        shared[3 * i].store (curve.points[i].x, std::memory_order_relaxed);
        shared[3 * i + 1].store (curve.points[i].y, std::memory_order_relaxed);
        shared[3 * i + 2].store (curve.points[i].tension, std::memory_order_relaxed);
    }
    shared[curveValues - 1].store (static_cast<float> (curve.count), std::memory_order_relaxed);
    curveGeneration[static_cast<std::size_t> (index)].fetch_add (1, std::memory_order_release);
}

int ShellProcessor::modControllerNumber (int slot) const noexcept
{
    return slot >= 0 && slot < osp::mod::ccSlots ? ccNumbers[static_cast<std::size_t> (slot)].load (std::memory_order_relaxed) : -1;
}

void ShellProcessor::setModControllerNumber (int slot, int controller) noexcept
{
    if (slot >= 0 && slot < osp::mod::ccSlots)
        ccNumbers[static_cast<std::size_t> (slot)].store (std::clamp (controller, 0, 119), std::memory_order_relaxed);
}

//==============================================================================
// SHAPER CUSTOM

osp::ShaperPattern ShellProcessor::shaperCustomPattern() const
{
    const juce::ScopedLock lock (shaperLock);
    return shaperPattern;
}

void ShellProcessor::setShaperCustomPattern (const osp::ShaperPattern& pattern, bool undoable)
{
    const auto before = shaperCustomPattern();
    if (before == pattern)
        return;
    if (undoable)
    {
        undoManager.perform (new ShaperAction (*this, before, pattern));
        return;
    }
    {
        const juce::ScopedLock lock (shaperLock);
        shaperPattern = pattern;
    }
    publishShaperPattern();
}

void ShellProcessor::publishShaperPattern() noexcept
{
    const auto pattern = shaperCustomPattern();
    for (std::size_t i = 0; i < pattern.size(); ++i)
    {
        shaperSteps[3 * i].store (std::clamp (pattern[i].start, 0.0f, 1.0f), std::memory_order_relaxed);
        shaperSteps[3 * i + 1].store (std::clamp (pattern[i].end, 0.0f, 1.0f), std::memory_order_relaxed);
        shaperSteps[3 * i + 2].store (static_cast<float> (pattern[i].shape), std::memory_order_relaxed);
    }
    shaperGeneration.fetch_add (1, std::memory_order_release);
}

int ShellProcessor::recentVelocityAt (int index) const noexcept
{
    return recentVelocity[static_cast<std::size_t> (((index % velocityHistory) + velocityHistory) % velocityHistory)].load (std::memory_order_relaxed);
}

} // namespace andor::shell

// The plugin's entry point: every product built from the shell is a ShellProcessor.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new andor::shell::ShellProcessor();
}
