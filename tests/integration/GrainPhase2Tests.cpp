// Unified engine, Phase 2, in OSP's own granulator:
//  - KEY DNA AMOUNT is live on sounding notes (smoothed, deterministic, the key's identity fixed);
//  - SCATTER, PAN and the grain windows are neutral by default and draw from their own stream;
//  - the grain budget is deterministic and fair across A, B and C, and changes nothing until it
//    is reached;
//  - none of it allocates on the audio thread.

#include "engine/InstrumentEngine.h"
#include "engine/KeyDna.h"
#include "research/RenderConfig.h"
#include "research/UnifiedScenes.h"
#include "support/AllocationCounter.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>

using namespace osp;
using namespace osp::research;
using Catch::Approx;

namespace
{
    const unified::Scene& scene (const std::vector<unified::Scene>& scenes, const char* name)
    {
        const auto* s = unified::findScene (scenes, name);
        REQUIRE (s != nullptr);
        return *s;
    }
    const std::vector<unified::Scene>& baseline()
    {
        static const auto s = unified::baselineScenes();
        return s;
    }
    const std::vector<unified::Scene>& phase2()
    {
        static const auto s = unified::phase2Scenes();
        return s;
    }
    const std::vector<unified::Scene>& keydnaDemos()
    {
        static const auto s = unified::keyDnaScenes();
        return s;
    }

    AudioData renderWith (const unified::Scene& base, const std::function<void (RenderConfig&)>& edit)
    {
        auto config = base.config;
        edit (config);
        return renderLayers (unified::modelsFor (base), base.midi, config).audio;
    }

    double energy (const std::vector<float>& x, std::size_t from = 0, std::size_t to = SIZE_MAX)
    {
        double e = 0.0;
        to = std::min (to, x.size());
        for (std::size_t i = from; i < to; ++i)
            e += static_cast<double> (x[i]) * x[i];
        return e;
    }
    double db (double ratio) { return 10.0 * std::log10 (std::max (1.0e-30, ratio)); }

    struct MidSide
    {
        double mid = 0.0, side = 0.0;
    };
    MidSide midSide (const AudioData& a)
    {
        MidSide m;
        for (std::size_t i = 0; i < a.channels[0].size(); ++i)
        {
            const double l = a.channels[0][i], r = a.channels[1][i];
            m.mid += 0.25 * (l + r) * (l + r);
            m.side += 0.25 * (l - r) * (l - r);
        }
        return m;
    }

    /** The largest sample-to-sample step in [from, to) of either channel. */
    double maxStep (const AudioData& a, std::size_t from, std::size_t to)
    {
        double m = 0.0;
        for (const auto& ch : a.channels)
            for (std::size_t i = std::max<std::size_t> (from, 1); i < std::min (to, ch.size()); ++i)
                m = std::max (m, static_cast<double> (std::abs (ch[i] - ch[i - 1])));
        return m;
    }

    /** A live engine on the demonstration vowel: one or three Granular layers, LIFE and effects off. */
    struct Rig
    {
        InstrumentEngine engine;
        int block;
        std::vector<float> l, r;
        Rig (const EngineSettings& s, int layers = 1, int blockSize = 256) : block (blockSize)
        {
            engine.prepare (48000.0, block, s);
            const auto models = unified::modelsFor (scene (baseline(), "layers-abc-granular"));
            for (int i = 0; i < layers; ++i)
                engine.setModel (models[static_cast<std::size_t> (i)], i);
            l.resize (static_cast<std::size_t> (block));
            r.resize (static_cast<std::size_t> (block));
        }
        void run (double seconds, const std::function<void()>& eachBlock = {})
        {
            const int blocks = std::max (1, static_cast<int> (seconds * 48000.0 / block));
            for (int b = 0; b < blocks; ++b)
            {
                float* ch[2] = { l.data(), r.data() };
                engine.render (ch, 2, block);
                if (eachBlock)
                    eachBlock();
            }
        }
        const InstrumentVoice* voiceFor (int note) const
        {
            for (int i = 0; i < InstrumentEngine::voiceSlots(); ++i)
                if (const auto& v = engine.voiceAt (i); v.isActive() && ! v.isReleased() && v.note() == note)
                    return &v;
            return nullptr;
        }
        /** Every grain the display shows on `layer` this block: positions and lanes, in order. */
        std::vector<float> grainsNow (int layer) const
        {
            std::vector<float> out;
            const auto& snap = engine.grainSnapshot (layer);
            for (int i = 0; i < snap.count.load(); ++i)
            {
                out.push_back (snap.position[static_cast<std::size_t> (i)].load());
                out.push_back (snap.lane[static_cast<std::size_t> (i)].load());
            }
            return out;
        }
    };

