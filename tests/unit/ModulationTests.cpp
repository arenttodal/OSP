// MODULATION: LFO shapes, phase and sync, one-shot, envelopes, the route rules (scope,
// sign, sum, bypass), and the engine following the routes (shared stages and per-voice
// destinations), deterministic and identical to the unmodulated engine without routes.

#include "audio/utility/TestSignals.h"
#include "engine/InstrumentBuilder.h"
#include "engine/InstrumentEngine.h"
#include "engine/Modulation.h"
#include "engine/Shaping.h"
#include "research/RenderConfig.h"
#include "support/TestHelpers.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
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
    // An envelope reaches a shared stage through its global instance (it used to be refused).
    CHECK (mod::compatible (s, Src::env1, D::drive));
    CHECK (mod::compatible (s, Src::env2, D::echo));
    CHECK (mod::compatible (s, Src::env1, D::cutoff));
    // The wheel reaches everything; START takes no envelope (it has no value as a note starts);
    // an envelope's own times take the LFOs and the wheel, never an envelope.
    CHECK (mod::compatible (s, Src::modWheel, D::space));
    CHECK (mod::compatible (s, Src::modWheel, D::startB));
    CHECK (mod::compatible (s, Src::lfo1, D::startA));
    CHECK_FALSE (mod::compatible (s, Src::env1, D::startA));
    CHECK (mod::compatible (s, Src::lfo1, D::env1Attack));
    CHECK (mod::compatible (s, Src::modWheel, D::env2Release));
    CHECK_FALSE (mod::compatible (s, Src::env1, D::env1Attack));
    CHECK_FALSE (mod::compatible (s, Src::env2, D::env1Sustain));
    CHECK_FALSE (mod::createsCycle (s, Src::lfo1, D::env1Attack));
    CHECK (mod::createsCycle (s, Src::env1, D::env1Decay));   // a source on its own setting
    CHECK (mod::compatible (s, Src::lfo2, D::grainPositionB));
    s.routes[0] = { Src::lfo1, D::cutoff, 0.5, true };
    s.routes[1] = { Src::env1, D::cutoff, -0.25, true };
    s.routes[2] = { Src::lfo2, D::space, 1.0, true };      // invalid scope (a poly LFO on a shared stage)
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
    std::array<float, mod::sourceCount> values { 1.0f, 0.0f, 0.8f, 0.0f, 0.0f };
    CHECK (c.sum (D::cutoff, values) == Approx (0.5 * 1.0 - 0.25 * 0.8));
    mod::VoiceState v;
    mod::Runtime rt;
    rt.setSettings (s);
    v.values = values;
    CHECK (v.offset (rt, D::cutoff) == Approx (1.0 * 0.3));   // span: the filter's whole travel
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
    // An envelope on a shared stage runs its global instance (every note restarts it).
    m.routes[1] = { mod::Source::env1, mod::Dest::drive, 1.0, true };
    e.setModulation (m);
    CHECK (e.modulation().compiled.anyGlobalDest);
}

TEST_CASE ("mod: a global LFO keeps running - after ONE SHOT, without voices, across transport changes and repeated keys", "[unit][mod][regression]")
{
    // The reported failure: a global LFO that once ran as ONE SHOT stood still after being
    // switched (or a preset loaded) to FREE, because the finished flag survived the change.
    mod::Settings s;
    s.lfo[0].rateHz = 4.0;
    s.lfo[0].mode = mod::LfoMode::oneShot;
    s.routes[0] = { mod::Source::lfo1, mod::Dest::drive, 0.5, true };
    mod::Runtime rt;
    rt.setSettings (s);
    for (int i = 0; i < 200; ++i)   // one second: the one-shot cycle is long done
        rt.advanceGlobal (240, rate);
    CHECK (rt.globalLfo[0].finished);
    s.lfo[0].mode = mod::LfoMode::free;
    rt.setSettings (s);
    auto moves = [&rt] (double seconds) {
        float lo = 2.0f, hi = -2.0f;
        for (int i = 0; i < static_cast<int> (seconds * rate / 240.0); ++i)
        {
            rt.advanceGlobal (240, rate);
            lo = std::min (lo, rt.globalValue[0]);
            hi = std::max (hi, rt.globalValue[0]);
        }
        return hi - lo;
    };
    CHECK (moves (0.5) > 1.5f);   // runs again: a full sine swing within two cycles
    CHECK_FALSE (rt.globalLfo[0].finished);

    // No voice is needed, and the transport starting, stopping, jumping and losing its
    // position never stops it (synced: it follows the song, then runs on at the tempo).
    s.lfo[0].sync = true;
    s.lfo[0].division = 2;   // 1/8
    rt.setSettings (s);
    for (int round = 0; round < 20; ++round)
    {
        HostTiming t;
        t.valid = round % 3 != 2;
        t.playing = round % 2 == 0;
        t.bpm = 90.0 + 10.0 * round;
        t.ppq = 7.25 * round;
        rt.setTiming (t);
        CHECK (moves (0.3) > 0.5f);
    }

    // RETRIGGER restarts after silence even when a key was pressed twice without its
    // note-off (a held count that drifted up used to keep it from restarting).
    s.lfo[0].sync = false;
    s.lfo[0].mode = mod::LfoMode::oneShot;
    rt.setSettings (s);
    rt.noteStarted (60, 1);
    rt.noteStarted (60, 1);   // the same key again
    rt.noteEnded (60, 1);
    CHECK (rt.heldNotes == 0);
    for (int i = 0; i < 200; ++i)
        rt.advanceGlobal (240, rate);
    CHECK (rt.globalLfo[0].finished);
    rt.noteStarted (62, 1);
    CHECK_FALSE (rt.globalLfo[0].finished);   // the next note after silence restarts it
}

TEST_CASE ("mod: ENV on a shared stage - one global envelope, restarted by each note, released by the last", "[unit][mod]")
{
    mod::Settings s;
    s.env[0].attackSeconds = 0.05;
    s.env[0].decaySeconds = 0.05;
    s.env[0].sustain = 0.5;
    s.env[0].releaseSeconds = 0.05;
    s.routes[0] = { mod::Source::env1, mod::Dest::drive, 1.0, true };
    mod::Runtime rt;
    rt.setSettings (s);
    auto run = [&rt] (double seconds) {
        for (int i = 0; i < static_cast<int> (seconds * rate / 32.0); ++i)
            rt.advanceGlobal (32, rate);
        return rt.sharedValue[2];
    };
    CHECK (run (0.1) == 0.0f);   // silence: nothing
    rt.noteStarted (60, 1);
    CHECK (run (0.03) > 0.3f);   // attacking
    CHECK (run (0.3) == Approx (0.5f));   // sustaining
    CHECK (rt.globalValue[2] == 0.0f);    // voices never see it: their envelopes are their own
    rt.noteStarted (64, 1);               // a second note restarts it from where it is (no jump)
    const float at = run (0.0);
    CHECK (at == Approx (0.5f).margin (0.02));
    CHECK (run (0.04) > 0.6f);
    rt.noteEnded (60, 1);                 // one key still down: it holds
    CHECK (run (0.3) == Approx (0.5f));
    rt.setPedal (true);
    rt.noteEnded (64, 1);                 // the pedal keeps it
    CHECK (run (0.2) == Approx (0.5f));
    rt.setPedal (false);                  // now it releases
    CHECK (run (0.2) < 0.01f);

    // In the engine: audible, deterministic, and only with the route.
    const auto model = sineModel (220.0);
    auto settings = quietSettings();
    auto render = [&] (bool routed) {
        InstrumentEngine e;
        e.prepare (rate, 256, settings);
        e.setModel (model.get());
        mod::Settings m;
        m.env[0].attackSeconds = 0.2;
        if (routed)
            m.routes[0] = { mod::Source::env1, mod::Dest::drive, 1.0, true };
        e.setModulation (m);
        return renderNotes (e, { 57, 64 }, 1.0, 256);
    };
    const auto plain = render (false), driven = render (true);
    CHECK (driven != plain);
    CHECK (render (true) == driven);
}

