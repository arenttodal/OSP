#include "library/Catalog.h"

#include <sqlite3.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <sstream>

namespace osp::library
{

namespace
{
    std::int64_t now()
    {
        return std::chrono::duration_cast<std::chrono::seconds> (std::chrono::system_clock::now().time_since_epoch()).count();
    }

    /** A prepared statement (finalized on scope exit); binds by position from 1. */
    class Statement
    {
    public:
        Statement (sqlite3* db, const char* sql)
        {
            if (sqlite3_prepare_v2 (db, sql, -1, &stmt, nullptr) != SQLITE_OK)
                stmt = nullptr;
        }
        ~Statement() { sqlite3_finalize (stmt); }
        Statement (const Statement&) = delete;
        Statement& operator= (const Statement&) = delete;

        explicit operator bool() const noexcept { return stmt != nullptr; }
        Statement& bind (int i, const std::string& v) { sqlite3_bind_text (stmt, i, v.c_str(), static_cast<int> (v.size()), SQLITE_TRANSIENT); return *this; }
        Statement& bind (int i, std::int64_t v) { sqlite3_bind_int64 (stmt, i, v); return *this; }
        Statement& bind (int i, int v) { sqlite3_bind_int (stmt, i, v); return *this; }
        Statement& bind (int i, double v) { sqlite3_bind_double (stmt, i, v); return *this; }
        Statement& bindNull (int i) { sqlite3_bind_null (stmt, i); return *this; }
        /** SQLITE_ROW / SQLITE_DONE / an error. */
        int step() { return stmt != nullptr ? sqlite3_step (stmt) : SQLITE_MISUSE; }
        bool run() { return step() == SQLITE_DONE; }
        std::string text (int c) const
        {
            const auto* t = sqlite3_column_text (stmt, c);
            return t != nullptr ? std::string (reinterpret_cast<const char*> (t)) : std::string();
        }
        std::int64_t integer (int c) const { return sqlite3_column_int64 (stmt, c); }
        double real (int c) const { return sqlite3_column_double (stmt, c); }
        bool isNull (int c) const { return sqlite3_column_type (stmt, c) == SQLITE_NULL; }

    private:
        sqlite3_stmt* stmt = nullptr;
    };

    /** One transaction; rolled back unless committed. */
    class Transaction
    {
    public:
        explicit Transaction (sqlite3* d) : db (d) { open = sqlite3_exec (db, "BEGIN IMMEDIATE", nullptr, nullptr, nullptr) == SQLITE_OK; }
        ~Transaction()
        {
            if (open)
                sqlite3_exec (db, "ROLLBACK", nullptr, nullptr, nullptr);
        }
        bool ok() const noexcept { return open; }
        bool commit()
        {
            open = false;
            return sqlite3_exec (db, "COMMIT", nullptr, nullptr, nullptr) == SQLITE_OK;
        }

    private:
        sqlite3* db;
        bool open = false;
    };

    /** A word list for FTS5: every word a quoted prefix ("warm"* "bass"*), so nothing the user
        types is read as query syntax. */
    std::string ftsQuery (const std::string& text)
    {
        std::istringstream in (text);
        std::string word, out;
        while (in >> word)
        {
            std::string clean;
            for (const char c : word)
                if (c != '"')
                    clean += c;
            if (clean.empty())
                continue;
            if (! out.empty())
                out += ' ';
            out += "\"" + clean + "\"*";
        }
        return out;
    }

    std::string lower (std::string s)
    {
        std::transform (s.begin(), s.end(), s.begin(), [] (unsigned char c) { return static_cast<char> (std::tolower (c)); });
        return s;
    }

    const char* tagSourceName (TagSource s) noexcept
    {
        switch (s)
        {
            case TagSource::user: return "user";
            case TagSource::filename: return "filename";
            case TagSource::folder: return "folder";
            case TagSource::audio: return "audio";
        }
        return "user";
    }

    TagSource tagSourceFrom (const std::string& s) noexcept
    {
        if (s == "filename") return TagSource::filename;
        if (s == "folder") return TagSource::folder;
        if (s == "audio") return TagSource::audio;
        return TagSource::user;
    }

