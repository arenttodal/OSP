// Note Atlas -> InstrumentSet (unified engine Phase 3): prepared atlases on deterministic
// recordings (research/AtlasFixtures) built into ordinary OSP sets and played by OSP's own
// voices - pitch, fine tuning, FILL, round robin, velocity, polyphony, region boundaries,
// One Shot and Granular, KEY DNA, the grain budget, A/B/C independence, replacement while
// sounding, missing audio, memory limits, determinism and real-time safety.

#include "analysis/Analyzer.h"
#include "core/PitchMath.h"
#include "engine/AtlasSet.h"
#include "engine/InstrumentEngine.h"
#include "engine/KeyDna.h"
#include "research/AtlasFixtures.h"
#include "support/AllocationCounter.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <functional>
#include <map>
#include <numbers>
#include <set>

using namespace osp;
using namespace osp::research;
using Catch::Approx;

namespace
{
    std::shared_ptr<const InstrumentSet> setOf (const atlasfixtures::Fixture& f)
    {
        auto r = atlasfixtures::build (f);
        INFO (f.name << ": " << (r.report.errors.empty() ? std::string() : r.report.errors.front()));
        REQUIRE (r.set != nullptr);
        return r.set;
    }

    const atlasfixtures::Fixture& fixture (const char* name)
    {
        static std::map<std::string, atlasfixtures::Fixture> cache;
        auto it = cache.find (name);
        if (it == cache.end())
            it = cache.emplace (name, atlasfixtures::make (name)).first;
        return it->second;
    }

    /** A dry engine (no LIFE, MOVEMENT, SPACE, REIMAGINED): the members as they play. */
    EngineSettings dry()
    {
        EngineSettings s;
        s.macros.life = 0.0;
        s.macros.motion = 0.0;
        s.macros.space = 0.0;
        s.macros.reimagined = 0.0;
        return s;
    }

    struct Rig
    {
        InstrumentEngine engine;
        std::vector<float> l = std::vector<float> (256), r = std::vector<float> (256);
        AudioData out = AudioData::allocate (2, 0, 48000.0);
        bool record = true;   ///< off where allocations are counted (recording grows `out`)
        explicit Rig (const EngineSettings& s = dry()) { engine.prepare (48000.0, 256, s); }
        void run (double seconds, const std::function<void()>& eachBlock = {})
        {
            for (int b = 0; b < static_cast<int> (seconds * 48000.0 / 256); ++b)
            {
                float* ch[2] = { l.data(), r.data() };
                engine.render (ch, 2, 256);
                if (! record)
                    continue;
                out.channels[0].insert (out.channels[0].end(), l.begin(), l.end());
                out.channels[1].insert (out.channels[1].end(), r.begin(), r.end());
                if (eachBlock)
                    eachBlock();
            }
        }
        std::vector<const InstrumentVoice*> voices() const
        {
            std::vector<const InstrumentVoice*> v;
            for (int i = 0; i < InstrumentEngine::voiceSlots(); ++i)
                if (engine.voiceAt (i).isActive() && ! engine.voiceAt (i).isFading())
                    v.push_back (&engine.voiceAt (i));
            return v;
        }
    };

    /** The fundamental of a render (OSP's own analyser on the left channel: the stereo test
        vowels' channels differ slightly, and their mix reads about 10 cents sharp), as MIDI. */
    double pitchOf (const AudioData& audio)
    {
        AudioData left = AudioData::allocate (1, audio.numFrames(), audio.sampleRate);
        left.channels[0] = audio.channels[0];
        const auto a = Analyzer::analyse (left);
        return a.pitch.detected ? hzToMidi (a.pitch.fundamentalHz) : -1.0;
    }

    /** Energy at `hz` (Goertzel over the whole render, left channel). */
    double energyAt (const AudioData& audio, double hz)
    {
        const double w = 2.0 * std::numbers::pi * hz / audio.sampleRate;
        double s1 = 0.0, s2 = 0.0;
        for (float x : audio.channels[0])
        {
            const double s0 = x + 2.0 * std::cos (w) * s1 - s2;
            s2 = s1;
            s1 = s0;
        }
        return s1 * s1 + s2 * s2 - 2.0 * std::cos (w) * s1 * s2;
    }