    EngineSettings plainGranular (int layers = 1, GranularParams g = { 0.5, 0.08, 20.0, 0.0, 0.2 })
    {
        EngineSettings s;
        s.macros.life = 0.0;
        s.macros.motion = 0.0;
        s.macros.space = 0.0;
        s.macros.reimagined = 0.0;
        for (int l = 0; l < layers; ++l)
        {
            s.sourceMode[static_cast<std::size_t> (l)] = SourceMode::granular;
            s.granular[static_cast<std::size_t> (l)] = g;
        }
        return s;
    }
}

// ---------------------------------------------------------------------------------------------
// The grain options

TEST_CASE ("Phase 2 options: SCATTER 0, PAN 0 and HANN are exactly the Granulator as it was", "[integration][phase2]")
{
    const auto& neutral = scene (phase2(), "p2-options-neutral-chord4");
    const auto& original = scene (keydnaDemos(), "keydna-a-static-chord4");
    CHECK (unified::digest (unified::render (neutral).audio).hash == unified::digest (unified::render (original).audio).hash);
}

TEST_CASE ("Phase 2 options: PAN and the windows never move a grain (their own stream)", "[integration][phase2]")
{
    // Where every grain reads, and its display lane, block by block: identical with the option on.
    auto trace = [] (GranularParams g) {
        Rig rig (plainGranular (1, g));
        std::vector<float> all;
        rig.engine.noteOn (57, 100);
        rig.engine.noteOn (64, 90);
        rig.run (0.8, [&] { const auto now = rig.grainsNow (0); all.insert (all.end(), now.begin(), now.end()); });
        return all;
    };
    GranularParams base { 0.5, 0.08, 20.0, 0.0, 0.2 };
    const auto plain = trace (base);
    REQUIRE (plain.size() > 200);
    auto panned = base;
    panned.pan = 0.8;
    CHECK (trace (panned) == plain);
    auto soft = base;
    soft.window = GrainWindow::soft;
    CHECK (trace (soft) == plain);
    auto perc = base;
    perc.window = GrainWindow::percussive;
    perc.pan = 0.3;
    CHECK (trace (perc) == plain);

    // SCATTER changes each grain's pitch (so where it reads next), never when grains start or end.
    auto counts = [] (GranularParams g) {
        Rig rig (plainGranular (1, g));
        std::vector<int> n;
        rig.engine.noteOn (57, 100);
        rig.run (0.8, [&] { n.push_back (rig.engine.liveGrainCount()); });
        return n;
    };
    auto scattered = base;
    scattered.scatterSemitones = 7.0;
    CHECK (counts (scattered) == counts (base));
    CHECK (trace (scattered) != plain);
}

TEST_CASE ("Phase 2 options: PAN widens a mono recording without losing level, and stays mono-compatible", "[integration][phase2]")
{
    const auto& single = scene (phase2(), "p2-pan-100-mono-single");
    const auto centred = renderWith (single, [] (RenderConfig& c) { c.engineSettings.granular[0].pan = 0.0; });
    const auto wide = unified::render (single).audio;
    const auto half = renderWith (single, [] (RenderConfig& c) { c.engineSettings.granular[0].pan = 0.5; });
    const auto c0 = midSide (centred), c1 = midSide (wide), c5 = midSide (half);
    INFO ("side/mid: PAN 0 " << c0.side / c0.mid << ", 50 % " << c5.side / c5.mid << ", 100 % " << c1.side / c1.mid);
    CHECK (c0.side / c0.mid < 0.02);   // a mono recording without PAN: centred (but for SPACE's reverb)
    CHECK (c5.side / c5.mid > 0.02);
    CHECK (c1.side / c1.mid > 2.0 * (c5.side / c5.mid));
    // Equal power: L + R energy within 1 dB; the mono sum loses at most ~1.5 dB at full PAN.
    const double total0 = energy (centred.channels[0]) + energy (centred.channels[1]);
    const double total1 = energy (wide.channels[0]) + energy (wide.channels[1]);
    INFO ("L+R energy " << db (total1 / total0) << " dB, mono sum " << db (c1.mid / c0.mid) << " dB");
    CHECK (std::abs (db (total1 / total0)) < 1.0);
    CHECK (db (c1.mid / c0.mid) > -1.5);
}

