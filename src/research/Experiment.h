#pragma once

#include "core/AudioData.h"

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace osp::research
{

/**
    Blind listening experiments (spec §68, §71, §107–110): every source is rendered
    under several conditions (engine A, B or C with any instrument settings), the
    renders of one group are trimmed to a common length, level-matched and written
    with random names. key.json keeps the mapping and guard-rail metrics;
    listening.json holds only what a listener may see (groups, shuffled clip ids,
    rating scales, texts). research/listening/build.py turns one or more runs into
    the listening page.

    Plan format: docs/testing.md ("Listening experiments") and research/experiments/.
*/
struct ExperimentSummary
{
    int groups = 0;
    int clips = 0;
    std::vector<std::string> errors;
    std::filesystem::path keyFile;
};

ExperimentSummary runExperiment (const std::filesystem::path& planFile, const std::filesystem::path& outputDir,
                                 const std::function<void (const std::string&)>& log, std::string& error);

/**
    Objective repetition measure for long renders: the strongest average
    self-similarity of log-band spectra at lags 0.5–20 s, from `fromSeconds` on.
    A plain loop of the same material scores close to 1; non-repeating
    continuation scores lower. A guard rail only; listening decides.
*/
double repetitionScore (const AudioData& audio, double fromSeconds, double* lagSeconds = nullptr);

/**
    Click / seam detector: the loudest 3 ms window of second-difference energy
    relative to the median window, in dB, between fromSeconds and toSeconds. Steady
    material sits around 6-10 dB; audible clicks and hard seams show up as 20+ dB.
*/
double discontinuityDb (const AudioData& audio, double fromSeconds, double toSeconds, double* atSeconds = nullptr);

} // namespace osp::research