    const SetMember& memberPlayed (const InstrumentSet& set, const InstrumentVoice& v)
    {
        for (const auto& m : set.members)
            if (m.model.get() == v.model())
                return m;
        FAIL ("the voice plays no member of the set");
        return set.members.front();
    }
}

TEST_CASE ("Atlas set: every included note becomes a member at its key, with its region", "[integration][atlas]")
{
    const auto& f = fixture ("sung-four");
    const auto result = atlasfixtures::build (f);
    REQUIRE (result.set != nullptr);
    const auto& set = *result.set;
    CHECK (result.report.ok);
    CHECK (result.report.notesUsed == 4);
    CHECK (result.report.notesSkipped == 0);
    CHECK (result.report.memberBytes > 0);
    REQUIRE (set.groups.size() == 4);
    const double keys[] = { 57, 60, 64, 67 };
    for (std::size_t g = 0; g < 4; ++g)
    {
        CHECK (set.groups[g].rootMidi == keys[g]);
        REQUIRE (set.groups[g].members.size() == 1);
        const auto& m = set.members[static_cast<std::size_t> (set.groups[g].members.front())];
        const auto* note = f.atlas.findNote (m.regionId);
        REQUIRE (note != nullptr);
        CHECK (m.sourceAssetId == "src0");
        CHECK (m.regionStart == note->attackStart);
        CHECK (m.regionEnd == note->sourceEnd);
        CHECK (m.model->rootMidi == Approx (keys[g]).margin (1.0e-9));
        CHECK (m.model->original.source->numFrames() == note->sourceEnd - note->attackStart);
    }
    CHECK (set.fill);
    CHECK (set.fillLimitSemitones == 12.0);
}

TEST_CASE ("Atlas set: each recorded pitch plays at its own key, untransposed", "[integration][atlas]")
{
    const auto set = setOf (fixture ("sung-four"));
    for (int key : { 57, 60, 64, 67 })
    {
        Rig rig;
        rig.engine.setInstrumentSet (set.get());
        rig.engine.noteOn (key, 100);
        rig.run (0.5);
        const auto v = rig.voices();
        REQUIRE (v.size() == 1);
        CHECK (memberPlayed (*set, *v.front()).model->rootMidi == Approx (key));
        const double heard = pitchOf (rig.out);
        INFO ("key " << key << " sounds at MIDI " << heard);
        CHECK (heard == Approx (key).margin (0.08));   // within 8 cents
    }
}

TEST_CASE ("Atlas set: fine tuning - out-of-tune notes play in tune, or as recorded", "[integration][atlas]")
{
    const auto& f = fixture ("out-of-tune");
    for (bool correct : { true, false })
    {
        auto a = f.atlas;
        a.mapping.correctTuning = correct;
        const auto r = atlasset::buildInstrumentSet (a, f.audio, {});
        REQUIRE (r.set != nullptr);
        const std::pair<int, double> notes[] = { { 60, 60.35 }, { 64, 63.60 }, { 67, 67.12 } };
        for (const auto& [key, recorded] : notes)
        {
            Rig rig;
            rig.engine.setInstrumentSet (r.set.get());
            rig.engine.noteOn (key, 100);
            rig.run (0.5);
            const double heard = pitchOf (rig.out);
            INFO ((correct ? "in tune" : "as recorded") << ": key " << key << " sounds at " << heard);
            CHECK (heard == Approx (correct ? static_cast<double> (key) : recorded).margin (0.08));
        }
    }
}

