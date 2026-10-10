#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
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
    std::string packId;           ///< the pack it came with (catalog 5), or empty
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
    std::string operations;       ///< a derived sound: what was done (JSON: fades, gain, analysis version)
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

/** A folder the user chose to scan (Stage 6.3). */
struct ScanRoot
{
    std::string id, path;
    std::string include, exclude;     ///< ';'-separated name patterns (* and ?); empty include: every supported file
    std::int64_t added = 0;
};

/** A file a scan found: indexed in place, a candidate until the user imports it. */
struct ScanFile
{
    std::string path, rootId;
    std::int64_t size = 0, mtime = 0;
    std::string contentHash;          ///< empty until the deep stage hashed it
    std::string state;                ///< "indexed" (fast stage), "analysed", "failed", "imported"
    std::string assetId;              ///< the Library record once imported
    bool rejected = false;            ///< the user set it aside (never deleted)
    double durationSeconds = 0.0, sampleRate = 0.0;
    int channels = 0;
    std::string format, error;
};

/** A file in an Inbox folder (Stage 7) and where it is on its way into the Library. */
struct InboxItem
{
    std::string path, folder;
    std::string state;                ///< detected, waiting, ready, importing, imported, failed, missing
    std::int64_t size = 0, mtime = 0;
    std::int64_t stableSince = 0;     ///< when its size and time last changed (milliseconds)
    std::string contentHash, assetId, error;
    int attempts = 0;
    std::int64_t detected = 0, updated = 0;
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
        3: assets.rating (0-5 stars).
        4: scan_files details (rejected, duration, rate, channels, format, error), classification
           overrides, inbox_items. */
    static constexpr int schemaVersion = 5;

    /** Opens (creating and migrating as needed) the catalog at `file`. nullptr and `error` on
        failure (the file is never deleted or replaced). */
    static std::unique_ptr<Catalog> open (const std::filesystem::path& file, std::string& error);
    /** <user app data>/OSP/Library/catalog.db (OSP_LIBRARY_DIR overrides). */
    static std::filesystem::path defaultFile();
    ~Catalog();

    int version() const;
    /** Runs `work` in one transaction (a scan's batch of files): committed when it returns
        true, rolled back otherwise. */
    bool inTransaction (const std::function<bool()>& work);
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

    // Scanning (Stage 6) ---------------------------------------------------------
    /** A folder to scan (the same path again: its record, patterns updated). */
    std::optional<std::string> addScanRoot (const std::string& path, const std::string& include = {}, const std::string& exclude = {});
    std::vector<ScanRoot> scanRoots() const;
    std::optional<ScanRoot> scanRoot (const std::string& id) const;
    /** The folder is no longer scanned; its candidates go with it (no file is touched). */
    bool removeScanRoot (const std::string& id);
    bool putScanFile (const ScanFile& file);
    std::optional<ScanFile> scanFile (const std::string& path) const;
    /** Candidates: of one folder (or every one), optionally in one state, rejected ones only on request. */
    std::vector<ScanFile> scanFiles (const std::string& rootId = {}, const std::string& state = {}, bool includeRejected = false, int limit = 100000) const;
    bool setScanFileRejected (const std::string& path, bool rejected);
    /** A candidate's file is gone: its row goes (the Library's own records stay). */
    bool forgetScanFile (const std::string& path);
    /** The sidebar's counts: scanned files (not set aside), set aside, Inbox items not imported yet. */
    struct CandidateCounts
    {
        int scanned = 0, setAside = 0, inbox = 0;
    };
    CandidateCounts candidateCounts() const;

    // Analysis results and the user's say (Stage 6.5/6.6) ------------------------
    bool putAnalysis (const std::string& contentHash, int version, const std::string& classification, const std::string& features);
    struct AnalysisRow
    {
        int version = 0;
        std::string classification, features;
    };
    /** The newest analysis of a content. */
    std::optional<AnalysisRow> analysis (const std::string& contentHash) const;
    /** The user's classification (empty clears it): it always wins over the analysis and
        survives every re-analysis. */
    bool setClassificationOverride (const std::string& contentHash, const std::string& classification);
    std::string classificationOverride (const std::string& contentHash) const;
    /** What a content is: the user's override, else the analysis, else empty. */
    std::string classificationOf (const std::string& contentHash) const;

