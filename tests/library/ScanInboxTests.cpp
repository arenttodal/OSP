// Library Stages 6-8 on disk: scanning folders (fast and deep stages, incremental, patterns,
// pause and cancel, corrections that survive rescans), importing candidates with their
// evidence, the Inbox's states through slow copies, cloud placeholders, duplicates, renames,
// offline folders and crashes, and slices saved as new sounds with their provenance.

#include "LabelledSet.h"
#include "io/AudioFileIO.h"
#include "io/ContentHash.h"
#include "library/FolderScan.h"
#include "library/Inbox.h"
#include "library/Slicer.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <cstdio>
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
            dir = std::filesystem::temp_directory_path() / ("osp-scan-test-" + std::to_string (counter++) + "-" + std::to_string (reinterpret_cast<std::uintptr_t> (this)));
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
        std::filesystem::path write (const std::string& relative, const AudioData& audio)
        {
            const auto path = files / relative;
            std::filesystem::create_directories (path.parent_path());
            std::string error;
            REQUIRE (io::writeAudioFile (path, audio, io::SampleFormat::pcm24, error));
            return path;
        }
    };

    int countState (const Catalog& c, const std::string& state)
    {
        return static_cast<int> (c.scanFiles ({}, state, true).size());
    }

    bool hasTag (const Catalog& c, const std::string& id, const std::string& name, TagSource source)
    {
        for (const auto& t : c.tagsOf (id))
            if (t.name == name && t.source == source)
                return true;
        return false;
    }

    AudioData loop (double rate = 48000.0)
    {
        const double eighth = 0.25;
        auto a = labelled::blank (16 * eighth + 0.05, rate);
        for (int s = 0; s < 16; ++s)
            labelled::mix (a, s % 4 == 0 ? labelled::kick (rate, 0.8) : labelled::hat (rate, static_cast<std::uint64_t> (s), 0.4), s * eighth);
        return a;
    }
}

TEST_CASE ("library scan: patterns match names and paths, case-insensitively", "[unit][library][scan]")
{
    CHECK (passesPatterns ("Drums/Kick 01.wav", {}, {}));
    CHECK (passesPatterns ("Drums/Kick 01.wav", "*kick*", {}));
    CHECK_FALSE (passesPatterns ("Pads/Warm.wav", "*kick*;*snare*", {}));
    CHECK (passesPatterns ("Pads/Warm.wav", "*kick*; pads/*", {}));
    CHECK_FALSE (passesPatterns ("Loops/Beat.wav", {}, "loops/*"));
    CHECK_FALSE (passesPatterns ("Drums/Kick ?? copy.wav", {}, "*copy*"));
    CHECK (passesPatterns ("a.WAV", "*.wav", {}));
}

