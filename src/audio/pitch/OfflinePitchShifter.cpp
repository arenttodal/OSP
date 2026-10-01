#include "audio/pitch/OfflinePitchShifter.h"

#include <signalsmith-stretch/signalsmith-stretch.h>

namespace osp
{

AudioData stretchShiftOffline (const AudioData& source, const StretchShiftOptions& options)
{
    if (source.isEmpty())
        return source;

    signalsmith::stretch::SignalsmithStretch<float> stretch (static_cast<long> (options.seed & 0x7fffffff));
    stretch.presetDefault (source.numChannels(), static_cast<float> (source.sampleRate));
    stretch.setTransposeSemitones (static_cast<float> (options.semitones));
    if (options.preserveFormants)
    {
        stretch.setFormantFactor (1.0f, true);
        // The library expects the F0 hint relative to the sample rate (0 = estimate it).
        stretch.setFormantBase (static_cast<float> (options.formantBaseHz / source.sampleRate));
    }

    auto output = AudioData::allocate (source.numChannels(), source.numFrames(), source.sampleRate);
    const auto frames = static_cast<int> (source.numFrames());
    if (! stretch.exact (source.channels, frames, output.channels, frames))
        return source; // too short for the shifter's latency: leave untouched
    return output;
}

} // namespace osp
