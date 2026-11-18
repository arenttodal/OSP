// PEDAL LOOP x REIMAGINED (docs/research/pedal-reimagined): the sustain pedal's Forward and
// Ping-Pong loops hold a One Shot note in every REIMAGINED mode, at every amount.

#include "audio/utility/TestSignals.h"
#include "engine/InstrumentBuilder.h"
#include "engine/InstrumentEngine.h"
#include "support/TestHelpers.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <vector>

using namespace osp;

namespace
{
    constexpr double rate = 48000.0;

    /** A struck, decaying tone with a real tail (the pedal-loop suite's piano-like note). */
    AudioData decaying()
    {
        auto a = testsignals::pluck (220.0, 3.0, rate, 7, 0.7);
        testsignals::applyFades (a, 0.0, 1.4);
        return a;
    }

    const char* modeName (ReimaginedMode m)
    {
        switch (m)
        {
            case ReimaginedMode::kaleidoscope: return "KALEIDOSCOPE";
            case ReimaginedMode::tapeFrame: return "TAPE FRAME";
            case ReimaginedMode::toybox: return "TOYBOX";
            case ReimaginedMode::mosaic: return "MOSAIC";
            case ReimaginedMode::mirage: return "MIRAGE";
        }
        return "?";
    }

    const char* pedalName (PedalLoopMode m)
    {
        return m == PedalLoopMode::forward ? "Forward" : m == PedalLoopMode::pingPong ? "Ping-Pong" : "Normal";
    }

    struct Rig
    {
        InstrumentEngine engine;
        std::vector<float> l, r, out;
        std::shared_ptr<const InstrumentModel> model;
        static constexpr int block = 64;
        Rig (std::shared_ptr<const InstrumentModel> m, ReimaginedMode mode, double amount, PedalLoopMode pedal, bool loop = false)
            : model (std::move (m))
        {
            EngineSettings s;
            s.reimaginedRouting = ReimaginedRouting::perLayer;
            s.macros.life = 0.0;
            s.macros.space = 0.0;
            s.macros.motion = 0.0;
            s.shaping = Shaping::neutral();
            s.adsr.releaseSeconds = 0.1;
            s.pedalTurnSeconds = 0.008;
            for (auto& layer : s.layer)
            {
                layer.reimagined = amount;
                layer.reimaginedSettings.mode = mode;
                layer.pedalLoop = pedal;
                layer.loop = loop;
            }
            engine.prepare (rate, block, s);
            engine.setModel (model.get(), 0);
            l.resize (block);
            r.resize (block);
        }
        template <typename Each>
        void run (double seconds, Each each)
        {
            const auto total = static_cast<int> (seconds * rate);
            for (int done = 0; done < total; done += block)
            {
                const int n = std::min (block, total - done);
                float* ch[2] = { l.data(), r.data() };
                engine.render (ch, 2, n);
                out.insert (out.end(), l.begin(), l.begin() + n);
                each();
            }
        }
        void run (double seconds) { run (seconds, [] {}); }
        const InstrumentVoice* voiceFor (int note) const
        {
            for (int i = 0; i < InstrumentEngine::voiceSlots(); ++i)
                if (const auto& v = engine.voiceAt (i); v.isActive() && v.note() == note)
                    return &v;
            return nullptr;
        }
        double rmsDb (double from, double to) const
        {
            const auto a = static_cast<std::size_t> (from * rate), b = std::min (out.size(), static_cast<std::size_t> (to * rate));
            double sum = 0.0;
            for (auto i = a; i < b; ++i)
                sum += static_cast<double> (out[i]) * out[i];
            return 10.0 * std::log10 (std::max (1.0e-20, sum / static_cast<double> (std::max<std::size_t> (1, b - a))));
        }
    };
}

