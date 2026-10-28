// Phase 2 investigation (hidden: osp_tests "[.][stereo-investigation]"): why does the expressive
// KEY DNA demonstration's four-note chord have less side energy than the static one? Each
// variant changes one thing; the output is the evidence for docs/research/unified-andor/
// phase-2-report.md §stereo. Nothing here changes the engine.

#include "analysis/Analyzer.h"
#include "engine/InstrumentBuilder.h"
#include "research/UnifiedScenes.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdio>

using namespace osp;
using namespace osp::research;

namespace
{
    double width (const AudioData& a, std::size_t from = 0, std::size_t to = SIZE_MAX)
    {
        double mid = 0.0, side = 0.0;
        to = std::min (to, a.channels[0].size());
        for (std::size_t i = from; i < to; ++i)
        {
            const double l = a.channels[0][i], r = a.channels[1][i];
            mid += 0.25 * (l + r) * (l + r);
            side += 0.25 * (l - r) * (l - r);
        }
        return side / std::max (1.0e-20, mid);
    }
}

TEST_CASE ("stereo investigation: expressive KEY DNA chord width", "[.][stereo-investigation]")
{
    const auto scenes = unified::keyDnaScenes();
    const auto& stat = *unified::findScene (scenes, "keydna-a-static-chord4");
    const auto& expr = *unified::findScene (scenes, "keydna-c-expressive-chord4");

    // 1. The source itself: how wide is the recording along its length (where grains read)?
    const auto src = unified::sourceAudio (unified::SourceId::vowelA3);
    std::printf ("\n1. Source vowel-A3 side/mid by position (10 %% windows):\n  ");
    const auto frames = src.channels[0].size();
    for (int w = 0; w < 10; ++w)
        std::printf ("%.0f%%: %.3f  ", w * 10.0, width (src, frames * static_cast<std::size_t> (w) / 10, frames * static_cast<std::size_t> (w + 1) / 10));
    std::printf ("\n");

    auto run = [] (const unified::Scene& base, auto&& edit) {
        auto s = base;
        edit (s.config);
        return width (unified::render (s).audio);
    };
    auto only = [] (keydna::Param keep) {
        return [keep] (RenderConfig& c) {
            auto& d = c.keyDna[0];
            for (int p = 0; p < keydna::paramCount; ++p)
                if (static_cast<keydna::Param> (p) != keep)
                {
                    d.params[static_cast<std::size_t> (p)].curveDepth = 0.0;
                    d.params[static_cast<std::size_t> (p)].variation = 0.0;
                }
        };
    };
    const auto none = [] (RenderConfig&) {};
    std::printf ("\n2. Chord4 side/mid, one change at a time:\n");
    std::printf ("  static                          %.3f\n", run (stat, none));
    std::printf ("  expressive (all)                %.3f\n", run (expr, none));
    std::printf ("  expressive, POS only            %.3f\n", run (expr, only (keydna::Param::position)));
    std::printf ("  expressive, SIZE only           %.3f\n", run (expr, only (keydna::Param::size)));
    std::printf ("  expressive, DENS only           %.3f\n", run (expr, only (keydna::Param::density)));
    std::printf ("  expressive, SPREAD only         %.3f\n", run (expr, only (keydna::Param::spread)));
    std::printf ("  expressive, all but SPREAD      %.3f\n", run (expr, [] (RenderConfig& c) { c.keyDna[0][keydna::Param::spread].variation = 0.0; }));
    std::printf ("  expressive, all but POS         %.3f\n", run (expr, [] (RenderConfig& c) { c.keyDna[0][keydna::Param::position].variation = 0.0; }));
    auto dry = [] (RenderConfig& c) { c.engineSettings.macros.space = 0.0; c.engineSettings.macros.life = 0.0; };
    std::printf ("  static, SPACE 0 LIFE 0          %.3f\n", run (stat, dry));
    std::printf ("  expressive, SPACE 0 LIFE 0      %.3f\n", run (expr, dry));
    std::printf ("  expressive POS only, dry        %.3f\n", run (expr, [&] (RenderConfig& c) { dry (c); only (keydna::Param::position) (c); }));

    // 3. Each chord key alone, dry: its own width with and without the profile, and where its grains read.
    std::printf ("\n3. Each key alone (dry): side/mid static -> expressive, and its POS\n");
    keydna::Table t;
    t.compute (unified::expressiveKeyDna());
    for (int key : { 60, 64, 67, 71 })
    {
        auto one = [key, &dry] (const unified::Scene& base) {
            auto s = base;
            dry (s.config);
            s.midi.events.clear();
            s.midi.events.push_back ({ 0.05, MidiEvent::Type::noteOn, key, 96, 1 });
            s.midi.events.push_back ({ 2.05, MidiEvent::Type::noteOff, key, 0, 1 });
            return width (unified::render (s).audio);
        };
        const double pos = std::clamp (0.45 + t.at (key).position, 0.0, 1.0), spread = std::clamp (0.15 + t.at (key).spread, 0.0, 1.0);
        // The recording's own width over the stretch this key's grains read (POS +- SPREAD / 2
        // of the recording, plus the 4 ms jitter), with and without the profile.
        auto region = [&] (double p, double sp) {
            const double half = sp * 0.5 * static_cast<double> (frames) + 0.004 * 48000.0;
            const auto from = static_cast<std::size_t> (std::clamp (p * static_cast<double> (frames) - half, 0.0, static_cast<double> (frames)));
            const auto to = static_cast<std::size_t> (std::clamp (p * static_cast<double> (frames) + half, 0.0, static_cast<double> (frames)));
            return width (src, from, std::max (from + 1, to));
        };
        std::printf ("  key %d: %.3f -> %.3f   POS %.3f -> %.3f, SPREAD %.3f -> %.3f   source over its read region %.3f -> %.3f\n", key, one (stat),
                     one (expr), 0.45, pos, 0.15, spread, region (0.45, 0.15), region (pos, spread));
    }

    // 4. The control: a recording whose width is the same all along (the vowel's left channel,
    // and the right a 0.7 ms-delayed copy of it). If reading other places is the cause, the
    // expressive chord is no narrower than the static one here.
    {
        auto uniform = unified::sourceAudio (unified::SourceId::vowelA3);
        auto& r = uniform.channels[1];
        const auto& l = uniform.channels[0];
        const std::size_t delay = 34;   // 0.7 ms at 48 kHz
        for (std::size_t i = 0; i < r.size(); ++i)
            r[i] = i >= delay ? l[i - delay] : 0.0f;
        InstrumentBuildOptions options;
        options.seed = 1;
        const auto model = instrument::buildComplete (uniform, Analyzer::analyse (uniform), options, false);
        const std::array<const InstrumentModel*, 3> models { model.get(), nullptr, nullptr };
        std::printf ("\n4. Control - a recording with the same width all along: source side/mid by 10 %% windows:\n  ");
        for (int w = 0; w < 10; ++w)
            std::printf ("%.0f%%: %.3f  ", w * 10.0, width (uniform, frames * static_cast<std::size_t> (w) / 10, frames * static_cast<std::size_t> (w + 1) / 10));
        auto chordWidth = [&] (const unified::Scene& base) { return width (renderLayers (models, base.midi, base.config).audio); };
        std::printf ("\n  chord4 side/mid: static %.3f, expressive %.3f\n", chordWidth (stat), chordWidth (expr));
    }
    SUCCEED();
}
