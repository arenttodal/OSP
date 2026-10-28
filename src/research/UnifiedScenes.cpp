#include "research/UnifiedScenes.h"

#include "analysis/Analyzer.h"
#include "audio/utility/TestSignals.h"
#include "core/PitchMath.h"
#include "core/PlatformTag.h"
#include "engine/InstrumentBuilder.h"
#include "engine/InstrumentEngine.h"
#include "io/JsonUtil.h"
#include "research/AtlasFixtures.h"

#if ! defined(_WIN32)
 #include <sys/resource.h>
#endif

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <functional>
#include <map>
#include <memory>
#include <mutex>

namespace osp::research::unified
{

const char* sourceName (SourceId id) noexcept
{
    switch (id)
    {
        case SourceId::vowelA3: return "vowel-A3";
        case SourceId::pluckE2: return "pluck-E2";
        case SourceId::vowelE4: return "vowel-E4";
        case SourceId::vowelA3Mono: return "vowel-A3-mono";
    }
    return "";
}

AudioData sourceAudio (SourceId id)
{
    // The same stand-ins as the plugin's reference scenes (tests/plugin: a-vowel, b-pluck, c-vowel).
    switch (id)
    {
        case SourceId::vowelA3: return testsignals::vowel (midiToHz (57), 2.5, 48000.0, 3);
        case SourceId::pluckE2: return testsignals::pluck (midiToHz (40), 2.5, 48000.0, 8, 0.7, 1);
        case SourceId::vowelE4: return testsignals::vowel (midiToHz (64), 2.5, 48000.0, 7);
        case SourceId::vowelA3Mono:
        {
            auto mono = sourceAudio (SourceId::vowelA3);
            mono.channels.resize (1);
            return mono;
        }
    }
    return {};
}

namespace
{
    // MIDI: every scene's notes, written out (sample-accurate at 48 kHz).
    struct Notes
    {
        MidiSequence seq;
        Notes& note (double at, double length, int key, int velocity = 100)
        {
            seq.events.push_back ({ at, MidiEvent::Type::noteOn, key, velocity, 1 });
            seq.events.push_back ({ at + length, MidiEvent::Type::noteOff, key, 0, 1 });
            return *this;
        }
        Notes& chord (double at, double length, std::initializer_list<int> keys, int velocity = 96)
        {
            int v = velocity;
            for (int k : keys)
                note (at, length, k, v--);
            return *this;
        }
        MidiSequence done (const char* name)
        {
            seq.name = name;
            seq.sort();
            return seq;
        }
    };

    MidiSequence singleNote() { return Notes().note (0.05, 2.4, 57, 100).done ("single-a3"); }
    MidiSequence repeatedNotes()
    {
        Notes n;
        for (int i = 0; i < 6; ++i)
            n.note (0.05 + 0.4 * i, 0.32, 57, 92 + 4 * (i % 3));
        return n.done ("repeated-a3x6");
    }
    MidiSequence scale()
    {
        Notes n;
        const int keys[] = { 48, 50, 52, 53, 55, 57, 59, 60 };
        for (int i = 0; i < 8; ++i)
            n.note (0.05 + 0.3 * i, 0.28, keys[i], 84 + 4 * (i % 4));
        return n.done ("scale-c3-c4");
    }
    MidiSequence chord4() { return Notes().chord (0.05, 2.0, { 60, 64, 67, 71 }).done ("chord4-cmaj7"); }
    MidiSequence chord8() { return Notes().chord (0.05, 2.0, { 36, 43, 48, 52, 55, 60, 64, 71 }, 100).done ("chord8-c"); }
    MidiSequence legatoLine()
    {
        // Overlapping notes: in mono each new key glides the sounding note (no restart).
        Notes n;
        const int keys[] = { 57, 60, 64, 62, 57 };
        for (int i = 0; i < 5; ++i)
            n.note (0.05 + 0.35 * i, 0.45, keys[i], 96);
        return n.done ("legato-line");
    }

    RenderConfig baseConfig()
    {
        RenderConfig c;
        c.engine = EngineId::instrument;
        c.sampleRate = 48000.0;
        c.blockSize = 256;
        c.maxTailSeconds = 4.0;
        c.engineSettings.seed = 1;
        c.modulation.seed = 1;
        return c;
    }
    /** A plain instrument: no LIFE, no post effects (the granular stream itself). */
    RenderConfig dryConfig()
    {
        auto c = baseConfig();
        c.engineSettings.macros.life = 0.0;
        c.engineSettings.macros.motion = 0.0;
        c.engineSettings.macros.space = 0.0;
        c.engineSettings.macros.reimagined = 0.0;
        return c;
    }
    void granular (RenderConfig& c, int layer, GranularParams g = {})
    {
        c.engineSettings.sourceMode[static_cast<std::size_t> (layer)] = SourceMode::granular;
        c.engineSettings.granular[static_cast<std::size_t> (layer)] = g;
    }
    mod::Route route (mod::Source s, mod::Dest d, double depth, int target = -1)
    {
        mod::Route r;
        r.source = s;
        r.dest = d;
        r.depth = depth;
        r.target = target;
        return r;
    }

