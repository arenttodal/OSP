// The five-macro shaping system v1.0 rendered through the whole engine.
#include "audio/utility/TestSignals.h"
#include "core/Fft.h"
#include "engine/InstrumentBuilder.h"
#include "engine/InstrumentEngine.h"
#include "research/RenderSession.h"
#include "support/TestHelpers.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <complex>
#include <numbers>

using namespace osp;

namespace
{
    std::shared_ptr<InstrumentModel> sawModel()
    {
        auto audio = testsignals::saw (220.0, 3.0, 48000.0, 0.4);
        testsignals::applyFades (audio, 0.01, 0.3);
        return instrument::buildComplete (audio, test::analyse (audio), {}, false);
    }

    research::RenderConfig quiet()
    {
        research::RenderConfig c;
        c.engineSettings.macros.life = 0.0;
        c.engineSettings.macros.space = 0.0;
        c.engineSettings.macros.reimagined = 0.0;
        c.engineSettings.macros.motion = 0.0;
        return c;
    }

    AudioData play (const InstrumentModel& model, const research::RenderConfig& config, int velocity = 100, double seconds = 1.5, int note = 57)
    {
        MidiSequence s;
        s.events.push_back ({ 0.0, MidiEvent::Type::noteOn, note, velocity, 1 });
        s.events.push_back ({ seconds, MidiEvent::Type::noteOff, note, 0, 1 });
        return research::renderInstrument (model, s, config).audio;
    }

    double centroid (const AudioData& a, double from, int order = 12)
    {
        const Fft fft (order);
        const int n = fft.size();
        std::vector<std::complex<double>> b (static_cast<std::size_t> (n));
        const auto start = static_cast<std::size_t> (from * a.sampleRate);
        for (int i = 0; i < n; ++i)
            b[static_cast<std::size_t> (i)] = { a.channels[0][start + static_cast<std::size_t> (i)] * (0.5 - 0.5 * std::cos (2.0 * std::numbers::pi * i / n)), 0.0 };
        fft.forward (b.data());
        double num = 0, den = 0;
        for (int k = 1; k < n / 2; ++k)
        {
            const double p = std::norm (b[static_cast<std::size_t> (k)]);
            num += p * k * a.sampleRate / n;
            den += p;
        }
        return num / den;
    }
}

TEST_CASE ("shaping: CHARACTER sweeps the filter range, log-mapped and reversible", "[integration][shaping]")
{
    CHECK (std::exp2 (shaping::cutoffOctaves ({}, 0.5)) == Catch::Approx (std::sqrt (450.0 * 18000.0)).epsilon (1e-6));
    const auto model = sawModel();
    auto at = [&] (double character, double minHz = 450.0, double maxHz = 18000.0) {
        auto c = quiet();
        c.engineSettings.macros.character = character;
        c.engineSettings.shaping.filterMinHz = minHz;
        c.engineSettings.shaping.filterMaxHz = maxHz;
        c.engineSettings.shaping.envAmount = 0.0;
        return centroid (play (*model, c), 0.5);
    };
    const double dark = at (0.0), mid = at (0.5), open = at (1.0);
    INFO ("centroid " << dark << " / " << mid << " / " << open << " Hz");
    CHECK (dark < mid);
    CHECK (mid < open);
    CHECK (12.0 * std::log2 (open / dark) > 12.0); // more than an octave of brightness
    // MIN above MAX: turning CHARACTER up makes it darker.
    CHECK (at (1.0, 12000.0, 500.0) < at (0.0, 12000.0, 500.0));
}

TEST_CASE ("shaping: the filter envelope articulates every note", "[integration][shaping]")
{
    const auto model = sawModel();
    auto c = quiet();
    c.engineSettings.macros.character = 0.25;
    c.engineSettings.shaping.envAmount = 0.6;
    c.engineSettings.shaping.envAttackSeconds = 0.005;
    c.engineSettings.shaping.envDecaySeconds = 0.4;
    const auto out = play (*model, c);
    const double early = centroid (out, 0.01, 10), late = centroid (out, 1.0, 10); // 21 ms windows
    INFO ("centroid at 20 ms " << early << " Hz, at 1 s " << late << " Hz");
    CHECK (early > 1.5 * late);
    c.engineSettings.shaping.envAmount = -0.6; // negative: the filter dips first
    const auto dip = play (*model, c);
    CHECK (centroid (dip, 0.01, 10) < centroid (dip, 1.0, 10));
}

TEST_CASE ("shaping: DYNAMICS TONE makes hard notes brighter", "[integration][shaping]")
{
    const auto model = sawModel();
    auto spread = [&] (double tone) {
        auto c = quiet();
        c.engineSettings.macros.dynamics = 1.0;
        c.engineSettings.macros.character = 0.5;
        c.engineSettings.shaping.dynamicsTone = tone;
        return 12.0 * std::log2 (centroid (play (*model, c, 127), 0.3) / centroid (play (*model, c, 30), 0.3));
    };
    const double without = spread (0.0), with = spread (1.0);
    INFO ("hard/soft brightness: tone 0 " << without << " st, tone 1 " << with << " st");
    CHECK (with > without + 4.0);
}

