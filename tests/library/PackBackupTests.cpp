// Library Stage 10: packs (install, update, remove, export, factory packs, hostile archives),
// backups (a consistent snapshot, restore into an empty or a living Library, conflicts,
// duplicates, damaged and hostile backups, external files) and the analysis cache.

#include "LabelledSet.h"
#include "io/AudioFileIO.h"
#include "io/ContentHash.h"
#include "library/Archive.h"
#include "library/Backup.h"
#include "library/Packs.h"
#include "library/SoundImport.h"

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <fstream>
#include <thread>

using namespace osp;
using namespace osp::library;

namespace
{
    struct Place
    {
        std::filesystem::path dir, store, packs, factory, presets, templates, settings, favourites, samples;
        std::unique_ptr<Catalog> catalog;
        explicit Place (const std::string& name = "a")
        {
            static std::atomic<int> counter { 0 };
            dir = std::filesystem::temp_directory_path() / ("osp-pack-test-" + name + "-" + std::to_string (counter++) + "-" + std::to_string (reinterpret_cast<std::uintptr_t> (this)));
            std::filesystem::remove_all (dir);
            store = dir / "Library" / "store";
            packs = dir / "Library" / "Packs";
            factory = dir / "Library" / "Factory";
            presets = dir / "Documents" / "Presets";
            templates = dir / "Documents" / "Starting States";
            settings = dir / "Library" / "settings.json";
            favourites = presets / "Favourites.txt";
            samples = dir / "Samples";
            for (const auto& d : { store, packs, factory, presets, templates, samples })
                std::filesystem::create_directories (d);
            std::string error;
            catalog = Catalog::open (dir / "Library" / "catalog.db", error);
            REQUIRE (catalog != nullptr);
        }
        ~Place()
        {
            catalog.reset();
            std::error_code ec;
            std::filesystem::remove_all (dir, ec);
        }
        PackPlaces packPlaces (const std::string& version = "1.0.0") const { return { store, packs, factory, version }; }
        BackupPlaces backupPlaces() const { return { store, presets, templates, packs, settings, favourites, "1.0.0" }; }
        /** A sound in the Library (imported like a load). */
        ImportResult sound (const std::string& name, const AudioData& audio, const std::string& provenance = "user")
        {
            const auto path = samples / (name + ".wav");
            std::string error;
            REQUIRE (io::writeAudioFile (path, audio, io::SampleFormat::pcm24, error));
            ImportRequest r;
            r.file = path;
            r.provenance = provenance;
            auto result = importSound (*catalog, store, r);
            REQUIRE (result.ok);
            return result;
        }
        /** A preset file (state XML) needing `hashes`, and its record. */
        std::string preset (const std::string& name, const std::vector<std::string>& hashes, bool isTemplate = false)
        {
            const auto file = (isTemplate ? templates : presets) / (name + (isTemplate ? ".ospstate" : ".osppreset"));
            std::ofstream out (file);
            out << "<OSPSTATE stateVersion=\"10\" keptSlots=\"2\">";
            for (const auto& h : hashes)
                out << "<Instrument contentHash=\"" << h << "\"/>";
            out << "</OSPSTATE>\n";
            out.close();
            Asset a;
            a.type = isTemplate ? AssetType::templateState : AssetType::preset;
            a.name = name;
            PresetInfo info;
            info.file = file.string();
            info.root = "user";
            info.stateVersion = 10;
            info.soundHashes = hashes;
            info.layers = static_cast<int> (hashes.size());
            const auto id = catalog->addPreset (a, info);
            REQUIRE (id);
            return *id;
        }
        int count (std::optional<Origin> origin = {}, bool trashed = false) const
        {
            SearchQuery q;
            q.origin = origin;
            q.trashedOnly = trashed;
            return catalog->count (q);
        }
    };

    AudioData tone (double hz, double seconds, std::uint64_t seed) { return labelled::held (hz, seconds, 48000.0, 0.01, seed); }

    std::string fileText (const std::filesystem::path& p)
    {
        std::ifstream in (p, std::ios::binary);
        return { std::istreambuf_iterator<char> (in), std::istreambuf_iterator<char>() };
    }

    //==========================================================================
    // A zip written by hand, so archives can be made as broken or hostile as an attacker would.
    struct RawEntry
    {
        std::string name, data;
        std::uint32_t attributes = 0;          ///< external attributes (0xA... << 28: a link)
        std::int64_t claimedSize = -1;         ///< a header that lies about the size
    };

    std::uint32_t crc32 (const std::string& data)
    {
        std::uint32_t c = 0xffffffffu;
        for (const unsigned char b : data)
        {
            c ^= b;
            for (int k = 0; k < 8; ++k)
                c = (c >> 1) ^ (0xedb88320u & (0u - (c & 1u)));
        }
        return ~c;
    }