TEST_CASE ("mod: full depth reaches a knob's whole range - LEVEL from silence to its top, times within their knobs", "[unit][mod]")
{
    // Every span is its knob's whole range.
    CHECK (mod::destInfo (mod::Dest::levelB).span == 54.0);
    CHECK (mod::destInfo (mod::Dest::fineTuneA).span == 4800.0);
    CHECK (mod::destInfo (mod::Dest::panA).span == 2.0);
    CHECK (mod::destInfo (mod::Dest::startA).span == 1.0);
    CHECK (mod::destInfo (mod::Dest::grainPositionC).span == 1.0);
    CHECK (mod::destInfo (mod::Dest::cutoff).span == 1.0);
    CHECK (std::exp2 (mod::destInfo (mod::Dest::grainDensityA).span) == Approx (10.0));
    CHECK (std::exp2 (mod::destInfo (mod::Dest::grainSizeA).span) == Approx (20.0));
    CHECK (std::exp2 (mod::destInfo (mod::Dest::eqBellFrequencyA).span) == Approx (1000.0));
    CHECK (mod::destInfo (mod::Dest::eqBellGainA).span == 36.0);
    CHECK (std::exp2 (mod::destInfo (mod::Dest::ampRelease).span) == Approx (750.0));
    CHECK (std::exp2 (mod::destInfo (mod::Dest::env1Decay).span) == Approx (10000.0));
    // Times stop at their knobs' ends; a time at 0 stays 0.
    CHECK (mod::modulatedSeconds (mod::Dest::ampAttack, 1.5, 4.0) == 2.0);
    CHECK (mod::modulatedSeconds (mod::Dest::env1Release, 0.5, -20.0) == Approx (0.001));
    CHECK (mod::modulatedSeconds (mod::Dest::ampAttack, 0.0, 10.0) == 0.0);
    CHECK (mod::modulatedSeconds (mod::Dest::ampDecay, 0.5, 1.0) == Approx (1.0));
    // The old spans, for older sessions.
    CHECK (mod::legacySpan (mod::Dest::levelA) == 24.0);
    CHECK (mod::legacySpan (mod::Dest::fineTuneC) == 100.0);
    CHECK (mod::legacySpan (mod::Dest::env2Decay) == 4.0);
    CHECK (mod::legacySpan (mod::Dest::space) == 1.0);

    const auto model = sineModel (220.0);
    auto peakAt = [&] (double levelDb, double depth, float wheel) {
        auto settings = quietSettings();
        settings.layer[0].levelDb = levelDb;
        InstrumentEngine e;
        e.prepare (rate, 256, settings);
        e.setModel (model.get());
        mod::Settings m;
        if (depth != 0.0)
            m.routes[0] = { mod::Source::modWheel, mod::Dest::levelA, depth, true };
        e.setModulation (m);
        e.setModWheel (wheel);
        const auto out = renderNotes (e, { 57 }, 0.5, 256);
        double p = 0.0;
        for (std::size_t i = out.size() / 2; i < out.size(); ++i)
            p = std::max (p, static_cast<double> (std::abs (out[i])));
        return p;
    };
    const double top = peakAt (6.0, 0.0, 0.0f);
    REQUIRE (top > 0.01);
    // The report: from -39.3 dB, +100 % takes LEVEL to its top (+6 dB), not 24 dB higher.
    CHECK (peakAt (-39.3, 1.0, 1.0f) / top == Approx (1.0).margin (0.02));
    CHECK (peakAt (-39.3, 0.5, 1.0f) / top == Approx (std::pow (10.0, (-39.3 + 27.0 - 6.0) / 20.0)).margin (0.003));
    // Never past the top: from 0 dB, +100 % is +6 dB.
    CHECK (peakAt (0.0, 1.0, 1.0f) / top == Approx (1.0).margin (0.02));
    // From the knob's bottom (silence) a route lifts it all the way; down there it is silent.
    CHECK (peakAt (-48.0, 0.0, 0.0f) == 0.0);
    CHECK (peakAt (-48.0, 1.0, 0.0f) < 1.0e-9);
    CHECK (peakAt (-48.0, 1.0, 1.0f) / top == Approx (1.0).margin (0.02));
    // ... and down to silence from anywhere.
    CHECK (peakAt (0.0, -1.0, 1.0f) < 1.0e-9);
}

TEST_CASE ("mod: MOD WHEEL - a smoothed 0..1 source for every destination", "[unit][mod]")
{
    mod::Settings s;
    s.routes[0] = { mod::Source::modWheel, mod::Dest::space, 1.0, true };
    mod::Runtime rt;
    rt.setSettings (s);
    rt.setModWheel (1.0f);
    rt.advanceGlobal (48, rate);   // 1 ms: still gliding (no step)
    CHECK (rt.globalValue[4] > 0.0f);
    CHECK (rt.globalValue[4] < 0.5f);
    for (int i = 0; i < 100; ++i)
        rt.advanceGlobal (48, rate);
    CHECK (rt.globalValue[4] == Approx (1.0f).margin (1.0e-3));
    CHECK (rt.sharedValue[4] == rt.globalValue[4]);
    CHECK (std::string (mod::sourceName (mod::Source::modWheel)) == "MOD WHEEL");

    const auto model = sineModel (220.0);
    auto settings = quietSettings();
    auto render = [&] (float wheel, bool routed) {
        InstrumentEngine e;
        e.prepare (rate, 256, settings);
        e.setModel (model.get());
        mod::Settings m;
        if (routed)
            m.routes[0] = { mod::Source::modWheel, mod::Dest::levelA, -12.0 / 54.0, true };   // LEVEL's whole range is 54 dB
        e.setModulation (m);
        e.setModWheel (wheel);
        return renderNotes (e, { 57 }, 0.5, 256);
    };
    CHECK (render (0.0f, true) == render (0.0f, false));   // wheel down: as without the route
    const auto up = render (1.0f, true);
    double peakUp = 0.0, peakDown = 0.0;
    const auto down = render (0.0f, true);
    for (std::size_t i = up.size() / 2; i < up.size(); ++i)
    {
        peakUp = std::max (peakUp, static_cast<double> (std::abs (up[i])));
        peakDown = std::max (peakDown, static_cast<double> (std::abs (down[i])));
    }
    CHECK (peakUp / peakDown == Approx (std::pow (10.0, -12.0 / 20.0)).margin (0.02));   // -12 dB at full wheel
}

TEST_CASE ("mod: START - taken as each note begins, never moving a sounding note", "[unit][mod]")
{
    // 223 Hz: 25 % of the 2 s recording is 111.5 cycles, so a moved START is heard (a phase flip).
    const auto model = sineModel (223.0);
    auto settings = quietSettings();
    auto render = [&] (double start, float wheel, float wheelLater, bool routed, bool reverse) {
        auto s = settings;
        s.layer[0].start = start;
        s.layer[0].reverse = reverse;
        InstrumentEngine e;
        e.prepare (rate, 256, s);
        e.setModel (model.get());
        mod::Settings m;
        if (routed)
            m.routes[0] = { mod::Source::modWheel, mod::Dest::startA, 0.25, true };   // full wheel: +25 % (100 % = the whole recording)
        e.setModulation (m);
        e.setModWheel (wheel);
        std::vector<float> l (256), r (256), out;
        float* ch[2] = { l.data(), r.data() };
        for (int i = 0; i < 40; ++i)   // the wheel settles (10 ms glide) before the note
            e.render (ch, 2, 256);
        e.noteOn (57, 100);
        for (int i = 0; i < 60; ++i)
        {
            if (i == 10)
                e.setModWheel (wheelLater);
            e.render (ch, 2, 256);
            out.insert (out.end(), l.begin(), l.end());
        }
        return out;
    };
    auto close = [] (const std::vector<float>& a, const std::vector<float>& b) {
        double worst = 0.0;
        for (std::size_t i = 0; i < a.size(); ++i)
            worst = std::max (worst, static_cast<double> (std::abs (a[i] - b[i])));
        return worst;
    };
    for (bool reverse : { false, true })
    {
        CAPTURE (reverse);
        // Full wheel = START 25 % further in: the same note as START 10 % + 25 % = 35 %.
        const auto modulated = render (0.10, 1.0f, 1.0f, true, reverse);
        const auto equivalent = render (0.35, 1.0f, 1.0f, false, reverse);
        CHECK (close (modulated, equivalent) < 2.0e-3);
        CHECK (close (modulated, render (0.10, 1.0f, 1.0f, false, reverse)) > 1.0e-2);   // it did move
        // Moving the wheel while the note sounds changes nothing of that note.
        CHECK (close (render (0.10, 1.0f, 0.0f, true, reverse), modulated) < 1.0e-6);
        // Bounded: START near the end with more on top stays inside the recording (finite, no read past it).
        const auto edge = render (0.95, 1.0f, 1.0f, true, reverse);
        for (float v : edge)
            CHECK (std::isfinite (v));
    }
}

