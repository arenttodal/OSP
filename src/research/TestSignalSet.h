#pragma once

#include "core/AudioData.h"

#include <filesystem>
#include <string>
#include <vector>

namespace osp::research
{

struct GeneratedSignal
{
    std::string filename;
    double expectedHz = 0.0;   ///< 0 = no pitch expected (noise, silence, impulse, broken)
    bool expectDecodable = true;
};

/**
    Writes the synthetic ground-truth set (various formats/rates/channel counts) plus
    deliberately broken and unsupported files, for exercising the loader, analysis and
    the corpus runner without the private corpus.
*/
/** Minimal AIFF-C 'fl32' writer (JUCE only writes integer AIFF); used to test float AIFF decoding. */
bool writeAifcFloat32 (const std::filesystem::path& path, const AudioData& audio, std::string& error);

std::vector<GeneratedSignal> writeTestSignalSet (const std::filesystem::path& directory, std::string& error);

} // namespace osp::research