    // Version 1. Later versions append steps (never edit a released one).
    constexpr const char* schemaV1 = R"SQL(
CREATE TABLE IF NOT EXISTS meta (key TEXT PRIMARY KEY, value TEXT NOT NULL);
CREATE TABLE assets (
    id TEXT PRIMARY KEY,
    type TEXT NOT NULL CHECK (type IN ('sound', 'preset', 'template')),
    origin TEXT NOT NULL,
    name TEXT NOT NULL,
    category TEXT NOT NULL DEFAULT '',
    notes TEXT NOT NULL DEFAULT '',
    favourite INTEGER NOT NULL DEFAULT 0,
    created INTEGER NOT NULL,
    modified INTEGER NOT NULL,
    trashed_at INTEGER);
CREATE INDEX assets_type ON assets (type, trashed_at);
CREATE TABLE sounds (
    asset_id TEXT PRIMARY KEY REFERENCES assets (id) ON DELETE CASCADE,
    content_hash TEXT NOT NULL,
    managed INTEGER NOT NULL,
    format TEXT NOT NULL DEFAULT '',
    sample_rate REAL NOT NULL DEFAULT 0,
    channels INTEGER NOT NULL DEFAULT 0,
    bit_depth INTEGER NOT NULL DEFAULT 0,
    frames INTEGER NOT NULL DEFAULT 0,
    duration REAL NOT NULL DEFAULT 0,
    file_size INTEGER NOT NULL DEFAULT 0,
    root_midi REAL,
    provenance TEXT NOT NULL DEFAULT 'unknown',
    parent_asset TEXT REFERENCES assets (id) ON DELETE SET NULL,
    slice_start REAL NOT NULL DEFAULT 0,
    slice_end REAL NOT NULL DEFAULT 0,
    operations TEXT NOT NULL DEFAULT '');
CREATE INDEX sounds_content ON sounds (content_hash);
CREATE TABLE presets (
    asset_id TEXT PRIMARY KEY REFERENCES assets (id) ON DELETE CASCADE,
    file TEXT NOT NULL,
    root TEXT NOT NULL,
    state_version INTEGER NOT NULL DEFAULT 0,
    layers INTEGER NOT NULL DEFAULT 0);
CREATE TABLE preset_sounds (
    preset_id TEXT NOT NULL REFERENCES assets (id) ON DELETE CASCADE,
    content_hash TEXT NOT NULL,
    PRIMARY KEY (preset_id, content_hash));
CREATE INDEX preset_sounds_content ON preset_sounds (content_hash);
CREATE TABLE collections (id TEXT PRIMARY KEY, name TEXT NOT NULL, created INTEGER NOT NULL);
CREATE TABLE collection_items (
    collection_id TEXT NOT NULL REFERENCES collections (id) ON DELETE CASCADE,
    asset_id TEXT NOT NULL REFERENCES assets (id) ON DELETE CASCADE,
    PRIMARY KEY (collection_id, asset_id));
CREATE TABLE tags (id INTEGER PRIMARY KEY, name TEXT NOT NULL UNIQUE);
CREATE TABLE asset_tags (
    asset_id TEXT NOT NULL REFERENCES assets (id) ON DELETE CASCADE,
    tag_id INTEGER NOT NULL REFERENCES tags (id) ON DELETE CASCADE,
    source TEXT NOT NULL,
    confidence REAL NOT NULL DEFAULT 1,
    rejected INTEGER NOT NULL DEFAULT 0,
    PRIMARY KEY (asset_id, tag_id));
CREATE TABLE history (
    id INTEGER PRIMARY KEY,
    asset_id TEXT NOT NULL REFERENCES assets (id) ON DELETE CASCADE,
    at INTEGER NOT NULL,
    action TEXT NOT NULL,
    layer INTEGER NOT NULL DEFAULT -1);
CREATE INDEX history_asset ON history (asset_id, at);
CREATE INDEX history_at ON history (at);
CREATE TABLE source_locations (
    content_hash TEXT NOT NULL,
    path TEXT NOT NULL,
    kind TEXT NOT NULL,
    last_seen INTEGER NOT NULL,
    available INTEGER NOT NULL DEFAULT 1,
    PRIMARY KEY (content_hash, path));
CREATE TABLE audio_analysis (
    content_hash TEXT NOT NULL,
    version INTEGER NOT NULL,
    classification TEXT NOT NULL DEFAULT '',
    features TEXT NOT NULL DEFAULT '',
    PRIMARY KEY (content_hash, version));
CREATE TABLE scan_roots (id TEXT PRIMARY KEY, path TEXT NOT NULL UNIQUE, include_glob TEXT NOT NULL DEFAULT '', exclude_glob TEXT NOT NULL DEFAULT '', added INTEGER NOT NULL);
CREATE TABLE scan_files (
    path TEXT PRIMARY KEY,
    root_id TEXT REFERENCES scan_roots (id) ON DELETE CASCADE,
    size INTEGER NOT NULL,
    mtime INTEGER NOT NULL,
    content_hash TEXT,
    state TEXT NOT NULL,
    asset_id TEXT REFERENCES assets (id) ON DELETE SET NULL);
CREATE TABLE import_jobs (id TEXT PRIMARY KEY, path TEXT NOT NULL, state TEXT NOT NULL, error TEXT NOT NULL DEFAULT '', created INTEGER NOT NULL, updated INTEGER NOT NULL);
CREATE TABLE packs (id TEXT PRIMARY KEY, name TEXT NOT NULL, version TEXT NOT NULL, creator TEXT NOT NULL DEFAULT '', manifest TEXT NOT NULL, installed INTEGER NOT NULL);
)SQL";