TEST_CASE ("mod: LFO and wheel on the modulation envelopes' times - taken at note-on, no restart, no loop", "[unit][mod]")
{
    mod::Settings s;
    s.env[0].attackSeconds = 0.1;
    s.env[0].decaySeconds = 0.1;
    s.env[0].sustain = 0.5;
    s.env[0].releaseSeconds = 0.1;
    // 100 % moves a time across its knob's whole range (1 ms .. 10 s: log2 10000 octaves).
    const double octave = 1.0 / std::log2 (10000.0);
    s.routes[0] = { mod::Source::modWheel, mod::Dest::env1Attack, octave, true };    // +1 octave at full wheel
    s.routes[1] = { mod::Source::modWheel, mod::Dest::env1Sustain, -0.25, true };    // 0.5 -> 0.25
    s.routes[2] = { mod::Source::modWheel, mod::Dest::env1Release, -octave, true };  // halved
    mod::Runtime rt;
    rt.setSettings (s);
    rt.setModWheel (1.0f);
    for (int i = 0; i < 200; ++i)
        rt.advanceGlobal (48, rate);
    mod::VoiceState v;
    v.start (rt);
    CHECK (v.envNow[0].attackSeconds == Approx (0.2));
    CHECK (v.envNow[1].attackSeconds == s.env[1].attackSeconds);   // ENV 2 untouched
    // The wheel moves during the attack: the note keeps the time it took.
    rt.setModWheel (0.0f);
    for (int i = 0; i < 200; ++i)
        rt.advanceGlobal (48, rate);
    double seconds = 0.0;
    while (v.env[0].stage == mod::EnvState::Stage::attack && seconds < 1.0)
    {
        v.advance (rt, 0.001);
        seconds += 0.001;
    }
    CHECK (seconds == Approx (0.2).margin (0.002));
    // Sustain follows the wheel continuously (now down again: back to 0.5).
    for (int i = 0; i < 300; ++i)
        v.advance (rt, 0.001);
    CHECK (v.env[0].level == Approx (0.5).margin (1e-6));
    // An envelope may not drive an envelope's time: refused, so no loop can form.
    s.routes[3] = { mod::Source::env2, mod::Dest::env1Decay, 1.0, true };
    s.routes[4] = { mod::Source::env1, mod::Dest::env2Decay, 1.0, true };
    CHECK (mod::routeState (s, s.routes[3]) != mod::RouteState::active);
    CHECK (mod::routeState (s, s.routes[4]) != mod::RouteState::active);
    // Without a route to them the envelopes follow their knobs exactly as before.
    mod::Settings plain;
    mod::Runtime r2;
    r2.setSettings (plain);
    mod::VoiceState w;
    w.start (r2);
    CHECK (w.envNow[0].attackSeconds == plain.env[0].attackSeconds);
}

//==============================================================================
// Meta-modulation: a source moving another route's depth.

TEST_CASE ("mod: depth routes - one level, no self, the target's scope, no loop; states and compiled terms", "[unit][mod][meta]")
{
    using Src = mod::Source;
    using D = mod::Dest;
    using R = mod::DepthRule;
    mod::Settings s;
    s.routes[0] = { Src::env1, D::cutoff, 0.4, true };          // ENV 1 -> CHARACTER (each note's filter)
    s.routes[1] = { Src::lfo1, D::routeDepth, 0.2, true, 0 };   // LFO 1 -> its depth
    s.routes[2] = { Src::lfo2, D::drive, 0.5, true };           // LFO 2 -> DRIVE (a shared stage)
    s.routes[3] = { Src::lfo1, D::env1Attack, 0.3, true };      // LFO 1 -> ENV 1's attack
    CHECK (mod::depthRule (s, Src::lfo1, 0) == R::ok);
    CHECK (mod::depthRule (s, Src::modWheel, 0) == R::ok);
    CHECK (mod::depthRule (s, Src::env2, 0) == R::ok);           // a voice destination: any source
    CHECK (mod::depthRule (s, Src::env1, 0) == R::self);         // its own route's depth
    CHECK (mod::depthRule (s, Src::lfo2, 1) == R::nested);       // a depth route's depth: one level only
    CHECK (mod::depthRule (s, Src::lfo1, 5) == R::noRoute);      // an empty slot
    CHECK (mod::depthRule (s, Src::lfo1, -1) == R::noRoute);
    CHECK (mod::depthRule (s, Src::lfo1, 99) == R::noRoute);
    CHECK (mod::depthRule (s, Src::env1, 2) == R::ok);           // the global envelope on a shared stage
    CHECK (mod::depthRule (s, Src::env2, 3) == R::scope);        // an envelope never reaches an envelope's times
    CHECK (mod::depthRule (s, Src::env1, 3) == R::scope);
    s.lfo[0].scope = mod::Scope::poly;
    CHECK (mod::depthRule (s, Src::lfo1, 2) == R::scope);        // a poly LFO has no single value on DRIVE
    s.lfo[0].scope = mod::Scope::global;
    s.routes[4] = { Src::modWheel, D::startA, 0.5, true };
    CHECK (mod::depthRule (s, Src::env2, 4) == R::scope);        // no envelope value yet when a note starts
    CHECK_FALSE (mod::compatible (s, Src::lfo1, D::routeDepth)); // only through a route
    CHECK_FALSE (mod::createsCycle (s, Src::modWheel, D::cutoff));

    CHECK (mod::routeState (s, 1) == mod::RouteState::active);
    mod::Compiled c;
    c.compile (s);
    const auto& cutoff = c.terms[static_cast<std::size_t> (D::cutoff)];
    REQUIRE (c.termCount[static_cast<std::size_t> (D::cutoff)] == 1);
    CHECK (cutoff[0].metaCount == 1);
    CHECK (c.terms[static_cast<std::size_t> (D::drive)][0].metaCount == 0);   // only the targeted route
    CHECK (c.termCount[static_cast<std::size_t> (D::routeDepth)] == 0);         // never a destination itself
    CHECK (c.used[0]);

    // effective depth = clamp (0.4 + 0.2 x LFO 1); the depth then scales ENV 1.
    std::array<float, mod::sourceCount> v {};
    v[2] = 0.5f;   // ENV 1
    for (const float lfo : { -1.0f, 0.0f, 0.5f, 1.0f })
    {
        v[0] = lfo;
        CHECK (c.depthOf (cutoff[0], v) == Approx (0.4 + 0.2 * lfo).margin (1.0e-6));
        CHECK (c.sum (D::cutoff, v) == Approx ((0.4 + 0.2 * lfo) * 0.5).margin (1.0e-6));
    }
    // Clamped to -1..1; a negative amount inverts the influence; through zero to the other way.
    s.routes[1].depth = 1.0;
    s.routes[0].depth = 0.8;
    c.compile (s);
    v[0] = 1.0f;
    CHECK (c.depthOf (c.terms[static_cast<std::size_t> (D::cutoff)][0], v) == Approx (1.0));
    s.routes[1].depth = -1.0;
    c.compile (s);
    CHECK (c.depthOf (c.terms[static_cast<std::size_t> (D::cutoff)][0], v) == Approx (-0.2).margin (1.0e-6));
    v[0] = -1.0f;
    CHECK (c.depthOf (c.terms[static_cast<std::size_t> (D::cutoff)][0], v) == Approx (1.0));

    // A route at depth 0 still works while a depth route moves it; a depth route without its
    // route does nothing (and says so); bypassed, it stops.
    s.routes[0].depth = 0.0;
    CHECK (mod::routeState (s, 0) == mod::RouteState::active);
    s.routes[1].enabled = false;
    CHECK (mod::routeState (s, 0) == mod::RouteState::empty);
    CHECK (mod::routeState (s, 1) == mod::RouteState::bypassed);
    s.routes[1].enabled = true;
    s.routes[0] = {};
    CHECK (mod::routeState (s, 1) == mod::RouteState::noTarget);
    c.compile (s);
    CHECK (c.metaCount == 0);
    // A refused pairing (the self rule) is shown, never silently compiled.
    s.routes[0] = { Src::lfo1, D::cutoff, 0.4, true };
    CHECK (mod::routeState (s, 1) == mod::RouteState::scope);
    c.compile (s);
    CHECK (c.terms[static_cast<std::size_t> (D::cutoff)][0].metaCount == 0);

    // Several sources on one route's depth add up; several routes keep their own.
    mod::Settings m;
    m.routes[0] = { Src::env1, D::cutoff, 0.4, true };
    m.routes[1] = { Src::lfo2, D::cutoff, 0.3, true };
    m.routes[2] = { Src::lfo1, D::routeDepth, 0.2, true, 0 };
    m.routes[3] = { Src::modWheel, D::routeDepth, -0.1, true, 0 };
    c.compile (m);
    REQUIRE (c.termCount[static_cast<std::size_t> (D::cutoff)] == 2);
    const auto& t = c.terms[static_cast<std::size_t> (D::cutoff)];
    CHECK (t[0].metaCount == 2);
    CHECK (t[1].metaCount == 0);
    std::array<float, mod::sourceCount> w {};
    w[0] = 1.0f;   // LFO 1
    w[1] = 1.0f;   // LFO 2
    w[2] = 1.0f;   // ENV 1
    w[4] = 1.0f;   // wheel
    CHECK (c.depthOf (t[0], w) == Approx (0.5).margin (1.0e-6));   // 0.4 + 0.2 - 0.1
    CHECK (c.depthOf (t[1], w) == Approx (0.3).margin (1.0e-6));   // LFO 2's route untouched
}

