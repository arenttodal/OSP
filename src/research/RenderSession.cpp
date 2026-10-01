#include "research/RenderSession.h"

#include "audio/sampler/BaselineSampler.h"
#include "core/PitchMath.h"
#include "model/PlaybackSource.h"

#include <algorithm>
#include <cmath>

namespace osp::research
{

RenderOutput renderSequence (const AudioData& source, double rootMidi, const MidiSequence& sequence,
                             const RenderConfig& config)
{
    RenderOutput output;
    const double outputRate = config.sampleRate > 0.0 ? config.sampleRate : source.sampleRate;
    const int blockSize = std::max (1, config.blockSize);

    BaselineSampler sampler;
    sampler.prepare (outputRate, blockSize, config.effectiveSamplerSettings());
    const PlaybackSource playback (source, rootMidi, sampler.requiredSourcePadding());
    sampler.setSource (&playback);

    const auto& events = sequence.events;
    output.lastEventSeconds = sequence.endTimeSeconds();
    const auto lastEventSample = static_cast<std::int64_t> (std::llround (output.lastEventSeconds * outputRate));
    const auto limit = lastEventSample + static_cast<std::int64_t> (config.maxTailSeconds * outputRate);

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
        sampler.render (channels, 2, count);
    };

    auto apply = [&] (const MidiEvent& e) {
        switch (e.type)
        {
            case MidiEvent::Type::noteOn: sampler.noteOn (e.note, e.value); break;
            case MidiEvent::Type::noteOff: sampler.noteOff (e.note); break;
            case MidiEvent::Type::sustainPedal: sampler.setSustainPedal (e.value >= 64); break;
            case MidiEvent::Type::allNotesOff: sampler.allNotesOff(); break;
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
        if (eventsDone && sampler.activeVoiceCount() == 0)
            break;
        if (position >= limit)
            break;
    }

    output.audio.sampleRate = outputRate;
    output.audio.channels = { std::move (left), std::move (right) };
    return output;
}

} // namespace osp::research
