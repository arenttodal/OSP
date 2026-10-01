#pragma once

#include "model/AnalysisData.h"

#include <juce_core/juce_core.h>

#include <filesystem>
#include <optional>
#include <string>

namespace osp::io
{

/** Serialises an AnalysisData to the versioned JSON report (see docs/analysis-schema.md). */
juce::var analysisToJson (const AnalysisData& data);

/**
    Parses a report. Fails with a clear message for a missing/unsupported schemaVersion.
    Older schema versions are migrated here when they exist (currently only v1).
*/
std::optional<AnalysisData> analysisFromJson (const juce::var& json, std::string& error);

bool writeAnalysis (const std::filesystem::path& path, const AnalysisData& data, std::string& error);
std::optional<AnalysisData> readAnalysis (const std::filesystem::path& path, std::string& error);

} // namespace osp::io