TEST_CASE ("mod: depth routes are heard - the route's strength moves, the stored values never", "[unit][mod][meta]")
{
    const auto model = sineModel (220.0);
    auto settings = quietSettings();
    // ENV 1 (instant, held at 1) -> LEVEL A at -50 % (-12 dB); the wheel moves that depth.
    auto render = [&] (float wheel, double meta, bool withMeta, int block = 256) {
        InstrumentEngine e;
        e.prepare (rate, block, settings);
        e.setModel (model.get());
        mod::Settings m;
        m.env[0].attackSeconds = 0.0;
        m.env[0].decaySeconds = 0.0;
        m.env[0].sustain = 1.0;
        // In units of 24 dB (LEVEL's whole range is 54 dB): -50 % of them is -12 dB.
        const double k = 24.0 / 54.0;
        m.routes[0] = { mod::Source::env1, mod::Dest::levelA, -0.5 * k, true };
        if (withMeta)
            m.routes[5] = { mod::Source::modWheel, mod::Dest::routeDepth, meta * k, true, 0 };
        e.setModulation (m);
        e.setModWheel (wheel);
        auto out = renderNotes (e, { 57 }, 0.5, block);
        // Test C: what is stored stays as it was (the effective depth lives in the sum only).
        CHECK (e.modulation().settings.routes[0].depth == -0.5 * k);
        CHECK (e.modulation().settings.routes[5].depth == (withMeta ? meta * k : 0.0));
        return out;
    };
    auto peak = [] (const std::vector<float>& x) {
        double p = 0.0;
        for (std::size_t i = x.size() / 2; i < x.size(); ++i)
            p = std::max (p, static_cast<double> (std::abs (x[i])));
        return p;
    };
    const auto plain = render (0.0f, 0.0, false);
    const auto none = [&] {
        InstrumentEngine e;
        e.prepare (rate, 256, settings);
        e.setModel (model.get());
        return renderNotes (e, { 57 }, 0.5, 256);
    }();
    // The wheel down: the depth routes add nothing - exactly the route alone.
    CHECK (render (0.0f, 0.5, true) == plain);
    // Test A: +50 % at full wheel takes the depth from -50 % to 0: no level change at all.
    CHECK (peak (render (1.0f, 0.5, true)) / peak (none) == Approx (1.0).margin (0.01));
    CHECK (peak (plain) / peak (none) == Approx (std::pow (10.0, -12.0 / 20.0)).margin (0.02));
    // Test E: a negative amount deepens it: -100 % -> -24 dB.
    CHECK (peak (render (1.0f, -0.5, true)) / peak (none) == Approx (std::pow (10.0, -24.0 / 20.0)).margin (0.01));
    // Halfway: -50 % + 25 % = -25 % -> -6 dB.
    CHECK (peak (render (0.5f, 0.5, true)) / peak (none) == Approx (std::pow (10.0, -6.0 / 20.0)).margin (0.02));
    // Deterministic and the same in any block size.
    CHECK (render (1.0f, -0.5, true) == render (1.0f, -0.5, true));
    const auto small = render (1.0f, -0.5, true, 32), large = render (1.0f, -0.5, true, 512);
    double worst = 0.0;
    for (std::size_t i = 0; i < small.size(); ++i)
        worst = std::max (worst, static_cast<double> (std::abs (small[i] - large[i])));
    CHECK (worst < 1.0e-4);
}

TEST_CASE ("mod: a global LFO on a route's depth - a shared stage moves, another route does not, after reloads and transport", "[unit][mod][meta][regression]")
{
    // Test F and I on the shared stages: LFO 1 (global, FREE) moves the depth of LFO 2 -> SPACE
    // only; LFO 2 -> DRIVE keeps its own. The movement carries on with no keys, after ONE SHOT
    // was switched away, across transport starts and stops, and after settings are reloaded.
    mod::Settings s;
    s.lfo[0].rateHz = 2.0;
    s.lfo[1].shape = mod::LfoShape::pulse;   // LFO 2 holds +1 for half a cycle
    s.lfo[1].rateHz = 0.01;
    s.routes[0] = { mod::Source::lfo2, mod::Dest::space, 0.5, true };
    s.routes[1] = { mod::Source::lfo2, mod::Dest::drive, 0.5, true };
    s.routes[2] = { mod::Source::lfo1, mod::Dest::routeDepth, 0.4, true, 0 };
    mod::Runtime rt;
    rt.setSettings (s);
    auto spread = [&rt] (int blocks) {
        double lo = 9.0, hi = -9.0, driveLo = 9.0, driveHi = -9.0;
        for (int i = 0; i < blocks; ++i)
        {
            rt.advanceGlobal (480, rate);
            const double space = rt.compiled.sum (mod::Dest::space, rt.sharedValue);
            const double drive = rt.compiled.sum (mod::Dest::drive, rt.sharedValue);
            lo = std::min (lo, space);
            hi = std::max (hi, space);
            driveLo = std::min (driveLo, drive);
            driveHi = std::max (driveHi, drive);
        }
        CHECK (driveHi - driveLo < 1.0e-3);   // DRIVE's route is not touched
        return hi - lo;
    };
    CHECK (spread (100) == Approx (0.8).margin (0.05));   // depth 0.5 +/- 0.4, LFO 2 at +1
    rt.noteStarted (60, 1);
    rt.noteEnded (60, 1);
    CHECK (spread (100) > 0.7);
    // ONE SHOT, then FREE again (the old freeze): it runs on.
    s.lfo[0].mode = mod::LfoMode::oneShot;
    rt.setSettings (s);
    rt.noteStarted (61, 1);
    spread (200);
    s.lfo[0].mode = mod::LfoMode::free;
    rt.setSettings (s);
    CHECK (spread (100) > 0.7);
    // Transport starts and stops.
    HostTiming playing;
    playing.valid = true;
    playing.playing = true;
    playing.bpm = 120.0;
    playing.ppq = 8.0;
    rt.setTiming (playing);
    CHECK (spread (100) > 0.7);
    playing.playing = false;
    rt.setTiming (playing);
    CHECK (spread (100) > 0.7);
    // A preset reloaded: a fresh copy of the same settings.
    mod::Runtime reloaded;
    reloaded.setSettings (mod::Settings (s));
    rt.setSettings (s);
    CHECK (spread (100) > 0.7);
}

TEST_CASE ("mod: depth routes in render configs, by id or by place", "[unit][mod][meta]")
{
    const auto dir = std::filesystem::temp_directory_path() / "osp-meta-config-test";
    std::filesystem::create_directories (dir);
    const auto file = dir / "meta.json";
    {
        std::ofstream out (file);
        out << R"({ "schemaVersion": 1, "engine": "C", "modulation": { "routes": [
                    { "id": "env", "source": "env1", "dest": "cutoff", "depth": 0.4 },
                    { "source": "lfo2", "dest": "cutoff", "depth": 0.3 },
                    { "source": "lfo1", "dest": "routeDepth", "target": "env", "depth": 0.2 },
                    { "source": "mod wheel", "dest": "routeDepth", "target": 1, "depth": -0.5 },
                    { "source": "lfo2", "dest": "routeDepth", "target": "missing", "depth": 0.5 } ] } })";
    }
    std::string error;
    const auto config = research::loadRenderConfig (file, error);
    std::filesystem::remove_all (dir);
    REQUIRE (config.has_value());
    const auto& routes = config->modulation.routes;
    CHECK (routes[2].dest == mod::Dest::routeDepth);
    CHECK (routes[2].target == 0);
    CHECK (routes[3].source == mod::Source::modWheel);
    CHECK (routes[3].target == 1);
    CHECK (routes[4].target == -1);
    CHECK (routes[0].target == -1);
    CHECK (mod::routeState (config->modulation, 2) == mod::RouteState::active);
    CHECK (mod::routeState (config->modulation, 3) == mod::RouteState::active);
    CHECK (mod::routeState (config->modulation, 4) == mod::RouteState::noTarget);
}

