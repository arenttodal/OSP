// Ported from ANDOR Lab (andor-lab eeb5f39, tests/andor/AtlasTests.cpp) with OSP's schema 2 additions.
#include "model/NoteAtlas.h"
#include "io/NoteAtlasJson.h"
#include "core/AudioData.h"
#include "io/AudioFileIO.h"
#include "io/ContentHash.h"
#include "support/TestHelpers.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <fstream>

using namespace osp;
using Catch::Approx;

namespace
{
    atlas::NoteAtlas sampleAtlas()
    {
        atlas::NoteAtlas a;
        a.atlasId = "atlas:test";
        a.createdBy = "tests";
        a.analysis.method = "yin-viterbi";
        a.analysis.version = 1;
        atlas::SourceAsset s;
        s.assetId = "src0";
        s.contentHash = "sha256:abc";
        s.fileName = "phrase.wav";
        s.sampleRate = 48000.0;
        s.numChannels = 2;
        s.numFrames = 480000;
        a.sources.push_back (s);
        for (int i = 0; i < 3; ++i)
        {
            atlas::NoteEvent n;
            n.id = a.nextNoteId();
            n.sourceId = "src0";
            n.sourceStart = 1000 + i * 48000;
            n.attackStart = n.sourceStart + 100;
            n.sustain = { n.attackStart + 2000, n.attackStart + 20000 };
            n.releaseStart = n.sustain.end;
            n.sourceEnd = n.releaseStart + 4000;
            n.fadeInFrames = 32;
            n.fadeOutFrames = 480;
            n.midiPitch = 60.0 + 2 * i + 0.13;
            n.pitchHz = 261.6;
            n.confidence = 0.9;
            n.voicing = 0.95;
            n.analysisMethod = "yin-viterbi";
            n.analysisVersion = 101;
            a.notes.push_back (n);
        }
        a.notes[1].grainRegion = atlas::FrameRange { 60000, 70000 };
        a.notes[2].corrections.pitch = true;
        a.notes[2].rootMidi = 63;
        a.notes[2].midiPitch = 64.13;
        atlas::remapNotes (a);
        return a;
    }
}

TEST_CASE ("Note Atlas: ids, lookups and validation", "[unit][atlas]")
{
    auto a = sampleAtlas();
    CHECK (a.notes[0].id == "n0000");
    CHECK (a.nextNoteId() == "n0003");
    CHECK (a.findNote ("n0001") == &a.notes[1]);
    CHECK (a.indexOf ("n0002") == 2);
    CHECK (a.findSource ("src0") != nullptr);
    CHECK (validate (a).ok());

    SECTION ("ids are never reused after a removal")
    {
        a.notes.erase (a.notes.begin() + 1);
        CHECK (a.nextNoteId() == "n0003");
    }
    SECTION ("broken references, ordering and ranges are reported")
    {
        a.notes[0].sourceId = "missing";
        a.notes[1].sustain.start = a.notes[1].sourceEnd + 10;
        a.notes[2].sourceEnd = 999999999;
        a.notes.push_back (a.notes.back());
        const auto report = validate (a);
        CHECK_FALSE (report.ok());
        CHECK (report.errors.size() >= 4);
    }
    SECTION ("repair puts markers back in order inside the source")
    {
        a.notes[1].sustain.start = a.notes[1].sourceEnd + 10;
        a.notes[2].sourceEnd = 999999999;
        CHECK (repair (a) == 2);
        CHECK (validate (a).ok());
    }
}

TEST_CASE ("Note Atlas: the tuning rule rounds deterministically and keeps cents apart", "[unit][atlas]")
{
    CHECK (atlas::roundPitch (60.49).key == 60);
    CHECK (atlas::roundPitch (60.5).key == 60);     // exactly +50 cents stays on the lower key
    CHECK (atlas::roundPitch (60.51).key == 61);
    CHECK (atlas::roundPitch (59.6).key == 60);
    CHECK (atlas::roundPitch (59.6).cents == Approx (-40.0));
    // A recording tuned 30 cents sharp: +30 cents is the key itself.
    const auto r = atlas::roundPitch (61.75, 30.0);
    CHECK (r.key == 61);
    CHECK (r.cents == Approx (75.0));
    CHECK (atlas::roundPitch (200.0).key == 127);
}

