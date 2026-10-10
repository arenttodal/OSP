// Library Stage 11: stress and security cases across the Library:
// - Unicode and long names;
// - thousands of tags;
// - very short, huge, corrupt and unsupported audio;
// - links in scanned folders;
// - repeated import cancellation;
// - damaged catalog records;
// - a store that cannot be written (the stand-in for a full disk).

#include "LabelledSet.h"
#include "io/AudioFileIO.h"
#include "io/ContentHash.h"
#include "library/Classifier.h"
#include "library/FolderScan.h"
#include "library/Inbox.h"
#include "library/Slicer.h"
#include "library/SoundImport.h"

#include <catch2/catch_test_macros.hpp>
#include <sqlite3.h>

#include <atomic>
#include <chrono>
#include <fstream>
#include <thread>

using namespace osp;
using namespace osp::library;

namespace
{
    struct Place
    {
        std::filesystem::path dir, store, files;
        std::unique_ptr<Catalog> catalog;
        Place()
        {
            static std::atomic<int> counter { 0 };
            dir = std::filesystem::temp_directory_path() / ("osp-qa-test-" + std::to_string (counter++) + "-" + std::to_string (reinterpret_cast<std::uintptr_t> (this)));
            std::filesystem::remove_all (dir);
            store = dir / "store";
            files = dir / "Samples";
            std::filesystem::create_directories (files);
            std::string error;
            catalog = Catalog::open (dir / "catalog.db", error);
            REQUIRE (catalog != nullptr);
        }
        ~Place()
        {
            catalog.reset();
            std::error_code ec;
            std::filesystem::remove_all (dir, ec);
        }
        std::filesystem::path write (const std::filesystem::path& relative, const AudioData& audio, io::SampleFormat format = io::SampleFormat::pcm24)
        {
            const auto path = files / relative;
            std::filesystem::create_directories (path.parent_path());
            std::string error;
            REQUIRE (io::writeAudioFile (path, audio, format, error));
            return path;
        }
        std::filesystem::path bytes (const std::filesystem::path& relative, const std::string& data)
        {
            const auto path = files / relative;
            std::filesystem::create_directories (path.parent_path());
            std::ofstream (path, std::ios::binary) << data;
            return path;
        }
        ImportResult import (const std::filesystem::path& file, const std::atomic<bool>* cancel = nullptr)
        {
            ImportRequest r;
            r.file = file;
            return importSound (*catalog, store, r, cancel);
        }
        int sounds() const
        {
            SearchQuery q;
            q.type = AssetType::sound;
            return catalog->count (q);
        }
        int leftovers() const
        {
            int n = 0;
            std::error_code ec;
            for (const auto& e : std::filesystem::directory_iterator (store, ec))
                n += e.path().string().find (".partial") != std::string::npos ? 1 : 0;
            return n;
        }
    };

    AudioData tone (double hz, double seconds, std::uint64_t seed) { return labelled::held (hz, seconds, 48000.0, 0.01, seed); }

    /** A WAV header (44 bytes) that claims `dataBytes` of 16-bit PCM, whatever follows. */
    std::string wavHeader (std::uint32_t dataBytes, std::uint16_t channels = 1, std::uint32_t rate = 48000)
    {
        std::string h;
        auto u16 = [&h] (std::uint32_t v) { h += static_cast<char> (v & 0xff); h += static_cast<char> ((v >> 8) & 0xff); };
        auto u32 = [&u16] (std::uint32_t v) { u16 (v & 0xffff); u16 (v >> 16); };
        h += "RIFF";
        u32 (36 + dataBytes);
        h += "WAVEfmt ";
        u32 (16); u16 (1); u16 (channels); u32 (rate); u32 (rate * channels * 2u); u16 (channels * 2u); u16 (16);
        h += "data";
        u32 (dataBytes);
        return h;
    }
}

