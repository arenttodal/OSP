// OSP's own FX route tests: Analog's route cases (analog/tests/FxGraphTests.cpp) rewritten to
// run through osp::InstrumentEngine, plus what only OSP has (a mono output, the realtime
// allocation guard). The CLASSIC cases are the contract that keeps every existing OSP patch
// sounding exactly as before: no published route, or a CLASSIC one, plays PostProcessor bit
// for bit.

#include "audio/utility/TestSignals.h"
#include "engine/InstrumentBuilder.h"
#include "engine/InstrumentEngine.h"
#include "engine/fx/Effects.h"
#include "engine/fx/FxEngine.h"
#include "support/AllocationCounter.h"
#include "support/TestHelpers.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

using namespace osp;
using namespace osp::fx;

namespace
{
    constexpr double rate = 48000.0;
    constexpr int block = 256;

    struct Patch
    {
        Macros macros;
        double echoFeedback = 0.55;
        fx::Settings fx;
        bool mono = false;
    };

    /** Every original post effect audibly on: DRIVE, MOVEMENT, ECHO and SPACE. */
    Patch chainPatch()
    {
        Patch p;
        p.macros.life = 0.0;
        p.macros.drive = 0.5;
        p.macros.motion = 0.6;
        p.macros.echo = 0.4;
        p.macros.space = 0.5;
        return p;
    }

    const InstrumentModel& sourceModel()
    {
        static const auto model = [] {
            auto audio = testsignals::saw (220.0, 1.5, rate);
            testsignals::applyFades (audio, 0.01, 0.05);
            return instrument::buildComplete (audio, test::analyse (audio), {}, false);
        }();
        return *model;
    }

    std::unique_ptr<InstrumentEngine> makeEngine (const Patch& p)
    {
        EngineSettings es;
        es.macros = p.macros;
        es.shaping.echoFeedback = p.echoFeedback;
        auto e = std::make_unique<InstrumentEngine>();
        // The new effects' settings before prepare(): prepare() snaps every processor to them
        // (afterwards each one glides, e.g. RETRO's MIX from its default).
        e->setFxSettings (p.fx);
        e->prepare (rate, block, es);
        e->setModel (&sourceModel());
        e->setMacros (p.macros);
        return e;
    }

    struct Stereo
    {
        std::vector<float> left, right;
        double rms (double fromSeconds, double toSeconds) const
        {
            const auto from = static_cast<std::size_t> (fromSeconds * rate);
            const auto to = std::min (left.size(), static_cast<std::size_t> (toSeconds * rate));
            double e = 0.0;
            for (auto i = from; i < to; ++i)
                e += static_cast<double> (left[i]) * left[i] + static_cast<double> (right[i]) * right[i];
            return to > from ? std::sqrt (e / (2.0 * static_cast<double> (to - from))) : 0.0;
        }
    };

    /** Renders `seconds`; two notes (a fifth) start at 0 and end at 0.5 s when `play` is set. */
    Stereo render (InstrumentEngine& e, double seconds, bool play, bool mono)
    {
        Stereo out;
        std::vector<float> l (block), r (block);
        const int total = static_cast<int> (seconds * rate);
        if (play)
        {
            e.noteOn (50, 100);
            e.noteOn (57, 90);
        }
        bool released = ! play;
        for (int done = 0; done < total; done += block)
        {
            if (! released && done >= static_cast<int> (0.5 * rate))
            {
                e.noteOff (50);
                e.noteOff (57);
                released = true;
            }
            float* ch[2] = { l.data(), r.data() };
            e.render (ch, mono ? 1 : 2, block);
            out.left.insert (out.left.end(), l.begin(), l.end());
            if (mono)
                out.right.insert (out.right.end(), l.begin(), l.end());
            else
                out.right.insert (out.right.end(), r.begin(), r.end());
        }
        return out;
    }

    Stereo play (const Patch& p, const Graph* g, double seconds = 2.0)
    {
        auto e = makeEngine (p);
        if (g != nullptr)
            e->setFxGraph (fx::compile (*g, 1));
        return render (*e, seconds, true, p.mono);
    }

    bool identical (const Stereo& a, const Stereo& b) { return a.left == b.left && a.right == b.right; }

