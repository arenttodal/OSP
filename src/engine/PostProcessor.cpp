#include "engine/PostProcessor.h"

#include "engine/InstrumentEngine.h"
#include "model/InstrumentModel.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace osp
{

void PostProcessor::prepare (double rate, int /*maximumBlockSize*/)
{
    sampleRate = rate;
    // Schroeder allpasses for decorrelation (3.1, 4.7, 7.3, 11.3 ms).
    const double apMs[4] = { 3.1, 4.7, 7.3, 11.3 };
    int total = 0;
    for (int i = 0; i < 4; ++i)
    {
        allpassDelay[static_cast<std::size_t> (i)] = std::max (1, static_cast<int> (apMs[i] * 0.001 * rate));
        allpassOffset[static_cast<std::size_t> (i)] = total;
        total += allpassDelay[static_cast<std::size_t> (i)];
    }
    allpassBuffer.assign (static_cast<std::size_t> (total), 0.0f);

    // Feedback delay network (mutually prime-ish lengths).
    const double fdnMs[fdnLines] = { 29.7, 37.1, 41.1, 53.3 };
    total = 0;
    for (int i = 0; i < fdnLines; ++i)
    {
        fdnLength[static_cast<std::size_t> (i)] = std::max (1, static_cast<int> (fdnMs[i] * 0.001 * rate));
        fdnOffset[static_cast<std::size_t> (i)] = total;
        total += fdnLength[static_cast<std::size_t> (i)];
    }
    fdnBuffer.assign (static_cast<std::size_t> (total), 0.0f);
    reset();
}

void PostProcessor::reset() noexcept
{
    std::fill (allpassBuffer.begin(), allpassBuffer.end(), 0.0f);
    std::fill (fdnBuffer.begin(), fdnBuffer.end(), 0.0f);
    allpassWrite.fill (0);
    fdnWrite.fill (0);
    fdnLowpass.fill (0.0f);
    for (auto* bank : { &characterL, &characterR })
        for (auto& f : *bank)
            f.reset();
    for (auto& f : resonatorBank)
        f.reset();
    tiltHighL.reset();
    tiltHighR.reset();
    tiltLowL.reset();
    tiltLowR.reset();
    character = characterTarget;
    space = spaceTarget;
    reimagined = reimaginedTarget;
    appliedCharacter = appliedSpace = appliedReimagined = -1.0;
    countdown = 0;
}

void PostProcessor::setModel (const InstrumentModel* newModel) noexcept
{
    if (newModel == model)
        return;
    model = newModel;
    numPeaks = 0;
    numResonators = 0;
    if (model != nullptr)
    {
        for (double hz : model->bodyPeaksHz)
            if (numPeaks < maxPeaks)
                peakHz[static_cast<std::size_t> (numPeaks++)] = hz;
        for (double hz : model->resonanceHz)
            if (numResonators < resonators)
                resonatorHz[static_cast<std::size_t> (numResonators++)] = hz;
        sourceWidth = model->analysis.stereo.width;
    }
    modelDirty = true;
}

void PostProcessor::setMacros (const Macros& macros) noexcept
{
    characterTarget = std::clamp (macros.character, 0.0, 1.0);
    spaceTarget = std::clamp (macros.space, 0.0, 1.0);
    reimaginedTarget = std::clamp (macros.reimagined, 0.0, 1.0);
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
    character += (characterTarget - character) * smoothing;
    space += (spaceTarget - space) * smoothing;
    reimagined += (reimaginedTarget - reimagined) * smoothing;

    if (modelDirty || std::abs (character - appliedCharacter) > 0.002)
    {
        appliedCharacter = character;
        const double d = (character - 0.5) * 2.0; // -1 larger/darker .. +1 smaller/brighter
        characterActive = std::abs (d) > 0.01;
        if (characterActive)
        {
            const double shift = std::pow (2.0, d * 5.0 / 12.0); // body resonances move up to a fourth
            for (int i = 0; i < maxPeaks; ++i)
            {
                const bool used = i < numPeaks;
                const double hz = used ? peakHz[static_cast<std::size_t> (i)] : 1000.0;
                const double g = used ? 6.0 * std::abs (d) : 0.0;
                peaking (characterL[static_cast<std::size_t> (2 * i)], sampleRate, hz, 2.0, -g);
                peaking (characterL[static_cast<std::size_t> (2 * i + 1)], sampleRate, hz * shift, 2.0, g);
                for (int k = 2 * i; k < 2 * i + 2; ++k)
                {
                    // Same coefficients for the right channel; its own state is kept (no clicks).
                    const auto& src = characterL[static_cast<std::size_t> (k)];
                    auto& dst = characterR[static_cast<std::size_t> (k)];
                    dst.b0 = src.b0;
                    dst.b1 = src.b1;
                    dst.b2 = src.b2;
                    dst.a1 = src.a1;
                    dst.a2 = src.a2;
                }
            }
            tiltHighL.setup (ShelfFilter::Type::high, sampleRate, 2500.0, 5.0 * d);
            tiltHighR.setup (ShelfFilter::Type::high, sampleRate, 2500.0, 5.0 * d);
            tiltLowL.setup (ShelfFilter::Type::low, sampleRate, 200.0, -4.0 * d);
            tiltLowR.setup (ShelfFilter::Type::low, sampleRate, 200.0, -4.0 * d);
        }
    }

    if (modelDirty || std::abs (reimagined - appliedReimagined) > 0.002)
    {
        appliedReimagined = reimagined;
        const double t60 = 0.6 + 3.0 * reimagined;
        for (int i = 0; i < numResonators; ++i)
            bandpass (resonatorBank[static_cast<std::size_t> (i)], sampleRate, resonatorHz[static_cast<std::size_t> (i)], t60);
        const double amount = std::max (0.0, reimagined - 0.1) / 0.9;
        resonanceMix = numResonators > 0 ? static_cast<float> (0.9 * amount / std::sqrt (static_cast<double> (numResonators))) : 0.0f;
    }

    if (modelDirty || std::abs (space - appliedSpace) > 0.002)
    {
        appliedSpace = space;
        sideGain = static_cast<float> (1.0 + 0.8 * space);
        // Narrow (mono-ish) sources get synthetic width from decorrelated mid.
        decorrelation = static_cast<float> (space * std::clamp (1.0 - 4.0 * sourceWidth, 0.0, 1.0) * 0.35);
        const double t60 = 0.5 + 2.0 * space;
        const double meanLength = 0.04 * sampleRate;
        fdnFeedback = static_cast<float> (std::pow (10.0, -3.0 * meanLength / (t60 * sampleRate)));
        fdnDamping = static_cast<float> (0.25 + 0.3 * (1.0 - space));
        reverbMix = static_cast<float> (0.3 * std::pow (space, 1.3));
        spaceTrim = static_cast<float> (1.0 - 0.35 * space);
    }
    modelDirty = false;
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

        if (characterActive)
        {
            for (int k = 0; k < 2 * numPeaks; ++k)
            {
                l = characterL[static_cast<std::size_t> (k)].process (l);
                r = characterR[static_cast<std::size_t> (k)].process (r);
            }
            l = tiltLowL.process (tiltHighL.process (l));
            r = tiltLowR.process (tiltHighR.process (r));
        }

        if (resonanceMix > 0.0f)
        {
            const float excite = 0.5f * (l + r);
            float even = 0.0f, odd = 0.0f;
            for (int k = 0; k < numResonators; ++k)
            {
                const float y = resonatorBank[static_cast<std::size_t> (k)].process (excite);
                ((k & 1) == 0 ? even : odd) += y;
            }
            l += resonanceMix * (0.75f * even + 0.25f * odd);
            r += resonanceMix * (0.25f * even + 0.75f * odd);
        }

        if (space > 0.001)
        {
            float mid = 0.5f * (l + r);
            float side = 0.5f * (l - r) * sideGain;
            if (decorrelation > 0.0f)
            {
                float x = mid;
                for (int k = 0; k < 4; ++k)
                {
                    const auto ks = static_cast<std::size_t> (k);
                    float* buf = allpassBuffer.data() + allpassOffset[ks];
                    const float delayed = buf[allpassWrite[ks]];
                    const float v = x + 0.6f * delayed;
                    buf[allpassWrite[ks]] = v;
                    x = delayed - 0.6f * v;
                    allpassWrite[ks] = (allpassWrite[ks] + 1) % allpassDelay[ks];
                }
                side += decorrelation * x;
            }
            l = mid + side;
            r = mid - side;

            if (reverbMix > 0.0f)
            {
                float out[fdnLines];
                for (int k = 0; k < fdnLines; ++k)
                {
                    const auto ks = static_cast<std::size_t> (k);
                    out[k] = fdnBuffer[static_cast<std::size_t> (fdnOffset[ks] + fdnWrite[ks])];
                }
                // Hadamard mixing keeps the network lossless before damping and feedback.
                const float h0 = 0.5f * (out[0] + out[1] + out[2] + out[3]);
                const float h1 = 0.5f * (out[0] - out[1] + out[2] - out[3]);
                const float h2 = 0.5f * (out[0] + out[1] - out[2] - out[3]);
                const float h3 = 0.5f * (out[0] - out[1] - out[2] + out[3]);
                const float mixed[fdnLines] = { h0, h1, h2, h3 };
                const float input = 0.5f * (l + r);
                for (int k = 0; k < fdnLines; ++k)
                {
                    const auto ks = static_cast<std::size_t> (k);
                    fdnLowpass[ks] += (mixed[k] - fdnLowpass[ks]) * (1.0f - fdnDamping);
                    fdnBuffer[static_cast<std::size_t> (fdnOffset[ks] + fdnWrite[ks])] = input + fdnFeedback * fdnLowpass[ks];
                    fdnWrite[ks] = (fdnWrite[ks] + 1) % fdnLength[ks];
                }
                l += reverbMix * (out[0] + 0.5f * out[2]);
                r += reverbMix * (out[1] + 0.5f * out[3]);
            }
            l *= spaceTrim;
            r *= spaceTrim;
        }

        left[i] = l;
        right[i] = r;
    }
}

} // namespace osp
