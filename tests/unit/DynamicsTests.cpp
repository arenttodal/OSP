#include "audio/utility/TestSignals.h"
#include "core/Fft.h"
#include "engine/InstrumentBuilder.h"
#include "engine/InstrumentEngine.h"
#include "research/RenderSession.h"
#include "support/TestHelpers.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <complex>
#include <numbers>

using namespace osp;

namespace
{
    NoteShape shapeAt (int velocity, double macro, DynamicsMode mode)
    {
        NoteShape s;
        DynamicsProfile profile;
        SourceCharacter character;
        character.transientTonal = 0.5;
        InstrumentEngine::applyDynamics (s, velocity, profile, character, macro, mode);
        return s;
    }

    /** Power-weighted spectral centroid of 8192 samples of the left channel from `from` seconds. */
    double centroidOf (const AudioData& audio, double from, double)
    {
        const Fft fft (13);
        const int n = fft.size();
        const auto a = static_cast<std::size_t> (from * audio.sampleRate);
        std::vector<std::complex<double>> buf (static_cast<std::size_t> (n));
        for (int i = 0; i < n; ++i)
        {
            const double w = 0.5 - 0.5 * std::cos (2.0 * std::numbers::pi * i / n);
            buf[static_cast<std::size_t> (i)] = { audio.channels[0][a + static_cast<std::size_t> (i)] * w, 0.0 };
        }
        fft.forward (buf.data());
        double num = 0.0, den = 0.0;
        for (int k = 1; k < n / 2; ++k)
        {
            const double p = std::norm (buf[static_cast<std::size_t> (k)]);
            num += p * k * audio.sampleRate / n;
            den += p;
        }
        return num / den;
    }
}

TEST_CASE ("dynamics: gain-only and DYNAMICS 0 leave timbre alone", "[unit][dynamics]")
{
    for (int v : { 20, 100, 127 })
    {
        const auto a = shapeAt (v, 0.5, DynamicsMode::gainOnly);
        const auto b = shapeAt (v, 0.0, DynamicsMode::full);
        for (const auto& s : { a, b })
        {
            CHECK (s.brightnessDb == 0.0f);
            CHECK (s.transientDb == 0.0f);
            CHECK (s.attackSoftenSeconds == 0.0f);
        }
    }
}

TEST_CASE ("dynamics: harder is brighter with more bite, softer is darker and gentler", "[unit][dynamics]")
{
    const auto soft = shapeAt (20, 0.5, DynamicsMode::full);
    const auto mid = shapeAt (100, 0.5, DynamicsMode::full);
    const auto hard = shapeAt (127, 0.5, DynamicsMode::full);
    CHECK (soft.brightnessDb < mid.brightnessDb);
    CHECK (mid.brightnessDb < hard.brightnessDb);
    CHECK (soft.transientDb < 0.0f);
    CHECK (hard.transientDb > 0.0f);
    CHECK (soft.attackSoftenSeconds > 0.0f);
    CHECK (hard.attackSoftenSeconds == 0.0f);
    CHECK (hard.pitchSettleCents > soft.pitchSettleCents);
    CHECK (std::abs (mid.brightnessDb) < 0.01f); // velocity 100 = as recorded
    // DYNAMICS 1 is twice the calibrated effect.
    CHECK (std::abs (shapeAt (20, 1.0, DynamicsMode::full).brightnessDb - 2.0f * soft.brightnessDb) < 1.0e-4f);
}

TEST_CASE ("dynamics: a crescendo changes the spectrum, not only the level", "[integration][dynamics]")
{
    auto audio = testsignals::saw (220.0, 2.0, 48000.0);
    testsignals::applyFades (audio, 0.01, 0.2);
    const auto model = instrument::buildComplete (audio, test::analyse (audio), {}, false);
    auto render = [&] (DynamicsMode mode, int velocity) {
        research::RenderConfig config;
        config.engineSettings.dynamicsMode = mode;
        config.engineSettings.macros.life = 0.0;
        config.engineSettings.macros.dynamics = 0.5;
        MidiSequence s;
        s.events.push_back ({ 0.0, MidiEvent::Type::noteOn, 57, velocity, 1 });
        s.events.push_back ({ 0.8, MidiEvent::Type::noteOff, 57, 0, 1 });
        return research::renderInstrument (*model, s, config).audio;
    };
    const double softFull = centroidOf (render (DynamicsMode::full, 20), 0.2, 0.5);
    const double hardFull = centroidOf (render (DynamicsMode::full, 127), 0.2, 0.5);
    const double softGain = centroidOf (render (DynamicsMode::gainOnly, 20), 0.2, 0.5);
    const double hardGain = centroidOf (render (DynamicsMode::gainOnly, 127), 0.2, 0.5);
    CHECK (12.0 * std::log2 (hardFull / softFull) > 2.0);
    CHECK (std::abs (12.0 * std::log2 (hardGain / softGain)) < 0.2);
}
