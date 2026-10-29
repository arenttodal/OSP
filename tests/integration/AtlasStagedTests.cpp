// Staged atlas preparation (unified engine Phase 4): Stage A is playable at once, Stage B adds
// continuation and gives exactly the single-pass set; publication while notes sound, partial
// builds that say so, cancellation, thread-count independence - and a recording analysed by
// the phrase analysis plays its notes at their own keys through the ordinary atlas path.

#include "analysis/Analyzer.h"
#include "analysis/phrase/PhraseAnalysis.h"
#include "core/PitchMath.h"
#include "engine/AtlasSet.h"
#include "engine/InstrumentEngine.h"
#include "research/AtlasFixtures.h"
#include "research/PhraseFixtures.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <cmath>
#include <functional>

using namespace osp;
using namespace osp::research;

namespace
{
    EngineSettings dry()
    {
        EngineSettings s;
        s.macros.life = 0.0;
        s.macros.motion = 0.0;
        s.macros.space = 0.0;
        s.macros.reimagined = 0.0;
        return s;
    }

    /** Plays `notes` (key, start s, length s) through a dry engine; `eachBlock` may swap sets. */
    AudioData play (const InstrumentSet* set, const std::vector<std::tuple<int, double, double>>& notes, double seconds,
                    const std::function<void (InstrumentEngine&, double)>& eachBlock = {})
    {
        InstrumentEngine engine;
        engine.prepare (48000.0, 256, dry());
        engine.setInstrumentSet (set);
        auto out = AudioData::allocate (2, 0, 48000.0);
        std::vector<float> l (256), r (256);
        const int blocks = static_cast<int> (seconds * 48000.0 / 256);
        for (int b = 0; b < blocks; ++b)
        {
            const double t0 = b * 256 / 48000.0, t1 = (b + 1) * 256 / 48000.0;
            for (const auto& [key, start, length] : notes)
            {
                if (start >= t0 && start < t1)
                    engine.noteOn (key, 100);
                if (start + length >= t0 && start + length < t1)
                    engine.noteOff (key);
            }
            if (eachBlock)
                eachBlock (engine, t0);
            float* ch[2] = { l.data(), r.data() };
            engine.render (ch, 2, 256);
            out.channels[0].insert (out.channels[0].end(), l.begin(), l.end());
            out.channels[1].insert (out.channels[1].end(), r.begin(), r.end());
        }
        return out;
    }

    bool identical (const AudioData& a, const AudioData& b)
    {
        return a.channels == b.channels;
    }

    double rms (const AudioData& a)
    {
        double s = 0.0;
        for (float x : a.channels[0])
            s += static_cast<double> (x) * x;
        return a.channels[0].empty() ? 0.0 : std::sqrt (s / static_cast<double> (a.channels[0].size()));
    }

    const std::vector<std::tuple<int, double, double>> melody { { 60, 0.0, 0.4 }, { 62, 0.3, 0.6 }, { 64, 0.8, 1.5 }, { 65, 1.2, 0.5 } };
}

TEST_CASE ("Staged atlas: Stage A then Stage B is exactly the single-pass set, on any thread count", "[integration][atlas][staged]")
{
    const auto f = atlasfixtures::make ("sung-four");
    const auto single = atlasfixtures::build (f);
    REQUIRE (single.set != nullptr);

    atlasset::BuildOptions options;
    auto staged = atlasset::buildPlayableSet (f.atlas, f.audio, options);
    REQUIRE (staged.playable.set != nullptr);
    REQUIRE (staged.pending != nullptr);
    const auto finished = atlasset::enhanceSet (*staged.pending, options);
    REQUIRE (finished.set != nullptr);
    CHECK_FALSE (finished.report.playableOnly);
    CHECK (finished.set->members.size() == single.set->members.size());
    CHECK (identical (play (finished.set.get(), melody, 3.0), play (single.set.get(), melody, 3.0)));

    // Several threads: the same set (members are independent and deterministic).
    options.threads = 3;
    const auto parallel = atlasset::buildInstrumentSet (f.atlas, f.audio, options);
    REQUIRE (parallel.set != nullptr);
    CHECK (identical (play (parallel.set.get(), melody, 3.0), play (single.set.get(), melody, 3.0)));
    for (std::size_t i = 0; i < single.set->members.size(); ++i)
    {
        const auto& a = single.set->members[i];
        const auto& b = parallel.set->members[i];
        CHECK (a.regionId == b.regionId);
        CHECK (a.pitchGroup == b.pitchGroup);
        CHECK (a.take == b.take);
        CHECK (a.model->rootMidi == b.model->rootMidi);
        CHECK (a.model->playback.gainDb == b.model->playback.gainDb);
    }
}

