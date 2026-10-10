#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace juce
{
class ZipFile;
}

namespace osp::library
{

/**
    Reading and writing the Library's archives (packs, Stage 10) and checking what comes in
    from outside (packs, backups). Everything read is untrusted:
    - Entry names must be plain relative paths. No absolute path, drive letter, "..",
      backslash, control character or hidden component, and not too deep.
    - Links are refused.
    - Sizes are capped.
    - Text is cleaned before it is shown or stored.
    Nothing in an archive is ever run.

    Background threads only (file I/O).
*/
struct ArchiveLimits
{
    int maxEntries = 60000;                         ///< a zip without zip64 holds at most 65,535
    std::int64_t maxEntryBytes = 2LL << 30;         ///< 2 GiB per file
    std::int64_t maxTotalBytes = 4LL << 30;         ///< 4 GiB in all (the zip format's own limit)
    std::int64_t maxManifestBytes = 8LL << 20;      ///< 8 MiB of JSON
    int maxDepth = 8;                               ///< folders below the top
};

/** A path inside an archive or a backup that is safe below a folder; `why` says what is wrong. */
bool safeRelativePath (const std::string& path, int maxDepth, std::string& why);

/** Text from outside: valid UTF-8 (anything else replaced), control characters removed (new
    lines kept when `multiline`), trimmed, at most `maxBytes`. */
std::string cleanText (const std::string& text, std::size_t maxBytes, bool multiline = false);

/** A pack id: lower-case letters, digits, '.', '_' and '-', 1-100 characters, not starting
    with '.' or '-'. */
bool validPackId (const std::string& id);

/** "1", "1.2", "1.2.3" (up to four numbers). */
bool validVersion (const std::string& version);
/** < 0, 0, > 0 like strcmp, number by number ("1.10" > "1.9"). */
int compareVersions (const std::string& a, const std::string& b);

/** A zip archive opened as untrusted: refused whole if any entry is unsafe. */
class ArchiveReader
{
public:
    ArchiveReader (const std::filesystem::path& file, const ArchiveLimits& limits = {});
    ~ArchiveReader();

    bool ok() const noexcept { return error.empty(); }
    const std::string& problem() const noexcept { return error; }

    bool has (const std::string& name) const { return index.count (name) > 0; }
    std::int64_t size (const std::string& name) const;
    std::vector<std::string> names() const;
    /** A small text entry (the manifest), at most `maxBytes`. */
    std::optional<std::string> readText (const std::string& name, std::int64_t maxBytes) const;
    /** Writes an entry to `target` (through `target.partial`, then renamed). Its size must be
        `expectedBytes` (< 0: any) and its SHA-256 `expectedSha256` (empty: not checked). */
    bool extract (const std::string& name, const std::filesystem::path& target, std::int64_t expectedBytes, const std::string& expectedSha256,
                  std::string& why) const;

private:
    std::unique_ptr<juce::ZipFile> zip;
    std::map<std::string, std::pair<int, std::int64_t>> index;   // name -> entry, size
    ArchiveLimits limits;
    std::string error;
};

/** Writes a zip archive (through `out.partial`, then renamed: never a half archive). */
class ArchiveWriter
{
public:
    ArchiveWriter();
    ~ArchiveWriter();
    void addFile (const std::filesystem::path& source, const std::string& name, bool compress);
    void addText (const std::string& name, const std::string& text);
    bool writeTo (const std::filesystem::path& out, std::string& why);

private:
    struct Entry
    {
        std::filesystem::path source;
        std::string name, text;
        bool compress = false, isText = false;
    };
    std::vector<Entry> entries;
};

} // namespace osp::library
