// KEY DNA inside OSP's own granulator (Phase 1): off or neutral it changes nothing, it acts
// only on Granular layers, each key keeps its identity, polyphonic voices and A/B/C layers keep
// their own, a sounding note keeps its profile, modulation still works on top, nothing is
// applied twice, and the audio thread does not allocate.

#include "engine/InstrumentEngine.h"
#include "engine/KeyDna.h"
#include "research/RenderConfig.h"
#include "research/UnifiedScenes.h"
#include "support/AllocationCounter.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>

using namespace osp;
using namespace osp::research;
using Catch::Approx;

namespace
{
    const unified::Scene& baseline (const char* name)
    {
        static const auto scenes = unified::baselineScenes();
        const auto* s = unified::findScene (scenes, name);
        REQUIRE (s != nullptr);
        return *s;
    }

    std::string hashWith (const unified::Scene& scene, const std::array<keydna::Settings, 3>& dna, int block = 0)
    {
        auto config = scene.config;
        config.keyDna = dna;
        if (block > 0)
            config.blockSize = block;
        return unified::digest (renderLayers (unified::modelsFor (scene), scene.midi, config).audio).hash;
    }

    keydna::Settings expressive (std::uint64_t seed = 7)
    {
        keydna::Settings s;
        s.enabled = true;
        s.seed = seed;
        s.individuality = 0.6;
        for (auto& p : s.params)
            p.variation = 0.5;
        return s;
    }

    /** Only POS moves (by the key's individuality), so a voice's grains sit around base + offset. */
    keydna::Settings positionOnly (double variation = 0.8, std::uint64_t seed = 21)
    {
        keydna::Settings s;
        s.enabled = true;
        s.seed = seed;
        s.individuality = 1.0;
        s[keydna::Param::position].variation = variation;
        return s;
    }

    /** One Granular layer on the vowel, no LIFE, no post stages, SPREAD 0: grains read at POS. */
    EngineSettings plainGranular (int layers = 1)
    {
        EngineSettings s;
        s.macros.life = 0.0;
        s.macros.motion = 0.0;
        s.macros.space = 0.0;
        s.macros.reimagined = 0.0;
        for (int l = 0; l < layers; ++l)
        {
            s.sourceMode[static_cast<std::size_t> (l)] = SourceMode::granular;
            s.granular[static_cast<std::size_t> (l)] = { 0.5, 0.06, 20.0, 0.0, 0.0 };
        }
        return s;
    }

    struct Rig
    {
        InstrumentEngine engine;
        int block;
        std::vector<float> l, r;
        Rig (const EngineSettings& s, int layers = 1, int blockSize = 256) : block (blockSize)
        {
            engine.prepare (48000.0, block, s);
            const unified::Scene& scene = baseline ("layers-abc-granular");
            const auto models = unified::modelsFor (scene);
            for (int i = 0; i < layers; ++i)
                engine.setModel (models[static_cast<std::size_t> (i)], i);
            l.resize (static_cast<std::size_t> (block));
            r.resize (static_cast<std::size_t> (block));
        }
        /** Renders `seconds`; returns the mean read position of layer `layer`'s grains (0..1) and how many were seen. */
        std::pair<double, int> run (double seconds, int layer = 0, std::vector<float>* positions = nullptr)
        {
            double sum = 0.0;
            int seen = 0;
            const int blocks = static_cast<int> (seconds * 48000.0 / block);
            for (int b = 0; b < blocks; ++b)
            {
                float* ch[2] = { l.data(), r.data() };
                engine.render (ch, 2, block);
                const auto& snap = engine.grainSnapshot (layer);
                const int n = snap.count.load();
                for (int i = 0; i < n; ++i)
                {
                    const float p = snap.position[static_cast<std::size_t> (i)].load();
                    sum += p;
                    ++seen;
                    if (positions != nullptr)
                        positions->push_back (p);
                }
            }
            return { seen > 0 ? sum / seen : -1.0, seen };
        }
    };
}