TEST_CASE ("Atlas set: FILL borrows the nearest recorded note within its reach; out of reach is silent and steals nothing", "[integration][atlas]")
{
    const auto& f = fixture ("missing-notes");   // only 48, 60, 72
    auto played = [] (const InstrumentSet& set, int key) {
        InstrumentEngine e;
        e.prepare (48000.0, 256, dry());
        e.setInstrumentSet (&set);
        const int m = e.memberFor (key, 100, 1);
        return m < 0 ? -1.0 : set.groups[static_cast<std::size_t> (set.members[static_cast<std::size_t> (m)].pitchGroup)].rootMidi;
    };
    {
        const auto set = setOf (f);   // fill, 12 semitones
        CHECK (played (*set, 60) == 60.0);
        CHECK (played (*set, 62) == 60.0);
        CHECK (played (*set, 54) == 48.0);   // a tie (6 and 6): the lower note, deterministically
        CHECK (played (*set, 66) == 60.0);
        CHECK (played (*set, 84) == 72.0);   // 12 away: within reach
        CHECK (played (*set, 85) == -1.0);   // 13 away: silent
        CHECK (played (*set, 35) == -1.0);
        // A filled key is transposed from its note: it sounds at the played key.
        Rig rig;
        rig.engine.setInstrumentSet (set.get());
        rig.engine.noteOn (64, 100);
        rig.run (0.5);
        CHECK (pitchOf (rig.out) == Approx (64.0).margin (0.08));
    }
    {
        auto a = f.atlas;
        a.mapping.maxTransposeSemitones = 4.0;
        const auto r = atlasset::buildInstrumentSet (a, f.audio, {});
        REQUIRE (r.set != nullptr);
        CHECK (played (*r.set, 52) == 48.0);
        CHECK (played (*r.set, 53) == -1.0);
        // An out-of-reach key starts no voice and steals none, even with the polyphony full.
        auto s = dry();
        s.polyphony = 1;
        Rig rig (s);
        rig.engine.setInstrumentSet (r.set.get());
        rig.engine.noteOn (60, 100);
        rig.run (0.1);
        REQUIRE (rig.voices().size() == 1);
        const auto* held = rig.voices().front();
        rig.engine.noteOn (54, 100);
        rig.run (0.1);
        CHECK (rig.voices().size() == 1);
        CHECK (rig.voices().front() == held);
        CHECK_FALSE (held->isFading());
    }
    {
        auto a = f.atlas;
        a.mapping.fill = false;
        const auto r = atlasset::buildInstrumentSet (a, f.audio, {});
        REQUIRE (r.set != nullptr);
        CHECK (played (*r.set, 60) == 60.0);
        CHECK (played (*r.set, 61) == -1.0);
        CHECK (played (*r.set, 59) == -1.0);
    }
}

TEST_CASE ("Atlas set: repeated recordings of one note play as round-robin variations", "[integration][atlas]")
{
    const auto set = setOf (fixture ("repeated-note"));
    REQUIRE (set->groups.size() == 1);
    REQUIRE (set->groups.front().members.size() == 3);
    auto sequence = [&] {
        Rig rig;
        rig.engine.setInstrumentSet (set.get());
        std::vector<int> takes;
        for (int i = 0; i < 12; ++i)
        {
            rig.engine.noteOn (62, 100);
            rig.run (0.05);
            for (const auto* v : rig.voices())
                if (! v->isReleased())
                {
                    takes.push_back (memberPlayed (*set, *v).take);
                    break;
                }
            rig.engine.noteOff (62);
            rig.run (0.05);
        }
        return takes;
    };
    const auto takes = sequence();
    REQUIRE (takes.size() == 12);
    for (std::size_t i = 1; i < takes.size(); ++i)
        CHECK (takes[i] != takes[i - 1]);   // never the same take twice in a row
    CHECK (std::set<int> (takes.begin(), takes.end()).size() == 3);
    CHECK (sequence() == takes);            // deterministic
}

TEST_CASE ("Atlas set: velocity layers - soft notes play the soft recordings, hard notes the loud ones", "[integration][atlas]")
{
    const auto set = setOf (fixture ("velocity-layers"));
    REQUIRE (set->groups.size() == 1);
    CHECK (set->groups.front().layers == 2);
    for (const auto& [velocity, layer] : { std::pair<int, int> { 20, 0 }, { 50, 0 }, { 90, 1 }, { 127, 1 } })
    {
        InstrumentEngine e;
        e.prepare (48000.0, 256, dry());
        e.setInstrumentSet (set.get());
        for (std::uint64_t ev = 1; ev < 5; ++ev)
        {
            const int m = e.memberFor (60, velocity, ev);
            REQUIRE (m >= 0);
            CHECK (set->members[static_cast<std::size_t> (m)].layer == layer);
        }
    }
}

TEST_CASE ("Atlas set: polyphony - a chord plays each key's own recording", "[integration][atlas]")
{
    const auto set = setOf (fixture ("sung-four"));
    Rig rig;
    rig.engine.setInstrumentSet (set.get());
    for (int key : { 57, 60, 64, 67 })
        rig.engine.noteOn (key, 100);
    rig.run (0.3);
    const auto v = rig.voices();
    REQUIRE (v.size() == 4);
    std::set<const InstrumentModel*> models;
    for (const auto* voice : v)
    {
        models.insert (voice->model());
        CHECK (memberPlayed (*set, *voice).model->rootMidi == Approx (voice->note()));
    }
    CHECK (models.size() == 4);
}

