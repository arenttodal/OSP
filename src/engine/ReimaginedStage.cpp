#include "engine/ReimaginedStage.h"

#include "engine/Shaping.h"
#include "model/InstrumentModel.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace osp
{

void ReimaginedStage::prepare (double rate, std::uint64_t seed) noexcept
{
    sampleRate = rate;
    morphSeed = seed;
    reset();
}

void ReimaginedStage::reset() noexcept
{
    for (auto& f : bank)
        f.reset();
    for (auto& f : formants)
        f.reset();
    clock = 0;
    morphActive = false;
    smoothed = target;
    applied = -1.0;
}

void ReimaginedStage::setModel (const InstrumentModel* newModel) noexcept
{
    if (newModel == model)
        return;
    model = newModel;
    numResonators = 0;
    if (model != nullptr)
        for (double hz : model->resonanceHz)
            if (numResonators < resonators)
                resonatorHz[static_cast<std::size_t> (numResonators++)] = hz;
    modelDirty = true;
}

void ReimaginedStage::peaking (Biquad& f, double rate, double hz, double q, double gainDb) noexcept
{
    const double a = std::pow (10.0, gainDb / 40.0);
    const double w0 = 2.0 * std::numbers::pi * std::clamp (hz, 20.0, 0.45 * rate) / rate;
    const double alpha = std::sin (w0) / (2.0 * q);
    const double c = std::cos (w0);
    const double a0 = 1.0 + alpha / a;
    f.b0 = static_cast<float> ((1.0 + alpha * a) / a0);
    f.b1 = static_cast<float> (-2.0 * c / a0);
    f.b2 = static_cast<float> ((1.0 - alpha * a) / a0);
    f.a1 = static_cast<float> (-2.0 * c / a0);
    f.a2 = static_cast<float> ((1.0 - alpha / a) / a0);
}

void ReimaginedStage::bandpass (Biquad& f, double rate, double hz, double t60) noexcept
{
    // Two-pole resonator with a given decay time; zeros at DC and Nyquist keep it from
    // ringing on rumble or hiss. Normalised to roughly unit gain at the centre.
    const double w = 2.0 * std::numbers::pi * std::clamp (hz, 30.0, 0.45 * rate) / rate;
    const double r = std::pow (10.0, -3.0 / (std::max (0.05, t60) * rate));
    const double g = (1.0 - r * r) * 0.5;
    f.b0 = static_cast<float> (g);
    f.b1 = 0.0f;
    f.b2 = static_cast<float> (-g);
    f.a1 = static_cast<float> (-2.0 * r * std::cos (w));
    f.a2 = static_cast<float> (r * r);
}

void ReimaginedStage::update() noexcept
{
    const double smoothing = 0.06;
    smoothed += (target - smoothed) * smoothing;

    if (modelDirty || std::abs (smoothed - applied) > 0.002)
    {
        applied = smoothed;
        const double t60 = 0.6 + 4.5 * smoothed;
        for (int i = 0; i < numResonators; ++i)
        {
            const auto hz = resonatorHz[static_cast<std::size_t> (i)];
            bandpass (bank[static_cast<std::size_t> (i)], sampleRate, hz, t60);
            bandpass (bank[static_cast<std::size_t> (i + resonators)], sampleRate, std::min (1.5 * hz, 0.45 * sampleRate), t60);
        }
        // Audible from the middle of the range (lab, continuum-1: 0..100 % sounded alike).
        const double amountNow = std::max (0.0, smoothed - 0.1) / 0.9;
        resonanceMix = numResonators > 0 ? static_cast<float> (2.2 * amountNow / std::sqrt (static_cast<double> (numResonators))) : 0.0f;
        remapMix = static_cast<float> (resonanceMix * 0.7 * std::clamp ((smoothed - 0.5) / 0.5, 0.0, 1.0));
    }
    modelDirty = false;

    // Spectral evolution: formants around vowel regions (F1 350-900 Hz, F2 1.1-2.6 kHz)
    // wander independently at a few seconds per move; depth grows past 40 % Reimagined.
    morph = std::clamp ((smoothed - 0.4) / 0.6, 0.0, 1.0);
    if (morph > 1.0e-4)
    {
        if (! morphActive)
            for (auto& f : formants)
                f.reset();
        morphActive = true;
        const double t = static_cast<double> (clock) / sampleRate;
        const double w1 = 0.5 + 0.5 * shaping::sharedWander (morphSeed ^ 0x6631ull, t, 0.11);
        const double w2 = 0.5 + 0.5 * shaping::sharedWander (morphSeed ^ 0x6632ull, t, 0.07);
        const double f1 = shaping::logLerp (350.0, 900.0, w1);
        const double f2 = shaping::logLerp (1100.0, 2600.0, w2);
        const double gainDb = 10.0 * std::pow (morph, 0.8);
        peaking (formants[0], sampleRate, f1, 2.8, gainDb);
        peaking (formants[1], sampleRate, f2, 3.5, 0.8 * gainDb);
        formants[2].b0 = formants[0].b0; formants[2].b1 = formants[0].b1; formants[2].b2 = formants[0].b2;
        formants[2].a1 = formants[0].a1; formants[2].a2 = formants[0].a2;
        formants[3].b0 = formants[1].b0; formants[3].b1 = formants[1].b1; formants[3].b2 = formants[1].b2;
        formants[3].a1 = formants[1].a1; formants[3].a2 = formants[1].a2;
    }
    else
        morphActive = false;
}

} // namespace osp