    void writeZip (const std::filesystem::path& file, const std::vector<RawEntry>& entries)
    {
        std::string out, central;
        auto u16 = [] (std::string& s, std::uint32_t v) { s += static_cast<char> (v & 0xff); s += static_cast<char> ((v >> 8) & 0xff); };
        auto u32 = [&u16] (std::string& s, std::uint32_t v) { u16 (s, v & 0xffff); u16 (s, v >> 16); };
        for (const auto& e : entries)
        {
            const auto offset = static_cast<std::uint32_t> (out.size());
            const auto size = static_cast<std::uint32_t> (e.claimedSize >= 0 ? e.claimedSize : static_cast<std::int64_t> (e.data.size()));
            const auto crc = crc32 (e.data);
            u32 (out, 0x04034b50); u16 (out, 20); u16 (out, 0); u16 (out, 0); u16 (out, 0); u16 (out, 0x21);
            u32 (out, crc); u32 (out, static_cast<std::uint32_t> (e.data.size())); u32 (out, size);
            u16 (out, static_cast<std::uint32_t> (e.name.size())); u16 (out, 0);
            out += e.name + e.data;
            u32 (central, 0x02014b50); u16 (central, 0x0314); u16 (central, 20); u16 (central, 0); u16 (central, 0); u16 (central, 0); u16 (central, 0x21);
            u32 (central, crc); u32 (central, static_cast<std::uint32_t> (e.data.size())); u32 (central, size);
            u16 (central, static_cast<std::uint32_t> (e.name.size())); u16 (central, 0); u16 (central, 0); u16 (central, 0); u16 (central, 0);
            u32 (central, e.attributes); u32 (central, offset);
            central += e.name;
        }
        const auto centralOffset = static_cast<std::uint32_t> (out.size());
        out += central;
        u32 (out, 0x06054b50); u16 (out, 0); u16 (out, 0);
        u16 (out, static_cast<std::uint32_t> (entries.size())); u16 (out, static_cast<std::uint32_t> (entries.size()));
        u32 (out, static_cast<std::uint32_t> (central.size())); u32 (out, centralOffset); u16 (out, 0);
        std::ofstream f (file, std::ios::binary);
        f << out;
    }

    std::string sha (const std::string& data)
    {
        const auto tmp = std::filesystem::temp_directory_path() / ("osp-sha-" + std::to_string (std::hash<std::string> {}(data)));
        {
            std::ofstream f (tmp, std::ios::binary);
            f << data;
        }
        const auto h = *io::sha256OfFile (tmp);
        std::filesystem::remove (tmp);
        return h;
    }

    /** A valid one-sound pack's manifest and files, to break one piece at a time. */
    struct HandPack
    {
        std::string wav;
        PackManifest m;
        explicit HandPack (const std::filesystem::path& scratch)
        {
            const auto file = scratch / "hand.wav";
            std::string error;
            REQUIRE (io::writeAudioFile (file, tone (330.0, 0.3, 9), io::SampleFormat::pcm16, error));
            wav = fileText (file);
            m.id = "test.hand";
            m.name = "Hand Pack";
            m.version = "1.0.0";
            PackItem item;
            item.uid = "tone";
            item.sha256 = sha (wav);
            item.path = "sounds/" + item.sha256 + ".wav";
            item.bytes = static_cast<std::int64_t> (wav.size());
            item.name = "Tone";
            m.items.push_back (item);
        }
        std::vector<RawEntry> entries() const { return { { "manifest.json", toJson (m) }, { m.items[0].path, wav } }; }
    };
}

//==============================================================================

TEST_CASE ("library archives: names, text, ids and versions from outside are checked", "[unit][library][packs]")
{
    std::string why;
    CHECK (safeRelativePath ("sounds/abc.wav", 1, why));
    CHECK (safeRelativePath ("presets/Keys/Warm Hymn.osppreset", 2, why));
    for (const char* bad : { "", "/etc/passwd", "../x", "sounds/../../x", "sounds/./x", "a\\b", "C:/x", "sounds/.hidden", "a//b", "sounds/x\n" })
    {
        INFO (bad);
        CHECK_FALSE (safeRelativePath (bad, 4, why));
    }
    CHECK_FALSE (safeRelativePath ("a/b/c/d", 2, why));   // too deep
    CHECK (cleanText ("  Warm\x01 Keys\t\n ", 100) == "Warm Keys");
    CHECK (cleanText ("line one\nline two", 100, true) == "line one\nline two");
    CHECK (cleanText (std::string (5000, 'x'), 200).size() == 200);
    CHECK (cleanText ("caf\xc3\xa9 \xff\xfe", 100).find ('\xff') == std::string::npos);   // invalid UTF-8 replaced
    CHECK (validPackId ("com.example.warm-keys_2"));
    for (const char* bad : { "", ".x", "-x", "Upper", "has space", "a/b", "../x" })
        CHECK_FALSE (validPackId (bad));
    CHECK (compareVersions ("1.10", "1.9") > 0);
    CHECK (compareVersions ("1.0", "1.0.0") == 0);
    CHECK (compareVersions ("2", "10") < 0);
    CHECK_FALSE (validVersion ("1.x"));
    CHECK_FALSE (validVersion ("1.2.3.4.5"));
}