TEST_CASE ("pedal loop x reimagined: the current state, every mode (PL-R0 reproduction, printed)", "[unit][pedal-loop][pedal-reimagined][.pl-r0]")
{
    // The primary case: One Shot, the layer's LOOP off, the pedal down before the note, the key
    // released at 0.5 s and the pedal held. The recording is 3 s long (its tail from 1.6 s): a
    // working loop still sounds at 7-8 s; a voice that fell back to Normal is silent by then.
    const auto model = instrument::buildComplete (decaying(), test::analyse (decaying()), {}, false);
    REQUIRE (model->original.continuation.pedal.valid);
    std::printf ("\n%-13s %-6s %-9s  armed  looping  alive@8s  level 7-8 s (dB)\n", "mode", "amount", "pedal");
    for (const auto mode : { ReimaginedMode::kaleidoscope, ReimaginedMode::tapeFrame, ReimaginedMode::toybox, ReimaginedMode::mosaic, ReimaginedMode::mirage })
        for (const double amount : { 0.0, 0.5, 1.0 })
            for (const auto pedal : { PedalLoopMode::forward, PedalLoopMode::pingPong })
            {
                Rig rig (model, mode, amount, pedal);
                rig.engine.setSustainPedal (true);
                rig.engine.noteOn (57, 100);
                rig.run (0.5);
                const auto* v = rig.voiceFor (57);
                const bool armed = v != nullptr && v->pedalArmed();
                rig.engine.noteOff (57);
                rig.run (7.5);
                v = rig.voiceFor (57);
                std::printf ("%-13s %5.0f%% %-9s  %-5s  %-7s  %-8s  %6.1f\n", modeName (mode), amount * 100.0, pedalName (pedal), armed ? "yes" : "no",
                             v != nullptr && v->pedalLooping() ? "yes" : "no", v != nullptr ? "yes" : "no", rig.rmsDb (7.0, 8.0));
            }
}

#include "engine/PedalFold.h"

namespace
{
    /** One region over [10 000, 30 000) frames with a 600-frame jump crossfade. */
    PedalLoopModel foldModel()
    {
        PedalLoopModel m;
        m.valid = true;
        m.reason.clear();
        PedalLoopRegion r;
        r.startFrame = 10000.0;
        r.endFrame = 30000.0;
        r.crossfadeFrames = 600.0;
        r.correlation = 0.9f;
        r.levelDb = -20.0f;
        m.regions.push_back (r);
        m.levelHopFrames = 480.0;
        m.levelDb.assign (200, -20.0f);
        for (std::size_t i = 80; i < m.levelDb.size(); ++i)
            m.levelDb[i] = -32.0f;   // later on, the note is quieter
        return m;
    }

    /** What a head reading a periodic signal hears at linear position p (970 frames: the
        Forward loop, 19 400 frames, holds 20 periods, so its jump is seamless). */
    double heard (const PedalFold& fold, double p)
    {
        const auto t = fold.map (p);
        double y = 0.0;
        for (int i = 0; i < t.count; ++i)
            y += t.gain[static_cast<std::size_t> (i)] * std::sin (2.0 * 3.141592653589793 * t.pos[static_cast<std::size_t> (i)] / 970.0);
        return y;
    }
}

