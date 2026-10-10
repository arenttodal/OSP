#pragma once

#include "library/Catalog.h"
#include "library/SoundImport.h"

#include <atomic>
#include <filesystem>
#include <mutex>
#include <string>

namespace osp::library
{

/**
    Scanning a folder the user chose (Stage 6.3/6.4). Never a whole drive on its own: only
    roots the user added (Catalog::addScanRoot).

    Two stages:
      - fast: walk the folder (recursively unless told not to; hidden files and folders
        skipped), keep supported audio that the include/exclude patterns allow, read each
        header (format, rate, channels, length) and record it. Unchanged files (same size and
        time) are skipped, files gone from disk are forgotten. Search results are there at once.
      - deep: hash and analyse each new or changed file in the background, classify it
        (Classifier) and keep the answer by content, so the same bytes are never analysed twice.

    A candidate is not a Library sound until the user imports it (importCandidate). Nothing
    is ever deleted or changed on disk.

    Background thread only, with its own catalog connection. Pausable and cancellable
    between files; a cancel keeps everything done so far.
*/
struct ScanControl
{
    std::atomic<bool> paused { false }, cancelled { false };
};

struct ScanProgress
{
    std::atomic<int> stage { 0 };   ///< 0 idle, 1 indexing, 2 analysing, 3 done, 4 cancelled
    std::atomic<int> found { 0 }, indexed { 0 }, unchanged { 0 }, analysed { 0 }, failed { 0 }, skipped { 0 };
    std::atomic<int> likelyOneShots { 0 }, possibleOneShots { 0 }, multipleEvents { 0 }, loops { 0 }, longRecordings { 0 };
    std::atomic<int> toAnalyse { 0 };
    void setCurrent (const std::string& file)
    {
        const std::lock_guard<std::mutex> lock (mutex);
        current = file;
    }
    std::string currentFile() const
    {
        const std::lock_guard<std::mutex> lock (mutex);
        return current;
    }
    void reset();

private:
    mutable std::mutex mutex;
    std::string current;
};

struct ScanSettings
{
    bool recursive = true;
    bool deep = true;
    double skipLongerThanSeconds = 0.0;   ///< > 0: longer files are indexed but not analysed
};

struct ScanSummary
{
    int found = 0, indexed = 0, unchanged = 0, removed = 0, analysed = 0, failed = 0, skipped = 0;
    bool cancelled = false;
    std::string error;                    ///< the folder itself could not be read (disconnected drive, permission)
};

/** Scans one root (see above). */
ScanSummary scanFolder (Catalog& catalog, const ScanRoot& root, const ScanSettings& settings, ScanControl& control, ScanProgress& progress);

/** Whether a file passes the root's patterns: ';'-separated, '*' and '?' wildcards, matched
    against the file name and its path inside the root (case-insensitive). */
bool passesPatterns (const std::filesystem::path& relative, const std::string& include, const std::string& exclude);

/** Imports a candidate with what is known about it: tags from its name and folders
    (inferred), the audio's tags and Type, a root from its name when the audio agrees (else the
    audio's own), into `collectionId` if given. */
ImportResult importCandidate (Catalog& catalog, const std::filesystem::path& storeDir, const ScanFile& candidate, const std::string& rootPath,
                              const std::string& collectionId = {}, Origin origin = Origin::user);

/** Classifies a content once (a newer classifier version analyses it again): the analysis is
    stored by content, so the same bytes anywhere are analysed once. */
bool analyseContent (Catalog& catalog, const std::string& path, const std::string& contentHash, std::string& error);

/** An import request carrying a file's evidence: its name's and folders' tags (below `rootPath`),
    the analysis' audio tags, a Type (name, then folders, then audio) and a root (the name's when
    the audio agrees within a semitone, else the audio's). */
ImportRequest requestWithEvidence (const Catalog& catalog, const std::filesystem::path& file, const std::string& contentHash, const std::string& rootPath);

/** The JSON fields importCandidate reads back from an analysis (small and fixed). */
std::string jsonString (const std::string& json, const std::string& key);
double jsonNumber (const std::string& json, const std::string& key, double fallback = 0.0);
std::vector<std::string> jsonStrings (const std::string& json, const std::string& key);

} // namespace osp::library
