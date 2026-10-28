// Research benchmark (scratch, not part of the production build): OSP engine C driven the way
// the ANDOR Lab benchmark drives Grain/Phrase (N notes held, one re-triggered every 0.5 s),
// across the unified-source scenarios. Links build/libosp_dsp.a only.
#include "analysis/Analyzer.h"
#include "core/AudioData.h"
#include "core/Prng.h"
#include "engine/InstrumentBuilder.h"
#include "engine/InstrumentEngine.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <numbers>
#include <string>
#include <vector>

using namespace osp;

static AudioData tone (double rootHz, double seconds, double decay, std::uint64_t seed)
{
    const double rate = 48000.0;
    auto a = AudioData::allocate (2, static_cast<std::int64_t> (seconds * rate), rate);
    Prng n (seed);
    for (std::size_t i = 0; i < a.channels[0].size(); ++i)
    {
        const double t = static_cast<double> (i) / rate;
        const double env = std::min (1.0, t / 0.005) * std::exp (-decay * t);
        double s = 0.0;
        for (int h = 1; h <= 8; ++h)
            s += std::sin (2.0 * std::numbers::pi * rootHz * h * t + 0.3 * h) / (h * 1.3);
        a.channels[0][i] = static_cast<float> (0.4 * env * s + 0.004 * n.bipolar());
        a.channels[1][i] = static_cast<float> (0.4 * env * s * 0.97 + 0.004 * n.bipolar());
    }
    return a;
}

struct Scenario
{
    std::string name;
    int layers = 1;
    int notes = 8;
    SourceMode mode[3] { SourceMode::oneShot, SourceMode::oneShot, SourceMode::oneShot };
    double reimagined = 0.2;        // the macro default a dropped sound gets
    GranularParams gran {};
    bool set = false;               // layer A plays a 12-member multi-sample set (pitch anchors)
    bool pedal = false;
    bool maxMod = false;            // 16 routes: poly LFOs, envelopes, NOTE, VEL onto every granular destination
};

