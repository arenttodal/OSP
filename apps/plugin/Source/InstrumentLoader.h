#pragma once

#include "LoadedInstrument.h"

#include <juce_core/juce_core.h>

#include <memory>
#include <optional>
#include <string>

namespace osp::plugin
{

/**
    Application-managed sample storage (spec §53): every imported file is copied once,
    named by its content hash, so sessions survive the original file moving. Analysis
    results are cached next to it (versioned JSON) so recall does not re-analyse.
*/
class SampleStore
{
public:
    explicit SampleStore (juce::File directory);

    /** Default location: <user app data>/OSP/Samples (env OSP_SAMPLE_STORE overrides). */
    static juce::File defaultDirectory();

    const juce::File& directory() const noexcept { return dir; }

    /** Copies `file` into the store if needed. Returns the stored file, or nullopt on failure. */
    std::optional<juce::File> import (const juce::File& file, const std::string& sha256Hex, std::string& error);

    /** Finds a stored file by hash (any extension). */
    std::optional<juce::File> find (const std::string& sha256Hex) const;

    juce::File analysisCacheFor (const std::string& sha256Hex) const;

private:
    juce::File dir;
};

struct LoadRequest
{
    juce::File file;                   ///< file to import (drop / file chooser)
    std::string expectedHash;          ///< state recall: look up the store first
    std::string originalPath;          ///< state recall: fallback location
    std::string filename;              ///< state recall: display name
};

struct LoadResult
{
    std::shared_ptr<const LoadedInstrument> instrument;  ///< null on failure
    std::string error;
    std::vector<std::string> warnings;
};

/**
    Import + analyse + prepare playback data. Runs on a background thread (allocates,
    reads files, analyses). Never throws.
*/
LoadResult loadInstrument (const LoadRequest& request, SampleStore& store, std::uint64_t generation);

} // namespace osp::plugin
