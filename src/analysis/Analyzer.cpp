#include "analysis/Analyzer.h"

#include "analysis/loudness/EnvelopeAnalyzer.h"
#include "analysis/onset/OnsetDetector.h"
#include "analysis/pitch/PitchAnalyzer.h"
#include "analysis/spectrum/SpectralAnalyzer.h"
#include "analysis/stereo/StereoAnalyzer.h"

#include <cmath>

namespace osp
{

AnalysisData Analyzer::analyse (const AudioData& audio, const AnalysisOptions& options)
{
    AnalysisData data;
    data.source.sampleRate = audio.sampleRate;
    data.source.channels = audio.numChannels();
    data.source.frames = audio.numFrames();
    data.source.durationSeconds = audio.durationSeconds();

    if (audio.isEmpty())
    {
        data.warnings.push_back ("source contains no audio");
        data.envelope.rmsDb.hopSeconds = options.hopSeconds;
        return data;
    }

    if (audio.durationSeconds() > options.maxAnalysisSeconds)
        data.warnings.push_back ("source longer than " + std::to_string (static_cast<int> (options.maxAnalysisSeconds))
                                 + " s; analysis covers the beginning only");

    const auto frames = AnalysisFrames::build (audio, options);

    PitchFrames pitchFrames;
    data.pitch = PitchAnalyzer::analyse (frames, options, pitchFrames);
    data.envelope = EnvelopeAnalyzer::analyse (audio, frames);

    std::vector<double> flux;
    data.spectral = SpectralAnalyzer::analyse (frames, pitchFrames, flux);
    data.envelope.onsets = OnsetDetector::detect (frames, flux);
    data.stereo = StereoAnalyzer::analyse (audio, options.maxAnalysisSeconds);

    if (! frames.anyActive())
        data.warnings.push_back ("source is silent or nearly silent (max RMS below -100 dBFS)");
    else if (! data.pitch.detected)
        data.warnings.push_back (data.pitch.midiNote >= 0
                                     ? "pitch uncertain (low confidence); root should be confirmed manually"
                                     : "no pitch detected; root must be chosen manually");

    if (audio.durationSeconds() < 0.05)
        data.warnings.push_back ("source is extremely short (< 50 ms)");

    if (data.envelope.peakDbfs >= -0.01)
        data.warnings.push_back ("source peaks at or above 0 dBFS (possible clipping in the recording)");

    return data;
}

} // namespace osp
