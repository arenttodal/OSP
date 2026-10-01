#pragma once

#include "research/RenderConfig.h"

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace osp::research
{

struct CorpusRunOptions
{
    std::filesystem::path corpusDir;
    std::filesystem::path reportsDir = "research/reports";
    std::filesystem::path rendersDir = "research/renders";
    std::vector<std::string> fixtures;              ///< from a profile
    std::string profileName = "standard";
    std::vector<EngineId> engines { EngineId::baselineA };
    RenderConfig config {};
    int jobs = 1;
    bool writeAudio = true;                         ///< false: metrics only (faster CI)
    std::function<void (const std::string&)> log;  ///< progress lines (may be called from worker threads, serialised)
};

struct CorpusRunSummary
{
    int totalFiles = 0;
    int supportedFiles = 0;
    int unsupportedFiles = 0;
    int duplicateFiles = 0;
    int analysed = 0;
    int failed = 0;
    int pitchHigh = 0;
    int pitchModerate = 0;
    int pitchLow = 0;
    int pitchNone = 0;
    int rendersOk = 0;
    int rendersWarning = 0;
    int rendersError = 0;
    double wallSeconds = 0.0;
    std::filesystem::path summaryJson;
    std::filesystem::path summaryMarkdown;
};

/**
    Index -> analyse -> render fixtures -> metrics, for every supported file. One
    failing file never stops the run; it is recorded in the summary. Output:

        <reports>/index.json
        <reports>/summary.json, summary.md
        <reports>/<folder>/analysis.json
        <reports>/<folder>/<fixture>[.<engine>].metrics.json
        <renders>/<folder>/<fixture>[.<engine>].wav
*/
CorpusRunSummary runCorpus (const CorpusRunOptions& options);

} // namespace osp::research
