// Short atlas notes sustain (unified engine Phase 4B): a Note Atlas note with 0.3 s of stable
// material holds through continuation, its loops scaled to the region; shorter or decaying ones
// play as recorded. Sample sets keep the 0.5 s minimum and their analysis is unchanged.

#include "analysis/continuation/ContinuationAnalyzer.h"
#include "analysis/phrase/PhraseAnalysis.h"
#include "audio/utility/TestSignals.h"
#include "engine/AtlasSet.h"
#include "engine/InstrumentEngine.h"
#include "research/AtlasFixtures.h"
#include "support/AllocationCounter.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>

using namespace osp;
using namespace osp::research;

namespace
{
    /** One note, analysed into an atlas (the longest note is the one under test) and built with
        `minSustainSeconds`. */
    struct Short
    {
        AudioData audio;
        atlasset::BuildResult built;
        const ContinuationModel& continuation() const { return built.set->members.front().model->original.continuation; }
        double regionSeconds() const { return (continuation().sustainEndFrame - continuation().sustainStartFrame) / 48000.0; }
    };

    Short shortNote (AudioData audio, double minSustainSeconds)
    {
        Short s;
        s.audio = std::move (audio);
        atlas::SourceAsset source;
        source.assetId = "src0";
        source.contentHash = atlasfixtures::contentHashOf (s.audio);
        source.fileName = "short.wav";
        auto analysed = phrase::analysePhrase (s.audio, source);
        REQUIRE_FALSE (analysed.atlas.notes.empty());
        // The note under test is the longest (a fade can leave a short harmonic scrap after it).
        auto& notes = analysed.atlas.notes;
        const auto main = std::max_element (notes.begin(), notes.end(), [] (const auto& a, const auto& b)
                                            { return a.sourceEnd - a.attackStart < b.sourceEnd - b.attackStart; });
        for (auto& n : notes)
            n.included = &n == &*main;
        atlasset::BuildOptions options;
        options.minSustainSeconds = minSustainSeconds;
        s.built = atlasset::buildInstrumentSet (analysed.atlas, { { "src0", std::make_shared<const AudioData> (s.audio) } }, options);
        REQUIRE (s.built.set != nullptr);
        REQUIRE (s.built.set->members.size() == 1);
        return s;
    }

    /** A steady bowed-like tone (saw) or a sung-like one (vibrato), 20 ms attack, 60 ms release. */
    AudioData tone (bool vibrato, double seconds)
    {
        auto a = vibrato ? testsignals::vibratoSine (440.0, 5.5, 20.0, seconds, 48000.0, 0.4, 2) : testsignals::saw (220.0, seconds, 48000.0, 0.4, 2);
        testsignals::applyFades (a, 0.02, 0.06);
        return a;
    }

    /** RMS (dB) of a held key in 50 ms windows from 0.3 s to `holdSeconds`; `allocations`:
        heap allocations while it played (the note-on and every block). */
    std::vector<double> heldLevels (const InstrumentSet& set, int key, double holdSeconds, int& allocations)
    {
        EngineSettings s;
        s.macros.life = 0.0;
        s.macros.motion = 0.0;
        s.macros.space = 0.0;
        s.macros.reimagined = 0.0;
        InstrumentEngine engine;
        engine.prepare (48000.0, 240, s);
        engine.setInstrumentSet (&set);
        std::vector<float> l (240), r (240);
        std::vector<double> energy (static_cast<std::size_t> (holdSeconds * 200.0), 0.0);
        {
            test::AllocationScope scope;
            engine.noteOn (key, 100);
            for (auto& e : energy)
            {
                float* ch[2] = { l.data(), r.data() };
                engine.render (ch, 2, 240);
                for (float x : l)
                    e += static_cast<double> (x) * x;
            }
            allocations = scope.count();
        }
        std::vector<double> levels;
        for (std::size_t b = 60; b + 10 <= energy.size(); b += 10)   // from 0.3 s, 50 ms windows
        {
            double sum = 0.0;
            for (std::size_t k = b; k < b + 10; ++k)
                sum += energy[k];
            levels.push_back (10.0 * std::log10 (sum / 2400.0 + 1.0e-20));
        }
        return levels;
    }
}