TEST_CASE ("Note Atlas: remapping respects user pitch corrections and orders variations", "[unit][atlas]")
{
    auto a = sampleAtlas();
    CHECK (a.notes[0].rootMidi == 60);
    CHECK (a.notes[0].centsOffset == Approx (13.0));
    CHECK (a.notes[2].rootMidi == 63);                      // corrected: kept
    CHECK (a.notes[2].centsOffset == Approx (113.0));

    // Two notes on one key form a group in time order; an explicit group wins.
    a.notes[1].midiPitch = 60.1;
    atlas::remapNotes (a);
    const auto keys = atlas::notesByKey (a);
    REQUIRE (keys[60].size() == 2);
    CHECK (keys[60][0] == 0);
    CHECK (keys[60][1] == 1);
    CHECK (a.notes[1].variationIndex == 1);

    a.notes[1].included = false;
    CHECK (atlas::notesByKey (a)[60].size() == 1);
    a.notes[1].included = true;
    a.notes[1].assignedKey = 72;
    CHECK (atlas::notesByKey (a)[72].size() == 1);
    CHECK (atlas::groupOf (a.notes[1]) == "key72.v0");
    a.notes[1].velocityLayer = 2;
    CHECK (atlas::groupOf (a.notes[1]) == "key72.v2");   // round robins are per key and velocity layer
}