TEST_CASE ("library qa: Unicode and long names everywhere a name goes", "[unit][library][qa]")
{
    Place p;
    const std::string unicode = "K\xc3\xb6lner Glocke \xe9\x90\x98 \xf0\x9f\x8e\xb5";   // Kölner Glocke 鐘 🎵
    const auto file = p.write ("\xc3\x9cn\xc3\xaf" "c\xc3\xb6" "d\xc3\xa9/\xe3\x83\x89\xe3\x83\xa9\xe3\x83\xa0/" + unicode + ".wav", tone (440.0, 0.4, 1));   // Ünïcödé/ドラム/
    const auto imported = p.import (file);
    INFO (imported.error);
    REQUIRE (imported.ok);
    CHECK (p.catalog->asset (imported.assetId)->name == unicode);
    SearchQuery q;
    q.text = "glocke";
    CHECK (p.catalog->count (q) == 1);
    // A name as long as file systems allow (the extension included, 255 bytes).
    const auto longName = std::string (251, 'x');
    const auto longFile = p.write (longName + ".wav", tone (220.0, 0.3, 2));
    const auto longImport = p.import (longFile);
    REQUIRE (longImport.ok);
    CHECK (p.catalog->asset (longImport.assetId)->name == longName);
    // Scans and the Inbox see the same files under the same names.
    const auto root = *p.catalog->scanRoot (*p.catalog->addScanRoot (p.files.string()));
    ScanControl control;
    ScanProgress progress;
    const auto summary = scanFolder (*p.catalog, root, {}, control, progress);
    CHECK (summary.found == 2);
    CHECK (summary.failed == 0);
    CHECK (p.catalog->scanFile (file.string()).has_value());
    reconcileInbox (*p.catalog, { p.files }, 1'000'000);
    reconcileInbox (*p.catalog, { p.files }, 1'010'000);
    REQUIRE (p.catalog->inboxItem (file.string()));
    CHECK (p.catalog->inboxItem (file.string())->state == "imported");   // its bytes are in the Library already
}

TEST_CASE ("library qa: thousands of tags stay quick to search and to list", "[unit][library][qa]")
{
    Place p;
    std::vector<std::string> ids;
    for (int i = 0; i < 600; ++i)
    {
        Asset a;
        a.name = "Sound " + std::to_string (i);
        SoundInfo s;
        s.contentHash = "sha256:" + std::to_string (i);
        const auto id = p.catalog->addSound (a, s);
        REQUIRE (id);
        ids.push_back (*id);
    }
    for (int t = 0; t < 3000; ++t)
        REQUIRE (p.catalog->addTag (ids[static_cast<std::size_t> (t % 600)], "tag" + std::to_string (t)));
    for (int t = 0; t < 120; ++t)
        p.catalog->addTag (ids[0], "many" + std::to_string (t));
    CHECK (p.catalog->tagsOf (ids[0]).size() == 125);
    const auto t0 = std::chrono::steady_clock::now();
    SearchQuery q;
    q.text = "tag2999";
    const auto found = p.catalog->search (q);
    const double ms = std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - t0).count();
    REQUIRE (found.size() == 1);
    CHECK (found[0].id == ids[2999 % 600]);
    CHECK (ms < 100.0);
}

