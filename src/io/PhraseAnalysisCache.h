#pragma once

#include "analysis/phrase/PhraseAnalysis.h"
#include "model/NoteAtlas.h"

#include <filesystem>
#include <optional>
#include <string>

namespace osp::io
{

/**
    Finished phrase analyses on disk (unified engine Phase 4), so a recording is analysed once:
    reopening it, using it on another layer or in another session reads the stored atlas. The
    key is the recording's content hash, the analysis version and the settings digest
    (phrase::AnalysisQueue::keyOf) - a newer analysis, other settings or other audio never read
    a stale entry. One .andoratlas per key, written atomically.

    Only fresh analyses are stored here, never the musician's corrections: those live in the
    session (its Atlas) and survive a re-analysis through phrase::edit::mergeReanalysisReported.
    The plugin keeps this folder inside its sample store ("phrase-analysis/").
*/
class PhraseAnalysisCache
{
public:
    explicit PhraseAnalysisCache (std::filesystem::path folder);

    /** The stored analysis of this recording with these settings, or nothing (absent, stale,
        unreadable). Its source record is the caller's (name, path, origin): the audio is the same. */
    std::optional<atlas::NoteAtlas> find (const atlas::SourceAsset& source, const phrase::AnalysisSettings& settings) const;

    /** Stores a fresh analysis (its first source is the recording). */
    bool store (const atlas::NoteAtlas& atlas, const phrase::AnalysisSettings& settings, std::string& error) const;

    std::filesystem::path pathFor (const atlas::SourceAsset& source, const phrase::AnalysisSettings& settings) const;
    const std::filesystem::path& folder() const noexcept { return dir; }

private:
    std::filesystem::path dir;
};

} // namespace osp::io
