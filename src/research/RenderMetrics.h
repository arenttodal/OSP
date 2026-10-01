#pragma once

#include "core/AudioData.h"
#include "midi/MidiEvent.h"

#include <juce_core/juce_core.h>

#include <string>
#include <vector>

namespace osp::research
{

struct Issue
{
    std::string severity;  ///< "error" or "warning"
    std::string code;      ///< stable identifier, e.g. "non-finite", "clipping"
    std::string message;
};

/** Pitch check of one isolated note in a render. */
struct NoteCheck
{
    double timeSeconds = 0.0;
    int note = 0;
    double expectedHz = 0.0;
    double detectedHz = 0.0;     ///< 0 if nothing detected
    double centsError = 0.0;
    double confidence = 0.0;
    bool evaluated = false;      ///< false when the expected pitch is outside the detector's reliable range
};

struct RenderMetrics
{
    static constexpr int schemaVersion = 1;

    double sampleRate = 0.0;
    int channels = 0;
    std::int64_t frames = 0;
    double durationSeconds = 0.0;
    double peakDbfs = -200.0;
    double rmsDbfs = -200.0;
    std::vector<double> dcOffset;     ///< mean per channel (linear)
    std::int64_t nanCount = 0;
    std::int64_t infCount = 0;
    std::int64_t clippedSamples = 0;  ///< |x| >= 1.0
    double centroidMeanHz = 0.0;
    double centroidStdHz = 0.0;
    double outputPitchHz = 0.0;       ///< whole-render estimate; only computed when one pitch is played (else 0)
    double outputPitchConfidence = 0.0;
    std::vector<NoteCheck> notes;
    double maxAbsCentsError = 0.0;    ///< over confidently detected isolated notes
    std::string sampleHash;           ///< sha256 of the raw float samples (determinism checks)
    std::vector<Issue> issues;

    /** "error" if any error issue, else "warning" if any warning, else "ok". */
    std::string status() const;
};

struct MetricsContext
{
    int expectedChannels = 2;
    double expectedSampleRate = 0.0;     ///< 0 = don't check
    const MidiSequence* sequence = nullptr;
    double sourceF0Hz = 0.0;             ///< measured source F0; 0 disables note checks
    double rootMidi = 60.0;
    double pitchToleranceCents = 50.0;
};

/** Safety checks + summary metrics for any render. Never throws. */
RenderMetrics computeMetrics (const AudioData& audio, const MetricsContext& context);

juce::var metricsToJson (const RenderMetrics& metrics);

} // namespace osp::research