TEST_CASE ("KEY DNA off, neutral or at AMOUNT 0 renders bit-identically to no KEY DNA", "[integration][keydna]")
{
    keydna::Settings neutralOn;
    neutralOn.enabled = true;   // on, nothing to move
    auto zeroAmount = expressive();
    zeroAmount.amount = 0.0;
    auto switchedOff = expressive();
    switchedOff.enabled = false;
    for (const char* name : { "granular-chord4", "granular-repeated", "layers-abc-granular", "granular-modulated", "patch-granular-textured",
                              "granular-mono-glide" })
    {
        INFO (name);
        const auto& scene = baseline (name);
        const auto reference = hashWith (scene, {});
        CHECK (hashWith (scene, { neutralOn, neutralOn, neutralOn }) == reference);
        CHECK (hashWith (scene, { zeroAmount, zeroAmount, zeroAmount }) == reference);
        CHECK (hashWith (scene, { switchedOff, switchedOff, switchedOff }) == reference);
    }
}

TEST_CASE ("KEY DNA acts on Granular layers only: One Shot plays exactly as before", "[integration][keydna]")
{
    const auto dna = expressive();
    for (const char* name : { "oneshot-chord4", "oneshot-scale-pluck" })
    {
        INFO (name);
        const auto& scene = baseline (name);
        CHECK (hashWith (scene, { dna, dna, dna }) == hashWith (scene, {}));
    }
    // A, B Granular and C One Shot: C's part is untouched, A and B change.
    const auto& mixed = baseline ("layers-abc-mixed");
    CHECK (hashWith (mixed, { dna, dna, dna }) != hashWith (mixed, {}));
}

TEST_CASE ("KEY DNA changes Granular playback, deterministically, at any block size", "[integration][keydna]")
{
    const auto dna = expressive();
    const auto& scene = baseline ("granular-chord4");
    const auto off = hashWith (scene, {});
    const auto on = hashWith (scene, { dna, {}, {} });
    CHECK (on != off);
    CHECK (hashWith (scene, { dna, {}, {} }) == on);
    for (int block : { 64, 1000 })
        CHECK (hashWith (scene, { dna, {}, {} }, block) == hashWith (scene, { dna, {}, {} }, block));

    auto config = scene.config;
    config.keyDna[0] = dna;
    const auto d = unified::digest (renderLayers (unified::modelsFor (scene), scene.midi, config).audio);
    CHECK (std::isfinite (d.rmsDb));
    CHECK (d.peakDb < 0.0);
    CHECK (d.rmsDb > -60.0);
}

TEST_CASE ("KEY DNA: a key keeps its identity every time it is played; other keys have their own", "[integration][keydna]")
{
    const auto dna = positionOnly();
    keydna::Table table;
    table.compute (dna);
    for (int block : { 64, 256, 512 })
    {
        INFO ("block " << block);
        Rig rig (plainGranular(), 1, block);
        rig.engine.setLayerKeyDna (0, dna);
        auto play = [&rig] (int key) {
            rig.engine.noteOn (key, 100);
            const auto mean = rig.run (0.4);
            rig.engine.noteOff (key);
            rig.run (0.3);   // let the grains end
            return mean;
        };
        std::vector<double> measured;
        for (int key : { 36, 48, 60, 72, 84 })
        {
            const double expected = std::clamp (0.5 + table.at (key).position, 0.0, 1.0);
            const auto first = play (key);
            const auto again = play (key);
            INFO ("key " << key << ": expected POS " << expected << ", measured " << first.first << " then " << again.first);
            REQUIRE (first.second > 10);
            // The grains sit at this key's own POS (base + its offset, once: not twice) ...
            CHECK (first.first == Approx (expected).margin (0.03));
            // ... and there again when the key is struck again (the grains themselves differ).
            CHECK (again.first == Approx (first.first).margin (0.02));
            measured.push_back (first.first);
        }
        // Different keys, different places: the five keys spread over a wide part of the recording.
        const auto [lo, hi] = std::minmax_element (measured.begin(), measured.end());
        CHECK (*hi - *lo > 0.15);
    }
}

