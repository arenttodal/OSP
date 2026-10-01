#include "support/TestHelpers.h"

#include "analysis/Analyzer.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>

namespace osp::test
{

TempDir::TempDir (const std::string& prefix)
{
    static std::atomic<int> counter { 0 };
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    dir = std::filesystem::temp_directory_path() / (prefix + "-" + std::to_string (stamp) + "-" + std::to_string (counter++));
    std::filesystem::create_directories (dir);
}

TempDir::~TempDir()
{
    std::error_code ec;
    std::filesystem::remove_all (dir, ec);
}

AnalysisData analyse (const AudioData& audio)
{
    return Analyzer::analyse (audio);
}

double rms (const AudioData& audio)
{
    double sum = 0.0;
    std::size_t count = 0;
    for (const auto& ch : audio.channels)
        for (float s : ch)
        {
            sum += static_cast<double> (s) * s;
            ++count;
        }
    return count > 0 ? std::sqrt (sum / static_cast<double> (count)) : 0.0;
}

double maxDifference (const AudioData& a, const AudioData& b)
{
    double diff = 0.0;
    for (std::size_t c = 0; c < std::min (a.channels.size(), b.channels.size()); ++c)
        for (std::size_t i = 0; i < std::min (a.channels[c].size(), b.channels[c].size()); ++i)
            diff = std::max (diff, static_cast<double> (std::abs (a.channels[c][i] - b.channels[c][i])));
    return diff;
}

} // namespace osp::test
