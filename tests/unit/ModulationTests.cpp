// MODULATION: LFO shapes, phase and sync, one-shot, envelopes, the route rules (scope,
// sign, sum, bypass), and the engine following the routes (shared stages and per-voice
// destinations), deterministic and identical to the unmodulated engine without routes.

#include "audio/utility/TestSignals.h"
#include "engine/InstrumentBuilder.h"
#include "engine/InstrumentEngine.h"
#include "engine/Modulation.h"
#include "support/TestHelpers.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <numbers>
#include <vector>

using namespace osp;
using Catch::Approx;

TEST_CASE ("mod: LFO shapes and polarity", "[unit][mod]")
{
    mod::CurveTable flat {};
    flat.fill (0.75f);
    using S = mod::LfoShape;
    CHECK (mod::lfoShapeValue (S::sine, 0.25, flat, 1, 0) == Approx (1.0));
    CHECK (mod::lfoShapeValue (S::sine, 0.75, flat, 1, 0) == Approx (-1.0));
    CHECK (mod::lfoShapeValue (S::triangle, 0.0, flat, 1, 0) == Approx (0.0).margin (1e-12));
    CHECK (mod::lfoShapeValue (S::triangle, 0.25, flat, 1, 0) == Approx (1.0));
    CHECK (mod::lfoShapeValue (S::triangle, 0.75, flat, 1, 0) == Approx (-1.0));
    CHECK (mod::lfoShapeValue (S::rampUp, 0.0, flat, 1, 0) == Approx (-1.0));
    CHECK (mod::lfoShapeValue (S::rampUp, 0.75, flat, 1, 0) == Approx (0.5));
    CHECK (mod::lfoShapeValue (S::rampDown, 0.25, flat, 1, 0) == Approx (0.5));
    CHECK (mod::lfoShapeValue (S::pulse, 0.2, flat, 1, 0) == Approx (1.0));
    CHECK (mod::lfoShapeValue (S::pulse, 0.7, flat, 1, 0) == Approx (-1.0));
    CHECK (mod::lfoShapeValue (S::custom, 0.4, flat, 1, 0) == Approx (0.5));
    // Smooth random: deterministic, continuous across cycles, within -1..1, seed-dependent.
    for (std::int64_t k = -3; k < 40; ++k)
    {
        const double end = mod::lfoShapeValue (S::smoothRandom, 0.999999, flat, 7, k);
        const double next = mod::lfoShapeValue (S::smoothRandom, 0.0, flat, 7, k + 1);
        CHECK (end == Approx (next).margin (1e-6));
        CHECK (std::abs (end) <= 1.0);
        CHECK (mod::lfoShapeValue (S::smoothRandom, 0.3, flat, 7, k) == mod::lfoShapeValue (S::smoothRandom, 0.3, flat, 7, k));
    }
    CHECK (mod::lfoShapeValue (S::smoothRandom, 0.0, flat, 7, 3) != mod::lfoShapeValue (S::smoothRandom, 0.0, flat, 8, 3));
    mod::LfoSettings uni;
    uni.bipolar = false;
    CHECK (mod::lfoOutput (uni, 0.75, flat, 1, 0) == Approx (0.0).margin (1e-12));   // sine trough
    CHECK (mod::lfoOutput (uni, 0.25, flat, 1, 0) == Approx (1.0));
}

TEST_CASE ("mod: LFO phase has no drift and no block-size dependence; sync follows the song", "[unit][mod]")
{
    mod::LfoSettings s;
    s.rateHz = 3.7;
    for (double rate : { 44100.0, 48000.0, 96000.0 })
    {
        CAPTURE (rate);
        mod::LfoState a, b;
        a.start (s, 0.0);
        b.start (s, 0.0);
        const int total = static_cast<int> (10.0 * rate);
        for (int done = 0; done < total; done += 32)
            a.advance (s, 32.0 / rate, 120.0);
        for (int done = 0; done < total; done += 4096)
            b.advance (s, std::min (4096, total - done) / rate, 120.0);
        const double expected = 3.7 * (static_cast<double> ((total + 31) / 32 * 32) / rate);
        CHECK (static_cast<double> (a.cycle) + a.phase == Approx (expected).epsilon (1e-9));
        CHECK (static_cast<double> (b.cycle) + b.phase == Approx (3.7 * total / rate).epsilon (1e-9));
    }
    // Synced: the phase is the song position's, so a loop or a bounce repeats exactly.
    s.sync = true;
    s.division = 3;   // 1/4
    s.phase = 0.25;
    mod::LfoState c;
    c.follow (s, 10.5);
    CHECK (c.cycle == 10);
    CHECK (c.phase == Approx (0.75));
    c.follow (s, 2.0);   // a jump back
    CHECK (c.cycle == 2);
    CHECK (c.phase == Approx (0.25));
    // Free at the host's tempo when it is stopped: 1/8 at 120 BPM = 4 Hz.
    s.division = 2;
    mod::LfoState d;
    d.start (s, 0.0);
    d.advance (s, 0.25, 120.0);
    CHECK (static_cast<double> (d.cycle) + d.phase == Approx (1.0));
}