TEST_CASE ("Staged atlas: Stage A plays every note at its key at once, before its continuation exists", "[integration][atlas][staged]")
{
    const auto f = atlasfixtures::make ("sung-four");
    const auto staged = atlasset::buildPlayableSet (f.atlas, f.audio, {});
    REQUIRE (staged.playable.set != nullptr);
    const auto& set = *staged.playable.set;
    CHECK (staged.playable.report.playableOnly);
    CHECK (set.isValid());
    const auto full = atlasset::buildInstrumentSet (f.atlas, f.audio, {});
    REQUIRE (full.set != nullptr);
    REQUIRE (set.members.size() == full.set->members.size());
    for (std::size_t i = 0; i < set.members.size(); ++i)
    {
        const auto& a = set.members[i];
        const auto& b = full.set->members[i];
        CHECK (a.model->stage == InstrumentModel::Stage::provisional);
        CHECK (b.model->stage == InstrumentModel::Stage::complete);
        // The same places, roots, regions and level: Stage B only adds.
        CHECK (a.regionId == b.regionId);
        CHECK (a.pitchGroup == b.pitchGroup);
        CHECK (a.layer == b.layer);
        CHECK (a.take == b.take);
        CHECK (a.model->rootMidi == b.model->rootMidi);
        CHECK (a.model->playback.gainDb == b.model->playback.gainDb);
        CHECK (a.model->original.source->numFrames() == b.model->original.source->numFrames());
        CHECK (a.model->original.transient == nullptr);
    }
    // It sounds (each key's own recording).
    CHECK (rms (play (&set, melody, 2.0)) > 1.0e-3);
    CHECK (staged.playable.report.buildMilliseconds < full.report.buildMilliseconds);
}

TEST_CASE ("Staged atlas: publishing Stage B while a note sounds leaves that note as it was", "[integration][atlas][staged]")
{
    const auto f = atlasfixtures::make ("sung-four");
    auto staged = atlasset::buildPlayableSet (f.atlas, f.audio, {});
    REQUIRE (staged.pending != nullptr);
    const auto finished = atlasset::enhanceSet (*staged.pending, {});
    REQUIRE (finished.set != nullptr);
    const auto* stageA = staged.playable.set.get();
    const auto* stageB = finished.set.get();

    // One held note; Stage B arrives 0.25 s into it. The held note must not change at all.
    const std::vector<std::tuple<int, double, double>> held { { 60, 0.0, 1.0 } };
    const auto onlyA = play (stageA, held, 1.5);
    const auto swapped = play (stageA, held, 1.5, [&] (InstrumentEngine& e, double t) {
        if (t >= 0.25 && t < 0.25 + 256 / 48000.0)
            e.setInstrumentSet (stageB);
    });
    CHECK (identical (onlyA, swapped));

    // A note started after the swap plays Stage B's member (as a Stage B-only render).
    const std::vector<std::tuple<int, double, double>> later { { 62, 0.5, 0.5 } };
    const auto swappedLater = play (stageA, later, 1.5, [&] (InstrumentEngine& e, double t) {
        if (t >= 0.25 && t < 0.25 + 256 / 48000.0)
            e.setInstrumentSet (stageB);
    });
    CHECK (identical (swappedLater, play (stageB, later, 1.5)));
}

