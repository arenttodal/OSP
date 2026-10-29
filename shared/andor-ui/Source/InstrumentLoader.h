#pragma once

#include "LoadedInstrument.h"

#include "analysis/phrase/AnalysisJobs.h"
#include "core/AudioData.h"
#include "engine/AtlasSet.h"
#include "engine/SampleSetInference.h"
#include "io/PhraseAnalysisCache.h"
#include "model/NoteAtlas.h"

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

    /** Phase 4: where finished phrase analyses are kept (a subfolder: never mistaken for audio). */
    juce::File phraseAnalysisFolder() const { return dir.getChildFile ("phrase-analysis"); }

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

/** A multi-file drop or a recalled multi-sample session (Phase 7). */
struct SetLoadRequest
{
    std::vector<LoadRequest> files;          ///< one entry per file (file or hash/originalPath/filename)
    std::vector<SetAssignment> assignments;  ///< user corrections from the Samples inspector
};

/**
    A Note Atlas for one layer (unified engine Phase 3): its notes become a multi-sample
    instrument built from region copies of its source assets (engine/AtlasSet). The audio is
    found by content hash - the managed sample store first (where imports, Library assets and,
    later, finalised live captures live), then the atlas's path hints (relative to
    `atlasFolder`, the last known path, the managed copy, `searchFolders` by name). A found
    file must be the asset (same hash) or at least its shape (rate, channels, length).
*/
struct AtlasLoadRequest
{
    atlas::NoteAtlas atlas;
    std::string atlasFolder;                  ///< where a stand-alone .andoratlas was read from (relative hints)
    std::vector<std::string> searchFolders;   ///< extra folders to look in by file name
    /** Phase 4 capacity: region audio this layer may hold (seconds; the session's policy
        decides it from what the other layers hold). Beyond it notes are left out - listed,
        never silently - and the atlas keeps them all. */
    double capacitySeconds = 300.0;
    int threads = 2;                          ///< per-member work (results do not depend on it)
    /** Phase 5: sources already decoded (an edit of the atlas a layer plays): used as they are
        when their shape matches, so an edit never decodes or hashes the recording again. */
    std::vector<atlasset::SourceAudio> preloaded;
};

/**
    Phase 4: a recording to analyse into a Note Atlas for one layer. The audio is imported into
    the store by hash, decoded once, analysed (or its stored analysis reused), and built in two
    stages. A recording that does not suit a monophonic analysis (polyphonic, unpitched, silent,
    no notes) is loaded as one ordinary sample instead - playable as recorded - with the reason.
*/
struct AnalyseRequest
{
    juce::File file;
    phrase::AnalysisSettings settings;
    double capacitySeconds = 300.0;
    int threads = 2;
};

struct LoadResult
{
    std::shared_ptr<const LoadedInstrument> instrument;  ///< null on failure
    std::shared_ptr<const AudioData> audio;              ///< decoded source, kept for the next model stages
    std::string error;
    std::vector<std::string> warnings;
    /** Atlas loads: the sources whose audio was not found (the load then fails: a session must
        never look restored without its audio). "<asset id>: <name> (<hash>)". */
    std::vector<std::string> missingSources;
    /** Phase 4: an atlas load's Stage B input (the instrument published now is Stage A). */
    std::shared_ptr<const atlasset::PreparedAtlas> atlasPending;
    /** Phase 5: the request (load id) this answers, failures included (0: not told). */
    std::uint64_t requestId = 0;
    /** Phase 5: an atlas instrument's decoded sources (kept for its edits). */
    std::vector<atlasset::SourceAudio> atlasAudio;
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

/**
    Several files -> one multi-sample instrument: import + analyse every file, infer the
    set (pitch groups, layers, round robins) and build every member with continuation.
    Files that fail are skipped with a warning; fails only if no file loads.
*/
LoadResult loadInstrumentSet (const SetLoadRequest& request, SampleStore& store, std::uint64_t generation);

/**
    A Note Atlas -> one multi-sample instrument (Phase 3): finds and decodes every source the
    atlas plays (keeping a managed copy, by hash), builds the set from region copies. Fails -
    with `missingSources` - when any source's audio cannot be found or does not match.
*/
LoadResult loadInstrumentAtlas (const AtlasLoadRequest& request, SampleStore& store, std::uint64_t generation);

/**
    Phase 4, Stage B of an atlas load: the same instrument with every member's continuation.
    Background thread; never throws. A cancelled or failed stage leaves Stage A playing.
*/
LoadResult enhanceInstrumentAtlas (const LoadedInstrument& base, const atlasset::PreparedAtlas& pending, std::uint64_t generation,
                                   const std::atomic<bool>* cancel = nullptr, int threads = 2);

/**
    Phase 4: a recording -> phrase analysis (through `queue`, reusing `cache`) -> Stage A of
    its atlas instrument (Stage B follows with enhanceInstrumentAtlas). Falls back to the
    recording as one sample when it does not suit. Background thread (it waits for the
    analysis); `cancel` stops it. Never throws.
*/
LoadResult loadAnalysedRecording (const AnalyseRequest& request, SampleStore& store, phrase::AnalysisQueue& queue,
                                  const io::PhraseAnalysisCache& cache, std::uint64_t generation, const std::atomic<bool>* cancel = nullptr);

/** Re-infers the current set with new user assignments (no re-analysis). */
LoadResult reassignInstrumentSet (const LoadedInstrument& base, const std::vector<SetAssignment>& assignments, std::uint64_t generation);

} // namespace osp::plugin
