#include "audio/capture/CaptureBuffer.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace osp::capture
{

void CaptureBuffer::prepare (double sampleRate, double seconds)
{
    rate = sampleRate > 0.0 ? sampleRate : 48000.0;
    margin = static_cast<std::int64_t> (std::ceil (ringMarginSeconds * rate));
    capacity = static_cast<std::int64_t> (std::ceil (std::max (seconds, 0.0) * rate)) + margin;
    ring.assign (2, std::vector<float> (static_cast<std::size_t> (capacity), 0.0f));
    written.store (0, std::memory_order_release);
    peak.store (0.0f, std::memory_order_relaxed);
}

void CaptureBuffer::release()
{
    ring.clear();
    ring.shrink_to_fit();
    capacity = 0;
    margin = 0;
    written.store (0, std::memory_order_release);
}

void CaptureBuffer::write (const float* const* input, int numChannels, int numSamples) noexcept
{
    if (capacity <= 0 || input == nullptr || numChannels <= 0 || numSamples <= 0)
        return;
    const auto start = written.load (std::memory_order_relaxed);
    float loudest = 0.0f;
    for (int ch = 0; ch < 2; ++ch)
    {
        const float* source = input[std::min (ch, numChannels - 1)];
        if (source == nullptr)
            continue;
        auto& dest = ring[static_cast<std::size_t> (ch)];
        // A block longer than the ring keeps only its end (never happens with real hosts).
        int done = std::max (0, numSamples - static_cast<int> (capacity - margin));
        while (done < numSamples)
        {
            const auto at = static_cast<std::size_t> ((start + done) % capacity);
            const int n = static_cast<int> (std::min<std::int64_t> (numSamples - done, capacity - static_cast<std::int64_t> (at)));
            std::memcpy (dest.data() + at, source + done, sizeof (float) * static_cast<std::size_t> (n));
            done += n;
        }
        if (ch < numChannels)
            for (int i = 0; i < numSamples; ++i)
                loudest = std::max (loudest, std::abs (source[i]));
    }
    if (loudest > peak.load (std::memory_order_relaxed))
        peak.store (loudest, std::memory_order_relaxed);
    written.store (start + numSamples, std::memory_order_release);
}

std::int64_t CaptureBuffer::oldestSafe() const noexcept
{
    // The writer may be filling up to `margin` frames past what it has published.
    return std::max<std::int64_t> (0, position() + margin - capacity);
}

bool CaptureBuffer::copy (std::int64_t from, std::int64_t to, AudioData& out) const
{
    out = {};
    if (capacity <= 0 || to <= from || from < oldestSafe() || to > position())
        return false;
    const auto frames = to - from;
    AudioData take;
    take.sampleRate = rate;
    take.channels.assign (2, std::vector<float> (static_cast<std::size_t> (frames)));
    for (std::size_t ch = 0; ch < 2; ++ch)
    {
        std::int64_t done = 0;
        while (done < frames)
        {
            const auto at = (from + done) % capacity;
            const auto n = std::min (frames - done, capacity - at);
            std::memcpy (take.channels[ch].data() + done, ring[ch].data() + at, sizeof (float) * static_cast<std::size_t> (n));
            done += n;
        }
    }
    // Overwritten while it was copied (the input kept coming): refused, never torn.
    if (from < oldestSafe())
        return false;
    if (std::equal (take.channels[0].begin(), take.channels[0].end(), take.channels[1].begin()))
        take.channels.pop_back();   // a mono input
    out = std::move (take);
    return true;
}

AudioData trimTake (const AudioData& take, double floorDb, double leadSeconds, double tailSeconds)
{
    if (take.isEmpty())
        return {};
    const auto frames = take.numFrames();
    const auto window = std::max<std::int64_t> (1, static_cast<std::int64_t> (0.01 * take.sampleRate));
    const float floor = static_cast<float> (std::pow (10.0, floorDb / 20.0));
    auto loud = [&] (std::int64_t w) {
        const auto a = w * window, b = std::min (frames, a + window);
        for (const auto& channel : take.channels)
            for (auto i = a; i < b; ++i)
                if (std::abs (channel[static_cast<std::size_t> (i)]) >= floor)
                    return true;
        return false;
    };
    const auto windows = (frames + window - 1) / window;
    std::int64_t first = -1, last = -1;
    for (std::int64_t w = 0; w < windows; ++w)
        if (loud (w))
        {
            if (first < 0)
                first = w;
            last = w;
        }
    if (first < 0)
        return {};
    const auto start = std::max<std::int64_t> (0, first * window - static_cast<std::int64_t> (leadSeconds * take.sampleRate));
    const auto end = std::min (frames, (last + 1) * window + static_cast<std::int64_t> (tailSeconds * take.sampleRate));
    AudioData out;
    out.sampleRate = take.sampleRate;
    for (const auto& channel : take.channels)
        out.channels.emplace_back (channel.begin() + start, channel.begin() + end);
    return out;
}

} // namespace osp::capture