TEST_CASE ("library packs: export a collection, install it elsewhere, update keeps the user's edits, remove", "[unit][library][packs]")
{
    Place a ("maker");
    const auto kick = a.sound ("Kick Deep", labelled::kick (48000.0), "original");
    const auto pad = a.sound ("Pad Warm", tone (220.0, 1.2, 3), "original");
    const auto other = a.sound ("Found Loop", tone (440.0, 0.5, 4), "unknown");
    REQUIRE (a.catalog->addTag (kick.assetId, "kick"));
    REQUIRE (a.catalog->setCategory (kick.assetId, "Percussion"));
    const auto presetId = a.preset ("Warm Hymn", { pad.contentHash, other.contentHash });
    const auto templateId = a.preset ("Two Layers", {}, true);
    const auto collection = *a.catalog->createCollection ("For the pack");
    for (const auto& id : { kick.assetId, presetId, templateId })
        REQUIRE (a.catalog->addToCollection (collection, id));
    SearchQuery inCollection;
    inCollection.collection = collection;
    PackExport request;
    for (const auto& asset : a.catalog->search (inCollection))
        request.assetIds.push_back (asset.id);
    request.id = "com.example.warm";
    request.name = "Warm Things";
    request.creator = "Arent";
    request.license = "Royalty-free for music.";
    const auto file = a.dir / "warm.osppack";
    // A preset brings its sounds; one of them has unknown rights: refused, by name, until confirmed.
    auto made = exportPack (*a.catalog, a.store, request, file, "1.0.0");
    CHECK_FALSE (made.ok);
    CHECK (made.error.find ("Found Loop") != std::string::npos);
    CHECK_FALSE (std::filesystem::exists (file));
    request.confirmUnknownRights = true;
    made = exportPack (*a.catalog, a.store, request, file, "1.0.0");
    INFO (made.error);
    REQUIRE (made.ok);
    CHECK (made.sounds == 3);   // the kick, and the preset's two
    CHECK (made.presets == 1);
    CHECK (made.templates == 1);
    CHECK (a.catalog->provenanceOf (other.contentHash) == "cleared");   // the confirmation is kept

    // Installed on another machine.
    Place b ("user");
    const auto mine = b.sound ("My Voice", tone (180.0, 0.8, 5));
    const auto look = inspectPack (*b.catalog, file, b.packPlaces());
    INFO (look.error);
    REQUIRE (look.ok);
    CHECK (look.manifest.name == "Warm Things");
    CHECK (look.sounds == 3);
    CHECK (b.count (Origin::pack) == 0);   // inspecting changes nothing
    const auto installed = installPack (*b.catalog, file, b.packPlaces());
    INFO (installed.error);
    REQUIRE (installed.ok);
    CHECK (b.count (Origin::pack) == 5);
    CHECK (installed.sounds == 3);
    CHECK (installed.presets == 1);
    CHECK (installed.templates == 1);
    REQUIRE (b.catalog->pack ("com.example.warm"));
    CHECK (b.catalog->pack ("com.example.warm")->version == "1.0.0");
    const auto packKick = b.catalog->assetForPackKey ("com.example.warm", "sound-" + hexOf (kick.contentHash).substr (0, 20));
    REQUIRE (packKick);
    CHECK (b.catalog->asset (*packKick)->category == "Percussion");
    CHECK (b.catalog->asset (*packKick)->packId == "com.example.warm");
    CHECK (findStored (b.store, hexOf (kick.contentHash)));
    const auto packPreset = b.catalog->assetForPackKey ("com.example.warm", presetId);
    REQUIRE (packPreset);
    const auto presetFile = std::filesystem::path (b.catalog->preset (*packPreset)->file);
    CHECK (presetFile.parent_path() == b.packs / "com.example.warm" / "Presets");
    CHECK (std::filesystem::exists (presetFile));
    CHECK (b.catalog->preset (*packPreset)->soundHashes.size() == 2);
    // Installing the same version again repairs, never duplicates.
    const auto again = installPack (*b.catalog, file, b.packPlaces());
    REQUIRE (again.ok);
    CHECK (again.alreadyInstalled);
    CHECK (b.count (Origin::pack) == 5);

    // The user's edits on pack content.
    REQUIRE (b.catalog->setFavourite (*packKick, true));
    REQUIRE (b.catalog->setRating (*packKick, 4));
    REQUIRE (b.catalog->addTag (*packKick, "my favourite kick"));

    // Version 2: the kick's audio changed, the template is gone.
    std::string why;
    auto v2 = parsePackManifest (*ArchiveReader (file).readText ("manifest.json", 1 << 20), why);
    REQUIRE (v2);
    v2->version = "2.0.0";
    const auto newKickFile = b.dir / "kick2.wav";
    std::string error;
    REQUIRE (io::writeAudioFile (newKickFile, labelled::kick (48000.0, 0.6), io::SampleFormat::pcm24, error));
    const auto newKick = fileText (newKickFile);
    std::vector<RawEntry> entries;
    const ArchiveReader v1 (file);
    PackManifest kept = *v2;
    kept.items.clear();
    for (auto item : v2->items)
    {
        if (item.kind == PackItem::Kind::templateState)
            continue;
        std::string data;
        if (item.uid == "sound-" + hexOf (kick.contentHash).substr (0, 20))
        {
            data = newKick;
            item.sha256 = sha (data);
            item.path = "sounds/" + item.sha256 + ".wav";
            item.bytes = static_cast<std::int64_t> (data.size());
        }
        else
        {
            const auto tmp = b.dir / "x";
            REQUIRE (v1.extract (item.path, tmp, item.bytes, item.sha256, error));
            data = fileText (tmp);
            std::filesystem::remove (tmp);
        }
        entries.push_back ({ item.path, data });
        kept.items.push_back (item);
    }
    entries.insert (entries.begin(), { "manifest.json", toJson (kept) });
    const auto file2 = b.dir / "warm-2.osppack";
    writeZip (file2, entries);
    const auto updated = installPack (*b.catalog, file2, b.packPlaces());
    INFO (updated.error);
    REQUIRE (updated.ok);
    CHECK (updated.upgraded);
    CHECK (updated.removedItems == 1);
    CHECK (b.catalog->assetForPackKey ("com.example.warm", "sound-" + hexOf (kick.contentHash).substr (0, 20)) == packKick);   // same identity
    CHECK (b.catalog->sound (*packKick)->contentHash == io::contentId (sha (newKick)));   // the new audio
    CHECK (b.catalog->asset (*packKick)->favourite);                                       // the user's edits kept
    CHECK (b.catalog->asset (*packKick)->rating == 4);
    bool tagKept = false;
    for (const auto& t : b.catalog->tagsOf (*packKick))
        tagKept = tagKept || t.name == "my favourite kick";
    CHECK (tagKept);
    CHECK (b.count (Origin::pack) == 4);
    CHECK (b.catalog->pack ("com.example.warm")->version == "2.0.0");
    // A downgrade is refused with nothing changed.
    const auto down = installPack (*b.catalog, file, b.packPlaces());
    CHECK_FALSE (down.ok);
    CHECK (down.error.find ("newer") != std::string::npos);
    CHECK (b.catalog->pack ("com.example.warm")->version == "2.0.0");
    // A pack that needs a newer plugin is refused.
    CHECK_FALSE (inspectPack (*b.catalog, file2, b.packPlaces ("0.9.0")).ok);

    // Removal: the pack's presets go with its folder; its sounds to the trash; the user's own stay.
    std::vector<std::filesystem::path> trashed;
    const auto removed = removePack (*b.catalog, "com.example.warm", b.packPlaces(), [&trashed] (const std::filesystem::path& p) {
        trashed.push_back (p);
        std::error_code ec;
        std::filesystem::remove_all (p, ec);
        return true;
    });
    REQUIRE (removed.ok);
    CHECK_FALSE (b.catalog->pack ("com.example.warm"));
    CHECK (b.count (Origin::pack) == 0);
    CHECK (b.count (Origin::pack, true) == 3);   // recoverable sounds
    CHECK (trashed.size() == 1);
    CHECK (b.catalog->asset (mine.assetId));
    CHECK_FALSE (b.catalog->asset (mine.assetId)->trashed);
    CHECK (findStored (b.store, hexOf (kick.contentHash)));   // audio stays while anything may need it
}

