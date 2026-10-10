// The Library catalog: schema, persistence across reopen, assets of the three types, search
// (text, filters, sort), tags (inferred apart from the user's; a removal sticks), history,
// trash, the dependency of presets on sounds, a newer catalog left alone, several instances
// writing at once.

#include "library/Catalog.h"

#include <sqlite3.h>

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <thread>
#include <vector>

using namespace osp::library;

namespace
{
    struct TempLibrary
    {
        std::filesystem::path dir;
        TempLibrary()
        {
            static std::atomic<int> counter { 0 };
            dir = std::filesystem::temp_directory_path() / ("osp-library-test-" + std::to_string (counter++) + "-" + std::to_string (reinterpret_cast<std::uintptr_t> (this)));
            std::filesystem::remove_all (dir);
            std::filesystem::create_directories (dir);
        }
        ~TempLibrary() { std::filesystem::remove_all (dir); }
        std::filesystem::path file() const { return dir / "catalog.db"; }
    };

    std::unique_ptr<Catalog> openOrFail (const std::filesystem::path& file)
    {
        std::string error;
        auto c = Catalog::open (file, error);
        INFO (error);
        REQUIRE (c != nullptr);
        return c;
    }

    SoundInfo soundOf (const std::string& hash, double seconds = 1.5)
    {
        SoundInfo s;
        s.contentHash = hash;
        s.format = "wav";
        s.sampleRate = 48000.0;
        s.channels = 2;
        s.bitDepth = 24;
        s.frames = static_cast<std::int64_t> (seconds * 48000.0);
        s.durationSeconds = seconds;
        s.fileSize = s.frames * 6;
        s.provenance = "user";
        return s;
    }

    Asset named (const std::string& name, Origin origin = Origin::user)
    {
        Asset a;
        a.name = name;
        a.origin = origin;
        return a;
    }
}

TEST_CASE ("library: a new catalog has the schema, survives reopening and checks out", "[unit][library]")
{
    TempLibrary tmp;
    std::string soundId, presetId;
    {
        auto c = openOrFail (tmp.file());
        CHECK (c->version() == Catalog::schemaVersion);
        CHECK (c->hasFullTextSearch());
        CHECK (c->integrityCheck() == "ok");
        const auto id = c->addSound (named ("Warm Bass C2"), soundOf ("sha256:aaa"));
        REQUIRE (id.has_value());
        soundId = *id;
        PresetInfo info;
        info.file = "Bass/Warm.osppreset";
        info.root = "user";
        info.stateVersion = 10;
        info.layers = 1;
        info.soundHashes = { "sha256:aaa" };
        Asset p = named ("Warm Keys");
        p.type = AssetType::preset;
        const auto pid = c->addPreset (p, info);
        REQUIRE (pid.has_value());
        presetId = *pid;
    }
    // A second session (another plugin instance, a restart) sees everything.
    auto c = openOrFail (tmp.file());
    const auto a = c->asset (soundId);
    REQUIRE (a.has_value());
    CHECK (a->name == "Warm Bass C2");
    CHECK (a->type == AssetType::sound);
    CHECK (a->origin == Origin::user);
    CHECK (a->created > 0);
    const auto s = c->sound (soundId);
    REQUIRE (s.has_value());
    CHECK (s->contentHash == "sha256:aaa");
    CHECK (s->channels == 2);
    CHECK (s->durationSeconds == 1.5);
    CHECK_FALSE (s->rootMidi.has_value());
    const auto p = c->preset (presetId);
    REQUIRE (p.has_value());
    CHECK (p->file == "Bass/Warm.osppreset");
    CHECK (p->soundHashes == std::vector<std::string> { "sha256:aaa" });
    CHECK (c->contentNeeded ("sha256:aaa"));
    CHECK_FALSE (c->contentNeeded ("sha256:bbb"));
    // A trashed preset no longer holds its sounds; restoring it does again.
    REQUIRE (c->trash (presetId));
    CHECK_FALSE (c->contentNeeded ("sha256:aaa"));
    REQUIRE (c->restore (presetId));
    CHECK (c->contentNeeded ("sha256:aaa"));
}

TEST_CASE ("library: one content, several sound records; identities never come from slots or names", "[unit][library]")
{
    TempLibrary tmp;
    auto c = openOrFail (tmp.file());
    const auto a = c->addSound (named ("Kick"), soundOf ("sha256:k1"));
    const auto b = c->addSound (named ("Kick (copy in Drums)"), soundOf ("sha256:k1"));
    REQUIRE (a.has_value());
    REQUIRE (b.has_value());
    CHECK (*a != *b);
    CHECK (a->size() == 36);   // UUID form
    CHECK (c->soundsWithContent ("sha256:k1").size() == 2);
    REQUIRE (c->rename (*a, "Kick Deep"));
    CHECK (c->asset (*a)->name == "Kick Deep");
    CHECK (c->asset (*b)->name == "Kick (copy in Drums)");
    CHECK_FALSE (c->rename (*a, ""));   // a name is never empty
    CHECK_FALSE (c->rename ("no-such-id", "x"));
}

