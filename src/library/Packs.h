#pragma once

#include "library/Catalog.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace osp::library
{

/**
    Sound packs (Stage 10, docs/library/packs.md): a `.osppack` is a zip holding
    `manifest.json` and the files it lists:
    - sounds as `sounds/<sha256>.<ext>`;
    - presets as `presets/<name>.osppreset`;
    - templates as `templates/<name>.ospstate`.

    A pack is data only; nothing in it is ever run. Installing checks everything first: the
    manifest, the archive's safety, every file's size and checksum, the plugin version it
    needs, the sounds its presets need, and the packs it requires. Then:
    - sounds go into the store;
    - presets and templates go into the pack's own folder;
    - records are made with origin Pack (Factory for a factory pack).

    An update keeps every record's identity, through the manifest's stable `uid`, so a user's
    favourite, rating, tags, notes and collections on pack content survive it. It never
    touches the user's own content.

    Background threads only.
*/
struct PackItem
{
    enum class Kind { sound, preset, templateState } kind = Kind::sound;
    std::string uid;                  ///< stable within the pack, across versions
    std::string path;                 ///< inside the archive
    std::string sha256;               ///< hex
    std::int64_t bytes = 0;
    std::string name, category;       ///< category: a sound's Type, a preset's category
    std::vector<std::string> tags;
    std::optional<double> rootMidi;   ///< sounds
    std::vector<std::string> sounds;  ///< presets: the sounds (sha256 hex) they need
};

struct PackManifest
{
    static constexpr int schemaVersion = 1;
    std::string id, name, version, creator, description, license, requiresPlugin;
    bool factory = false;
    std::vector<std::pair<std::string, std::string>> dependencies;   ///< "requires": other packs (id, at least this version)
    std::vector<PackItem> items;
};

/** The manifest from its JSON, checked and cleaned (`why`: what is wrong). */
std::optional<PackManifest> parsePackManifest (const std::string& json, std::string& why);
std::string toJson (const PackManifest& manifest);

struct PackPlaces
{
    std::filesystem::path storeDir;
    std::filesystem::path packsDir;     ///< third-party packs: <packsDir>/<id>/Presets, Templates
    std::filesystem::path factoryDir;   ///< factory packs, apart from user data
    std::string pluginVersion;          ///< the running plugin's version ("1.2.0")
};

struct PackResult
{
    bool ok = false;
    std::string error;
    PackManifest manifest;
    bool upgraded = false, alreadyInstalled = false;
    std::string previousVersion;
    int sounds = 0, presets = 0, templates = 0, removedItems = 0;
    std::vector<std::string> notes;     ///< what the user should know (licence, kept edits)
};

/** Reads and checks a pack without installing anything (for the confirmation). */
PackResult inspectPack (const Catalog& catalog, const std::filesystem::path& file, const PackPlaces& places);
/** Installs or updates a pack (a downgrade is refused). Reinstalling the same version repairs it. */
PackResult installPack (Catalog& catalog, const std::filesystem::path& file, const PackPlaces& places);
/** Removes a pack. Its presets and templates go with its folder (through `moveToTrash`).
    Its sounds go to the Library's trash, recoverable; their audio stays in the store while
    anything (a user preset, a project) may need it. */
PackResult removePack (Catalog& catalog, const std::string& packId, const PackPlaces& places,
                       const std::function<bool (const std::filesystem::path&)>& moveToTrash);
/** Every factory pack in `folder` (`*.osppack` with "factory": true) that is new or newer
    than what is installed. */
std::vector<PackResult> installFactoryPacks (Catalog& catalog, const std::filesystem::path& folder, const PackPlaces& places);

/** Making a pack from Library records (a collection). A preset brings the sounds it needs.
    Sounds whose rights are unknown are refused, by name, until `confirmUnknownRights`; the
    confirmation is then recorded (D-05, Stage 3's rule). */
struct PackExport
{
    std::vector<std::string> assetIds;
    std::string id, name, version = "1.0.0", creator, description, license;
    bool confirmUnknownRights = false;
};
PackResult exportPack (Catalog& catalog, const std::filesystem::path& storeDir, const PackExport& request, const std::filesystem::path& out,
                       const std::string& pluginVersion);

} // namespace osp::library