    bool finite (const Stereo& s)
    {
        auto ok = [] (const std::vector<float>& x) { return std::all_of (x.begin(), x.end(), [] (float v) { return std::isfinite (v); }); };
        return ok (s.left) && ok (s.right);
    }

    /** The largest sample difference relative to `a`'s peak, in dB. */
    double peakDifferenceDb (const Stereo& a, const Stereo& b)
    {
        double diff = 0.0, peak = 1.0e-12;
        for (std::size_t i = 0; i < std::min (a.left.size(), b.left.size()); ++i)
        {
            diff = std::max ({ diff, std::abs (static_cast<double> (a.left[i]) - b.left[i]), std::abs (static_cast<double> (a.right[i]) - b.right[i]) });
            peak = std::max ({ peak, std::abs (static_cast<double> (a.left[i])), std::abs (static_cast<double> (a.right[i])) });
        }
        return 20.0 * std::log10 (std::max (1.0e-15, diff) / peak);
    }

    Node fxNode (Type t) { return Node::effect ({ t, 0 }); }

    Graph custom (std::vector<Node> chain)
    {
        Graph g;
        g.custom = true;
        g.chain = std::move (chain);
        return g;
    }
}

TEST_CASE ("osp fx route: no route and a CLASSIC route both play the original chain bit for bit", "[unit][fx][route][legacy]")
{
    for (const bool mono : { false, true })
    {
        auto p = chainPatch();
        p.mono = mono;
        const auto none = play (p, nullptr);
        const auto classic = fx::classicGraph();
        const auto withClassic = play (p, &classic);
        CHECK (identical (none, withClassic));
        CHECK (finite (none));
        CHECK (none.rms (0.1, 0.5) > 0.01);
    }
}

TEST_CASE ("osp fx route: CLASSIC's arrangement as a CUSTOM graph sounds as CLASSIC does", "[unit][fx][route]")
{
    const auto p = chainPatch();
    const auto classic = play (p, nullptr, 2.5);
    auto g = fx::classicGraph();
    g.custom = true;
    const auto routed = play (p, &g, 2.5);
    REQUIRE (finite (routed));
    CHECK (peakDifferenceDb (classic, routed) < -100.0);
}

TEST_CASE ("osp fx route: other orders differ, all finite; ECHO feeds SPACE only when wired in series", "[unit][fx][route]")
{
    auto p = chainPatch();
    auto classicCustom = fx::classicGraph();
    classicCustom.custom = true;
    const auto reference = play (p, &classicCustom);

    const auto echoFirst = custom ({ fxNode (Type::echo), fxNode (Type::drive), fxNode (Type::movement), fxNode (Type::space) });
    const auto spaceBeforeMovement = custom ({ fxNode (Type::drive), fxNode (Type::space), fxNode (Type::movement), fxNode (Type::echo) });
    const auto echoIntoSpace = custom ({ fxNode (Type::drive), fxNode (Type::movement), fxNode (Type::echo), fxNode (Type::space) });
    for (const auto* g : { &echoFirst, &spaceBeforeMovement, &echoIntoSpace })
    {
        const auto out = play (p, g);
        CHECK (finite (out));
        CHECK (peakDifferenceDb (reference, out) > -60.0);
        CHECK (out.rms (0.1, 0.5) > 0.01);
    }

    p.macros.drive = 0.0;
    p.macros.motion = 0.0;
    p.macros.space = 0.6;
    p.macros.echo = 0.6;
    const auto series = custom ({ fxNode (Type::echo), fxNode (Type::space) });
    const auto parallel = custom ({ Node::parallel ({ fxNode (Type::echo) }, { fxNode (Type::space) }, Law::send) });
    const auto s = play (p, &series, 3.0), q = play (p, &parallel, 3.0);
    REQUIRE (finite (s));
    REQUIRE (finite (q));
    // In series the reverb hears the repeats; under the send it hears only the dry sound. (Not
    // compared by late energy as in Analog: OSP's SPACE crossfades its dry in series, so the
    // echo tail is partly traded for its reverb, louder or softer depending on the source.)
    Stereo lateS, lateQ;
    const auto from = static_cast<std::ptrdiff_t> (1.2 * rate);
    lateS.left.assign (s.left.begin() + from, s.left.end());
    lateS.right.assign (s.right.begin() + from, s.right.end());
    lateQ.left.assign (q.left.begin() + from, q.left.end());
    lateQ.right.assign (q.right.begin() + from, q.right.end());
    CHECK (peakDifferenceDb (lateQ, lateS) > -20.0);
    CHECK (s.rms (1.2, 3.0) > 1.0e-4);
}

