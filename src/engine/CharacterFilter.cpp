#include "engine/CharacterFilter.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace osp
{

namespace
{
    // Smooth odd saturator (Padé tanh), accurate to ~1 % up to |x| = 3, then clamped.
    inline float softClip (float x) noexcept
    {
        x = std::clamp (x, -3.0f, 3.0f);
        const float x2 = x * x;
        return x * (27.0f + x2) / (27.0f + 9.0f * x2);
    }
}

void CharacterFilter::prepare (double rate) noexcept
{
    sampleRate = rate;
    fadeLength = std::max (1, static_cast<int> (0.04 * rate));
    lastCutoff = lastResonance = lastDrive = -1.0;   // the rate changed: recompute everything
    reset();
}

void CharacterFilter::reset() noexcept
{
    ladderL = {};
    ladderR = {};
    svfL = {};
    svfR = {};
    tiltLowL.reset();
    tiltLowR.reset();
    tiltHighL.reset();
    tiltHighR.reset();
    fadeRemaining = 0;
    previous = current;
    appliedTilt = 1.0e9f;
}

void CharacterFilter::setParameters (FilterType type, double cutoffHz, double resonance, double drive, double tiltDb) noexcept
{
    if (type != current)
    {
        previous = current;
        current = type;
        fadeRemaining = fadeLength;
    }

    const double fc = std::clamp (cutoffHz, 20.0, 0.45 * sampleRate);
    const double res = std::clamp (resonance, 0.0, 0.9);

    // Ladder: TPT one-pole coefficient; feedback below self-oscillation (k < 4). The
    // passband loses 1/(1+k) at DC; half of it is given back, so resonance thins the
    // bass a little (as on the hardware) without jumping in level.
    // This runs every control period per voice, but resonance, drive and often the cutoff
    // hold still: each transcendental is recomputed only when its own input changed, from
    // the same expressions, so the coefficients are bit-identical to recomputing them.
    if (fc != lastCutoff || res != lastResonance || current != lastType)
    {
        if (fc != lastCutoff)
            gw = std::tan (std::numbers::pi * fc / sampleRate);
        G = static_cast<float> (gw / (1.0 + gw));
        if (res != lastResonance)
            k = static_cast<float> (3.8 * std::pow (res, 0.85));
        comp = 1.0f + 0.5f * k;

        // SVF: Q from 0.55 (soft) upwards; HP and BP stay tamer.
        if (res != lastResonance || current != lastType)
        {
            const double qMax = current == FilterType::lp12 ? 8.0 : 3.5;
            const double q = 0.55 + std::pow (res / 0.9, 1.6) * (qMax - 0.55);
            kSvf = static_cast<float> (1.0 / q);
        }
        g = static_cast<float> (gw);
        a1 = 1.0f / (1.0f + g * (g + kSvf));
        a2 = g * a1;
        a3 = g * a2;
        lastCutoff = fc;
        lastResonance = res;
        lastType = current;
    }

    // Drive: up to +20 dB into the saturator, slightly asymmetric (even harmonics), and
    // most of the gain taken back afterwards so turning DRIVE adds colour, not level.
    const double d = std::clamp (drive, 0.0, 1.0);
    if (d != lastDrive)
    {
        driveGain = static_cast<float> (std::pow (10.0, (2.0 + 18.0 * d * d) / 20.0) * 0.5);
        driveBias = static_cast<float> (0.25 * d);
        driveOut = static_cast<float> (1.0 / std::pow (driveGain, 0.85));
        lastDrive = d;
    }

    const auto tilt = static_cast<float> (std::clamp (tiltDb, -15.0, 15.0));
    if ((current == FilterType::tilt || previous == FilterType::tilt) && std::abs (tilt - appliedTilt) > 0.02f)
    {
        appliedTilt = tilt;
        tiltLowL.setup (ShelfFilter::Type::low, sampleRate, 350.0, -0.5 * tilt);
        tiltLowR.setup (ShelfFilter::Type::low, sampleRate, 350.0, -0.5 * tilt);
        tiltHighL.setup (ShelfFilter::Type::high, sampleRate, 1800.0, 0.5 * tilt);
        tiltHighR.setup (ShelfFilter::Type::high, sampleRate, 1800.0, 0.5 * tilt);
    }
}

float CharacterFilter::ladder (Ladder& st, float x) const noexcept
{
    // Zero-delay feedback: solve the loop for the input of the first stage, then
    // saturate it (the transistor pair at the ladder's input).
    const float beta = 1.0f - G;
    const float sigma = beta * (G * G * G * st.s[0] + G * G * st.s[1] + G * st.s[2] + st.s[3]);
    const float g4 = G * G * G * G;
    float u = (x - k * sigma) / (1.0f + k * g4);
    u = softClip (u + driveBias) - softClip (driveBias);
    float y = u;
    for (int i = 0; i < 4; ++i)
    {
        const float v = (y - st.s[i]) * G;
        const float out = v + st.s[i];
        st.s[i] = out + v;
        y = out;
    }
    return y * comp;
}

void CharacterFilter::svf (Svf& st, float x, float& low, float& band, float& high) const noexcept
{
    const float v0 = softClip (x + driveBias) - softClip (driveBias);
    const float v3 = v0 - st.ic2;
    const float v1 = a1 * st.ic1 + a2 * v3;
    const float v2 = st.ic2 + a2 * st.ic1 + a3 * v3;
    st.ic1 = 2.0f * v1 - st.ic1;
    st.ic2 = 2.0f * v2 - st.ic2;
    low = v2;
    band = v1;
    high = v0 - kSvf * v1 - v2;
}

void CharacterFilter::process (float& left, float& right) noexcept
{
    if (current == FilterType::off && fadeRemaining == 0)
        return;
    const bool fading = fadeRemaining > 0;
    auto active = [&] (FilterType t) { return current == t || (fading && previous == t); };
    const bool useLadder = active (FilterType::lp24);
    const bool useSvf = active (FilterType::lp12) || active (FilterType::hp12) || active (FilterType::bp12);
    const bool useTilt = active (FilterType::tilt);

    // Each topology runs once per sample; a crossfade reads two of their outputs.
    float out[2][6] {};   // per channel, indexed by FilterType: lp24, lp12, hp12, bp12, tilt, off (dry)
    const float in[2] = { left, right };
    for (int c = 0; c < 2; ++c)
    {
        out[c][5] = in[c];
        if (useLadder)
            out[c][0] = ladder (c == 0 ? ladderL : ladderR, in[c] * driveGain) * driveOut;
        if (useSvf)
        {
            float low, band, high;
            svf (c == 0 ? svfL : svfR, in[c] * driveGain, low, band, high);
            out[c][1] = low * driveOut;
            out[c][2] = high * driveOut;
            out[c][3] = 1.4f * kSvf * band * driveOut;
        }
        if (useTilt)
            out[c][4] = c == 0 ? tiltHighL.process (tiltLowL.process (in[c])) : tiltHighR.process (tiltLowR.process (in[c]));
    }
    const auto ci = static_cast<std::size_t> (current), pi = static_cast<std::size_t> (previous);
    if (fading)
    {
        // Equal-gain crossfade from the previous topology.
        const float w = static_cast<float> (fadeRemaining) / static_cast<float> (fadeLength);
        left = out[0][ci] + w * (out[0][pi] - out[0][ci]);
        right = out[1][ci] + w * (out[1][pi] - out[1][ci]);
        --fadeRemaining;
        return;
    }
    left = out[0][ci];
    right = out[1][ci];
}

} // namespace osp
