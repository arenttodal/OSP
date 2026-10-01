#pragma once

#include "model/AnalysisData.h"

#include <optional>
#include <string>

namespace osp
{

/** Which root a source is played at, and why. Shared by the renderer and the plugin. */
struct RootChoice
{
    double rootMidi = 60.0;     ///< fractional: includes the detected cents offset
    std::string origin;         ///< "override", "analysis", "analysis-low-confidence", "fallback"
    double sourceF0Hz = 0.0;    ///< measured source F0 when confidently detected (0 if unknown)
};

/**
    Resolves the playback root: explicit override > detected pitch > low-confidence
    estimate > C4 fallback. A low-confidence choice is labelled so it is never mistaken
    for a certain one.
*/
RootChoice chooseRoot (const AnalysisData* analysis, std::optional<double> overrideMidi);

/** Short musician-facing description from analysis, e.g. "SUSTAINED · TONAL". */
std::string describeCharacter (const AnalysisData& analysis);

} // namespace osp