TEST_CASE ("osp fx route: an empty linear pair is transparent; a bypassed pair is unity", "[unit][fx][route]")
{
    auto p = chainPatch();
    const auto plain = custom ({ fxNode (Type::drive), fxNode (Type::movement) });
    const auto withEmpty = custom ({ fxNode (Type::drive), Node::parallel ({}, {}, Law::linear), fxNode (Type::movement) });
    CHECK (identical (play (p, &plain), play (p, &withEmpty)));

    auto bypassedPair = custom ({ Node::parallel ({ fxNode (Type::echo) }, { fxNode (Type::space) }, Law::linear) });
    bypassedPair.chain[0].lanes[0][0].bypassed = true;
    bypassedPair.chain[0].lanes[1][0].bypassed = true;
    p.macros.echo = p.macros.space = 0.0;
    p.macros.drive = 0.0;
    p.macros.motion = 0.0;
    const auto a = play (p, nullptr), b = play (p, &bypassedPair);
    CHECK (peakDifferenceDb (a, b) < -90.0);
}

TEST_CASE ("osp fx route: bypassing SPACE glides out and lets its tail drain", "[unit][fx][route]")
{
    auto p = chainPatch();
    p.macros.drive = p.macros.motion = p.macros.echo = 0.0;
    p.macros.space = 0.7;
    auto g = custom ({ fxNode (Type::space) });
    auto e = makeEngine (p);
    e->setFxGraph (fx::compile (g, 1));
    render (*e, 0.6, true, false);
    g.chain[0].bypassed = true;
    e->setFxGraph (fx::compile (g, 2));
    const auto after = render (*e, 1.5, false, false);
    CHECK (finite (after));
    CHECK (after.rms (0.05, 0.3) > 1.0e-4);
    CHECK (after.rms (1.2, 1.5) < after.rms (0.05, 0.3));
}

TEST_CASE ("osp fx route: swapping routes while notes and tails play stays finite and click-free", "[unit][fx][route]")
{
    auto p = chainPatch();
    auto e = makeEngine (p);
    std::vector<Graph> routes;
    routes.push_back (fx::classicGraph());
    auto c = fx::classicGraph();
    c.custom = true;
    routes.push_back (c);
    routes.push_back (custom ({ fxNode (Type::space), fxNode (Type::echo), fxNode (Type::drive), fxNode (Type::movement) }));
    routes.push_back (custom ({ Node::parallel ({ fxNode (Type::drive), fxNode (Type::echo) }, { fxNode (Type::movement), fxNode (Type::space) }) }));
    routes.push_back (custom ({ fxNode (Type::spread), fxNode (Type::compressor), fxNode (Type::retro), fxNode (Type::space) }));
    routes.push_back (custom ({}));
    std::vector<float> l (128), r (128);
    std::uint64_t generation = 1;
    float largestStep = 0.0f, previous = 0.0f;
    e->noteOn (50, 100);
    e->noteOn (57, 100);
    for (int b = 0; b < 600; ++b)
    {
        if (b % 40 == 0)
            e->setFxGraph (fx::compile (routes[static_cast<std::size_t> ((b / 40) % static_cast<int> (routes.size()))], ++generation));
        float* ch[2] = { l.data(), r.data() };
        e->render (ch, 2, 128);
        for (int i = 0; i < 128; ++i)
        {
            REQUIRE (std::isfinite (l[static_cast<std::size_t> (i)]));
            largestStep = std::max (largestStep, std::abs (l[static_cast<std::size_t> (i)] - previous));
            previous = l[static_cast<std::size_t> (i)];
        }
        e->fxRouting().collectGarbage();
    }
    CHECK (largestStep < 0.5f);
}