TEST_CASE ("library scan: fast index, deep analysis, incremental rescans, corrections that stick", "[unit][library][scan]")
{
    Place p;
    const auto kick = p.write ("Drums/Kicks/Kick 01.wav", labelled::kick (48000.0));
    p.write ("Pads/Warm Pad C3.wav", labelled::pad (130.81, 4.0, 48000.0));
    p.write ("Loops/Beat 120bpm.wav", loop());
    p.write (".hidden/secret.wav", labelled::kick (48000.0));
    p.write ("Pads/.DS_Store.wav", labelled::kick (48000.0));
    {
        std::ofstream (p.files / "notes.txt") << "not audio";
        std::ofstream bad (p.files / "damaged.wav", std::ios::binary);
        bad << "RIFF\x10\x00\x00\x00WAVEjunk";
    }
    const auto rootId = p.catalog->addScanRoot (p.files.string());
    REQUIRE (rootId);
    CHECK (p.catalog->addScanRoot (p.files.string()) == rootId);   // the same folder: one root
    const auto root = *p.catalog->scanRoot (*rootId);
    ScanControl control;
    ScanProgress progress;
    auto summary = scanFolder (*p.catalog, root, {}, control, progress);
    CHECK (summary.error.empty());
    CHECK (summary.found == 4);        // hidden files and folders, and text, are not audio to scan
    CHECK (summary.indexed == 3);
    CHECK (summary.failed == 1);       // the damaged file, with a reason
    CHECK (summary.analysed == 3);
    CHECK (progress.stage.load() == 3);
    CHECK (progress.likelyOneShots.load() == 2);
    CHECK (progress.loops.load() == 1);
    const auto damaged = p.catalog->scanFile ((p.files / "damaged.wav").string());
    REQUIRE (damaged);
    CHECK (damaged->state == "failed");
    CHECK_FALSE (damaged->error.empty());
    const auto kickFile = p.catalog->scanFile (kick.string());
    REQUIRE (kickFile);
    CHECK (kickFile->state == "analysed");
    CHECK (kickFile->durationSeconds == Catch::Approx (0.45).margin (0.01));
    CHECK (p.catalog->classificationOf (kickFile->contentHash) == "likely-one-shot");
    CHECK (p.catalog->search ({}).empty());   // candidates are not Library sounds until imported

    // Rescan: nothing new, nothing analysed again.
    summary = scanFolder (*p.catalog, root, {}, control, progress);
    CHECK (summary.unchanged == 4);
    CHECK (summary.analysed == 0);
    // The user says the loop is a one-shot: that survives every rescan and re-analysis.
    const auto loopFile = *p.catalog->scanFile ((p.files / "Loops/Beat 120bpm.wav").string());
    REQUIRE (p.catalog->setClassificationOverride (loopFile.contentHash, "likely-one-shot"));
    p.write ("Loops/Beat 120bpm.wav", loop());   // written again: changed time, same content
    summary = scanFolder (*p.catalog, root, {}, control, progress);
    CHECK (summary.indexed == 1);
    CHECK (summary.analysed == 1);
    CHECK (p.catalog->classificationOf (loopFile.contentHash) == "likely-one-shot");
    // Setting a candidate aside never deletes it.
    REQUIRE (p.catalog->setScanFileRejected (loopFile.path, true));
    CHECK (p.catalog->scanFiles (*rootId).size() == 3);
    CHECK (p.catalog->scanFiles (*rootId, {}, true).size() == 4);
    CHECK (std::filesystem::exists (loopFile.path));
    // A file removed from the folder: its candidate goes.
    std::filesystem::remove (p.files / "damaged.wav");
    summary = scanFolder (*p.catalog, root, {}, control, progress);
    CHECK (summary.removed == 1);
    // Patterns: an exclusion takes the loops out of the next scan.
    const auto excluding = p.catalog->addScanRoot (p.files.string(), {}, "loops/*");
    summary = scanFolder (*p.catalog, *p.catalog->scanRoot (*excluding), {}, control, progress);
    CHECK (summary.found == 2);
    // A folder that is gone: nothing changes.
    ScanRoot gone = root;
    gone.path = (p.dir / "unplugged drive").string();
    summary = scanFolder (*p.catalog, gone, {}, control, progress);
    CHECK_FALSE (summary.error.empty());
    CHECK (p.catalog->scanFiles (*rootId, {}, true).size() == 2);
}

