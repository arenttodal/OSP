#include "AndorTestUtil.h"

#include "andor/dsp/OfflineRender.h"
#include "andor/grain/GrainEngine.h"
#include "andor/io/NoteAtlasJson.h"
#include "andor/phrase/PhraseAnalysis.h"
#include "andor/phrase/PhraseEdits.h"
#include "andor/phrase/PhraseEngine.h"
#include "andor/phrase/PhraseFixtures.h"
#include "core/PitchMath.h"
#include "io/AudioFileIO.h"
#include "io/ContentHash.h"
#include "support/TestHelpers.h"

#include <catch2/catch_test_macros.hpp>

using namespace osp;
using namespace osp::andortest;

TEST_CASE ("Interop: a Phrase atlas, exported and re-read, drives Grain from one shared buffer", "[interop][integration]")
{
    test::TempDir dir ("andor-interop");
    const auto fx = phrase::makePhraseFixture ("vocal-phrase");
    const auto wav = dir / "phrase.wav";
    std::string error;
    REQUIRE (io::writeAudioFile (wav, fx.audio, io::SampleFormat::float32, error));

    // Phrase: analyse, correct one note by hand (it plays on a new key), export.
    atlas::SourceAsset src;
    src.contentHash = io::contentId (*io::sha256OfFile (wav));
    src.fileName = "phrase.wav";
    src.lastKnownPath = wav.string();
    auto atlas = phrase::analysePhrase (fx.audio, src).atlas;
    REQUIRE (atlas.notes.size() == 8);
    const auto moved = atlas.notes[6].id;   // D4 at 4.5 s
    REQUIRE (phrase::edit::assignKey (atlas, moved, 74));
    REQUIRE (io::writeNoteAtlas (dir / "phrase.andoratlas", atlas, error));

    // Grain: read it back, find the audio by fingerprint, load it once.
    const auto back = io::readNoteAtlas (dir / "phrase.andoratlas", error);
    REQUIRE (back);
    const auto resolved = io::resolveSource (back->sources[0], dir.path());
    REQUIRE (resolved);
    CHECK (resolved->match == atlas::RelinkResult::exact);
    const auto loaded = io::loadAudioFile (resolved->path);
    REQUIRE (loaded.ok);
    // One padded buffer serves both engines (the hybrid will need exactly this).
    auto source = std::make_shared<const PlaybackSource> (loaded.audio, 60.0, std::max (phrase::phrasePadding(), grain::grainPadding()));
    const auto phraseModel = phrase::buildPhraseModel (source, *back, 1);
    const auto grainModel = grain::buildGrainModel (source, std::make_shared<const atlas::NoteAtlas> (*back), 60.0, 1);
    CHECK (phraseModel->source.get() == grainModel->source.get());
    CHECK (source.use_count() == 3);   // this test, Phrase, Grain: no copies

    REQUIRE (grainModel->regions.size() == 8);
    for (const auto& r : grainModel->regions)
    {
        const auto& n = back->notes[static_cast<std::size_t> (r.noteIndex)];
        CHECK (r.key == n.playKey());
        CHECK (r.start == static_cast<double> (n.sustain.start));
        CHECK (r.end == static_cast<double> (n.sustain.end));
    }
    CHECK (grainModel->keyRegions[74].size() == 1);   // the user's key assignment travelled

    grain::GrainParams p;
    p.policy = grain::SourcePolicy::mapped;
    p.triggerVariation = 0.0;
    p.dna.variationAmount = 0.0;
    p.positionScatter = 0.1;
    p.sizeSeconds = 0.08;
    p.density = 40.0;
    auto play = [&] (int key)
    {
        grain::GrainEngine e;
        e.prepare (48000.0, 256);
        e.setModel (grainModel.get());
        e.setParams (p);
        MidiSequence seq;
        andor::addNote (seq, 0.0, 1.0, key);
        return andor::renderOffline (e, seq, 48000.0, 256, 0.3);
    };
    for (int key : { 62, 64, 66, 69, 67 })
    {
        INFO (key);
        const auto out = play (key);
        CHECK (rmsOf (out, 0.2, 0.9) > 0.003);
        CHECK (std::abs (1200.0 * std::log2 (pitchOf (out, 0.2, 0.9) / midiToHz (key))) < 30.0);
    }
    // PLAYS ON maps a recording to a key as it is (untransposed there, like a sample's root
    // key) in both instruments; PITCH is the separate correction of what note it is.
    const auto movedOut = play (74);
    CHECK (std::abs (1200.0 * std::log2 (pitchOf (movedOut, 0.2, 0.9) / midiToHz (62))) < 30.0);
    CHECK (play (69).channels == play (69).channels);   // deterministic
}