TEST_CASE ("mod: many depth routes stay cheap and finite", "[unit][mod][meta]")
{
    // Test J: eight routes, each with a depth route at the extremes, against the same eight
    // without: finite, bounded, and not much dearer.
    const auto model = sineModel (220.0);
    auto settings = quietSettings();
    settings.shaping.filterType = FilterType::lp24;
    auto run = [&] (bool meta, double& seconds) {
        InstrumentEngine e;
        e.prepare (rate, 256, settings);
        e.setModel (model.get());
        mod::Settings m;
        m.lfo[0].rateHz = 7.0;
        m.lfo[1].rateHz = 0.3;
        m.lfo[1].scope = mod::Scope::poly;
        const std::array<mod::Dest, 8> dests { mod::Dest::cutoff, mod::Dest::levelA, mod::Dest::panA, mod::Dest::fineTuneA,
                                               mod::Dest::resonance, mod::Dest::drive, mod::Dest::space, mod::Dest::character };
        for (std::size_t i = 0; i < dests.size(); ++i)
        {
            const auto global = mod::destInfo (dests[i]).owner == mod::Owner::global;
            m.routes[i] = { global ? mod::Source::env1 : mod::Source::lfo2, dests[i], i % 2 == 0 ? 1.0 : -1.0, true };
            if (meta)
                m.routes[8 + i] = { i % 2 == 0 ? mod::Source::lfo1 : mod::Source::modWheel, mod::Dest::routeDepth, i % 2 == 0 ? 1.0 : -1.0, true, static_cast<int> (i) };
        }
        e.setModulation (m);
        e.setModWheel (1.0f);
        const auto begin = std::chrono::steady_clock::now();
        auto out = renderNotes (e, { 45, 52, 57, 60, 64, 69 }, 2.0, 256);
        seconds = std::chrono::duration<double> (std::chrono::steady_clock::now() - begin).count();
        return out;
    };
    double plainSeconds = 0.0, metaSeconds = 0.0;
    run (false, plainSeconds);
    const auto out = run (true, metaSeconds);
    run (false, plainSeconds);   // warmed up
    for (const float x : out)
        REQUIRE (std::isfinite (x));
    double peak = 0.0;
    for (const float x : out)
        peak = std::max (peak, static_cast<double> (std::abs (x)));
    CHECK (peak < 4.0);
    CHECK (metaSeconds < 2.0 * plainSeconds + 0.05);
}

//==============================================================================
// Final modulation system, stage 1: VEL, NOTE, CC 1-4, PRESSURE.

TEST_CASE ("mod: new sources are appended - saved route sources keep their meaning", "[unit][mod]")
{
    // Sessions save a route's source by its place: the first five never move.
    CHECK (static_cast<int> (mod::Source::lfo1) == 1);
    CHECK (static_cast<int> (mod::Source::env2) == 4);
    CHECK (static_cast<int> (mod::Source::modWheel) == 5);
    CHECK (static_cast<int> (mod::Source::vel) == 6);
    CHECK (static_cast<int> (mod::Source::pressure) == mod::sourceCount);
    CHECK (mod::sourceIndex (mod::Source::vel) == mod::slot::vel);
    CHECK (mod::sourceIndex (mod::Source::note) == mod::slot::note);
    CHECK (mod::sourceIndex (mod::Source::cc1) == mod::slot::cc1);
    CHECK (mod::sourceIndex (mod::Source::pressure) == mod::slot::pressure);
    CHECK (std::string (mod::sourceName (mod::Source::vel)) == "VEL");
    CHECK (std::string (mod::sourceName (mod::Source::cc3)) == "CC 3");
    CHECK (std::string (mod::sourceName (mod::Source::pressure)) == "PRESSURE");
}

TEST_CASE ("mod: VEL and NOTE - per note through their curves; shared stages follow the input key", "[unit][mod]")
{
    mod::Settings s;
    mod::Runtime rt;
    rt.setSettings (s);
    mod::VoiceState v;
    v.note = 72;
    v.velocity = 127;
    v.start (rt);
    CHECK (v.values[mod::slot::vel] == Approx (1.0f));
    CHECK (v.values[mod::slot::note] == Approx (72.0f / 127.0f).margin (1.0e-3));
    v.advance (rt, 0.5);   // held for the note's life
    CHECK (v.values[mod::slot::vel] == Approx (1.0f));

    // A curve shapes them (inverted: the hardest touch gives 0).
    mod::Curve inverted;
    inverted.count = 2;
    inverted.points[0] = { 0.0f, 1.0f, 0.0f };
    inverted.points[1] = { 1.0f, 0.0f, 0.0f };
    s.velCurve = mod::compileCurve (inverted);
    rt.setSettings (s);
    v.start (rt);
    CHECK (v.values[mod::slot::vel] == Approx (0.0f).margin (1.0e-3));

    // Shared stages: the latest input key; voices never read it from there.
    rt.setInputNote (48, 64);
    rt.advanceGlobal (32, rate);
    CHECK (rt.sharedValue[mod::slot::vel] == Approx (1.0f - 64.0f / 127.0f).margin (1.0e-2));
    CHECK (rt.sharedValue[mod::slot::note] == Approx (48.0f / 127.0f).margin (1.0e-3));
    CHECK (rt.globalValue[mod::slot::vel] == 0.0f);
    CHECK (rt.globalValue[mod::slot::note] == 0.0f);

    // Rules: per note, and on shared stages; they know their value as a note starts.
    for (const auto src : { mod::Source::vel, mod::Source::note })
    {
        CHECK (mod::isPolySource (s, src));
        CHECK (mod::compatible (s, src, mod::Dest::drive));
        CHECK (mod::compatible (s, src, mod::Dest::levelA));
        CHECK (mod::compatible (s, src, mod::Dest::startA));
        CHECK (mod::compatible (s, src, mod::Dest::env1Attack));
        CHECK_FALSE (mod::createsCycle (s, src, mod::Dest::env1Attack));
    }

    // In the engine: a soft note is quieter than a hard one with VEL -> LEVEL; NOTE moves a
    // high note's level differently from a low one's; no route renders as before.
    const auto model = sineModel (220.0);
    auto settings = quietSettings();
    auto render = [&] (int note, int velocity, mod::Source source, double depth) {
        InstrumentEngine e;
        e.prepare (rate, 256, settings);
        e.setModel (model.get());
        mod::Settings m;
        if (source != mod::Source::none)
            m.routes[0] = { source, mod::Dest::levelA, depth, true };
        e.setModulation (m);
        std::vector<float> l (256), r (256), out;
        e.noteOn (note, velocity);
        for (int done = 0; done < static_cast<int> (0.4 * rate); done += 256)
        {
            float* ch[2] = { l.data(), r.data() };
            e.render (ch, 2, 256);
            out.insert (out.end(), l.begin(), l.end());
        }
        return out;
    };
    auto peak = [] (const std::vector<float>& x) {
        double p = 0.0;
        for (std::size_t i = x.size() / 2; i < x.size(); ++i)
            p = std::max (p, static_cast<double> (std::abs (x[i])));
        return p;
    };
    // VEL -> LEVEL +12 dB at full velocity: the hard note gains 12 dB more than at velocity 0
    // would; compared at the same velocity with and without the route.
    const double withRoute = peak (render (57, 127, mod::Source::vel, 12.0 / 54.0));
    const double without = peak (render (57, 127, mod::Source::none, 0.0));
    CHECK (withRoute / without == Approx (std::pow (10.0, 6.0 / 20.0)).margin (0.05));   // +12 dB at 1, but LEVEL tops out at +6 dB
    CHECK (render (57, 127, mod::Source::vel, 12.0 / 54.0) == render (57, 127, mod::Source::vel, 12.0 / 54.0));
    const double soft = peak (render (57, 20, mod::Source::vel, -24.0 / 54.0)) / peak (render (57, 20, mod::Source::none, 0.0));
    const double hard = peak (render (57, 120, mod::Source::vel, -24.0 / 54.0)) / peak (render (57, 120, mod::Source::none, 0.0));
    CHECK (soft > hard * 2.0);   // -24 dB x velocity: the hard note is cut far more
    const double low = peak (render (45, 100, mod::Source::note, -24.0 / 54.0)) / peak (render (45, 100, mod::Source::none, 0.0));
    const double high = peak (render (81, 100, mod::Source::note, -24.0 / 54.0)) / peak (render (81, 100, mod::Source::none, 0.0));
    CHECK (low > high * 1.5);
}