TEST_CASE ("library scan: importing candidates brings their evidence; a user's removal sticks", "[unit][library][scan]")
{
    Place p;
    const auto file = p.write ("Synths/Pads/Warm Pad C3.wav", labelled::held (130.81, 3.0, 48000.0, 0.3, 4, true));
    const auto rootId = *p.catalog->addScanRoot (p.files.string());
    ScanControl control;
    ScanProgress progress;
    scanFolder (*p.catalog, *p.catalog->scanRoot (rootId), {}, control, progress);
    const auto candidate = *p.catalog->scanFile (file.string());
    const auto collection = *p.catalog->createCollection ("From the drive");
    const auto r = importCandidate (*p.catalog, p.store, candidate, p.files.string(), collection);
    INFO (r.error);
    REQUIRE (r.ok);
    CHECK (hasTag (*p.catalog, r.assetId, "pad", TagSource::filename));
    CHECK (hasTag (*p.catalog, r.assetId, "pad", TagSource::filename));
    CHECK (hasTag (*p.catalog, r.assetId, "synth", TagSource::folder));
    CHECK (hasTag (*p.catalog, r.assetId, "tonal", TagSource::audio));
    CHECK (p.catalog->asset (r.assetId)->category == "Pad");
    CHECK (p.catalog->sound (r.assetId)->rootMidi == 48.0);   // the name's C3, which the audio agrees with
    CHECK (p.catalog->collectionsOf (r.assetId).size() == 1);
    CHECK (p.catalog->scanFile (file.string())->state == "imported");
    CHECK (p.catalog->scanFile (file.string())->assetId == r.assetId);
    // The user removes an inferred tag and renames it; importing (or rescanning) again keeps both.
    REQUIRE (p.catalog->removeTag (r.assetId, "synth"));
    REQUIRE (p.catalog->rename (r.assetId, "My Pad"));
    REQUIRE (p.catalog->setCategory (r.assetId, "Texture"));
    scanFolder (*p.catalog, *p.catalog->scanRoot (rootId), {}, control, progress);
    const auto again = importCandidate (*p.catalog, p.store, *p.catalog->scanFile (file.string()), p.files.string());
    REQUIRE (again.ok);
    CHECK (again.assetId == r.assetId);
    CHECK_FALSE (hasTag (*p.catalog, r.assetId, "synth", TagSource::folder));
    CHECK (p.catalog->asset (r.assetId)->name == "My Pad");
    CHECK (p.catalog->asset (r.assetId)->category == "Texture");
}

TEST_CASE ("library scan: pause, resume and cancel between files; a cancel keeps what is done", "[unit][library][scan]")
{
    Place p;
    for (int i = 0; i < 40; ++i)
        p.write ("Many/hit " + std::to_string (i) + ".wav", labelled::snare (48000.0, static_cast<std::uint64_t> (i)));
    const auto root = *p.catalog->scanRoot (*p.catalog->addScanRoot (p.files.string()));
    ScanControl control;
    ScanProgress progress;
    control.paused = true;
    std::atomic<bool> finished { false };
    ScanSummary summary;
    std::thread worker ([&] {
        std::string e;
        auto own = Catalog::open (p.dir / "catalog.db", e);   // the scanner's own connection
        summary = scanFolder (*own, root, {}, control, progress);
        finished = true;
    });
    std::this_thread::sleep_for (std::chrono::milliseconds (200));
    CHECK_FALSE (finished.load());   // paused: waits
    control.paused = false;
    while (progress.analysed.load() < 5 && ! finished.load())
        std::this_thread::sleep_for (std::chrono::milliseconds (5));
    control.cancelled = true;
    worker.join();
    CHECK (summary.cancelled);
    CHECK (summary.analysed < 40);
    const int done = countState (*p.catalog, "analysed");
    CHECK (done >= 5);
    // Scanning again finishes the rest.
    ScanControl fresh;
    const auto rest = scanFolder (*p.catalog, root, {}, fresh, progress);
    CHECK (rest.analysed == 40 - done);
    CHECK (countState (*p.catalog, "analysed") == 40);
    // Longer files can be left out of the deep stage.
    p.write ("Long/hour.wav", labelled::blank (8.0));
    ScanSettings shortOnly;
    shortOnly.skipLongerThanSeconds = 5.0;
    const auto skipping = scanFolder (*p.catalog, root, shortOnly, fresh, progress);
    CHECK (skipping.skipped == 1);
}