TEST_CASE ("Note Atlas: JSON round trip is exact for positions and lossless for edits", "[unit][atlas]")
{
    auto a = sampleAtlas();
    a.notes[0].sourceStart = 9'000'000'001;   // beyond 32 bits: frames stay exact
    a.sources[0].numFrames = 9'100'000'000;
    a.notes[0].attackStart = 9'000'000'101;
    a.notes[0].sustain = { 9'000'002'000, 9'000'030'000 };
    a.notes[0].releaseStart = 9'000'030'000;
    a.notes[0].sourceEnd = 9'000'040'000;
    a.notes[1].corrections.boundaries = true;
    a.notes[1].assignedKey = 70;
    a.notes[1].variationGroup = "lead";
    a.notes[1].articulation = atlas::Articulation::legato;
    a.notes[1].included = false;

    std::string error;
    const auto text = io::noteAtlasToString (a);
    const auto back = io::noteAtlasFromString (text, error);
    REQUIRE (back);
    CHECK (error.empty());
    REQUIRE (back->notes.size() == a.notes.size());
    CHECK (back->atlasId == a.atlasId);
    CHECK (back->sources[0].numFrames == 9'100'000'000);
    CHECK (back->notes[0].sourceStart == 9'000'000'001);
    CHECK (back->notes[0].sustain.end == 9'000'030'000);
    CHECK (back->notes[1].grainRegion.has_value());
    CHECK (back->notes[1].grainRegion->start == 60000);
    CHECK (back->notes[1].corrections.boundaries);
    CHECK (back->notes[1].assignedKey == 70);
    CHECK (back->notes[1].variationGroup == "lead");
    CHECK (back->notes[1].articulation == atlas::Articulation::legato);
    CHECK_FALSE (back->notes[1].included);
    CHECK (back->notes[2].corrections.pitch);
    CHECK (back->notes[2].midiPitch == Approx (64.13));
    CHECK (io::noteAtlasToString (*back) == text);   // stable serialisation

    // Values playback reads survive exactly (bit-identical session recall).
    a.notes[0].midiPitch = 61.123456789012345;
    a.notes[0].pitchHz = 280.98765432109876;
    a.notes[0].confidence = 0.912345678901234;
    a.notes[0].rmsDb = -23.456789012345678;
    const auto exact = io::noteAtlasFromString (io::noteAtlasToString (a), error);
    REQUIRE (exact);
    CHECK (exact->notes[0].midiPitch == a.notes[0].midiPitch);
    CHECK (exact->notes[0].pitchHz == a.notes[0].pitchHz);
    CHECK (exact->notes[0].confidence == a.notes[0].confidence);
    CHECK (exact->notes[0].rmsDb == a.notes[0].rmsDb);
}

TEST_CASE ("Note Atlas: versioning refuses what it cannot read", "[unit][atlas]")
{
    std::string error;
    CHECK_FALSE (io::noteAtlasFromString ("[1, 2]", error));
    CHECK_FALSE (io::noteAtlasFromString (R"({"schemaVersion": 1})", error));
    CHECK (error.find ("format") != std::string::npos);
    CHECK_FALSE (io::noteAtlasFromString (R"({"format": "andor.noteAtlas"})", error));
    CHECK_FALSE (io::noteAtlasFromString (R"({"format": "andor.noteAtlas", "schemaVersion": 99})", error));
    CHECK (error.find ("newer") != std::string::npos);
    // Unknown fields are ignored (forward compatible additions); ANDOR Lab's version 1 is
    // migrated to 2: its analysed notes stay analysis notes, layer 0, file-import sources.
    const auto ok = io::noteAtlasFromString (R"({"format": "andor.noteAtlas", "schemaVersion": 1, "futureField": 3,
        "sources": [{"id": "src0", "contentHash": "sha256:00", "sampleRate": 44100, "numChannels": 1, "numFrames": 100}],
        "notes": [{"id": "n0000", "source": "src0", "sourceStart": 0, "sourceEnd": 90, "attackStart": 0, "midiPitch": 60.0,
                   "rootMidi": 60, "method": "yin-viterbi"}]})", error);
    REQUIRE (ok);
    CHECK (ok->schemaVersion == atlas::currentSchemaVersion);
    CHECK (ok->sources[0].numFrames == 100);
    CHECK (ok->sources[0].origin == atlas::AssetOrigin::fileImport);
    REQUIRE (ok->notes.size() == 1);
    CHECK (ok->notes[0].origin == atlas::NoteOrigin::analysis);
    CHECK (ok->notes[0].velocityLayer == 0);
    CHECK (ok->mapping == atlas::Mapping {});
    CHECK (validate (*ok).ok());
}

TEST_CASE ("Note Atlas: files, relative paths, resolving and relinking sources", "[integration][atlas]")
{
    test::TempDir dir ("andor-atlas");
    std::filesystem::create_directories (dir.path() / "bundle" / "audio");
    const auto audioPath = dir.path() / "bundle" / "audio" / "phrase.wav";
    auto audio = AudioData::allocate (1, 4800, 48000.0);
    for (std::size_t i = 0; i < 4800; ++i)
        audio.channels[0][i] = 0.1f * static_cast<float> (std::sin (0.05 * static_cast<double> (i)));
    std::string error;
    REQUIRE (io::writeAudioFile (audioPath, audio, io::SampleFormat::pcm24, error));
    const auto hash = io::contentId (*io::sha256OfFile (audioPath));

    auto a = sampleAtlas();
    a.sources[0].contentHash = hash;
    a.sources[0].lastKnownPath = audioPath.string();
    a.sources[0].fileName = "phrase.wav";
    a.sources[0].numChannels = 1;
    a.sources[0].numFrames = 4800;
    const auto atlasPath = dir.path() / "bundle" / "phrase.andoratlas";
    REQUIRE (io::writeNoteAtlas (atlasPath, a, error));
    auto loaded = io::readNoteAtlas (atlasPath, error);
    REQUIRE (loaded);
    CHECK (loaded->sources[0].relativePath == "audio/phrase.wav");

    // The whole bundle moves: the relative path still finds the audio, by fingerprint.
    std::filesystem::rename (dir.path() / "bundle", dir.path() / "moved");
    loaded = io::readNoteAtlas (dir.path() / "moved" / "phrase.andoratlas", error);
    REQUIRE (loaded);
    auto resolved = io::resolveSource (loaded->sources[0], dir.path() / "moved");
    REQUIRE (resolved);
    CHECK (resolved->match == atlas::RelinkResult::exact);

    // Missing everywhere: nothing resolves until the user relinks.
    std::filesystem::rename (dir.path() / "moved" / "audio" / "phrase.wav", dir.path() / "elsewhere.wav");
    CHECK_FALSE (io::resolveSource (loaded->sources[0], dir.path() / "moved"));
    // Search folders by file name find a renamed folder's copy (same bytes).
    std::filesystem::create_directories (dir.path() / "found");
    std::filesystem::copy_file (dir.path() / "elsewhere.wav", dir.path() / "found" / "phrase.wav");
    resolved = io::resolveSource (loaded->sources[0], dir.path() / "moved", { dir.path() / "found" });
    REQUIRE (resolved);
    CHECK (resolved->match == atlas::RelinkResult::exact);

    // Relink to a different file of the same shape: accepted as compatible, new fingerprint.
    auto other = audio;
    other.channels[0][10] = 0.5f;
    REQUIRE (io::writeAudioFile (dir.path() / "edited.wav", other, io::SampleFormat::pcm24, error));
    atlas::RelinkResult match {};
    REQUIRE (io::relinkSource (*loaded, "src0", dir.path() / "edited.wav", error, &match));
    CHECK (match == atlas::RelinkResult::compatible);
    CHECK (loaded->sources[0].contentHash != hash);

    // A different length is refused and leaves the atlas alone.
    auto shorter = AudioData::allocate (1, 1000, 48000.0);
    REQUIRE (io::writeAudioFile (dir.path() / "short.wav", shorter, io::SampleFormat::pcm16, error));
    const auto before = loaded->sources[0].lastKnownPath;
    CHECK_FALSE (io::relinkSource (*loaded, "src0", dir.path() / "short.wav", error));
    CHECK (loaded->sources[0].lastKnownPath == before);
}

TEST_CASE ("Note Atlas (schema 2): sources are content-identified assets, from a file or a capture", "[unit][atlas]")
{
    auto a = sampleAtlas();
    // A live capture finalised into an asset: no path at all, only its identity and origin.
    a.sources[0].origin = atlas::AssetOrigin::liveCapture;
    a.sources[0].captureId = "capture:2026-10-28T20:00:00Z/track-3";
    a.sources[0].relativePath.clear();
    a.sources[0].lastKnownPath.clear();
    a.sources[0].managedCopy.clear();
    CHECK (validate (a).ok());
    std::string error;
    const auto back = io::noteAtlasFromString (io::noteAtlasToString (a), error);
    REQUIRE (back);
    CHECK (back->sources[0].origin == atlas::AssetOrigin::liveCapture);
    CHECK (back->sources[0].captureId == a.sources[0].captureId);
    // Without a content hash the audio cannot be identified: not playable.
    a.sources[0].contentHash.clear();
    CHECK_FALSE (validate (a).ok());
}

TEST_CASE ("Note Atlas (schema 2): velocity layers, note origin, mapping and corrections round-trip", "[unit][atlas]")
{
    auto a = sampleAtlas();
    a.notes[0].velocityLayer = 1;
    a.notes[0].corrections.velocity = true;
    a.notes[1].origin = atlas::NoteOrigin::analysis;
    a.mapping.fill = false;
    a.mapping.maxTransposeSemitones = 4.5;
    a.mapping.correctTuning = false;
    std::string error;
    const auto back = io::noteAtlasFromString (io::noteAtlasToString (a), error);
    REQUIRE (back);
    CHECK (back->notes[0].velocityLayer == 1);
    CHECK (back->notes[0].corrections.velocity);
    CHECK (back->notes[0].corrections.any());
    CHECK (back->notes[1].origin == atlas::NoteOrigin::analysis);
    CHECK (back->mapping == a.mapping);
    a.notes[2].velocityLayer = 40;
    a.mapping.maxTransposeSemitones = -1.0;
    CHECK (validate (a).errors.size() >= 2);
}