TEST_CASE ("KEY DNA: polyphonic voices keep their own keys' values", "[integration][keydna]")
{
    const auto dna = positionOnly();
    keydna::Table table;
    table.compute (dna);
    const int low = 48, high = 72;
    const double a = std::clamp (0.5 + table.at (low).position, 0.0, 1.0), b = std::clamp (0.5 + table.at (high).position, 0.0, 1.0);
    REQUIRE (std::abs (a - b) > 0.1);
    Rig rig (plainGranular());
    rig.engine.setLayerKeyDna (0, dna);
    rig.engine.noteOn (low, 100);
    rig.engine.noteOn (high, 100);
    std::vector<float> positions;
    rig.run (0.6, 0, &positions);
    int nearA = 0, nearB = 0, elsewhere = 0;
    for (float p : positions)
    {
        if (std::abs (p - a) < 0.04)
            ++nearA;
        else if (std::abs (p - b) < 0.04)
            ++nearB;
        else
            ++elsewhere;
    }
    INFO ("near " << a << ": " << nearA << ", near " << b << ": " << nearB << ", elsewhere: " << elsewhere);
    CHECK (nearA > 20);
    CHECK (nearB > 20);
    CHECK (elsewhere < (nearA + nearB) / 20);
}

TEST_CASE ("KEY DNA: A, B and C each have their own profile", "[integration][keydna]")
{
    // B's profile changes B's grains and nothing of A's or C's.
    auto layerGrains = [] (const std::array<keydna::Settings, 3>& dna, int layer) {
        Rig rig (plainGranular (3), 3);
        for (int l = 0; l < 3; ++l)
            rig.engine.setLayerKeyDna (l, dna[static_cast<std::size_t> (l)]);
        rig.engine.noteOn (55, 100);
        rig.engine.noteOn (67, 100);
        std::vector<float> positions;
        rig.run (0.5, layer, &positions);
        return positions;
    };
    const std::array<keydna::Settings, 3> none {}, onlyB { keydna::Settings {}, positionOnly(), keydna::Settings {} };
    CHECK (layerGrains (none, 0) == layerGrains (onlyB, 0));
    CHECK (layerGrains (none, 2) == layerGrains (onlyB, 2));
    CHECK (layerGrains (none, 1) != layerGrains (onlyB, 1));
}

TEST_CASE ("KEY DNA: a sounding note keeps its profile when the settings change", "[integration][keydna]")
{
    auto render = [] (bool change) {
        Rig rig (plainGranular());
        rig.engine.setLayerKeyDna (0, positionOnly (0.8, 21));
        rig.engine.noteOn (60, 100);
        std::vector<float> out;
        for (int b = 0; b < 200; ++b)
        {
            if (change && b == 60)
                rig.engine.setLayerKeyDna (0, positionOnly (0.8, 99));   // a new profile for notes started from now
            float* ch[2] = { rig.l.data(), rig.r.data() };
            rig.engine.render (ch, 2, rig.block);
            out.insert (out.end(), rig.l.begin(), rig.l.end());
        }
        return out;
    };
    CHECK (render (true) == render (false));
}