TEST_CASE ("shaping: MOVEMENT is clearly audible in every mode and grows with the knob", "[integration][shaping]")
{
    // A steady sine: any modulation shows as pitch (zero-crossing) or level variance.
    auto audio = testsignals::sine (330.0, 6.0, 48000.0, 0.4, 2);
    testsignals::applyFades (audio, 0.01, 0.3);
    const auto model = instrument::buildComplete (audio, test::analyse (audio), {}, false);
    struct Variance { double pitchCents, levelDb, side; };
    auto measure = [&] (MovementMode mode, double amount) {
        auto c = quiet();
        c.engineSettings.shaping = Shaping::neutral();
        c.engineSettings.shaping.movementMode = mode;
        c.engineSettings.macros.motion = amount;
        const auto out = play (*model, c, 100, 5.0, 64);
        // Pitch from zero-crossing intervals (left), level and side over 20 ms frames.
        std::vector<double> periods;
        double last = -1.0;
        const auto& x = out.channels[0];
        for (std::size_t i = 48000; i < 4 * 48000; ++i)
            if (x[i - 1] < 0.0f && x[i] >= 0.0f)
            {
                const double t = static_cast<double> (i - 1) + x[i - 1] / (x[i - 1] - x[i]);
                if (last > 0.0)
                    periods.push_back (t - last);
                last = t;
            }
        double mean = 0.0;
        for (double p : periods) mean += p;
        mean /= static_cast<double> (periods.size());
        double var = 0.0;
        for (double p : periods) var += std::pow (1200.0 * std::log2 (p / mean), 2.0);
        std::vector<double> lv;
        double side = 0.0, mid = 0.0;
        for (std::size_t i = 48000; i + 960 < 4 * 48000; i += 960)
        {
            double e = 0.0;
            for (std::size_t j = i; j < i + 960; ++j)
            {
                e += 0.5 * (out.channels[0][j] * out.channels[0][j] + out.channels[1][j] * out.channels[1][j]);
                side += std::pow (out.channels[0][j] - out.channels[1][j], 2.0);
                mid += std::pow (out.channels[0][j] + out.channels[1][j], 2.0);
            }
            lv.push_back (10.0 * std::log10 (e / 960.0 + 1e-20));
        }
        double lm = 0.0;
        for (double v : lv) lm += v;
        lm /= static_cast<double> (lv.size());
        double lvar = 0.0;
        for (double v : lv) lvar += (v - lm) * (v - lm);
        return Variance { std::sqrt (var / static_cast<double> (periods.size())), std::sqrt (lvar / static_cast<double> (lv.size())), side / mid };
    };
    const auto still = measure (MovementMode::drift, 0.0);
    const auto drift25 = measure (MovementMode::drift, 0.25), drift50 = measure (MovementMode::drift, 0.5);
    const auto tape = measure (MovementMode::tape, 0.5), chorus = measure (MovementMode::chorus, 0.5), pulse = measure (MovementMode::pulse, 0.5);
    INFO ("pitch sd (cents): still " << still.pitchCents << ", drift 25/50 " << drift25.pitchCents << "/" << drift50.pitchCents << ", tape " << tape.pitchCents);
    INFO ("level sd (dB): still " << still.levelDb << ", drift50 " << drift50.levelDb << ", pulse " << pulse.levelDb << "; chorus side/mid " << chorus.side);
    CHECK (still.pitchCents < 0.3);
    CHECK (drift25.pitchCents > 0.8);
    CHECK (drift50.pitchCents > 1.4 * drift25.pitchCents); // 25 and 50 clearly differ (depth grows as amount^0.75)
    CHECK (tape.pitchCents > 2.0);
    CHECK (pulse.levelDb > 2.0);
    CHECK (chorus.side > still.side + 0.01);
}

TEST_CASE ("shaping: the popup ranges reach their documented ends", "[unit][shaping]")
{
    // Sub-1 ranges must not be clamped (DRIFT SPEED 0.01-0.8 Hz, CHORUS WIDTH from 0.2 ms...).
    CHECK (shaping::driftSpeedHz (0.0) == Catch::Approx (0.01));
    CHECK (shaping::driftSpeedHz (1.0) == Catch::Approx (0.8));
    CHECK (shaping::driftSpeedHz (0.5) == Catch::Approx (std::sqrt (0.01 * 0.8)));
    CHECK (shaping::chorusRateHz (0.0) == Catch::Approx (0.05));
    CHECK (shaping::chorusRateHz (1.0) == Catch::Approx (3.0));
    CHECK (shaping::chorusWidthMs (0.0) == Catch::Approx (0.2));
    CHECK (shaping::chorusWidthMs (1.0) == Catch::Approx (18.0));
    CHECK (shaping::pulseRateHz (0.0) == Catch::Approx (0.05));
    CHECK (shaping::pulseRateHz (1.0) == Catch::Approx (10.0));
}