    constexpr const char* ftsV1 =
        "CREATE VIRTUAL TABLE IF NOT EXISTS assets_fts USING fts5 (asset_id UNINDEXED, name, tags, collections, notes, folder, pack, "
        "tokenize = 'unicode61 remove_diacritics 2');";
}

const char* toString (AssetType t) noexcept
{
    switch (t)
    {
        case AssetType::sound: return "sound";
        case AssetType::preset: return "preset";
        case AssetType::templateState: return "template";
    }
    return "sound";
}

const char* toString (Origin o) noexcept
{
    switch (o)
    {
        case Origin::factory: return "factory";
        case Origin::user: return "user";
        case Origin::pack: return "pack";
        case Origin::captured: return "captured";
        case Origin::external: return "external";
    }
    return "user";
}

std::optional<AssetType> assetTypeFrom (const std::string& s) noexcept
{
    if (s == "sound") return AssetType::sound;
    if (s == "preset") return AssetType::preset;
    if (s == "template") return AssetType::templateState;
    return std::nullopt;
}

std::optional<Origin> originFrom (const std::string& s) noexcept
{
    if (s == "factory") return Origin::factory;
    if (s == "user") return Origin::user;
    if (s == "pack") return Origin::pack;
    if (s == "captured") return Origin::captured;
    if (s == "external") return Origin::external;
    return std::nullopt;
}

//==============================================================================

std::filesystem::path Catalog::defaultFile()
{
    if (const char* dir = std::getenv ("OSP_LIBRARY_DIR"); dir != nullptr && *dir != 0)
        return std::filesystem::path (dir) / "catalog.db";
#if defined(__APPLE__)
    const char* home = std::getenv ("HOME");
    return std::filesystem::path (home != nullptr ? home : ".") / "Library/Application Support/OSP/Library/catalog.db";
#elif defined(_WIN32)
    const char* appData = std::getenv ("APPDATA");
    return std::filesystem::path (appData != nullptr ? appData : ".") / "OSP/Library/catalog.db";
#else
    const char* data = std::getenv ("XDG_DATA_HOME");
    const char* home = std::getenv ("HOME");
    const auto base = data != nullptr && *data != 0 ? std::filesystem::path (data) : std::filesystem::path (home != nullptr ? home : ".") / ".local/share";
    return base / "OSP/Library/catalog.db";
#endif
}

std::unique_ptr<Catalog> Catalog::open (const std::filesystem::path& file, std::string& error)
{
    std::error_code ec;
    std::filesystem::create_directories (file.parent_path(), ec);
    sqlite3* db = nullptr;
    if (sqlite3_open_v2 (file.string().c_str(), &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX, nullptr) != SQLITE_OK)
    {
        error = "cannot open the Library at " + file.string() + (db != nullptr ? std::string (": ") + sqlite3_errmsg (db) : std::string());
        sqlite3_close (db);
        return nullptr;
    }
    std::unique_ptr<Catalog> catalog (new Catalog (db));
    // Several plugin instances (and hosts) share the file: WAL lets readers run beside one
    // writer; a writer waits for a busy lock instead of failing.
    sqlite3_busy_timeout (db, 5000);
    catalog->exec ("PRAGMA journal_mode = WAL");
    catalog->exec ("PRAGMA synchronous = NORMAL");
    catalog->exec ("PRAGMA foreign_keys = ON");
    if (! catalog->migrate())
    {
        error = catalog->error;
        return nullptr;
    }
    return catalog;
}

Catalog::Catalog (sqlite3* d) : db (d) {}

Catalog::~Catalog()
{
    sqlite3_close (db);
}

bool Catalog::exec (const char* sql)
{
    char* message = nullptr;
    if (sqlite3_exec (db, sql, nullptr, nullptr, &message) != SQLITE_OK)
    {
        error = message != nullptr ? message : "database error";
        sqlite3_free (message);
        return false;
    }
    return true;
}

int Catalog::version() const
{
    Statement s (db, "SELECT value FROM meta WHERE key = 'schema_version'");
    return s && s.step() == SQLITE_ROW ? std::atoi (s.text (0).c_str()) : 0;
}