TEST_CASE ("library packs: hostile or broken archives are refused whole, nothing is written", "[unit][library][packs][security]")
{
    Place p ("hostile");
    const HandPack good (p.dir);
    auto refused = [&p] (const std::vector<RawEntry>& entries, const std::string& expect) {
        static int n = 0;
        const auto file = p.dir / ("bad-" + std::to_string (n++) + ".osppack");
        writeZip (file, entries);
        const auto r = installPack (*p.catalog, file, p.packPlaces());
        INFO (expect << ": " << r.error);
        CHECK_FALSE (r.ok);
        CHECK (r.error.find (expect) != std::string::npos);
        CHECK (p.count() == 0);
        CHECK (p.catalog->packs().empty());
        CHECK (std::filesystem::is_empty (p.store));
        CHECK (std::filesystem::is_empty (p.packs));
    };
    // The good one installs (the test pack itself is sound).
    {
        Place q ("good");
        writeZip (q.dir / "good.osppack", good.entries());
        const auto r = installPack (*q.catalog, q.dir / "good.osppack", q.packPlaces());
        INFO (r.error);
        CHECK (r.ok);
    }
    auto entries = good.entries();
    // Paths that escape, links, duplicates.
    for (const char* escape : { "../../evil.wav", "/tmp/evil.wav", "sounds/..\\..\\evil.wav", "sounds/../../../evil.wav" })
    {
        auto e = entries;
        e.push_back ({ escape, "x" });
        refused (e, "unsafe");
    }
    {
        auto e = entries;
        e.push_back ({ "sounds/link.wav", "/etc/passwd", 0xA1ED0000u });
        refused (e, "link");
    }
    {
        auto e = entries;
        e.push_back (e[1]);
        refused (e, "twice");
    }
    // A header that claims a huge file.
    {
        auto e = entries;
        e.push_back ({ "sounds/huge.wav", "tiny", 0, std::int64_t (3) << 30 });
        refused (e, "too large");
    }
    // The manifest: missing, unreadable, newer, a bad id, a path outside its folder.
    refused ({ entries[1] }, "no manifest");
    refused ({ { "manifest.json", "{ not json" }, entries[1] }, "not readable");
    {
        auto m = toJson (good.m);
        m.replace (m.find ("\"schemaVersion\": 1"), 18, "\"schemaVersion\": 9");
        refused ({ { "manifest.json", m }, entries[1] }, "newer version");
    }
    {
        auto bad = good.m;
        bad.id = "../escape";
        refused ({ { "manifest.json", toJson (bad) }, entries[1] }, "id");
    }
    {
        auto bad = good.m;
        bad.items[0].path = "presets/" + bad.items[0].sha256 + ".wav";
        refused ({ { "manifest.json", toJson (bad) }, { bad.items[0].path, good.wav } }, "not valid");
    }
    // Contents that do not match: a different checksum, a different size, a missing file.
    {
        auto bad = good.m;
        bad.items[0].sha256 = std::string (64, 'a');
        bad.items[0].path = "sounds/" + bad.items[0].sha256 + ".wav";
        refused ({ { "manifest.json", toJson (bad) }, { bad.items[0].path, good.wav } }, "checksum");
    }
    {
        auto bad = good.m;
        bad.items[0].bytes += 10;
        refused ({ { "manifest.json", toJson (bad) }, entries[1] }, "size");
    }
    refused ({ entries[0] }, "missing");
    // Dependencies: a preset needing a sound that is nowhere; a pack that is not installed.
    {
        auto bad = good.m;
        PackItem preset;
        preset.kind = PackItem::Kind::preset;
        preset.uid = "p";
        const std::string xml = "<OSPSTATE stateVersion=\"10\"/>";
        preset.sha256 = sha (xml);
        preset.path = "presets/P.osppreset";
        preset.bytes = static_cast<std::int64_t> (xml.size());
        preset.sounds = { std::string (64, 'b') };
        bad.items.push_back (preset);
        refused ({ { "manifest.json", toJson (bad) }, entries[1], { preset.path, xml } }, "needs a sound");
    }
    {
        auto bad = good.m;
        bad.dependencies.push_back ({ "test.other", "1.0" });
        refused ({ { "manifest.json", toJson (bad) }, entries[1] }, "install it first");
    }
    // Untrusted text is cleaned (control characters, length) before it is shown or stored.
    {
        auto loud = good.m;
        loud.name = std::string ("Evil\x1b[31m Name") + std::string (500, 'x');
        loud.description = std::string (100000, 'd');
        std::string why;
        const auto parsed = parsePackManifest (toJson (loud), why);
        REQUIRE (parsed);
        CHECK (parsed->name.find ('\x1b') == std::string::npos);
        CHECK (parsed->name.size() <= 200);
        CHECK (parsed->description.size() <= 4000);
    }
}

