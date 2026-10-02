#include "research/RenderSession.h"

#include "audio/sampler/BaselineSampler.h"
#include "core/PitchMath.h"
#include "engine/InstrumentBuilder.h"
#include "engine/InstrumentEngine.h"
#include "model/PlaybackSource.h"

#include <algorithm>
#include <cmath>

namespace osp::research
{

namespace
{
    /** Host-style block loop shared by every engine (sample-accurate events inside blocks). */
    template <typename Engine>
    RenderOutput runBlocks (Engine& engine, double outputRate, int blockSize, const MidiSequence& sequence, double maxTailSeconds)
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

RenderOutput renderSequence (const AudioData& source, double rootMidi, const MidiSequence& sequence,
                             const RenderConfig& config, const PlaybackPreparation& preparation)
{
    const double outputRate = config.sampleRate > 0.0 ? config.sampleRate : source.sampleRate;
    const int blockSize = std::max (1, config.blockSize);

    BaselineSampler sampler;
    sampler.prepare (outputRate, blockSize, config.effectiveSamplerSettings());
    const PlaybackSource playback (source, rootMidi, sampler.requiredSourcePadding(), preparation.startSeconds,
                                   preparation.gainDb);
    sampler.setSource (&playback);
    return runBlocks (sampler, outputRate, blockSize, sequence, config.maxTailSeconds);
}

RenderOutput renderInstrument (const InstrumentModel& model, const MidiSequence& sequence, const RenderConfig& config)
{
    const double outputRate = config.sampleRate > 0.0 ? config.sampleRate : model.original.source->sampleRate();
    const int blockSize = std::max (1, config.blockSize);
    InstrumentEngine engine;
    engine.prepare (outputRate, blockSize, config.engineSettings);
    engine.setModel (&model);
    return runBlocks (engine, outputRate, blockSize, sequence, config.maxTailSeconds);
}

RenderOutput renderSet (const InstrumentSet& set, const MidiSequence& sequence, const RenderConfig& config)
{
    const auto& primary = *set.members[static_cast<std::size_t> (set.primary)].model;
    const double outputRate = config.sampleRate > 0.0 ? config.sampleRate : primary.original.source->sampleRate();
    const int blockSize = std::max (1, config.blockSize);
    InstrumentEngine engine;
    engine.prepare (outputRate, blockSize, config.engineSettings);
    engine.setInstrumentSet (&set);
    return runBlocks (engine, outputRate, blockSize, sequence, config.maxTailSeconds);
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
    return renderInstrument (*model, sequence, config);
}

} // namespace osp::research