TEST_CASE ("pedal loop x reimagined: the shared fold loops in bounds, continuously, and lets go", "[unit][pedal-loop][pedal-reimagined]")
{
    const auto model = foldModel();
    for (const auto mode : { PedalLoopMode::forward, PedalLoopMode::pingPong })
    {
        INFO (pedalName (mode));
        PedalFold fold;
        // Off: every position reads itself.
        CHECK (fold.map (1234.5).count == 1);
        CHECK (fold.map (1234.5).pos[0] == 1234.5);
        // Armed in time (the head before the region): the read goes on to it, then loops.
        REQUIRE (fold.arm (model, mode, 2000.0, 48000.0, 140000.0, 400.0, 0.5f));
        CHECK (fold.holding());
        CHECK (fold.regionIndex() == 0);
        CHECK (fold.map (5000.0).pos[0] == 5000.0);
        CHECK_FALSE (fold.looping (5000.0));
        double previous = heard (fold, 2000.0), worst = 0.0;
        bool turnedBack = false;
        double last = fold.position (2000.0);
        bool inBounds = true;
        for (double p = 2000.0; p < 400000.0; p += 1.0)
        {
            const auto t = fold.map (p);
            for (int i = 0; i < t.count; ++i)
                inBounds = inBounds && t.pos[static_cast<std::size_t> (i)] >= 2000.0 && t.pos[static_cast<std::size_t> (i)] <= 30000.0;
            const double y = heard (fold, p);
            worst = std::max (worst, std::abs (y - previous));
            previous = y;
            const double now = fold.position (p);
            turnedBack = turnedBack || now < last - 0.5;
            last = now;
        }
        CHECK (inBounds);
        CHECK (worst < 0.05);   // no jump anywhere: only the signal's own slope and smooth crossfades
        CHECK (fold.looping (400000.0));
        CHECK (fold.passes (400000.0) > 10);
        CHECK (turnedBack);   // Forward wraps back to the start; Ping-Pong reads backwards
        // Pedal up: the read fades back to the recording, going forward from where the loop is.
        fold.release (400000.0, 48000.0);
        CHECK_FALSE (fold.holding());
        CHECK (fold.engaged());
        previous = heard (fold, 400000.0);
        worst = 0.0;
        for (double p = 400000.0; p < 402000.0; p += 1.0)
        {
            fold.update (p);
            worst = std::max (worst, std::abs (heard (fold, p) - previous));
            previous = heard (fold, p);
        }
        CHECK (worst < 0.05);
        CHECK_FALSE (fold.engaged());
        const double q = fold.position (402000.0);
        CHECK (fold.position (403000.0) == q + 1000.0);   // ordinary playback from there
        CHECK (q > 9000.0);
        CHECK (q < 32000.0);
    }
}

TEST_CASE ("pedal loop x reimagined: a late pedal fades into the last region at the note's level; too late, nothing", "[unit][pedal-loop][pedal-reimagined]")
{
    const auto model = foldModel();
    PedalFold fold;
    // Past every loop point (the region ends at 30 000): a bounded entry, level-matched.
    REQUIRE (fold.arm (model, PedalLoopMode::forward, 50000.0, 48000.0, 140000.0, 400.0, 0.5f));
    auto t = fold.map (50000.0);
    REQUIRE (t.count >= 1);
    CHECK (t.pos[0] == 50000.0);   // the recording where it is, fading out
    t = fold.map (50000.0 + 2400.0 + 1.0);   // after the 50 ms entry
    double loudest = 0.0;
    for (int i = 0; i < t.count; ++i)
    {
        CHECK ((t.pos[static_cast<std::size_t> (i)] >= 10000.0 && t.pos[static_cast<std::size_t> (i)] < 30000.0));
        loudest = std::max (loudest, static_cast<double> (t.gain[static_cast<std::size_t> (i)]));
    }
    CHECK (loudest == Catch::Approx (std::pow (10.0, -12.0 / 20.0)).margin (0.01));   // -32 dB here vs a -20 dB region
    // Released: back where the late pedal found the recording.
    fold.release (100000.0, 48000.0);
    for (double p = 100000.0; p < 103000.0; p += 1.0)
        fold.update (p);
    CHECK (fold.position (103000.0) == Catch::Approx (50000.0 + 2400.0 + 3000.0).margin (2.0));
    // Within 2 ms of the end of the recording: nothing to bring back.
    PedalFold tooLate;
    CHECK_FALSE (tooLate.arm (model, PedalLoopMode::pingPong, 139950.0, 48000.0, 140000.0, 400.0, 0.5f));
    CHECK_FALSE (tooLate.engaged());
    // Normal, or no regions: never armed.
    CHECK_FALSE (tooLate.arm (model, PedalLoopMode::normal, 0.0, 48000.0, 140000.0, 400.0, 0.5f));
    CHECK_FALSE (tooLate.arm (PedalLoopModel {}, PedalLoopMode::forward, 0.0, 48000.0, 140000.0, 400.0, 0.5f));
}

namespace
{
    constexpr ReimaginedMode allModes[] = { ReimaginedMode::kaleidoscope, ReimaginedMode::tapeFrame, ReimaginedMode::toybox, ReimaginedMode::mosaic,
                                            ReimaginedMode::mirage };

