#pragma once

#include "research/PedalLoopLab.h"

#include <filesystem>
#include <string>
#include <vector>

namespace osp::research
{

struct PedalReimaginedResult
{
    std::string markdown;   ///< the report (every render's measurements, CPU)
    int renders = 0;
    bool allFinite = true;
};

/**
    PEDAL LOOP x REIMAGINED (docs/research/pedal-reimagined), the listening matrix of the spec's
    section 16 for every source and every REIMAGINED mode: A (REIMAGINED off, the pedal loop),
    B (50 %, the pedal loop), C (100 %, the pedal loop), each with Forward and Ping-Pong, and D
    (100 %, Normal). One performance: the pedal down, the note at 0.1 s, the key up at 1 s, the
    pedal up at 8 s, 2 s more for the natural release. Measures the sustained level (7-8 s),
    the worst click over the held loop (high-passed energy in 5 ms against the 40 ms around
    it), the largest rise in 10 ms level (a repeated attack), the quietest 50 ms against the
    held level (an unexpected silence), the level step at pedal up, and the voices left at the
    end. Writes 24-bit WAVs under `outDir` when `writeAudio`.
*/
PedalReimaginedResult runPedalReimaginedLab (const std::vector<PedalLabSource>& sources, const std::filesystem::path& outDir, bool writeAudio);

/**
    CPU of the pedal loop in every REIMAGINED mode at 100 %: 16 notes held under the pedal,
    Normal against Forward and Ping-Pong over the same window (every voice sounding), 48 kHz.
    Share of one core, median of `repeats`.
*/
std::string runPedalReimaginedBenchmark (const PedalLabSource& source, int repeats = 3);

} // namespace osp::research
