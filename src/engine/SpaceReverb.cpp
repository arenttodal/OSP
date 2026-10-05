#include "engine/SpaceReverb.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace osp
{

namespace
{
    struct TypeDesign
    {
        double fdnMs[8];
        double diffMs[4];
        float diffGain;
        double erMs[8];
        float erLevel;
        double preMs;
        float damping;      // in-loop one-pole (0 bright .. 1 dark)
        double toneLowHz;   // output low-pass
        double toneHighHz;  // output high-pass
        float width;
        double modSamples48k, modRateHz;
        float outputGain;
    };

    // Curated per type (§46): the musician sees TYPE and DECAY only.
    constexpr TypeDesign room { { 7.1, 8.9, 10.3, 11.7, 13.1, 14.9, 16.7, 18.3 }, { 1.1, 1.7, 2.3, 3.1 }, 0.6f,
                                { 3.3, 5.9, 8.7, 11.1, 14.3, 17.9, 21.1, 26.3 }, 0.55f, 6.0, 0.45f, 6500.0, 90.0, 0.6f, 0.0, 0.3, 0.9f };
    constexpr TypeDesign chamber { { 17.3, 21.7, 25.1, 29.3, 33.7, 37.9, 41.3, 46.1 }, { 2.3, 3.7, 5.3, 7.1 }, 0.7f,
                                   { 7.1, 11.3, 15.7, 21.1, 27.3, 33.1, 41.9, 49.3 }, 0.3f, 14.0, 0.33f, 8500.0, 80.0, 0.85f, 6.0, 0.3, 0.75f };
    constexpr TypeDesign plate { { 11.3, 14.1, 17.9, 21.7, 25.3, 29.9, 33.1, 38.7 }, { 1.3, 2.1, 3.7, 5.9 }, 0.75f,
                                 { 1, 1, 1, 1, 1, 1, 1, 1 }, 0.0f, 4.0, 0.12f, 12000.0, 150.0, 1.0f, 12.0, 0.8, 0.7f };

    int powerOfTwoAtLeast (int n)
    {
        int s = 1;
        while (s < n)
            s <<= 1;
        return s;
    }
}

void SpaceReverb::Delay::allocate (int samples)
{
    const int size = powerOfTwoAtLeast (std::max (4, samples));
    buffer.assign (static_cast<std::size_t> (size), 0.0f);
    mask = size - 1;
    write = 0;
}

void SpaceReverb::Delay::clear() noexcept
{
    std::fill (buffer.begin(), buffer.end(), 0.0f);
    write = 0;
}

float SpaceReverb::Delay::tapFrac (double delay) const noexcept
{
    const auto i = static_cast<int> (delay);
    const auto t = static_cast<float> (delay - i);
    return tap (i) + t * (tap (i + 1) - tap (i));
}

void SpaceReverb::prepare (double rate)
{
    sampleRate = rate;
    const auto ms = [rate] (double m) { return static_cast<int> (m * 0.001 * rate) + 8; };
    pre.allocate (ms (60.0));
    er.allocate (ms (60.0));
    for (auto& d : diff)
        d.allocate (ms (10.0));
    for (auto& d : fdn)
        d.allocate (ms (50.0) + 64);
    springA.allocate (ms (60.0));
    springB.allocate (ms (60.0));
    configure (type, decay);
    reset();
}

void SpaceReverb::reset() noexcept
{
    pre.clear();
    er.clear();
    for (auto& d : diff)
        d.clear();
    for (auto& d : fdn)
        d.clear();
    springA.clear();
    springB.clear();
    damp.fill (0.0f);
    springStateA.fill (0.0f);
    springStateB.fill (0.0f);
    lowL = lowR = highStateL = highStateR = springLow = springHigh = 0.0f;
    modPhase = 0.0;
}

SpaceReverb::Portrait SpaceReverb::portrait (SpaceType t) noexcept
{
    Portrait p;
    if (t == SpaceType::spring)
    {
        p.spring = true;
        p.erMs = { 33.0, 41.0, 66.0, 82.0, 99.0, 123.0, 132.0, 164.0 };
        p.erLevel = 0.7f;
        p.damping = 0.3f;
        p.width = 0.3f;
        p.diffusionMs = 6.0;
        return p;
    }
    const TypeDesign& d = t == SpaceType::room ? room : (t == SpaceType::chamber ? chamber : plate);
    p.preMs = d.preMs;
    for (std::size_t i = 0; i < p.erMs.size(); ++i)
        p.erMs[i] = d.erMs[i];
    p.erLevel = d.erLevel;
    p.damping = d.damping;
    p.width = d.width;
    p.diffusionMs = d.diffMs[3];
    return p;
}

void SpaceReverb::configure (SpaceType newType, double decaySeconds) noexcept
{
    type = newType;
    double lo, hi;
    shaping::decayRange (type, lo, hi);
    decay = std::clamp (decaySeconds, lo, hi);
    const double rate = sampleRate;
    const auto samples = [rate] (double m) { return std::max (1, static_cast<int> (m * 0.001 * rate)); };

    if (type == SpaceType::spring)
    {
        springLengthA = samples (33.0);
        springLengthB = samples (41.0);
        springCoef = 0.68f;
        // Dispersion makes the loop longer than its delay; the gain follows the delay only.
        springGain = static_cast<float> (std::pow (10.0, -3.0 * 0.037 / decay));
        toneLow = static_cast<float> (1.0 - std::exp (-2.0 * std::numbers::pi * 4200.0 / rate));
        toneHigh = static_cast<float> (1.0 - std::exp (-2.0 * std::numbers::pi * 160.0 / rate));
        width = 0.3f;
        outputGain = 0.55f;
        return;
    }

    const TypeDesign& d = type == SpaceType::room ? room : (type == SpaceType::chamber ? chamber : plate);
    for (int i = 0; i < lines; ++i)
    {
        fdnLength[static_cast<std::size_t> (i)] = samples (d.fdnMs[i]);
        // Gain per pass for a -60 dB decay over `decay` seconds.
        fdnGain[static_cast<std::size_t> (i)] = static_cast<float> (std::pow (10.0, -3.0 * d.fdnMs[i] * 0.001 / decay));
    }
    for (int i = 0; i < diffusers; ++i)
        diffLength[static_cast<std::size_t> (i)] = samples (d.diffMs[i]);
    diffGain = d.diffGain;
    for (int i = 0; i < erTaps; ++i)
    {
        erDelay[static_cast<std::size_t> (i)] = samples (d.erMs[i]);
        const float g = 1.0f / (1.0f + 0.35f * static_cast<float> (i)); // later reflections weaker
        erGainL[static_cast<std::size_t> (i)] = (i % 2 == 0 ? 1.0f : 0.45f) * g;
        erGainR[static_cast<std::size_t> (i)] = (i % 2 == 0 ? 0.45f : 1.0f) * g;
    }
    erLevel = d.erLevel;
    preDelay = samples (d.preMs);
    dampCoef = d.damping;
    toneLow = static_cast<float> (1.0 - std::exp (-2.0 * std::numbers::pi * d.toneLowHz / rate));
    toneHigh = static_cast<float> (1.0 - std::exp (-2.0 * std::numbers::pi * d.toneHighHz / rate));
    width = d.width;
    modDepth = static_cast<float> (d.modSamples48k * rate / 48000.0);
    modRate = d.modRateHz;
    outputGain = d.outputGain;
}

void SpaceReverb::process (float inL, float inR, float& outL, float& outR) noexcept
{
    const float input = 0.5f * (inL + inR);
    float l = 0.0f, r = 0.0f;

    if (type == SpaceType::spring)
    {
        // Band-limit the drive into the tank (springs do not pass deep bass or air).
        springHigh += (input - springHigh) * toneHigh;
        float x = input - springHigh;
        springLow += (x - springLow) * toneLow;
        x = springLow;
        auto tank = [&] (Delay& line, int length, std::array<float, springStages>& state) {
            float y = x + springGain * line.tap (length);
            for (auto& s : state)
            {
                // First-order allpass (one state): the chain disperses - the spring's chirp.
                const float v = y - springCoef * s;
                y = springCoef * v + s;
                s = v;
            }
            line.push (y);
            return y;
        };
        const float a = tank (springA, springLengthA, springStateA);
        const float b = tank (springB, springLengthB, springStateB);
        l = a + 0.6f * b;
        r = b + 0.6f * a;
    }
    else
    {
        pre.push (input);
        float x = pre.tap (preDelay);

        float erL = 0.0f, erR = 0.0f;
        if (erLevel > 0.0f)
        {
            er.push (x);
            for (int i = 0; i < erTaps; ++i)
            {
                const float t = er.tap (erDelay[static_cast<std::size_t> (i)]);
                erL += erGainL[static_cast<std::size_t> (i)] * t;
                erR += erGainR[static_cast<std::size_t> (i)] * t;
            }
        }

        for (int i = 0; i < diffusers; ++i)
        {
            auto& d = diff[static_cast<std::size_t> (i)];
            const float delayed = d.tap (diffLength[static_cast<std::size_t> (i)]);
            const float v = x + diffGain * delayed;
            d.push (v);
            x = delayed - diffGain * v;
        }

        float o[lines];
        modPhase += 2.0 * std::numbers::pi * modRate / sampleRate;
        for (int i = 0; i < lines; ++i)
        {
            // Gentle, out-of-phase modulation keeps tails from ringing metallically.
            const double m = modDepth > 0.0f ? modDepth * (1.0 + std::sin (modPhase + 0.785 * i)) : 0.0;
            o[i] = fdn[static_cast<std::size_t> (i)].tapFrac (fdnLength[static_cast<std::size_t> (i)] + m);
        }
        // Fast Walsh-Hadamard transform: lossless 8x8 mixing.
        float h[lines];
        for (int i = 0; i < lines; ++i)
            h[i] = o[i];
        for (int len = 1; len < lines; len <<= 1)
            for (int i = 0; i < lines; i += 2 * len)
                for (int j = i; j < i + len; ++j)
                {
                    const float u = h[j], v = h[j + len];
                    h[j] = u + v;
                    h[j + len] = u - v;
                }
        const float norm = 0.35355339f; // 1/sqrt(8)
        for (int i = 0; i < lines; ++i)
        {
            const auto is = static_cast<std::size_t> (i);
            damp[is] += (h[i] * norm - damp[is]) * (1.0f - dampCoef);
            const float in = (i % 2 == 0 ? x : -x) * 0.5f;
            fdn[is].push (in + fdnGain[is] * damp[is]);
        }
        l = 0.5f * (o[0] - o[2] + o[4] - o[6]) + erLevel * erL;
        r = 0.5f * (o[1] - o[3] + o[5] - o[7]) + erLevel * erR;
    }

    // Width, then the type's output tone (high-pass, low-pass).
    const float mid = 0.5f * (l + r), side = 0.5f * (l - r) * width;
    l = mid + side;
    r = mid - side;
    highStateL += (l - highStateL) * toneHigh;
    highStateR += (r - highStateR) * toneHigh;
    l -= highStateL;
    r -= highStateR;
    lowL += (l - lowL) * toneLow;
    lowR += (r - lowR) * toneLow;
    outL = lowL * outputGain;
    outR = lowR * outputGain;
}

} // namespace osp
