// PEDAL LOOP x REIMAGINED (docs/research/pedal-reimagined): the sustain pedal's Forward and
// Ping-Pong loops hold a One Shot note in every REIMAGINED mode, at every amount.

#include "audio/utility/TestSignals.h"
#include "engine/InstrumentBuilder.h"
#include "engine/InstrumentEngine.h"
#include "support/TestHelpers.h"

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