TEST_CASE ("mod: CC 1-4 and PRESSURE - smoothed global sources, heard on every destination", "[unit][mod]")
{
    mod::Settings s;
    mod::Runtime rt;
    rt.setSettings (s);
    rt.setController (2, 1.0f);
    rt.setPressure (0.5f);
    rt.advanceGlobal (48, rate);   // 1 ms: gliding, never a step
    CHECK (rt.globalValue[mod::slot::cc1 + 2] > 0.0f);
    CHECK (rt.globalValue[mod::slot::cc1 + 2] < 0.5f);
    for (int i = 0; i < 100; ++i)
        rt.advanceGlobal (48, rate);
    CHECK (rt.globalValue[mod::slot::cc1 + 2] == Approx (1.0f).margin (1.0e-3));
    CHECK (rt.globalValue[mod::slot::pressure] == Approx (0.5f).margin (1.0e-3));
    CHECK (rt.globalValue[mod::slot::cc1] == 0.0f);
    CHECK (rt.sharedValue[mod::slot::pressure] == rt.globalValue[mod::slot::pressure]);
    rt.setController (9, 1.0f);   // no such slot: ignored
    rt.setController (-1, 1.0f);
    for (const auto src : { mod::Source::cc1, mod::Source::cc4, mod::Source::pressure })
    {
        CHECK_FALSE (mod::isPolySource (s, src));
        CHECK (mod::compatible (s, src, mod::Dest::space));
        CHECK (mod::compatible (s, src, mod::Dest::cutoff));
    }
    // A voice reads them as they move.
    mod::VoiceState v;
    v.start (rt);
    CHECK (v.values[mod::slot::cc1 + 2] == Approx (1.0f).margin (1.0e-3));

    const auto model = sineModel (220.0);
    auto settings = quietSettings();
    auto render = [&] (float value, bool routed) {
        InstrumentEngine e;
        e.prepare (rate, 256, settings);
        e.setModel (model.get());
        mod::Settings m;
        if (routed)
            m.routes[0] = { mod::Source::cc2, mod::Dest::levelA, -12.0 / 54.0, true };
        e.setModulation (m);
        e.setModController (1, value);
        return renderNotes (e, { 57 }, 0.5, 256);
    };
    CHECK (render (0.0f, true) == render (0.0f, false));
    CHECK (render (1.0f, true) != render (0.0f, true));
    auto pressure = [&] (float value) {
        InstrumentEngine e;
        e.prepare (rate, 256, settings);
        e.setModel (model.get());
        mod::Settings m;
        m.routes[0] = { mod::Source::pressure, mod::Dest::levelA, -12.0 / 54.0, true };
        e.setModulation (m);
        e.setModPressure (value);
        return renderNotes (e, { 57 }, 0.5, 256);
    };
    CHECK (pressure (1.0f) != pressure (0.0f));
}

TEST_CASE ("mod: VEL presets - SOFT, LINEAR and HARD are DYNAMICS' curves exactly; APPLY TO AMP uses the VEL curve", "[unit][mod][dynamics]")
{
    // Every velocity maps exactly as DYNAMICS' own curve did (older sessions sound the same).
    const std::array<std::pair<mod::CurvePreset, VelocityCurve>, 3> pairs { { { mod::CurvePreset::soft, VelocityCurve::soft },
                                                                             { mod::CurvePreset::linear, VelocityCurve::linear },
                                                                             { mod::CurvePreset::hard, VelocityCurve::hard } } };
    for (const auto& [preset, dynamics] : pairs)
    {
        // The plugin saves curves with 4 decimals: compare through that too.
        auto curve = mod::presetCurve (preset);
        for (int i = 0; i < curve.count; ++i)
            curve.points[static_cast<std::size_t> (i)].tension = std::round (curve.points[static_cast<std::size_t> (i)].tension * 1.0e4f) / 1.0e4f;
        const auto table = mod::compileCurve (curve);
        for (int v = 1; v <= 127; ++v)
        {
            CAPTURE (v, static_cast<int> (preset));
            const int viaCurve = std::clamp (static_cast<int> (std::lround (127.0 * mod::curveAt (table, v / 127.0))), 1, 127);
            CHECK (viaCurve == shaping::curvedVelocity (dynamics, v));
        }
        CHECK (mod::matchingPreset (mod::presetCurve (preset)) == static_cast<int> (preset));
    }
    for (int i = 0; i < mod::curvePresetCount; ++i)
    {
        auto c = mod::presetCurve (static_cast<mod::CurvePreset> (i));
        const auto before = c;
        c.normalise();
        CHECK (c == before);   // valid as they are
    }
    CHECK (mod::matchingPreset (mod::defaultLfoCurve()) == -1);

    // The engine: APPLY TO AMP with the HARD preset renders exactly as DYNAMICS HARD.
    const auto model = sineModel (220.0);
    auto render = [&] (VelocityCurve dynamics, bool viaVel, mod::CurvePreset preset) {
        auto settings = quietSettings();
        settings.shaping.velocityCurve = dynamics;
        InstrumentEngine e;
        e.prepare (rate, 256, settings);
        e.setModel (model.get());
        mod::Settings m;
        m.velocityToAmp = viaVel;
        m.velCurve = mod::compileCurve (mod::presetCurve (preset));
        e.setModulation (m);
        std::vector<float> l (256), r (256), out;
        e.noteOn (57, 70);
        for (int done = 0; done < static_cast<int> (0.3 * rate); done += 256)
        {
            float* ch[2] = { l.data(), r.data() };
            e.render (ch, 2, 256);
            out.insert (out.end(), l.begin(), l.end());
        }
        return out;
    };
    CHECK (render (VelocityCurve::linear, true, mod::CurvePreset::hard) == render (VelocityCurve::hard, false, mod::CurvePreset::linear));
    CHECK (render (VelocityCurve::hard, true, mod::CurvePreset::linear) == render (VelocityCurve::linear, false, mod::CurvePreset::linear));   // never both
    CHECK (render (VelocityCurve::linear, true, mod::CurvePreset::flat) != render (VelocityCurve::linear, false, mod::CurvePreset::flat));
}

// ---------------------------------------------------------------------------------------------
// DAMPEN (MOD-FINAL Stage 6): per voice, by key, after CHARACTER

namespace
{
    /** Energy above `hz` relative to the total (a crude spectral split by one-pole high-pass). */
    double highShare (const std::vector<float>& x, double hz)
    {
        const double a = std::exp (-2.0 * std::numbers::pi * hz / rate);
        double lp = 0.0, high = 0.0, total = 0.0;
        for (const float s : x)
        {
            lp = (1.0 - a) * s + a * lp;
            const double h = s - lp;
            high += h * h;
            total += static_cast<double> (s) * s;
        }
        return total > 0.0 ? high / total : 0.0;
    }

    std::shared_ptr<InstrumentModel> brightModel()
    {
        // A bright tone (a stack of harmonics) so the damping is measurable.
        auto audio = testsignals::sine (220.0, 2.0, rate, 0.1, 1);
        for (int h = 2; h <= 30; ++h)
        {
            const auto partial = testsignals::sine (220.0 * h, 2.0, rate, 0.1 / h, 1);
            for (std::size_t c = 0; c < audio.channels.size(); ++c)
                for (std::size_t i = 0; i < audio.channels[c].size(); ++i)
                    audio.channels[c][i] += partial.channels[c][i];
        }
        testsignals::applyFades (audio, 0.01, 0.05);
        return instrument::buildComplete (audio, test::analyse (audio), {}, false);
    }
}

TEST_CASE ("dampen: bypassed is exactly no DAMPEN; on, high keys lose their highs and low keys keep them", "[unit][mod][mod-final][dampen]")
{
    const auto model = brightModel();
    const auto settings = quietSettings();
    auto run = [&] (const dampen::Settings* d, int note, int block = 256, const mod::Settings* m = nullptr) {
        InstrumentEngine e;
        e.prepare (rate, block, settings);
        e.setModel (model.get());
        if (d != nullptr)
            e.setLayerDampen (0, *d);
        if (m != nullptr)
            e.setModulation (*m);
        return renderNotes (e, { note }, 0.8, block);
    };
    const auto plainHigh = run (nullptr, 96);
    auto off = dampen::defaultSettings();   // bypassed by default
    CHECK_FALSE (off.enabled);
    CHECK (run (&off, 96) == plainHigh);

    auto on = dampen::defaultSettings();
    on.enabled = true;
    // The default curve: nothing up to C4, full at the top.
    CHECK (dampen::noteDamping (on, 48) == 0.0);
    CHECK (dampen::noteDamping (on, 127) == Approx (1.0));
    CHECK (run (&on, 48) == run (nullptr, 48));   // a key the curve leaves alone: untouched
    const auto shelf = run (&on, 96);
    CHECK (shelf != plainHigh);
    CHECK (highShare (shelf, 4000.0) < 0.8 * highShare (plainHigh, 4000.0));
    on.mode = dampen::Mode::lowPass;
    const auto lowPass = run (&on, 96);
    CHECK (highShare (lowPass, 4000.0) < highShare (plainHigh, 4000.0));
    // AMOUNT 0: nothing; and deterministic, block-size independent.
    on.amount = 0.0;
    CHECK (run (&on, 96) == plainHigh);
    on.amount = 1.0;
    const auto small = run (&on, 96, 32), large = run (&on, 96, 512);
    double worst = 0.0;
    for (std::size_t i = 0; i < small.size(); ++i)
        worst = std::max (worst, static_cast<double> (std::abs (small[i] - large[i])));
    CHECK (worst < 1.0e-4);
    CHECK (run (&on, 96) == lowPass);

    // AMOUNT is a destination: the wheel at 0 through -100 % leaves the amount; a route at
    // -100 % from a source held at 1 takes it to nothing.
    mod::Settings m;
    m.routes[0] = { mod::Source::modWheel, mod::Dest::dampenAmountA, -1.0, true };
    {
        InstrumentEngine e;
        e.prepare (rate, 256, settings);
        e.setModel (model.get());
        e.setLayerDampen (0, on);
        e.setModulation (m);
        e.setModWheel (1.0f);
        const auto undamped = renderNotes (e, { 96 }, 0.8, 256);
        CHECK (highShare (undamped, 4000.0) > highShare (lowPass, 4000.0));
    }
}

