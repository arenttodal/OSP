#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

struct sqlite3;

namespace osp::library
{

/**
    The Library's catalog: every sound, preset and template the user has, with their names,
    tags, collections, history and where their files are. One SQLite database (WAL) shared by
    every plugin instance on the computer.

    It is an index plus user metadata, never the store of record for audio: sounds live in the
    content-addressed sample store and presets refer to them by content hash, so a project
    loads without the catalog and the catalog can be rebuilt (docs/library/architecture.md).

    Never used on the audio thread. One Catalog (one connection) per thread; every call is a
    short transaction, nothing is decoded or hashed inside one. Errors are returned, never
    thrown.
*/

enum class AssetType : std::uint8_t
{
    sound,
    preset,
    templateState   ///< a complete instrument without sounds (.ospstate)
};

enum class Origin : std::uint8_t
{
    factory,
    user,
    pack,
    captured,
    external   ///< indexed in place (a scanned folder), not copied
};

const char* toString (AssetType) noexcept;
const char* toString (Origin) noexcept;
std::optional<AssetType> assetTypeFrom (const std::string&) noexcept;
std::optional<Origin> originFrom (const std::string&) noexcept;

/** Where a tag came from: the user, or inferred (kept apart; a user's removal sticks). */
enum class TagSource : std::uint8_t
{
    user,
    filename,
    folder,
    audio
};

struct Asset
{
    std::string id;               ///< stable identity (assigned by the catalog)
    AssetType type = AssetType::sound;
    Origin origin = Origin::user;
    std::string name;             ///< display name
    std::string category;
    std::string notes;
    bool favourite = false;
    int rating = 0;               ///< 0 (none) .. 5 stars (catalog 3)
    std::int64_t created = 0;     ///< seconds since 1970 (UTC)
    std::int64_t modified = 0;
    bool trashed = false;
};

struct SoundInfo
{
    std::string contentHash;      ///< "sha256:<hex>"
    bool managed = true;          ///< a copy in the sample store (false: indexed in place)
    std::string format;           ///< "wav", "aiff", "flac", ...
    double sampleRate = 0.0;
    int channels = 0;
    int bitDepth = 0;
    std::int64_t frames = 0;
    double durationSeconds = 0.0;
    std::int64_t fileSize = 0;
    std::optional<double> rootMidi;
    std::string provenance;       ///< "original", "user", "licensed", "unknown", "cleared"
    std::string parentAsset;      ///< a slice or trim: the sound it came from
    double sliceStart = 0.0, sliceEnd = 0.0;   ///< seconds in the parent
};

struct PresetInfo
{
    std::string file;             ///< path relative to its root
    std::string root;             ///< "user", "factory", "pack:<id>"
    int stateVersion = 0;
    int layers = 0;               ///< sounds it holds (a template: the source count it expects)
    std::vector<std::string> soundHashes;   ///< what it needs (cleanup never removes these)
};

struct Tag
{
    std::string name;
    TagSource source = TagSource::user;
    double confidence = 1.0;
};

struct HistoryEntry
{
    std::string assetId;
    std::int64_t at = 0;
    std::string action;           ///< "loaded", "imported", "previewed"
    int layer = -1;
};

struct SearchQuery
{
    std::string text;                        ///< words (prefix match), any order
    std::optional<AssetType> type;
    std::optional<Origin> origin;
    std::vector<std::string> tags;           ///< every one must be on the asset
    std::string collection;                  ///< collection id
    std::string category;                    ///< exact (a sound's category is its Type)
    std::optional<double> minSeconds, maxSeconds;   ///< sounds: length bounds
    bool favouritesOnly = false;
    bool recentOnly = false;                 ///< only assets with a history entry
    bool includeTrashed = false;
    bool trashedOnly = false;                ///< the Library's trash
    enum class Sort : std::uint8_t { name, recentlyUsed, added, rating, length } sort = Sort::name;
    int limit = 200;
    int offset = 0;
};

struct Collection
{
    std::string id, name;
    int count = 0;                ///< assets in it (not trashed)
};

class Catalog
{
public:
    /** 2: presets.trashed_from (where a trashed preset file came from).
        3: assets.rating (0-5 stars). */
    static constexpr int schemaVersion = 3;

    /** Opens (creating and migrating as needed) the catalog at `file`. nullptr and `error` on
        failure (the file is never deleted or replaced). */
    static std::unique_ptr<Catalog> open (const std::filesystem::path& file, std::string& error);
    /** <user app data>/OSP/Library/catalog.db (OSP_LIBRARY_DIR overrides). */
    static std::filesystem::path defaultFile();
    ~Catalog();

    int version() const;
    bool hasFullTextSearch() const noexcept { return fts; }
    /** PRAGMA integrity_check: "ok" or what is wrong. */
    std::string integrityCheck() const;
    const std::string& lastError() const noexcept { return error; }

