// Library Stage 2: the import transaction and finding a sound again. Imports are all or nothing,
// one stored copy per content, interruptions leave nothing that looks like a sound, damaged and
// unsupported files are refused with a reason, several instances importing the same file make
// one record, and a sound is found by its bytes - never by a file that merely has its name.

#include "audio/utility/TestSignals.h"
#include "io/AudioFileIO.h"
#include "io/ContentHash.h"
#include "library/SoundImport.h"

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <fstream>
#include <thread>

using namespace osp;
using namespace osp::library;

namespace
{
    struct Fixture
    {
        std::filesystem::path dir, store, samples;
        std::unique_ptr<Catalog> catalog;
        Fixture()
        {
            static std::atomic<int> counter { 0 };
            dir = std::filesystem::temp_directory_path() / ("osp-import-test-" + std::to_string (counter++) + "-" + std::to_string (reinterpret_cast<std::uintptr_t> (this)));
            std::filesystem::remove_all (dir);
            store = dir / "store";
            samples = dir / "samples";
            std::filesystem::create_directories (samples);
            std::string error;
            catalog = Catalog::open (dir / "catalog.db", error);
            REQUIRE (catalog != nullptr);
        }
        ~Fixture()
        {
            catalog.reset();
            std::error_code ec;
            std::filesystem::permissions (store, std::filesystem::perms::all, ec);
            std::filesystem::remove_all (dir, ec);
        }
        std::filesystem::path wav (const std::string& name, double hz = 220.0, int channels = 2, double seconds = 0.5)
        {
            const auto path = samples / name;
            std::filesystem::create_directories (path.parent_path());
            std::string error;
            REQUIRE (io::writeAudioFile (path, testsignals::sine (hz, seconds, 48000.0, 0.4, channels), io::SampleFormat::pcm24, error));
            return path;
        }
        static ImportRequest request (const std::filesystem::path& file)
        {
            ImportRequest r;
            r.file = file;
            return r;
        }
        int storedFiles() const
        {
            int n = 0;
            std::error_code ec;
            for (const auto& e : std::filesystem::directory_iterator (store, ec))
                n += e.is_regular_file() ? 1 : 0;
            return n;
        }
    };
}

TEST_CASE ("library import: a sound is stored once under its hash, recorded with its details", "[unit][library][import]")
{
    Fixture f;
    const auto file = f.wav ("Warm Pad C3.wav", 130.81, 2, 0.75);
    const auto r = importSound (*f.catalog, f.store, Fixture::request (file));
    INFO (r.error);
    REQUIRE (r.ok);
    const auto hex = io::sha256OfFile (file);
    REQUIRE (hex);
    CHECK (r.contentHash == "sha256:" + *hex);
    CHECK (r.stored == f.store / (*hex + ".wav"));
    CHECK (std::filesystem::exists (r.stored));
    CHECK (io::sha256OfFile (r.stored) == hex);
    const auto a = f.catalog->asset (r.assetId);
    REQUIRE (a);
    CHECK (a->name == "Warm Pad C3");
    const auto s = f.catalog->sound (r.assetId);
    REQUIRE (s);
    CHECK (s->channels == 2);
    CHECK (s->sampleRate == 48000.0);
    CHECK (s->bitDepth == 24);
    CHECK (s->frames == 36000);
    CHECK (s->durationSeconds == 0.75);
    CHECK (s->format == "wav");
    CHECK (s->managed);
    CHECK (s->fileSize == static_cast<std::int64_t> (std::filesystem::file_size (file)));
    CHECK (f.catalog->locations (r.contentHash).size() == 1);
    CHECK (f.catalog->recent (5).front().action == "imported");

    // The same file again: the same record, no second copy.
    const auto again = importSound (*f.catalog, f.store, Fixture::request (file));
    REQUIRE (again.ok);
    CHECK (again.alreadyInLibrary);
    CHECK (again.assetId == r.assetId);
    CHECK (f.storedFiles() == 1);
    // The same bytes under another name elsewhere: a record of its own (its own name and
    // place), the stored copy shared.
    const auto copy = f.samples / "Drums" / "pad copy.wav";
    std::filesystem::create_directories (copy.parent_path());
    std::filesystem::copy_file (file, copy);
    const auto other = importSound (*f.catalog, f.store, Fixture::request (copy));
    REQUIRE (other.ok);
    CHECK_FALSE (other.alreadyInLibrary);
    CHECK (other.contentAlreadyStored);
    CHECK (other.assetId != r.assetId);
    CHECK (f.storedFiles() == 1);
    CHECK (f.catalog->soundsWithContent (r.contentHash).size() == 2);
}

