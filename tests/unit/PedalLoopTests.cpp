// Pedal Loop 1.0 (docs/research/pedal-loop): the sustain pedal holds a One Shot note in a
// Forward or Ping-Pong loop; Normal is the pedal as it always was.

#include "audio/utility/TestSignals.h"
#include "engine/InstrumentBuilder.h"
#include "engine/InstrumentEngine.h"
#include "research/PedalLoopLab.h"
#include "support/TestHelpers.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <vector>

using namespace osp;

namespace
{
    constexpr double rate = 48000.0;

    /** A struck, decaying tone (a piano's kind of note: the continuation will not hold it). */
    AudioData decaying()
    {
        auto a = testsignals::pluck (220.0, 3.0, rate, 7, 0.7);
        testsignals::applyFades (a, 0.0, 1.4);   // a real tail: the last loop point well before the end
        return a;
    }

    /** A sustained tone the continuation holds (an organ's kind of note). */
    AudioData sustained()
    {
        auto a = testsignals::vibratoSine (196.0, 5.0, 15.0, 3.0, rate, 0.4);
        testsignals::applyFades (a, 0.03, 0.3);
        return a;
    }

    std::shared_ptr<const InstrumentModel> build (const AudioData& audio)
    {
        return instrument::buildComplete (audio, test::analyse (audio), {}, false);
    }

    EngineSettings settings()
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

    struct Rig
    {
        InstrumentEngine engine;
        std::vector<float> l, r, out;
        std::shared_ptr<const InstrumentModel> model;
        static constexpr int block = 64;
        Rig (std::shared_ptr<const InstrumentModel> m, PedalLoopMode mode, bool loop = true, double turnSeconds = 0.008)
            : model (std::move (m))
        {
            auto s = settings();
            s.layer[0].pedalLoop = mode;
            s.layer[0].loop = loop;
            s.pedalTurnSeconds = turnSeconds;
            engine.prepare (rate, block, s);
            engine.setModel (model.get(), 0);
            l.resize (block);
            r.resize (block);
        }
        /** Renders `seconds`, calling `each` after every block. */
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
        void run (double seconds)
        {
            run (seconds, [] {});
        }
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
        float peak (double from, double to) const
        {
            float p = 0.0f;
            for (auto i = static_cast<std::size_t> (from * rate); i < std::min (out.size(), static_cast<std::size_t> (to * rate)); ++i)
                p = std::max (p, std::abs (out[i]));
            return p;
        }
        /** The largest rise in 10 ms level over 30 ms (dB) between `from` and `to`: an attack
            played again shows as a sudden rise. */
        double largestRise (double from, double to) const
        {
            double worst = -100.0;
            for (double t = from; t + 0.01 < to; t += 0.005)
                worst = std::max (worst, rmsDb (t, t + 0.01) - rmsDb (t - 0.03, t - 0.02));
            return worst;
        }
        bool finite() const
        {
            return std::all_of (out.begin(), out.end(), [] (float x) { return std::isfinite (x); });
        }
    };
}