    std::shared_ptr<const InstrumentModel> decayingModel (double sampleRate = rate)
    {
        auto a = testsignals::pluck (220.0, 3.0, sampleRate, 7, 0.7);
        testsignals::applyFades (a, 0.0, 1.4);
        return instrument::buildComplete (a, test::analyse (a), {}, false);
    }

    /** The read head (0..1 of the recording) of the voice playing `note`, or -1. */
    double readAt (const Rig& rig, int note)
    {
        float where = 0.0f, level = 0.0f;
        const auto* v = rig.voiceFor (note);
        return v != nullptr && v->playhead (where, level) ? static_cast<double> (where) : -1.0;
    }

    /** The smallest 50 ms level (dB) between `from` and `to`: a loop that falls silent shows. */
    double quietest (const Rig& rig, double from, double to)
    {
        double q = 0.0;
        for (double t = from; t + 0.05 <= to; t += 0.05)
            q = std::min (q, rig.rmsDb (t, t + 0.05));
        return q;
    }

    /** The largest rise of the 10 ms level over the one 20 ms before (dB): a new attack or a click. */
    double largestRise (const Rig& rig, double from, double to)
    {
        double worst = -100.0;
        for (double t = from; t + 0.01 < to; t += 0.005)
            worst = std::max (worst, rig.rmsDb (t, t + 0.01) - rig.rmsDb (t - 0.03, t - 0.02));
        return worst;
    }

    bool finite (const Rig& rig)
    {
        return std::all_of (rig.out.begin(), rig.out.end(), [] (float x) { return std::isfinite (x); });
    }
}

TEST_CASE ("pedal loop x reimagined: every mode, every amount, Forward and Ping-Pong hold the note from one attack", "[unit][pedal-loop][pedal-reimagined]")
{
    // The recording is 3 s (its tail from 1.6 s); the key goes up at 0.5 s, the pedal stays down.
    const auto model = decayingModel();
    for (const auto mode : allModes)
        for (const double amount : { 0.0, 0.01, 0.25, 0.5, 0.75, 1.0 })
            for (const auto pedal : { PedalLoopMode::forward, PedalLoopMode::pingPong })
            {
                INFO (modeName (mode) << " " << amount * 100.0 << " % " << pedalName (pedal));
                Rig rig (model, mode, amount, pedal);
                rig.engine.setSustainPedal (true);
                rig.engine.noteOn (57, 100);
                rig.run (0.5);
                const auto* v = rig.voiceFor (57);
                REQUIRE (v != nullptr);
                CHECK (v->pedalLoopMode() == pedal);   // never silently Normal
                CHECK (v->pedalArmed());
                rig.engine.noteOff (57);
                double lowest = 1.0, highest = 0.0, previous = -1.0;
                bool back = false;
                rig.run (5.5, [&] {
                    const double at = readAt (rig, 57);
                    if (at >= 0.0 && rig.out.size() > static_cast<std::size_t> (2.0 * rate))
                    {
                        lowest = std::min (lowest, at);
                        highest = std::max (highest, at);
                        back = back || (previous >= 0.0 && at < previous - 1.0e-4);
                        previous = at;
                    }
                });
                v = rig.voiceFor (57);
                REQUIRE (v != nullptr);                       // still sounding at 6 s: twice the recording
                CHECK (v->pedalLooping());
                CHECK (rig.rmsDb (5.0, 6.0) > -60.0);
                CHECK (quietest (rig, 1.0, 6.0) > rig.rmsDb (1.0, 6.0) - 30.0);   // no silence on the way
                CHECK (largestRise (rig, 0.4, 6.0) < 9.0);                           // the attack only once, no clicks
                CHECK (highest < 0.9);                                                // never near the recording's end
                if (mode != ReimaginedMode::mosaic || amount < 1.0)
                    CHECK (back);   // the read really loops: Forward wraps back, Ping-Pong turns
                // The pedal up (the key already up): the natural release, then silence.
                rig.engine.setSustainPedal (false);
                rig.run (1.5);
                CHECK (rig.engine.activeVoiceCount() == 0);
                CHECK (finite (rig));
            }
}

