// SATELLITE in ANDOR OSP's engine (src/engine/SatelliteMotion.h, applied at each layer's voices'
// pan, as ANDOR ANALOG applies it to a card's whole voices): off is bit for bit, the place is
// where it is set, PAN travels at its rate, BOUNCE + NOTE alternates per note, switching off
// returns to the original output exactly, and a render repeats.

#include "audio/utility/TestSignals.h"
#include "engine/InstrumentBuilder.h"
#include "engine/InstrumentEngine.h"
#include "support/TestHelpers.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <functional>
#include <vector>

using namespace osp;
using Catch::Approx;

namespace
{
    constexpr double rate = 48000.0;
    constexpr int block = 256;

    struct Stereo
    {
        std::vector<float> l, r;
    };

    std::shared_ptr<InstrumentModel> sustained()
    {
        auto audio = testsignals::saw (220.0, 3.0, rate);
        testsignals::applyFades (audio, 0.01, 0.05);
        return instrument::buildComplete (audio, test::analyse (audio), {}, false);
    }

    EngineSettings neutral()
    {
        EngineSettings s;
        s.macros.life = 0.0;
        s.macros.space = 0.0;
        s.macros.motion = 0.0;
        s.macros.reimagined = 0.0;   // (KALEIDOSCOPE spreads the layer after its voices)
        s.shaping = Shaping::neutral();
        return s;
    }

    /** Plays `notes` (each `noteSeconds` long, one after another) and renders `seconds`. */
    Stereo run (const InstrumentModel& model, const EngineSettings& es, double seconds, std::vector<int> notes = { 57 }, double noteSeconds = 10.0,
                std::function<void (InstrumentEngine&, int)> at = {})
    {
        InstrumentEngine e;
        e.prepare (rate, block, es);
        e.setModel (&model);
        Stereo out;
        std::vector<float> l (block), r (block);
        const int total = static_cast<int> (seconds * rate);
        const int perNote = static_cast<int> (noteSeconds * rate);
        std::size_t next = 0;
        int playing = -1;
        for (int done = 0; done < total; done += block)
        {
            if (at)
                at (e, done);
            if (next < notes.size() && done >= static_cast<int> (next) * perNote)
            {
                if (playing >= 0)
                    e.noteOff (playing);
                playing = notes[next++];
                e.noteOn (playing, 100);
            }
            float* ch[2] = { l.data(), r.data() };
            e.render (ch, 2, block);
            out.l.insert (out.l.end(), l.begin(), l.end());
            out.r.insert (out.r.end(), r.begin(), r.end());
        }
        return out;
    }

    double rms (const std::vector<float>& x, std::size_t from, std::size_t to)
    {
        double e = 0.0;
        to = std::min (to, x.size());
        for (auto i = from; i < to; ++i)
            e += static_cast<double> (x[i]) * x[i];
        return std::sqrt (e / static_cast<double> (std::max<std::size_t> (1, to - from)));
    }

    /** Where the sound sits in a window: -1 all left .. +1 all right (by level). */
    double balance (const Stereo& s, double fromSeconds, double toSeconds)
    {
        const auto a = static_cast<std::size_t> (fromSeconds * rate), b = static_cast<std::size_t> (toSeconds * rate);
        const double l = rms (s.l, a, b), r = rms (s.r, a, b);
        return (r - l) / std::max (1.0e-12, r + l);
    }
}

TEST_CASE ("satellite: off is bit for bit (whatever its other settings)", "[unit][satellite]")
{
    const auto model = sustained();
    const auto plain = run (*model, neutral(), 1.0);
    auto settings = neutral();
    auto& st = settings.layer[0].satellite;
    st.mode = satellite::Mode::orbit;
    st.timing = satellite::Timing::free;
    st.rateHz = 3.0;
    st.depth = 1.0;
    st.offset = 0.4;
    st.width = 0.2;
    CHECK_FALSE (st.enabled);
    const auto off = run (*model, settings, 1.0);
    CHECK (off.l == plain.l);
    CHECK (off.r == plain.r);
}

