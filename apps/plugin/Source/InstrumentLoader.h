#pragma once

#include "LoadedInstrument.h"

#include "core/AudioData.h"

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

    /** State recall: the playback model the session was saved with. Restored exactly so a
        project sounds identical after reopening, even if the analyser has changed since. */
    struct SavedPlayback
    {
        double rootMidi = 60.0;
        std::string rootOrigin;
        double startSeconds = 0.0;
        double gainDb = 0.0;
    };
    std::optional<SavedPlayback> savedPlayback;
};

struct LoadResult
{
    std::shared_ptr<const LoadedInstrument> instrument;  ///< null on failure
    std::shared_ptr<const AudioData> audio;              ///< decoded source, kept for the next model stages
    std::string error;
    std::vector<std::string> warnings;
};

/**
    Stage 1: import + analyse + provisional (playable) model. Runs on a background thread
    (allocates, reads files, analyses). Never throws.
*/
LoadResult loadInstrument (const LoadRequest& request, SampleStore& store, std::uint64_t generation);

/**
    Stages 2 and 3: the same instrument with continuation (stage 2) or register anchors
    added (stage 3). Background thread; never throws (returns the error instead).
*/
LoadResult refineInstrument (const LoadedInstrument& base, std::shared_ptr<const AudioData> audio, std::uint64_t generation);

} // namespace osp::plugin