TEST_CASE ("pedal loop: regions are analysed offline beside the continuation, inside the note's body", "[unit][pedal-loop]")
{
    const auto piano = build (decaying());
    const auto& c = piano->original.continuation;
    CHECK_FALSE (c.canSustain);   // a struck note plays once today
    REQUIRE (c.pedal.valid);
    CHECK (c.pedal.reason == "body loops");
    REQUIRE (! c.pedal.regions.empty());
    CHECK (c.pedal.regions.size() <= static_cast<std::size_t> (PedalLoopModel::maxRegions));
    const double attackEnd = (piano->analysis.envelope.onsetSeconds + piano->analysis.envelope.attackSeconds) * rate;
    double lastEnd = 0.0;
    for (const auto& r : c.pedal.regions)
    {
        CHECK (r.startFrame > attackEnd);                            // the attack is never looped
        CHECK (r.endFrame - r.startFrame >= 0.06 * rate);
        CHECK (r.crossfadeFrames > 0.0);
        CHECK (r.crossfadeFrames < r.endFrame - r.startFrame);
        CHECK (r.endFrame <= c.pedal.soundEndFrame);
        CHECK (r.endFrame > lastEnd);
        lastEnd = r.endFrame;
    }
    CHECK (c.pedal.regions.size() > 1);   // along the decay: a late pedal finds one near
    // A sustained source offers its continuation's best loop first.
    const auto organ = build (sustained());
    const auto& o = organ->original.continuation;
    REQUIRE (o.canSustain);
    REQUIRE (o.pedal.valid);
    CHECK (o.pedal.reason == "continuation loop");
    const auto& best = o.jumps[static_cast<std::size_t> (std::max (0, o.bestLoop))];
    CHECK (o.pedal.regions.front().startFrame == best.toFrame);
    // Nothing to loop: silence, or a click with no body.
    CHECK_FALSE (build (testsignals::silence (1.0, rate))->original.continuation.pedal.valid);
    CHECK_FALSE (build (testsignals::impulse (0.5, rate, 0.1, 0.9))->original.continuation.pedal.valid);
}

TEST_CASE ("pedal loop: Normal leaves the pedal as it was; Forward holds a decaying note from one attack", "[unit][pedal-loop]")
{
    const auto model = build (decaying());
    // Normal: pedal down, a note, the key released: the recording plays once and ends.
    {
        Rig rig (model, PedalLoopMode::normal);
        rig.engine.setSustainPedal (true);
        rig.engine.noteOn (57, 100);
        rig.run (0.5);
        REQUIRE (rig.voiceFor (57) != nullptr);
        CHECK_FALSE (rig.voiceFor (57)->pedalArmed());
        rig.engine.noteOff (57);
        rig.run (4.5);
        CHECK (rig.engine.activeVoiceCount() == 0);
    }
    // Forward: the same performance holds the note for as long as the pedal is down.
    Rig rig (model, PedalLoopMode::forward);
    rig.engine.setSustainPedal (true);
    rig.engine.noteOn (57, 100);
    rig.run (0.5);
    const auto* v = rig.voiceFor (57);
    REQUIRE (v != nullptr);
    CHECK (v->pedalLoopMode() == PedalLoopMode::forward);
    CHECK (v->pedalArmed());
    rig.engine.noteOff (57);   // the key up: the pedal keeps it (scenario 7)
    rig.run (7.5);
    v = rig.voiceFor (57);
    REQUIRE (v != nullptr);
    CHECK (v->pedalLooping());
    const auto& region = model->original.continuation.pedal.regions[static_cast<std::size_t> (v->pedalRegionIndex())];
    CHECK (v->readHead() >= region.startFrame - 1.0);
    CHECK (v->readHead() <= region.endFrame + 1.0);
    CHECK (rig.rmsDb (7.0, 8.0) > -50.0);                                // still sounding at 8 s (3 s of recording)
    CHECK (rig.largestRise (0.3, 8.0) < 3.0);                            // the attack only once: no new onset
    // Pedal up (key already up): the note releases and ends (scenario 8).
    rig.engine.setSustainPedal (false);
    rig.run (0.6);
    CHECK (rig.engine.activeVoiceCount() == 0);
    CHECK (rig.finite());
}