TEST_CASE ("Atlas set: grains and One Shot never read into a neighbouring note", "[integration][atlas]")
{
    // 440 Hz right next to 3 kHz in the recording. Play the 440 Hz note's key: neither its
    // One Shot read nor dense grains with SPREAD 100 % may bring any of the 3 kHz note in.
    const auto set = setOf (fixture ("boundary-probe"));
    const int key = static_cast<int> (std::lround (hzToMidi (440.0)));
    for (bool granular : { false, true })
    {
        auto s = dry();
        if (granular)
        {
            s.sourceMode[0] = SourceMode::granular;
            s.granular[0] = { 0.5, 0.2, 40.0, 0.0, 1.0 };
            s.granular[0].scatterSemitones = 0.0;
        }
        Rig rig (s);
        rig.engine.setInstrumentSet (set.get());
        rig.engine.noteOn (key, 100);
        rig.run (1.0);
        const double wanted = energyAt (rig.out, 440.0), leak = energyAt (rig.out, 3000.0);
        INFO ((granular ? "granular" : "one shot") << ": 3 kHz is " << 10.0 * std::log10 (leak / wanted) << " dB re 440 Hz");
        CHECK (wanted > 0.0);
        CHECK (leak < wanted * 1.0e-6);   // below -60 dB
    }
}

TEST_CASE ("Atlas set: short regions, legato boundaries and long silences build and play", "[integration][atlas]")
{
    for (const char* name : { "legato", "silence-gaps", "bowed-phrase", "stereo-wide" })
    {
        DYNAMIC_SECTION (name)
        {
            const auto& f = fixture (name);
            const auto set = setOf (f);
            CHECK (set->members.size() == f.atlas.notes.size());
            for (int mode = 0; mode < 2; ++mode)
            {
                auto s = dry();
                s.sourceMode[0] = mode == 0 ? SourceMode::oneShot : SourceMode::granular;
                Rig rig (s);
                rig.engine.setInstrumentSet (set.get());
                for (const auto& g : set->groups)
                    rig.engine.noteOn (static_cast<int> (g.rootMidi), 100);
                rig.run (0.6);
                double peak = 0.0;
                for (const auto& ch : rig.out.channels)
                    for (float x : ch)
                        peak = std::max (peak, static_cast<double> (std::abs (x)));
                CHECK (std::isfinite (peak));
                CHECK (peak > 1.0e-3);
                CHECK (peak < 1.0);
            }
        }
    }
    // Regions down to 30 ms (and a 100-frame note) still become members and play.
    auto f = atlasfixtures::manyRegions (6, 0.03, 6, 1);
    auto tiny = f.atlas.notes.back();
    tiny.id = f.atlas.nextNoteId();
    tiny.sourceEnd = tiny.attackStart + 100;
    tiny.sustain = { tiny.attackStart + 10, tiny.attackStart + 80 };
    tiny.releaseStart = tiny.sustain.end;
    tiny.rootMidi = 90;
    tiny.midiPitch = 90.0;
    f.atlas.notes.push_back (tiny);
    const auto r = atlasfixtures::build (f);
    REQUIRE (r.set != nullptr);
    CHECK (r.report.notesUsed == 7);
    for (int mode = 0; mode < 2; ++mode)
    {
        auto s = dry();
        s.sourceMode[0] = mode == 0 ? SourceMode::oneShot : SourceMode::granular;
        Rig rig (s);
        rig.engine.setInstrumentSet (r.set.get());
        rig.engine.noteOn (90, 100);
        rig.engine.noteOn (50, 100);
        rig.run (0.3);
        for (float x : rig.out.channels[0])
            REQUIRE (std::isfinite (x));
    }
}