TEST_CASE ("library qa: very short, huge, corrupt and unsupported audio never breaks anything", "[unit][library][qa]")
{
    Place p;
    // Very short: one sample, ten samples.
    for (const int frames : { 1, 10 })
    {
        AudioData a = AudioData::allocate (1, frames, 48000.0);
        a.channels[0][0] = 0.5f;
        const auto file = p.write ("short-" + std::to_string (frames) + ".wav", a, io::SampleFormat::pcm16);
        const auto r = p.import (file);
        INFO (r.error);
        CHECK (r.ok);
        CHECK_NOTHROW (classifySound (a));
        CHECK (sliceRecording (a).size() <= 1);
        const auto trim = suggestTrim (a);
        CHECK (trim.end >= trim.start);
        CHECK (renderSlice (a, { 0, 0 }).numFrames() == 0);
    }
    // Huge: two and a half minutes (classified from its first 120 s, in bounded time).
    {
        const auto file = p.write ("long take.wav", tone (110.0, 150.0, 5), io::SampleFormat::pcm16);
        const auto t0 = std::chrono::steady_clock::now();
        const auto r = p.import (file);
        REQUIRE (r.ok);
        std::string why;
        CHECK (analyseContent (*p.catalog, file.string(), r.contentHash, why));
        const double seconds = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
        CHECK (seconds < 30.0);
        CHECK_FALSE (p.catalog->classificationOf (r.contentHash).empty());
    }
    // Corrupt: random bytes, an empty file, a header promising data that is not there, a header
    // with no channels, a fake AIFF.
    std::string noise (4096, '\0');
    for (std::size_t i = 0; i < noise.size(); ++i)
        noise[i] = static_cast<char> ((i * 2654435761u) >> 13);
    const std::vector<std::filesystem::path> bad {
        p.bytes ("bad/noise.wav", noise),
        p.bytes ("bad/empty.wav", ""),
        p.bytes ("bad/truncated.wav", wavHeader (1'000'000) + std::string (100, '\x01')),
        p.bytes ("bad/no channels.wav", wavHeader (400, 0) + std::string (400, '\x01')),
        p.bytes ("bad/fake.aif", "FORM\x00\x00\x00\x10" "AIFFjunkjunk"),
    };
    const int before = p.sounds();
    for (const auto& file : bad)
    {
        INFO (file.filename().string());
        const auto r = p.import (file);
        CHECK_FALSE (r.ok);
        CHECK_FALSE (r.error.empty());
    }
    CHECK (p.sounds() == before);
    CHECK (p.leftovers() == 0);
    // Unsupported formats are not even looked at by a scan.
    p.bytes ("bad/clip.mp4", "....ftypisom");
    p.bytes ("bad/notes.txt", "hello");
    const auto root = *p.catalog->scanRoot (*p.catalog->addScanRoot ((p.files / "bad").string()));
    ScanControl control;
    ScanProgress progress;
    const auto summary = scanFolder (*p.catalog, root, {}, control, progress);
    CHECK (summary.found == 5);                // the five audio-named files only
    CHECK (summary.failed + summary.analysed == 5);
    CHECK (summary.failed >= 4);               // reported, the scan carried on
}

TEST_CASE ("library qa: links in a scanned or Inbox folder are never followed", "[unit][library][qa][security]")
{
    Place p;
    const auto outside = p.dir / "Private";
    std::filesystem::create_directories (outside);
    std::string error;
    REQUIRE (io::writeAudioFile (outside / "secret.wav", tone (330.0, 0.3, 3), io::SampleFormat::pcm16, error));
    const auto inside = p.write ("real.wav", tone (440.0, 0.3, 4));
    std::filesystem::create_directory_symlink (outside, p.files / "link to private");
    std::filesystem::create_symlink (outside / "secret.wav", p.files / "linked.wav");
    const auto root = *p.catalog->scanRoot (*p.catalog->addScanRoot (p.files.string()));
    ScanControl control;
    ScanProgress progress;
    const auto summary = scanFolder (*p.catalog, root, {}, control, progress);
    CHECK (summary.found == 1);
    CHECK (p.catalog->scanFile (inside.string()).has_value());
    CHECK_FALSE (p.catalog->scanFile ((p.files / "linked.wav").string()).has_value());
    CHECK_FALSE (p.catalog->scanFile ((p.files / "link to private" / "secret.wav").string()).has_value());
    reconcileInbox (*p.catalog, { p.files }, 1'000'000);
    CHECK (p.catalog->inboxItems().size() == 1);
}

TEST_CASE ("library qa: imports cancelled again and again leave nothing behind", "[unit][library][qa]")
{
    Place p;
    const auto file = p.write ("big.wav", tone (220.0, 20.0, 6));   // about 2.8 MB
    int cancelled = 0, finished = 0;
    for (int i = 0; i < 40; ++i)
    {
        std::atomic<bool> cancel { i % 4 == 0 };   // some before they start, the rest mid-way
        std::thread stopper ([&cancel, i] {
            std::this_thread::sleep_for (std::chrono::microseconds (200 * (i % 10)));
            cancel = true;
        });
        const auto r = p.import (file, &cancel);
        stopper.join();
        if (r.ok)
            ++finished;
        else
        {
            CHECK (r.error == "cancelled");
            ++cancelled;
        }
        // The store never has a half copy, and a cancelled import leaves no record.
        CHECK (p.leftovers() == 0);
        CHECK (p.sounds() == (finished > 0 ? 1 : 0));
    }
    CHECK (cancelled >= 10);
    CHECK (p.catalog->integrityCheck() == "ok");
}