TEST_CASE ("Staged atlas: a partial build says so - the notes left out and cut are listed", "[integration][atlas][staged]")
{
    const auto f = atlasfixtures::manyRegions (10, 0.5, 10);
    atlasset::BuildOptions options;
    options.maxTotalSeconds = 2.2;   // room for four 0.5 s notes
    CHECK (atlasset::regionSeconds (f.atlas, options) == Catch::Approx (5.0).margin (0.05));
    const auto r = atlasset::buildInstrumentSet (f.atlas, f.audio, options);
    REQUIRE (r.set != nullptr);
    CHECK (r.report.partial);
    CHECK (r.report.notesUsed == 4);
    CHECK (r.report.skippedNoteIds.size() == 6);
    CHECK (r.report.cutNoteIds.empty());
    bool said = false;
    for (const auto& w : r.report.warnings)
        said = said || w.rfind ("partial instrument", 0) == 0;
    CHECK (said);
    // The atlas itself is untouched: every note is still there to rebuild with more room.
    CHECK (f.atlas.notes.size() == 10);

    options.maxTotalSeconds = 600.0;
    options.maxRegionSeconds = 0.25;
    const auto cut = atlasset::buildInstrumentSet (f.atlas, f.audio, options);
    REQUIRE (cut.set != nullptr);
    CHECK (cut.report.partial);
    CHECK (cut.report.cutNoteIds.size() == 10);
    CHECK (cut.report.skippedNoteIds.empty());

    const auto whole = atlasset::buildInstrumentSet (f.atlas, f.audio, {});
    REQUIRE (whole.set != nullptr);
    CHECK_FALSE (whole.report.partial);
}

TEST_CASE ("Staged atlas: a cancelled preparation builds nothing and says so", "[integration][atlas][staged]")
{
    const auto f = atlasfixtures::manyRegions (8, 0.4, 8);
    std::atomic<bool> cancel { true };
    atlasset::BuildOptions options;
    options.cancel = &cancel;
    const auto a = atlasset::buildPlayableSet (f.atlas, f.audio, options);
    CHECK (a.playable.set == nullptr);
    CHECK (a.playable.report.cancelled);

    cancel = false;
    std::atomic<float> progress { 0.0f };
    options.progress = &progress;
    auto staged = atlasset::buildPlayableSet (f.atlas, f.audio, options);
    REQUIRE (staged.pending != nullptr);
    cancel = true;
    const auto b = atlasset::enhanceSet (*staged.pending, options);
    CHECK (b.set == nullptr);
    CHECK (b.report.cancelled);
    // Stage A stays playable when Stage B is cancelled.
    CHECK (staged.playable.set != nullptr);

    cancel = false;
    const auto done = atlasset::enhanceSet (*staged.pending, options);
    REQUIRE (done.set != nullptr);
    CHECK (progress.load() == Catch::Approx (1.0f));
}

TEST_CASE ("Phrase analysis -> atlas -> instrument: each analysed note plays at its own key and pitch", "[integration][atlas][phrase]")
{
    const auto fx = phrase::makePhraseFixture ("clean-scale", 48000.0);
    atlas::SourceAsset source;
    source.assetId = "src0";
    source.contentHash = atlasfixtures::contentHashOf (fx.audio);
    source.fileName = "clean-scale.wav";
    const auto analysed = phrase::analysePhrase (fx.audio, source);
    REQUIRE (analysed.ok);
    const auto& a = analysed.atlas;
    REQUIRE (atlas::validate (a).ok());
    CHECK (a.schemaVersion == atlas::currentSchemaVersion);
    const auto built = atlasset::buildInstrumentSet (a, { { "src0", std::make_shared<const AudioData> (fx.audio) } }, {});
    REQUIRE (built.set != nullptr);
    CHECK_FALSE (built.report.partial);

    int included = 0;
    for (const auto& n : a.notes)
    {
        if (! n.included)
            continue;
        ++included;
        // The note's key plays its own region (no FILL borrowing), tuned to the key.
        const int group = built.set->groupFor (n.playKey());
        REQUIRE (group >= 0);
        CHECK (built.set->groups[static_cast<std::size_t> (group)].rootMidi == Catch::Approx (n.playKey()));
        const auto render = play (built.set.get(), { { n.playKey(), 0.0, 0.5 } }, 0.6);
        AudioData left = AudioData::allocate (1, render.numFrames(), 48000.0);
        left.channels[0] = render.channels[0];
        const auto pitch = Analyzer::analyse (left).pitch;
        REQUIRE (pitch.detected);
        CHECK (hzToMidi (pitch.fundamentalHz) == Catch::Approx (n.playKey()).margin (0.2));
    }
    CHECK (included == static_cast<int> (fx.notes.size()));
    CHECK (built.report.notesUsed == included);
}