TEST_CASE ("library: search by words, type, origin, tags, collection, favourites, sorted", "[unit][library]")
{
    TempLibrary tmp;
    auto c = openOrFail (tmp.file());
    const auto bass = *c->addSound (named ("Warm Bass Pluck"), soundOf ("sha256:1"));
    const auto pad = *c->addSound (named ("Glass Pad"), soundOf ("sha256:2"));
    const auto voice = *c->addSound (named ("Vowel Ah"), soundOf ("sha256:3"));
    const auto factory = *c->addSound (named ("Factory Bass", Origin::factory), soundOf ("sha256:4"));
    Asset t = named ("Bass Template");
    t.type = AssetType::templateState;
    const auto tmpl = *c->addPreset (t, PresetInfo { "Bass.ospstate", "user", 10, 0, {} });

    auto names = [&c] (SearchQuery q) {
        std::vector<std::string> out;
        for (const auto& a : c->search (q))
            out.push_back (a.name);
        return out;
    };
    SearchQuery q;
    q.text = "bas";   // a prefix
    CHECK (names (q) == std::vector<std::string> { "Bass Template", "Factory Bass", "Warm Bass Pluck" });
    q.type = AssetType::sound;
    CHECK (names (q) == std::vector<std::string> { "Factory Bass", "Warm Bass Pluck" });
    q.origin = Origin::factory;
    CHECK (names (q) == std::vector<std::string> { "Factory Bass" });
    // Words in any order; quotes and operators are just text.
    SearchQuery words;
    words.text = "pluck warm";
    CHECK (names (words) == std::vector<std::string> { "Warm Bass Pluck" });
    words.text = "\"bass\" OR -NEAR( *";
    CHECK (names (words).empty());   // no error, no syntax
    CHECK (c->lastError().empty());

    // Tags and collections are searchable too, and filter.
    REQUIRE (c->addTag (pad, "Texture"));
    REQUIRE (c->addTag (voice, "voice", TagSource::filename, 0.8));
    SearchQuery byTag;
    byTag.tags = { "texture" };
    CHECK (names (byTag) == std::vector<std::string> { "Glass Pad" });
    SearchQuery tagText;
    tagText.text = "textu";
    CHECK (names (tagText) == std::vector<std::string> { "Glass Pad" });
    const auto col = c->createCollection ("Night Session");
    REQUIRE (col.has_value());
    REQUIRE (c->addToCollection (*col, voice));
    REQUIRE (c->addToCollection (*col, bass));
    SearchQuery inCol;
    inCol.collection = *col;
    CHECK (names (inCol) == std::vector<std::string> { "Vowel Ah", "Warm Bass Pluck" });
    SearchQuery colText;
    colText.text = "night";
    CHECK (names (colText).size() == 2);
    REQUIRE (c->removeFromCollection (*col, bass));
    CHECK (names (inCol) == std::vector<std::string> { "Vowel Ah" });

    // Favourites, trash.
    REQUIRE (c->setFavourite (pad, true));
    SearchQuery fav;
    fav.favouritesOnly = true;
    CHECK (names (fav) == std::vector<std::string> { "Glass Pad" });
    REQUIRE (c->trash (factory));
    CHECK (names (q).empty());
    SearchQuery all;
    CHECK (names (all).size() == 4);
    all.includeTrashed = true;
    CHECK (names (all).size() == 5);

    // Recently used first.
    REQUIRE (c->recordUse (voice, "loaded", 0));
    REQUIRE (c->recordUse (pad, "loaded", 1));
    SearchQuery recentFirst;
    recentFirst.type = AssetType::sound;
    recentFirst.sort = SearchQuery::Sort::recentlyUsed;
    const auto order = names (recentFirst);
    REQUIRE (order.size() == 3);
    CHECK ((order[0] == "Glass Pad" || order[0] == "Vowel Ah"));   // the same second: either
    CHECK (order[2] == "Warm Bass Pluck");
    (void) tmpl;
}

