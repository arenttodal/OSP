#pragma once

#include <cstdint>
#include <vector>

namespace osp
{

/**
    Canonical in-memory audio: planar 32-bit float channels at a given sample rate.

    This is the only representation DSP and analysis code sees. Decoders convert into
    it; the original file is never modified.
*/
struct AudioData
{
    double sampleRate = 0.0;
    std::vector<std::vector<float>> channels;

    int numChannels() const noexcept { return static_cast<int> (channels.size()); }

    std::int64_t numFrames() const noexcept
    {
        return channels.empty() ? 0 : static_cast<std::int64_t> (channels.front().size());
    }

    double durationSeconds() const noexcept
    {
        return sampleRate > 0.0 ? static_cast<double> (numFrames()) / sampleRate : 0.0;
    }

    bool isEmpty() const noexcept { return numFrames() == 0 || sampleRate <= 0.0; }

    /** Average of all channels (equal weight). */
    std::vector<float> mixToMono() const
    {
        std::vector<float> mono (static_cast<std::size_t> (numFrames()), 0.0f);
        if (channels.empty())
            return mono;
        const float scale = 1.0f / static_cast<float> (channels.size());
        for (const auto& ch : channels)
            for (std::size_t i = 0; i < mono.size(); ++i)
                mono[i] += ch[i] * scale;
        return mono;
    }

    static AudioData allocate (int numChannels, std::int64_t numFrames, double sampleRate)
    {
        AudioData data;
        data.sampleRate = sampleRate;
        data.channels.assign (static_cast<std::size_t> (numChannels),
                              std::vector<float> (static_cast<std::size_t> (numFrames), 0.0f));
        return data;
    }
};

} // namespace osp
