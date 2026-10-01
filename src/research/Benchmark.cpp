#include "research/Benchmark.h"

#include "audio/sampler/BaselineSampler.h"
#include "core/Prng.h"
#include "io/JsonUtil.h"
#include "model/PlaybackSource.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <numbers>

namespace osp::research
{

BenchmarkResult runBenchmark (const BenchmarkOptions& options)
{
    BenchmarkResult result;
    const double rate = options.sampleRate;
    const int block = std::max (1, options.blockSize);

    // Stereo source long enough that voices transposed up an octave never run out.
    const double sourceSeconds = 2.0 * options.seconds + 2.0;
    auto audio = AudioData::allocate (2, static_cast<std::int64_t> (sourceSeconds * 44100.0), 44100.0);
    Prng noise (7);
    for (std::size_t i = 0; i < audio.channels[0].size(); ++i)
    {
        const double t = static_cast<double> (i) / 44100.0;
        const double tone = 0.3 * std::sin (2.0 * std::numbers::pi * 220.0 * t) + 0.1 * std::sin (2.0 * std::numbers::pi * 440.0 * t);
        audio.channels[0][i] = static_cast<float> (tone + 0.02 * noise.bipolar());
        audio.channels[1][i] = static_cast<float> (tone + 0.02 * noise.bipolar());
    }

    auto settings = options.sampler;
    settings.polyphony = options.voices;

    BaselineSampler sampler;
    sampler.prepare (rate, block, settings);
    const PlaybackSource source (audio, 57.0, sampler.requiredSourcePadding());
    sampler.setSource (&source);

    std::vector<float> left (static_cast<std::size_t> (block)), right (static_cast<std::size_t> (block));
    float* channels[2] = { left.data(), right.data() };

    const int totalBlocks = static_cast<int> (std::ceil (options.seconds * rate / block));
    std::vector<double> times;
    times.reserve (static_cast<std::size_t> (totalBlocks));

    const int retriggerEvery = std::max (1, static_cast<int> (0.05 * rate / block));
    int retriggerNote = 69;
    int lastRetrigger = -1;

    double activeSum = 0.0;
    const auto wallStart = std::chrono::steady_clock::now();
    for (int b = 0; b < totalBlocks; ++b)
    {
        const auto t0 = std::chrono::steady_clock::now();

        // Held chord spread over two octaves. In dense mode two voices are left for the
        // retriggers, which keep stealing each other (released voices are stolen first),
        // so the sampler stays at full polyphony with constant voice turnover.
        const int held = options.denseRetriggers ? std::max (1, options.voices - 2) : options.voices;
        if (b == 0)
            for (int v = 0; v < held; ++v)
                sampler.noteOn (45 + (v * 7) % 24, 90);

        if (options.denseRetriggers && b > 0 && b % retriggerEvery == 0)
        {
            if (lastRetrigger >= 0)
                sampler.noteOff (lastRetrigger);
            retriggerNote = 69 + (retriggerNote - 69 + 5) % 12; // above the held chord's notes
            sampler.noteOn (retriggerNote, 100);
            lastRetrigger = retriggerNote;
        }

        sampler.render (channels, 2, block);
        const auto t1 = std::chrono::steady_clock::now();
        times.push_back (std::chrono::duration<double, std::micro> (t1 - t0).count());
        const int active = sampler.activeVoiceCount();
        result.peakActiveVoices = std::max (result.peakActiveVoices, active);
        activeSum += active;
    }
    result.wallSeconds = std::chrono::duration<double> (std::chrono::steady_clock::now() - wallStart).count();

    result.blocks = totalBlocks;
    result.meanActiveVoices = totalBlocks > 0 ? activeSum / totalBlocks : 0.0;
    result.renderedSeconds = totalBlocks * static_cast<double> (block) / rate;
    result.realtimeFactor = result.wallSeconds > 0.0 ? result.renderedSeconds / result.wallSeconds : 0.0;
    result.budgetMicros = 1.0e6 * block / rate;

    double sum = 0.0;
    for (double t : times)
        sum += t;
    result.meanBlockMicros = times.empty() ? 0.0 : sum / static_cast<double> (times.size());
    result.worstBlockMicros = times.empty() ? 0.0 : *std::max_element (times.begin(), times.end());
    if (! times.empty())
    {
        auto sorted = times;
        std::sort (sorted.begin(), sorted.end());
        result.p99BlockMicros = sorted[static_cast<std::size_t> (0.99 * static_cast<double> (sorted.size() - 1))];
    }
    result.meanBudgetPercent = 100.0 * result.meanBlockMicros / result.budgetMicros;
    result.worstBudgetPercent = 100.0 * result.worstBlockMicros / result.budgetMicros;
    return result;
}

juce::var benchmarkToJson (const BenchmarkOptions& options, const BenchmarkResult& r)
{
    using json::number;
    using json::set;
    auto root = json::object();
    set (root, "schemaVersion", 1);
    auto config = json::object();
    set (config, "sampleRate", number (options.sampleRate, 1));
    set (config, "blockSize", options.blockSize);
    set (config, "voices", options.voices);
    set (config, "seconds", number (options.seconds, 2));
    set (config, "denseRetriggers", options.denseRetriggers);
    set (config, "interpolationZeroCrossings", options.sampler.interpolationZeroCrossings);
    set (root, "config", config);
    set (root, "blocks", r.blocks);
    set (root, "renderedSeconds", number (r.renderedSeconds, 3));
    set (root, "wallSeconds", number (r.wallSeconds, 4));
    set (root, "realtimeFactor", number (r.realtimeFactor, 2));
    set (root, "budgetMicros", number (r.budgetMicros, 2));
    set (root, "meanBlockMicros", number (r.meanBlockMicros, 2));
    set (root, "p99BlockMicros", number (r.p99BlockMicros, 2));
    set (root, "worstBlockMicros", number (r.worstBlockMicros, 2));
    set (root, "meanBudgetPercent", number (r.meanBudgetPercent, 2));
    set (root, "worstBudgetPercent", number (r.worstBudgetPercent, 2));
    set (root, "peakActiveVoices", r.peakActiveVoices);
    set (root, "meanActiveVoices", number (r.meanActiveVoices, 2));
    return root;
}

} // namespace osp::research