TEST_CASE ("osp fx route latency: RETRO's delay is aligned and reported; CLASSIC reports none", "[unit][fx][route][latency]")
{
    auto p = chainPatch();
    p.macros.drive = p.macros.motion = p.macros.echo = p.macros.space = 0.0;
    p.fx.retro[0].mix = 0.0f;   // RETRO at MIX 0: exactly its aligned dry
    const int latency = fx::Retro::frameFor (rate);
    REQUIRE (latency > 0);
    const auto plain = custom ({ fxNode (Type::utility) });
    const auto reference = play (p, &plain);
    auto delayedBy = [&reference] (const Stereo& out, int samples) {
        double diff = 0.0, peak = 1.0e-12;
        for (std::size_t i = 0; i + static_cast<std::size_t> (samples) < out.left.size(); ++i)
        {
            const auto j = i + static_cast<std::size_t> (samples);
            diff = std::max ({ diff, std::abs (static_cast<double> (out.left[j]) - reference.left[i]), std::abs (static_cast<double> (out.right[j]) - reference.right[i]) });
            peak = std::max (peak, std::abs (static_cast<double> (reference.left[i])));
        }
        return 20.0 * std::log10 (std::max (1.0e-15, diff) / peak);
    };
    auto routed = [&p] (const Graph& g, int& reported) {
        auto e = makeEngine (p);
        e->setFxGraph (fx::compile (g, 1));
        auto out = render (*e, 2.0, true, false);
        reported = e->fxLatencySamples();
        return out;
    };
    int reported = -1;
    const auto serial = custom ({ fxNode (Type::utility), Node::effect ({ Type::retro, 0 }) });
    CHECK (delayedBy (routed (serial, reported), latency) < -120.0);
    CHECK (reported == latency);
    p.fx.retro[0].on = false;   // its own ON off: the route keeps the same latency
    CHECK (delayedBy (routed (serial, reported), latency) < -120.0);
    CHECK (reported == latency);
    p.fx.retro[0].on = true;
    const auto send = custom ({ fxNode (Type::utility), Node::parallel ({ Node::effect ({ Type::retro, 0 }) }, {}, Law::send) });
    CHECK (delayedBy (routed (send, reported), latency) < -100.0);
    CHECK (reported == latency);

    auto e = makeEngine (p);
    render (*e, 0.1, true, false);
    CHECK (e->fxLatencySamples() == 0);
}

TEST_CASE ("osp fx route: a CUSTOM route on a mono output runs in stereo and folds down, finite", "[unit][fx][route]")
{
    auto p = chainPatch();
    p.mono = true;
    const auto g = custom ({ fxNode (Type::spread), fxNode (Type::drive), fxNode (Type::echo), fxNode (Type::space) });
    const auto out = play (p, &g);
    REQUIRE (finite (out));
    CHECK (out.rms (0.1, 0.5) > 0.01);
    CHECK (out.left == out.right);
}

TEST_CASE ("osp fx route: rendering a CUSTOM route never allocates", "[unit][fx][route][realtime]")
{
    auto p = chainPatch();
    auto e = makeEngine (p);
    const auto g = custom ({ fxNode (Type::spread), fxNode (Type::compressor), fxNode (Type::eq), fxNode (Type::filter), fxNode (Type::drive),
                             Node::parallel ({ fxNode (Type::echo), fxNode (Type::phaser) }, { fxNode (Type::space), fxNode (Type::flanger) }, Law::send),
                             fxNode (Type::retro), fxNode (Type::utility) });
    REQUIRE (fx::compile (g, 1) != nullptr);
    e->setFxGraph (fx::compile (g, 1));
    std::vector<float> l (block), r (block);
    float* ch[2] = { l.data(), r.data() };
    e->noteOn (50, 100);
    for (int b = 0; b < 8; ++b)
        e->render (ch, 2, block);   // (the swap and first-use warm-up happen here)
    int allocations = 0;
    {
        test::AllocationScope scope;
        for (int b = 0; b < 200; ++b)
        {
            e->render (ch, 2, block);
            e->render (ch, 1, block);
        }
        allocations = scope.count();
    }
    CHECK (allocations == 0);
}