TEST_CASE ("dampen: the filters - shelf at -18 dB, low-pass on a log scale, finite at every rate", "[unit][mod-final][dampen]")
{
    CHECK (dampen::lowPassHz (0.0, 2000.0) == Approx (20000.0));
    CHECK (dampen::lowPassHz (1.0, 2000.0) == Approx (2000.0));
    CHECK (dampen::lowPassHz (0.5, 2000.0) == Approx (std::sqrt (20000.0 * 2000.0)));
    for (double sr : { 22050.0, 44100.0, 96000.0, 192000.0 })
        for (auto mode : { dampen::Mode::softShelf, dampen::Mode::lowPass })
        {
            dampen::Filter f;
            f.setup (mode, sr, 1.0, 1000.0, 300.0);
            CHECK (f.active());
            // A tone far above the corner: the shelf takes 18 dB, the low-pass more.
            double in = 0.0, out = 0.0;
            for (int i = 0; i < 20000; ++i)
            {
                float l = static_cast<float> (std::sin (2.0 * std::numbers::pi * 0.48 * i)), r = l;   // near Nyquist
                const float x = l;
                f.process (l, r);
                if (i > 2000)
                {
                    in += x * x;
                    out += l * l;
                }
                CHECK (std::isfinite (l));
            }
            const double db = 10.0 * std::log10 (out / in);
            if (mode == dampen::Mode::softShelf)
                CHECK (db == Approx (-18.0).margin (1.5));
            else
                CHECK (db < -20.0);
            f.setup (mode, sr, 0.0, 3000.0, 2000.0);
            CHECK_FALSE (f.active());
        }
}

// ---------------------------------------------------------------------------------------------
// Voice economy (CPU step 1): two voices per key, inaudible voices retired

TEST_CASE ("voices: a key struck again and again under the pedal keeps at most two voices", "[unit][voices]")
{
    const auto model = sineModel (220.0);
    auto settings = quietSettings();
    settings.adsr.releaseSeconds = 2.0;
    std::vector<float> l (256), r (256);
    float* ch[2] = { l.data(), r.data() };
    for (int limit : { 2, 0 })
    {
        CAPTURE (limit);
        settings.voicesPerKey = limit;
        InstrumentEngine e;
        e.prepare (rate, 256, settings);
        e.setModel (model.get());
        e.setSustainPedal (true);
        for (int strike = 0; strike < 6; ++strike)
        {
            e.noteOn (57, 100);
            e.noteOff (57);
            for (int b = 0; b < 20; ++b)   // ~0.1 s between strikes
                e.render (ch, 2, 256);
        }
        // The steal fades (5 ms) are over: what still sounds.
        if (limit == 2)
            CHECK (e.activeVoiceCount (0) <= 2);
        else
            CHECK (e.activeVoiceCount (0) == 6);   // no limit: they pile up under the pedal
    }
}

TEST_CASE ("voices: a released voice nobody can hear ends; one that can be heard rings its release out", "[unit][voices]")
{
    auto settings = quietSettings();
    settings.adsr.releaseSeconds = 3.0;
    std::vector<float> l (256), r (256);
    float* ch[2] = { l.data(), r.data() };
    auto voicesAfterRelease = [&] (double amplitude, double retireDb, double seconds) {
        auto audio = testsignals::sine (220.0, 2.0, rate, amplitude, 1);
        testsignals::applyFades (audio, 0.01, 0.05);
        const auto model = instrument::buildComplete (audio, test::analyse (audio), {}, false);
        settings.retireBelowDb = retireDb;
        InstrumentEngine e;
        e.prepare (rate, 256, settings);
        e.setModel (model.get());
        e.noteOn (57, 100);
        for (int b = 0; b < 40; ++b)
            e.render (ch, 2, 256);
        e.noteOff (57);
        for (int b = 0; b < static_cast<int> (seconds * rate / 256); ++b)
            e.render (ch, 2, 256);
        return e.activeVoiceCount (0);
    };
    // A sound at -126 dBFS: under the threshold from the start, gone 0.25 s after its release.
    CHECK (voicesAfterRelease (1.0e-6, -90.0, 0.6) == 0);
    CHECK (voicesAfterRelease (1.0e-6, 0.0, 0.6) == 1);   // retiring off: it rings its 3 s release
    // An ordinary level: its release is heard, so it plays it.
    CHECK (voicesAfterRelease (0.4, -90.0, 0.6) == 1);
}

TEST_CASE ("mod: a product's own destinations follow ANDOR OSP's; without them the route choices are ANDOR OSP's", "[unit][mod]")
{
    using D = mod::Dest;
    // ANDOR OSP names none: every route choice is its destination, as saved sessions hold it.
    mod::setProductDestinations ({});
    CHECK (mod::productDestCount() == 0);
    CHECK (mod::hostChoiceCount() == mod::hostDestCount);
    for (int c = 0; c < mod::hostDestCount; ++c)
    {
        CHECK (static_cast<int> (mod::destFromChoice (c)) == c);
        CHECK (mod::choiceOf (static_cast<D> (c)) == c);
    }
    CHECK (mod::destFromChoice (mod::hostDestCount) == D::none);
    CHECK (mod::choiceOf (D::keyDnaAmountA) == -1);   // (engine-only)

    // A product's: after ANDOR OSP's, each its knob's whole travel, owned as it says.
    mod::setProductDestinations ({ { "product.cutoff", "CUTOFF", mod::Owner::voice, mod::Update::continuous },
                                   { "product.reverb", "REVERB SIZE", mod::Owner::global, mod::Update::continuous } });
    CHECK (mod::productDestCount() == 2);
    CHECK (mod::hostChoiceCount() == mod::hostDestCount + 2);
    CHECK (mod::destFromChoice (mod::hostDestCount) == mod::productDest (0));
    CHECK (mod::choiceOf (mod::productDest (1)) == mod::hostDestCount + 1);
    const auto& cutoff = mod::destInfo (mod::productDest (0));
    CHECK (std::string (cutoff.id) == "product.cutoff");
    CHECK (std::string (cutoff.name) == "CUTOFF");
    CHECK (cutoff.domain == mod::Domain::unit);
    CHECK (cutoff.span == 1.0);
    // The rules hold for them as for ANDOR OSP's: a POLY LFO reaches a voice's control only.
    mod::Settings s;
    s.lfo[0].scope = mod::Scope::poly;
    CHECK (mod::compatible (s, mod::Source::lfo1, mod::productDest (0)));
    CHECK_FALSE (mod::compatible (s, mod::Source::lfo1, mod::productDest (1)));
    CHECK (mod::compatible (s, mod::Source::env1, mod::productDest (1)));   // (the global envelope)
    // Routed, summed and compiled like any other.
    s.routes[0] = { mod::Source::lfo2, mod::productDest (0), 0.5, true, -1 };
    mod::Runtime rt;
    rt.setSettings (s);
    CHECK (rt.compiled.has (mod::productDest (0)));
    CHECK (rt.compiled.anyVoiceDest);
    // An unnamed slot is a placeholder no route can reach.
    CHECK (mod::destFromChoice (mod::hostDestCount + 2) == D::none);
    CHECK (std::string (mod::destInfo (mod::productDest (5)).id) == "product6");
    mod::setProductDestinations ({});
}