    std::uint64_t fnv (const AudioData& audio)
    {
        std::uint64_t h = 1469598103934665603ull;
        for (const auto& channel : audio.channels)
            for (float x : channel)
            {
                std::uint32_t bits;
                std::memcpy (&bits, &x, sizeof bits);
                for (int b = 0; b < 4; ++b)
                    h = (h ^ ((bits >> (8 * b)) & 0xffu)) * 1099511628211ull;
            }
        return h;
    }
}

std::vector<Scene> baselineScenes()
{
    using L = std::vector<LayerSpec>;
    std::vector<Scene> scenes;
    auto add = [&scenes] (const char* name, const char* description, L layers, RenderConfig config, MidiSequence midi) {
        scenes.push_back ({ name, description, std::move (layers), std::move (config), std::move (midi) });
    };
    const L vowel { { SourceId::vowelA3 } };

    // One Shot (the read-through instrument).
    add ("oneshot-chord4", "One Shot, default instrument, four-note chord", vowel, baseConfig(), chord4());
    add ("oneshot-scale-pluck", "One Shot pluck, default instrument, a scale", L { { SourceId::pluckE2 } }, baseConfig(), scale());

    // Granular, default settings.
    {
        auto c = dryConfig();
        granular (c, 0);
        add ("granular-single", "Granular defaults, one held note, LIFE 0 (the plain grain stream)", vowel, c, singleNote());
    }
    {
        auto c = baseConfig();
        granular (c, 0);
        add ("granular-scale", "Granular defaults, default instrument (LIFE 50 %), a scale", vowel, c, scale());
        add ("granular-repeated", "Granular defaults, the same key six times (LIFE varies each trigger)", vowel, c, repeatedNotes());
        add ("granular-chord4", "Granular defaults, four-note chord", vowel, c, chord4());
        add ("granular-chord8", "Granular defaults, eight-note chord over four octaves", vowel, c, chord8());
    }

    // A/B/C layered.
    {
        auto c = baseConfig();
        granular (c, 0, { 0.3, 0.12, 18.0, 0.0, 0.25 });
        granular (c, 1, { 0.6, 0.08, 25.0, 12.0, 0.4 });
        c.engineSettings.mixX = 0.5;
        c.engineSettings.mixY = 0.35;
        c.engineSettings.layer[2].levelDb = -3.0;
        c.engineSettings.layer[1].pan = -0.4;
        add ("layers-abc-mixed", "A vowel granular, B pluck granular, C vowel One Shot, mix in the triangle's middle, chord",
             L { { SourceId::vowelA3 }, { SourceId::pluckE2 }, { SourceId::vowelE4 } }, c, chord4());
    }
    {
        auto c = baseConfig();
        granular (c, 0, { 0.25, 0.2, 10.0, 0.0, 0.1 });
        granular (c, 1, { 0.5, 0.05, 34.0, -12.0, 0.7 });
        granular (c, 2, { 0.8, 0.3, 8.0, 7.0, 0.3 });
        c.engineSettings.mixX = 0.45;
        c.engineSettings.mixY = 0.4;
        add ("layers-abc-granular", "A, B and C all Granular with different settings, a scale",
             L { { SourceId::vowelA3 }, { SourceId::pluckE2 }, { SourceId::vowelE4 } }, c, scale());
    }
    {
        auto c = baseConfig();
        granular (c, 1, { 0.4, 0.1, 20.0, 0.0, 0.3 });
        c.engineSettings.blend = 0.5;
        add ("layers-ab-oneshot-granular", "A One Shot, B Granular, equal blend, chord", L { { SourceId::vowelA3 }, { SourceId::pluckE2 } }, c,
             chord4());
    }

    // Modulation-heavy granular: every granular destination, poly and global sources, a meta route.
    {
        auto c = baseConfig();
        granular (c, 0, { 0.45, 0.14, 16.0, 0.0, 0.2 });
        auto& m = c.modulation;
        m.lfo[0].shape = mod::LfoShape::triangle;
        m.lfo[0].rateHz = 1.7;
        m.lfo[0].scope = mod::Scope::poly;
        m.lfo[1].shape = mod::LfoShape::sine;
        m.lfo[1].rateHz = 0.4;
        m.env[0].attackSeconds = 0.4;
        m.env[0].decaySeconds = 0.8;
        m.env[0].sustain = 0.3;
        m.routes[0] = route (mod::Source::lfo1, mod::Dest::grainPositionA, 0.35);
        m.routes[1] = route (mod::Source::lfo2, mod::Dest::grainSizeA, 0.5);
        m.routes[2] = route (mod::Source::env1, mod::Dest::grainDensityA, 0.6);
        m.routes[3] = route (mod::Source::vel, mod::Dest::grainSpreadA, 0.3);
        m.routes[4] = route (mod::Source::note, mod::Dest::grainPositionA, -0.3);
        m.routes[5] = route (mod::Source::modWheel, mod::Dest::grainSizeA, -0.4);
        m.routes[6] = route (mod::Source::lfo2, mod::Dest::routeDepth, 0.3, 0);   // meta: LFO 2 moves route 0's depth
        c.modWheel = 0.5;
        add ("granular-modulated", "Granular with LFO 1 (poly), LFO 2, ENV 1, VEL, NOTE, MOD WHEEL routes and a meta route", vowel, c, chord4());
    }

    // Patches with non-default granular settings (what existing sessions hold).
    {
        auto c = baseConfig();
        granular (c, 0, { 0.72, 0.32, 32.0, 7.0, 0.6 });
        c.engineSettings.layer[0].start = 0.1;
        c.engineSettings.layer[0].reverse = true;
        c.engineSettings.layer[0].follow = false;
        c.engineSettings.macros.life = 0.8;
        c.engineSettings.shaping.lifeMode = LifeMode::loose;
        c.engineSettings.reimaginedRouting = ReimaginedRouting::perLayer;
        c.engineSettings.layer[0].reimagined = 0.6;   // KALEIDOSCOPE on grains: doubling stream and scatter
        c.engineSettings.macros.space = 0.4;
        add ("patch-granular-textured", "POS 72, SIZE 320 ms, DENS 32, TUNE +7, SPREAD 60, START 10, REVERSE, FOLLOW off, LIFE 80 loose, "
             "REIMAGINED 60 per layer, SPACE 40", vowel, c, chord4());
    }
    {
        auto c = baseConfig();
        granular (c, 0, { 0.5, 0.03, 40.0, -12.0, 1.0 });
        c.engineSettings.macros.life = 0.9;
        c.engineSettings.shaping.lifeMode = LifeMode::fray;
        c.engineSettings.shaping.lifePitchCents = 12.0;
        add ("patch-granular-glitch", "SIZE 30 ms, DENS 40, TUNE -12, SPREAD 100, LIFE 90 fray with pitch", vowel, c, scale());
    }
    {
        auto c = baseConfig();
        granular (c, 0, { 0.35, 0.18, 12.0, 0.0, 0.15 });
        c.engineSettings.mono = true;
        c.engineSettings.glideSeconds = 0.08;
        add ("granular-mono-glide", "Granular in mono with glide: keys glide the sounding note (voice allocation)", vowel, c, legatoLine());
    }
    // CPU stress: three Granular layers, long dense grains, eight notes (hundreds of grains).
    {
        auto c = baseConfig();
        granular (c, 0, { 0.3, 0.3, 40.0, 0.0, 0.5 });
        granular (c, 1, { 0.55, 0.25, 36.0, 12.0, 0.6 });
        granular (c, 2, { 0.75, 0.35, 32.0, -12.0, 0.4 });
        c.engineSettings.mixX = 0.5;
        c.engineSettings.mixY = 1.0 / 3.0;
        add ("stress-abc-granular-chord8", "CPU stress: A, B, C Granular, SIZE 250-350 ms, DENS 32-40, eight-note chord",
             L { { SourceId::vowelA3 }, { SourceId::pluckE2 }, { SourceId::vowelE4 } }, c, chord8());
    }
    return scenes;
}

namespace
{
    mod::CurveTable line (float fromY, float toY)
    {
        mod::Curve c;
        c.count = 2;
        c.points[0] = { 0.0f, fromY, 0.0f };
        c.points[1] = { 1.0f, toY, 0.0f };
        return keydna::curveFrom (c);
    }