TEST_CASE ("library packs: factory packs install apart from user data, update, and never touch the user's content", "[unit][library][packs]")
{
    Place p ("factory");
    const auto mine = p.sound ("My Kick", labelled::kick (48000.0));
    REQUIRE (p.catalog->rename (mine.assetId, "My Own Kick"));
    const auto shipped = p.dir / "Resources";
    std::filesystem::create_directories (shipped);
    HandPack factory (p.dir);
    factory.m.id = "osp.factory.core";
    factory.m.name = "Core Sounds";
    factory.m.factory = true;
    writeZip (shipped / "core-1.osppack", { { "manifest.json", toJson (factory.m) }, { factory.m.items[0].path, factory.wav } });
    HandPack thirdParty (p.dir);   // not a factory pack: never installed from here
    thirdParty.m.id = "someone.else";
    writeZip (shipped / "other.osppack", thirdParty.entries());
    auto results = installFactoryPacks (*p.catalog, shipped, p.packPlaces());
    REQUIRE (results.size() == 1);
    INFO (results[0].error);
    CHECK (results[0].ok);
    CHECK (p.count (Origin::factory) == 1);
    CHECK_FALSE (p.catalog->pack ("someone.else"));
    // Nothing new: nothing done.
    CHECK (installFactoryPacks (*p.catalog, shipped, p.packPlaces()).empty());
    // A newer factory pack replaces the old one's content; the user's sound is as it was.
    factory.m.version = "1.1.0";
    factory.m.items[0].name = "Tone (new)";
    std::filesystem::remove (shipped / "core-1.osppack");
    writeZip (shipped / "core-2.osppack", { { "manifest.json", toJson (factory.m) }, { factory.m.items[0].path, factory.wav } });
    results = installFactoryPacks (*p.catalog, shipped, p.packPlaces());
    REQUIRE (results.size() == 1);
    CHECK (results[0].upgraded);
    CHECK (p.count (Origin::factory) == 1);
    CHECK (p.catalog->asset (*p.catalog->assetForPackKey ("osp.factory.core", "tone"))->name == "Tone (new)");
    CHECK (p.catalog->asset (mine.assetId)->name == "My Own Kick");
    CHECK (p.catalog->asset (mine.assetId)->origin == Origin::user);
}

//==============================================================================

