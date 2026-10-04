#include "engine/PostProcessor.h"

#include "engine/InstrumentEngine.h"
#include "engine/Shaping.h"
#include "model/InstrumentModel.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace osp
{

void PostProcessor::prepare (double rate, int /*maximumBlockSize*/, std::uint64_t seed)
{
    sampleRate = rate;
    morphSeed = seed;
    movement.prepare (rate, seed);
    for (auto& r : reverbs)
        r.prepare (rate);
    reverbFadeLength = std::max (1, static_cast<int> (0.25 * rate));
    spaceCoef = 1.0 - std::exp (-1.0 / (0.05 * rate));
    reset();
}

void PostProcessor::reset() noexcept
{
    for (auto& f : resonatorBank)
        f.reset();
    for (auto& f : formants)
        f.reset();
    clock = 0;
    morphActive = false;
    reimagined = reimaginedTarget;
    space = spaceTarget;
    appliedReimagined = -1.0;
    countdown = 0;
    movement.setTargets (shaping, motionTarget);
    movement.reset();
    appliedType = shaping.spaceType;
    appliedDecay = shaping.spaceDecaySeconds;
    activeReverb = 0;
    reverbFade = 0;
    for (auto& r : reverbs)
    {
        r.configure (appliedType, appliedDecay);
        r.reset();
    }
    spaceIdle = space < 1.0e-5;
}

void PostProcessor::setModel (const InstrumentModel* newModel) noexcept
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

void PostProcessor::setMacros (const Macros& macros) noexcept
{
    reimaginedTarget = std::clamp (macros.reimagined, 0.0, 1.0);
    motionTarget = std::clamp (macros.motion, 0.0, 1.0);
    // SPACE is a send: perceptual wet level (10 % is a touch, 100 % is drenched).
    spaceTarget = 1.25 * std::pow (std::clamp (macros.space, 0.0, 1.0), 1.2);
    movement.setTargets (shaping, motionTarget);
}

void PostProcessor::setShaping (const Shaping& newShaping) noexcept
{
    shaping = newShaping;
    movement.setTargets (shaping, motionTarget);
}

void PostProcessor::peaking (Biquad& f, double rate, double hz, double q, double gainDb) noexcept
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

void PostProcessor::bandpass (Biquad& f, double rate, double hz, double t60) noexcept
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

void PostProcessor::updateCoefficients() noexcept
{
    const double smoothing = 0.06;
    reimagined += (reimaginedTarget - reimagined) * smoothing;

    if (modelDirty || std::abs (reimagined - appliedReimagined) > 0.002)
    {
        appliedReimagined = reimagined;
        const double t60 = 0.6 + 4.5 * reimagined;
        for (int i = 0; i < numResonators; ++i)
        {
            const auto hz = resonatorHz[static_cast<std::size_t> (i)];
            bandpass (resonatorBank[static_cast<std::size_t> (i)], sampleRate, hz, t60);
            bandpass (resonatorBank[static_cast<std::size_t> (i + resonators)], sampleRate, std::min (1.5 * hz, 0.45 * sampleRate), t60);
        }
        // Audible from the middle of the range (lab, continuum-1: 0..100 % sounded alike).
        const double amount = std::max (0.0, reimagined - 0.1) / 0.9;
        resonanceMix = numResonators > 0 ? static_cast<float> (2.2 * amount / std::sqrt (static_cast<double> (numResonators))) : 0.0f;
        remapMix = static_cast<float> (resonanceMix * 0.7 * std::clamp ((reimagined - 0.5) / 0.5, 0.0, 1.0));
    }
    modelDirty = false;

    // Spectral evolution: formants around vowel regions (F1 350-900 Hz, F2 1.1-2.6 kHz)
    // wander independently at a few seconds per move; depth grows past 40 % Reimagined.
    morph = std::clamp ((reimagined - 0.4) / 0.6, 0.0, 1.0);
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

    // SPACE: a new type fades in on the idle reverb; a new decay retunes in place.
    if (shaping.spaceType != appliedType && reverbFade == 0)
    {
        appliedType = shaping.spaceType;
        appliedDecay = shaping.spaceDecaySeconds;
        activeReverb = 1 - activeReverb;
        auto& next = reverbs[static_cast<std::size_t> (activeReverb)];
        next.configure (appliedType, appliedDecay);
        next.reset();
        reverbFade = reverbFadeLength;
    }
    else if (std::abs (shaping.spaceDecaySeconds - appliedDecay) > 1.0e-3)
    {
        // Gradual: at most 3 % per control tick, so the tail never jumps.
        appliedDecay += std::clamp (shaping.spaceDecaySeconds - appliedDecay, -0.03 * appliedDecay, 0.03 * appliedDecay);
        reverbs[static_cast<std::size_t> (activeReverb)].configure (appliedType, appliedDecay);
    }
}

void PostProcessor::process (float* left, float* right, int numSamples) noexcept
{
    for (int i = 0; i < numSamples; ++i)
    {
        if (countdown-- <= 0)
        {
            countdown = controlInterval - 1;
            updateCoefficients();
        }
        float l = left[i];
        float r = right[i];

        if (resonanceMix > 0.0f)
        {
            const float excite = 0.5f * (l + r);
            float even = 0.0f, odd = 0.0f;
            for (int k = 0; k < numResonators; ++k)
            {
                const float y = resonatorBank[static_cast<std::size_t> (k)].process (excite);
                ((k & 1) == 0 ? even : odd) += y;
            }
            if (remapMix > 0.0f)
                for (int k = 0; k < numResonators; ++k)
                {
                    const float y = remapMix / resonanceMix * resonatorBank[static_cast<std::size_t> (k + resonators)].process (excite);
                    ((k & 1) == 0 ? odd : even) += y; // the other side: the halo spreads
                }
            l += resonanceMix * (0.75f * even + 0.25f * odd);
            r += resonanceMix * (0.25f * even + 0.75f * odd);
        }

        if (morphActive)
        {
            // Peaks only (no cut), so the level rises a little: compensate gently.
            const auto trim = static_cast<float> (1.0 / (1.0 + 0.9 * morph));
            l = formants[1].process (formants[0].process (l)) * trim;
            r = formants[3].process (formants[2].process (r)) * trim;
        }
        ++clock;

        movement.process (l, r);

        // SPACE as a send. Idle (no wet level for a while) skips the reverb entirely.
        space += (spaceTarget - space) * spaceCoef;
        if (space > 1.0e-5 || reverbFade > 0)
        {
            if (spaceIdle)
            {
                spaceIdle = false;
                for (auto& rv : reverbs)
                    rv.reset();
            }
            float wl, wr;
            reverbs[static_cast<std::size_t> (activeReverb)].process (l, r, wl, wr);
            if (reverbFade > 0)
            {
                float ol, orr;
                reverbs[static_cast<std::size_t> (1 - activeReverb)].process (l, r, ol, orr);
                const float w = static_cast<float> (reverbFade) / static_cast<float> (reverbFadeLength);
                wl += w * (ol - wl);
                wr += w * (orr - wr);
                --reverbFade;
            }
            const auto wet = static_cast<float> (space);
            const auto dry = static_cast<float> (1.0 - 0.2 * std::min (1.0, space));
            l = dry * l + wet * wl;
            r = dry * r + wet * wr;
        }
        else
            spaceIdle = true;

        left[i] = l;
        right[i] = r;
    }
}

} // namespace osp
