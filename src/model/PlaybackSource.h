#pragma once

#include "core/AudioData.h"

#include <algorithm>
#include <cstdint>
#include <vector>

namespace osp
{

/**
    Immutable, playback-ready view of a source recording.

    Built off the audio thread (it allocates). Channels are zero-padded on both sides
    so interpolators can read past the ends without bounds checks. Voices hold a
    const pointer to this object; it must outlive any voice reading from it.
*/
class PlaybackSource
{
public:
    PlaybackSource() = default;

    /**
        @param audio        decoded source (1 or more channels; only the first two are used)
        @param rootMidi     fractional MIDI note at which the recording plays untransposed
        @param padding      zero samples added on both sides (>= interpolator reach)
    */
    PlaybackSource (const AudioData& audio, double rootMidi, int padding)
        : sourceSampleRate (audio.sampleRate),
          root (rootMidi),
          pad (padding),
          frames (audio.numFrames())
    {
        const int used = std::min (audio.numChannels(), 2);
        padded.resize (static_cast<std::size_t> (used));
        for (int ch = 0; ch < used; ++ch)
        {
            auto& dst = padded[static_cast<std::size_t> (ch)];
            dst.assign (static_cast<std::size_t> (frames + 2 * pad), 0.0f);
            const auto& src = audio.channels[static_cast<std::size_t> (ch)];
            std::copy (src.begin(), src.end(), dst.begin() + pad);
        }
    }

    double sampleRate() const noexcept { return sourceSampleRate; }
    double rootMidi() const noexcept { return root; }
    std::int64_t numFrames() const noexcept { return frames; }
    int numChannels() const noexcept { return static_cast<int> (padded.size()); }
    int padding() const noexcept { return pad; }
    bool isValid() const noexcept { return frames > 0 && sourceSampleRate > 0.0 && ! padded.empty(); }

    /** Pointer to source sample 0 of a channel (padding precedes it). Mono sources map every channel to 0. */
    const float* channelData (int channel) const noexcept
    {
        const auto index = static_cast<std::size_t> (std::min (channel, numChannels() - 1));
        return padded[index].data() + pad;
    }

private:
    double sourceSampleRate = 0.0;
    double root = 60.0;
    int pad = 0;
    std::int64_t frames = 0;
    std::vector<std::vector<float>> padded;
};

} // namespace osp