TEST_CASE ("library inbox: a slow copy waits, a placeholder waits, then ready; duplicates are known", "[unit][library][inbox]")
{
    Place p;
    const auto inbox = p.dir / "Inbox";
    std::filesystem::create_directories (inbox);
    std::int64_t now = 1'000'000;
    auto pass = [&] { return reconcileInbox (*p.catalog, { inbox }, now); };
    // A recording arriving in pieces (a sync client writing it).
    std::string error;
    const auto source = p.dir / "take.wav";
    REQUIRE (io::writeAudioFile (source, labelled::held (220.0, 2.0, 48000.0, 0.05, 1), io::SampleFormat::pcm24, error));
    std::ifstream in (source, std::ios::binary);
    const std::string bytes ((std::istreambuf_iterator<char> (in)), std::istreambuf_iterator<char>());
    const auto target = inbox / "Voice Memo 1.wav";
    for (int piece = 1; piece <= 4; ++piece)
    {
        {
            std::ofstream out (target, std::ios::binary | std::ios::trunc);
            out.write (bytes.data(), static_cast<std::streamsize> (bytes.size() * static_cast<std::size_t> (piece) / 4));
        }
        std::filesystem::last_write_time (target, std::filesystem::file_time_type::clock::now() + std::chrono::seconds (piece));
        now += 1500;   // less than the stable time between pieces
        pass();
        const auto item = p.catalog->inboxItem (target.string());
        REQUIRE (item);
        CHECK (item->state != "failed");   // a partial file is never called damaged
        CHECK (item->state != "ready");
    }
    now += 2500;
    pass();
    REQUIRE (p.catalog->inboxItem (target.string())->state == "ready");

    // An iCloud placeholder: waiting until the file itself is there.
    {
        std::ofstream (inbox / ".Idea.wav.icloud") << "placeholder";
    }
    pass();
    const auto idea = inbox / "Idea.wav";
    REQUIRE (p.catalog->inboxItem (idea.string()));
    CHECK (p.catalog->inboxItem (idea.string())->state == "waiting");
    CHECK (p.catalog->inboxItem (idea.string())->error.find ("cloud") != std::string::npos);
    std::filesystem::remove (inbox / ".Idea.wav.icloud");
    std::filesystem::copy_file (source, idea);
    now += 100;
    pass();
    now += 2500;
    pass();
    CHECK (p.catalog->inboxItem (idea.string())->state == "ready");   // downloaded: ready like any other
}