TEST_CASE ("Atlas set: mono and stereo recordings keep their channels", "[integration][atlas]")
{
    auto sideMid = [] (const InstrumentSet& set, int key) {
        Rig rig;
        rig.engine.setInstrumentSet (&set);
        rig.engine.noteOn (key, 100);
        rig.run (0.5);
        double m = 0.0, s = 0.0;
        for (std::size_t i = 0; i < rig.out.channels[0].size(); ++i)
        {
            const double l = rig.out.channels[0][i], r = rig.out.channels[1][i];
            m += (l + r) * (l + r);
            s += (l - r) * (l - r);
        }
        return s / std::max (1.0e-20, m);
    };
    const auto mono = setOf (fixture ("silence-gaps"));
    const auto wide = setOf (fixture ("stereo-wide"));
    CHECK (mono->members.front().model->original.source->numChannels() == 1);
    CHECK (wide->members.front().model->original.source->numChannels() == 2);
    CHECK (sideMid (*mono, 48) < 1.0e-6);
    CHECK (sideMid (*wide, 57) > 0.3);
}

TEST_CASE ("Atlas set: A, B and C are independent; three atlases play together", "[integration][atlas]")
{
    const auto sung = setOf (fixture ("sung-four"));
    const auto bowed = setOf (fixture ("bowed-phrase"));
    auto sparse = fixture ("missing-notes").atlas;
    sparse.mapping.fill = false;   // C plays only C3, C4, C5
    const auto sparseSet = atlasset::buildInstrumentSet (sparse, fixture ("missing-notes").audio, {}).set;
    REQUIRE (sparseSet != nullptr);
    Rig rig;
    rig.engine.setInstrumentSet (sung.get(), 0);
    rig.engine.setInstrumentSet (bowed.get(), 1);
    rig.engine.setInstrumentSet (sparseSet.get(), 2);
    rig.engine.noteOn (59, 100);   // A: nearest 57/60, B: B3 itself, C: nothing (no fill)
    rig.engine.noteOn (60, 100);   // all three
    rig.run (0.3);
    std::map<int, std::vector<int>> keysByLayer;
    for (const auto* v : rig.voices())
        keysByLayer[v->layerIndex()].push_back (v->note());
    CHECK (keysByLayer[0].size() == 2);
    CHECK (keysByLayer[1].size() == 2);
    REQUIRE (keysByLayer[2].size() == 1);
    CHECK (keysByLayer[2].front() == 60);
    for (const auto* v : rig.voices())
    {
        const auto& set = v->layerIndex() == 0 ? *sung : (v->layerIndex() == 1 ? *bowed : *sparseSet);
        memberPlayed (set, *v);   // each voice plays its own layer's atlas
    }
}

TEST_CASE ("Atlas set: Granular, KEY DNA and the grain options act on atlas notes as on any sound", "[integration][atlas][keydna]")
{
    const auto set = setOf (fixture ("sung-four"));
    auto s = dry();
    s.sourceMode[0] = SourceMode::granular;
    s.granular[0] = { 0.5, 0.1, 20.0, 0.0, 0.0 };
    auto render = [&] (const keydna::Settings* dna, double scatter, double pan) {
        auto settings = s;
        settings.granular[0].scatterSemitones = scatter;
        settings.granular[0].pan = pan;
        Rig rig (settings);
        rig.engine.setInstrumentSet (set.get());
        if (dna != nullptr)
            rig.engine.setLayerKeyDna (0, *dna);
        rig.engine.noteOn (64, 100);
        rig.run (0.6);
        return rig.out;
    };
    keydna::Settings dna;
    dna.enabled = true;
    dna.seed = 5;
    dna.individuality = 1.0;
    dna[keydna::Param::position].variation = 0.8;
    const auto plain = render (nullptr, 0.0, 0.0);
    const auto withDna = render (&dna, 0.0, 0.0);
    CHECK (pitchOf (plain) == Approx (64.0).margin (0.1));
    CHECK (withDna.channels[0] != plain.channels[0]);
    CHECK (render (&dna, 0.0, 0.0).channels[0] == withDna.channels[0]);   // deterministic
    keydna::Table t;
    t.compute (dna);
    CHECK (t.at (64).active);   // (KEY DNA applies the played key's offsets)
    CHECK (render (nullptr, 3.0, 0.0).channels[0] != plain.channels[0]);   // SCATTER
    CHECK (render (nullptr, 0.0, 0.6).channels[1] != plain.channels[1]);   // PAN
    // Grains read inside the note: the display's positions are 0..1 of the member's region.
    Rig rig (s);
    rig.engine.setInstrumentSet (set.get());
    rig.engine.noteOn (64, 100);
    int seen = 0;
    rig.run (0.5, [&] {
        const auto& snap = rig.engine.grainSnapshot (0);
        for (int i = 0; i < snap.count.load(); ++i)
        {
            const float p = snap.position[static_cast<std::size_t> (i)].load();
            CHECK (p >= 0.0f);
            CHECK (p <= 1.0f);
            ++seen;
        }
    });
    CHECK (seen > 50);
}

