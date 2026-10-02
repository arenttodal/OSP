#pragma once

#include "analysis/continuation/ContinuationAnalyzer.h"
#include "core/AudioData.h"
#include "model/InstrumentModel.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace osp
{

struct InstrumentBuildOptions
{
    int interpolationZeroCrossings = 16;
    PlaybackOptions playback { true, 0.03, true };   ///< start at the onset, level matched
    std::optional<double> rootOverrideMidi;
    ContinuationOptions continuation;
    std::vector<double> anchorOffsets { -24.0, -12.0, 12.0, 24.0 };
    std::uint64_t seed = 1;
};

/**
    Builds InstrumentModels in the staged order of spec §62. Every stage returns a new,
    complete, immutable model that can be published to the audio thread; audio buffers
    are shared between stages, not copied. All functions allocate and may take time:
    call them from a worker thread, never from the audio thread.
*/
namespace instrument
{
    /** Stage 1: decode + root + onset -> playable like the baseline sampler. */
    std::shared_ptr<InstrumentModel> buildProvisional (const AudioData& audio, const AnalysisData& analysis,
                                                       const InstrumentBuildOptions& options);

    /** Stage 2: + continuation (sustain region, jumps, release graft). */
    std::shared_ptr<InstrumentModel> addContinuation (const InstrumentModel& model, const AudioData& audio,
                                                      const InstrumentBuildOptions& options);

    /** Stage 3: + register anchors (Natural pitch character), each with aligned jumps. */
    std::shared_ptr<InstrumentModel> addAnchors (const InstrumentModel& model, const AudioData& audio,
                                                 const InstrumentBuildOptions& options);

    /** All stages at once (research renderer, tests). */
    std::shared_ptr<InstrumentModel> buildComplete (const AudioData& audio, const AnalysisData& analysis,
                                                    const InstrumentBuildOptions& options, bool withAnchors);

    /** Soft source-behaviour memberships from analysis (spec §8). */
    SourceCharacter estimateCharacter (const AnalysisData& analysis, const ContinuationModel* continuation);

    /** Performance and dynamics calibration from the character (spec §80, §34). */
    PerformanceProfile calibratePerformance (const SourceCharacter& character, const AnalysisData& analysis);
    DynamicsProfile calibrateDynamics (const SourceCharacter& character, const AnalysisData& analysis);
}

} // namespace osp