TEST_CASE ("library inbox: import, duplicates, renames, missing files, offline folders, crashes, unreadable files", "[unit][library][inbox]")
{
    Place p;
    const auto inbox = p.dir / "Inbox";
    std::filesystem::create_directories (inbox / "Sub");
    std::int64_t now = 5'000'000;
    auto pass = [&] { return reconcileInbox (*p.catalog, { inbox }, now); };
    auto settle = [&] {
        pass();
        now += 3000;
        return pass();
    };
    std::string error;
    const auto memo = inbox / "Hum.wav";
    REQUIRE (io::writeAudioFile (memo, labelled::held (196.0, 1.5, 48000.0, 0.05, 2), io::SampleFormat::pcm24, error));
    REQUIRE (io::writeAudioFile (inbox / "Sub" / "Clap.wav", labelled::snare (48000.0, 3), io::SampleFormat::pcm24, error));
    auto counts = settle();
    CHECK (counts.ready == 2);
    // Imported: into the Library as a captured sound; the file stays where it is.
    const auto r = importInboxItem (*p.catalog, p.store, memo.string(), "Morning hum", { "voice memo" });
    INFO (r.error);
    REQUIRE (r.ok);
    CHECK (p.catalog->inboxItem (memo.string())->state == "imported");
    CHECK (p.catalog->asset (r.assetId)->origin == Origin::captured);
    CHECK (p.catalog->asset (r.assetId)->name == "Morning hum");
    CHECK (std::filesystem::exists (memo));
    CHECK (p.catalog->provenanceOf (r.contentHash) == "original");
    // A copy of the same bytes under another name: known as already in the Library.
    std::filesystem::copy_file (memo, inbox / "Hum copy.wav");
    settle();
    CHECK (p.catalog->inboxItem ((inbox / "Hum copy.wav").string())->state == "imported");
    CHECK (p.catalog->inboxItem ((inbox / "Hum copy.wav").string())->assetId == r.assetId);
    // Renamed after import: the old item goes, the new name is known by its bytes.
    std::filesystem::rename (memo, inbox / "Hum renamed.wav");
    settle();
    CHECK_FALSE (p.catalog->inboxItem (memo.string()).has_value());
    CHECK (p.catalog->inboxItem ((inbox / "Hum renamed.wav").string())->state == "imported");
    // A ready file the user deleted: missing (and back if it returns).
    const auto clap = inbox / "Sub" / "Clap.wav";
    const auto clapCopy = p.dir / "clap.wav";
    std::filesystem::copy_file (clap, clapCopy);
    std::filesystem::remove (clap);
    pass();
    CHECK (p.catalog->inboxItem (clap.string())->state == "missing");
    std::filesystem::copy_file (clapCopy, clap);
    settle();
    CHECK (p.catalog->inboxItem (clap.string())->state == "ready");
    // The folder goes offline (a disconnected drive): missing, never failed; back, ready again.
    const auto parked = p.dir / "Parked";
    std::filesystem::rename (inbox, parked);
    counts = pass();
    REQUIRE (counts.offlineFolders.size() == 1);
    CHECK (p.catalog->inboxItem (clap.string())->state == "missing");
    std::filesystem::rename (parked, inbox);
    settle();
    CHECK (p.catalog->inboxItem (clap.string())->state == "ready");
    // A crash while importing: the item is ready again on the next pass.
    auto item = *p.catalog->inboxItem (clap.string());
    item.state = "importing";
    REQUIRE (p.catalog->putInboxItem (item));
    pass();
    CHECK (p.catalog->inboxItem (clap.string())->state == "ready");
    // An unreadable file: tried again while it may still be arriving, failed only at the end.
    {
        std::ofstream bad (inbox / "Broken.wav", std::ios::binary);
        bad << "RIFF\x10\x00\x00\x00WAVEjunkjunkjunk";
    }
    pass();
    for (int i = 0; i < 4; ++i)
    {
        now += 2500;
        pass();
        CHECK (p.catalog->inboxItem ((inbox / "Broken.wav").string())->state == "waiting");
    }
    for (int i = 0; i < 3; ++i)
    {
        now += 2500;
        pass();
    }
    CHECK (p.catalog->inboxItem ((inbox / "Broken.wav").string())->state == "failed");
    // Temporary download names are not Inbox items.
    {
        std::ofstream (inbox / "Song.wav.crdownload") << "x";
        std::ofstream (inbox / "Song.wav.part") << "x";
    }
    pass();
    CHECK_FALSE (p.catalog->inboxItem ((inbox / "Song.wav.crdownload").string()).has_value());
    CHECK (isTemporaryDownload ("x.wav.sb-1234"));
    CHECK (cloudPlaceholderTarget ("/a/.Take 1.m4a.icloud") == std::filesystem::path ("/a/Take 1.m4a"));
}