TEST_CASE ("pedal loop x reimagined: an unused or Normal pedal loop leaves every mode exactly as it was", "[unit][pedal-loop][pedal-reimagined]")
{
    const auto model = decayingModel();
    for (const auto mode : allModes)
        for (const double amount : { 0.5, 1.0 })
        {
            INFO (modeName (mode) << " " << amount * 100.0 << " %");
            auto render = [&] (PedalLoopMode pedal, bool pedalDown) {
                Rig rig (model, mode, amount, pedal);
                rig.engine.setSustainPedal (pedalDown);
                rig.engine.noteOn (57, 100);
                rig.run (0.5);
                rig.engine.noteOff (57);
                rig.run (4.0);
                return rig.out;
            };
            const auto plain = render (PedalLoopMode::normal, false);
            CHECK (render (PedalLoopMode::forward, false) == plain);   // selected, the pedal never pressed: identical
            CHECK (render (PedalLoopMode::pingPong, false) == plain);
            // Normal with the pedal down: the pedal as it always was (One Shot, LOOP off: once, then the end).
            Rig check (model, mode, amount, PedalLoopMode::normal);
            check.engine.setSustainPedal (true);
            check.engine.noteOn (57, 100);
            check.run (12.0);   // (TOYBOX's walk travels the recording at about half speed)
            CHECK (check.engine.activeVoiceCount() == 0);
        }
}

TEST_CASE ("pedal loop x reimagined: pressed during the note, pressed late, lifted with the key held, toggled fast", "[unit][pedal-loop][pedal-reimagined]")
{
    const auto model = decayingModel();
    for (const auto mode : allModes)
        for (const auto pedal : { PedalLoopMode::forward, PedalLoopMode::pingPong })
        {
            INFO (modeName (mode) << " " << pedalName (pedal));
            // During the note (key held), then the key up: the pedal holds it.
            {
                Rig rig (model, mode, 0.75, pedal);
                rig.engine.noteOn (57, 100);
                rig.run (0.3);
                rig.engine.setSustainPedal (true);
                rig.run (0.4);
                rig.engine.noteOff (57);
                rig.run (4.3);
                REQUIRE (rig.voiceFor (57) != nullptr);
                CHECK (rig.rmsDb (4.5, 5.0) > -60.0);
                CHECK (largestRise (rig, 0.4, 5.0) < 9.0);
            }
            // Late (the read in the tail, past every loop point): the note is caught and held.
            {
                Rig rig (model, mode, 0.75, pedal);
                rig.engine.noteOn (57, 100);
                rig.run (2.2);
                rig.engine.setSustainPedal (true);
                rig.run (0.3);
                rig.engine.noteOff (57);
                rig.run (3.5);
                REQUIRE (rig.voiceFor (57) != nullptr);
                CHECK (rig.voiceFor (57)->pedalArmed());
                CHECK (rig.rmsDb (5.5, 6.0) > -70.0);
                CHECK (largestRise (rig, 2.1, 6.0) < 9.0);   // level-matched: no jump up to the louder region
            }
            // Pedal up with the key held: ordinary playback resumes and the recording plays out.
            {
                Rig rig (model, mode, 0.75, pedal);
                rig.engine.setSustainPedal (true);
                rig.engine.noteOn (57, 100);
                rig.run (4.0);
                REQUIRE (rig.voiceFor (57) != nullptr);
                rig.engine.setSustainPedal (false);
                rig.run (0.2);
                REQUIRE (rig.voiceFor (57) != nullptr);
                CHECK_FALSE (rig.voiceFor (57)->pedalArmed());
                CHECK (largestRise (rig, 3.9, 4.2) < 9.0);
                rig.run (10.0);   // (the key still down) it plays on to the recording's end and ends
                CHECK (rig.voiceFor (57) == nullptr);
                CHECK (finite (rig));
            }
            // Rapid pedal changes: safe, finite, and the last state wins.
            {
                Rig rig (model, mode, 0.75, pedal);
                rig.engine.noteOn (57, 100);
                rig.run (0.4);
                for (int k = 0; k < 24; ++k)
                {
                    rig.engine.setSustainPedal ((k & 1) == 0);
                    rig.run (0.011);
                }
                rig.engine.setSustainPedal (true);
                rig.engine.noteOff (57);
                rig.run (4.0);
                REQUIRE (rig.voiceFor (57) != nullptr);
                CHECK (rig.voiceFor (57)->pedalArmed());
                rig.engine.setSustainPedal (false);
                rig.run (1.0);
                CHECK (rig.engine.activeVoiceCount() == 0);
                CHECK (finite (rig));
            }
        }
}

