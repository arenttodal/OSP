#pragma once

#include "core/AudioData.h"
#include "library/Catalog.h"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <functional>
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
    std::string category;          ///< its Type, when the record has none yet
    struct Tag
    {
        std::string name;
        TagSource source = TagSource::user;
        double confidence = 1.0;
    };
    std::vector<Tag> tags;         ///< inferred tags stay inferred (a user's removal still wins)
    std::optional<double> rootMidi;
    std::string place;             ///< the location recorded instead of the file's own path ("(derived)" for a rendered slice)
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

/** What the managed store holds (Library settings: storage). */
struct StorageUsage
{
    int files = 0;
    std::int64_t bytes = 0;
    int library = 0;               ///< a Library sound holds it, or a preset needs it
    std::int64_t libraryBytes = 0;
    int trashOnly = 0;             ///< only sounds in the Library's trash hold it
    std::int64_t trashOnlyBytes = 0;
    int untracked = 0;             ///< no record (loaded before the Library existed): kept, projects may use it
    std::int64_t untrackedBytes = 0;
};
StorageUsage storageUsage (const Catalog& catalog, const std::filesystem::path& storeDir);

struct TrashEmptied
{
    int records = 0;               ///< sound and preset records removed for good
    int files = 0;                 ///< files moved away (stored copies, trashed preset files)
    std::int64_t bytes = 0;
    std::vector<std::string> failed;   ///< what could not be moved (left as it was)
};
/** Empties the Library's trash. Every trashed sound and preset record is removed for good; a
    trashed preset's file and a stored copy that nothing else holds (no other sound record, no
    preset, nothing in `inUse` - what open instances play) are handed to `moveAway` (the
    system Trash, so even this stays recoverable). A copy something still holds is kept.
    Background thread. */
TrashEmptied emptyTrash (Catalog& catalog, const std::filesystem::path& storeDir, const std::vector<std::string>& inUse,
                         const std::function<bool (const std::filesystem::path&)>& moveAway);

/** A trim or slice saved as a new sound (Stage 8.5): written as 32-bit float WAV (nothing lost),
    imported like any sound, and its provenance recorded (the parent's record, start and end in
    seconds, the operations as JSON). The parent is never changed. */
struct DerivedSound
{
    std::string parentAssetId;         ///< empty when the source was a file outside the Library
    std::string name;
    double startSeconds = 0.0, endSeconds = 0.0;
    std::string operations;            ///< e.g. {"fadeIn":0.0005,"fadeOut":0.005,"gainDb":0,"slicer":"sampleforge-1"}
    Origin origin = Origin::user;
    std::string category;
    std::vector<ImportRequest::Tag> tags;
    std::optional<double> rootMidi;
};
ImportResult saveDerivedSound (Catalog& catalog, const std::filesystem::path& storeDir, const AudioData& audio, const DerivedSound& derived);

/** "sha256:<hex>" -> "<hex>" (and plain hex unchanged). */
std::string hexOf (const std::string& contentHash);

} // namespace osp::library
