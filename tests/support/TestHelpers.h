#pragma once

#include "core/AudioData.h"
#include "model/AnalysisData.h"

#include <filesystem>
#include <string>

namespace osp::test
{

/** A fresh, uniquely named directory under the system temp dir, removed on destruction. */
class TempDir
{
public:
    explicit TempDir (const std::string& prefix = "osp-test");
    ~TempDir();
    TempDir (const TempDir&) = delete;
    TempDir& operator= (const TempDir&) = delete;

    const std::filesystem::path& path() const noexcept { return dir; }
    std::filesystem::path operator/ (const std::string& name) const { return dir / name; }

private:
    std::filesystem::path dir;
};

/** Full analysis of in-memory audio. */
AnalysisData analyse (const AudioData& audio);

/** RMS over all channels and samples. */
double rms (const AudioData& audio);

/** Largest absolute sample difference (audio must match in shape). */
double maxDifference (const AudioData& a, const AudioData& b);

} // namespace osp::test