TEST_CASE ("pedal loop x reimagined: chords, stealing, All Notes Off, pitches and sample rates", "[unit][pedal-loop][pedal-reimagined]")
{
    for (const double sampleRate : { 44100.0, 96000.0 })
    {
        const auto model = decayingModel (sampleRate);
        for (const auto mode : allModes)
        {
            INFO (modeName (mode) << " at " << sampleRate << " Hz (output 48 kHz)");
            // A chord at three pitches, held by the pedal: every note loops.
            Rig rig (model, mode, 0.8, PedalLoopMode::pingPong);
            rig.engine.setSustainPedal (true);
            for (const int note : { 45, 57, 81 })
                rig.engine.noteOn (note, 100);
            rig.run (0.5);
            for (const int note : { 45, 57, 81 })
                rig.engine.noteOff (note);
            rig.run (5.0);
            for (const int note : { 45, 57, 81 })
            {
                INFO ("note " << note);
                REQUIRE (rig.voiceFor (note) != nullptr);
                CHECK (rig.voiceFor (note)->pedalLooping());
            }
            CHECK (rig.rmsDb (5.0, 5.5) > -60.0);
            // All Notes Off ends them.
            rig.engine.allNotesOff();
            rig.run (1.0);
            CHECK (rig.engine.activeVoiceCount() == 0);
            CHECK (finite (rig));
        }
    }
    // Stealing: more held notes than voices; the stolen ones end cleanly, the rest loop.
    const auto model = decayingModel();
    for (const auto mode : allModes)
    {
        INFO (modeName (mode));
        Rig rig (model, mode, 0.8, PedalLoopMode::forward);
        rig.engine.setSustainPedal (true);
        for (int note = 48; note < 48 + 30; ++note)
        {
            rig.engine.noteOn (note, 90);
            rig.run (0.02);
            rig.engine.noteOff (note);
        }
        rig.run (4.0);
        CHECK (rig.engine.activeVoiceCount() > 0);
        CHECK (rig.rmsDb (3.5, 4.0) > -60.0);
        CHECK (finite (rig));
    }
}

TEST_CASE ("pedal loop x reimagined: the mode's character stays audible while the loop holds", "[unit][pedal-loop][pedal-reimagined]")
{
    // The same performance at 0 % and 100 %: through the held loop the two differ clearly (the
    // transformation is not bypassed by the pedal); at 0 % every mode is the plain loop.
    const auto model = decayingModel();
    auto loopSection = [&] (ReimaginedMode mode, double amount) {
        Rig rig (model, mode, amount, PedalLoopMode::forward);
        rig.engine.setSustainPedal (true);
        rig.engine.noteOn (57, 100);
        rig.run (0.5);
        rig.engine.noteOff (57);
        rig.run (5.5);
        return std::vector<float> (rig.out.begin() + static_cast<std::ptrdiff_t> (4.0 * rate), rig.out.begin() + static_cast<std::ptrdiff_t> (6.0 * rate));
    };
    const auto plain = loopSection (ReimaginedMode::kaleidoscope, 0.0);
    for (const auto mode : allModes)
    {
        INFO (modeName (mode));
        CHECK (loopSection (mode, 0.0) == plain);
        const auto reimagined = loopSection (mode, 1.0);
        double diff = 0.0, ref = 0.0;
        for (std::size_t i = 0; i < plain.size(); ++i)
        {
            diff += (static_cast<double> (reimagined[i]) - plain[i]) * (static_cast<double> (reimagined[i]) - plain[i]);
            ref += static_cast<double> (plain[i]) * plain[i];
        }
        CHECK (diff > 0.1 * ref);
    }
}
