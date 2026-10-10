#pragma once

#include "library/Catalog.h"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace osp::library
{

/**
    Bringing a sound into the Library, and finding a sound again.

    The managed audio store is the instrument's own content-addressed sample store
    (`<store>/<sha256><.ext>`, the layout projects and portable instruments already use). An
    import is all or nothing: the file is checked, read (header only), hashed, copied under a
    temporary name, verified and renamed into place (atomic), and only then recorded in the
    catalog. An interruption at any point leaves either nothing or a `.partial` file that is
    never taken for a sound and is cleared later.

    Background worker only (reads, hashes and copies files). Never on the audio thread.
*/

struct ImportRequest
{
    std::filesystem::path file;
    Origin origin = Origin::user;
    std::string name;              ///< display name (default: the file name without extension)
    std::string collectionId;      ///< also add it to this collection
    std::string provenance = "user";
};

struct ImportResult
{
    bool ok = false;
    std::string error;                 ///< what went wrong and what to do about it
    std::string assetId;
    std::string contentHash;           ///< "sha256:<hex>"
    std::filesystem::path stored;      ///< the managed copy
    bool alreadyInLibrary = false;     ///< this file was imported before: its record is returned
    bool contentAlreadyStored = false; ///< the bytes were in the store already (no second copy)
};

/** The stored file for a content (any extension), or nothing. A `.partial` file never counts. */
std::optional<std::filesystem::path> findStored (const std::filesystem::path& storeDir, const std::string& sha256Hex);

/** Removes `.partial` files an interrupted import left behind, when older than `age` (another
    instance may be copying one right now). Returns how many were removed. */
int clearInterruptedImports (const std::filesystem::path& storeDir, std::chrono::seconds age);

/** Imports one sound (see above). `cancel` is checked before anything is published. */
ImportResult importSound (Catalog& catalog, const std::filesystem::path& storeDir, const ImportRequest& request,
                          const std::atomic<bool>* cancel = nullptr);

/** Where a sound's bytes are now. Order: the managed store; then places it was seen
    (original, indexed), each checked by its content hash, never by its name. Nothing found:
    `file` empty, `missing` lists the places that no longer hold it (for relinking). */
struct Resolution
{
    std::optional<std::filesystem::path> file;
    std::string where;                          ///< "store", "original", "indexed", "inbox"
    std::vector<std::filesystem::path> missing; ///< known places that are gone or hold other bytes
};
Resolution resolveSound (const Catalog& catalog, const std::filesystem::path& storeDir, const std::string& contentHash);

/** "sha256:<hex>" -> "<hex>" (and plain hex unchanged). */
std::string hexOf (const std::string& contentHash);

} // namespace osp::library
