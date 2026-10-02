#pragma once

#include "research/RenderConfig.h"

#include <juce_core/juce_core.h>

namespace osp::research
{

struct BenchmarkOptions
{
    double sampleRate = 48000.0;
    int blockSize = 128;
    int voices = 24;              ///< polyphony
    double seconds = 20.0;        ///< rendered duration
    bool denseRetriggers = true;  ///< voices-2 held notes + a new short note every 50 ms (constant stealing);
                                  ///< false: `voices` held notes only
    SamplerSettings sampler {};
    bool instrumentEngine = false;  ///< engine C (continuation, performance, dynamics, post) instead of baseline A
    EngineSettings engine {};       ///< engine C settings (default macros)
};

struct BenchmarkResult
{
    int blocks = 0;
    double renderedSeconds = 0.0;
    double wallSeconds = 0.0;
    double realtimeFactor = 0.0;      ///< rendered seconds per wall second
    double budgetMicros = 0.0;        ///< block duration
    double meanBlockMicros = 0.0;
    double p99BlockMicros = 0.0;
    double worstBlockMicros = 0.0;
    double meanBudgetPercent = 0.0;
    double worstBudgetPercent = 0.0;
    int peakActiveVoices = 0;
    double meanActiveVoices = 0.0;
};

/**
    Measures the baseline sampler (or engine C) exactly as a host would drive it: one
    call per block, timing each callback individually (mean, p99, worst).
*/
BenchmarkResult runBenchmark (const BenchmarkOptions& options);

juce::var benchmarkToJson (const BenchmarkOptions& options, const BenchmarkResult& result);

} // namespace osp::research