TEST_CASE ("mod: ONE SHOT runs one cycle and holds", "[unit][mod]")
{
    mod::CurveTable curve {};
    mod::LfoSettings s;
    s.shape = mod::LfoShape::rampUp;
    s.mode = mod::LfoMode::oneShot;
    s.rateHz = 2.0;
    mod::LfoState st;
    st.start (s, 0.0);
    for (int i = 0; i < 100; ++i)
    {
        st.advance (s, 0.01, 120.0);
        st.evaluate (s, curve, 1);
    }
    CHECK (st.finished);
    CHECK (st.value == Approx (1.0).margin (1e-6));   // a ramp up holds at its top
}

TEST_CASE ("mod: envelopes - ADSR stages in time, release from where it is, one-shot curve", "[unit][mod]")
{
    mod::CurveTable curve {};
    for (int i = 0; i < mod::curvePoints; ++i)
        curve[static_cast<std::size_t> (i)] = static_cast<float> (i) / (mod::curvePoints - 1);
    mod::EnvSettings s;
    s.attackSeconds = 0.1;
    s.decaySeconds = 0.2;
    s.sustain = 0.5;
    s.releaseSeconds = 0.3;
    for (double rate : { 44100.0, 48000.0, 96000.0 })
    {
        CAPTURE (rate);
        const double dt = 32.0 / rate;
        mod::EnvState e;
        e.start();
        double t = 0.0, peakAt = -1.0;
        while (t < 0.5)
        {
            e.advance (s, curve, dt);
            t += dt;
            if (peakAt < 0.0 && e.level >= 1.0)
                peakAt = t;
        }
        CHECK (peakAt == Approx (0.1).margin (2.0 * dt));
        CHECK (e.level == Approx (0.5));
        e.release();
        for (double r = 0.0; r < 0.31; r += dt)
            e.advance (s, curve, dt);
        CHECK (e.level == Approx (0.0).margin (1e-9));
        CHECK (e.stage == mod::EnvState::Stage::done);
    }
    // Released during the attack: it falls from where it was, never jumps.
    mod::EnvState e;
    e.start();
    e.advance (s, curve, 0.05);
    const double before = e.level;
    e.release();
    e.advance (s, curve, 0.001);
    CHECK (e.level <= before);
    CHECK (e.level > 0.9 * before);
    // One-shot curve: the curve over its length, regardless of note-off.
    s.oneShotCurve = true;
    s.lengthSeconds = 1.0;
    mod::EnvState o;
    o.start();
    o.advance (s, curve, 0.5);
    CHECK (o.level == Approx (0.5).margin (0.01));
    o.release();
    o.advance (s, curve, 0.25);
    CHECK (o.level == Approx (0.75).margin (0.01));
    o.advance (s, curve, 1.0);
    CHECK (o.level == Approx (1.0));
}

TEST_CASE ("mod: route rules - scope, sign, sum, bypass, empty", "[unit][mod]")
{
    mod::Settings s;
    s.lfo[0].scope = mod::Scope::global;
    s.lfo[1].scope = mod::Scope::poly;
    using D = mod::Dest;
    using Src = mod::Source;
    CHECK (mod::compatible (s, Src::lfo1, D::space));
    CHECK_FALSE (mod::compatible (s, Src::lfo2, D::space));   // poly LFO on a shared stage
    CHECK_FALSE (mod::compatible (s, Src::env1, D::drive));   // a per-voice envelope on a shared stage
    CHECK (mod::compatible (s, Src::env1, D::cutoff));
    CHECK (mod::compatible (s, Src::lfo2, D::grainPositionB));
    s.routes[0] = { Src::lfo1, D::cutoff, 0.5, true };
    s.routes[1] = { Src::env1, D::cutoff, -0.25, true };
    s.routes[2] = { Src::env2, D::space, 1.0, true };      // invalid scope
    s.routes[3] = { Src::lfo1, D::panA, 0.7, false };     // bypassed
    s.routes[4] = { Src::lfo1, D::levelA, 0.0, true };    // depth 0
    CHECK (mod::routeState (s, s.routes[0]) == mod::RouteState::active);
    CHECK (mod::routeState (s, s.routes[2]) == mod::RouteState::scope);
    CHECK (mod::routeState (s, s.routes[3]) == mod::RouteState::bypassed);
    CHECK (mod::routeState (s, s.routes[4]) == mod::RouteState::empty);
    mod::Compiled c;
    c.compile (s);
    CHECK (c.any);
    CHECK (c.anyVoiceDest);
    CHECK_FALSE (c.anyGlobalDest);
    CHECK (c.has (D::cutoff));
    CHECK_FALSE (c.has (D::space));
    CHECK_FALSE (c.has (D::panA));
    std::array<float, mod::sourceCount> values { 1.0f, 0.0f, 0.8f, 0.0f };
    CHECK (c.sum (D::cutoff, values) == Approx (0.5 * 1.0 - 0.25 * 0.8));
    mod::VoiceState v;
    mod::Runtime rt;
    rt.setSettings (s);
    v.values = values;
    CHECK (v.offset (rt, D::cutoff) == Approx (4.0 * 0.3));   // span 4 octaves
    // Every destination has an id and a name; per-layer ones name their layer.
    for (int d = 1; d < mod::destCount; ++d)
    {
        const auto& info = mod::destInfo (static_cast<D> (d));
        CHECK (std::string (info.id).size() > 2);
        CHECK (info.span > 0.0);
    }
}