int main (int argc, char** argv)
{
    const int block = argc > 1 ? std::atoi (argv[1]) : 256;
    const double rate = 48000.0, seconds = argc > 2 ? std::atof (argv[2]) : 6.0;

    const auto srcA = tone (293.66, 3.0, 1.2, 1);   // piano-like pluck
    const auto srcB = tone (880.0, 6.0, 0.05, 2);   // long tone
    const auto srcC = tone (261.63, 3.0, 0.8, 3);   // harp-like
    InstrumentBuildOptions build;
    const auto mA = instrument::buildComplete (srcA, Analyzer::analyse (srcA), build, false);
    const auto mB = instrument::buildComplete (srcB, Analyzer::analyse (srcB), build, false);
    const auto mC = instrument::buildComplete (srcC, Analyzer::analyse (srcC), build, false);

    // A 12-note "atlas-like" set: one short note per semitone region, as Phrase would cut.
    std::vector<AudioData> setAudio;
    std::vector<AnalysisData> setAnalysis;
    for (int k = 0; k < 12; ++k)
    {
        setAudio.push_back (tone (220.0 * std::pow (2.0, k / 12.0), 1.2, 0.6, 10 + static_cast<std::uint64_t> (k)));
    }
    for (auto& a : setAudio)
        setAnalysis.push_back (Analyzer::analyse (a));
    std::vector<instrument::SetSource> sources;
    for (std::size_t k = 0; k < setAudio.size(); ++k)
        sources.push_back ({ &setAudio[k], &setAnalysis[k], "note" + std::to_string (k) + ".wav" });
    const auto set = instrument::buildSet (sources, build, {}, true);

    GranularParams dense;
    dense.sizeSeconds = 0.3;
    dense.density = 30.0;
    dense.spread = 0.4;

    std::vector<Scenario> scenarios;
    auto add = [&] (Scenario s) { scenarios.push_back (s); };
    add ({ "1 layer one-shot, 1 note", 1, 1 });
    add ({ "1 layer one-shot, 8 notes", 1, 8 });
    add ({ "1 layer one-shot, 16 notes", 1, 16 });
    add ({ "1 layer one-shot, 8 notes, REIM 0", 1, 8, {}, 0.0 });
    add ({ "1 layer one-shot, 8 notes, REIM 100", 1, 8, {}, 1.0 });
    add ({ "3 layers one-shot, 8 notes", 3, 8 });
    add ({ "1 layer granular, 8 notes", 1, 8, { SourceMode::granular, SourceMode::oneShot, SourceMode::oneShot } });
    add ({ "1 layer granular dense, 8 notes", 1, 8, { SourceMode::granular, SourceMode::oneShot, SourceMode::oneShot }, 0.2, dense });
    add ({ "1 layer granular, 16 notes", 1, 16, { SourceMode::granular, SourceMode::oneShot, SourceMode::oneShot } });
    add ({ "3 layers granular, 8 notes", 3, 8, { SourceMode::granular, SourceMode::granular, SourceMode::granular } });
    add ({ "3 layers granular dense, 8 notes", 3, 8, { SourceMode::granular, SourceMode::granular, SourceMode::granular }, 0.2, dense });
    add ({ "3 layers A one-shot + B,C granular, 8", 3, 8, { SourceMode::oneShot, SourceMode::granular, SourceMode::granular } });
    add ({ "3 layers granular, 8 notes, REIM 100", 3, 8, { SourceMode::granular, SourceMode::granular, SourceMode::granular }, 1.0 });
    add ({ "1 layer 12-member set, 8 notes", 1, 8, {}, 0.2, {}, true });
    add ({ "1 layer 12-member set, 16 notes", 1, 16, {}, 0.2, {}, true });
    add ({ "3 layers (set + 2 granular), 8 notes", 3, 8, { SourceMode::oneShot, SourceMode::granular, SourceMode::granular }, 0.2, {}, true });
    add ({ "3 layers one-shot, 8 notes, pedal", 3, 8, {}, 0.2, {}, false, true });
    add ({ "3 layers granular, 8 notes, 16 routes", 3, 8, { SourceMode::granular, SourceMode::granular, SourceMode::granular }, 0.2, {}, false, false, true });
    add ({ "3 layers granular, 16 notes, 16 routes", 3, 16, { SourceMode::granular, SourceMode::granular, SourceMode::granular }, 0.2, {}, false, false, true });

    std::printf ("rate %.0f Hz, block %d, %.1f s per scenario, default macros unless noted\n", rate, block, seconds);
    for (const auto& s : scenarios)
    {
        EngineSettings settings;
        settings.polyphony = 24;
        settings.macros.reimagined = s.reimagined;
        for (int l = 0; l < 3; ++l)
        {
            settings.sourceMode[static_cast<std::size_t> (l)] = s.mode[l];
            settings.granular[static_cast<std::size_t> (l)] = s.gran;
        }
        InstrumentEngine engine;
        engine.prepare (rate, block, settings);
        if (s.set)
            engine.setInstrumentSet (&set, 0);
        else
            engine.setModel (mA.get(), 0);
        if (s.layers > 1)
        {
            engine.setModel (mB.get(), 1);
            engine.setModel (mC.get(), 2);
        }
        std::vector<float> l (static_cast<std::size_t> (block)), r (static_cast<std::size_t> (block));
        float* out[2] = { l.data(), r.data() };
        const int total = static_cast<int> (seconds * rate / block);
        const int retrigger = std::max (1, static_cast<int> (0.5 * rate / block));
        std::vector<double> times;
        int peakVoices = 0, peakGrains = 0;
        double voiceSum = 0.0;
        int rr = 0;
        if (s.pedal)
            engine.setSustainPedal (true);
        if (s.maxMod)
        {
            mod::Settings m;
            m.lfo[0].scope = mod::Scope::poly;
            m.lfo[0].rateHz = 3.0;
            m.lfo[1].rateHz = 0.4;
            using D = mod::Dest;
            using S = mod::Source;
            const std::pair<S, D> r[16] = { { S::lfo1, D::grainPositionA }, { S::lfo1, D::grainPositionB }, { S::lfo1, D::grainPositionC },
                                            { S::lfo2, D::grainSizeA }, { S::lfo2, D::grainSizeB }, { S::lfo2, D::grainSizeC },
                                            { S::env1, D::grainDensityA }, { S::env1, D::grainDensityB }, { S::env1, D::grainDensityC },
                                            { S::note, D::grainSpreadA }, { S::note, D::grainSpreadB }, { S::note, D::grainSpreadC },
                                            { S::vel, D::cutoff }, { S::env2, D::levelA }, { S::lfo1, D::panB }, { S::modWheel, D::reimaginedC } };
            for (int i = 0; i < 16; ++i)
                m.routes[static_cast<std::size_t> (i)] = { r[i].first, r[i].second, 0.4, true, -1 };
            engine.setModulation (m);
        }
        for (int b = 0; b < total; ++b)
        {
            if (b == 0)
                for (int v = 0; v < s.notes; ++v)
                    engine.noteOn (45 + (v * 7) % 30, 96);
            else if (b % retrigger == 0)
            {
                const int note = 45 + ((rr++ % s.notes) * 7) % 30;
                engine.noteOff (note);
                engine.noteOn (note, 90);
            }
            const auto t0 = std::chrono::steady_clock::now();
            engine.render (out, 2, block);
            times.push_back (std::chrono::duration<double, std::micro> (std::chrono::steady_clock::now() - t0).count());
            const int voices = engine.activeVoiceCount();
            peakVoices = std::max (peakVoices, voices);
            voiceSum += voices;
            int grains = 0;
            for (int layer = 0; layer < 3; ++layer)
                grains += engine.grainSnapshot (layer).count.load();
            peakGrains = std::max (peakGrains, grains);
        }
        std::vector<double> sorted (times.begin() + 10, times.end());
        double sum = 0.0;
        for (double t : sorted)
            sum += t;
        std::sort (sorted.begin(), sorted.end());
        const double budget = 1.0e6 * block / rate;
        const double mean = sum / static_cast<double> (sorted.size());
        const int overruns = static_cast<int> (std::count_if (sorted.begin(), sorted.end(), [budget] (double t) { return t > budget; }));
        std::printf ("%-42s load %5.1f %%  p99 %5.1f %%  worst %6.1f %%  voices peak %2d mean %4.1f  grains(display) peak %3d  overruns %d/%zu\n",
                     s.name.c_str(), 100.0 * mean / budget, 100.0 * sorted[static_cast<std::size_t> (0.99 * static_cast<double> (sorted.size() - 1))] / budget,
                     100.0 * sorted.back() / budget, peakVoices, voiceSum / total, peakGrains, overruns, sorted.size());
    }
    return 0;
}