TEST_CASE ("satellite: the place is where it is set - OFFSET hard right leaves nothing on the left", "[unit][satellite]")
{
    const auto model = sustained();
    auto settings = neutral();
    auto& st = settings.layer[0].satellite;
    st.enabled = true;
    st.depth = 0.0;
    st.offset = 1.0;
    const auto right = run (*model, settings, 1.0);
    const auto plain = run (*model, neutral(), 1.0);
    const auto a = static_cast<std::size_t> (0.2 * rate), b = static_cast<std::size_t> (0.9 * rate);
    CHECK (rms (right.l, a, b) < 1.0e-3 * rms (right.r, a, b));
    // Equal power: the right side carries the centre's power (+3 dB on one channel).
    CHECK (rms (right.r, a, b) == Approx (std::sqrt (2.0) * rms (plain.r, a, b)).epsilon (0.02));
    st.offset = -1.0;
    const auto left = run (*model, settings, 1.0);
    CHECK (rms (left.r, a, b) < 1.0e-3 * rms (left.l, a, b));
}

TEST_CASE ("satellite: PAN travels at its rate (FREE 1 Hz: right at a quarter, left at three quarters)", "[unit][satellite]")
{
    const auto model = sustained();
    auto settings = neutral();
    auto& st = settings.layer[0].satellite;
    st.enabled = true;
    st.mode = satellite::Mode::pan;
    st.timing = satellite::Timing::free;
    st.rateHz = 1.0;
    st.depth = 1.0;
    const auto s = run (*model, settings, 2.0);
    // (The local clock starts with the note; a cycle is one second.)
    CHECK (balance (s, 0.22, 0.28) > 0.8);
    CHECK (balance (s, 0.72, 0.78) < -0.8);
    CHECK (std::abs (balance (s, 0.48, 0.52)) < 0.2);
    CHECK (balance (s, 1.22, 1.28) > 0.8);   // the next cycle
    // DEPTH halves the travel.
    st.depth = 0.5;
    const auto half = run (*model, settings, 1.0);
    CHECK (balance (half, 0.22, 0.28) > 0.2);
    CHECK (balance (half, 0.22, 0.28) < balance (s, 0.22, 0.28));
}

TEST_CASE ("satellite: SYNC follows the host's quarter notes; ORBIT is quieter and darker behind", "[unit][satellite]")
{
    const auto model = sustained();
    auto settings = neutral();
    auto& st = settings.layer[0].satellite;
    st.enabled = true;
    st.mode = satellite::Mode::pan;
    st.timing = satellite::Timing::sync;
    st.division = 7;   // 1/4: one cycle a beat
    st.depth = 1.0;
    // 120 BPM, playing from beat 0: a beat is 0.5 s, so the right is at 0.125 s.
    const auto synced = run (*model, settings, 1.0, { 57 }, 10.0, [] (InstrumentEngine& e, int done) {
        HostTiming t;
        t.valid = t.playing = true;
        t.bpm = 120.0;
        t.ppq = done / rate * 2.0;
        e.setHostTiming (t);
    });
    CHECK (balance (synced, 0.10, 0.15) > 0.8);
    CHECK (balance (synced, 0.35, 0.40) < -0.8);
    // ORBIT: the same travel, its far half (cycle 0.5) quieter than its near half (cycle 0).
    st.mode = satellite::Mode::orbit;
    st.timing = satellite::Timing::free;
    st.rateHz = 0.5;
    const auto orbit = run (*model, settings, 2.0);
    auto level = [&] (double t0, double t1) {
        const auto a = static_cast<std::size_t> (t0 * rate), b = static_cast<std::size_t> (t1 * rate);
        return std::hypot (rms (orbit.l, a, b), rms (orbit.r, a, b));
    };
    CHECK (level (0.95, 1.05) < 0.8 * level (1.9, 2.0));   // behind (cycle 0.5) against the front (cycle 1.0)
    CHECK (level (0.95, 1.05) > 0.4 * level (1.9, 2.0));   // a cue, never a cut
}