TEST_CASE ("Short atlas notes: 0.3 s of stable material sustains, a longer region loops as before", "[integration][atlas][sustain]")
{
    // Sample sets keep the fixed minimums: their continuation analysis is unchanged.
    CHECK (ContinuationOptions {}.minRegionSeconds == 0.5);
    CHECK (ContinuationOptions {}.shortRegionSeconds == 0.0);
    CHECK (atlasset::BuildOptions {}.minSustainSeconds == 0.3);

    int sustainedShort = 0;
    for (bool vibrato : { false, true })
    for (double seconds : { 0.35, 0.45, 0.55, 0.65, 0.75, 1.2 })
    {
        INFO ((vibrato ? "vibrato " : "steady ") << seconds << " s note");
        const auto now = shortNote (tone (vibrato, seconds), 0.3);
        const auto before = shortNote (tone (vibrato, seconds), 0.5);
        const double region = now.regionSeconds();
        INFO ("stable region " << region << " s: " << now.continuation().reason);
        // The region is the note's stable body, well short of the note itself (attack, release).
        CHECK (region < seconds - 0.05);
        CHECK (region == before.regionSeconds());
        if (region < 0.3)
        {
            CHECK_FALSE (now.continuation().canSustain);
            CHECK (now.continuation().reason == "stable region too short");
        }
        else if (region >= 0.5)
        {
            // Exactly the analysis the 0.5 s threshold gave.
            CHECK (now.continuation().canSustain == before.continuation().canSustain);
            REQUIRE (now.continuation().jumps.size() == before.continuation().jumps.size());
            for (std::size_t j = 0; j < now.continuation().jumps.size(); ++j)
            {
                CHECK (now.continuation().jumps[j].fromFrame == before.continuation().jumps[j].fromFrame);
                CHECK (now.continuation().jumps[j].toFrame == before.continuation().jumps[j].toFrame);
            }
        }
        else
        {
            CHECK_FALSE (before.continuation().canSustain);
            CHECK (now.continuation().canSustain);
            sustainedShort += now.continuation().canSustain ? 1 : 0;
            // Loops scaled to the region, never shorter than 0.6 of it.
            for (const auto& j : now.continuation().jumps)
                if (j.toFrame < j.fromFrame)
                    CHECK ((j.fromFrame - j.toFrame) / 48000.0 >= 0.6 * region - 0.02);
        }
        if (now.continuation().canSustain)
        {
            // Held three times its length: it keeps sounding at an even level (no dropout, no
            // pumping from loop to loop).
            const int key = now.built.set->members.front().model->rootMidi > 0 ? static_cast<int> (std::lround (now.built.set->members.front().model->rootMidi)) : 69;
            int allocations = -1;
            const auto levels = heldLevels (*now.built.set, key, 3.0 * seconds + 0.5, allocations);
            CHECK (allocations == 0);
            REQUIRE (levels.size() > 4);
            const auto [lo, hi] = std::minmax_element (levels.begin(), levels.end());
            CHECK (*lo > -60.0);
            CHECK (*hi - *lo < 6.0);
        }
    }
    CHECK (sustainedShort >= 4);   // the change does something: 0.3-0.5 s bodies now hold
}

TEST_CASE ("Short atlas notes: a decaying note is not forced to sustain", "[integration][atlas][sustain]")
{
    auto plucked = testsignals::pluck (220.0, 0.7, 48000.0, 3);
    testsignals::applyFades (plucked, 0.002, 0.05);
    const auto s = shortNote (std::move (plucked), 0.3);
    CHECK_FALSE (s.continuation().canSustain);
}
