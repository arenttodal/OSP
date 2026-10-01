#include "audio/utility/TestSignals.h"
#include "core/PitchMath.h"
#include "support/TestHelpers.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using Catch::Approx;
using namespace osp;
namespace ts = osp::testsignals;

namespace
{
    void requireRoot (const AnalysisData& a, int midiNote, double toleranceCents = 5.0)
    {
        INFO ("detected " << a.pitch.noteName << " " << a.pitch.fundamentalHz << " Hz, confidence " << a.pitch.confidence);
        REQUIRE (a.pitch.detected);
        CHECK (a.pitch.midiNote == midiNote);
        CHECK (std::abs (centsBetween (midiToHz (midiNote), a.pitch.fundamentalHz)) < toleranceCents);
        CHECK (a.pitch.confidenceLevel == "high");
    }
}

TEST_CASE ("pitch: synthetic sines are detected exactly", "[unit][analysis][pitch]")
{
    requireRoot (test::analyse (ts::sine (440.0, 1.0, 44100.0)), 69);
    requireRoot (test::analyse (ts::sine (midiToHz (48), 1.0, 48000.0)), 48);
    requireRoot (test::analyse (ts::sine (midiToHz (84), 1.0, 96000.0)), 84);
    requireRoot (test::analyse (ts::sine (midiToHz (36), 1.5, 88200.0)), 36);
}

TEST_CASE ("pitch: cents offset is reported, not rounded away", "[unit][analysis][pitch]")
{
    const auto a = test::analyse (ts::sine (440.0 * centsToRatio (-23.0), 1.0, 48000.0));
    CHECK (a.pitch.midiNote == 69);
    CHECK (a.pitch.centsOffset == Approx (-23.0).margin (1.0));
}

TEST_CASE ("pitch: harmonically rich and low sources", "[unit][analysis][pitch]")
{
    requireRoot (test::analyse (ts::saw (midiToHz (45), 1.0, 48000.0)), 45);
    requireRoot (test::analyse (ts::saw (midiToHz (28), 2.0, 44100.0)), 28, 10.0); // E1, 41 Hz
    requireRoot (test::analyse (ts::pluck (midiToHz (40), 2.0, 48000.0, 1)), 40, 10.0);
    requireRoot (test::analyse (ts::vowel (midiToHz (57), 2.0, 48000.0, 2)), 57, 10.0);
}

TEST_CASE ("pitch: vibrato does not move the root and is measured", "[unit][analysis][pitch]")
{
    const auto a = test::analyse (ts::vibratoSine (440.0, 5.5, 40.0, 3.0, 44100.0));
    requireRoot (a, 69);
    REQUIRE (a.pitch.vibrato.has_value());
    CHECK (a.pitch.vibrato->rateHz == Approx (5.5).margin (0.2));
    CHECK (a.pitch.vibrato->depth == Approx (40.0).margin (4.0));

    const auto steady = test::analyse (ts::sine (440.0, 3.0, 44100.0));
    CHECK_FALSE (steady.pitch.vibrato.has_value());
}

TEST_CASE ("pitch: tremolo does not move the root", "[unit][analysis][pitch]")
{
    requireRoot (test::analyse (ts::tremoloSine (midiToHz (48), 6.0, 12.0, 3.0, 48000.0)), 48);
}

TEST_CASE ("pitch: noise, silence and impulses never claim a confident pitch", "[unit][analysis][pitch]")
{
    for (const auto& audio : { ts::whiteNoise (2.0, 48000.0, 0.3, 9), ts::silence (1.0, 48000.0), ts::impulse (1.0, 48000.0),
                               ts::sine (440.0, 0.01, 48000.0) })
    {
        const auto a = test::analyse (audio);
        CHECK_FALSE (a.pitch.detected);
        CHECK (a.pitch.confidence < 0.4);
        CHECK_FALSE (a.warnings.empty());
    }
}

TEST_CASE ("pitch: empty audio is handled", "[unit][analysis][pitch]")
{
    AudioData empty;
    empty.sampleRate = 48000.0;
    const auto a = test::analyse (empty);
    CHECK_FALSE (a.pitch.detected);
    CHECK_FALSE (a.warnings.empty());
}

TEST_CASE ("envelope: silences, onset, attack and peak", "[unit][analysis][envelope]")
{
    // 0.5 s silence, 0.2 s linear ramp, 1 s steady, 0.3 s silence.
    const double rate = 48000.0;
    auto audio = AudioData::allocate (1, static_cast<std::int64_t> (2.0 * rate), rate);
    const auto tone = ts::sine (330.0, 2.0, rate);
    for (std::size_t i = 0; i < audio.channels[0].size(); ++i)
    {
        const double t = static_cast<double> (i) / rate;
        double g = 0.0;
        if (t >= 0.5 && t < 0.7) g = (t - 0.5) / 0.2;
        else if (t >= 0.7 && t < 1.7) g = 1.0;
        audio.channels[0][i] = static_cast<float> (g * tone.channels[0][i]);
    }

    const auto e = test::analyse (audio).envelope;
    CHECK (e.leadingSilenceSeconds == Approx (0.5).margin (0.01));
    CHECK (e.trailingSilenceSeconds == Approx (0.3).margin (0.01));
    CHECK (e.onsetSeconds == Approx (0.52).margin (0.02));   // -20 dB re max on a linear ramp
    CHECK (e.attackSeconds == Approx (0.14).margin (0.03));  // to -3 dB
    CHECK (e.peakSeconds > 0.65);
    CHECK (e.peakDbfs == Approx (-6.02).margin (0.05));
    CHECK_FALSE (e.endsWhileSounding);
    CHECK (e.rmsDb.values.size() > 190);
}

