#pragma once

#include <juce_core/juce_core.h>

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace osp::research
{

struct CorpusEntry
{
    std::string id;              ///< "sha256:<hex>" of the file bytes (stable across renames/moves)
    std::string shortId;         ///< first 12 hex digits
    std::string relativePath;    ///< relative to the corpus root, '/' separated
    std::string filename;
    std::string extension;       ///< lower case, without dot
    std::uintmax_t sizeBytes = 0;
    bool supported = false;      ///< WAV/AIFF by extension
    std::string duplicateOf;     ///< relativePath of an earlier identical file, if any
    std::string error;           ///< hashing failure, if any

    /** Folder name used for reports/renders: "<sanitised stem>__<first 8 hex>". */
    std::string folderName() const;
};

struct CorpusIndex
{
    static constexpr int schemaVersion = 1;
    std::string root;
    std::vector<CorpusEntry> files;  ///< sorted by relativePath (deterministic)
};

/**
    Recursively scans a directory. Every regular file is listed (unsupported ones are
    marked, not dropped, so the summary can report them). Hidden files and files
    named index.json / README* are ignored.
*/
CorpusIndex buildCorpusIndex (const std::filesystem::path& root);

juce::var corpusIndexToJson (const CorpusIndex& index);

} // namespace osp::research
