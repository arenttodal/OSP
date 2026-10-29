#include "io/PhraseAnalysisCache.h"

#include "analysis/phrase/AnalysisJobs.h"
#include "io/NoteAtlasJson.h"

#include <cstdint>
#include <cstdio>

namespace osp::io
{

namespace
{
    /** FNV-1a: a short, file-name-safe tag for the version + settings part of the key. */
    std::string tagOf (const std::string& text)
    {
        std::uint64_t h = 1469598103934665603ULL;
        for (unsigned char c : text)
        {
            h ^= c;
            h *= 1099511628211ULL;
        }
        char buffer[17];
        std::snprintf (buffer, sizeof (buffer), "%016llx", static_cast<unsigned long long> (h));
        return buffer;
    }

    std::string hexOf (const std::string& contentHash)
    {
        return contentHash.rfind ("sha256:", 0) == 0 ? contentHash.substr (7) : contentHash;
    }
}

PhraseAnalysisCache::PhraseAnalysisCache (std::filesystem::path folder) : dir (std::move (folder)) {}

std::filesystem::path PhraseAnalysisCache::pathFor (const atlas::SourceAsset& source, const phrase::AnalysisSettings& settings) const
{
    return dir / (hexOf (source.contentHash) + "-" + tagOf (phrase::AnalysisQueue::keyOf (source, settings)) + ".andoratlas");
}

std::optional<atlas::NoteAtlas> PhraseAnalysisCache::find (const atlas::SourceAsset& source, const phrase::AnalysisSettings& settings) const
{
    if (source.contentHash.empty())
        return std::nullopt;
    const auto path = pathFor (source, settings);
    std::error_code ec;
    if (! std::filesystem::is_regular_file (path, ec))
        return std::nullopt;
    std::string error;
    auto atlas = readNoteAtlas (path, error);
    // The key is in the name; the contents must agree (a hand-copied or damaged file is ignored).
    if (! atlas || atlas->sources.empty() || atlas->sources.front().contentHash != source.contentHash
        || atlas->analysis.version != phrase::analysisVersion || atlas->analysis.settingsDigest != settings.digest())
        return std::nullopt;
    auto& s = atlas->sources.front();
    const auto frames = s.numFrames;
    const auto rate = s.sampleRate;
    const auto channels = s.numChannels;
    const auto id = s.assetId;
    s = source;
    s.assetId = id;
    s.numFrames = frames;
    s.sampleRate = rate;
    s.numChannels = channels;
    return atlas;
}

bool PhraseAnalysisCache::store (const atlas::NoteAtlas& atlas, const phrase::AnalysisSettings& settings, std::string& error) const
{
    if (atlas.sources.empty() || atlas.sources.front().contentHash.empty())
    {
        error = "an analysis without an identified recording cannot be cached";
        return false;
    }
    std::error_code ec;
    std::filesystem::create_directories (dir, ec);
    if (ec)
    {
        error = "cannot create " + dir.string() + ": " + ec.message();
        return false;
    }
    // Paths are hints of one session; the cache holds the analysis, not where the file was.
    auto copy = atlas;
    for (auto& s : copy.sources)
    {
        s.lastKnownPath.clear();
        s.relativePath.clear();
    }
    return writeNoteAtlas (pathFor (atlas.sources.front(), settings), copy, error);
}

} // namespace osp::io
