#include "library/Inbox.h"

#include "io/AudioFileIO.h"
#include "io/ContentHash.h"
#include "library/FolderScan.h"

#include <algorithm>
#include <cctype>
#include <map>
#include <set>

namespace osp::library
{

namespace
{
    std::string lower (std::string s)
    {
        for (auto& c : s)
            c = static_cast<char> (std::tolower (static_cast<unsigned char> (c)));
        return s;
    }

    std::int64_t modified (const std::filesystem::path& file)
    {
        std::error_code ec;
        const auto t = std::filesystem::last_write_time (file, ec);
        return ec ? 0 : static_cast<std::int64_t> (t.time_since_epoch().count());
    }

    bool insideFolder (const std::string& path, const std::string& folder)
    {
        return path.size() > folder.size() && path.compare (0, folder.size(), folder) == 0;
    }
}

std::optional<std::filesystem::path> cloudPlaceholderTarget (const std::filesystem::path& file)
{
    const auto name = file.filename().string();
    if (name.size() > 8 && name[0] == '.' && lower (name.substr (name.size() - 7)) == ".icloud")
        return file.parent_path() / name.substr (1, name.size() - 8);
    return std::nullopt;
}

bool isTemporaryDownload (const std::filesystem::path& file)
{
    const auto name = lower (file.filename().string());
    for (const char* suffix : { ".part", ".partial", ".download", ".crdownload", ".tmp", ".dropbox", ".sb-" })
        if (name.size() > std::string (suffix).size() && name.find (suffix) != std::string::npos)
            return true;
    return name.rfind ("~$", 0) == 0;
}

InboxPass reconcileInbox (Catalog& catalog, const std::vector<std::filesystem::path>& folders, std::int64_t now, const InboxPolicy& policy)
{
    InboxPass pass;
    std::error_code ec;
    for (const auto& folderPath : folders)
    {
        const auto folder = folderPath.string();
        std::map<std::string, InboxItem> known;
        for (auto& item : catalog.inboxItems (folder))
            known[item.path] = item;

        if (! std::filesystem::is_directory (folderPath, ec))
        {
            // Offline (a disconnected drive, a sync client not running): what was waiting is
            // missing for now and comes back when the folder does; nothing is called failed.
            pass.offlineFolders.push_back (folder);
            for (auto& [path, item] : known)
                if (item.state != "imported" && item.state != "missing")
                {
                    item.state = "missing";
                    item.error = "its folder is not available (offline?)";
                    catalog.putInboxItem (item);
                    ++pass.missing;
                }
            continue;
        }

        // What is there now (real files, and iCloud placeholders for files not downloaded).
        std::map<std::string, bool> present;   // path -> placeholder only
        auto consider = [&] (const std::filesystem::path& file) {
            if (const auto target = cloudPlaceholderTarget (file))
            {
                if (io::isSupportedAudioExtension (*target) && present.count (target->string()) == 0)
                    present[target->string()] = true;
                return;
            }
            const auto name = file.filename().string();
            if (name.empty() || name[0] == '.' || isTemporaryDownload (file) || ! io::isSupportedAudioExtension (file))
                return;
            present[file.string()] = false;
        };
        const auto options = std::filesystem::directory_options::skip_permission_denied;
        if (policy.recursive)
        {
            for (std::filesystem::recursive_directory_iterator it (folderPath, options, ec), end; it != end && ! ec; it.increment (ec))
            {
                const auto leaf = it->path().filename().string();
                if (it->is_symlink (ec))
                    continue;   // never followed
                if (it->is_directory (ec) && ! leaf.empty() && leaf[0] == '.')
                {
                    it.disable_recursion_pending();
                    continue;
                }
                if (it->is_regular_file (ec))
                    consider (it->path());
            }
        }
        else
            for (std::filesystem::directory_iterator it (folderPath, options, ec), end; it != end && ! ec; it.increment (ec))
                if (! it->is_symlink (ec) && it->is_regular_file (ec))
                    consider (it->path());

        for (const auto& [path, placeholderOnly] : present)
        {
            auto item = known.count (path) > 0 ? known[path] : InboxItem {};
            const bool isNew = item.path.empty();
            if (isNew)
            {
                item.path = path;
                item.folder = folder;
                item.state = "detected";
                item.detected = now;
                item.stableSince = now;
                ++pass.detected;
            }
            if (placeholderOnly)
            {
                item.state = "waiting";
                item.error = "in the cloud, not downloaded yet (download it, or keep it downloaded)";
                item.stableSince = now;
                catalog.putInboxItem (item);
                ++pass.waiting;
                continue;
            }
            const auto size = static_cast<std::int64_t> (std::filesystem::file_size (path, ec));
            const auto time = modified (path);
            const bool changed = isNew || size != item.size || time != item.mtime;
            if (item.state == "importing")
                item.state = "ready";   // a crash or a closed host left it half done: the copy is checked again on import
            if (changed)
            {
                const bool wasImported = item.state == "imported";
                item.size = size;
                item.mtime = time;
                item.stableSince = now;
                if (! isNew || size == 0)
                {
                    item.state = "waiting";
                    item.error = wasImported ? "changed since it was imported" : "still arriving";
                }
                if (wasImported)
                    item.assetId.clear(), item.contentHash.clear(), item.attempts = 0;
            }
            if ((item.state == "detected" || item.state == "waiting" || item.state == "missing") && now - item.stableSince >= policy.stableMilliseconds && size > 0)
            {
                io::AudioFileInfo info;
                std::string error;
                if (io::readAudioFileInfo (path, info, error))
                {
                    if (const auto hex = io::sha256OfFile (path))
                    {
                        item.contentHash = io::contentId (*hex);
                        const auto records = catalog.soundsWithContent (item.contentHash);
                        if (! records.empty() && catalog.contentInLibrary (item.contentHash))
                        {
                            item.state = "imported";
                            item.assetId = records.front();
                            item.error = "already in the Library";
                        }
                        else
                        {
                            // What it is, before the user decides (the same bytes are analysed
                            // once; a file that cannot be decoded still waits for its import to say so).
                            std::string analysisError;
                            analyseContent (catalog, path, item.contentHash, analysisError);
                            item.state = "ready";
                            item.error.clear();
                        }
                        item.attempts = 0;
                    }
                    else
                        error = "cannot read it to the end yet";
                }
                if (item.state != "ready" && item.state != "imported")
                {
                    // Not readable yet: maybe still downloading. Tried again until the limit.
                    ++item.attempts;
                    item.stableSince = now;
                    if (item.attempts >= policy.maxAttempts)
                    {
                        item.state = "failed";
                        item.error = error.empty() ? "cannot be read" : error;
                    }
                    else
                    {
                        item.state = "waiting";
                        item.error = "not readable yet (" + error + "); trying again";
                    }
                }
            }
            catalog.putInboxItem (item);
            if (item.state == "waiting" || item.state == "detected")
                ++pass.waiting;
            else if (item.state == "ready")
                ++pass.ready;
            else if (item.state == "imported")
                ++pass.imported;
            else if (item.state == "failed")
                ++pass.failed;
        }

        // Known items no longer there: an imported one is done with (the Library has it); any
        // other is missing (moved or deleted by the user; it comes back if the file does).
        for (auto& [path, item] : known)
        {
            if (present.count (path) > 0 || ! insideFolder (path, folder))
                continue;
            if (item.state == "imported")
                catalog.removeInboxItem (path);
            else if (item.state != "missing")
            {
                item.state = "missing";
                item.error = "the file is not there any more";
                catalog.putInboxItem (item);
                ++pass.missing;
            }
        }
    }
    return pass;
}

ImportResult importInboxItem (Catalog& catalog, const std::filesystem::path& storeDir, const std::string& path, const std::string& name,
                              const std::vector<std::string>& tags, const std::string& collectionId)
{
    ImportResult result;
    auto item = catalog.inboxItem (path);
    if (! item || (item->state != "ready" && item->state != "failed"))
    {
        result.error = item ? "it is " + item->state + ", not ready" : "it is not in the Inbox";
        return result;
    }
    item->state = "importing";
    catalog.putInboxItem (*item);
    // The name's and the folders' evidence (and the analysis' when there is one), like a scan's.
    auto request = requestWithEvidence (catalog, path, item->contentHash, item->folder);
    request.origin = Origin::captured;
    request.name = name;
    request.collectionId = collectionId;
    request.provenance = "original";   // the user's own capture
    for (const auto& t : tags)
        request.tags.push_back ({ t, TagSource::user, 1.0 });
    result = importSound (catalog, storeDir, request);
    item->state = result.ok ? "imported" : "failed";
    item->assetId = result.assetId;
    item->contentHash = result.ok ? result.contentHash : item->contentHash;
    item->error = result.ok ? std::string() : result.error;
    catalog.putInboxItem (*item);
    return result;
}

} // namespace osp::library