TEST_CASE ("pedal loop: Ping-Pong turns at both ends of its region, in bounds, at any pitch", "[unit][pedal-loop]")
{
    const auto model = build (decaying());
    for (const int note : { 57, 45, 81, 93 })
    {
        for (const double turn : { 0.0, 0.008 })
        {
            Rig rig (model, PedalLoopMode::pingPong, true, turn);
            rig.engine.setSustainPedal (true);
            rig.engine.noteOn (note, 100);
            bool sawBack = false, sawForward = false, inBounds = true;
            const double frames = static_cast<double> (model->original.source->numFrames());
            rig.run (6.0, [&] {
                if (const auto* v = rig.voiceFor (note); v != nullptr && v->pedalLooping())
                {
                    sawBack = sawBack || v->readDirection() < 0.0;
                    sawForward = sawForward || v->readDirection() > 0.0;
                    const auto& r = model->original.continuation.pedal.regions[static_cast<std::size_t> (v->pedalRegionIndex())];
                    // (within one block's overshoot of a step at this pitch: the read reaches the turning
                    // point, at most a step past it, as the turn begins)
                    const double slack = 2.0 + 2.0 * std::exp2 ((note - 57) / 12.0);
                    inBounds = inBounds && v->readHead() >= r.startFrame - slack && v->readHead() <= r.endFrame + slack && v->readHead() >= 0.0
                               && v->readHead() < frames;
                }
            });
            INFO ("note " << note << ", turn " << turn);
            CHECK (sawBack);
            CHECK (sawForward);
            CHECK (inBounds);
            CHECK (rig.voiceFor (note) != nullptr);
            CHECK (rig.finite());
            CHECK (rig.rmsDb (5.0, 6.0) > -55.0);
        }
    }
}

TEST_CASE ("pedal loop: a pedal pressed mid-note plays on to the loop ahead; a late one crossfades into the last region at the note's level", "[unit][pedal-loop]")
{
    const auto model = build (decaying());
    const auto& regions = model->original.continuation.pedal.regions;
    // Mid-note (scenario 3): no reset; the read keeps going forward to the region ahead of it.
    {
        Rig rig (model, PedalLoopMode::forward);
        rig.engine.noteOn (57, 100);
        rig.run (0.9);
        const auto* v = rig.voiceFor (57);
        REQUIRE (v != nullptr);
        const double before = v->readHead();
        rig.engine.setSustainPedal (true);
        REQUIRE (v->pedalArmed());
        const auto& r = regions[static_cast<std::size_t> (v->pedalRegionIndex())];
        CHECK (r.endFrame - r.crossfadeFrames > before);   // ahead, not behind
        rig.run (0.02);
        CHECK (v->readHead() > before);                     // no jump back
        rig.run (5.0);
        CHECK (rig.voiceFor (57) != nullptr);
        CHECK (rig.voiceFor (57)->pedalLooping());
        CHECK (rig.voiceFor (57)->pedalGain() == 1.0f);
    }
    // Late (scenario 4): past every loop point, before the recording ends.
    {
        Rig rig (model, PedalLoopMode::pingPong);
        rig.engine.noteOn (57, 100);
        const double lastLeave = regions.back().endFrame - 0.008 * rate;
        const auto* v = rig.voiceFor (57);
        while (rig.voiceFor (57) != nullptr && rig.voiceFor (57)->readHead() < lastLeave + 0.05 * rate)
            rig.run (0.01);
        v = rig.voiceFor (57);
        REQUIRE (v != nullptr);
        const auto armedAt = rig.out.size();
        rig.engine.setSustainPedal (true);
        CHECK (v->pedalArmed());
        rig.run (4.0);
        REQUIRE (rig.voiceFor (57) != nullptr);
        CHECK (rig.voiceFor (57)->pedalLooping());
        CHECK (rig.voiceFor (57)->pedalGain() < 1.0f);   // matched down to the tail's level
        // No restart: the level after the crossfade stays near the level before it.
        const double t = static_cast<double> (armedAt) / rate;
        CHECK (std::abs (rig.rmsDb (t + 0.1, t + 0.3) - rig.rmsDb (t - 0.1, t)) < 6.0);
        // Key still held, pedal up (scenario 6): back to the recording where it was found; it ends.
        rig.engine.setSustainPedal (false);
        rig.run (0.1);
        {
            const auto* after = rig.voiceFor (57);
            CHECK_FALSE ((after != nullptr && after->pedalArmed()));
        }
        rig.run (3.0);
        CHECK (rig.engine.activeVoiceCount() == 0);
        CHECK (rig.finite());
    }
}

