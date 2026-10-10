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

bool Catalog::inTransaction (const std::function<bool()>& work)
{
    Transaction t (db);
    if (! t.ok())
        return false;
    return work() && t.commit();
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
    if (version() < 2)
    {
        // 2: a trashed preset file remembers where it came from.
        Transaction t (db);
        if (! t.ok() || ! exec ("ALTER TABLE presets ADD COLUMN trashed_from TEXT NOT NULL DEFAULT ''")
            || ! exec ("UPDATE meta SET value = '2' WHERE key = 'schema_version'") || ! t.commit())
            return false;
    }
    if (version() < 3)
    {
        // 3: a star rating (presets, templates and sounds).
        Transaction t (db);
        if (! t.ok() || ! exec ("ALTER TABLE assets ADD COLUMN rating INTEGER NOT NULL DEFAULT 0")
            || ! exec ("UPDATE meta SET value = '3' WHERE key = 'schema_version'") || ! t.commit())
            return false;
    }
    if (version() < 4)
    {
        // 4: what a scan found about each file, the user's classifications, the Inbox.
        Transaction t (db);
        if (! t.ok()
            || ! exec ("ALTER TABLE scan_files ADD COLUMN rejected INTEGER NOT NULL DEFAULT 0; "
                       "ALTER TABLE scan_files ADD COLUMN duration REAL NOT NULL DEFAULT 0; "
                       "ALTER TABLE scan_files ADD COLUMN sample_rate REAL NOT NULL DEFAULT 0; "
                       "ALTER TABLE scan_files ADD COLUMN channels INTEGER NOT NULL DEFAULT 0; "
                       "ALTER TABLE scan_files ADD COLUMN format TEXT NOT NULL DEFAULT ''; "
                       "ALTER TABLE scan_files ADD COLUMN error TEXT NOT NULL DEFAULT ''; "
                       "CREATE INDEX IF NOT EXISTS scan_files_root ON scan_files (root_id, state); "
                       "CREATE TABLE classification_overrides (content_hash TEXT PRIMARY KEY, classification TEXT NOT NULL, updated INTEGER NOT NULL); "
                       "CREATE TABLE inbox_items (path TEXT PRIMARY KEY, folder TEXT NOT NULL, state TEXT NOT NULL, size INTEGER NOT NULL DEFAULT 0, "
                       "mtime INTEGER NOT NULL DEFAULT 0, stable_since INTEGER NOT NULL DEFAULT 0, content_hash TEXT NOT NULL DEFAULT '', "
                       "asset_id TEXT NOT NULL DEFAULT '', error TEXT NOT NULL DEFAULT '', attempts INTEGER NOT NULL DEFAULT 0, "
                       "detected INTEGER NOT NULL, updated INTEGER NOT NULL);")
            || ! exec ("UPDATE meta SET value = '4' WHERE key = 'schema_version'") || ! t.commit())
            return false;
    }
    if (version() < 5)
    {
        // 5: which pack an asset came with and its key there (updates keep identities).
        Transaction t (db);
        if (! t.ok()
            || ! exec ("ALTER TABLE assets ADD COLUMN pack_id TEXT NOT NULL DEFAULT ''; "
                       "ALTER TABLE assets ADD COLUMN pack_key TEXT NOT NULL DEFAULT ''; "
                       "CREATE INDEX IF NOT EXISTS assets_pack ON assets (pack_id, pack_key);")
            || ! exec ("UPDATE meta SET value = '5' WHERE key = 'schema_version'") || ! t.commit())
            return false;
    }
    // Full-text search when this SQLite has FTS5; plain LIKE matching otherwise (R-09).
    fts = exec (ftsV1);
    if (fts)
    {
        // The index's own version: its rows are keyed by the assets' rowids (an update is one
        // keyed delete and insert, never a scan). Any other version is rebuilt.
        Statement indexVersion (db, "SELECT value FROM meta WHERE key = 'fts_version'");
        const bool indexCurrent = indexVersion.step() == SQLITE_ROW && indexVersion.text (0) == "3";
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
            exec ("INSERT OR REPLACE INTO meta (key, value) VALUES ('fts_version', '3')");
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
    Statement s (db, "INSERT INTO assets (id, type, origin, name, category, notes, favourite, created, modified, rating) VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?8, ?9)");
    s.bind (1, uuid).bind (2, std::string (toString (a.type))).bind (3, std::string (toString (a.origin))).bind (4, a.name).bind (5, a.category).bind (6, a.notes)
        .bind (7, a.favourite ? 1 : 0).bind (8, t).bind (9, std::clamp (a.rating, 0, 5));
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

std::optional<std::string> Catalog::presetWithFile (const std::string& file, bool includeTrashed) const
{
    Statement s (db, "SELECT p.asset_id FROM presets p JOIN assets a ON a.id = p.asset_id WHERE p.file = ?1 AND (?2 = 1 OR a.trashed_at IS NULL) "
                     "ORDER BY a.created LIMIT 1");
    s.bind (1, file).bind (2, includeTrashed ? 1 : 0);
    if (s.step() != SQLITE_ROW)
        return std::nullopt;
    return s.text (0);
}

std::optional<std::string> Catalog::savePreset (const Asset& a, const PresetInfo& info)
{
    const auto existing = presetWithFile (info.file);
    if (! existing)
        return addPreset (a, info);
    Transaction t (db);
    if (! t.ok())
        return std::nullopt;
    Statement asset (db, "UPDATE assets SET name = ?1, type = ?2, modified = ?3 WHERE id = ?4");
    asset.bind (1, a.name).bind (2, std::string (toString (a.type == AssetType::sound ? AssetType::preset : a.type))).bind (3, now()).bind (4, *existing);
    Statement preset (db, "UPDATE presets SET state_version = ?1, layers = ?2, root = ?3 WHERE asset_id = ?4");
    preset.bind (1, info.stateVersion).bind (2, info.layers).bind (3, info.root).bind (4, *existing);
    Statement clear (db, "DELETE FROM preset_sounds WHERE preset_id = ?1");
    clear.bind (1, *existing);
    if (! asset.run() || ! preset.run() || ! clear.run())
        return std::nullopt;
    for (const auto& hash : info.soundHashes)
    {
        Statement d (db, "INSERT OR IGNORE INTO preset_sounds (preset_id, content_hash) VALUES (?1, ?2)");
        d.bind (1, *existing).bind (2, hash);
        if (! d.run())
            return std::nullopt;
    }
    reindex (*existing);
    if (! t.commit())
        return std::nullopt;
    return existing;
}

bool Catalog::movePreset (const std::string& id, const std::string& newFile, const std::string& newName, const std::string& from)
{
    Transaction t (db);
    if (! t.ok())
        return false;
    Statement p (db, "UPDATE presets SET file = ?1, trashed_from = ?2 WHERE asset_id = ?3");
    p.bind (1, newFile).bind (2, from).bind (3, id);
    if (! p.run() || sqlite3_changes (db) != 1)
        return false;
    if (! newName.empty())
    {
        Statement a (db, "UPDATE assets SET name = ?1, modified = ?2 WHERE id = ?3");
        a.bind (1, newName).bind (2, now()).bind (3, id);
        if (! a.run())
            return false;
    }
    reindex (id);
    return t.commit();
}

std::string Catalog::trashedFrom (const std::string& id) const
{
    Statement s (db, "SELECT trashed_from FROM presets WHERE asset_id = ?1");
    s.bind (1, id);
    return s.step() == SQLITE_ROW ? s.text (0) : std::string();
}

std::string Catalog::provenanceOf (const std::string& contentHash) const
{
    Statement s (db, "SELECT provenance FROM sounds s JOIN assets a ON a.id = s.asset_id WHERE s.content_hash = ?1 AND a.trashed_at IS NULL");
    s.bind (1, contentHash);
    std::string best = "unknown";
    auto rank = [] (const std::string& p) { return p == "cleared" ? 5 : p == "original" ? 4 : p == "user" ? 3 : p == "licensed" ? 2 : 0; };
    while (s.step() == SQLITE_ROW)
        if (rank (s.text (0)) > rank (best))
            best = s.text (0);
    return best;
}

bool Catalog::setProvenance (const std::string& contentHash, const std::string& provenance)
{
    Statement s (db, "UPDATE sounds SET provenance = ?1 WHERE content_hash = ?2");
    s.bind (1, provenance).bind (2, contentHash);
    return s.run();
}

std::optional<Asset> Catalog::asset (const std::string& id) const
{
    Statement s (db, "SELECT id, type, origin, name, category, notes, favourite, created, modified, trashed_at, rating, pack_id FROM assets WHERE id = ?1");
    s.bind (1, id);
    if (s.step() != SQLITE_ROW)
        return std::nullopt;
    Asset a;
    a.packId = s.text (11);
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
    a.rating = static_cast<int> (s.integer (10));
    return a;
}

std::optional<SoundInfo> Catalog::sound (const std::string& id) const
{
    Statement s (db, "SELECT content_hash, managed, format, sample_rate, channels, bit_depth, frames, duration, file_size, root_midi, provenance, "
                     "coalesce(parent_asset, ''), slice_start, slice_end, operations FROM sounds WHERE asset_id = ?1");
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
    i.operations = s.text (14);
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

bool Catalog::purge (const std::string& id)
{
    Transaction t (db);
    if (! t.ok())
        return false;
    if (fts)
    {
        Statement drop (db, "DELETE FROM assets_fts WHERE rowid = (SELECT rowid FROM assets WHERE id = ?1 AND trashed_at IS NOT NULL)");
        drop.bind (1, id);
        if (! drop.run())
            return false;
    }
    // Sounds, presets, tags, history and collection places go with it (ON DELETE CASCADE).
    Statement s (db, "DELETE FROM assets WHERE id = ?1 AND trashed_at IS NOT NULL");
    s.bind (1, id);
    if (! s.run() || sqlite3_changes (db) != 1)
        return false;
    return t.commit();
}

bool Catalog::setRating (const std::string& id, int stars)
{
    Statement s (db, "UPDATE assets SET rating = ?1, modified = ?2 WHERE id = ?3");
    s.bind (1, std::clamp (stars, 0, 5)).bind (2, now()).bind (3, id);
    return s.run() && sqlite3_changes (db) == 1;
}

bool Catalog::setRootMidi (const std::string& id, std::optional<double> midi)
{
    Statement s (db, "UPDATE sounds SET root_midi = ?1 WHERE asset_id = ?2");
    if (midi)
        s.bind (1, std::clamp (*midi, 0.0, 127.0));
    else
        s.bindNull (1);
    s.bind (2, id);
    return s.run() && sqlite3_changes (db) == 1;
}

bool Catalog::contentInLibrary (const std::string& contentHash) const
{
    Statement s (db, "SELECT 1 FROM sounds s JOIN assets a ON a.id = s.asset_id WHERE s.content_hash = ?1 AND a.trashed_at IS NULL LIMIT 1");
    s.bind (1, contentHash);
    return s.step() == SQLITE_ROW;
}

std::int64_t Catalog::lastUsed (const std::string& id) const
{
    Statement s (db, "SELECT coalesce(max(at), 0) FROM history WHERE asset_id = ?1");
    s.bind (1, id);
    return s.step() == SQLITE_ROW ? s.integer (0) : 0;
}

std::int64_t Catalog::presetBytes (const std::string& id) const
{
    Statement s (db, "SELECT coalesce(sum(size), 0) FROM (SELECT ps.content_hash, max(so.file_size) AS size FROM preset_sounds ps "
                     "JOIN sounds so ON so.content_hash = ps.content_hash WHERE ps.preset_id = ?1 GROUP BY ps.content_hash)");
    s.bind (1, id);
    return s.step() == SQLITE_ROW ? s.integer (0) : 0;
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

bool Catalog::renameCollection (const std::string& collectionId, const std::string& name)
{
    if (name.empty())
        return false;
    std::vector<std::string> members;
    {
        Statement m (db, "SELECT asset_id FROM collection_items WHERE collection_id = ?1");
        m.bind (1, collectionId);
        while (m.step() == SQLITE_ROW)
            members.push_back (m.text (0));
    }
    Statement s (db, "UPDATE collections SET name = ?1 WHERE id = ?2");
    s.bind (1, name).bind (2, collectionId);
    if (! s.run() || sqlite3_changes (db) != 1)
        return false;
    for (const auto& id : members)   // collection names are searchable
        reindex (id);
    return true;
}

bool Catalog::deleteCollection (const std::string& collectionId)
{
    std::vector<std::string> members;
    {
        Statement m (db, "SELECT asset_id FROM collection_items WHERE collection_id = ?1");
        m.bind (1, collectionId);
        while (m.step() == SQLITE_ROW)
            members.push_back (m.text (0));
    }
    Statement s (db, "DELETE FROM collections WHERE id = ?1");
    s.bind (1, collectionId);
    if (! s.run() || sqlite3_changes (db) != 1)
        return false;
    for (const auto& id : members)
        reindex (id);
    return true;
}

std::vector<Collection> Catalog::collections (std::optional<AssetType> type) const
{
    std::vector<Collection> list;
    Statement s (db, "SELECT c.id, c.name, (SELECT count(*) FROM collection_items ci JOIN assets a ON a.id = ci.asset_id "
                     "WHERE ci.collection_id = c.id AND a.trashed_at IS NULL AND (?1 = '' OR a.type = ?1)) FROM collections c ORDER BY c.name COLLATE NOCASE");
    s.bind (1, type ? std::string (toString (*type)) : std::string());
    while (s.step() == SQLITE_ROW)
        list.push_back ({ s.text (0), s.text (1), static_cast<int> (s.integer (2)) });
    return list;
}

std::vector<Collection> Catalog::collectionsOf (const std::string& assetId) const
{
    std::vector<Collection> list;
    Statement s (db, "SELECT c.id, c.name FROM collections c JOIN collection_items ci ON ci.collection_id = c.id WHERE ci.asset_id = ?1 ORDER BY c.name COLLATE NOCASE");
    s.bind (1, assetId);
    while (s.step() == SQLITE_ROW)
        list.push_back ({ s.text (0), s.text (1), 0 });
    return list;
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
                     "coalesce((SELECT k.name FROM packs k WHERE k.id = a.pack_id), (SELECT p.root FROM presets p WHERE p.asset_id = a.id), '') "
                     "FROM assets a WHERE a.id = ?1");
    s.bind (1, id);
    s.run();
}

//==============================================================================
// Scanning

std::optional<std::string> Catalog::addScanRoot (const std::string& path, const std::string& include, const std::string& exclude)
{
    {
        Statement found (db, "SELECT id FROM scan_roots WHERE path = ?1");
        found.bind (1, path);
        if (found.step() == SQLITE_ROW)
        {
            const auto id = found.text (0);
            Statement update (db, "UPDATE scan_roots SET include_glob = ?1, exclude_glob = ?2 WHERE id = ?3");
            update.bind (1, include).bind (2, exclude).bind (3, id);
            return update.run() ? std::optional<std::string> (id) : std::nullopt;
        }
    }
    Statement id (db, "SELECT lower(hex(randomblob(16)))");
    if (id.step() != SQLITE_ROW)
        return std::nullopt;
    const auto value = id.text (0);
    Statement s (db, "INSERT INTO scan_roots (id, path, include_glob, exclude_glob, added) VALUES (?1, ?2, ?3, ?4, ?5)");
    s.bind (1, value).bind (2, path).bind (3, include).bind (4, exclude).bind (5, now());
    if (! s.run())
    {
        error = sqlite3_errmsg (db);
        return std::nullopt;
    }
    return value;
}

std::vector<ScanRoot> Catalog::scanRoots() const
{
    std::vector<ScanRoot> list;
    Statement s (db, "SELECT id, path, include_glob, exclude_glob, added FROM scan_roots ORDER BY path");
    while (s.step() == SQLITE_ROW)
        list.push_back ({ s.text (0), s.text (1), s.text (2), s.text (3), s.integer (4) });
    return list;
}

std::optional<ScanRoot> Catalog::scanRoot (const std::string& id) const
{
    for (auto& r : scanRoots())
        if (r.id == id)
            return r;
    return std::nullopt;
}

bool Catalog::removeScanRoot (const std::string& id)
{
    Statement s (db, "DELETE FROM scan_roots WHERE id = ?1");
    s.bind (1, id);
    return s.run() && sqlite3_changes (db) == 1;
}

bool Catalog::putScanFile (const ScanFile& f)
{
    Statement s (db, "INSERT INTO scan_files (path, root_id, size, mtime, content_hash, state, asset_id, rejected, duration, sample_rate, channels, format, error) "
                     "VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12, ?13) "
                     "ON CONFLICT (path) DO UPDATE SET root_id = excluded.root_id, size = excluded.size, mtime = excluded.mtime, content_hash = excluded.content_hash, "
                     "state = excluded.state, asset_id = excluded.asset_id, duration = excluded.duration, sample_rate = excluded.sample_rate, "
                     "channels = excluded.channels, format = excluded.format, error = excluded.error");
    s.bind (1, f.path);
    if (f.rootId.empty())
        s.bindNull (2);
    else
        s.bind (2, f.rootId);
    s.bind (3, f.size).bind (4, f.mtime);
    if (f.contentHash.empty())
        s.bindNull (5);
    else
        s.bind (5, f.contentHash);
    s.bind (6, f.state);
    if (f.assetId.empty())
        s.bindNull (7);
    else
        s.bind (7, f.assetId);
    s.bind (8, f.rejected ? 1 : 0).bind (9, f.durationSeconds).bind (10, f.sampleRate).bind (11, f.channels).bind (12, f.format).bind (13, f.error);
    if (! s.run())
    {
        error = sqlite3_errmsg (db);
        return false;
    }
    return true;
}

namespace
{
    ScanFile scanFileFrom (const Statement& s)
    {
        ScanFile f;
        f.path = s.text (0);
        f.rootId = s.text (1);
        f.size = s.integer (2);
        f.mtime = s.integer (3);
        f.contentHash = s.text (4);
        f.state = s.text (5);
        f.assetId = s.text (6);
        f.rejected = s.integer (7) != 0;
        f.durationSeconds = s.real (8);
        f.sampleRate = s.real (9);
        f.channels = static_cast<int> (s.integer (10));
        f.format = s.text (11);
        f.error = s.text (12);
        return f;
    }
    constexpr const char* scanColumns = "path, coalesce(root_id, ''), size, mtime, coalesce(content_hash, ''), state, coalesce(asset_id, ''), rejected, duration, "
                                        "sample_rate, channels, format, error";
}

std::optional<ScanFile> Catalog::scanFile (const std::string& path) const
{
    const std::string sql = std::string ("SELECT ") + scanColumns + " FROM scan_files WHERE path = ?1";
    Statement s (db, sql.c_str());
    s.bind (1, path);
    if (s.step() != SQLITE_ROW)
        return std::nullopt;
    return scanFileFrom (s);
}

std::vector<ScanFile> Catalog::scanFiles (const std::string& rootId, const std::string& state, bool includeRejected, int limit) const
{
    const std::string sql = std::string ("SELECT ") + scanColumns
                          + " FROM scan_files WHERE (?1 = '' OR root_id = ?1) AND (?2 = '' OR state = ?2) AND (?3 = 1 OR rejected = 0) ORDER BY path LIMIT ?4";
    Statement s (db, sql.c_str());
    s.bind (1, rootId).bind (2, state).bind (3, includeRejected ? 1 : 0).bind (4, limit);
    std::vector<ScanFile> list;
    while (s.step() == SQLITE_ROW)
        list.push_back (scanFileFrom (s));
    return list;
}

bool Catalog::setScanFileRejected (const std::string& path, bool rejected)
{
    Statement s (db, "UPDATE scan_files SET rejected = ?1 WHERE path = ?2");
    s.bind (1, rejected ? 1 : 0).bind (2, path);
    return s.run() && sqlite3_changes (db) == 1;
}

bool Catalog::forgetScanFile (const std::string& path)
{
    Statement s (db, "DELETE FROM scan_files WHERE path = ?1");
    s.bind (1, path);
    return s.run();
}

Catalog::CandidateCounts Catalog::candidateCounts() const
{
    CandidateCounts counts;
    Statement s (db, "SELECT COALESCE(SUM(rejected = 0), 0), COALESCE(SUM(rejected = 1), 0) FROM scan_files");
    if (s.step() == SQLITE_ROW)
        counts.scanned = static_cast<int> (s.integer (0)), counts.setAside = static_cast<int> (s.integer (1));
    Statement i (db, "SELECT COUNT(*) FROM inbox_items WHERE state <> 'imported'");
    if (i.step() == SQLITE_ROW)
        counts.inbox = static_cast<int> (i.integer (0));
    return counts;
}

bool Catalog::putAnalysis (const std::string& contentHash, int version, const std::string& classification, const std::string& features)
{
    Statement s (db, "INSERT INTO audio_analysis (content_hash, version, classification, features) VALUES (?1, ?2, ?3, ?4) "
                     "ON CONFLICT (content_hash, version) DO UPDATE SET classification = excluded.classification, features = excluded.features");
    s.bind (1, contentHash).bind (2, version).bind (3, classification).bind (4, features);
    return s.run();
}

std::optional<Catalog::AnalysisRow> Catalog::analysis (const std::string& contentHash) const
{
    Statement s (db, "SELECT version, classification, features FROM audio_analysis WHERE content_hash = ?1 ORDER BY version DESC LIMIT 1");
    s.bind (1, contentHash);
    if (s.step() != SQLITE_ROW)
        return std::nullopt;
    return AnalysisRow { static_cast<int> (s.integer (0)), s.text (1), s.text (2) };
}

bool Catalog::setClassificationOverride (const std::string& contentHash, const std::string& classification)
{
    if (classification.empty())
    {
        Statement s (db, "DELETE FROM classification_overrides WHERE content_hash = ?1");
        s.bind (1, contentHash);
        return s.run();
    }
    Statement s (db, "INSERT INTO classification_overrides (content_hash, classification, updated) VALUES (?1, ?2, ?3) "
                     "ON CONFLICT (content_hash) DO UPDATE SET classification = excluded.classification, updated = excluded.updated");
    s.bind (1, contentHash).bind (2, classification).bind (3, now());
    return s.run();
}

std::string Catalog::classificationOverride (const std::string& contentHash) const
{
    Statement s (db, "SELECT classification FROM classification_overrides WHERE content_hash = ?1");
    s.bind (1, contentHash);
    return s.step() == SQLITE_ROW ? s.text (0) : std::string();
}

std::string Catalog::classificationOf (const std::string& contentHash) const
{
    if (auto o = classificationOverride (contentHash); ! o.empty())
        return o;
    const auto a = analysis (contentHash);
    return a ? a->classification : std::string();
}

bool Catalog::putInboxItem (const InboxItem& i)
{
    Statement s (db, "INSERT INTO inbox_items (path, folder, state, size, mtime, stable_since, content_hash, asset_id, error, attempts, detected, updated) "
                     "VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12) "
                     "ON CONFLICT (path) DO UPDATE SET folder = excluded.folder, state = excluded.state, size = excluded.size, mtime = excluded.mtime, "
                     "stable_since = excluded.stable_since, content_hash = excluded.content_hash, asset_id = excluded.asset_id, error = excluded.error, "
                     "attempts = excluded.attempts, updated = excluded.updated");
    s.bind (1, i.path).bind (2, i.folder).bind (3, i.state).bind (4, i.size).bind (5, i.mtime).bind (6, i.stableSince).bind (7, i.contentHash)
        .bind (8, i.assetId).bind (9, i.error).bind (10, i.attempts).bind (11, i.detected > 0 ? i.detected : now()).bind (12, now());
    if (! s.run())
    {
        error = sqlite3_errmsg (db);
        return false;
    }
    return true;
}

namespace
{
    constexpr const char* inboxColumns = "path, folder, state, size, mtime, stable_since, content_hash, asset_id, error, attempts, detected, updated";
    InboxItem inboxFrom (const Statement& s)
    {
        InboxItem i;
        i.path = s.text (0);
        i.folder = s.text (1);
        i.state = s.text (2);
        i.size = s.integer (3);
        i.mtime = s.integer (4);
        i.stableSince = s.integer (5);
        i.contentHash = s.text (6);
        i.assetId = s.text (7);
        i.error = s.text (8);
        i.attempts = static_cast<int> (s.integer (9));
        i.detected = s.integer (10);
        i.updated = s.integer (11);
        return i;
    }
}

std::optional<InboxItem> Catalog::inboxItem (const std::string& path) const
{
    const std::string sql = std::string ("SELECT ") + inboxColumns + " FROM inbox_items WHERE path = ?1";
    Statement s (db, sql.c_str());
    s.bind (1, path);
    if (s.step() != SQLITE_ROW)
        return std::nullopt;
    return inboxFrom (s);
}

std::vector<InboxItem> Catalog::inboxItems (const std::string& folder) const
{
    const std::string sql = std::string ("SELECT ") + inboxColumns + " FROM inbox_items WHERE (?1 = '' OR folder = ?1) ORDER BY detected DESC, path";
    Statement s (db, sql.c_str());
    s.bind (1, folder);
    std::vector<InboxItem> list;
    while (s.step() == SQLITE_ROW)
        list.push_back (inboxFrom (s));
    return list;
}

bool Catalog::removeInboxItem (const std::string& path)
{
    Statement s (db, "DELETE FROM inbox_items WHERE path = ?1");
    s.bind (1, path);
    return s.run();
}

bool Catalog::setDerivation (const std::string& id, const std::string& parentId, double start, double end, const std::string& operations)
{
    Statement s (db, "UPDATE sounds SET parent_asset = ?1, slice_start = ?2, slice_end = ?3, operations = ?4 WHERE asset_id = ?5");
    if (parentId.empty())
        s.bindNull (1);
    else
        s.bind (1, parentId);
    s.bind (2, start).bind (3, end).bind (4, operations).bind (5, id);
    return s.run() && sqlite3_changes (db) == 1;
}

std::vector<std::string> Catalog::derivedFrom (const std::string& parentId) const
{
    std::vector<std::string> ids;
    Statement s (db, "SELECT s.asset_id FROM sounds s JOIN assets a ON a.id = s.asset_id WHERE s.parent_asset = ?1 AND a.trashed_at IS NULL ORDER BY s.slice_start");
    s.bind (1, parentId);
    while (s.step() == SQLITE_ROW)
        ids.push_back (s.text (0));
    return ids;
}

struct Catalog::Filter
{
    std::string sql;      ///< "FROM ... WHERE ..."
    std::string words;    ///< ?1
    bool useFts = false;
};

Catalog::Filter Catalog::filterFor (const SearchQuery& q) const
{
    Filter f;
    f.sql = "FROM assets a";
    std::vector<std::string> where;
    f.words = ftsQuery (q.text);
    f.useFts = fts && ! f.words.empty();
    if (! f.useFts)
        f.words = q.text;
    if (f.useFts)
        f.sql += " JOIN assets_fts f ON f.rowid = a.rowid";
    if (q.trashedOnly)
        where.push_back ("a.trashed_at IS NOT NULL");
    else if (! q.includeTrashed)
        where.push_back ("a.trashed_at IS NULL");
    if (f.useFts)
        where.push_back ("assets_fts MATCH ?1");
    else if (! q.text.empty())
        where.push_back ("(a.name LIKE '%' || ?1 || '%' OR a.notes LIKE '%' || ?1 || '%')");
    if (q.type)
        where.push_back ("a.type = ?2");
    if (q.origin)
        where.push_back ("a.origin = ?3");
    if (q.favouritesOnly)
        where.push_back ("a.favourite = 1");
    if (q.recentOnly)
        where.push_back ("EXISTS (SELECT 1 FROM history h WHERE h.asset_id = a.id)");
    if (! q.collection.empty())
        where.push_back ("EXISTS (SELECT 1 FROM collection_items ci WHERE ci.asset_id = a.id AND ci.collection_id = ?4)");
    if (! q.category.empty())
        where.push_back ("a.category = ?7 COLLATE NOCASE");
    if (q.minSeconds)
        where.push_back ("EXISTS (SELECT 1 FROM sounds so WHERE so.asset_id = a.id AND so.duration >= ?8)");
    if (q.maxSeconds)
        where.push_back ("EXISTS (SELECT 1 FROM sounds so WHERE so.asset_id = a.id AND so.duration <= ?9)");
    for (std::size_t i = 0; i < q.tags.size() && i < 8; ++i)
        where.push_back ("EXISTS (SELECT 1 FROM asset_tags at JOIN tags t ON t.id = at.tag_id WHERE at.asset_id = a.id AND at.rejected = 0 AND t.name = ?"
                         + std::to_string (10 + i) + ")");
    for (std::size_t i = 0; i < where.size(); ++i)
        f.sql += (i == 0 ? " WHERE " : " AND ") + where[i];
    return f;
}

namespace
{
    template <typename Bindable>
    void bindFilter (Bindable& s, const SearchQuery& q, const std::string& words)
    {
        s.bind (1, words);
        if (q.type)
            s.bind (2, std::string (toString (*q.type)));
        if (q.origin)
            s.bind (3, std::string (toString (*q.origin)));
        s.bind (4, q.collection).bind (7, q.category);
        if (q.minSeconds)
            s.bind (8, *q.minSeconds);
        if (q.maxSeconds)
            s.bind (9, *q.maxSeconds);
        for (std::size_t i = 0; i < q.tags.size() && i < 8; ++i)
            s.bind (static_cast<int> (10 + i), lower (q.tags[i]));
    }
}

std::vector<Asset> Catalog::search (const SearchQuery& q) const
{
    const auto f = filterFor (q);
    std::string sql = "SELECT a.id " + f.sql;
    switch (q.sort)
    {
        case SearchQuery::Sort::name: sql += " ORDER BY a.name COLLATE NOCASE, a.id"; break;
        case SearchQuery::Sort::added: sql += " ORDER BY a.created DESC, a.id"; break;
        case SearchQuery::Sort::rating: sql += " ORDER BY a.rating DESC, a.name COLLATE NOCASE, a.id"; break;
        case SearchQuery::Sort::length:
            sql += " ORDER BY coalesce((SELECT duration FROM sounds WHERE asset_id = a.id), 0), a.name COLLATE NOCASE, a.id";
            break;
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
    bindFilter (s, q, f.words);
    s.bind (5, q.limit).bind (6, q.offset);
    std::vector<std::string> ids;
    while (s.step() == SQLITE_ROW)
        ids.push_back (s.text (0));
    for (const auto& id : ids)
        if (auto a = asset (id))
            out.push_back (*a);
    return out;
}

int Catalog::count (const SearchQuery& q) const
{
    const auto f = filterFor (q);
    const std::string sql = "SELECT count(*) " + f.sql;
    Statement s (db, sql.c_str());
    if (! s)
    {
        error = sqlite3_errmsg (db);
        return 0;
    }
    bindFilter (s, q, f.words);
    return s.step() == SQLITE_ROW ? static_cast<int> (s.integer (0)) : 0;
}

std::vector<std::pair<std::string, int>> Catalog::categoryCounts (AssetType type) const
{
    std::vector<std::pair<std::string, int>> list;
    Statement s (db, "SELECT category, count(*) FROM assets WHERE type = ?1 AND trashed_at IS NULL AND category <> '' GROUP BY category COLLATE NOCASE "
                     "ORDER BY category COLLATE NOCASE");
    s.bind (1, std::string (toString (type)));
    while (s.step() == SQLITE_ROW)
        list.emplace_back (s.text (0), static_cast<int> (s.integer (1)));
    return list;
}

//==============================================================================
// Packs

bool Catalog::putPack (const PackRecord& p)
{
    Statement s (db, "INSERT INTO packs (id, name, version, creator, manifest, installed) VALUES (?1, ?2, ?3, ?4, ?5, ?6) "
                     "ON CONFLICT (id) DO UPDATE SET name = excluded.name, version = excluded.version, creator = excluded.creator, "
                     "manifest = excluded.manifest, installed = excluded.installed");
    s.bind (1, p.id).bind (2, p.name).bind (3, p.version).bind (4, p.creator).bind (5, p.manifest).bind (6, p.installed > 0 ? p.installed : now());
    return s.run();
}

std::optional<Catalog::PackRecord> Catalog::pack (const std::string& id) const
{
    Statement s (db, "SELECT id, name, version, creator, manifest, installed FROM packs WHERE id = ?1");
    s.bind (1, id);
    if (s.step() != SQLITE_ROW)
        return std::nullopt;
    return PackRecord { s.text (0), s.text (1), s.text (2), s.text (3), s.text (4), s.integer (5) };
}

std::vector<Catalog::PackRecord> Catalog::packs() const
{
    std::vector<PackRecord> list;
    Statement s (db, "SELECT id, name, version, creator, manifest, installed FROM packs ORDER BY name COLLATE NOCASE");
    while (s.step() == SQLITE_ROW)
        list.push_back ({ s.text (0), s.text (1), s.text (2), s.text (3), s.text (4), s.integer (5) });
    return list;
}

bool Catalog::removePackRecord (const std::string& id)
{
    Statement s (db, "DELETE FROM packs WHERE id = ?1");
    s.bind (1, id);
    return s.run();
}

bool Catalog::setPackKey (const std::string& assetId, const std::string& packId, const std::string& key)
{
    Statement s (db, "UPDATE assets SET pack_id = ?1, pack_key = ?2 WHERE id = ?3");
    s.bind (1, packId).bind (2, key).bind (3, assetId);
    if (! s.run() || sqlite3_changes (db) != 1)
        return false;
    reindex (assetId);
    return true;
}

std::optional<std::string> Catalog::assetForPackKey (const std::string& packId, const std::string& key) const
{
    Statement s (db, "SELECT id FROM assets WHERE pack_id = ?1 AND pack_key = ?2 ORDER BY created LIMIT 1");
    s.bind (1, packId).bind (2, key);
    if (s.step() != SQLITE_ROW)
        return std::nullopt;
    return s.text (0);
}

std::vector<std::pair<std::string, std::string>> Catalog::packAssets (const std::string& packId) const
{
    std::vector<std::pair<std::string, std::string>> list;
    Statement s (db, "SELECT id, pack_key FROM assets WHERE pack_id = ?1 ORDER BY pack_key");
    s.bind (1, packId);
    while (s.step() == SQLITE_ROW)
        list.emplace_back (s.text (0), s.text (1));
    return list;
}

bool Catalog::replaceSoundContent (const std::string& id, const SoundInfo& info, const std::string& place)
{
    Transaction t (db);
    if (! t.ok())
        return false;
    Statement s (db, "UPDATE sounds SET content_hash = ?1, managed = ?2, format = ?3, sample_rate = ?4, channels = ?5, bit_depth = ?6, frames = ?7, "
                     "duration = ?8, file_size = ?9, provenance = ?10 WHERE asset_id = ?11");
    s.bind (1, info.contentHash).bind (2, info.managed ? 1 : 0).bind (3, info.format).bind (4, info.sampleRate).bind (5, info.channels).bind (6, info.bitDepth)
        .bind (7, info.frames).bind (8, info.durationSeconds).bind (9, info.fileSize).bind (10, info.provenance.empty() ? std::string ("unknown") : info.provenance)
        .bind (11, id);
    Statement where (db, "INSERT INTO source_locations (content_hash, path, kind, last_seen, available) VALUES (?1, ?2, 'original', ?3, 1) "
                         "ON CONFLICT (content_hash, path) DO UPDATE SET last_seen = excluded.last_seen, available = 1");
    where.bind (1, info.contentHash).bind (2, place).bind (3, now());
    Statement touched (db, "UPDATE assets SET modified = ?1 WHERE id = ?2");
    touched.bind (1, now()).bind (2, id);
    if (! s.run() || sqlite3_changes (db) != 1 || ! where.run() || ! touched.run())
        return false;
    reindex (id);
    return t.commit();
}

bool Catalog::updatePreset (const std::string& id, const std::string& name, const PresetInfo& info)
{
    Transaction t (db);
    if (! t.ok())
        return false;
    Statement asset (db, "UPDATE assets SET name = ?1, modified = ?2 WHERE id = ?3");
    asset.bind (1, name).bind (2, now()).bind (3, id);
    Statement preset (db, "UPDATE presets SET file = ?1, root = ?2, state_version = ?3, layers = ?4 WHERE asset_id = ?5");
    preset.bind (1, info.file).bind (2, info.root).bind (3, info.stateVersion).bind (4, info.layers).bind (5, id);
    Statement clear (db, "DELETE FROM preset_sounds WHERE preset_id = ?1");
    clear.bind (1, id);
    if (! asset.run() || sqlite3_changes (db) != 1 || ! preset.run() || ! clear.run())
        return false;
    for (const auto& hash : info.soundHashes)
    {
        Statement d (db, "INSERT OR IGNORE INTO preset_sounds (preset_id, content_hash) VALUES (?1, ?2)");
        d.bind (1, id).bind (2, hash);
        if (! d.run())
            return false;
    }
    reindex (id);
    return t.commit();
}

std::vector<std::pair<std::string, std::string>> Catalog::presetFiles() const
{
    std::vector<std::pair<std::string, std::string>> list;
    Statement s (db, "SELECT asset_id, file FROM presets ORDER BY asset_id");
    while (s.step() == SQLITE_ROW)
        list.emplace_back (s.text (0), s.text (1));
    return list;
}

std::vector<std::string> Catalog::neededContent() const
{
    std::vector<std::string> hashes;
    Statement s (db, "SELECT content_hash FROM sounds UNION SELECT content_hash FROM preset_sounds ORDER BY 1");
    while (s.step() == SQLITE_ROW)
        hashes.push_back (s.text (0));
    return hashes;
}

//==============================================================================
// Backup and restore

bool Catalog::snapshotTo (const std::filesystem::path& file)
{
    std::error_code ec;
    std::filesystem::remove (file, ec);   // VACUUM INTO refuses an existing file
    Statement s (db, "VACUUM INTO ?1");
    s.bind (1, file.string());
    if (! s.run())
    {
        error = sqlite3_errmsg (db);
        return false;
    }
    return true;
}

std::optional<Catalog::MergeReport> Catalog::mergeFrom (const std::filesystem::path& other, bool newerWins,
                                                         const std::vector<std::pair<std::string, std::string>>& movedPrefixes)
{
    {
        Statement attach (db, "ATTACH DATABASE ?1 AS b");
        attach.bind (1, other.string());
        if (! attach.run())
        {
            error = sqlite3_errmsg (db);
            return std::nullopt;
        }
    }
    struct Detach
    {
        Catalog& c;
        ~Detach() { c.exec ("DETACH DATABASE b"); }
    } detach { *this };

    MergeReport report;
    {
        Statement v (db, "SELECT value FROM b.meta WHERE key = 'schema_version'");
        if (v.step() != SQLITE_ROW || std::atoi (v.text (0).c_str()) != schemaVersion)
        {
            error = "the backup's catalog is not at this version (open it first so it is migrated)";
            return std::nullopt;
        }
    }
    Transaction t (db);
    if (! t.ok())
    {
        error = sqlite3_errmsg (db);
        return std::nullopt;
    }
    auto ids = [this] (const char* sql) {
        std::vector<std::string> list;
        Statement s (db, sql);
        while (s.step() == SQLITE_ROW)
            list.push_back (s.text (0));
        return list;
    };
    report.addedIds = ids ("SELECT id FROM b.assets WHERE id NOT IN (SELECT id FROM main.assets) ORDER BY id");
    const auto both = ids ("SELECT x.id FROM b.assets x JOIN main.assets m ON m.id = x.id ORDER BY x.id");
    const auto newer = ids ("SELECT x.id FROM b.assets x JOIN main.assets m ON m.id = x.id WHERE x.modified > m.modified ORDER BY x.id");
    for (const auto& id : both)
    {
        const bool takeBackup = newerWins && std::find (newer.begin(), newer.end(), id) != newer.end();
        (takeBackup ? report.updatedIds : report.keptIds).push_back (id);
    }
    constexpr const char* assetColumns = "id, type, origin, name, category, notes, favourite, created, modified, trashed_at, rating, pack_id, pack_key";
    const std::string insertAssets = std::string ("INSERT INTO main.assets (") + assetColumns + ") SELECT " + assetColumns
                                   + " FROM b.assets WHERE id NOT IN (SELECT id FROM main.assets)";
    constexpr const char* soundColumns = "asset_id, content_hash, managed, format, sample_rate, channels, bit_depth, frames, duration, file_size, root_midi, "
                                         "provenance, parent_asset, slice_start, slice_end, operations";
    const std::string insertSounds = std::string ("INSERT OR IGNORE INTO main.sounds (") + soundColumns + ") SELECT " + soundColumns
                                   + " FROM b.sounds WHERE asset_id IN (SELECT id FROM main.assets)";
    if (! exec (insertAssets.c_str()) || ! exec (insertSounds.c_str())
        || ! exec ("INSERT OR IGNORE INTO main.presets (asset_id, file, root, state_version, layers, trashed_from) "
                   "SELECT asset_id, file, root, state_version, layers, trashed_from FROM b.presets WHERE asset_id IN (SELECT id FROM main.assets)")
        || ! exec ("INSERT OR IGNORE INTO main.preset_sounds (preset_id, content_hash) SELECT preset_id, content_hash FROM b.preset_sounds "
                   "WHERE preset_id IN (SELECT id FROM main.assets)"))
    {
        error = sqlite3_errmsg (db);
        return std::nullopt;
    }
    // A record in both that is newer in the backup (newerWins): the backup's fields.
    for (const auto& id : report.updatedIds)
    {
        Statement a (db, "UPDATE main.assets SET (name, category, notes, favourite, modified, trashed_at, rating) = "
                         "(SELECT name, category, notes, favourite, modified, trashed_at, rating FROM b.assets WHERE id = ?1) WHERE id = ?1");
        a.bind (1, id);
        Statement p (db, "UPDATE main.presets SET (file, state_version, layers) = (SELECT file, state_version, layers FROM b.presets WHERE asset_id = ?1) "
                         "WHERE asset_id = ?1 AND EXISTS (SELECT 1 FROM b.presets WHERE asset_id = ?1)");
        p.bind (1, id);
        Statement r (db, "UPDATE main.sounds SET root_midi = (SELECT root_midi FROM b.sounds WHERE asset_id = ?1) "
                         "WHERE asset_id = ?1 AND EXISTS (SELECT 1 FROM b.sounds WHERE asset_id = ?1)");
        r.bind (1, id);
        if (! a.run() || ! p.run() || ! r.run())
            return std::nullopt;
    }
    // Another machine's folders: presets that were in them are where this one keeps them.
    for (const auto& [from, to] : movedPrefixes)
    {
        if (from.empty() || from == to)
            continue;
        Statement m (db, "UPDATE main.presets SET file = ?2 || substr(file, length(?1) + 1) WHERE substr(file, 1, length(?1)) = ?1 "
                         "AND asset_id IN (SELECT id FROM b.assets)");
        m.bind (1, from).bind (2, to);
        if (! m.run())
            return std::nullopt;
    }
    auto changes = [this] (const char* sql, int& counter) {
        if (! exec (sql))
            return false;
        counter += sqlite3_changes (db);
        return true;
    };
    int ignored = 0;
    if (! changes ("INSERT OR IGNORE INTO main.collections (id, name, created) SELECT id, name, created FROM b.collections", report.collections)
        || ! changes ("INSERT OR IGNORE INTO main.collection_items (collection_id, asset_id) SELECT collection_id, asset_id FROM b.collection_items "
                      "WHERE asset_id IN (SELECT id FROM main.assets) AND collection_id IN (SELECT id FROM main.collections)",
                      report.collectionItems)
        || ! changes ("INSERT OR IGNORE INTO main.tags (name) SELECT name FROM b.tags", report.tags)
        || ! changes ("INSERT OR IGNORE INTO main.asset_tags (asset_id, tag_id, source, confidence, rejected) "
                      "SELECT x.asset_id, m.id, x.source, x.confidence, x.rejected FROM b.asset_tags x JOIN b.tags bt ON bt.id = x.tag_id "
                      "JOIN main.tags m ON m.name = bt.name WHERE x.asset_id IN (SELECT id FROM main.assets)",
                      ignored)
        || ! changes ("INSERT INTO main.history (asset_id, at, action, layer) SELECT x.asset_id, x.at, x.action, x.layer FROM b.history x "
                      "WHERE x.asset_id IN (SELECT id FROM main.assets) AND NOT EXISTS (SELECT 1 FROM main.history h "
                      "WHERE h.asset_id = x.asset_id AND h.at = x.at AND h.action = x.action)",
                      report.history)
        || ! changes ("INSERT OR IGNORE INTO main.source_locations (content_hash, path, kind, last_seen, available) "
                      "SELECT content_hash, path, kind, last_seen, available FROM b.source_locations",
                      ignored)
        || ! changes ("INSERT OR IGNORE INTO main.audio_analysis (content_hash, version, classification, features) "
                      "SELECT content_hash, version, classification, features FROM b.audio_analysis",
                      ignored)
        || ! changes ("INSERT OR IGNORE INTO main.classification_overrides (content_hash, classification, updated) "
                      "SELECT content_hash, classification, updated FROM b.classification_overrides",
                      ignored)
        || ! changes ("INSERT OR IGNORE INTO main.packs (id, name, version, creator, manifest, installed) "
                      "SELECT id, name, version, creator, manifest, installed FROM b.packs",
                      report.packs))
    {
        error = sqlite3_errmsg (db);
        return std::nullopt;
    }
    for (const auto* list : { &report.addedIds, &report.updatedIds })
        for (const auto& id : *list)
            reindex (id);
    if (! t.commit())
    {
        error = sqlite3_errmsg (db);
        return std::nullopt;
    }
    report.added = static_cast<int> (report.addedIds.size());
    report.kept = static_cast<int> (report.keptIds.size());
    report.updated = static_cast<int> (report.updatedIds.size());
    return report;
}

//==============================================================================
// Caches

namespace
{
    constexpr const char* unusedAnalysis = "content_hash NOT IN (SELECT content_hash FROM sounds) AND content_hash NOT IN (SELECT content_hash FROM preset_sounds) "
                                           "AND content_hash NOT IN (SELECT content_hash FROM scan_files WHERE content_hash IS NOT NULL) "
                                           "AND content_hash NOT IN (SELECT content_hash FROM inbox_items)";
}

Catalog::CacheUsage Catalog::cacheUsage() const
{
    CacheUsage u;
    Statement all (db, "SELECT count(*), coalesce(sum(length(features) + length(classification) + length(content_hash)), 0) FROM audio_analysis");
    if (all.step() == SQLITE_ROW)
        u.analysisRows = static_cast<int> (all.integer (0)), u.analysisBytes = all.integer (1);
    const std::string sql = std::string ("SELECT count(*), coalesce(sum(length(features) + length(classification) + length(content_hash)), 0) FROM audio_analysis WHERE ")
                          + unusedAnalysis;
    Statement unused (db, sql.c_str());
    if (unused.step() == SQLITE_ROW)
        u.unusedAnalysisRows = static_cast<int> (unused.integer (0)), u.unusedAnalysisBytes = unused.integer (1);
    return u;
}

int Catalog::forgetUnusedAnalysis()
{
    const std::string sql = std::string ("DELETE FROM audio_analysis WHERE ") + unusedAnalysis;
    return exec (sql.c_str()) ? sqlite3_changes (db) : 0;
}

} // namespace osp::library
