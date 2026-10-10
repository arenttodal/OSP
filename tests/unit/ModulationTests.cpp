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
            m.routes[0] = { mod::Source::modWheel, mod::Dest::levelA, -0.5, true };
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
    CHECK (peakUp / peakDown == Approx (std::pow (10.0, -12.0 / 20.0)).margin (0.02));   // -12 dB at full wheel (span 24 dB x 0.5)
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
            m.routes[0] = { mod::Source::modWheel, mod::Dest::startA, 0.5, true };   // full wheel: +25 %
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
    s.routes[0] = { mod::Source::modWheel, mod::Dest::env1Attack, 0.25, true };    // +1 octave at full wheel
    s.routes[1] = { mod::Source::modWheel, mod::Dest::env1Sustain, -0.25, true };  // 0.5 -> 0.25
    s.routes[2] = { mod::Source::modWheel, mod::Dest::env1Release, -0.25, true };  // halved
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