namespace
{
    /** The level of one frequency in a stretch of a render (its amplitude, by correlation). */
    double toneLevel (const std::vector<float>& x, double hz, double fromSeconds, double toSeconds)
    {
        const auto a = static_cast<std::size_t> (fromSeconds * rate), b = std::min (x.size(), static_cast<std::size_t> (toSeconds * rate));
        double s = 0.0, c = 0.0;
        for (std::size_t i = a; i < b; ++i)
        {
            const double w = 2.0 * std::numbers::pi * hz * static_cast<double> (i) / rate;
            s += x[i] * std::sin (w);
            c += x[i] * std::cos (w);
        }
        return 2.0 * std::sqrt (s * s + c * c) / static_cast<double> (std::max<std::size_t> (1, b - a));
    }
}

TEST_CASE ("mod: MIX - an LFO moves the blend between A and B, equal power; without a route nothing changes", "[unit][mod][mix]")
{
    // Each layer plays the key; B an octave up, so the two can be told apart (220 and 440 Hz).
    const auto a = sineModel (220.0), b = sineModel (220.0);
    auto settings = quietSettings();
    settings.blend = 0.5;
    auto run = [&] (const mod::Settings* m, int block, std::vector<InstrumentEngine::LiveMix>* live = nullptr) {
        InstrumentEngine e;
        e.prepare (rate, block, settings);
        e.setModel (a.get(), 0);
        e.setModel (b.get(), 1);
        e.setLayerPitchOffsetSemitones (1, 12.0);
        if (m != nullptr)
            e.setModulation (*m);
        std::vector<float> l (static_cast<std::size_t> (block)), r (static_cast<std::size_t> (block)), out;
        e.noteOn (57, 100);
        const auto total = static_cast<int> (1.6 * rate);
        for (int done = 0; done < total; done += block)
        {
            const int n = std::min (block, total - done);
            float* ch[2] = { l.data(), r.data() };
            e.render (ch, 2, n);
            out.insert (out.end(), l.begin(), l.begin() + n);
            if (live != nullptr)
                live->push_back (e.liveMix());
        }
        return out;
    };
    const auto plain = run (nullptr, 256);
    // A MIX route that is bypassed changes nothing (the mix keeps its 20 ms glide, bit for bit).
    mod::Settings off;
    off.lfo[0].rateHz = 3.0;
    off.routes[0] = { mod::Source::lfo1, mod::Dest::mixPosition, 0.5, false };
    CHECK (run (&off, 256) == plain);

    // A slow square LFO: +1 for the first second, -1 after. The blend goes to B, then to A.
    mod::Settings m;
    m.lfo[0].shape = mod::LfoShape::pulse;
    m.lfo[0].rateHz = 0.5;
    m.routes[0] = { mod::Source::lfo1, mod::Dest::mixPosition, 0.5, true };
    std::vector<InstrumentEngine::LiveMix> live;
    const auto moved = run (&m, 256, &live);
    CHECK (run (&m, 256) == moved);   // deterministic
    const auto at = [&] (double seconds) { return live[static_cast<std::size_t> (seconds * rate / 256.0)]; };
    CHECK (at (0.5).modulated);
    CHECK (at (0.5).blend == Approx (1.0).margin (1.0e-3));
    CHECK (at (1.4).blend == Approx (0.0).margin (1.0e-3));
    CHECK (toneLevel (moved, 440.0, 0.3, 0.6) > 20.0 * toneLevel (moved, 220.0, 0.3, 0.6));   // B only
    CHECK (toneLevel (moved, 220.0, 1.2, 1.5) > 20.0 * toneLevel (moved, 440.0, 1.2, 1.5));   // A only
    // Equal power at every point it passes through (the same weights as the control's own).
    for (const auto& p : live)
    {
        const auto w = InstrumentEngine::mixWeights ({ true, true, false }, p.blend, p.x, p.y);
        CHECK (w.gain[0] * w.gain[0] + w.gain[1] * w.gain[1] == Approx (1.0).margin (1.0e-6));
    }
    // The stored control never moves; with the route bypassed again it is back at the base.
    InstrumentEngine e;
    e.prepare (rate, 256, settings);
    CHECK (e.liveMix().blend == Approx (0.5));
    CHECK_FALSE (e.liveMix().modulated);
}

TEST_CASE ("mod: MIX - a 20 Hz LFO keeps its shape without clicks, at any block size", "[unit][mod][mix]")
{
    const auto a = sineModel (220.0), b = sineModel (220.0);
    auto settings = quietSettings();
    settings.blend = 0.5;
    mod::Settings m;
    m.lfo[0].rateHz = 20.0;
    m.routes[0] = { mod::Source::lfo1, mod::Dest::mixPosition, 0.5, true };
    auto run = [&] (int block, bool routed) {
        InstrumentEngine e;
        e.prepare (rate, block, settings);
        e.setModel (a.get(), 0);
        e.setModel (b.get(), 1);
        e.setLayerPitchOffsetSemitones (1, 12.0);   // B at 440 Hz
        if (routed)
            e.setModulation (m);
        std::vector<float> l (static_cast<std::size_t> (block)), r (static_cast<std::size_t> (block)), out;
        e.noteOn (57, 100);
        for (int done = 0; done < static_cast<int> (1.0 * rate); done += block)
        {
            float* ch[2] = { l.data(), r.data() };
            e.render (ch, 2, block);
            out.insert (out.end(), l.begin(), l.end());
        }
        return out;
    };
    auto worstStep = [] (const std::vector<float>& x) {
        double worst = 0.0;
        for (std::size_t i = static_cast<std::size_t> (0.05 * rate); i < x.size(); ++i)
            worst = std::max (worst, static_cast<double> (std::abs (x[i] - x[i - 1])));
        return worst;
    };
    const auto plain = run (256, false), wobble = run (256, true);
    CHECK (wobble != plain);
    // The gains ramp sample by sample: no step is larger than the tones' own.
    CHECK (worstStep (wobble) < 1.25 * worstStep (plain) + 1.0e-3);
    // The movement is the LFO's (20 Hz), not the 20 ms glide's smear: the 220 Hz tone's level
    // swings deeply within each cycle.
    double lo = 9.0, hi = 0.0;
    for (double t = 0.5; t < 0.6; t += 0.005)
    {
        const double level = toneLevel (wobble, 220.0, t, t + 0.01);
        lo = std::min (lo, level);
        hi = std::max (hi, level);
    }
    CHECK (hi > 2.0 * lo);
    const auto small = run (32, true), large = run (512, true);
    double worst = 0.0;
    for (std::size_t i = 0; i < std::min (small.size(), large.size()); ++i)
        worst = std::max (worst, static_cast<double> (std::abs (small[i] - large[i])));
    CHECK (worst < 1.0e-3);
}

TEST_CASE ("mod: MIX TOWARDS C - with three layers the node moves towards C, and MIX along A -> B", "[unit][mod][mix]")
{
    const auto a = sineModel (220.0), b = sineModel (330.0), c = sineModel (495.0);
    auto settings = quietSettings();
    settings.mixX = 0.25;   // the middle of A-B: C silent
    settings.mixY = 0.5;
    auto liveAfter = [&] (mod::Dest dest, double depth) {
        mod::Settings m;
        m.lfo[0].shape = mod::LfoShape::pulse;   // +1 for the first second
        m.lfo[0].rateHz = 0.5;
        m.routes[0] = { mod::Source::lfo1, dest, depth, true };
        InstrumentEngine e;
        e.prepare (rate, 256, settings);
        e.setModel (a.get(), 0);
        e.setModel (b.get(), 1);
        e.setModel (c.get(), 2);
        e.setModulation (m);
        e.noteOn (57, 100);
        std::vector<float> l (256), r (256);
        for (int done = 0; done < static_cast<int> (0.4 * rate); done += 256)
        {
            float* ch[2] = { l.data(), r.data() };
            e.render (ch, 2, 256);
        }
        return e.liveMix();
    };
    // All the way towards C: the corner (1, 0).
    const auto towards = liveAfter (mod::Dest::mixTowardsC, 1.0);
    CHECK (towards.x == Approx (1.0).margin (1.0e-3));
    CHECK (towards.y == Approx (0.0).margin (1.0e-3));
    CHECK (InstrumentEngine::triangleShares (towards.x, towards.y)[2] == Approx (1.0).margin (1.0e-3));
    // MIX: from the middle of A-B halfway towards B.
    const auto along = liveAfter (mod::Dest::mixPosition, 0.25);
    CHECK (along.x == Approx (0.375).margin (1.0e-3));
    CHECK (along.y == Approx (0.75).margin (1.0e-3));
    const auto shares = InstrumentEngine::triangleShares (along.x, along.y);
    CHECK (shares[1] > shares[0]);
    CHECK (shares[2] == Approx (0.0).margin (1.0e-3));
}
