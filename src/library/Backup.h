#pragma once

#include "library/Catalog.h"

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace osp::library
{

/**
    Library backup and restore (Stage 10, docs/library/backup.md).

    A backup is a folder, `<name>.ospbackup` (a folder so it has no size limit; D-19). It holds:
    - `manifest.json`, written last, so a backup without it is incomplete and is refused;
    - `catalog.db`, a consistent snapshot (VACUUM INTO, never a copy of the live file);
    - `store/`, the managed sounds;
    - `presets/`, `templates/`, `packs/` (installed third-party packs), `settings.json` and
      `favourites.txt`.

    Restoring merges into the Library that is there, which may be empty (a new machine). It
    does not replace it:
    - every record keeps its identity;
    - duplicates are one record and one store copy;
    - a record in both is kept as it is here, unless `newerWins` and the backup's is newer;
    - a file that would replace a different one is written beside it ("(restored)"), or it
      sends the old one to the trash first (a newer record that wins);
    - nothing is lost.

    Scan folders, the Inbox and preview caches belong to one machine and are not carried.
    Indexed external files are included only when asked (`includeExternal`).

    Background threads only.
*/
struct BackupPlaces
{
    std::filesystem::path storeDir, presetsDir, templatesDir, packsDir;
    std::filesystem::path settingsFile, favouritesFile;
    std::string pluginVersion;
};

struct BackupOptions
{
    bool includeExternal = false;    ///< sounds not in the store (indexed in place, or only found at their original place)
    bool includeUntracked = true;    ///< store copies no record holds (loaded before the Library; old projects may use them)
    const std::atomic<bool>* cancel = nullptr;
    std::function<void (int done, int total)> progress;
};

struct BackupResult
{
    bool ok = false;
    std::string error;
    int records = 0, sounds = 0, presets = 0, templates = 0, packFiles = 0, external = 0;
    std::int64_t bytes = 0;
    std::vector<std::string> notIncluded;   ///< sounds that are not in the backup, and why
};

/** Writes `<out>` (a folder ending in `.ospbackup`; it must not exist yet). */
BackupResult exportBackup (Catalog& catalog, const BackupPlaces& places, const std::filesystem::path& out, const BackupOptions& options = {});

struct BackupInfo
{
    bool ok = false;
    std::string error;
    std::string created, pluginVersion;
    int catalogVersion = 0, records = 0, sounds = 0, presets = 0, templates = 0;
    std::int64_t bytes = 0;
};
/** Reads and checks a backup without changing anything. */
BackupInfo inspectBackup (const std::filesystem::path& folder);

struct RestoreOptions
{
    bool newerWins = false;   ///< false: a record in both stays as it is here
    const std::atomic<bool>* cancel = nullptr;
    std::function<bool (const std::filesystem::path&)> moveToTrash;   ///< the old file when a newer one replaces it
};

struct RestoreReport
{
    bool ok = false;
    std::string error;
    int added = 0, kept = 0, updated = 0;                    ///< records
    int soundsAdded = 0, soundsPresent = 0;                  ///< store copies
    int filesAdded = 0, filesSame = 0, filesBeside = 0, filesReplaced = 0;
    int tags = 0, collections = 0, favourites = 0;
    std::vector<std::string> notes;
};
RestoreReport restoreBackup (Catalog& catalog, const BackupPlaces& places, const std::filesystem::path& folder, const RestoreOptions& options = {});

} // namespace osp::library