TEST_CASE ("library: inferred tags stay apart from the user's, and a removal survives a rescan", "[unit][library]")
{
    TempLibrary tmp;
    auto c = openOrFail (tmp.file());
    const auto id = *c->addSound (named ("strings_pluck_C3.wav"), soundOf ("sha256:s"));
    REQUIRE (c->addTag (id, "pluck", TagSource::filename, 0.9));
    REQUIRE (c->addTag (id, "String", TagSource::folder, 0.6));
    auto tags = c->tagsOf (id);
    REQUIRE (tags.size() == 2);
    CHECK (tags[0].name == "pluck");
    CHECK (tags[0].source == TagSource::filename);
    CHECK (tags[1].name == "string");   // names are case-folded
    // The user removes "pluck"; a rescan infers it again: it stays removed.
    REQUIRE (c->removeTag (id, "pluck"));
    REQUIRE (c->addTag (id, "pluck", TagSource::filename, 0.95));
    tags = c->tagsOf (id);
    REQUIRE (tags.size() == 1);
    CHECK (tags[0].name == "string");
    // The user adds it back themselves: it is theirs now, and inference never changes it.
    REQUIRE (c->addTag (id, "pluck"));
    REQUIRE (c->addTag (id, "pluck", TagSource::audio, 0.2));
    tags = c->tagsOf (id);
    REQUIRE (tags.size() == 2);
    CHECK (tags[0].source == TagSource::user);
    CHECK (tags[0].confidence == 1.0);
    CHECK_FALSE (c->addTag (id, ""));
}

TEST_CASE ("library: history - recent sounds, one entry each, use counts", "[unit][library]")
{
    TempLibrary tmp;
    auto c = openOrFail (tmp.file());
    const auto a = *c->addSound (named ("A"), soundOf ("sha256:a"));
    const auto b = *c->addSound (named ("B"), soundOf ("sha256:b"));
    Asset p = named ("P");
    p.type = AssetType::preset;
    const auto preset = *c->addPreset (p, PresetInfo { "P.osppreset", "user", 10, 1, { "sha256:a" } });
    REQUIRE (c->recordUse (a, "loaded", 0));
    REQUIRE (c->recordUse (b, "loaded", 1));
    REQUIRE (c->recordUse (a, "loaded", 2));
    REQUIRE (c->recordUse (preset, "loaded"));
    const auto recent = c->recent (10, AssetType::sound);
    REQUIRE (recent.size() == 2);
    CHECK (recent[0].assetId == a);   // used last
    CHECK (recent[0].layer == 2);
    CHECK (recent[1].assetId == b);
    CHECK (c->useCount (a) == 2);
    CHECK (c->recent (10).size() == 3);
    REQUIRE (c->trash (b));
    CHECK (c->recent (10, AssetType::sound).size() == 1);
}

TEST_CASE ("library: locations of a content, newest first; searchable by folder", "[unit][library]")
{
    TempLibrary tmp;
    auto c = openOrFail (tmp.file());
    const auto id = *c->addSound (named ("Hit"), soundOf ("sha256:h"));
    REQUIRE (c->addLocation ("sha256:h", "/Users/me/Samples/Field/hit.wav", "original"));
    REQUIRE (c->addLocation ("sha256:h", "/Volumes/Drive/hit copy.wav", "indexed"));
    REQUIRE (c->addLocation ("sha256:h", "/Users/me/Samples/Field/hit.wav", "original"));   // seen again
    CHECK (c->locations ("sha256:h").size() == 2);
    REQUIRE (c->rename (id, "Hit"));   // reindexes (folders join the search text)
    SearchQuery q;
    q.text = "field";
    REQUIRE (c->search (q).size() == 1);
}

TEST_CASE ("library: a catalog from a newer build is left alone; a lost search index is rebuilt", "[unit][library]")
{
    TempLibrary tmp;
    {
        auto c = openOrFail (tmp.file());
        REQUIRE (c->addSound (named ("Keep Me"), soundOf ("sha256:k")).has_value());
    }
    // The search index is derived data: dropped (or damaged), it is rebuilt on open.
    {
        sqlite3* raw = nullptr;
        REQUIRE (sqlite3_open (tmp.file().string().c_str(), &raw) == SQLITE_OK);
        REQUIRE (sqlite3_exec (raw, "DROP TABLE assets_fts", nullptr, nullptr, nullptr) == SQLITE_OK);
        sqlite3_close (raw);
        auto c = openOrFail (tmp.file());
        SearchQuery q;
        q.text = "keep";
        CHECK (c->search (q).size() == 1);
    }
    // Newer schema: refused with a reason, and the file is not touched.
    {
        sqlite3* raw = nullptr;
        REQUIRE (sqlite3_open (tmp.file().string().c_str(), &raw) == SQLITE_OK);
        REQUIRE (sqlite3_exec (raw, "UPDATE meta SET value = '99' WHERE key = 'schema_version'", nullptr, nullptr, nullptr) == SQLITE_OK);
        sqlite3_close (raw);
    }
    const auto size = std::filesystem::file_size (tmp.file());
    std::string error;
    CHECK (Catalog::open (tmp.file(), error) == nullptr);
    CHECK (error.find ("newer") != std::string::npos);
    CHECK (std::filesystem::exists (tmp.file()));
    CHECK (std::filesystem::file_size (tmp.file()) == size);
}