TEST_CASE ("Atlas set: a bounded grain budget holds with three granular atlases, even with more streams than grains", "[integration][atlas][budget]")
{
    const auto sung = setOf (fixture ("sung-four"));
    const auto bowed = setOf (fixture ("bowed-phrase"));
    const auto legato = setOf (fixture ("legato"));
    for (int budget : { 64, 6 })
    {
        DYNAMIC_SECTION ("budget " << budget)
        {
            auto s = dry();
            for (int l = 0; l < 3; ++l)
            {
                s.sourceMode[static_cast<std::size_t> (l)] = SourceMode::granular;
                s.granular[static_cast<std::size_t> (l)] = { 0.5, 0.4, 40.0, 0.0, 0.5 };
            }
            s.grainBudget = budget;
            s.grainBudgetPolicy = GrainBudgetPolicy::bounded;
            Rig rig (s);
            rig.engine.setInstrumentSet (sung.get(), 0);
            rig.engine.setInstrumentSet (bowed.get(), 1);
            rig.engine.setInstrumentSet (legato.get(), 2);
            for (int key : { 55, 57, 59, 60, 62, 64, 65, 67 })
                rig.engine.noteOn (key, 100);
            int most = 0;
            rig.run (1.0, [&] { most = std::max (most, rig.engine.liveGrainCount()); });
            INFO ("at most " << most << " grains");
            CHECK (most <= budget);
            CHECK (most >= budget / 2);
        }
    }
}

TEST_CASE ("Grain budget (bounded): never above the budget, with more streams than grains; Unlimited unchanged", "[integration][budget]")
{
    // The Phase 2 edge case: 24 dense streams under a budget of 10. The shared policy's floor
    // of one grain per stream lets them reach 24; the bounded one holds 10.
    auto s = dry();
    s.sourceMode[0] = SourceMode::granular;
    s.granular[0] = { 0.5, 0.4, 40.0, 0.0, 0.5 };
    s.polyphony = 24;
    const auto set = setOf (fixture ("sung-four"));
    auto most = [&] (GrainBudgetPolicy policy, int budget) {
        auto settings = s;
        settings.grainBudget = budget;
        settings.grainBudgetPolicy = policy;
        Rig rig (settings);
        rig.engine.setInstrumentSet (set.get());
        for (int k = 0; k < 24; ++k)
            rig.engine.noteOn (40 + k, 100);
        int m = 0;
        rig.run (1.0, [&] { m = std::max (m, rig.engine.liveGrainCount()); });
        return m;
    };
    CHECK (most (GrainBudgetPolicy::shared, 10) > 10);   // the documented edge case
    CHECK (most (GrainBudgetPolicy::bounded, 10) <= 10);
    CHECK (most (GrainBudgetPolicy::bounded, 10) >= 8);
    // Unlimited: the policy is ignored.
    CHECK (most (GrainBudgetPolicy::shared, 0) == most (GrainBudgetPolicy::bounded, 0));
}

TEST_CASE ("Atlas set: replacing an atlas while notes sound keeps the sounding notes on their own audio", "[integration][atlas]")
{
    auto first = setOf (fixture ("sung-four"));
    const auto second = setOf (fixture ("bowed-phrase"));
    Rig rig;
    rig.engine.setInstrumentSet (first.get());
    rig.engine.noteOn (60, 100);
    rig.run (0.2);
    // The host replaces the layer's atlas (ModelExchange in the plugin); the sounding note
    // keeps reading the first set's member, a new note uses the second.
    rig.engine.setInstrumentSet (second.get());
    rig.engine.noteOn (59, 100);
    rig.run (0.2);
    CHECK (rig.engine.isSetInUse (first.get()));
    CHECK (rig.engine.isSetInUse (second.get()));
    bool oldNote = false, newNote = false;
    for (const auto* v : rig.voices())
    {
        if (v->note() == 60)
        {
            memberPlayed (*first, *v);
            oldNote = true;
        }
        if (v->note() == 59)
        {
            memberPlayed (*second, *v);
            newNote = true;
        }
    }
    CHECK (oldNote);
    CHECK (newNote);
    // Once released and finished, the first set is no longer in use: the host may free it.
    rig.engine.noteOff (60);
    rig.run (3.0);
    CHECK_FALSE (rig.engine.isSetInUse (first.get()));
}

