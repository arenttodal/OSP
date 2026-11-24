#pragma once

#include "audio/pitch/OctaveDecimator.h"
#include "core/AudioData.h"

#include <algorithm>
#include <cmath>
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
        @param startSeconds where notes start reading (0 = start of file; see PlaybackPreparation)
        @param gainDb       non-destructive playback gain applied to every note
    */
    PlaybackSource (const AudioData& audio, double rootMidi, int padding, double startSeconds = 0.0, double gainDb = 0.0)
        : sourceSampleRate (audio.sampleRate),
          root (rootMidi),
          pad (padding),
          frames (audio.numFrames()),
          start (std::clamp (startSeconds * audio.sampleRate, 0.0, static_cast<double> (std::max<std::int64_t> (0, audio.numFrames() - 1)))),
          gainLinear (std::pow (10.0, gainDb / 20.0))
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

    /**
        Adds `levels` octave-down copies of every channel (level L: the rate halved L times,
        each padded as the original), for reads more than two octaves above the recording
        (InstrumentVoice::readFrame): those read a copy instead, so their cost stays that of a
        read two octaves up. Off the audio thread (allocates; ~0.94x the samples again for 4).
        Baselines A / B never call it.
    */
    void buildOctaveLevels (int levels)
    {
        octaves.clear();
        std::vector<std::vector<float>> previous;
        for (int ch = 0; ch < numChannels(); ++ch)
        {
            const auto& p = padded[static_cast<std::size_t> (ch)];
            previous.emplace_back (p.begin() + pad, p.begin() + pad + frames);
        }
        for (int level = 1; level <= levels && ! previous.empty() && previous.front().size() > 1; ++level)
        {
            std::vector<std::vector<float>> next, stored;
            for (const auto& channel : previous)
            {
                auto half = decimateByTwo (channel);
                std::vector<float> withPad (half.size() + 2 * static_cast<std::size_t> (pad), 0.0f);
                std::copy (half.begin(), half.end(), withPad.begin() + pad);
                stored.push_back (std::move (withPad));
                next.push_back (std::move (half));
            }
            octaves.push_back (std::move (stored));
            previous = std::move (next);
        }
    }

    /** How many octave-down copies there are (0: none). */
    int octaveLevels() const noexcept { return static_cast<int> (octaves.size()); }

    /** Sample 0 of a channel of octave-down copy `level` (1..octaveLevels(); 0: the original). */
    const float* octaveData (int level, int channel) const noexcept
    {
        if (level <= 0)
            return channelData (channel);
        const auto& copy = octaves[static_cast<std::size_t> (level - 1)];
        return copy[static_cast<std::size_t> (std::min (channel, numChannels() - 1))].data() + pad;
    }

    /** Copy played at a different root (a user's root correction). Off the audio thread. */
    PlaybackSource withRootMidi (double newRootMidi) const
    {
        auto copy = *this;
        copy.root = newRootMidi;
        return copy;
    }

    double sampleRate() const noexcept { return sourceSampleRate; }
    double rootMidi() const noexcept { return root; }
    /** Read position (source frames) at which notes start. */
    double startFrame() const noexcept { return start; }
    /** Linear playback gain (1 = the recording's own level). */
    double playbackGain() const noexcept { return gainLinear; }
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
    double start = 0.0;
    double gainLinear = 1.0;
    std::vector<std::vector<float>> padded;
    std::vector<std::vector<std::vector<float>>> octaves;   ///< [level - 1][channel], padded
};

} // namespace osp