TEST_CASE ("pedal loop: key held, pedal up leaves the loop into the recording; chords loop apart; fast pedalling never sticks", "[unit][pedal-loop]")
{
    const auto model = build (decaying());
    // Scenario 6, Ping-Pong while reading back: it turns forward and plays on to the end.
    {
        Rig rig (model, PedalLoopMode::pingPong);
        rig.engine.setSustainPedal (true);
        rig.engine.noteOn (57, 100);
        bool reading_back = false;
        rig.run (6.0, [&] {
            if (! reading_back)
                if (const auto* v = rig.voiceFor (57); v != nullptr && v->pedalLooping() && v->readDirection() < 0.0)
                {
                    reading_back = true;
                    rig.engine.setSustainPedal (false);
                }
        });
        CHECK (reading_back);
        CHECK (rig.engine.activeVoiceCount() == 0);   // the recording's own ending, then done
        CHECK (rig.finite());
    }
    // A chord: each note its own region and read; none in step with another; all end.
    {
        Rig rig (model, PedalLoopMode::pingPong);
        rig.engine.setSustainPedal (true);
        for (const int n : { 57, 60, 64, 67 })
            rig.engine.noteOn (n, 100);
        rig.run (0.3);
        for (const int n : { 57, 60, 64, 67 })
            rig.engine.noteOff (n);
        rig.run (5.0);
        std::vector<double> heads;
        for (const int n : { 57, 60, 64, 67 })
        {
            const auto* v = rig.voiceFor (n);
            REQUIRE (v != nullptr);
            CHECK (v->pedalLooping());
            heads.push_back (v->readHead());
        }
        std::sort (heads.begin(), heads.end());
        CHECK (std::adjacent_find (heads.begin(), heads.end()) == heads.end());
        rig.engine.setSustainPedal (false);
        rig.run (0.6);
        CHECK (rig.engine.activeVoiceCount() == 0);
    }
    // Pedal pumped every few ms, notes repeated: always bounded, never stuck.
    for (const auto mode : { PedalLoopMode::forward, PedalLoopMode::pingPong })
    {
        Rig rig (model, mode);
        bool down = false;
        int blocks = 0;
        rig.engine.noteOn (57, 100);
        rig.run (4.0, [&] {
            ++blocks;
            if (blocks % 3 == 0)
                rig.engine.setSustainPedal (down = ! down);
            if (blocks % 200 == 0)
            {
                rig.engine.noteOff (57);
                rig.engine.noteOn (57, 90);
            }
        });
        rig.engine.noteOff (57);
        rig.engine.setSustainPedal (false);
        rig.run (4.0);
        CHECK (rig.engine.activeVoiceCount() == 0);
        CHECK (rig.finite());
        CHECK (rig.peak (0.0, 8.0) < 2.0f);
    }
    // All Notes Off and a reset end looping notes.
    {
        Rig rig (model, PedalLoopMode::forward);
        rig.engine.setSustainPedal (true);
        rig.engine.noteOn (57, 100);
        rig.run (4.0);
        REQUIRE (rig.voiceFor (57) != nullptr);
        rig.engine.allNotesOff();
        rig.run (0.5);
        CHECK (rig.engine.activeVoiceCount() == 0);
        rig.engine.setSustainPedal (true);
        rig.engine.noteOn (57, 100);
        rig.run (4.0);
        rig.engine.reset();
        CHECK (rig.engine.activeVoiceCount() == 0);
    }
}