TEST_CASE ("Phase 2 options: PAN on a stereo recording moves each grain's image, keeping its energy", "[integration][phase2]")
{
    // A stereo grain is balanced (its L and R scaled), so a hard-panned grain is one channel of
    // the recording: on a very wide source (the test vowel's side/mid is about 2) PAN makes the
    // image move rather than widen. The energy and the mono sum still hold.
    const auto& single = scene (phase2(), "p2-pan-100-single");
    const auto centred = renderWith (single, [] (RenderConfig& c) { c.engineSettings.granular[0].pan = 0.0; });
    const auto wide = unified::render (single).audio;
    const double total0 = energy (centred.channels[0]) + energy (centred.channels[1]);
    const double total1 = energy (wide.channels[0]) + energy (wide.channels[1]);
    INFO ("L+R energy " << db (total1 / total0) << " dB");
    CHECK (std::abs (db (total1 / total0)) < 1.0);
    CHECK (unified::digest (wide).hash != unified::digest (centred).hash);
}

TEST_CASE ("Phase 2 options: SOFT and PERCUSSIVE are level-matched to HANN", "[integration][phase2]")
{
    const auto& chord = scene (phase2(), "p2-options-neutral-chord4");
    auto rms = [&] (GrainWindow w) {
        const auto a = renderWith (chord, [w] (RenderConfig& c) { c.engineSettings.granular[0].window = w; });
        return energy (a.channels[0]) + energy (a.channels[1]);
    };
    const double hann = rms (GrainWindow::hann);
    const double soft = db (rms (GrainWindow::soft) / hann), perc = db (rms (GrainWindow::percussive) / hann);
    INFO ("SOFT " << soft << " dB, PERCUSSIVE " << perc << " dB against HANN");
    CHECK (std::abs (soft) < 1.5);
    CHECK (std::abs (perc) < 1.5);
}

TEST_CASE ("Phase 2 options: every option at its extreme stays finite and below full scale", "[integration][phase2]")
{
    for (const char* name : { "p2-scatter-12-single", "p2-pan-100-single", "p2-window-percussive-sparse-scale", "p2-keydna-scatter-pan-chord8" })
    {
        const auto a = unified::render (scene (phase2(), name)).audio;
        const auto d = unified::digest (a);
        INFO (name << ": peak " << d.peakDb << " dB, rms " << d.rmsDb << " dB");
        CHECK (std::isfinite (d.rmsDb));
        CHECK (d.rmsDb > -40.0);
        CHECK (d.peakDb < 0.0);
    }
}

TEST_CASE ("Phase 2 KEY DNA: SCATTER and PAN by key - the high keys are the wide, scattered ones", "[integration][phase2][keydna]")
{
    auto widthOf = [] (int key) {
        Rig rig (plainGranular (1, { 0.45, 0.12, 16.0, 0.0, 0.15 }));
        rig.engine.setLayerKeyDna (0, unified::scatterPanKeyDna());
        AudioData a;
        a.channels.resize (2);
        rig.engine.noteOn (key, 100);
        rig.run (0.8, [&] {
            a.channels[0].insert (a.channels[0].end(), rig.l.begin(), rig.l.end());
            a.channels[1].insert (a.channels[1].end(), rig.r.begin(), rig.r.end());
        });
        const auto m = midSide (a);
        return m.side / m.mid;
    };
    keydna::Table t;
    t.compute (unified::scatterPanKeyDna());
    CHECK (t.at (84).pan > t.at (40).pan + 0.2);
    CHECK (t.at (84).scatterSemitones > t.at (40).scatterSemitones + 1.0);
    const double low = widthOf (40), high = widthOf (84);
    INFO ("side/mid: key 40 " << low << ", key 84 " << high);
    CHECK (high > low);
}