TEST_CASE ("library import: missing, unsupported, damaged and empty files are refused, and nothing is left", "[unit][library][import]")
{
    Fixture f;
    auto refused = [&f] (const std::filesystem::path& p, const std::string& reasonPart) {
        const auto r = importSound (*f.catalog, f.store, Fixture::request (p));
        CHECK_FALSE (r.ok);
        INFO (r.error);
        CHECK (r.error.find (reasonPart) != std::string::npos);
        CHECK (f.storedFiles() == 0);
        CHECK (f.catalog->search ({}).empty());
    };
    refused (f.samples / "gone.wav", "not there");
    {
        std::ofstream (f.samples / "notes.txt") << "not audio";
        refused (f.samples / "notes.txt", "unsupported");
    }
    {
        std::ofstream out (f.samples / "damaged.wav", std::ios::binary);
        out << "RIFF\x10\x00\x00\x00WAVEjunkjunkjunkjunk";
    }
    refused (f.samples / "damaged.wav", "could not read");
    {
        std::ofstream (f.samples / "empty.wav", std::ios::binary);
    }
    refused (f.samples / "empty.wav", "could not read");
}

TEST_CASE ("library import: an interruption leaves nothing that looks like a sound; a cancel publishes nothing", "[unit][library][import]")
{
    Fixture f;
    const auto file = f.wav ("hit.wav");
    const auto hex = *io::sha256OfFile (file);
    // A copy that died halfway: a .partial is never a sound, and is cleared later.
    std::filesystem::create_directories (f.store);
    {
        std::ofstream half (f.store / (hex + ".wav.123-0.partial"), std::ios::binary);
        half << "RIFF";
    }
    CHECK_FALSE (findStored (f.store, hex).has_value());
    CHECK (clearInterruptedImports (f.store, std::chrono::hours (1)) == 0);   // maybe still being written
    CHECK (clearInterruptedImports (f.store, std::chrono::seconds (0)) == 1);
    CHECK (f.storedFiles() == 0);
    // Cancelled before publishing: no file, no record.
    std::atomic<bool> cancel { true };
    const auto r = importSound (*f.catalog, f.store, Fixture::request (file), &cancel);
    CHECK_FALSE (r.ok);
    CHECK (r.error == "cancelled");
    CHECK (f.storedFiles() == 0);
    CHECK (f.catalog->search ({}).empty());
    // And it imports normally afterwards.
    CHECK (importSound (*f.catalog, f.store, Fixture::request (file)).ok);
}

TEST_CASE ("library import: a store that cannot be written refuses the import cleanly", "[unit][library][import]")
{
    Fixture f;
    const auto file = f.wav ("pluck.wav");
    std::filesystem::create_directories (f.store);
    std::filesystem::permissions (f.store, std::filesystem::perms::owner_read | std::filesystem::perms::owner_exec);
    {
        std::ofstream probe (f.store / "probe");
        if (probe.good())
        {
            // Running as root: permissions do not stop the write; nothing to test here.
            probe.close();
            std::filesystem::permissions (f.store, std::filesystem::perms::all);
            std::filesystem::remove (f.store / "probe");
            WARN ("skipped: the test runs with permissions that ignore a read-only folder");
            return;
        }
    }
    const auto r = importSound (*f.catalog, f.store, Fixture::request (file));
    CHECK_FALSE (r.ok);
    CHECK (r.error.find ("cannot copy") != std::string::npos);
    CHECK (f.catalog->search ({}).empty());
}

TEST_CASE ("library import: four instances importing the same files at once make one copy and one record each", "[unit][library][import]")
{
    Fixture f;
    std::vector<std::filesystem::path> files;
    for (int i = 0; i < 6; ++i)
        files.push_back (f.wav ("tone " + std::to_string (i) + ".wav", 110.0 * (i + 1)));
    std::atomic<int> failures { 0 };
    std::vector<std::thread> threads;
    for (int t = 0; t < 4; ++t)
        threads.emplace_back ([&] {
            std::string error;
            auto catalog = Catalog::open (f.dir / "catalog.db", error);   // its own connection
            if (catalog == nullptr)
            {
                ++failures;
                return;
            }
            for (const auto& file : files)
                if (! importSound (*catalog, f.store, Fixture::request (file)).ok)
                    ++failures;
        });
    for (auto& th : threads)
        th.join();
    CHECK (failures.load() == 0);
    CHECK (f.storedFiles() == 6);
    SearchQuery all;
    all.limit = 100;
    CHECK (f.catalog->search (all).size() == 6);
    // Checked from the connection that was open before the others wrote: its search index
    // view is refreshed first, so no false alarm (and a fresh connection agrees below).
    CHECK (f.catalog->integrityCheck() == "ok");
    // The connection opened before the others wrote: its full-text search sees their records,
    // and its own writes keep the index sound.
    SearchQuery tone;
    tone.text = "tone";
    CHECK (f.catalog->search (tone).size() == 6);
    const auto own = f.catalog->search (all).front().id;
    REQUIRE (f.catalog->rename (own, "Renamed Tone"));
    SearchQuery renamed;
    renamed.text = "renamed";
    CHECK (f.catalog->search (renamed).size() == 1);
    {
        std::string error;
        auto fresh = Catalog::open (f.dir / "catalog.db", error);
        REQUIRE (fresh != nullptr);
        CHECK (fresh->integrityCheck() == "ok");
        CHECK (fresh->search (renamed).size() == 1);
    }
    CHECK (f.catalog->integrityCheck() == "ok");
}