    // The Inbox (Stage 7) -------------------------------------------------------
    bool putInboxItem (const InboxItem& item);
    std::optional<InboxItem> inboxItem (const std::string& path) const;
    std::vector<InboxItem> inboxItems (const std::string& folder = {}) const;
    bool removeInboxItem (const std::string& path);

    // Packs (Stage 10) ----------------------------------------------------------
    struct PackRecord
    {
        std::string id, name, version, creator, manifest;   ///< manifest: the pack's JSON as installed
        std::int64_t installed = 0;
    };
    bool putPack (const PackRecord& pack);
    std::optional<PackRecord> pack (const std::string& id) const;
    std::vector<PackRecord> packs() const;
    bool removePackRecord (const std::string& id);
    /** A pack's asset: which pack and its item's stable key in the pack (the manifest's uid), so
        an update finds the same record (its identity, and the user's favourite, tags, rating). */
    bool setPackKey (const std::string& assetId, const std::string& packId, const std::string& key);
    std::optional<std::string> assetForPackKey (const std::string& packId, const std::string& key) const;
    /** Every asset of a pack (trashed ones too), with its key. */
    std::vector<std::pair<std::string, std::string>> packAssets (const std::string& packId) const;
    /** A sound record's audio replaced (a pack update): same identity, new content and details. */
    bool replaceSoundContent (const std::string& id, const SoundInfo& info, const std::string& place);
    /** A preset or template record's file and details replaced (a pack update). */
    bool updatePreset (const std::string& id, const std::string& name, const PresetInfo& info);
    /** Every preset and template record's file (trashed ones too), by id. */
    std::vector<std::pair<std::string, std::string>> presetFiles() const;
    /** Every content a record or a preset needs (trashed records too: their restore needs it). */
    std::vector<std::string> neededContent() const;

    // Backup and restore (Stage 10) ----------------------------------------------
    /** A consistent copy of the whole catalog (VACUUM INTO), safe while others write. */
    bool snapshotTo (const std::filesystem::path& file);
    /** Restore by merging another catalog (a backup's snapshot) into this one, in one
        transaction. Records keep their identities. A record in both: kept as it is here
        (keepExisting), or the more recently modified one wins (newerWins). Preset paths
        starting with a `from` prefix are moved to its `to` prefix (another machine's folders).
        Scan and Inbox state are this machine's and are not merged. */
    struct MergeReport
    {
        int added = 0, kept = 0, updated = 0;
        int tags = 0, collections = 0, collectionItems = 0, history = 0, packs = 0;
        std::vector<std::string> addedIds, keptIds, updatedIds;
    };
    std::optional<MergeReport> mergeFrom (const std::filesystem::path& other, bool newerWins,
                                          const std::vector<std::pair<std::string, std::string>>& movedPrefixes);

    // Caches (Stage 10) ----------------------------------------------------------
    struct CacheUsage
    {
        int analysisRows = 0, unusedAnalysisRows = 0;
        std::int64_t analysisBytes = 0, unusedAnalysisBytes = 0;
    };
    CacheUsage cacheUsage() const;
    /** Analyses of content nothing refers to any more (no record, scan or Inbox item): they can
        always be made again. Returns how many went. */
    int forgetUnusedAnalysis();

    // Derived sounds (Stage 8) ----------------------------------------------------
    /** Records where a sound came from: a slice or trim of `parentId` (seconds), the
        operations applied (JSON) and the analysis version. */
    bool setDerivation (const std::string& id, const std::string& parentId, double start, double end, const std::string& operations);
    std::vector<std::string> derivedFrom (const std::string& parentId) const;

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
