// Pedal Loop 1.0 with the rest of the instrument (docs/research/pedal-loop/04-integration.md):
// Note Atlas members each loop over their own body; SYNTH / Imprint carries on with the loop
// without a new attack.

#include "analysis/phrase/PhraseAnalysis.h"
#include "audio/utility/TestSignals.h"
#include "engine/AtlasSet.h"
#include "engine/InstrumentBuilder.h"
#include "engine/InstrumentEngine.h"
#include "research/AtlasFixtures.h"
#include "support/AllocationCounter.h"
#include "support/TestHelpers.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <set>

using namespace osp;
using namespace osp::research;

namespace
{
    constexpr double rate = 48000.0;

    /** Three sung notes in a row (G3, B3, D4), each 0.8 s. */
    AudioData sungPhrase()
    {
        AudioData out;
        out.sampleRate = rate;
        out.channels.resize (1);
        for (const double f : { 196.0, 246.9, 293.7 })
        {
            auto v = testsignals::vowel (f, 0.8, rate, static_cast<std::uint64_t> (f), 0.45, 1);
            testsignals::applyFades (v, 0.03, 0.05);
            out.channels[0].insert (out.channels[0].end(), v.channels[0].begin(), v.channels[0].end());
        }
        return out;
    }

    void run (InstrumentEngine& engine, double seconds)
    {
        std::vector<float> l (64), r (64);
        float* ch[2] = { l.data(), r.data() };
        for (int done = 0; done < static_cast<int> (seconds * rate); done += 64)
            engine.render (ch, 2, 64);
    }
}

TEST_CASE ("pedal loop: Note Atlas members each loop over their own body, held apart", "[integration][pedal-loop]")
{
    const auto audio = sungPhrase();
    atlas::SourceAsset source;
    source.assetId = "src0";
    source.contentHash = atlasfixtures::contentHashOf (audio);
    source.fileName = "phrase.wav";
    auto analysed = phrase::analysePhrase (audio, source);
    REQUIRE (analysed.atlas.notes.size() >= 2);
    const auto built = atlasset::buildInstrumentSet (analysed.atlas, { { "src0", std::make_shared<const AudioData> (audio) } }, {});
    REQUIRE (built.set != nullptr);
    REQUIRE (built.set->members.size() >= 2);
    int withLoops = 0;
    for (const auto& m : built.set->members)
        withLoops += m.model->original.continuation.pedal.valid ? 1 : 0;
    CHECK (withLoops == static_cast<int> (built.set->members.size()));   // each from its own audio

    EngineSettings s;
    s.macros.space = 0.0;
    s.macros.motion = 0.0;
    s.layer[0].pedalLoop = PedalLoopMode::pingPong;
    InstrumentEngine engine;
    engine.prepare (rate, 64, s);
    engine.setInstrumentSet (built.set.get(), 0);
    engine.setSustainPedal (true);
    std::vector<int> keys;
    for (const auto& m : built.set->members)
        keys.push_back (static_cast<int> (std::lround (m.model->rootMidi)));
    for (const int k : keys)
        engine.noteOn (k, 100);
    run (engine, 0.2);
    for (const int k : keys)
        engine.noteOff (k);
    run (engine, 5.0);
    std::set<const InstrumentModel*> models;
    for (int i = 0; i < InstrumentEngine::voiceSlots(); ++i)
        if (const auto& v = engine.voiceAt (i); v.isActive())
        {
            CHECK (v.pedalLooping());
            const auto& regions = v.model()->original.continuation.pedal.regions;
            REQUIRE (v.pedalRegionIndex() >= 0);
            const auto& r = regions[static_cast<std::size_t> (v.pedalRegionIndex())];
            CHECK (v.readHead() >= r.startFrame - 4.0);   // its own member's region, not another's
            CHECK (v.readHead() <= r.endFrame + 4.0);
            models.insert (v.model());
        }
    CHECK (models.size() == keys.size());   // one member per voice, kept for the note
    engine.setSustainPedal (false);
    run (engine, 1.0);
    CHECK (engine.activeVoiceCount() == 0);
}