    // Assets ------------------------------------------------------------------
    /** A new sound record (several may share one content: different names, collections). */
    std::optional<std::string> addSound (const Asset& asset, const SoundInfo& info);
    /** An import: the sound record for `info` found at `path` - the existing one when this
        content was already imported from there (one transaction, so instances importing the
        same file at once make one record). */
    struct Added
    {
        std::string id;
        bool existed = false;
    };
    std::optional<Added> addImportedSound (const Asset& asset, const SoundInfo& info, const std::string& path, const std::string& kind);
    std::optional<std::string> addPreset (const Asset& asset, const PresetInfo& info);
    /** A preset or template file saved (again): its record updated in place (same identity,
        new name, version, sounds), or a new one. */
    std::optional<std::string> savePreset (const Asset& asset, const PresetInfo& info);
    std::optional<std::string> presetWithFile (const std::string& file, bool includeTrashed = false) const;
    /** A preset or template file moved (renamed, sent to the trash, restored): its record
        follows. `trashedFrom`: the place it came from (restoring puts it back there). */
    bool movePreset (const std::string& id, const std::string& newFile, const std::string& newName, const std::string& trashedFrom = {});
    std::string trashedFrom (const std::string& id) const;

    /** Distribution rights of a content: the best any of its records holds ("cleared",
        "original", "user", "licensed"), else "unknown". Unknown never becomes cleared by
        itself: only setProvenance does that (a user's confirmation). */
    std::string provenanceOf (const std::string& contentHash) const;
    bool setProvenance (const std::string& contentHash, const std::string& provenance);
    std::optional<Asset> asset (const std::string& id) const;
    std::optional<SoundInfo> sound (const std::string& id) const;
    std::optional<PresetInfo> preset (const std::string& id) const;
    std::vector<std::string> soundsWithContent (const std::string& contentHash) const;
    /** Any preset or template (not trashed) needs this content. */
    bool contentNeeded (const std::string& contentHash) const;
    bool rename (const std::string& id, const std::string& name);
    bool setFavourite (const std::string& id, bool favourite);
    bool setNotes (const std::string& id, const std::string& notes);
    bool setCategory (const std::string& id, const std::string& category);
    bool setRating (const std::string& id, int stars);
    /** A sound's root note (fractional MIDI), or none (the analysis decides). */
    bool setRootMidi (const std::string& id, std::optional<double> midi);
    /** Recoverable removal: hidden from results until restored. */
    bool trash (const std::string& id);
    bool restore (const std::string& id);
    /** Removes a trashed record for good (its tags, history and collection places with it).
        Only a trashed record: anything else is refused. Files are never touched here. */
    bool purge (const std::string& id);
    /** A sound record that is not trashed holds this content. */
    bool contentInLibrary (const std::string& contentHash) const;
    /** When it was last used (loaded, imported), 0 if never. */
    std::int64_t lastUsed (const std::string& id) const;
    /** A preset's size: its sounds' bytes (each content once). */
    std::int64_t presetBytes (const std::string& id) const;

    // Tags and collections ------------------------------------------------------
    bool addTag (const std::string& id, const std::string& tag, TagSource source = TagSource::user, double confidence = 1.0);
    /** A user removal: an inferred tag stays rejected when the folder is scanned again. */
    bool removeTag (const std::string& id, const std::string& tag);
    std::vector<Tag> tagsOf (const std::string& id) const;
    std::optional<std::string> createCollection (const std::string& name);
    bool addToCollection (const std::string& collectionId, const std::string& assetId);
    bool removeFromCollection (const std::string& collectionId, const std::string& assetId);
    bool renameCollection (const std::string& collectionId, const std::string& name);
    /** The collection goes; its assets stay. */
    bool deleteCollection (const std::string& collectionId);
    /** Every collection with how many (not trashed) assets of `type` it holds (any type when empty), by name. */
    std::vector<Collection> collections (std::optional<AssetType> type = {}) const;
    std::vector<Collection> collectionsOf (const std::string& assetId) const;

    // History -----------------------------------------------------------------
    bool recordUse (const std::string& id, const std::string& action, int layer = -1);
    /** Most recently used first (one entry per asset). */
    std::vector<HistoryEntry> recent (int limit = 50, std::optional<AssetType> type = {}) const;
    int useCount (const std::string& id) const;

    // Locations ---------------------------------------------------------------
    bool addLocation (const std::string& contentHash, const std::string& path, const std::string& kind);
    std::vector<std::string> locations (const std::string& contentHash) const;

    // Search ------------------------------------------------------------------
    std::vector<Asset> search (const SearchQuery& query) const;
    /** How many assets match (limit and offset ignored). */
    int count (const SearchQuery& query) const;
    /** Every category in use among `type`'s assets, with counts (not trashed), by name. */
    std::vector<std::pair<std::string, int>> categoryCounts (AssetType type) const;

private:
    explicit Catalog (sqlite3* db);
    bool migrate();
    bool exec (const char* sql);
    void reindex (const std::string& id);
    /** The FROM / WHERE part of a search (and its bound values), shared by search and count. */
    struct Filter;
    Filter filterFor (const SearchQuery& query) const;
    std::optional<std::string> addAsset (const Asset& asset);

    sqlite3* db = nullptr;
    bool fts = false;
    mutable std::string error;
};

} // namespace osp::library