TEST_CASE ("satellite: BOUNCE with NOTE - each new note takes the other side and keeps it", "[unit][satellite]")
{
    const auto model = sustained();
    auto settings = neutral();
    auto& st = settings.layer[0].satellite;
    st.enabled = true;
    st.mode = satellite::Mode::bounce;
    st.retrigger = satellite::Retrigger::note;
    st.depth = 1.0;
    const auto s = run (*model, settings, 1.6, { 57, 57, 57, 57 }, 0.4);
    CHECK (balance (s, 0.1, 0.35) > 0.9);    // the first note: right
    CHECK (balance (s, 0.5, 0.75) < -0.9);   // the second: left
    CHECK (balance (s, 0.9, 1.15) > 0.9);    // the third: right again
    CHECK (balance (s, 0.2, 0.35) == Approx (balance (s, 0.1, 0.2)).margin (0.05));   // kept for the note
}

TEST_CASE ("satellite: switched off, the layer returns to its original output exactly; renders repeat", "[unit][satellite]")
{
    const auto model = sustained();
    auto settings = neutral();
    auto& st = settings.layer[0].satellite;
    st.enabled = true;
    st.timing = satellite::Timing::free;
    st.rateHz = 2.0;
    st.depth = 1.0;
    const auto plain = run (*model, neutral(), 1.0);
    const auto moved = run (*model, settings, 1.0);
    CHECK (moved.l != plain.l);
    CHECK (run (*model, settings, 1.0).l == moved.l);   // deterministic
    // Off at 0.4 s: after its 20 ms glide (and the chunk it ends in) the samples are the original.
    const auto switched = run (*model, settings, 1.0, { 57 }, 10.0, [&] (InstrumentEngine& e, int done) {
        if (done == static_cast<int> (0.4 * rate) / block * block)
            e.setLayerSatellite (0, {});
    });
    const auto from = static_cast<std::size_t> (0.45 * rate);
    bool same = true;
    for (auto i = from; i < plain.l.size(); ++i)
        same = same && switched.l[i] == plain.l[i] && switched.r[i] == plain.r[i];
    CHECK (same);
    // ... and it glides, never jumps: no sample step at the switch larger than the signal's own.
    float maxStep = 0.0f, ownStep = 0.0f;
    for (auto i = static_cast<std::size_t> (0.38 * rate); i < from; ++i)
        maxStep = std::max (maxStep, std::abs (switched.l[i] - switched.l[i - 1]));
    for (auto i = static_cast<std::size_t> (0.5 * rate); i < static_cast<std::size_t> (0.6 * rate); ++i)
        ownStep = std::max (ownStep, std::abs (plain.l[i] - plain.l[i - 1]));
    CHECK (maxStep < 1.5f * ownStep);
}

TEST_CASE ("satellite: the engine reports where the layer is (the panel's marker)", "[unit][satellite]")
{
    const auto model = sustained();
    InstrumentEngine e;
    auto settings = neutral();
    e.prepare (rate, block, settings);
    e.setModel (model.get());
    float x = 0.0f, rear = 0.0f;
    CHECK_FALSE (e.satelliteView (0, x, rear));
    satellite::Settings st;
    st.enabled = true;
    st.depth = 0.0;
    st.offset = -0.5;
    e.setLayerSatellite (0, st);
    std::vector<float> l (block), r (block);
    float* ch[2] = { l.data(), r.data() };
    e.noteOn (57, 100);
    for (int i = 0; i < 20; ++i)
        e.render (ch, 2, block);
    CHECK (e.satelliteView (0, x, rear));
    CHECK (x == Approx (-0.5f).margin (0.01));
    CHECK_FALSE (e.satelliteView (1, x, rear));
}