TEST_CASE ("pedal loop: Forward on a note the layer's LOOP already holds changes nothing; Granular and REVERSE keep Normal", "[unit][pedal-loop]")
{
    const auto organ = build (sustained());
    {
        Rig rig (organ, PedalLoopMode::forward);
        rig.engine.setSustainPedal (true);
        rig.engine.noteOn (55, 100);
        rig.run (0.2);
        REQUIRE (rig.voiceFor (55) != nullptr);
        CHECK (rig.voiceFor (55)->pedalLoopMode() == PedalLoopMode::normal);   // the continuation walk holds it
    }
    {
        Rig rig (organ, PedalLoopMode::forward, false);   // LOOP off: the pedal loop holds it
        rig.engine.setSustainPedal (true);
        rig.engine.noteOn (55, 100);
        rig.run (5.0);
        REQUIRE (rig.voiceFor (55) != nullptr);
        CHECK (rig.voiceFor (55)->pedalLooping());
    }
    const auto piano = build (decaying());
    {
        Rig rig (piano, PedalLoopMode::pingPong);
        auto s = rig.engine.settings();
        s.layer[0].reverse = true;
        rig.engine.setLayerSettings (0, s.layer[0]);
        rig.engine.setSustainPedal (true);
        rig.engine.noteOn (57, 100);
        rig.run (0.2);
        REQUIRE (rig.voiceFor (57) != nullptr);
        CHECK (rig.voiceFor (57)->pedalLoopMode() == PedalLoopMode::normal);
    }
    {
        Rig rig (piano, PedalLoopMode::pingPong);
        rig.engine.setSourceMode (0, SourceMode::granular);
        rig.engine.setSustainPedal (true);
        rig.engine.noteOn (57, 100);
        rig.run (0.2);
        REQUIRE (rig.voiceFor (57) != nullptr);
        CHECK (rig.voiceFor (57)->pedalLoopMode() == PedalLoopMode::normal);
    }
}

TEST_CASE ("pedal loop: after Ping-Pong hands a held note back to the layer's own loop, the walk reads forward and the pedal loop never returns", "[unit][pedal-loop]")
{
    // (Found by the lab: a jump of the continuation walk scheduled after the pedal let go once
    // inherited the last turnaround's flags and read backwards, re-entering the loop.)
    const auto src = osp::research::pedalLabStandIns (48000.0)[6];   // the organ stand-in: LOOP holds it
    const auto organ = instrument::buildComplete (src.audio, test::analyse (src.audio), {}, false);
    REQUIRE (organ->original.continuation.canSustain);
    const int root = static_cast<int> (std::lround (organ->rootMidi));
    for (const bool keysUp : { true, false })
    {
        EngineSettings s;
        s.macros.space = 0.0;
        s.macros.motion = 0.0;
        s.macros.reimagined = 0.0;
        s.adsr.releaseSeconds = 0.3;
        for (auto& l : s.layer)
            l.pedalLoop = PedalLoopMode::pingPong;
        InstrumentEngine engine;
        engine.prepare (rate, 16, s);
        engine.setModel (organ.get(), 0);
        engine.setMixSlots (1);
        std::vector<float> l (16), r (16);
        float* ch[2] = { l.data(), r.data() };
        bool looped = false, backAfter = false, loopAfter = false;
        for (int b = 0; b < static_cast<int> (9.0 * rate / 16); ++b)
        {
            const double t = b * 16.0 / rate;
            if (b == 0)
                engine.setSustainPedal (true);
            if (b == static_cast<int> (0.05 * rate / 16))
                for (const int n : { 0, 4, 7, 12 })
                    engine.noteOn (root + n, 100);
            if (keysUp && b == static_cast<int> (1.0 * rate / 16))
                for (const int n : { 0, 4, 7, 12 })
                    engine.noteOff (root + n);
            if (b == static_cast<int> (6.0 * rate / 16))
                engine.setSustainPedal (false);
            engine.render (ch, 2, 16);
            for (int i = 0; i < InstrumentEngine::voiceSlots(); ++i)
            {
                const auto& v = engine.voiceAt (i);
                if (! v.isActive())
                    continue;
                looped = looped || v.pedalLooping();
                if (t > 6.05)   // (the exits' crossfades are 15 ms at most)
                {
                    loopAfter = loopAfter || v.pedalArmed();
                    backAfter = backAfter || v.readDirection() < 0.0;
                }
            }
        }
        INFO ("keys up " << keysUp);
        CHECK (looped);
        CHECK_FALSE (loopAfter);
        CHECK_FALSE (backAfter);
    }
}