TEST_CASE ("library inbox: a ready item is classified; its import carries its name's and folders' evidence", "[unit][library][inbox]")
{
    Place p;
    const auto inbox = p.dir / "Inbox";
    std::filesystem::create_directories (inbox / "Basses");
    std::int64_t now = 9'000'000;
    std::string error;
    const auto file = inbox / "Basses" / "Take C2.wav";   // the Type from the folder, the root from the name
    REQUIRE (io::writeAudioFile (file, labelled::held (65.41, 1.0, 48000.0, 0.02, 7), io::SampleFormat::pcm24, error));
    reconcileInbox (*p.catalog, { inbox }, now);
    now += 3000;
    reconcileInbox (*p.catalog, { inbox }, now);
    const auto item = p.catalog->inboxItem (file.string());
    REQUIRE (item);
    REQUIRE (item->state == "ready");
    // Classified before the user decides (shown in the Inbox).
    CHECK_FALSE (p.catalog->classificationOf (item->contentHash).empty());
    const auto r = importInboxItem (*p.catalog, p.store, file.string(), "Deep sub");
    REQUIRE (r.ok);
    CHECK (p.catalog->asset (r.assetId)->category == "Bass");
    bool fromFolder = false;
    for (const auto& t : p.catalog->tagsOf (r.assetId))
        fromFolder = fromFolder || t.source == TagSource::folder;
    CHECK (fromFolder);
    REQUIRE (p.catalog->sound (r.assetId)->rootMidi.has_value());
    CHECK (*p.catalog->sound (r.assetId)->rootMidi == Catch::Approx (36.0).margin (1.0));
}

TEST_CASE ("library slices: each saved slice is a new sound with its provenance; the original is intact", "[unit][library][slicer]")
{
    Place p;
    auto take = labelled::blank (4.0, 48000.0, 2);
    for (int i = 0; i < 4; ++i)
        labelled::mix (take, labelled::snare (48000.0, static_cast<std::uint64_t> (i)), 0.2 + i * 0.9);
    const auto file = p.write ("Takes/Hits.wav", take);
    const auto parent = importSound (*p.catalog, p.store, [&] {
        ImportRequest r;
        r.file = file;
        return r;
    }());
    REQUIRE (parent.ok);
    const auto before = *io::sha256OfFile (file);
    const auto slices = sliceRecording (take);
    REQUIRE (slices.size() == 4);
    std::vector<std::string> ids;
    for (std::size_t i = 0; i < slices.size(); ++i)
    {
        DerivedSound d;
        d.parentAssetId = parent.assetId;
        d.name = "Hit " + std::to_string (i + 1);
        d.startSeconds = static_cast<double> (slices[i].start) / 48000.0;
        d.endSeconds = static_cast<double> (slices[i].end) / 48000.0;
        d.operations = "{\"fadeIn\":0.0005,\"fadeOut\":0.005,\"gainDb\":0,\"slicer\":\"sampleforge-1\"}";
        d.category = "Percussion";
        const auto saved = saveDerivedSound (*p.catalog, p.store, renderSlice (take, slices[i]), d);
        INFO (saved.error);
        REQUIRE (saved.ok);
        ids.push_back (saved.assetId);
    }
    CHECK (p.catalog->derivedFrom (parent.assetId) == ids);
    const auto first = *p.catalog->sound (ids[0]);
    CHECK (first.parentAsset == parent.assetId);
    CHECK (first.sliceStart == Catch::Approx (0.2).margin (0.01));
    CHECK (first.operations.find ("sampleforge") != std::string::npos);
    CHECK (first.channels == 2);
    CHECK (p.catalog->classificationOf (first.contentHash) == "likely-one-shot");   // analysed when saved
    CHECK (p.catalog->asset (ids[0])->name == "Hit 1");
    CHECK (p.catalog->asset (ids[0])->category == "Percussion");
    // Each is in the store (resolvable without any temporary file), the original untouched.
    for (const auto& id : ids)
        CHECK (resolveSound (*p.catalog, p.store, p.catalog->sound (id)->contentHash).where == "store");
    CHECK (*io::sha256OfFile (file) == before);
    for (const auto& e : std::filesystem::directory_iterator (p.store))
        CHECK (e.path().filename().string().rfind (".derived-", 0) != 0);   // no leftovers
    // The same audio saved again (from elsewhere): the existing sound, its provenance kept.
    DerivedSound again;
    again.parentAssetId = "somewhere-else";
    again.name = "Hit again";
    again.startSeconds = 3.0;
    const auto same = saveDerivedSound (*p.catalog, p.store, renderSlice (take, slices[0]), again);
    REQUIRE (same.ok);
    CHECK (same.alreadyInLibrary);
    CHECK (same.assetId == ids[0]);
    CHECK (p.catalog->sound (ids[0])->parentAsset == parent.assetId);
    CHECK (p.catalog->sound (ids[0])->sliceStart == Catch::Approx (0.2).margin (0.01));
    // An empty slice is refused.
    CHECK_FALSE (saveDerivedSound (*p.catalog, p.store, AudioData {}, {}).ok);
}

