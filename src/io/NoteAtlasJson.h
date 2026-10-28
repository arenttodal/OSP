#pragma once

#include "model/NoteAtlas.h"

#include <juce_core/juce_core.h>

#include <filesystem>
#include <optional>
#include <string>

namespace osp::io
{

/** File extension of a stand-alone Note Atlas (readable JSON; the audio stays a separate file). */
inline constexpr const char* noteAtlasExtension = ".andoratlas";

/** Serialises an atlas (schema: docs/research/unified-andor/phase-3-report.md). Ported from ANDOR Lab
    (andor-lab eeb5f39, src/andor/io). Frame positions and every value playback reads are exact. */
juce::var noteAtlasToJson (const atlas::NoteAtlas& atlas);

/**
    Parses an atlas. Fails with a clear message when the format tag or schemaVersion is missing
    or newer than this build understands. Version 1 (ANDOR Lab's) is migrated: its notes are
    analysis notes when they name a method (else user notes), velocity layer 0, sources file
    imports, the default mapping. Unknown fields are ignored, so later builds may add fields
    without breaking older readers (adding a field is documented, not versioned).
*/
std::optional<atlas::NoteAtlas> noteAtlasFromJson (const juce::var& json, std::string& error);

std::string noteAtlasToString (const atlas::NoteAtlas& atlas);
std::optional<atlas::NoteAtlas> noteAtlasFromString (const std::string& text, std::string& error);

/**
    Writes `atlas` to `path`. Each source's relativePath is rewritten relative to the atlas
    file's folder when the source's lastKnownPath is set, so a folder holding the atlas and
    its audio can be moved as a whole.
*/
bool writeNoteAtlas (const std::filesystem::path& path, const atlas::NoteAtlas& atlas, std::string& error);
std::optional<atlas::NoteAtlas> readNoteAtlas (const std::filesystem::path& path, std::string& error);

/**
    Finds a source's audio: the relative path (against `atlasFolder`), the last known path,
    the managed copy, then `searchFolders` by file name. A found file must match the source's
    fingerprint (exact) or at least its rate / channels / length (compatible); the result says
    which. nullopt when nothing usable was found.
*/
struct ResolvedSource
{
    std::filesystem::path path;
    atlas::RelinkResult match = atlas::RelinkResult::incompatible;
};
std::optional<ResolvedSource> resolveSource (const atlas::SourceAsset& source, const std::filesystem::path& atlasFolder,
                                             const std::vector<std::filesystem::path>& searchFolders = {});

/** Points a source at a new file after checking it (relink). False with `error` when the file
    cannot be read or is incompatible (the atlas is unchanged then). */
bool relinkSource (atlas::NoteAtlas& atlas, const std::string& assetId, const std::filesystem::path& file, std::string& error,
                   atlas::RelinkResult* matchOut = nullptr);

} // namespace osp::io