    /** The demonstrations' instrument: one Granular vowel, a little LIFE, a little SPACE. */
    RenderConfig demoConfig()
    {
        auto c = baseConfig();
        granular (c, 0, { 0.45, 0.12, 16.0, 0.0, 0.15 });
        c.engineSettings.macros.life = 0.3;
        c.engineSettings.macros.space = 0.15;
        c.engineSettings.macros.motion = 0.0;
        c.engineSettings.macros.reimagined = 0.0;
        return c;
    }
}

keydna::Settings organicKeyDna()
{
    // Small and smooth: neighbouring keys nearly alike, longer grains towards the bass.
    keydna::Settings s;
    s.enabled = true;
    s.seed = 11;
    s.individuality = 0.15;
    s[keydna::Param::position].variation = 0.12;
    s[keydna::Param::size].curve = line (0.8f, 0.2f);   // low keys longer, high keys shorter
    s[keydna::Param::size].curveDepth = 0.2;
    s[keydna::Param::size].variation = 0.12;
    s[keydna::Param::density].variation = 0.1;
    s[keydna::Param::spread].variation = 0.1;
    return s;
}

keydna::Settings expressiveKeyDna()
{
    // Clearly heard: each key its own place in the recording, long sparse grains low, short
    // dense ones high, and a distinct individuality.
    keydna::Settings s;
    s.enabled = true;
    s.seed = 23;
    s.individuality = 0.6;
    s[keydna::Param::position].variation = 0.7;
    s[keydna::Param::size].curve = line (1.0f, 0.0f);
    s[keydna::Param::size].curveDepth = 0.55;
    s[keydna::Param::size].variation = 0.35;
    s[keydna::Param::density].curve = line (0.0f, 1.0f);
    s[keydna::Param::density].curveDepth = 0.5;
    s[keydna::Param::density].variation = 0.3;
    s[keydna::Param::spread].variation = 0.3;
    return s;
}

std::vector<Scene> keyDnaScenes()
{
    using L = std::vector<LayerSpec>;
    const L vowel { { SourceId::vowelA3 } };
    struct Patch
    {
        const char* name;
        const char* what;
        keydna::Settings dna;
    };
    const Patch patches[] = { { "a-static", "A static: the existing Granulator, KEY DNA off", keydna::Settings {} },
                              { "b-organic", "B subtle organic keyboard: small, smooth per-key differences", organicKeyDna() },
                              { "c-expressive", "C expressive KEY DNA: pitch-dependent POS, SIZE and DENS", expressiveKeyDna() } };
    MidiSequence singles = Notes().note (0.05, 0.9, 43).note (1.1, 0.9, 55).note (2.15, 0.9, 67).note (3.2, 0.9, 79).done ("singles-g2-g5");
    const std::pair<const char*, MidiSequence> takes[] = { { "singles", singles },
                                                           { "repeated", repeatedNotes() },
                                                           { "scale", scale() },
                                                           { "chord4", chord4() },
                                                           { "chord8", chord8() } };
    std::vector<Scene> scenes;
    for (const auto& patch : patches)
        for (const auto& [take, midi] : takes)
        {
            auto c = demoConfig();
            c.keyDna[0] = patch.dna;
            scenes.push_back ({ std::string ("keydna-") + patch.name + "-" + take, std::string (patch.what) + ", " + take, vowel, c, midi });
        }
    return scenes;
}

keydna::Settings scatterPanKeyDna()
{
    // Low keys steady and centred, high keys scattered in pitch and spread across the field,
    // with a little individuality so neighbours are not identical.
    keydna::Settings s;
    s.enabled = true;
    s.seed = 31;
    s.individuality = 0.4;
    s[keydna::Param::scatter].curve = line (0.5f, 1.0f);   // 0 at the bottom, +5 semitones at the top
    s[keydna::Param::scatter].curveDepth = 0.8;
    s[keydna::Param::scatter].variation = 0.1;
    s[keydna::Param::pan].curve = line (0.5f, 1.0f);       // centred low, +-50 % at the top
    s[keydna::Param::pan].curveDepth = 1.0;
    s[keydna::Param::pan].variation = 0.15;
    return s;
}

std::vector<Scene> phase2Scenes()
{
    using L = std::vector<LayerSpec>;
    const L vowel { { SourceId::vowelA3 } };
    std::vector<Scene> scenes;
    auto add = [&scenes, &vowel] (const char* name, const char* description, RenderConfig config, MidiSequence midi) {
        scenes.push_back ({ name, description, vowel, std::move (config), std::move (midi) });
    };
    auto with = [] (auto&& edit) {
        auto c = demoConfig();
        edit (c.engineSettings.granular[0]);
        return c;
    };
    add ("p2-options-neutral-chord4", "SCATTER 0, PAN 0, window HANN set explicitly: the Phase 1 static chord exactly",
         with ([] (GranularParams& g) { g.scatterSemitones = 0.0; g.pan = 0.0; g.window = GrainWindow::hann; }), chord4());
    add ("p2-scatter-3-chord4", "SCATTER +-3 semitones per grain", with ([] (GranularParams& g) { g.scatterSemitones = 3.0; }), chord4());
    add ("p2-scatter-12-single", "SCATTER +-12 semitones, one held note (a cloud)",
         with ([] (GranularParams& g) { g.scatterSemitones = 12.0; }), Notes().note (0.05, 3.0, 57).done ("held-a3"));
    add ("p2-pan-50-chord4", "PAN 50 %: each grain placed within +-50 % of the field", with ([] (GranularParams& g) { g.pan = 0.5; }), chord4());
    add ("p2-pan-100-single", "PAN 100 %, one held note", with ([] (GranularParams& g) { g.pan = 1.0; }),
         Notes().note (0.05, 3.0, 57).done ("held-a3"));
    scenes.push_back ({ "p2-pan-100-mono-single", "PAN 100 % on a mono recording, one held note", L { { SourceId::vowelA3Mono } },
                        with ([] (GranularParams& g) { g.pan = 1.0; }), Notes().note (0.05, 3.0, 57).done ("held-a3") });
    add ("p2-window-soft-chord4", "window SOFT (Tukey: fuller, less pulsing)", with ([] (GranularParams& g) { g.window = GrainWindow::soft; }), chord4());
    add ("p2-window-percussive-chord4", "window PERCUSSIVE (each grain a small attack)",
         with ([] (GranularParams& g) { g.window = GrainWindow::percussive; }), chord4());
    {
        // PERCUSSIVE is most audible with sparse, longer grains.
        auto c = demoConfig();
        c.engineSettings.granular[0] = { 0.45, 0.2, 8.0, 0.0, 0.3 };
        c.engineSettings.granular[0].window = GrainWindow::percussive;
        add ("p2-window-percussive-sparse-scale", "window PERCUSSIVE, SIZE 200 ms, DENS 8: a pulse of small attacks", c, scale());
    }
    {
        auto c = demoConfig();
        c.keyDna[0] = scatterPanKeyDna();
        add ("p2-keydna-scatter-pan-scale", "KEY DNA SCATTER and PAN: low keys steady and centred, high keys scattered and wide", c, scale());
        add ("p2-keydna-scatter-pan-chord8", "KEY DNA SCATTER and PAN on an eight-note chord", c, chord8());
    }
    const auto held = Notes().chord (0.05, 5.0, { 55, 62, 67, 74 }).done ("held-chord-g");
    {
        auto c = demoConfig();
        c.keyDna[0] = expressiveKeyDna();
        c.keyDna[0].amount = 0.0;
        c.keyDnaAmountAutomation[0] = { { 0.5, 0.0 }, { 2.0, 1.0 }, { 3.0, 1.0 }, { 4.5, 0.0 } };
        add ("p2-amount-automation-held-chord", "held chord, expressive KEY DNA, AMOUNT automated 0 -> 100 % -> 0 (live on the sounding notes)", c, held);
        auto step = c;
        step.keyDnaAmountAutomation[0] = { { 1.5, 0.0 }, { 1.5001, 1.0 }, { 3.5, 1.0 }, { 3.5001, 0.0 } };
        add ("p2-amount-step-held-chord", "held chord, AMOUNT stepped 0 -> 100 % -> 0 (the 30 ms smoothing is all that softens it)", step, held);
    }
    {
        auto c = demoConfig();
        c.keyDna[0] = expressiveKeyDna();
        c.keyDna[0].amount = 0.5;
        c.modulation.lfo[0].rateHz = 0.5;
        c.modulation.routes[0] = route (mod::Source::lfo1, mod::Dest::keyDnaAmountA, 0.5);
        add ("p2-amount-lfo-held-chord", "held chord, KEY DNA AMOUNT 50 % swept +-50 % by a 0.5 Hz LFO", c, held);
    }
    for (const auto& b : budgetScenes())
        if (b.name == "budget-stress-unlimited" || b.name == "budget-stress-128")
            scenes.push_back (b);
    return scenes;
}

std::vector<Scene> budgetScenes()
{
    const auto base = baselineScenes();
    const auto* stress = findScene (base, "stress-abc-granular-chord8");
    std::vector<Scene> scenes;
    if (stress == nullptr)
        return scenes;
    // The extreme: every layer at the pool (DENS 40 x SIZE 600 ms) and KALEIDOSCOPE's second
    // stream on every layer: 48 streams asking for 24 grains each.
    auto extreme = *stress;
    for (int l = 0; l < 3; ++l)
    {
        auto& g = extreme.config.engineSettings.granular[static_cast<std::size_t> (l)];
        g.density = 40.0;
        g.sizeSeconds = 0.6;
        extreme.config.engineSettings.layer[static_cast<std::size_t> (l)].reimagined = 0.6;
    }
    extreme.config.engineSettings.reimaginedRouting = ReimaginedRouting::perLayer;
    for (const auto& [material, scene] : { std::pair<const char*, const Scene*> { "stress", stress }, { "extreme", &extreme } })
        for (int budget : { 0, 512, 384, 256, 192, 128, 96, 64 })
        {
            auto s = *scene;
            s.config.engineSettings.grainBudget = budget;
            // Pinned in Phase 2 under its policy (the bounded one came with Phase 3).
            s.config.engineSettings.grainBudgetPolicy = GrainBudgetPolicy::shared;
            s.name = std::string ("budget-") + material + "-" + (budget == 0 ? std::string ("unlimited") : std::to_string (budget));
            s.description = std::string (material == std::string ("stress") ? "the CPU stress scene" : "every layer at the pool, doubled")
                            + (budget == 0 ? ", no budget" : ", grain budget " + std::to_string (budget));
            scenes.push_back (std::move (s));
        }
    return scenes;
}

std::vector<Scene> atlasScenes()
{
    using L = std::vector<LayerSpec>;
    auto atlasLayer = [] (const char* atlas) { return LayerSpec { SourceId::vowelA3, atlas }; };
    std::vector<Scene> scenes;
    auto add = [&scenes] (const char* name, const char* description, L layers, RenderConfig config, MidiSequence midi) {
        scenes.push_back ({ name, description, std::move (layers), std::move (config), std::move (midi) });
    };
    auto granularOn = [] (RenderConfig c, int layer = 0) {
        granular (c, layer);
        return c;
    };
    const L sung { atlasLayer ("sung-four") };
    Notes melodyNotes;
    {
        // Recorded keys (A3, C4, E4, G4) and the keys FILL plays from their neighbours.
        const int keys[] = { 57, 59, 60, 62, 64, 65, 67, 69, 67, 64, 60, 57 };
        for (int i = 0; i < 12; ++i)
            melodyNotes.note (0.05 + 0.32 * i, 0.3, keys[i], 88 + 3 * (i % 4));
    }
    const auto melody = melodyNotes.done ("atlas-melody");
    const auto chordSung = Notes().chord (0.05, 2.0, { 57, 60, 64, 67 }).done ("chord-sung-four");
    add ("atlas-oneshot-melody", "One Shot: a melody over the four recorded notes and the keys FILL lends them", sung, baseConfig(), melody);
    add ("atlas-oneshot-chord", "One Shot: the four recorded notes as a chord", sung, baseConfig(), chordSung);
    add ("atlas-granular-chord", "Granular from the atlas: each key's grains from its own note", sung, granularOn (baseConfig()), chordSung);
    {
        auto c = granularOn (baseConfig());
        c.keyDna[0] = expressiveKeyDna();
        add ("atlas-granular-keydna-scale", "Granular + expressive KEY DNA, a scale across recorded and filled keys", sung, c, scale());
        add ("atlas-granular-keydna-chord8", "Granular + expressive KEY DNA, an eight-note chord (FILL beyond the recorded range is silent)", sung, c, chord8());
    }
    {
        auto c = granularOn (baseConfig(), 1);
        c.engineSettings.granular[1].scatterSemitones = 0.5;
        c.engineSettings.granular[1].pan = 0.6;
        add ("atlas-three-abc-chord", "three atlases: A sung (One Shot), B bowed (Granular, SCATTER, PAN), C stereo-wide (One Shot)",
             L { atlasLayer ("sung-four"), atlasLayer ("bowed-phrase"), atlasLayer ("stereo-wide") }, c,
             Notes().chord (0.05, 2.0, { 55, 59, 62, 67 }).done ("chord-g"));
    }
    {
        auto c = baseConfig();
        c.engineSettings.polyphony = 24;
        Notes n;
        for (int k = 0; k < 24; ++k)
            n.note (0.05 + 0.02 * k, 2.5, 46 + k, 70 + k);
        add ("atlas-polyphony-24", "24 notes at once from four recorded notes (One Shot)", sung, c, n.done ("cluster-24"));
        add ("atlas-polyphony-24-granular", "24 notes at once, Granular", sung, granularOn (c), n.done ("cluster-24"));
    }
    {
        Notes n;
        for (int i = 0; i < 16; ++i)
            n.note (0.05 + 0.12 * i, 0.1, 62, 80 + 2 * (i % 5));
        add ("atlas-rapid-repeats", "D4 sixteen times, fast: the three recorded takes in round robin, never the same twice in a row",
             L { atlasLayer ("repeated-note") }, baseConfig(), n.done ("repeats-d4x16"));
    }
    {
        Notes n;
        for (int i = 0; i < 8; ++i)
            n.note (0.05 + 0.35 * i, 0.3, 60, i % 2 == 0 ? 40 : 120);
        add ("atlas-velocity-layers", "C4 soft and loud in turn: the soft and loud recordings (velocity layers)", L { atlasLayer ("velocity-layers") }, baseConfig(),
             n.done ("velocity-c4"));
    }
    {
        const auto tune = Notes().note (0.05, 0.6, 60).note (0.75, 0.6, 64).note (1.45, 0.6, 67).chord (2.2, 1.2, { 60, 64, 67 }).done ("tuning-c-major");
        add ("atlas-out-of-tune-corrected", "notes recorded 35 cents sharp, 40 flat, 12 sharp, played in tune", L { atlasLayer ("out-of-tune") }, baseConfig(), tune);
        add ("atlas-out-of-tune-as-recorded", "the same notes as recorded (Mapping.correctTuning off)", L { atlasLayer ("out-of-tune:as-recorded") },
             baseConfig(), tune);
    }
    {
        Notes n;
        for (int k = 36; k <= 84; k += 2)
            n.note (0.05 + 0.15 * (k - 36) / 2, 0.14, k, 96);
        const auto sweep = n.done ("sweep-c2-c6");
        add ("atlas-fill-12", "only C3, C4, C5 recorded: FILL within 12 semitones plays every key", L { atlasLayer ("missing-notes") }, baseConfig(), sweep);
        add ("atlas-fill-4", "the same with FILL limited to 4 semitones: keys out of reach stay silent", L { atlasLayer ("missing-notes:fill-4") }, baseConfig(),
             sweep);
        add ("atlas-no-fill", "FILL off: only the recorded keys sound", L { atlasLayer ("missing-notes:no-fill") }, baseConfig(), sweep);
    }
    add ("atlas-legato-line", "a line on four notes recorded legato (regions on adjacent frames)", L { atlasLayer ("legato") }, baseConfig(), legatoLine());
    add ("atlas-silence-gaps-scale", "mono plucks recorded with long silences between them, a scale", L { atlasLayer ("silence-gaps") }, baseConfig(), scale());
    {
        auto c = granularOn (baseConfig());
        c.engineSettings.granular[0].pan = 0.0;
        add ("atlas-stereo-wide-granular", "Granular from a wide stereo recording: each grain keeps its own stereo", L { atlasLayer ("stereo-wide") }, c,
             Notes().chord (0.05, 2.0, { 57, 62 }).done ("chord-a-d"));
    }
    {
        auto c = granularOn (dryConfig());
        c.engineSettings.granular[0].spread = 1.0;
        c.engineSettings.granular[0].sizeSeconds = 0.3;
        add ("atlas-boundary-granular", "Granular, SPREAD 100 %, on a 440 Hz note recorded next to a 3 kHz one: no 3 kHz may leak",
             L { atlasLayer ("boundary-probe") }, c, Notes().note (0.05, 2.0, 69).done ("held-a4"));
    }
    add ("atlas-regions-10-chord8", "an atlas of 10 one-second regions, eight-note chord", L { atlasLayer ("regions-10") }, baseConfig(), chord8());
    add ("atlas-regions-50-chord8", "an atlas of 50 one-second regions (round robins on 12 keys), eight-note chord", L { atlasLayer ("regions-50") },
         baseConfig(), chord8());
    {
        Notes n;
        n.chord (0.05, 3.5, { 55, 59, 62 });
        for (int i = 0; i < 12; ++i)
            n.note (0.1 + 0.28 * i, 0.25, 67 + (i % 3) * 2, 90);
        Scene s { "atlas-publication-while-rendering", "layer A's atlas replaced every 0.25 s (sung <-> bowed) while a chord is held and notes start",
                  sung, baseConfig(), n.done ("held-and-new") };
        s.swapAtlas = "bowed-phrase";
        s.swapEverySeconds = 0.25;
        scenes.push_back (std::move (s));
    }
    return scenes;
}

std::vector<KeyMeasure> measureKeys (const Scene& scene, const std::vector<int>& keys)
{
    const auto models = modelsFor (scene);
    keydna::Table table;
    table.compute (scene.config.keyDna[0]);
    std::vector<KeyMeasure> out;
    const int block = 256;
    std::vector<float> l (static_cast<std::size_t> (block)), r (static_cast<std::size_t> (block));
    auto settings = scene.config.engineSettings;
    settings.macros.life = 0.0;   // LIFE's per-trigger variation would blur what each key's profile does
    for (int key : keys)
    {
        InstrumentEngine engine;
        engine.prepare (scene.config.sampleRate, block, settings);
        engine.setModel (models[0], 0);
        engine.setLayerKeyDna (0, scene.config.keyDna[0]);
        engine.noteOn (key, 100);
        KeyMeasure m;
        m.key = key;
        const auto& o = table.at (key);
        m.posOffset = o.position;
        m.sizeOctaves = o.sizeOctaves;
        m.densityOctaves = o.densityOctaves;
        m.spreadOffset = o.spread;
        auto g = settings.granular[0];
        g.position = std::clamp (g.position + settings.layer[0].start, 0.0, 1.0);
        keydna::apply (o, 1.0, g);
        m.position = g.position;
        m.sizeMs = 1000.0 * g.sizeSeconds;
        m.density = g.density;
        m.spread = g.spread;
        double pos = 0.0, sum = 0.0, diff = 0.0;
        int grains = 0, blocks = 0;
        float previous = 0.0f;
        const int total = static_cast<int> (0.8 * scene.config.sampleRate / block);
        for (int b = 0; b < total; ++b)
        {
            float* ch[2] = { l.data(), r.data() };
            engine.render (ch, 2, block);
            const auto& snap = engine.grainSnapshot (0);
            const int n = snap.count.load();
            for (int i = 0; i < n; ++i)
                pos += snap.position[static_cast<std::size_t> (i)].load();
            grains += n;
            ++blocks;
            for (int i = 0; i < block; ++i)
            {
                sum += static_cast<double> (l[static_cast<std::size_t> (i)]) * l[static_cast<std::size_t> (i)];
                diff += static_cast<double> (l[static_cast<std::size_t> (i)] - previous) * (l[static_cast<std::size_t> (i)] - previous);
                previous = l[static_cast<std::size_t> (i)];
            }
        }
        m.meanGrainPosition = grains > 0 ? pos / grains : -1.0;
        m.meanGrains = static_cast<double> (grains) / std::max (1, blocks);
        m.rmsDb = 10.0 * std::log10 (std::max (1.0e-20, sum / std::max (1, total * block)));
        m.brightness = std::sqrt (diff / std::max (1.0e-20, sum));
        out.push_back (m);
    }
    return out;
}

const Scene* findScene (const std::vector<Scene>& scenes, const std::string& name)
{
    for (const auto& s : scenes)
        if (s.name == name)
            return &s;
    return nullptr;
}

namespace
{
    /** An atlas fixture by scene name: "<fixture>[:as-recorded|:fill-4|:no-fill]" or "regions-<n>". */
    atlasfixtures::Fixture atlasFixture (const std::string& spec)
    {
        const auto colon = spec.find (':');
        const auto base = spec.substr (0, colon);
        const auto variant = colon == std::string::npos ? std::string() : spec.substr (colon + 1);
        auto f = base.rfind ("regions-", 0) == 0 ? atlasfixtures::manyRegions (std::stoi (base.substr (8)), 1.0) : atlasfixtures::make (base);
        if (variant == "as-recorded")
            f.atlas.mapping.correctTuning = false;
        else if (variant == "fill-4")
            f.atlas.mapping.maxTransposeSemitones = 4.0;
        else if (variant == "no-fill")
            f.atlas.mapping.fill = false;
        return f;
    }