TEST_CASE ("library backup: a whole Library backed up and restored on another machine, identities kept", "[unit][library][backup]")
{
    Place a ("home");
    const auto kick = a.sound ("Kick", labelled::kick (48000.0));
    const auto pad = a.sound ("Pad", tone (220.0, 1.0, 1));
    const auto gone = a.sound ("Old", tone (110.0, 0.4, 2));
    REQUIRE (a.catalog->trash (gone.assetId));
    REQUIRE (a.catalog->addTag (kick.assetId, "punchy"));
    REQUIRE (a.catalog->setFavourite (kick.assetId, true));
    REQUIRE (a.catalog->setRating (pad.assetId, 5));
    REQUIRE (a.catalog->recordUse (pad.assetId, "loaded", 1));
    const auto collection = *a.catalog->createCollection ("Live set");
    REQUIRE (a.catalog->addToCollection (collection, kick.assetId));
    const auto presetId = a.preset ("Night Drive", { kick.contentHash, pad.contentHash });
    const auto templateId = a.preset ("Blank Two", {}, true);
    {
        std::ofstream (a.favourites) << "Night Drive\n";
        std::ofstream (a.settings) << "{\"schemaVersion\":1,\"previewGainDb\":-9}";
    }
    const auto out = a.dir / "Library.ospbackup";
    const auto made = exportBackup (*a.catalog, a.backupPlaces(), out);
    INFO (made.error);
    REQUIRE (made.ok);
    CHECK (made.sounds == 3);   // the trashed sound's audio too (its restore needs it)
    CHECK (made.presets == 1);
    CHECK (made.templates == 1);
    CHECK (made.notIncluded.empty());
    CHECK (std::filesystem::exists (out / "manifest.json"));
    CHECK_FALSE (std::filesystem::exists (std::filesystem::path (out.string() + ".partial")));
    const auto info = inspectBackup (out);
    REQUIRE (info.ok);
    CHECK (info.presets == 1);
    // A backup is never written over.
    CHECK_FALSE (exportBackup (*a.catalog, a.backupPlaces(), out).ok);

    // A new machine (other folders).
    Place b ("new");
    std::vector<std::filesystem::path> replaced;
    RestoreOptions options;
    const auto r = restoreBackup (*b.catalog, b.backupPlaces(), out, options);
    INFO (r.error);
    REQUIRE (r.ok);
    CHECK (r.added == 5);   // three sounds (one in the trash), a preset, a template
    CHECK (r.soundsAdded == 3);
    CHECK (r.filesAdded == 2);
    // Same identities, same details.
    REQUIRE (b.catalog->asset (kick.assetId));
    CHECK (b.catalog->asset (kick.assetId)->favourite);
    CHECK (b.catalog->asset (pad.assetId)->rating == 5);
    CHECK (b.catalog->asset (gone.assetId)->trashed);
    CHECK (b.catalog->useCount (pad.assetId) >= 1);
    bool punchy = false;
    for (const auto& t : b.catalog->tagsOf (kick.assetId))
        punchy = punchy || t.name == "punchy";
    CHECK (punchy);
    REQUIRE (b.catalog->collectionsOf (kick.assetId).size() == 1);
    CHECK (b.catalog->collectionsOf (kick.assetId)[0].id == collection);
    // Presets live where this machine keeps them, and still find their sounds in the store.
    const auto preset = b.catalog->preset (presetId);
    REQUIRE (preset);
    CHECK (std::filesystem::path (preset->file) == b.presets / "Night Drive.osppreset");
    CHECK (fileText (preset->file) == fileText (a.presets / "Night Drive.osppreset"));
    CHECK (std::filesystem::path (b.catalog->preset (templateId)->file) == b.templates / "Blank Two.ospstate");
    for (const auto& h : preset->soundHashes)
        CHECK (resolveSound (*b.catalog, b.store, h).where == "store");
    CHECK (fileText (b.favourites).find ("Night Drive") != std::string::npos);
    CHECK (fileText (b.settings).find ("-9") != std::string::npos);
    CHECK (b.catalog->integrityCheck() == "ok");
    // Search finds restored records (the index was rebuilt for them).
    SearchQuery q;
    q.text = "punchy";
    CHECK (b.catalog->count (q) == 1);

    // Restoring the same backup again: nothing doubles.
    const auto twice = restoreBackup (*b.catalog, b.backupPlaces(), out, options);
    REQUIRE (twice.ok);
    CHECK (twice.added == 0);
    CHECK (twice.kept == 5);
    CHECK (twice.soundsAdded == 0);
    CHECK (twice.filesSame == 2);
    CHECK (b.count() == 4);   // not in the trash
}

