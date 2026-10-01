#pragma once

#include "analysis/AnalysisFrames.h"
#include "core/AudioData.h"
#include "model/AnalysisData.h"

#include <filesystem>
#include <string>

namespace osp::research
{

struct AnalysedSource
{
    bool ok = false;
    std::string error;
    AudioData audio;
    AnalysisData analysis;
};

/** Load (WAV/AIFF) + hash + analyse one file. Never throws; failures are returned. */
AnalysedSource loadAndAnalyse (const std::filesystem::path& path, const AnalysisOptions& options = {});

} // namespace osp::research