TEST_CASE ("KEY DNA: modulation still works on top, and a route at zero is still no modulation", "[integration][keydna]")
{
    const auto& scene = baseline ("granular-chord4");
    const auto dna = expressive();
    auto withRoute = [&] (double depth, bool enabled) {
        auto config = scene.config;
        config.keyDna[0] = dna;
        config.modulation.lfo[0].rateHz = 2.0;
        config.modulation.routes[0].source = mod::Source::lfo1;
        config.modulation.routes[0].dest = mod::Dest::grainPositionA;
        config.modulation.routes[0].depth = depth;
        config.modulation.routes[0].enabled = enabled;
        return unified::digest (renderLayers (unified::modelsFor (scene), scene.midi, config).audio).hash;
    };
    const auto dnaOnly = hashWith (scene, { dna, {}, {} });
    CHECK (withRoute (0.4, false) == dnaOnly);   // a disabled route: exactly no modulation
    CHECK (withRoute (0.4, true) != dnaOnly);    // a live route moves POS around the key's own value
}

TEST_CASE ("KEY DNA: the audio thread allocates nothing, with profiles changing and notes starting", "[integration][keydna]")
{
    Rig rig (plainGranular (3), 3);
    rig.engine.setLayerKeyDna (0, expressive (1));
    // Warm up (first-use paths), then count.
    rig.engine.noteOn (60, 100);
    rig.run (0.1);
    int allocations = 0;
    {
        test::AllocationScope scope;
        rig.engine.setLayerKeyDna (0, expressive (2));
        rig.engine.setLayerKeyDna (1, positionOnly());
        rig.engine.setLayerKeyDna (2, expressive (3));
        for (int key : { 48, 52, 55, 59, 62, 67, 71, 74 })
            rig.engine.noteOn (key, 90);
        rig.run (0.3);
        for (int key : { 48, 52, 55, 59, 62, 67, 71, 74 })
            rig.engine.noteOff (key);
        rig.run (0.2);
        allocations = scope.count();
    }
    CHECK (allocations == 0);
}

TEST_CASE ("KEY DNA at its extremes stays finite and bounded on every layer", "[integration][keydna]")
{
    keydna::Settings wild;
    wild.enabled = true;
    wild.individuality = 1.0;
    wild.seed = 4;
    mod::Curve c;
    c.count = 2;
    c.points[0] = { 0.0f, 1.0f, 0.0f };
    c.points[1] = { 1.0f, 0.0f, 0.0f };
    for (auto& p : wild.params)
    {
        p.curve = keydna::curveFrom (c);
        p.curveDepth = 1.0;
        p.variation = 1.0;
    }
    const auto& scene = baseline ("stress-abc-granular-chord8");
    auto config = scene.config;
    config.keyDna = { wild, wild, wild };
    const auto out = renderLayers (unified::modelsFor (scene), scene.midi, config);
    bool finite = true;
    for (const auto& ch : out.audio.channels)
        for (float x : ch)
            finite = finite && std::isfinite (x);
    CHECK (finite);
    const auto d = unified::digest (out.audio);
    CHECK (d.peakDb < 6.0);
    CHECK (d.rmsDb > -60.0);
}

TEST_CASE ("KEY DNA: the research configs carry exactly the demonstration profiles", "[integration][keydna]")
{
    // research/configs/keydna-*.json render the same patches on any recording (--source).
    auto load = [] (const char* name) {
        std::string error;
        auto config = loadRenderConfig (std::filesystem::path (OSP_SOURCE_DIR) / "research" / "configs" / name, error);
        INFO (error);
        REQUIRE (config.has_value());
        return *config;
    };
    const auto a = load ("keydna-a-static.json");
    const auto b = load ("keydna-b-organic.json");
    const auto c = load ("keydna-c-expressive.json");
    CHECK (a.keyDna[0] == keydna::Settings {});
    CHECK (b.keyDna[0] == unified::organicKeyDna());
    CHECK (c.keyDna[0] == unified::expressiveKeyDna());
    for (const auto* config : { &a, &b, &c })
    {
        CHECK (config->engineSettings.sourceMode[0] == SourceMode::granular);
        CHECK (config->engineSettings.granular[0].sizeSeconds == Approx (0.12));
        CHECK (config->engineSettings.macros.life == Approx (0.3));
    }
}