namespace
{
    constexpr double rate = 48000.0;

    std::shared_ptr<InstrumentModel> sineModel (double hz)
    {
        auto audio = testsignals::sine (hz, 2.0, rate, 0.4, 1);
        testsignals::applyFades (audio, 0.01, 0.05);
        return instrument::buildComplete (audio, test::analyse (audio), {}, false);
    }

    EngineSettings quietSettings()
    {
        EngineSettings s;
        s.macros.life = 0.0;
        s.macros.space = 0.0;
        s.macros.motion = 0.0;
        s.macros.reimagined = 0.0;
        s.shaping = Shaping::neutral();
        s.adsr.releaseSeconds = 0.1;
        return s;
    }

    std::vector<float> renderNotes (InstrumentEngine& engine, std::initializer_list<int> notes, double seconds, int block)
    {
        std::vector<float> l (static_cast<std::size_t> (block)), r (static_cast<std::size_t> (block)), out;
        for (int note : notes)
            engine.noteOn (note, 100);
        const auto total = static_cast<int> (seconds * rate);
        for (int done = 0; done < total; done += block)
        {
            const int n = std::min (block, total - done);
            float* ch[2] = { l.data(), r.data() };
            engine.render (ch, 2, n);
            out.insert (out.end(), l.begin(), l.begin() + n);
        }
        return out;
    }
}

TEST_CASE ("mod: the engine - no route is exactly no modulation; routes are heard, deterministic and block-size independent", "[unit][mod]")
{
    const auto model = sineModel (220.0);
    auto settings = quietSettings();
    settings.shaping.filterType = FilterType::lp24;
    settings.macros.character = 0.5;

    auto run = [&] (const mod::Settings* m, int block) {
        InstrumentEngine e;
        e.prepare (rate, block, settings);
        e.setModel (model.get());
        if (m != nullptr)
            e.setModulation (*m);
        return renderNotes (e, { 57, 64 }, 1.5, block);
    };
    const auto plain = run (nullptr, 256);
    mod::Settings empty;
    CHECK (run (&empty, 256) == plain);   // settings without routes change nothing

    mod::Settings m;
    m.lfo[0].rateHz = 4.0;
    m.routes[0] = { mod::Source::lfo1, mod::Dest::cutoff, 0.6, true };
    const auto wobble = run (&m, 256);
    CHECK (wobble != plain);
    CHECK (run (&m, 256) == wobble);   // deterministic
    // A global LFO follows at control rate inside blocks: 32-sample and 512-sample blocks agree.
    const auto small = run (&m, 32), large = run (&m, 512);
    double worst = 0.0;
    for (std::size_t i = 0; i < small.size(); ++i)
        worst = std::max (worst, static_cast<double> (std::abs (small[i] - large[i])));
    CHECK (worst < 1.0e-4);

    // A route to a shared stage (SPACE) and a bypassed route.
    mod::Settings shared;
    shared.routes[0] = { mod::Source::lfo1, mod::Dest::space, 0.8, true };
    CHECK (run (&shared, 256) != plain);
    shared.routes[0].enabled = false;
    CHECK (run (&shared, 256) == plain);
}

TEST_CASE ("mod: per-voice envelopes are independent per note; poly LFOs per voice", "[unit][mod]")
{
    const auto model = sineModel (220.0);
    auto settings = quietSettings();
    InstrumentEngine e;
    e.prepare (rate, 256, settings);
    e.setModel (model.get());
    mod::Settings m;
    m.env[0].attackSeconds = 0.5;
    m.env[0].decaySeconds = 0.1;
    m.env[0].sustain = 0.2;
    m.routes[0] = { mod::Source::env1, mod::Dest::levelA, -1.0, true };
    e.setModulation (m);
    std::vector<float> l (256), r (256);
    float* ch[2] = { l.data(), r.data() };
    e.noteOn (57, 100);
    for (int i = 0; i < 40; ++i)   // ~0.21 s: the first note's envelope is well into its attack
        e.render (ch, 2, 256);
    e.noteOn (64, 100);
    for (int i = 0; i < 2; ++i)
        e.render (ch, 2, 256);
    // Two voices, two envelopes: the newer one has just begun.
    const auto view = e.modulationView();
    CHECK (view.voice);
    CHECK (view.value[2] < 0.1f);   // the newest note's ENV 1 is at its start
    CHECK (view.envStage[0] == static_cast<int> (mod::EnvState::Stage::attack));
    // A global route from a per-voice envelope to a shared stage is refused by the rules.
    m.routes[1] = { mod::Source::env1, mod::Dest::drive, 1.0, true };
    e.setModulation (m);
    CHECK_FALSE (e.modulation().compiled.anyGlobalDest);
}