TEST_CASE ("library backup: restoring into a Library in use keeps what is newer and never loses a file", "[unit][library][backup]")
{
    Place a ("old");
    const auto kick = a.sound ("Kick", labelled::kick (48000.0));
    const auto presetId = a.preset ("Song", { kick.contentHash });
    const auto out = a.dir / "Before.ospbackup";
    REQUIRE (exportBackup (*a.catalog, a.backupPlaces(), out).ok);

    // The same Library carries on (same machine): a rename after the backup; a different file
    // where the backup's preset would go.
    std::this_thread::sleep_for (std::chrono::milliseconds (1100));   // a newer modification time
    REQUIRE (a.catalog->rename (kick.assetId, "Kick (renamed later)"));
    const auto songFile = a.presets / "Song.osppreset";
    std::ofstream (songFile) << "<OSPSTATE stateVersion=\"10\" changed=\"1\"/>\n";
    const auto local = fileText (songFile);
    // Keep what is here (the default): the rename and the local file stay.
    auto r = restoreBackup (*a.catalog, a.backupPlaces(), out);
    REQUIRE (r.ok);
    CHECK (r.kept == 2);
    CHECK (a.catalog->asset (kick.assetId)->name == "Kick (renamed later)");
    CHECK (fileText (songFile) == local);
    // The backup is newer for a record that changed only in the backup's Library.
    Place b ("other");
    REQUIRE (restoreBackup (*b.catalog, b.backupPlaces(), out).ok);
    std::this_thread::sleep_for (std::chrono::milliseconds (1100));
    REQUIRE (a.catalog->rename (kick.assetId, "Kick (final)"));
    const auto later = a.dir / "Later.ospbackup";
    REQUIRE (exportBackup (*a.catalog, a.backupPlaces(), later).ok);
    RestoreOptions newer;
    newer.newerWins = true;
    std::vector<std::filesystem::path> trashed;
    newer.moveToTrash = [&trashed] (const std::filesystem::path& p) {
        trashed.push_back (p);
        std::error_code ec;
        std::filesystem::rename (p, p.string() + ".trashed", ec);
        return ! ec;
    };
    r = restoreBackup (*b.catalog, b.backupPlaces(), later, newer);
    REQUIRE (r.ok);
    CHECK (r.updated >= 1);
    CHECK (b.catalog->asset (kick.assetId)->name == "Kick (final)");
    // A preset file that differs from one here, with no record deciding: saved beside it.
    const auto bSong = b.presets / "Song.osppreset";
    std::ofstream (bSong) << "<OSPSTATE stateVersion=\"10\" mine=\"1\"/>\n";
    const auto mine = fileText (bSong);
    Place c ("third");
    std::filesystem::create_directories (c.presets);
    std::ofstream (c.presets / "Song.osppreset") << "<OSPSTATE stateVersion=\"10\" unrelated=\"1\"/>\n";
    const auto unrelated = fileText (c.presets / "Song.osppreset");
    r = restoreBackup (*c.catalog, c.backupPlaces(), out);
    REQUIRE (r.ok);
    CHECK (r.filesBeside == 1);
    CHECK (fileText (c.presets / "Song.osppreset") == unrelated);   // never overwritten
    CHECK (std::filesystem::exists (c.presets / "Song (restored).osppreset"));
    CHECK (std::filesystem::path (c.catalog->preset (presetId)->file) == c.presets / "Song (restored).osppreset");
    CHECK (fileText (bSong) == mine);
}

TEST_CASE ("library backup: a consistent snapshot while another instance writes; external files only when asked; cancel", "[unit][library][backup]")
{
    Place a ("busy");
    for (int i = 0; i < 5; ++i)
        a.sound ("Hit " + std::to_string (i), tone (200.0 + 50.0 * i, 0.2, static_cast<std::uint64_t> (i)));
    std::atomic<bool> stop { false };
    std::thread writer ([&] {
        std::string e;
        auto other = Catalog::open (a.dir / "Library" / "catalog.db", e);
        int n = 0;
        while (! stop)
        {
            Asset s;
            s.name = "Writer " + std::to_string (n++);
            SoundInfo info;
            info.contentHash = "sha256:" + std::string (64, 'c');
            other->addSound (s, info);
        }
    });
    const auto out = a.dir / "Busy.ospbackup";
    const auto made = exportBackup (*a.catalog, a.backupPlaces(), out);
    stop = true;
    writer.join();
    REQUIRE (made.ok);
    {
        std::string e;
        auto snap = Catalog::open (out / "catalog.db", e);
        REQUIRE (snap != nullptr);
        CHECK (snap->integrityCheck() == "ok");
    }
    // A sound whose audio is only outside the store (indexed in place).
    Place b ("external");
    const auto loose = b.sound ("Loose", tone (300.0, 0.3, 7));
    std::filesystem::remove (*findStored (b.store, hexOf (loose.contentHash)));
    REQUIRE (resolveSound (*b.catalog, b.store, loose.contentHash).where == "original");
    auto without = exportBackup (*b.catalog, b.backupPlaces(), b.dir / "Without.ospbackup");
    REQUIRE (without.ok);
    REQUIRE (without.notIncluded.size() == 1);
    CHECK (without.notIncluded[0].find ("include external files") != std::string::npos);
    BackupOptions external;
    external.includeExternal = true;
    auto with = exportBackup (*b.catalog, b.backupPlaces(), b.dir / "With.ospbackup", external);
    REQUIRE (with.ok);
    CHECK (with.external == 1);
    CHECK (with.notIncluded.empty());
    Place c ("restored");
    REQUIRE (restoreBackup (*c.catalog, c.backupPlaces(), b.dir / "With.ospbackup").ok);
    CHECK (resolveSound (*c.catalog, c.store, loose.contentHash).where == "store");
    // Cancelled: nothing is left behind.
    std::atomic<bool> cancel { true };
    BackupOptions cancelled;
    cancelled.cancel = &cancel;
    CHECK_FALSE (exportBackup (*a.catalog, a.backupPlaces(), a.dir / "Cancelled.ospbackup", cancelled).ok);
    CHECK_FALSE (std::filesystem::exists (a.dir / "Cancelled.ospbackup"));
    CHECK_FALSE (std::filesystem::exists (a.dir / "Cancelled.ospbackup.partial"));
}