TEST_CASE ("library: several plugin instances write one catalog at once", "[unit][library]")
{
    TempLibrary tmp;
    openOrFail (tmp.file());
    constexpr int instances = 4, each = 60;
    std::atomic<int> failures { 0 };
    std::vector<std::thread> threads;
    for (int t = 0; t < instances; ++t)
        threads.emplace_back ([&, t] {
            std::string error;
            auto c = Catalog::open (tmp.file(), error);   // its own connection, as another instance
            if (c == nullptr)
            {
                ++failures;
                return;
            }
            for (int i = 0; i < each; ++i)
            {
                const auto id = c->addSound (named ("Sound " + std::to_string (t) + "-" + std::to_string (i)), soundOf ("sha256:" + std::to_string (t * 1000 + i)));
                if (! id || ! c->recordUse (*id, "loaded", t % 3) || ! c->addTag (*id, "batch" + std::to_string (t), TagSource::user))
                    ++failures;
            }
        });
    for (auto& th : threads)
        th.join();
    CHECK (failures.load() == 0);
    auto c = openOrFail (tmp.file());
    SearchQuery q;
    q.limit = 10000;
    CHECK (c->search (q).size() == static_cast<std::size_t> (instances * each));
    CHECK (c->integrityCheck() == "ok");
}

namespace
{
    /** A library of `count` sounds with realistic names and tags; returns the seconds to search. */
    struct SearchTimes
    {
        double insertSeconds = 0.0, textSeconds = 0.0, filteredSeconds = 0.0, recentSeconds = 0.0;
        std::size_t textHits = 0, filteredHits = 0;
    };

    SearchTimes buildAndSearch (const std::filesystem::path& file, int count)
    {
        using clock = std::chrono::steady_clock;
        auto c = openOrFail (file);
        const char* kinds[] = { "Bass", "Pad", "Pluck", "Voice", "Texture", "Perc", "Strings", "Synth" };
        const char* moods[] = { "Warm", "Glass", "Dusty", "Bright", "Dark", "Soft", "Broken", "Wide" };
        SearchTimes t;
        const auto start = clock::now();
        for (int i = 0; i < count; ++i)
        {
            const std::string name = std::string (moods[i % 8]) + " " + kinds[(i / 8) % 8] + " " + std::to_string (i);
            const auto id = c->addSound (named (name), soundOf ("sha256:" + std::to_string (i)));
            REQUIRE (id.has_value());
            if (i % 3 == 0)
                c->addTag (*id, kinds[(i / 8) % 8], TagSource::filename, 0.8);
            if (i % 50 == 0)
                c->recordUse (*id, "loaded", 0);
        }
        t.insertSeconds = std::chrono::duration<double> (clock::now() - start).count();
        SearchQuery text;
        text.text = "warm bas";
        text.limit = 200;
        auto s0 = clock::now();
        t.textHits = c->search (text).size();
        t.textSeconds = std::chrono::duration<double> (clock::now() - s0).count();
        SearchQuery filtered;
        filtered.text = "dusty";
        filtered.type = AssetType::sound;
        filtered.tags = { "pluck" };
        filtered.sort = SearchQuery::Sort::recentlyUsed;
        s0 = clock::now();
        t.filteredHits = c->search (filtered).size();
        t.filteredSeconds = std::chrono::duration<double> (clock::now() - s0).count();
        s0 = clock::now();
        c->recent (50, AssetType::sound);
        t.recentSeconds = std::chrono::duration<double> (clock::now() - s0).count();
        return t;
    }
}

TEST_CASE ("library: searching 10,000 sounds stays immediate", "[unit][library]")
{
    TempLibrary tmp;
    const auto t = buildAndSearch (tmp.file(), 10000);
    // "Warm" names every 8th sound, "Bass" every 64-block's first 8: warm bass = 1 in 64.
    CHECK (t.textHits == 157);
    CHECK (t.filteredHits > 0);
    // Generous bounds (a shared CI machine); the measured figures are in docs/library/testing.md.
    CHECK (t.textSeconds < 0.25);
    CHECK (t.filteredSeconds < 0.5);
    CHECK (t.recentSeconds < 0.25);
}

TEST_CASE ("library: measured - catalog of 50,000 sounds", "[.][library-perf]")
{
    TempLibrary tmp;
    for (const int count : { 1000, 10000, 50000 })
    {
        const auto t = buildAndSearch (tmp.dir / ("catalog-" + std::to_string (count) + ".db"), count);
        WARN (count << " sounds: insert " << t.insertSeconds << " s, text search " << 1000.0 * t.textSeconds << " ms (" << t.textHits << " hits), filtered "
                    << 1000.0 * t.filteredSeconds << " ms, recent " << 1000.0 * t.recentSeconds << " ms");
    }
}
