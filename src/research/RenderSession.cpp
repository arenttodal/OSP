#include "research/RenderSession.h"

#include "audio/sampler/BaselineSampler.h"
#include "core/PitchMath.h"
#include "engine/InstrumentBuilder.h"
#include "engine/InstrumentEngine.h"
#include "model/PlaybackSource.h"
#include "research/SourceAnalysis.h"

#include <algorithm>
#include <cmath>

namespace osp::research
{

namespace
{
    struct NoAutomation
    {
        void operator() (double) const noexcept {}
    };

    struct KeyDnaAutomation
    {
        InstrumentEngine& engine;
        const RenderConfig& config;
        void operator() (double seconds) const { applyKeyDnaAutomation (engine, config, seconds); }
    };

    /** Host-style block loop shared by every engine (sample-accurate events inside blocks);
        `onBlock (seconds)` runs before each block (automation). */
    template <typename Engine, typename OnBlock = NoAutomation>
    RenderOutput runBlocks (Engine& engine, double outputRate, int blockSize, const MidiSequence& sequence, double maxTailSeconds,
                            OnBlock onBlock = {})
    {
        RenderOutput output;
        const auto& events = sequence.events;
        output.lastEventSeconds = sequence.endTimeSeconds();
        const auto lastEventSample = static_cast<std::int64_t> (std::llround (output.lastEventSeconds * outputRate));
        const auto limit = lastEventSample + static_cast<std::int64_t> (maxTailSeconds * outputRate);

        std::vector<float> left, right;
        left.reserve (static_cast<std::size_t> (std::max<std::int64_t> (limit, 0) + blockSize));
        right.reserve (left.capacity());
        std::vector<float> blockL (static_cast<std::size_t> (blockSize));
        std::vector<float> blockR (static_cast<std::size_t> (blockSize));

        std::size_t next = 0;
        std::int64_t position = 0;
        auto renderSpan = [&] (int offset, int count) {
            if (count <= 0)
                return;
            float* channels[2] = { blockL.data() + offset, blockR.data() + offset };
            engine.render (channels, 2, count);
        };
        auto apply = [&] (const MidiEvent& e) {
            switch (e.type)
            {
                case MidiEvent::Type::noteOn: engine.noteOn (e.note, e.value); break;
                case MidiEvent::Type::noteOff: engine.noteOff (e.note); break;
                case MidiEvent::Type::sustainPedal: engine.setSustainPedal (e.value >= 64); break;
                case MidiEvent::Type::allNotesOff: engine.allNotesOff(); break;
            }
        };

        while (true)
        {
            const std::int64_t blockEnd = position + blockSize;
            onBlock (static_cast<double> (position) / outputRate);
            int offset = 0;
            while (next < events.size())
            {
                const auto eventSample = std::max<std::int64_t> (position, std::llround (events[next].timeSeconds * outputRate));
                if (eventSample >= blockEnd)
                    break;
                const int eventOffset = static_cast<int> (eventSample - position);
                renderSpan (offset, eventOffset - offset);
                offset = eventOffset;
                apply (events[next++]);
            }
            renderSpan (offset, blockSize - offset);

            left.insert (left.end(), blockL.begin(), blockL.end());
            right.insert (right.end(), blockR.begin(), blockR.end());
            position = blockEnd;
            ++output.blocks;

            const bool eventsDone = next >= events.size() && position >= lastEventSample;
            if (eventsDone && engine.activeVoiceCount() == 0)
                break;
            if (position >= limit)
                break;
        }

        output.audio.sampleRate = outputRate;
        output.audio.channels = { std::move (left), std::move (right) };
        return output;
    }
}

MidiSequence arpeggiate (const MidiSequence& sequence, const RenderConfig& config, double outputRate)
{
    if (! config.arp.settings.enabled)
        return sequence;
    const int blockSize = std::max (1, config.blockSize);
    Arpeggiator arp;
    arp.setSeed (config.engineSettings.seed);
    arp.prepare (outputRate);
    HostTiming timing;
    timing.valid = timing.playing = config.arp.transport;
    timing.bpm = config.arp.bpm;
    // Long enough after the last event for the last gate (up to 150 % of a dotted quarter).
    const auto end = static_cast<std::int64_t> (std::llround ((sequence.endTimeSeconds() + 2.25 * 60.0 / config.arp.bpm + 0.01) * outputRate));
    MidiSequence out;
    out.name = sequence.name;
    std::size_t next = 0;
    for (std::int64_t position = 0; position < end; position += blockSize)
    {
        const int n = static_cast<int> (std::min<std::int64_t> (blockSize, end - position));
        arp.beginBlock (config.arp.settings, timing, n);
        while (next < sequence.events.size())
        {
            const auto& e = sequence.events[next];
            const auto at = std::max<std::int64_t> (position, std::llround (e.timeSeconds * outputRate));
            if (at >= position + n)
                break;
            const int offset = static_cast<int> (at - position);
            switch (e.type)
            {
                case MidiEvent::Type::noteOn: arp.noteOn (offset, e.note, e.value, e.channel); break;
                case MidiEvent::Type::noteOff: arp.noteOff (offset, e.note, e.channel); break;
                case MidiEvent::Type::sustainPedal: arp.sustainPedal (offset, e.value >= 64); break;
                case MidiEvent::Type::allNotesOff:
                    arp.allNotesOff (offset);
                    out.events.push_back (e);
                    break;
            }
            ++next;
        }
        arp.endBlock();
        for (int i = 0; i < arp.numEvents(); ++i)
        {
            const auto& g = arp.event (i);
            MidiEvent m;
            m.timeSeconds = static_cast<double> (position + g.offset) / outputRate;
            m.note = g.note;
            m.channel = g.channel;
            switch (g.kind)
            {
                case Arpeggiator::Event::Kind::noteOn: m.type = MidiEvent::Type::noteOn; m.value = g.velocity; break;
                case Arpeggiator::Event::Kind::noteOff: m.type = MidiEvent::Type::noteOff; m.value = 0; break;
                case Arpeggiator::Event::Kind::sustainOn: m.type = MidiEvent::Type::sustainPedal; m.value = 127; break;
                case Arpeggiator::Event::Kind::sustainOff: m.type = MidiEvent::Type::sustainPedal; m.value = 0; break;
            }
            out.events.push_back (m);
        }
        timing.ppq += n * config.arp.bpm / (60.0 * outputRate);
    }
    // Already in time order within each block; equal times keep the arpeggiator's order
    // (note-offs before note-ons).
    std::stable_sort (out.events.begin(), out.events.end(), [] (const MidiEvent& a, const MidiEvent& b) { return a.timeSeconds < b.timeSeconds; });
    return out;
}

RenderOutput renderSequence (const AudioData& source, double rootMidi, const MidiSequence& input,
                             const RenderConfig& config, const PlaybackPreparation& preparation)
{
    const double outputRate = config.sampleRate > 0.0 ? config.sampleRate : source.sampleRate;
    const int blockSize = std::max (1, config.blockSize);

    const auto sequence = arpeggiate (input, config, outputRate);
    BaselineSampler sampler;
    sampler.prepare (outputRate, blockSize, config.effectiveSamplerSettings());
    const PlaybackSource playback (source, rootMidi, sampler.requiredSourcePadding(), preparation.startSeconds,
                                   preparation.gainDb);
    sampler.setSource (&playback);
    return runBlocks (sampler, outputRate, blockSize, sequence, config.maxTailSeconds);
}

void applyKeyDnaAutomation (InstrumentEngine& engine, const RenderConfig& config, double seconds)
{
    for (int layer = 0; layer < 3; ++layer)
    {
        const auto& lane = config.keyDnaAmountAutomation[static_cast<std::size_t> (layer)];
        if (lane.empty())
            continue;
        double amount = lane.back().second;
        if (seconds <= lane.front().first)
            amount = lane.front().second;
        else
            for (std::size_t i = 1; i < lane.size(); ++i)
                if (seconds < lane[i].first)
                {
                    const auto& [t0, a0] = lane[i - 1];
                    const auto& [t1, a1] = lane[i];
                    amount = a0 + (a1 - a0) * (seconds - t0) / std::max (1.0e-9, t1 - t0);
                    break;
                }
        engine.setLayerKeyDnaAmount (layer, amount);
    }
}

RenderOutput renderInstrument (const InstrumentModel& model, const MidiSequence& sequence, const RenderConfig& config)
{
    const double outputRate = config.sampleRate > 0.0 ? config.sampleRate : model.original.source->sampleRate();
    const int blockSize = std::max (1, config.blockSize);
    InstrumentEngine engine;
    engine.prepare (outputRate, blockSize, config.engineSettings);
    engine.setModel (&model);
    for (int layer = 0; layer < 3; ++layer)
        engine.setLayerKeyDna (layer, config.keyDna[static_cast<std::size_t> (layer)]);
    engine.setModulation (config.modulation);
    engine.setModWheel (static_cast<float> (config.modWheel));
    return runBlocks (engine, outputRate, blockSize, arpeggiate (sequence, config, outputRate), config.maxTailSeconds,
                      KeyDnaAutomation { engine, config });
}

RenderOutput renderSet (const InstrumentSet& set, const MidiSequence& sequence, const RenderConfig& config)
{
    const auto& primary = *set.members[static_cast<std::size_t> (set.primary)].model;
    const double outputRate = config.sampleRate > 0.0 ? config.sampleRate : primary.original.source->sampleRate();
    const int blockSize = std::max (1, config.blockSize);
    InstrumentEngine engine;
    engine.prepare (outputRate, blockSize, config.engineSettings);
    engine.setInstrumentSet (&set);
    for (int layer = 0; layer < 3; ++layer)
        engine.setLayerKeyDna (layer, config.keyDna[static_cast<std::size_t> (layer)]);
    engine.setModulation (config.modulation);
    engine.setModWheel (static_cast<float> (config.modWheel));
    return runBlocks (engine, outputRate, blockSize, arpeggiate (sequence, config, outputRate), config.maxTailSeconds,
                      KeyDnaAutomation { engine, config });
}

RenderOutput renderLayers (const std::array<const InstrumentModel*, 3>& models, const MidiSequence& sequence, const RenderConfig& config)
{
    const InstrumentModel* first = nullptr;
    for (const auto* m : models)
        if (first == nullptr && m != nullptr)
            first = m;
    const double outputRate = config.sampleRate > 0.0 ? config.sampleRate : (first != nullptr ? first->original.source->sampleRate() : 48000.0);
    const int blockSize = std::max (1, config.blockSize);
    InstrumentEngine engine;
    engine.prepare (outputRate, blockSize, config.engineSettings);
    for (int layer = 0; layer < 3; ++layer)
    {
        engine.setModel (models[static_cast<std::size_t> (layer)], layer);
        engine.setLayerKeyDna (layer, config.keyDna[static_cast<std::size_t> (layer)]);
    }
    engine.setModulation (config.modulation);
    engine.setModWheel (static_cast<float> (config.modWheel));
    return runBlocks (engine, outputRate, blockSize, arpeggiate (sequence, config, outputRate), config.maxTailSeconds,
                      KeyDnaAutomation { engine, config });
}

RenderOutput renderLayerInstruments (const std::array<const InstrumentModel*, 3>& models, const std::array<const InstrumentSet*, 3>& sets,
                                     const MidiSequence& sequence, const RenderConfig& config,
                                     const std::function<void (InstrumentEngine&, double)>& onBlock)
{
    const double outputRate = config.sampleRate > 0.0 ? config.sampleRate : 48000.0;
    const int blockSize = std::max (1, config.blockSize);
    InstrumentEngine engine;
    engine.prepare (outputRate, blockSize, config.engineSettings);
    for (int layer = 0; layer < 3; ++layer)
    {
        if (const auto* set = sets[static_cast<std::size_t> (layer)])
            engine.setInstrumentSet (set, layer);
        else
            engine.setModel (models[static_cast<std::size_t> (layer)], layer);
        engine.setLayerKeyDna (layer, config.keyDna[static_cast<std::size_t> (layer)]);
    }
    engine.setModulation (config.modulation);
    engine.setModWheel (static_cast<float> (config.modWheel));
    return runBlocks (engine, outputRate, blockSize, arpeggiate (sequence, config, outputRate), config.maxTailSeconds,
                      [&] (double seconds) {
                          applyKeyDnaAutomation (engine, config, seconds);
                          if (onBlock)
                              onBlock (engine, seconds);
                      });
}

RenderOutput renderWithEngine (const AudioData& source, const AnalysisData& analysis, double rootMidi, const MidiSequence& sequence,
                               const RenderConfig& config, const PlaybackPreparation& preparation)
{
    if (config.engine != EngineId::instrument)
        return renderSequence (source, rootMidi, sequence, config, preparation);
    InstrumentBuildOptions options;
    options.rootOverrideMidi = rootMidi;
    options.interpolationZeroCrossings = config.engineSettings.interpolationZeroCrossings;
    options.seed = config.engineSettings.seed;
    const auto model = instrument::buildComplete (source, analysis, options, config.anchors);
    if (config.layerSources[1].empty() && config.layerSources[2].empty())
        return renderInstrument (*model, sequence, config);
    // Layers B and C play their own recordings (each analysed and built like layer A's,
    // at its own detected root).
    std::array<std::shared_ptr<InstrumentModel>, 3> layers { model, nullptr, nullptr };
    for (std::size_t l = 1; l < layers.size(); ++l)
        if (! config.layerSources[l].empty())
        {
            auto loaded = loadAndAnalyse (config.layerSources[l]);
            if (! loaded.ok)
                continue;   // an unreadable layer stays empty (the render goes on)
            InstrumentBuildOptions layerOptions = options;
            layerOptions.rootOverrideMidi.reset();
            layers[l] = instrument::buildComplete (loaded.audio, loaded.analysis, layerOptions, config.anchors);
        }
    return renderLayers ({ layers[0].get(), layers[1].get(), layers[2].get() }, sequence, config);
}

} // namespace osp::research