bool Catalog::migrate()
{
    if (! exec ("CREATE TABLE IF NOT EXISTS meta (key TEXT PRIMARY KEY, value TEXT NOT NULL)"))
        return false;
    const int current = version();
    if (current > schemaVersion)
    {
        // Made by a newer build: read-only use would be safe, writing might not be.
        error = "this Library was made by a newer version of the plugin (catalog version " + std::to_string (current) + ")";
        return false;
    }
    if (current < 1)
    {
        Transaction t (db);
        if (! t.ok() || ! exec (schemaV1))
            return false;
        Statement s (db, "INSERT OR REPLACE INTO meta (key, value) VALUES ('schema_version', '1'), ('created', ?1)");
        s.bind (1, std::to_string (now()));
        if (! s.run() || ! t.commit())
            return false;
    }
    // Full-text search when this SQLite has FTS5; plain LIKE matching otherwise (R-09).
    fts = exec (ftsV1);
    if (fts)
    {
        // The index's own version: its rows are keyed by the assets' rowids (an update is one
        // keyed delete and insert, never a scan). Any other version is rebuilt.
        Statement indexVersion (db, "SELECT value FROM meta WHERE key = 'fts_version'");
        const bool indexCurrent = indexVersion.step() == SQLITE_ROW && indexVersion.text (0) == "2";
        Statement count (db, "SELECT (SELECT count(*) FROM assets) - (SELECT count(*) FROM assets_fts)");
        if (! indexCurrent || (count && count.step() == SQLITE_ROW && count.integer (0) != 0))
        {
            // Missing or stale: rebuilt from the tables (it holds nothing else).
            exec ("DELETE FROM assets_fts");
            std::vector<std::string> ids;
            Statement all (db, "SELECT id FROM assets");
            while (all.step() == SQLITE_ROW)
                ids.push_back (all.text (0));
            for (const auto& id : ids)
                reindex (id);
            exec ("INSERT OR REPLACE INTO meta (key, value) VALUES ('fts_version', '2')");
        }
    }
    error.clear();
    return true;
}

std::string Catalog::integrityCheck() const
{
    // FTS5 keeps its index structure per connection and refreshes it when it next reads;
    // PRAGMA integrity_check alone would judge an index another instance changed by the stale
    // copy and report false damage. One read refreshes it.
    if (fts)
    {
        Statement touch (db, "SELECT count(*) FROM assets_fts WHERE assets_fts MATCH '\"osp\"*'");
        touch.step();
    }
    Statement s (db, "PRAGMA integrity_check");
    return s && s.step() == SQLITE_ROW ? s.text (0) : std::string ("cannot check");
}

//==============================================================================

std::optional<std::string> Catalog::addAsset (const Asset& a)
{
    // An identity from SQLite's own generator (not a seeded audio process: rule 2 is about sound).
    Statement id (db, "SELECT lower(hex(randomblob(16)))");
    if (! id || id.step() != SQLITE_ROW)
        return std::nullopt;
    const auto raw = id.text (0);
    const auto uuid = raw.substr (0, 8) + "-" + raw.substr (8, 4) + "-" + raw.substr (12, 4) + "-" + raw.substr (16, 4) + "-" + raw.substr (20);
    const auto t = a.created > 0 ? a.created : now();
    Statement s (db, "INSERT INTO assets (id, type, origin, name, category, notes, favourite, created, modified) VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?8)");
    s.bind (1, uuid).bind (2, std::string (toString (a.type))).bind (3, std::string (toString (a.origin))).bind (4, a.name).bind (5, a.category).bind (6, a.notes)
        .bind (7, a.favourite ? 1 : 0).bind (8, t);
    if (! s.run())
    {
        error = sqlite3_errmsg (db);
        return std::nullopt;
    }
    return uuid;
}

std::optional<std::string> Catalog::addSound (const Asset& a, const SoundInfo& info)
{
    Transaction t (db);
    if (! t.ok())
        return std::nullopt;
    auto record = a;
    record.type = AssetType::sound;
    const auto id = addAsset (record);
    if (! id)
        return std::nullopt;
    Statement s (db, "INSERT INTO sounds (asset_id, content_hash, managed, format, sample_rate, channels, bit_depth, frames, duration, file_size, root_midi, "
                     "provenance, parent_asset, slice_start, slice_end) VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12, ?13, ?14, ?15)");
    s.bind (1, *id).bind (2, info.contentHash).bind (3, info.managed ? 1 : 0).bind (4, info.format).bind (5, info.sampleRate).bind (6, info.channels)
        .bind (7, info.bitDepth).bind (8, info.frames).bind (9, info.durationSeconds).bind (10, info.fileSize)
        .bind (12, info.provenance.empty() ? std::string ("unknown") : info.provenance).bind (14, info.sliceStart).bind (15, info.sliceEnd);
    if (info.rootMidi)
        s.bind (11, *info.rootMidi);
    else
        s.bindNull (11);
    if (info.parentAsset.empty())
        s.bindNull (13);
    else
        s.bind (13, info.parentAsset);
    if (! s.run())
    {
        error = sqlite3_errmsg (db);
        return std::nullopt;
    }
    reindex (*id);
    if (! t.commit())
        return std::nullopt;
    return id;
}

