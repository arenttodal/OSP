// Phase 2 measurements (hidden: osp_tests "[.][phase2-measure]"): the figures quoted in
// docs/research/unified-andor/phase-2-report.md, printed from one reproducible run. The
// assertions live in tests/integration/GrainPhase2Tests.cpp; nothing here can fail.

#include "engine/KeyDna.h"
#include "research/UnifiedScenes.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>

using namespace osp;
using namespace osp::research;

namespace
{
    double energy (const std::vector<float>& x)
    {
        double e = 0.0;
        for (float v : x)
            e += static_cast<double> (v) * v;
        return e;
    }
    double db (double ratio) { return 10.0 * std::log10 (std::max (1.0e-30, ratio)); }
    std::pair<double, double> midSide (const AudioData& a)
    {
        double m = 0.0, s = 0.0;
        for (std::size_t i = 0; i < a.channels[0].size(); ++i)
        {
            const double l = a.channels[0][i], r = a.channels[1][i];
            m += 0.25 * (l + r) * (l + r);
            s += 0.25 * (l - r) * (l - r);
        }
        return { m, s };
    }
    double maxStep (const AudioData& a, std::size_t from, std::size_t to)
    {
        double m = 0.0;
        for (const auto& ch : a.channels)
            for (std::size_t i = std::max<std::size_t> (from, 1); i < std::min (to, ch.size()); ++i)
                m = std::max (m, static_cast<double> (std::abs (ch[i] - ch[i - 1])));
        return m;
    }
    AudioData with (const unified::Scene& base, const std::function<void (RenderConfig&)>& edit)
    {
        auto c = base.config;
        edit (c);
        return renderLayers (unified::modelsFor (base), base.midi, c).audio;
    }
}

TEST_CASE ("Phase 2 measurements", "[.][phase2-measure]")
{
    const auto scenes = unified::phase2Scenes();
    auto find = [&] (const char* n) { return *unified::findScene (scenes, n); };

    // 1. Profile computation (message thread): all 128 keys x 7 parameters.
    {
        keydna::Table t;
        const auto s = unified::expressiveKeyDna();
        const int runs = 2000;
        const auto t0 = std::chrono::steady_clock::now();
        for (int i = 0; i < runs; ++i)
            t.compute (s);
        const double us = std::chrono::duration<double, std::micro> (std::chrono::steady_clock::now() - t0).count() / runs;
        std::printf ("\n1. KEY DNA profile: %.1f us to compute (128 keys x %d parameters), %zu bytes\n", us, keydna::paramCount, sizeof (keydna::Table));
    }

    // 2. PAN.
    for (const char* name : { "p2-pan-100-mono-single", "p2-pan-100-single" })
    {
        const auto& sc = find (name);
        const auto off = with (sc, [] (RenderConfig& c) { c.engineSettings.granular[0].pan = 0.0; });
        const auto half = with (sc, [] (RenderConfig& c) { c.engineSettings.granular[0].pan = 0.5; });
        const auto full = unified::render (sc).audio;
        const auto [m0, s0] = midSide (off);
        const auto [m5, s5] = midSide (half);
        const auto [m1, s1] = midSide (full);
        std::printf ("2. %s: side/mid PAN 0 %.3f, 50 %% %.3f, 100 %% %.3f; L+R energy at 100 %% %+.2f dB; mono sum %+.2f dB (50 %%: %+.2f dB)\n", name,
                     s0 / m0, s5 / m5, s1 / m1, db ((energy (full.channels[0]) + energy (full.channels[1])) / (energy (off.channels[0]) + energy (off.channels[1]))),
                     db (m1 / m0), db (m5 / m0));
    }

    // 3. Windows' level against HANN (chord4 and the sparse scale).
    for (const char* name : { "p2-options-neutral-chord4", "p2-window-percussive-sparse-scale" })
    {
        const auto& sc = find (name);
        auto e = [&] (GrainWindow w) {
            const auto a = with (sc, [w] (RenderConfig& c) { c.engineSettings.granular[0].window = w; });
            return energy (a.channels[0]) + energy (a.channels[1]);
        };
        const double hann = e (GrainWindow::hann);
        std::printf ("3. %s: SOFT %+.2f dB, PERCUSSIVE %+.2f dB against HANN\n", name, db (e (GrainWindow::soft) / hann), db (e (GrainWindow::percussive) / hann));
    }
    {
        const auto& sc = find ("p2-scatter-3-chord4");
        const auto off = with (sc, [] (RenderConfig& c) { c.engineSettings.granular[0].scatterSemitones = 0.0; });
        const auto on = unified::render (sc).audio;
        const auto d0 = unified::digest (off), d1 = unified::digest (on);
        std::printf ("3b. SCATTER +-3 st: level %+.2f dB, brightness %.3f -> %.3f\n", d1.rmsDb - d0.rmsDb, d0.brightness, d1.brightness);
    }

    // 4. Live AMOUNT: the step's largest sample change against the chord held at 0 or 100 %.
    {
        const auto& step = find ("p2-amount-step-held-chord");
        const auto stepped = unified::render (step).audio;
        const auto zero = with (step, [] (RenderConfig& c) { c.keyDnaAmountAutomation[0] = { { 0.0, 0.0 } }; });
        const auto full = with (step, [] (RenderConfig& c) { c.keyDnaAmountAutomation[0] = { { 0.0, 1.0 } }; });
        for (double at : { 1.5, 3.5 })
        {
            const auto from = static_cast<std::size_t> ((at - 0.05) * 48000.0), to = static_cast<std::size_t> ((at + 0.25) * 48000.0);
            std::printf ("4. AMOUNT step at %.1f s: largest sample step %.4f; held at 0: %.4f, at 100 %%: %.4f\n", at, maxStep (stepped, from, to),
                         maxStep (zero, from, to), maxStep (full, from, to));
        }
    }

    // 5. The budget's level, against unlimited (the stress scenes).
    {
        const auto budget = unified::budgetScenes();
        for (const char* material : { "stress", "extreme" })
        {
            const auto ref = unified::digest (unified::render (*unified::findScene (budget, std::string ("budget-") + material + "-unlimited")).audio);
            std::printf ("5. %s:", material);
            for (int b : { 512, 384, 256, 192, 128, 96, 64 })
            {
                const auto d = unified::digest (unified::render (*unified::findScene (budget, std::string ("budget-") + material + "-" + std::to_string (b))).audio);
                std::printf ("  %d: %+.2f dB (peak %+.2f)%s", b, d.rmsDb - ref.rmsDb, d.peakDb - ref.peakDb, d.hash == ref.hash ? " =" : "");
            }
            std::printf ("\n");
        }
    }
    SUCCEED();
}