TEST_CASE ("library qa: damaged catalog records are read without harm", "[unit][library][qa][security]")
{
    Place p;
    const auto good = p.import (p.write ("ok.wav", tone (440.0, 0.3, 7)));
    REQUIRE (good.ok);
    p.catalog.reset();
    {
        sqlite3* raw = nullptr;
        REQUIRE (sqlite3_open ((p.dir / "catalog.db").string().c_str(), &raw) == SQLITE_OK);
        const char* sql = "INSERT INTO assets (id, type, origin, name, created, modified) VALUES ('bad-1', 'sound', 'martian', 'Name\x01with\x02" "controls', 0, 0);"
                          "INSERT INTO sounds (asset_id, content_hash, managed, sample_rate, channels, duration, root_midi) VALUES ('bad-1', '', 1, -5, -2, -1, 9999);"
                          "INSERT INTO audio_analysis (content_hash, version, classification, features) VALUES ('sha256:junk', 1, 'not-a-class', '{\"type\": [1,2,'), "
                          "('', 99, '', 'not json at all');"
                          "INSERT INTO scan_roots (id, path, added) VALUES ('r', '/nowhere/at/all', 0);"
                          "INSERT INTO scan_files (path, root_id, size, mtime, state) VALUES ('/nowhere/at/all/x.wav', 'r', -1, -1, 'weird');"
                          "INSERT INTO inbox_items (path, folder, state, detected, updated) VALUES ('/nowhere/y.wav', '/nowhere', 'impossible', 0, 0);";
        REQUIRE (sqlite3_exec (raw, sql, nullptr, nullptr, nullptr) == SQLITE_OK);
        sqlite3_close (raw);
    }
    std::string error;
    p.catalog = Catalog::open (p.dir / "catalog.db", error);
    REQUIRE (p.catalog != nullptr);
    CHECK (p.catalog->asset ("bad-1")->origin == Origin::user);   // an unknown origin reads as the safest
    CHECK (p.catalog->sound ("bad-1").has_value());
    SearchQuery all;
    CHECK (p.catalog->search (all).size() == 2);
    CHECK (p.catalog->classificationOf ("sha256:junk") == "not-a-class");
    CHECK_FALSE (soundClassFrom ("not-a-class").has_value());   // shown as "not analysed", never trusted
    CHECK (jsonString ("{\"type\": [1,2,", "type").empty());
    CHECK (jsonStrings ("not json at all", "tags").empty());
    CHECK_NOTHROW (requestWithEvidence (*p.catalog, "/nowhere/x.wav", "sha256:junk", "/nowhere"));
    CHECK_NOTHROW (storageUsage (*p.catalog, p.store));
    CHECK_NOTHROW (resolveSound (*p.catalog, p.store, ""));
    // The odd scan root and Inbox item are offline, not fatal.
    ScanControl control;
    ScanProgress progress;
    CHECK_FALSE (scanFolder (*p.catalog, *p.catalog->scanRoot ("r"), {}, control, progress).error.empty());
    CHECK_NOTHROW (reconcileInbox (*p.catalog, { "/nowhere" }, 1000));
    CHECK (p.catalog->integrityCheck() == "ok");
}

TEST_CASE ("library qa: a store that cannot be written (a full disk) refuses cleanly", "[unit][library][qa]")
{
    Place p;
    const auto file = p.write ("a.wav", tone (440.0, 0.3, 8));
    // The store's place is taken by a file: nothing can be written there.
    std::ofstream (p.store) << "not a folder";
    const auto r = p.import (file);
    CHECK_FALSE (r.ok);
    CHECK (r.error.find ("cannot") != std::string::npos);
    CHECK (p.sounds() == 0);
    DerivedSound d;
    d.name = "Slice";
    CHECK_FALSE (saveDerivedSound (*p.catalog, p.store, tone (440.0, 0.2, 9), d).ok);
    CHECK (p.sounds() == 0);
    CHECK (p.catalog->integrityCheck() == "ok");
}