std::optional<Catalog::Added> Catalog::addImportedSound (const Asset& a, const SoundInfo& info, const std::string& path, const std::string& kind)
{
    Transaction t (db);
    if (! t.ok())
    {
        error = sqlite3_errmsg (db);
        return std::nullopt;
    }
    std::string existingId;
    {
        Statement found (db, "SELECT s.asset_id FROM sounds s JOIN source_locations l ON l.content_hash = s.content_hash JOIN assets a ON a.id = s.asset_id "
                             "WHERE s.content_hash = ?1 AND l.path = ?2 AND a.trashed_at IS NULL ORDER BY a.created LIMIT 1");
        found.bind (1, info.contentHash).bind (2, path);
        if (found.step() == SQLITE_ROW)
            existingId = found.text (0);
    }
    if (! existingId.empty())
    {
        Statement seen (db, "UPDATE source_locations SET last_seen = ?1, available = 1 WHERE content_hash = ?2 AND path = ?3");
        seen.bind (1, now()).bind (2, info.contentHash).bind (3, path);
        seen.run();
        if (! t.commit())
            return std::nullopt;
        return Added { existingId, true };
    }
    auto record = a;
    record.type = AssetType::sound;
    const auto id = addAsset (record);
    if (! id)
        return std::nullopt;
    Statement s (db, "INSERT INTO sounds (asset_id, content_hash, managed, format, sample_rate, channels, bit_depth, frames, duration, file_size, root_midi, "
                     "provenance, parent_asset, slice_start, slice_end) VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12, NULL, 0, 0)");
    s.bind (1, *id).bind (2, info.contentHash).bind (3, info.managed ? 1 : 0).bind (4, info.format).bind (5, info.sampleRate).bind (6, info.channels)
        .bind (7, info.bitDepth).bind (8, info.frames).bind (9, info.durationSeconds).bind (10, info.fileSize)
        .bind (12, info.provenance.empty() ? std::string ("unknown") : info.provenance);
    if (info.rootMidi)
        s.bind (11, *info.rootMidi);
    else
        s.bindNull (11);
    Statement where (db, "INSERT INTO source_locations (content_hash, path, kind, last_seen, available) VALUES (?1, ?2, ?3, ?4, 1) "
                         "ON CONFLICT (content_hash, path) DO UPDATE SET last_seen = excluded.last_seen, available = 1");
    where.bind (1, info.contentHash).bind (2, path).bind (3, kind).bind (4, now());
    Statement history (db, "INSERT INTO history (asset_id, at, action, layer) VALUES (?1, ?2, 'imported', -1)");
    history.bind (1, *id).bind (2, now());
    if (! s.run() || ! where.run() || ! history.run())
    {
        error = sqlite3_errmsg (db);
        return std::nullopt;
    }
    reindex (*id);
    if (! t.commit())
        return std::nullopt;
    return Added { *id, false };
}

std::optional<std::string> Catalog::addPreset (const Asset& a, const PresetInfo& info)
{
    Transaction t (db);
    if (! t.ok())
        return std::nullopt;
    auto record = a;
    if (record.type == AssetType::sound)
        record.type = AssetType::preset;
    const auto id = addAsset (record);
    if (! id)
        return std::nullopt;
    Statement s (db, "INSERT INTO presets (asset_id, file, root, state_version, layers) VALUES (?1, ?2, ?3, ?4, ?5)");
    s.bind (1, *id).bind (2, info.file).bind (3, info.root).bind (4, info.stateVersion).bind (5, info.layers);
    if (! s.run())
    {
        error = sqlite3_errmsg (db);
        return std::nullopt;
    }
    for (const auto& hash : info.soundHashes)
    {
        Statement d (db, "INSERT OR IGNORE INTO preset_sounds (preset_id, content_hash) VALUES (?1, ?2)");
        d.bind (1, *id).bind (2, hash);
        if (! d.run())
            return std::nullopt;
    }
    reindex (*id);
    if (! t.commit())
        return std::nullopt;
    return id;
}

std::optional<Asset> Catalog::asset (const std::string& id) const
{
    Statement s (db, "SELECT id, type, origin, name, category, notes, favourite, created, modified, trashed_at FROM assets WHERE id = ?1");
    s.bind (1, id);
    if (s.step() != SQLITE_ROW)
        return std::nullopt;
    Asset a;
    a.id = s.text (0);
    a.type = assetTypeFrom (s.text (1)).value_or (AssetType::sound);
    a.origin = originFrom (s.text (2)).value_or (Origin::user);
    a.name = s.text (3);
    a.category = s.text (4);
    a.notes = s.text (5);
    a.favourite = s.integer (6) != 0;
    a.created = s.integer (7);
    a.modified = s.integer (8);
    a.trashed = ! s.isNull (9);
    return a;
}