TEST_CASE ("envelope: decaying pluck vs sustained tone vs tremolo", "[unit][analysis][envelope]")
{
    const auto pluck = test::analyse (ts::pluck (110.0, 3.0, 48000.0, 4)).envelope;
    CHECK (pluck.decaySlopeDbPerSecond < -3.0);
    CHECK_FALSE (pluck.tremolo.has_value());
    REQUIRE_FALSE (pluck.onsets.empty());
    CHECK (pluck.onsets.front().timeSeconds < 0.03);

    const auto sustained = test::analyse (ts::sine (220.0, 3.0, 48000.0)).envelope;
    CHECK (sustained.endsWhileSounding);
    CHECK (std::abs (sustained.decaySlopeDbPerSecond) < 0.5);
    CHECK (sustained.sustainFluctuationDb < 0.5);

    const auto trem = test::analyse (ts::tremoloSine (220.0, 6.0, 12.0, 3.0, 48000.0)).envelope;
    REQUIRE (trem.tremolo.has_value());
    CHECK (trem.tremolo->rateHz == Approx (6.0).margin (0.3));
    CHECK (trem.tremolo->depth == Approx (12.0).margin (2.0));
}

TEST_CASE ("envelope: secondary peaks are counted", "[unit][analysis][envelope]")
{
    const double rate = 48000.0;
    auto audio = ts::sine (220.0, 3.0, rate);
    for (std::size_t i = 0; i < audio.channels[0].size(); ++i)
    {
        const double t = static_cast<double> (i) / rate;
        // two swells: peak at 0.5 s, dip, second peak at 2.0 s
        const double g = std::exp (-std::pow ((t - 0.5) / 0.25, 2.0)) + 0.7 * std::exp (-std::pow ((t - 2.0) / 0.25, 2.0));
        audio.channels[0][i] *= static_cast<float> (g);
    }
    CHECK (test::analyse (audio).envelope.secondaryPeakCount == 1);
}

TEST_CASE ("spectrum: centroid, flatness and harmonicity behave", "[unit][analysis][spectrum]")
{
    const auto sine = test::analyse (ts::sine (1000.0, 1.0, 48000.0)).spectral;
    CHECK (sine.meanCentroidHz == Approx (1000.0).margin (30.0));
    CHECK (sine.meanFlatness < 0.01);
    CHECK (sine.harmonicEnergyRatio > 0.95);
    CHECK (sine.periodicity > 0.95);
    CHECK (sine.highFrequencyEnergyRatio < 0.01);

    const auto noise = test::analyse (ts::whiteNoise (1.0, 48000.0, 0.3, 3)).spectral;
    CHECK (noise.meanCentroidHz == Approx (12000.0).margin (600.0));
    CHECK (noise.meanFlatness > 0.5);
    CHECK (noise.harmonicEnergyRatio < 0.2);
    CHECK (noise.highFrequencyEnergyRatio > 0.75);

    const auto low = test::analyse (ts::sine (100.0, 1.0, 48000.0)).spectral;
    CHECK (low.lowFrequencyEnergyRatio > 0.95);

    const auto saw = test::analyse (ts::saw (220.0, 1.0, 48000.0)).spectral;
    CHECK (saw.meanCentroidHz == Approx (220.0 * 3.1).epsilon (0.15)); // power-weighted: f * H(n) / (pi^2 / 6)
    CHECK (saw.harmonicEnergyRatio > 0.9);
    CHECK (saw.meanRolloffHz > saw.meanCentroidHz);
}

TEST_CASE ("stereo: mono, dual mono, decorrelated and out-of-phase", "[unit][analysis][stereo]")
{
    const auto mono = test::analyse (ts::sine (220.0, 1.0, 48000.0, 0.5, 1)).stereo;
    CHECK (mono.isMono);

    const auto dual = test::analyse (ts::sine (220.0, 1.0, 48000.0, 0.5, 2)).stereo;
    CHECK_FALSE (dual.isMono);
    CHECK (dual.isDualMono);
    CHECK (dual.correlation == Approx (1.0));
    CHECK (dual.width == Approx (0.0).margin (1e-9));

    const auto wide = test::analyse (ts::whiteNoise (2.0, 48000.0, 0.3, 4, 2)).stereo;
    CHECK (wide.correlation == Approx (0.0).margin (0.02));
    CHECK (wide.width == Approx (0.5).margin (0.02));
    CHECK (wide.widthLow == Approx (0.5).margin (0.1));
    CHECK (wide.widthHigh == Approx (0.5).margin (0.05));

    auto antiPhase = ts::sine (220.0, 1.0, 48000.0, 0.5, 2);
    for (auto& s : antiPhase.channels[1])
        s = -s;
    const auto anti = test::analyse (antiPhase).stereo;
    CHECK (anti.correlation == Approx (-1.0));
    CHECK (anti.width == Approx (1.0));

    auto left = ts::sine (220.0, 1.0, 48000.0, 0.5, 2);
    for (auto& s : left.channels[1])
        s *= 0.5f;
    CHECK (test::analyse (left).stereo.balanceDb == Approx (6.02).margin (0.05));
}