TEST_CASE ("Atlas set: missing or mismatched audio builds nothing (it cannot look restored)", "[integration][atlas]")
{
    const auto& f = fixture ("sung-four");
    auto none = atlasset::buildInstrumentSet (f.atlas, {}, {});
    CHECK (none.set == nullptr);
    REQUIRE (none.report.missingSources.size() == 1);
    CHECK (none.report.missingSources.front() == "src0");
    CHECK_FALSE (none.report.ok);
    // The right id with the wrong audio (another length): refused, not played wrong.
    auto shorter = std::make_shared<AudioData> (*f.audio.front().audio);
    for (auto& ch : shorter->channels)
        ch.resize (ch.size() / 2);
    auto wrong = atlasset::buildInstrumentSet (f.atlas, { { "src0", shorter } }, {});
    CHECK (wrong.set == nullptr);
    CHECK_FALSE (wrong.report.errors.empty());
    // An invalid atlas (markers out of order) is refused with its validation errors.
    auto broken = f.atlas;
    broken.notes[1].sourceEnd = broken.notes[1].sourceStart - 10;
    CHECK (atlasset::buildInstrumentSet (broken, f.audio, {}).set == nullptr);
}

TEST_CASE ("Atlas set: the memory limit skips notes beyond it, with a warning", "[integration][atlas]")
{
    const auto f = atlasfixtures::manyRegions (10, 0.5, 10);
    atlasset::BuildOptions options;
    options.maxTotalSeconds = 2.2;   // room for four 0.5 s notes
    const auto r = atlasfixtures::build (f, options);
    REQUIRE (r.set != nullptr);
    CHECK (r.report.notesUsed == 4);
    CHECK (r.report.notesSkipped == 6);
    CHECK (r.report.warnings.size() >= 6);
    options.maxTotalSeconds = 600.0;
    options.maxRegionSeconds = 0.25;   // and a region longer than its limit is cut
    const auto cut = atlasfixtures::build (f, options);
    REQUIRE (cut.set != nullptr);
    CHECK (cut.set->members.front().model->original.source->numFrames() == static_cast<std::int64_t> (0.25 * 48000.0));
}

TEST_CASE ("Atlas set: file sample sets still play every key (their mapping is unchanged)", "[integration][atlas]")
{
    // A set inferred from dropped files keeps the original nearest-anchor behaviour.
    const auto& f = fixture ("missing-notes");
    const auto atlasSet = setOf (f);
    InstrumentSet files = *atlasSet;
    files.fill = true;
    files.fillLimitSemitones = std::numeric_limits<double>::infinity();
    files.loopSelects = false;   // an atlas's LOOP selection (file sets never have it)
    CHECK_FALSE (files.restrictsKeys());
    CHECK_FALSE (InstrumentSet {}.loopSelects);
    for (int key : { 0, 30, 100, 127 })
        CHECK (files.groupFor (key) >= 0);
    CHECK (InstrumentSet {}.fill);
    CHECK_FALSE (InstrumentSet {}.restrictsKeys());
}

TEST_CASE ("Atlas set: renders are deterministic, and playing allocates nothing", "[integration][atlas]")
{
    const auto set = setOf (fixture ("sung-four"));
    auto take = [&] {
        Rig rig;
        rig.engine.setInstrumentSet (set.get());
        for (int key : { 57, 60, 62, 64, 66, 67 })
            rig.engine.noteOn (key, 90);
        rig.run (0.5);
        return rig.out.channels[0];
    };
    CHECK (take() == take());
    auto s = dry();
    s.sourceMode[0] = SourceMode::granular;
    Rig rig (s);
    rig.engine.setInstrumentSet (set.get());
    rig.engine.noteOn (60, 100);
    rig.run (0.1);
    rig.record = false;
    int allocations = 0;
    {
        test::AllocationScope scope;
        for (int key : { 57, 59, 62, 64, 66, 67, 80, 20 })
            rig.engine.noteOn (key, 90);
        rig.run (0.2);
        rig.engine.allNotesOff();
        rig.run (0.2);
        allocations = scope.count();
    }
    CHECK (allocations == 0);
}