// ---------------------------------------------------------------------------------------------
// Live KEY DNA AMOUNT

TEST_CASE ("Live AMOUNT: a sounding note follows AMOUNT smoothly, within ~30 ms, and lands exactly", "[integration][phase2][keydna]")
{
    Rig rig (plainGranular());
    auto dna = unified::expressiveKeyDna();
    rig.engine.setLayerKeyDna (0, dna);
    rig.engine.noteOn (64, 100);
    rig.run (0.2);
    const auto* voice = rig.voiceFor (64);
    REQUIRE (voice != nullptr);
    CHECK (voice->keyDnaAmountNow() == 1.0);
    rig.engine.setLayerKeyDnaAmount (0, 0.0);
    rig.run (256.0 / 48000.0);   // one block
    const double afterOne = voice->keyDnaAmountNow();
    CHECK (afterOne < 1.0);
    CHECK (afterOne > 0.6);   // not a jump
    double previous = afterOne;
    bool monotonic = true;
    rig.run (0.1, [&] {
        monotonic = monotonic && voice->keyDnaAmountNow() <= previous;
        previous = voice->keyDnaAmountNow();
    });
    CHECK (monotonic);
    CHECK (voice->keyDnaAmountNow() < 0.05);
    rig.run (0.5);
    CHECK (voice->keyDnaAmountNow() == 0.0);   // snapped: exactly no KEY DNA
    // The key's identity is the profile's, untouched by AMOUNT.
    const auto before = rig.engine.keyDnaProfile (0)->at (64);
    rig.engine.setLayerKeyDnaAmount (0, 0.7);
    rig.run (0.5);
    CHECK (rig.engine.keyDnaProfile (0)->at (64).position == before.position);
    CHECK (voice->keyDnaAmountNow() == 0.7);
}

TEST_CASE ("Live AMOUNT: automation is deterministic, AMOUNT 0 throughout is no KEY DNA, and a step does not click", "[integration][phase2][keydna]")
{
    const auto& ramp = scene (phase2(), "p2-amount-automation-held-chord");
    const auto a = unified::digest (unified::render (ramp).audio).hash;
    CHECK (unified::digest (unified::render (ramp).audio).hash == a);
    CHECK (unified::digest (unified::render (ramp, 64).audio).hash == unified::digest (unified::render (ramp, 64).audio).hash);

    const auto zero = renderWith (ramp, [] (RenderConfig& c) { c.keyDnaAmountAutomation[0] = { { 0.0, 0.0 } }; });
    const auto off = renderWith (ramp, [] (RenderConfig& c) { c.keyDna[0] = {}; c.keyDnaAmountAutomation[0].clear(); });
    CHECK (unified::digest (zero).hash == unified::digest (off).hash);
    const auto full = renderWith (ramp, [] (RenderConfig& c) { c.keyDna[0].amount = 1.0; c.keyDnaAmountAutomation[0].clear(); });
    CHECK (unified::digest (unified::render (ramp).audio).hash != unified::digest (full).hash);

    // The step (0 -> 100 % at 1.5 s): the largest sample step around it is no larger than what
    // the chord does anyway with AMOUNT held at 0 or at 100 %.
    const auto stepped = unified::render (scene (phase2(), "p2-amount-step-held-chord")).audio;
    const auto from = static_cast<std::size_t> (1.45 * 48000.0), to = static_cast<std::size_t> (1.75 * 48000.0);
    const double reference = std::max (maxStep (zero, from, to), maxStep (full, from, to));
    INFO ("largest step: " << maxStep (stepped, from, to) << ", held at 0 or 1: " << reference);
    CHECK (maxStep (stepped, from, to) <= 1.25 * reference);
}