// Scanning 1,000, 10,000 and 50,000 files (docs/library/classification.md). Hidden: run with
// osp_tests "[library-scan-perf]".
TEST_CASE ("library scan performance: 1,000 / 10,000 / 50,000 files", "[.][library-scan-perf]")
{
    for (const int n : { 1000, 10000, 50000 })
    {
        Place p;
        // Tiny files, each different (so the deep stage analyses every one): the walk, the
        // catalog and the analysis per file are what is measured.
        for (int i = 0; i < n; ++i)
        {
            auto tiny = testsignals::impulse (0.02, 48000.0, 0.002 + 0.000001 * (i % 9000), 0.5 + 0.4 * (i % 997) / 997.0);
            const auto to = p.files / ("f" + std::to_string (i / 1000)) / (std::to_string (i) + ".wav");
            std::filesystem::create_directories (to.parent_path());
            std::string error;
            REQUIRE (io::writeAudioFile (to, tiny, io::SampleFormat::pcm16, error));
        }
        const auto root = *p.catalog->scanRoot (*p.catalog->addScanRoot (p.files.string()));
        ScanControl control;
        ScanProgress progress;
        ScanSettings fast;
        fast.deep = false;
        auto t0 = std::chrono::steady_clock::now();
        const auto indexed = scanFolder (*p.catalog, root, fast, control, progress);
        const double indexSeconds = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
        t0 = std::chrono::steady_clock::now();
        const auto again = scanFolder (*p.catalog, root, fast, control, progress);
        const double rescanSeconds = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
        double deepSeconds = 0.0;
        if (n <= 1000)
        {
            t0 = std::chrono::steady_clock::now();
            scanFolder (*p.catalog, root, {}, control, progress);
            deepSeconds = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
        }
        // Cancelling a deep pass: how long until it stops.
        double cancelSeconds = 0.0;
        if (n == 10000)
        {
            for (const auto& f : p.catalog->scanFiles ({}, "analysed"))
            {
                auto g = f;
                g.state = "indexed";
                p.catalog->putScanFile (g);
            }
            ScanControl stop;
            std::thread worker ([&] {
                std::string e;
                auto own = Catalog::open (p.dir / "catalog.db", e);
                ScanProgress pr;
                scanFolder (*own, root, {}, stop, pr);
            });
            std::this_thread::sleep_for (std::chrono::milliseconds (300));
            t0 = std::chrono::steady_clock::now();
            stop.cancelled = true;
            worker.join();
            cancelSeconds = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
        }
        long rssKb = 0;
        if (FILE* status = std::fopen ("/proc/self/status", "r"))
        {
            char line[256];
            while (std::fgets (line, sizeof (line), status) != nullptr)
                if (std::sscanf (line, "VmHWM: %ld kB", &rssKb) == 1)
                    break;
            std::fclose (status);
        }
        // The deep pass is timed at 1,000 files only (analysis time grows with the audio, not the count).
        std::printf ("%6d files: index %.2f s (%.0f files/s), rescan %.2f s, deep %s, cancel %s, peak memory %ld MB\n", n, indexSeconds, n / indexSeconds,
                     rescanSeconds, n <= 1000 ? (std::to_string (deepSeconds).substr (0, 4) + " s").c_str() : "not timed",
                     n == 10000 ? (std::to_string (cancelSeconds).substr (0, 5) + " s").c_str() : "not timed", rssKb / 1024);
        CHECK (indexed.indexed == n);
        CHECK (again.unchanged == n);
    }
}