std::optional<SoundInfo> Catalog::sound (const std::string& id) const
{
    Statement s (db, "SELECT content_hash, managed, format, sample_rate, channels, bit_depth, frames, duration, file_size, root_midi, provenance, "
                     "parent_asset, slice_start, slice_end FROM sounds WHERE asset_id = ?1");
    s.bind (1, id);
    if (s.step() != SQLITE_ROW)
        return std::nullopt;
    SoundInfo i;
    i.contentHash = s.text (0);
    i.managed = s.integer (1) != 0;
    i.format = s.text (2);
    i.sampleRate = s.real (3);
    i.channels = static_cast<int> (s.integer (4));
    i.bitDepth = static_cast<int> (s.integer (5));
    i.frames = s.integer (6);
    i.durationSeconds = s.real (7);
    i.fileSize = s.integer (8);
    if (! s.isNull (9))
        i.rootMidi = s.real (9);
    i.provenance = s.text (10);
    i.parentAsset = s.text (11);
    i.sliceStart = s.real (12);
    i.sliceEnd = s.real (13);
    return i;
}

std::optional<PresetInfo> Catalog::preset (const std::string& id) const
{
    Statement s (db, "SELECT file, root, state_version, layers FROM presets WHERE asset_id = ?1");
    s.bind (1, id);
    if (s.step() != SQLITE_ROW)
        return std::nullopt;
    PresetInfo p;
    p.file = s.text (0);
    p.root = s.text (1);
    p.stateVersion = static_cast<int> (s.integer (2));
    p.layers = static_cast<int> (s.integer (3));
    Statement d (db, "SELECT content_hash FROM preset_sounds WHERE preset_id = ?1 ORDER BY content_hash");
    d.bind (1, id);
    while (d.step() == SQLITE_ROW)
        p.soundHashes.push_back (d.text (0));
    return p;
}

std::vector<std::string> Catalog::soundsWithContent (const std::string& contentHash) const
{
    std::vector<std::string> ids;
    Statement s (db, "SELECT asset_id FROM sounds WHERE content_hash = ?1 ORDER BY asset_id");
    s.bind (1, contentHash);
    while (s.step() == SQLITE_ROW)
        ids.push_back (s.text (0));
    return ids;
}

bool Catalog::contentNeeded (const std::string& contentHash) const
{
    Statement s (db, "SELECT 1 FROM preset_sounds ps JOIN assets a ON a.id = ps.preset_id WHERE ps.content_hash = ?1 AND a.trashed_at IS NULL LIMIT 1");
    s.bind (1, contentHash);
    return s.step() == SQLITE_ROW;
}

namespace
{
    bool updateText (sqlite3* db, const char* column, const std::string& id, const std::string& value)
    {
        const auto sql = std::string ("UPDATE assets SET ") + column + " = ?1, modified = ?2 WHERE id = ?3";
        Statement s (db, sql.c_str());
        s.bind (1, value).bind (2, now()).bind (3, id);
        return s.run() && sqlite3_changes (db) == 1;
    }
}

bool Catalog::rename (const std::string& id, const std::string& name)
{
    if (name.empty() || ! updateText (db, "name", id, name))
        return false;
    reindex (id);
    return true;
}

bool Catalog::setNotes (const std::string& id, const std::string& notes)
{
    if (! updateText (db, "notes", id, notes))
        return false;
    reindex (id);
    return true;
}

bool Catalog::setCategory (const std::string& id, const std::string& category)
{
    return updateText (db, "category", id, category);
}

bool Catalog::setFavourite (const std::string& id, bool favourite)
{
    Statement s (db, "UPDATE assets SET favourite = ?1, modified = ?2 WHERE id = ?3");
    s.bind (1, favourite ? 1 : 0).bind (2, now()).bind (3, id);
    return s.run() && sqlite3_changes (db) == 1;
}

bool Catalog::trash (const std::string& id)
{
    Statement s (db, "UPDATE assets SET trashed_at = ?1 WHERE id = ?2 AND trashed_at IS NULL");
    s.bind (1, now()).bind (2, id);
    return s.run() && sqlite3_changes (db) == 1;
}

bool Catalog::restore (const std::string& id)
{
    Statement s (db, "UPDATE assets SET trashed_at = NULL WHERE id = ?1 AND trashed_at IS NOT NULL");
    s.bind (1, id);
    return s.run() && sqlite3_changes (db) == 1;
}

//==============================================================================