TEST_CASE ("Live AMOUNT: after AMOUNT moves and returns, a new note on the key is the same note", "[integration][phase2][keydna]")
{
    // A note, AMOUNT dipping while it sounds, then (after it has gone) the key again: the second
    // note is bit-identical to the same take with AMOUNT never touched.
    const auto& ramp = scene (phase2(), "p2-amount-automation-held-chord");
    auto take = ramp;
    take.midi = {};
    take.midi.events.push_back ({ 0.05, MidiEvent::Type::noteOn, 60, 100, 1 });
    take.midi.events.push_back ({ 0.6, MidiEvent::Type::noteOff, 60, 0, 1 });
    take.midi.events.push_back ({ 2.5, MidiEvent::Type::noteOn, 60, 100, 1 });
    take.midi.events.push_back ({ 3.0, MidiEvent::Type::noteOff, 60, 0, 1 });
    take.midi.sort();
    auto steady = [] (RenderConfig& c) {
        c.engineSettings.macros.life = 0.0;
        c.engineSettings.macros.space = 0.0;
        c.keyDna[0].amount = 1.0;
        c.keyDnaAmountAutomation[0].clear();
    };
    const auto still = renderWith (take, steady);
    const auto moved = renderWith (take, [&] (RenderConfig& c) {
        steady (c);
        c.keyDnaAmountAutomation[0] = { { 0.2, 1.0 }, { 0.3, 0.3 }, { 0.45, 1.0 } };
    });
    REQUIRE (still.channels[0].size() == moved.channels[0].size());
    const auto second = static_cast<std::size_t> (2.4 * 48000.0);
    bool same = true, firstDiffers = false;
    for (std::size_t i = 0; i < still.channels[0].size(); ++i)
        for (int ch = 0; ch < 2; ++ch)
        {
            const bool equal = still.channels[static_cast<std::size_t> (ch)][i] == moved.channels[static_cast<std::size_t> (ch)][i];
            if (i >= second)
                same = same && equal;
            else
                firstDiffers = firstDiffers || ! equal;
        }
    CHECK (firstDiffers);   // the dip was heard on the first note
    CHECK (same);           // and the key's identity came back unchanged
}

TEST_CASE ("Live AMOUNT: the keyDnaAmount modulation destination works, and is not in the plugin's frozen list", "[integration][phase2][keydna]")
{
    const auto& lfo = scene (phase2(), "p2-amount-lfo-held-chord");
    const auto routed = unified::digest (unified::render (lfo).audio).hash;
    const auto disabled = unified::digest (renderWith (lfo, [] (RenderConfig& c) { c.modulation.routes[0].enabled = false; })).hash;
    const auto none = unified::digest (renderWith (lfo, [] (RenderConfig& c) { c.modulation.routes[0] = {}; })).hash;
    CHECK (disabled == none);
    CHECK (routed != none);
    // The plugin's mod.routeN.dest choice keeps its 75 entries (automation ranges unchanged).
    CHECK (mod::hostDestCount == 75);
    CHECK (static_cast<int> (mod::Dest::keyDnaAmountA) == mod::hostDestCount);
    CHECK (std::string (mod::destInfo (mod::Dest::keyDnaAmountB).name) == "KEY DNA B AMOUNT");
}

TEST_CASE ("Live AMOUNT and published profiles: the audio thread allocates and computes nothing", "[integration][phase2][keydna]")
{
    Rig rig (plainGranular (3), 3);
    std::array<keydna::Table, 4> tables;
    tables[0].compute (unified::expressiveKeyDna());
    tables[1].compute (unified::organicKeyDna());
    tables[2].compute (unified::scatterPanKeyDna());
    tables[3].compute (unified::expressiveKeyDna());
    rig.engine.setLayerKeyDnaProfile (0, &tables[0]);
    rig.engine.noteOn (60, 100);
    rig.run (0.1);
    int allocations = 0;
    {
        test::AllocationScope scope;
        for (int round = 0; round < 6; ++round)
        {
            for (int l = 0; l < 3; ++l)
            {
                rig.engine.setLayerKeyDnaProfile (l, &tables[static_cast<std::size_t> ((round + l) % 4)]);
                rig.engine.setLayerKeyDnaAmount (l, 0.2 * round);
            }
            rig.engine.noteOn (48 + 5 * round, 90);
            rig.run (0.05);
        }
        rig.engine.allNotesOff();
        rig.run (0.2);
        allocations = scope.count();
    }
    CHECK (allocations == 0);
}

