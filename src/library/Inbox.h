#pragma once

#include "library/Catalog.h"
#include "library/SoundImport.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace osp::library
{

/**
    The capture Inbox (Stage 7): audio arriving in folders the user chose (Finder, Voice
    Memos exports, Dropbox, iCloud Drive, a future phone app; no account, no server).

    Reconciliation, not trust in notifications: every pass looks at the folders as they are
    and moves each file along explicit states:

        detected -> waiting (still arriving, or a cloud placeholder) -> ready -> importing ->
        imported;   failed (unreadable after several tries);   missing (gone, or its folder
        is offline).

    A file is only ready once its size and time have stood still for `stableMilliseconds`
    and its header reads. A partial download is never called damaged: an unreadable file
    waits and is tried again, and only after `maxAttempts` stable tries is it failed. Content
    already in the Library is marked imported (a duplicate), never copied twice. An item left
    "importing" by a crash goes back to ready. Files are never moved, changed or deleted.

    Background thread only (reads and hashes files).
*/
struct InboxPolicy
{
    std::int64_t stableMilliseconds = 2000;
    int maxAttempts = 6;
    bool recursive = true;
};

struct InboxPass
{
    int detected = 0, waiting = 0, ready = 0, imported = 0, failed = 0, missing = 0;
    std::vector<std::string> offlineFolders;
};

/** One pass over the folders (`nowMilliseconds`: the clock, injectable for tests). */
InboxPass reconcileInbox (Catalog& catalog, const std::vector<std::filesystem::path>& folders, std::int64_t nowMilliseconds,
                          const InboxPolicy& policy = {});

/** Brings a ready item into the Library (origin "captured"): its name, and optionally tags and
    a collection. The file stays where it is. */
ImportResult importInboxItem (Catalog& catalog, const std::filesystem::path& storeDir, const std::string& path, const std::string& name = {},
                              const std::vector<std::string>& tags = {}, const std::string& collectionId = {});

/** iCloud's stand-in for a file not downloaded yet (".Name.wav.icloud"): the real file's path. */
std::optional<std::filesystem::path> cloudPlaceholderTarget (const std::filesystem::path& file);
/** A download or sync still writing under a temporary name (.part, .crdownload, ...). */
bool isTemporaryDownload (const std::filesystem::path& file);

} // namespace osp::library