bool Catalog::addTag (const std::string& id, const std::string& tagName, TagSource source, double confidence)
{
    const auto name = lower (tagName);
    if (name.empty())
        return false;
    Transaction t (db);
    if (! t.ok())
        return false;
    Statement insert (db, "INSERT OR IGNORE INTO tags (name) VALUES (?1)");
    insert.bind (1, name);
    if (! insert.run())
        return false;
    // A user tag always wins (and clears an earlier rejection); an inferred tag never brings
    // back one the user removed, nor replaces the user's own.
    const char* sql = source == TagSource::user
                          ? "INSERT INTO asset_tags (asset_id, tag_id, source, confidence, rejected) SELECT ?1, id, 'user', 1, 0 FROM tags WHERE name = ?2 "
                            "ON CONFLICT (asset_id, tag_id) DO UPDATE SET source = 'user', confidence = 1, rejected = 0"
                          : "INSERT INTO asset_tags (asset_id, tag_id, source, confidence, rejected) SELECT ?1, id, ?3, ?4, 0 FROM tags WHERE name = ?2 "
                            "ON CONFLICT (asset_id, tag_id) DO UPDATE SET confidence = excluded.confidence WHERE asset_tags.source <> 'user' AND asset_tags.rejected = 0";
    Statement s (db, sql);
    s.bind (1, id).bind (2, name);
    if (source != TagSource::user)
        s.bind (3, std::string (tagSourceName (source))).bind (4, confidence);
    if (! s.run())
        return false;
    reindex (id);
    return t.commit();
}

bool Catalog::removeTag (const std::string& id, const std::string& tagName)
{
    Statement s (db, "UPDATE asset_tags SET rejected = 1 WHERE asset_id = ?1 AND tag_id = (SELECT id FROM tags WHERE name = ?2)");
    s.bind (1, id).bind (2, lower (tagName));
    if (! s.run() || sqlite3_changes (db) == 0)
        return false;
    reindex (id);
    return true;
}

std::vector<Tag> Catalog::tagsOf (const std::string& id) const
{
    std::vector<Tag> list;
    Statement s (db, "SELECT t.name, at.source, at.confidence FROM asset_tags at JOIN tags t ON t.id = at.tag_id WHERE at.asset_id = ?1 AND at.rejected = 0 ORDER BY t.name");
    s.bind (1, id);
    while (s.step() == SQLITE_ROW)
        list.push_back ({ s.text (0), tagSourceFrom (s.text (1)), s.real (2) });
    return list;
}

std::optional<std::string> Catalog::createCollection (const std::string& name)
{
    if (name.empty())
        return std::nullopt;
    Statement id (db, "SELECT lower(hex(randomblob(16)))");
    if (id.step() != SQLITE_ROW)
        return std::nullopt;
    const auto value = id.text (0);
    Statement s (db, "INSERT INTO collections (id, name, created) VALUES (?1, ?2, ?3)");
    s.bind (1, value).bind (2, name).bind (3, now());
    if (! s.run())
        return std::nullopt;
    return value;
}

bool Catalog::addToCollection (const std::string& collectionId, const std::string& assetId)
{
    Statement s (db, "INSERT OR IGNORE INTO collection_items (collection_id, asset_id) VALUES (?1, ?2)");
    s.bind (1, collectionId).bind (2, assetId);
    if (! s.run())
        return false;
    reindex (assetId);
    return true;
}

bool Catalog::removeFromCollection (const std::string& collectionId, const std::string& assetId)
{
    Statement s (db, "DELETE FROM collection_items WHERE collection_id = ?1 AND asset_id = ?2");
    s.bind (1, collectionId).bind (2, assetId);
    if (! s.run())
        return false;
    reindex (assetId);
    return true;
}

//==============================================================================

bool Catalog::recordUse (const std::string& id, const std::string& action, int layer)
{
    Statement s (db, "INSERT INTO history (asset_id, at, action, layer) VALUES (?1, ?2, ?3, ?4)");
    s.bind (1, id).bind (2, now()).bind (3, action).bind (4, layer);
    return s.run();
}

std::vector<HistoryEntry> Catalog::recent (int limit, std::optional<AssetType> type) const
{
    std::vector<HistoryEntry> list;
    // The newest entry of each asset (ties: the later row).
    Statement s (db, "SELECT h.asset_id, h.at, h.action, h.layer FROM history h JOIN assets a ON a.id = h.asset_id "
                     "WHERE a.trashed_at IS NULL AND (?1 = '' OR a.type = ?1) AND h.id = (SELECT max(id) FROM history WHERE asset_id = h.asset_id) "
                     "ORDER BY h.at DESC, h.id DESC LIMIT ?2");
    s.bind (1, type ? std::string (toString (*type)) : std::string()).bind (2, limit);
    while (s.step() == SQLITE_ROW)
        list.push_back ({ s.text (0), s.integer (1), s.text (2), static_cast<int> (s.integer (3)) });
    return list;
}

int Catalog::useCount (const std::string& id) const
{
    Statement s (db, "SELECT count(*) FROM history WHERE asset_id = ?1 AND action = 'loaded'");
    s.bind (1, id);
    return s.step() == SQLITE_ROW ? static_cast<int> (s.integer (0)) : 0;
}

bool Catalog::addLocation (const std::string& contentHash, const std::string& path, const std::string& kind)
{
    Statement s (db, "INSERT INTO source_locations (content_hash, path, kind, last_seen, available) VALUES (?1, ?2, ?3, ?4, 1) "
                     "ON CONFLICT (content_hash, path) DO UPDATE SET last_seen = excluded.last_seen, available = 1");
    s.bind (1, contentHash).bind (2, path).bind (3, kind).bind (4, now());
    return s.run();
}