// ---------------------------------------------------------------------------------------------
// The grain budget

namespace
{
    const unified::Scene& budgetScene (const char* name)
    {
        static const auto scenes = unified::budgetScenes();
        return scene (scenes, name);
    }
}

TEST_CASE ("Grain budget: unlimited is the original, and a budget never reached changes nothing", "[integration][phase2][budget]")
{
    const auto& unlimited = budgetScene ("budget-stress-unlimited");
    const auto original = unified::digest (unified::render (scene (baseline(), "stress-abc-granular-chord8")).audio).hash;
    CHECK (unified::digest (unified::render (unlimited).audio).hash == original);
    // The stress scene asks for about 300 grains: a budget of 2000 is never reached.
    auto roomy = unlimited;
    roomy.config.engineSettings.grainBudget = 2000;
    CHECK (unified::digest (unified::render (roomy).audio).hash == original);
}

TEST_CASE ("Grain budget: deterministic, bounded, and the level follows", "[integration][phase2][budget]")
{
    for (const char* name : { "budget-extreme-128", "budget-extreme-64", "budget-stress-128" })
    {
        DYNAMIC_SECTION (name)
        {
            const auto& s = budgetScene (name);
            const int budget = s.config.engineSettings.grainBudget;
            const auto out = unified::render (s).audio;
            CHECK (unified::digest (unified::render (s).audio).hash == unified::digest (out).hash);
            CHECK (unified::digest (unified::render (s, 64).audio).hash == unified::digest (unified::render (s, 64).audio).hash);

            // Bounded: count the engine's grains block by block once the chord is under way.
            auto config = s.config;
            InstrumentEngine engine;
            engine.prepare (config.sampleRate, config.blockSize, config.engineSettings);
            const auto models = unified::modelsFor (s);
            for (int l = 0; l < 3; ++l)
                engine.setModel (models[static_cast<std::size_t> (l)], l);
            for (const auto& e : s.midi.events)
                if (e.type == MidiEvent::Type::noteOn && e.timeSeconds < 0.1)
                    engine.noteOn (e.note, e.value);
            std::vector<float> l (static_cast<std::size_t> (config.blockSize)), r (l.size());
            int most = 0, streams = 0;
            for (int b = 0; b < 300; ++b)
            {
                float* ch[2] = { l.data(), r.data() };
                engine.render (ch, 2, config.blockSize);
                if (b > 4)
                    most = std::max (most, engine.liveGrainCount());
            }
            for (int i = 0; i < InstrumentEngine::voiceSlots(); ++i)
                streams += engine.voiceAt (i).grainStreams();
            INFO ("budget " << budget << ": at most " << most << " grains, " << streams << " streams, share " << engine.grainShare());
            CHECK (most <= std::max (budget, streams));
            CHECK (most >= std::min (budget, streams * 24) * 3 / 4);   // the budget is used, not wasted

            // The level stays near the unlimited render's (each stream normalised for what it plays).
            const auto& material = std::string (name).find ("extreme") != std::string::npos ? budgetScene ("budget-extreme-unlimited")
                                                                                            : budgetScene ("budget-stress-unlimited");
            const auto ref = unified::digest (unified::render (material).audio);
            const auto d = unified::digest (out);
            INFO ("rms " << d.rmsDb << " dB against unlimited " << ref.rmsDb << " dB");
            CHECK (std::abs (d.rmsDb - ref.rmsDb) < 2.0);
            CHECK (d.hash != ref.hash);
        }
    }
}