TEST_CASE ("library resolver: store first, then where it was seen by its bytes, never by its name", "[unit][library][import]")
{
    Fixture f;
    const auto file = f.wav ("Vowel Ah.wav", 196.0);
    const auto r = importSound (*f.catalog, f.store, Fixture::request (file));
    REQUIRE (r.ok);
    auto found = resolveSound (*f.catalog, f.store, r.contentHash);
    REQUIRE (found.file);
    CHECK (found.where == "store");
    CHECK (*found.file == r.stored);
    // The managed copy gone (a cleaned store): the original, checked by its bytes.
    std::filesystem::remove (r.stored);
    found = resolveSound (*f.catalog, f.store, r.contentHash);
    REQUIRE (found.file);
    CHECK (found.where == "original");
    // The original replaced by another recording with the same name: not this sound.
    std::filesystem::remove (file);
    f.wav ("Vowel Ah.wav", 440.0);
    found = resolveSound (*f.catalog, f.store, r.contentHash);
    CHECK_FALSE (found.file.has_value());
    REQUIRE (found.missing.size() == 1);
    // A disconnected drive: missing, with the place it was (for relinking).
    std::filesystem::remove_all (f.samples);
    found = resolveSound (*f.catalog, f.store, r.contentHash);
    CHECK_FALSE (found.file.has_value());
    CHECK (found.missing.size() == 1);
}

TEST_CASE ("library storage: usage by what holds it; emptying the trash moves away only what nothing holds", "[unit][library][import]")
{
    Fixture f;
    const auto keep = importSound (*f.catalog, f.store, Fixture::request (f.wav ("keep.wav", 110.0)));
    const auto drop = importSound (*f.catalog, f.store, Fixture::request (f.wav ("drop.wav", 220.0)));
    const auto needed = importSound (*f.catalog, f.store, Fixture::request (f.wav ("needed.wav", 330.0)));
    const auto playing = importSound (*f.catalog, f.store, Fixture::request (f.wav ("playing.wav", 440.0)));
    REQUIRE ((keep.ok && drop.ok && needed.ok && playing.ok));
    // A stored copy without any record (loaded before the Library existed): never touched.
    const auto loose = f.wav ("loose.wav", 550.0);
    const auto looseHex = *io::sha256OfFile (loose);
    std::filesystem::copy_file (loose, f.store / (looseHex + ".wav"));
    std::ofstream (f.store / (hexOf (drop.contentHash) + ".analysis.json")) << "{}";

    Asset preset;
    preset.type = AssetType::preset;
    preset.name = "Uses needed";
    REQUIRE (f.catalog->addPreset (preset, PresetInfo { "/p/n.osppreset", "user", 10, 1, { needed.contentHash } }));

    for (const auto* r : { &drop, &needed, &playing })
        REQUIRE (f.catalog->trash (r->assetId));
    auto usage = storageUsage (*f.catalog, f.store);
    CHECK (usage.files == 5);
    CHECK (usage.library == 2);     // keep, and needed (a preset needs it)
    CHECK (usage.trashOnly == 2);   // drop, playing
    CHECK (usage.untracked == 1);
    CHECK (usage.bytes == usage.libraryBytes + usage.trashOnlyBytes + usage.untrackedBytes);

    std::vector<std::filesystem::path> moved;
    const auto away = f.dir / "system-trash";
    std::filesystem::create_directories (away);
    const auto result = emptyTrash (*f.catalog, f.store, { playing.contentHash }, [&] (const std::filesystem::path& p) {
        moved.push_back (p);
        std::error_code ec;
        std::filesystem::rename (p, away / p.filename(), ec);
        return ! ec;
    });
    CHECK (result.records == 3);
    REQUIRE (result.files == 1);
    CHECK (moved.front() == drop.stored);
    CHECK (std::filesystem::exists (away / drop.stored.filename()));   // recoverable
    CHECK_FALSE (std::filesystem::exists (f.store / (hexOf (drop.contentHash) + ".analysis.json")));
    CHECK (std::filesystem::exists (needed.stored));    // a preset needs it
    CHECK (std::filesystem::exists (playing.stored));   // an open instance plays it
    CHECK (std::filesystem::exists (keep.stored));
    CHECK (std::filesystem::exists (f.store / (looseHex + ".wav")));
    CHECK_FALSE (f.catalog->asset (drop.assetId).has_value());
    CHECK (f.catalog->asset (keep.assetId).has_value());
    usage = storageUsage (*f.catalog, f.store);
    CHECK (usage.files == 4);
    CHECK (usage.trashOnly == 0);
    CHECK (usage.untracked == 2);   // playing has no record now, but stays
    CHECK (f.catalog->integrityCheck() == "ok");

    // A file that cannot be moved stays, and says so.
    const auto again = importSound (*f.catalog, f.store, Fixture::request (f.wav ("again.wav", 660.0)));
    REQUIRE (f.catalog->trash (again.assetId));
    const auto refused = emptyTrash (*f.catalog, f.store, {}, [] (const std::filesystem::path&) { return false; });
    CHECK (refused.files == 0);
    CHECK (refused.failed.size() == 1);
    CHECK (std::filesystem::exists (again.stored));
}