std::vector<std::string> Catalog::locations (const std::string& contentHash) const
{
    std::vector<std::string> list;
    Statement s (db, "SELECT path FROM source_locations WHERE content_hash = ?1 ORDER BY last_seen DESC, path");
    s.bind (1, contentHash);
    while (s.step() == SQLITE_ROW)
        list.push_back (s.text (0));
    return list;
}

//==============================================================================

void Catalog::reindex (const std::string& id)
{
    if (! fts)
        return;
    // Keyed by the asset's rowid: one indexed delete and insert, never a scan of the index.
    Statement drop (db, "DELETE FROM assets_fts WHERE rowid = (SELECT rowid FROM assets WHERE id = ?1)");
    drop.bind (1, id);
    drop.run();
    Statement s (db, "INSERT INTO assets_fts (rowid, asset_id, name, tags, collections, notes, folder, pack) SELECT a.rowid, a.id, a.name, "
                     "coalesce((SELECT group_concat(t.name, ' ') FROM asset_tags at JOIN tags t ON t.id = at.tag_id WHERE at.asset_id = a.id AND at.rejected = 0), ''), "
                     "coalesce((SELECT group_concat(c.name, ' ') FROM collection_items ci JOIN collections c ON c.id = ci.collection_id WHERE ci.asset_id = a.id), ''), "
                     "a.notes, "
                     "coalesce((SELECT group_concat(sl.path, ' ') FROM sounds so JOIN source_locations sl ON sl.content_hash = so.content_hash WHERE so.asset_id = a.id), ''), "
                     "coalesce((SELECT p.root FROM presets p WHERE p.asset_id = a.id), '') "
                     "FROM assets a WHERE a.id = ?1");
    s.bind (1, id);
    s.run();
}

std::vector<Asset> Catalog::search (const SearchQuery& q) const
{
    std::string sql = "SELECT a.id FROM assets a";
    std::vector<std::string> where;
    const auto words = ftsQuery (q.text);
    const bool useFts = fts && ! words.empty();
    if (useFts)
        sql += " JOIN assets_fts f ON f.rowid = a.rowid";
    if (! q.includeTrashed)
        where.push_back ("a.trashed_at IS NULL");
    if (useFts)
        where.push_back ("assets_fts MATCH ?1");
    else if (! q.text.empty())
        where.push_back ("(a.name LIKE '%' || ?1 || '%' OR a.notes LIKE '%' || ?1 || '%')");
    if (q.type)
        where.push_back ("a.type = ?2");
    if (q.origin)
        where.push_back ("a.origin = ?3");
    if (q.favouritesOnly)
        where.push_back ("a.favourite = 1");
    if (! q.collection.empty())
        where.push_back ("EXISTS (SELECT 1 FROM collection_items ci WHERE ci.asset_id = a.id AND ci.collection_id = ?4)");
    for (std::size_t i = 0; i < q.tags.size() && i < 8; ++i)
        where.push_back ("EXISTS (SELECT 1 FROM asset_tags at JOIN tags t ON t.id = at.tag_id WHERE at.asset_id = a.id AND at.rejected = 0 AND t.name = ?"
                         + std::to_string (10 + i) + ")");
    for (std::size_t i = 0; i < where.size(); ++i)
        sql += (i == 0 ? " WHERE " : " AND ") + where[i];
    switch (q.sort)
    {
        case SearchQuery::Sort::name: sql += " ORDER BY a.name COLLATE NOCASE, a.id"; break;
        case SearchQuery::Sort::added: sql += " ORDER BY a.created DESC, a.id"; break;
        case SearchQuery::Sort::recentlyUsed:
            sql += " ORDER BY coalesce((SELECT max(at) FROM history WHERE asset_id = a.id), 0) DESC, a.name COLLATE NOCASE";
            break;
    }
    sql += " LIMIT ?5 OFFSET ?6";
    Statement s (db, sql.c_str());
    std::vector<Asset> out;
    if (! s)
    {
        error = sqlite3_errmsg (db);
        return out;
    }
    s.bind (1, useFts ? words : q.text);
    if (q.type)
        s.bind (2, std::string (toString (*q.type)));
    if (q.origin)
        s.bind (3, std::string (toString (*q.origin)));
    s.bind (4, q.collection).bind (5, q.limit).bind (6, q.offset);
    for (std::size_t i = 0; i < q.tags.size() && i < 8; ++i)
        s.bind (static_cast<int> (10 + i), lower (q.tags[i]));
    std::vector<std::string> ids;
    while (s.step() == SQLITE_ROW)
        ids.push_back (s.text (0));
    for (const auto& id : ids)
        if (auto a = asset (id))
            out.push_back (*a);
    return out;
}

} // namespace osp::library