TEST_CASE ("Grain budget: fair - light notes keep every grain, dense ones share alike", "[integration][phase2][budget]")
{
    // A: dense (DENS 40 x SIZE 500 ms = 20 grains per note), B: light (DENS 10 x 100 ms = 1).
    auto settings = plainGranular (2, { 0.4, 0.5, 40.0, 0.0, 0.4 });
    settings.granular[1] = { 0.6, 0.1, 10.0, 0.0, 0.4 };
    auto trace = [&] (int budget, std::vector<int>* perVoice) {
        auto s = settings;
        s.grainBudget = budget;
        Rig rig (s, 2);
        std::vector<float> light;
        for (int key : { 48, 55, 60, 64, 67, 72 })
            rig.engine.noteOn (key, 100);
        rig.run (1.2, [&] { const auto now = rig.grainsNow (1); light.insert (light.end(), now.begin(), now.end()); });
        if (perVoice != nullptr)
            for (int i = 0; i < InstrumentEngine::voiceSlots(); ++i)
                if (const auto& v = rig.engine.voiceAt (i); v.isActive() && v.layerIndex() == 0)
                    perVoice->push_back (v.grainCount());
        return light;
    };
    std::vector<int> dense;
    const auto unlimited = trace (0, nullptr);
    const auto budgeted = trace (60, &dense);
    CHECK (budgeted == unlimited);   // B's grains: exactly as without a budget
    REQUIRE (dense.size() == 6);
    const auto [lo, hi] = std::minmax_element (dense.begin(), dense.end());
    INFO ("A's notes play " << *lo << " to " << *hi << " grains");
    CHECK (*hi <= 9);         // (60 - 6 light streams' 2) / 6 = 8 each, at most
    CHECK (*hi - *lo <= 2);   // alike
}

TEST_CASE ("Grain budget: when the share drops, the level glides instead of jumping", "[integration][phase2][budget]")
{
    // One dense note alone, then seven more join at 1 s and its share drops from 24 to 8.
    auto settings = plainGranular (1, { 0.4, 0.5, 40.0, 0.0, 0.4 });
    auto envelope = [&] (int budget) {
        auto s = settings;
        s.grainBudget = budget;
        Rig rig (s, 1, 128);
        std::vector<double> env;   // 10 ms RMS
        rig.engine.noteOn (48, 100);
        double acc = 0.0;
        int n = 0, block = 0;
        rig.run (2.0, [&] {
            if (++block == static_cast<int> (1.0 * 48000.0 / 128))
                for (int key : { 52, 55, 59, 62, 65, 69, 72 })
                    rig.engine.noteOn (key, 100);
            for (int i = 0; i < 128; ++i)
                acc += rig.l[static_cast<std::size_t> (i)] * rig.l[static_cast<std::size_t> (i)];
            n += 128;
            if (n >= 480)
            {
                env.push_back (std::sqrt (acc / n));
                acc = 0.0;
                n = 0;
            }
        });
        return env;
    };
    const auto free = envelope (0), capped = envelope (80);
    double worstJump = 0.0, worstFree = 0.0;
    for (std::size_t i = 95; i + 1 < std::min (free.size(), capped.size()) && i < 140; ++i)
    {
        worstJump = std::max (worstJump, std::abs (20.0 * std::log10 (capped[i + 1] / capped[i])));
        worstFree = std::max (worstFree, std::abs (20.0 * std::log10 (free[i + 1] / free[i])));
    }
    INFO ("largest 10 ms level change around the entry: budget " << worstJump << " dB, unlimited " << worstFree << " dB");
    CHECK (worstJump <= worstFree + 1.0);
}

TEST_CASE ("Grain budget: the audio thread allocates nothing", "[integration][phase2][budget]")
{
    auto s = plainGranular (3, { 0.4, 0.4, 40.0, 0.0, 0.4 });
    s.grainBudget = 64;
    Rig rig (s, 3);
    rig.engine.noteOn (60, 100);
    rig.run (0.1);
    int allocations = 0;
    {
        test::AllocationScope scope;
        for (int key : { 48, 52, 55, 59, 62, 67, 71, 74 })
            rig.engine.noteOn (key, 90);
        rig.run (0.3);
        rig.engine.setGrainBudget (128);
        rig.run (0.1);
        rig.engine.allNotesOff();
        rig.run (0.3);
        allocations = scope.count();
    }
    CHECK (allocations == 0);
}
