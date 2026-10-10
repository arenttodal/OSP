#include "library/SoundImport.h"

#include "io/AudioFileIO.h"
#include "io/ContentHash.h"

#include <algorithm>
#include <cctype>
#include <fstream>

namespace osp::library
{

namespace
{
    std::string lowerExtension (const std::filesystem::path& p)
    {
        auto ext = p.extension().string();
        std::transform (ext.begin(), ext.end(), ext.begin(), [] (unsigned char c) { return static_cast<char> (std::tolower (c)); });
        return ext;
    }

    bool cancelled (const std::atomic<bool>* cancel) { return cancel != nullptr && cancel->load (std::memory_order_relaxed); }

    /** A name for this copy alone (instances importing the same bytes never share one). */
    std::filesystem::path partialName (const std::filesystem::path& target)
    {
        static std::atomic<unsigned> counter { 0 };
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        return target.parent_path() / (target.filename().string() + "." + std::to_string (stamp) + "-" + std::to_string (counter++) + ".partial");
    }
}

std::string hexOf (const std::string& contentHash)
{
    return contentHash.rfind ("sha256:", 0) == 0 ? contentHash.substr (7) : contentHash;
}

std::optional<std::filesystem::path> findStored (const std::filesystem::path& storeDir, const std::string& sha256Hex)
{
    std::error_code ec;
    if (sha256Hex.empty() || ! std::filesystem::is_directory (storeDir, ec))
        return std::nullopt;
    // The common extensions first (no directory walk), then anything else with that stem.
    for (const char* ext : { ".wav", ".aif", ".aiff", ".flac", ".wave", ".aifc" })
    {
        const auto candidate = storeDir / (sha256Hex + ext);
        if (std::filesystem::is_regular_file (candidate, ec))
            return candidate;
    }
    for (const auto& entry : std::filesystem::directory_iterator (storeDir, ec))
    {
        const auto name = entry.path().filename().string();
        if (name.rfind (sha256Hex + ".", 0) == 0 && name.find (".partial") == std::string::npos && name.find (".analysis.json") == std::string::npos
            && entry.is_regular_file (ec))
            return entry.path();
    }
    return std::nullopt;
}

int clearInterruptedImports (const std::filesystem::path& storeDir, std::chrono::seconds age)
{
    std::error_code ec;
    int removed = 0;
    const auto cutoff = std::filesystem::file_time_type::clock::now() - age;
    for (const auto& entry : std::filesystem::directory_iterator (storeDir, ec))
    {
        const auto name = entry.path().filename().string();
        if (name.size() < 8 || name.compare (name.size() - 8, 8, ".partial") != 0)
            continue;
        if (entry.last_write_time (ec) <= cutoff && std::filesystem::remove (entry.path(), ec))
            ++removed;
    }
    return removed;
}

ImportResult importSound (Catalog& catalog, const std::filesystem::path& storeDir, const ImportRequest& request, const std::atomic<bool>* cancel)
{
    ImportResult result;
    std::error_code ec;
    // 1. Valid, readable audio (header only: nothing is decoded here).
    if (! std::filesystem::is_regular_file (request.file, ec))
    {
        result.error = request.file.filename().string() + " is not there any more (moved, renamed or on a disconnected drive)";
        return result;
    }
    {
        std::ifstream probe (request.file, std::ios::binary);
        if (! probe.good())
        {
            result.error = "cannot read " + request.file.filename().string() + " (no permission?)";
            return result;
        }
    }
    io::AudioFileInfo info;
    std::string readError;
    if (! io::readAudioFileInfo (request.file, info, readError))
    {
        result.error = readError;
        return result;
    }
    if (cancelled (cancel))
    {
        result.error = "cancelled";
        return result;
    }
    // 2. Its identity.
    const auto hash = io::sha256OfFile (request.file);
    if (! hash)
    {
        result.error = "cannot read " + request.file.filename().string() + " to the end";
        return result;
    }
    result.contentHash = io::contentId (*hash);
    if (cancelled (cancel))
    {
        result.error = "cancelled";
        return result;
    }
    // 3. The managed copy: one per content, published by an atomic rename after checking it.
    if (auto existing = findStored (storeDir, *hash))
    {
        result.stored = *existing;
        result.contentAlreadyStored = true;
    }
    else
    {
        std::filesystem::create_directories (storeDir, ec);
        const auto target = storeDir / (*hash + lowerExtension (request.file));
        const auto temp = partialName (target);
        std::filesystem::copy_file (request.file, temp, std::filesystem::copy_options::overwrite_existing, ec);
        if (ec)
        {
            std::filesystem::remove (temp, ec);
            result.error = "cannot copy " + request.file.filename().string() + " into the Library (disk full or no permission?)";
            return result;
        }
        const auto copied = io::sha256OfFile (temp);
        if (! copied || *copied != *hash || cancelled (cancel))
        {
            std::filesystem::remove (temp, ec);
            result.error = cancelled (cancel) ? "cancelled" : "the copy of " + request.file.filename().string() + " did not match the original (the drive may be failing)";
            return result;
        }
        if (auto raced = findStored (storeDir, *hash))
        {
            // Another instance published the same bytes meanwhile: keep its copy.
            std::filesystem::remove (temp, ec);
            result.stored = *raced;
            result.contentAlreadyStored = true;
        }
        else
        {
            std::filesystem::rename (temp, target, ec);
            if (ec)
            {
                std::filesystem::remove (temp, ec);
                result.error = "cannot store " + request.file.filename().string() + " in the Library";
                return result;
            }
            result.stored = target;
        }
    }
    // 4. The record (after the copy is in place; a copy without a record is harmless and is
    //    found again by the next import of the same bytes).
    Asset asset;
    asset.origin = request.origin;
    asset.name = request.name.empty() ? request.file.stem().string() : request.name;
    SoundInfo sound;
    sound.contentHash = result.contentHash;
    sound.managed = true;
    auto format = lowerExtension (request.file);
    sound.format = format.empty() ? info.formatName : format.substr (1);
    sound.sampleRate = info.sampleRate;
    sound.channels = info.channels;
    sound.bitDepth = info.bitDepth;
    sound.frames = info.frames;
    sound.durationSeconds = info.sampleRate > 0.0 ? static_cast<double> (info.frames) / info.sampleRate : 0.0;
    sound.fileSize = static_cast<std::int64_t> (std::filesystem::file_size (request.file, ec));
    sound.provenance = request.provenance;
    const auto absolute = std::filesystem::absolute (request.file, ec);
    const auto added = catalog.addImportedSound (asset, sound, (ec ? request.file : absolute).string(), request.origin == Origin::captured ? "inbox" : "original");
    if (! added)
    {
        result.error = "the Library could not record " + request.file.filename().string() + " (" + catalog.lastError() + ")";
        return result;
    }
    result.assetId = added->id;
    result.alreadyInLibrary = added->existed;
    if (! request.collectionId.empty())
        catalog.addToCollection (request.collectionId, added->id);
    result.ok = true;
    return result;
}

Resolution resolveSound (const Catalog& catalog, const std::filesystem::path& storeDir, const std::string& contentHash)
{
    Resolution r;
    const auto hex = hexOf (contentHash);
    if (auto stored = findStored (storeDir, hex))
    {
        r.file = *stored;
        r.where = "store";
        return r;
    }
    // Places it was seen: only bytes with this very hash count (never a file with the name).
    std::error_code ec;
    for (const auto& path : catalog.locations (io::contentId (hex)))
    {
        const std::filesystem::path p (path);
        if (! std::filesystem::is_regular_file (p, ec))
        {
            r.missing.push_back (p);
            continue;
        }
        const auto hash = io::sha256OfFile (p);
        if (hash && *hash == hex)
        {
            r.file = p;
            r.where = "original";
            return r;
        }
        r.missing.push_back (p);   // the name is there, the sound is not
    }
    return r;
}

namespace
{
    /** A stored audio file's content hash ("sha256:<hex>"), or empty for anything else. */
    std::string storedContent (const std::filesystem::directory_entry& entry)
    {
        std::error_code ec;
        if (! entry.is_regular_file (ec))
            return {};
        const auto name = entry.path().filename().string();
        if (name.find (".partial") != std::string::npos || name.find (".analysis.json") != std::string::npos)
            return {};
        const auto dot = name.find ('.');
        const auto stem = name.substr (0, dot);
        if (stem.size() != 64 || stem.find_first_not_of ("0123456789abcdef") != std::string::npos)
            return {};
        return "sha256:" + stem;
    }
}

StorageUsage storageUsage (const Catalog& catalog, const std::filesystem::path& storeDir)
{
    StorageUsage usage;
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator (storeDir, ec))
    {
        const auto content = storedContent (entry);
        if (content.empty())
            continue;
        const auto size = static_cast<std::int64_t> (entry.file_size (ec));
        ++usage.files;
        usage.bytes += size;
        if (catalog.contentInLibrary (content) || catalog.contentNeeded (content))
        {
            ++usage.library;
            usage.libraryBytes += size;
        }
        else if (! catalog.soundsWithContent (content).empty())
        {
            ++usage.trashOnly;
            usage.trashOnlyBytes += size;
        }
        else
        {
            ++usage.untracked;
            usage.untrackedBytes += size;
        }
    }
    return usage;
}