    std::mutex atlasLock;
    std::map<std::string, atlasset::BuildResult>& atlasCache()
    {
        static std::map<std::string, atlasset::BuildResult> cache;
        return cache;
    }
    const atlasset::BuildResult& builtAtlas (const std::string& spec)
    {
        const std::lock_guard<std::mutex> guard (atlasLock);
        auto& cache = atlasCache();
        auto it = cache.find (spec);
        if (it == cache.end())
            it = cache.emplace (spec, atlasfixtures::build (atlasFixture (spec))).first;
        return it->second;
    }
}

std::array<const InstrumentSet*, 3> setsFor (const Scene& scene)
{
    std::array<const InstrumentSet*, 3> sets {};
    for (std::size_t i = 0; i < scene.layers.size() && i < sets.size(); ++i)
        if (! scene.layers[i].atlas.empty())
            sets[i] = builtAtlas (scene.layers[i].atlas).set.get();
    return sets;
}

atlasset::BuildReport atlasReport (const std::string& atlas)
{
    return builtAtlas (atlas).report;
}

std::array<const InstrumentModel*, 3> modelsFor (const Scene& scene)
{
    static std::mutex lock;
    static std::map<SourceId, std::shared_ptr<InstrumentModel>> cache;
    const auto sets = setsFor (scene);
    const std::lock_guard<std::mutex> guard (lock);
    std::array<const InstrumentModel*, 3> models {};
    for (std::size_t i = 0; i < scene.layers.size() && i < models.size(); ++i)
    {
        if (const auto* set = sets[i]; set != nullptr && set->isValid())
        {
            models[i] = set->members[static_cast<std::size_t> (set->primary)].model.get();
            continue;
        }
        const auto id = scene.layers[i].source;
        auto& model = cache[id];
        if (model == nullptr)
        {
            const auto audio = sourceAudio (id);
            InstrumentBuildOptions options;
            options.seed = 1;
            model = instrument::buildComplete (audio, Analyzer::analyse (audio), options, false);
        }
        models[i] = model.get();
    }
    return models;
}

RenderOutput render (const Scene& scene, int blockSizeOverride)
{
    auto config = scene.config;
    if (blockSizeOverride > 0)
        config.blockSize = blockSizeOverride;
    const auto sets = setsFor (scene);
    if (std::all_of (sets.begin(), sets.end(), [] (const auto* s) { return s == nullptr; }))
        return renderLayers (modelsFor (scene), scene.midi, config);
    std::function<void (InstrumentEngine&, double)> swap;
    if (! scene.swapAtlas.empty() && scene.swapEverySeconds > 0.0)
    {
        const auto* other = builtAtlas (scene.swapAtlas).set.get();
        swap = [sets, other, every = scene.swapEverySeconds, last = -1] (InstrumentEngine& engine, double seconds) mutable {
            const int period = static_cast<int> (seconds / every);
            if (period != last)
            {
                last = period;
                engine.setInstrumentSet (period % 2 == 0 ? sets[0] : other, 0);
            }
        };
    }
    return renderLayerInstruments (modelsFor (scene), sets, scene.midi, config, swap);
}

Digest digest (const AudioData& audio)
{
    Digest d;
    d.hash = juce::String::toHexString (static_cast<juce::int64> (fnv (audio))).toStdString();
    double sum = 0.0, diff = 0.0, peak = 0.0, mid = 0.0, side = 0.0;
    std::size_t count = 0;
    for (const auto& channel : audio.channels)
    {
        float previous = 0.0f;
        for (float x : channel)
        {
            sum += static_cast<double> (x) * x;
            diff += static_cast<double> (x - previous) * (x - previous);
            peak = std::max (peak, static_cast<double> (std::abs (x)));
            previous = x;
            ++count;
        }
    }
    if (audio.channels.size() >= 2)
        for (std::size_t i = 0; i < audio.channels[0].size(); ++i)
        {
            const double l = audio.channels[0][i], r = audio.channels[1][i];
            mid += 0.25 * (l + r) * (l + r);
            side += 0.25 * (l - r) * (l - r);
        }
    d.seconds = audio.sampleRate > 0.0 && ! audio.channels.empty() ? static_cast<double> (audio.channels[0].size()) / audio.sampleRate : 0.0;
    d.rmsDb = 10.0 * std::log10 (std::max (1.0e-20, sum / std::max<std::size_t> (1, count)));
    d.peakDb = 20.0 * std::log10 (std::max (1.0e-10, peak));
    d.brightness = std::sqrt (diff / std::max (1.0e-20, sum));
    d.stereoWidth = side / std::max (1.0e-20, mid);
    return d;
}

juce::var digestToJson (const Scene& scene, const Digest& d, const std::string& platform)
{
    auto o = json::object();
    json::set (o, "schemaVersion", 1);
    json::set (o, "scene", json::str (scene.name));
    json::set (o, "description", json::str (scene.description));
    json::set (o, "midi", json::str (scene.midi.name));
    json::set (o, "sampleRate", scene.config.sampleRate);
    json::set (o, "blockSize", scene.config.blockSize);
    json::set (o, "seed", static_cast<juce::int64> (scene.config.engineSettings.seed));
    json::set (o, "platform", json::str (platform));
    json::set (o, "sampleHash", json::str (d.hash));
    json::set (o, "seconds", json::number (d.seconds, 6));
    json::set (o, "rmsDb", d.rmsDb);
    json::set (o, "peakDb", d.peakDb);
    json::set (o, "brightness", d.brightness);
    json::set (o, "stereoWidth", d.stereoWidth);
    return o;
}

std::string platformTag()
{
    return osp::platformTag();
}

Cost measure (const Scene& scene, int repeats)
{
    const auto config = scene.config;
    const double rate = config.sampleRate;
    const int block = std::max (1, config.blockSize);
    const auto models = modelsFor (scene);
    const auto sets = setsFor (scene);
    const InstrumentSet* swapSet = scene.swapAtlas.empty() ? nullptr : builtAtlas (scene.swapAtlas).set.get();
    const auto sequence = arpeggiate (scene.midi, config, rate);
    std::vector<double> best;
    Cost cost;
    std::vector<float> l (static_cast<std::size_t> (block)), r (static_cast<std::size_t> (block));
    for (int run = 0; run < std::max (1, repeats); ++run)
    {
        InstrumentEngine engine;
        engine.prepare (rate, block, config.engineSettings);
        for (int layer = 0; layer < 3; ++layer)
        {
            if (const auto* set = sets[static_cast<std::size_t> (layer)])
                engine.setInstrumentSet (set, layer);
            else
                engine.setModel (models[static_cast<std::size_t> (layer)], layer);
            engine.setLayerKeyDna (layer, config.keyDna[static_cast<std::size_t> (layer)]);
        }
        engine.setModulation (config.modulation);
        engine.setModWheel (static_cast<float> (config.modWheel));
        int swapPeriod = 0;
        const auto& events = sequence.events;
        const auto end = static_cast<std::int64_t> ((sequence.endTimeSeconds() + config.maxTailSeconds) * rate);
        std::size_t next = 0;
        int b = 0;
        for (std::int64_t pos = 0; pos < end; pos += block, ++b)
        {
            applyKeyDnaAutomation (engine, config, static_cast<double> (pos) / rate);
            const auto t0 = std::chrono::steady_clock::now();
            if (swapSet != nullptr && scene.swapEverySeconds > 0.0)
                if (const int period = static_cast<int> (static_cast<double> (pos) / rate / scene.swapEverySeconds); period != swapPeriod)
                {
                    // The plugin's audio thread takes a published instrument at a block's start: timed with the block.
                    swapPeriod = period;
                    engine.setInstrumentSet (period % 2 == 0 ? sets[0] : swapSet, 0);
                }
            int offset = 0;
            while (next < events.size())
            {
                const auto at = std::max<std::int64_t> (pos, std::llround (events[next].timeSeconds * rate));
                if (at >= pos + block)
                    break;
                const int eventOffset = static_cast<int> (at - pos);
                if (eventOffset > offset)
                {
                    float* ch[2] = { l.data() + offset, r.data() + offset };
                    engine.render (ch, 2, eventOffset - offset);
                }
                offset = eventOffset;
                const auto& e = events[next++];
                if (e.type == MidiEvent::Type::noteOn)
                    engine.noteOn (e.note, e.value);
                else if (e.type == MidiEvent::Type::noteOff)
                    engine.noteOff (e.note);
                else if (e.type == MidiEvent::Type::sustainPedal)
                    engine.setSustainPedal (e.value >= 64);
                else
                    engine.allNotesOff();
            }
            float* ch[2] = { l.data() + offset, r.data() + offset };
            engine.render (ch, 2, block - offset);
            const double micros = std::chrono::duration<double, std::micro> (std::chrono::steady_clock::now() - t0).count();
            cost.worstObservedMicros = std::max (cost.worstObservedMicros, micros);
            if (static_cast<std::size_t> (b) >= best.size())
                best.push_back (micros);
            else
                best[static_cast<std::size_t> (b)] = std::min (best[static_cast<std::size_t> (b)], micros);
            if (run == 0)
            {
                cost.peakVoices = std::max (cost.peakVoices, engine.musicalVoiceCount());
                int grains = 0;
                for (int layer = 0; layer < 3; ++layer)
                    grains += engine.grainSnapshot (layer).count.load (std::memory_order_relaxed);
                cost.peakGrains = std::max (cost.peakGrains, grains);
                cost.peakLiveGrains = std::max (cost.peakLiveGrains, engine.liveGrainCount());
            }
        }
    }
    if (best.empty())
        return cost;
    const double budget = 1.0e6 * block / rate;
    double total = 0.0;
    for (double v : best)
        total += v;
    auto sorted = best;
    std::sort (sorted.begin(), sorted.end());
    cost.renderedSeconds = static_cast<double> (best.size()) * block / rate;
    cost.meanBlockMicros = total / static_cast<double> (best.size());
    cost.p99BlockMicros = sorted[std::min (sorted.size() - 1, static_cast<std::size_t> (0.99 * static_cast<double> (sorted.size())))];
    cost.worstBlockMicros = sorted.back();
    cost.meanBudgetPercent = 100.0 * cost.meanBlockMicros / budget;
    cost.worstBudgetPercent = 100.0 * cost.worstBlockMicros / budget;
    return cost;
}

double peakResidentMegabytes()
{
#if defined(_WIN32)
    return 0.0;   // not measured on Windows
#else
    rusage usage {};
    getrusage (RUSAGE_SELF, &usage);
 #if defined(__APPLE__)
    return static_cast<double> (usage.ru_maxrss) / (1024.0 * 1024.0);   // bytes
 #else
    return static_cast<double> (usage.ru_maxrss) / 1024.0;              // kilobytes
 #endif
#endif
}

} // namespace osp::research::unified