TEST_CASE ("pedal loop: with SYNTH (Imprint) the oscillator carries on through the loop; the recording never ends under the pedal", "[integration][pedal-loop]")
{
    auto audio = testsignals::pluck (220.0, 2.0, rate, 5, 0.7);
    testsignals::applyFades (audio, 0.0, 0.8);
    const auto model = instrument::buildComplete (audio, test::analyse (audio), {}, false);
    EngineSettings s;
    s.macros.space = 0.0;
    s.macros.motion = 0.0;
    s.layer[0].pedalLoop = PedalLoopMode::pingPong;
    s.layer[0].synthesize = 0.5;
    s.layer[0].synth.mode = synth::Mode::imprint;
    s.layer[0].synth.imprint = 1.0;
    InstrumentEngine engine;
    engine.prepare (rate, 64, s);
    engine.setModel (model.get(), 0);
    engine.setSustainPedal (true);
    engine.noteOn (57, 100);
    run (engine, 0.3);
    engine.noteOff (57);
    std::vector<float> l (64), r (64);
    float* ch[2] = { l.data(), r.data() };
    double sum = 0.0;
    bool finite = true;
    for (int done = 0; done < static_cast<int> (5.0 * rate); done += 64)
    {
        engine.render (ch, 2, 64);
        for (int i = 0; i < 64; ++i)
        {
            finite = finite && std::isfinite (l[static_cast<std::size_t> (i)]);
            if (done > static_cast<int> (4.0 * rate))
                sum += static_cast<double> (l[static_cast<std::size_t> (i)]) * l[static_cast<std::size_t> (i)];
        }
    }
    CHECK (finite);
    CHECK (10.0 * std::log10 (std::max (1.0e-20, sum / rate)) > -60.0);   // both still sounding at 5 s (the recording is 2 s)
    bool looping = false;
    for (int i = 0; i < InstrumentEngine::voiceSlots(); ++i)
        if (const auto& v = engine.voiceAt (i); v.isActive())
            looping = looping || (v.pedalLooping() && v.synthesizeNow() > 0.4);
    CHECK (looping);
    engine.setSustainPedal (false);
    run (engine, 1.5);
    CHECK (engine.activeVoiceCount() == 0);
}

TEST_CASE ("pedal loop: no heap allocation while notes loop, turn, arm late, leave and the pedal is pumped", "[integration][pedal-loop][realtime]")
{
    auto audio = testsignals::pluck (220.0, 2.0, rate, 5, 0.7);
    testsignals::applyFades (audio, 0.0, 0.8);
    const auto model = instrument::buildComplete (audio, test::analyse (audio), {}, false);
    for (const auto mode : { PedalLoopMode::forward, PedalLoopMode::pingPong })
    {
        EngineSettings s;
        s.layer[0].pedalLoop = mode;
        InstrumentEngine engine;
        engine.prepare (rate, 64, s);   // (allocates: before the scope)
        engine.setModel (model.get(), 0);
        std::vector<float> l (64), r (64);
        float* ch[2] = { l.data(), r.data() };
        int allocations = -1;
        {
            test::AllocationScope scope;
            engine.setSustainPedal (true);
            for (const int n : { 57, 60, 64 })
                engine.noteOn (n, 100);
            bool down = true;
            for (int b = 0; b < static_cast<int> (6.0 * rate / 64); ++b)
            {
                if (b == static_cast<int> (0.5 * rate / 64))
                    for (const int n : { 57, 60, 64 })
                        engine.noteOff (n);
                if (b > static_cast<int> (3.0 * rate / 64) && b % 7 == 0)
                    engine.setSustainPedal (down = ! down);
                if (b == static_cast<int> (1.9 * rate / 64))
                    engine.noteOn (69, 90);   // a note whose pedal comes late (pumped below)
                engine.render (ch, 2, 64);
            }
            engine.setSustainPedal (false);
            engine.allNotesOff();
            engine.render (ch, 2, 64);
            allocations = scope.count();
        }
        INFO ("mode " << static_cast<int> (mode));
        CHECK (allocations == 0);
    }
}