TrashEmptied emptyTrash (Catalog& catalog, const std::filesystem::path& storeDir, const std::vector<std::string>& inUse,
                         const std::function<bool (const std::filesystem::path&)>& moveAway)
{
    TrashEmptied result;
    SearchQuery trashed;
    trashed.trashedOnly = true;
    trashed.limit = 1000000;
    std::vector<std::string> contents;
    for (const auto& asset : catalog.search (trashed))
    {
        if (asset.type == AssetType::sound)
        {
            if (const auto info = catalog.sound (asset.id))
                contents.push_back (info->contentHash);
        }
        else if (const auto preset = catalog.preset (asset.id))
        {
            // A trashed preset file sits in the Library's trash folder: moved away with its record.
            std::error_code ec;
            const std::filesystem::path file (preset->file);
            if (std::filesystem::is_regular_file (file, ec))
            {
                const auto size = static_cast<std::int64_t> (std::filesystem::file_size (file, ec));
                if (! moveAway (file))
                {
                    result.failed.push_back (file.string());
                    continue;   // the record stays with its file
                }
                ++result.files;
                result.bytes += size;
            }
        }
        if (catalog.purge (asset.id))
            ++result.records;
    }
    std::sort (contents.begin(), contents.end());
    contents.erase (std::unique (contents.begin(), contents.end()), contents.end());
    for (const auto& content : contents)
    {
        // Kept while anything holds it: another record (in or out of the trash), a preset, an
        // open instance.
        if (! catalog.soundsWithContent (content).empty() || catalog.contentNeeded (content)
            || std::find (inUse.begin(), inUse.end(), content) != inUse.end())
            continue;
        const auto hex = hexOf (content);
        const auto stored = findStored (storeDir, hex);
        if (! stored)
            continue;
        std::error_code ec;
        const auto size = static_cast<std::int64_t> (std::filesystem::file_size (*stored, ec));
        if (! moveAway (*stored))
        {
            result.failed.push_back (stored->string());
            continue;
        }
        ++result.files;
        result.bytes += size;
        const auto analysis = storeDir / (hex + ".analysis.json");
        if (std::filesystem::is_regular_file (analysis, ec))
            std::filesystem::remove (analysis, ec);   // derived data, made again when needed
    }
    return result;
}

} // namespace osp::library