TEST_CASE ("library backup: damaged and hostile backups are refused or reported, the Library unharmed", "[unit][library][backup][security]")
{
    Place a ("source");
    const auto kick = a.sound ("Kick", labelled::kick (48000.0));
    a.preset ("Song", { kick.contentHash });
    const auto good = a.dir / "Good.ospbackup";
    REQUIRE (exportBackup (*a.catalog, a.backupPlaces(), good).ok);
    auto copyOf = [&a, &good] (const std::string& name) {
        const auto dir = a.dir / (name + ".ospbackup");
        std::filesystem::copy (good, dir, std::filesystem::copy_options::recursive);
        return dir;
    };
    auto manifestOf = [] (const std::filesystem::path& dir) { return fileText (dir / "manifest.json"); };
    auto setManifest = [] (const std::filesystem::path& dir, const std::string& text) { std::ofstream (dir / "manifest.json") << text; };
    Place b ("target");
    const auto before = b.count();
    auto refused = [&] (const std::filesystem::path& dir, const std::string& expect) {
        const auto r = restoreBackup (*b.catalog, b.backupPlaces(), dir);
        INFO (expect << ": " << r.error);
        CHECK_FALSE (r.ok);
        CHECK (r.error.find (expect) != std::string::npos);
        CHECK (b.count() == before);
        CHECK (std::filesystem::is_empty (b.store));
    };
    {
        const auto dir = copyOf ("no-manifest");
        std::filesystem::remove (dir / "manifest.json");
        refused (dir, "no manifest");
    }
    {
        const auto dir = copyOf ("escape");
        auto m = manifestOf (dir);
        m.replace (m.find ("\"presets/Song.osppreset\""), 24, "\"presets/../../../x.txt\"");
        setManifest (dir, m);
        refused (dir, "not valid");
    }
    {
        const auto dir = copyOf ("newer");
        auto m = manifestOf (dir);
        m.replace (m.find ("\"schemaVersion\": 1"), 18, "\"schemaVersion\": 7");
        setManifest (dir, m);
        refused (dir, "newer");
    }
    {
        // A stored sound replaced by a link (to anything outside the backup).
        const auto dir = copyOf ("link");
        for (const auto& e : std::filesystem::directory_iterator (dir / "store"))
        {
            std::filesystem::remove (e.path());
            std::filesystem::create_symlink ("/etc/hosts", e.path());
        }
        refused (dir, "link");
    }
    {
        const auto dir = copyOf ("garbage-db");
        std::ofstream (dir / "catalog.db", std::ios::trunc) << std::string (8192, 'z');
        refused (dir, "catalog");
    }
    {
        // A stored sound damaged (same size, other bytes): reported, never stored; the rest restored.
        const auto dir = copyOf ("damaged");
        for (const auto& e : std::filesystem::directory_iterator (dir / "store"))
        {
            auto bytes = fileText (e.path());
            bytes[bytes.size() / 2] = static_cast<char> (bytes[bytes.size() / 2] ^ 0x5a);
            std::ofstream (e.path(), std::ios::binary | std::ios::trunc) << bytes;
        }
        const auto r = restoreBackup (*b.catalog, b.backupPlaces(), dir);
        REQUIRE (r.ok);
        CHECK (r.soundsAdded == 0);
        CHECK_FALSE (r.notes.empty());
        CHECK (std::filesystem::is_empty (b.store));
    }
}

TEST_CASE ("library storage: the analysis cache, and what can safely be forgotten", "[unit][library][backup]")
{
    Place p ("cache");
    const auto kick = p.sound ("Kick", labelled::kick (48000.0));
    REQUIRE (p.catalog->putAnalysis (kick.contentHash, 1, "likely-one-shot", "{\"class\":\"likely-one-shot\"}"));
    REQUIRE (p.catalog->putAnalysis ("sha256:" + std::string (64, 'd'), 1, "likely-loop", "{\"class\":\"likely-loop\",\"pad\":\"" + std::string (500, 'x') + "\"}"));
    const auto usage = p.catalog->cacheUsage();
    CHECK (usage.analysisRows == 2);
    CHECK (usage.unusedAnalysisRows == 1);
    CHECK (usage.unusedAnalysisBytes > 500);
    CHECK (p.catalog->forgetUnusedAnalysis() == 1);
    CHECK (p.catalog->analysis (kick.contentHash));   // what a record uses stays
    CHECK (p.catalog->cacheUsage().analysisRows == 1);
}
