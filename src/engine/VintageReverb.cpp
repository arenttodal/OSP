#include "engine/VintageReverb.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace osp
{

namespace
{
    constexpr double twoPi = 2.0 * std::numbers::pi;

    /** A variant's lengths (ms at SIZE 50 %) and voicing. Our own numbers. */
    struct Voicing
    {
        double half[2][4];   ///< per half: allpass 1, line 1, allpass 2, line 2
        double diff[4];      ///< the input diffusers
        float tank;          ///< allpass 1's gain (on the 1/32 grid)
        double dark;         ///< added to DAMP's pole
    };
    constexpr Voicing hall { { { 19.7, 131.3, 54.1, 97.9 }, { 27.3, 139.9, 79.7, 113.3 } }, { 4.3, 3.1, 11.9, 8.7 }, 0.5f, 0.1 };
    constexpr Voicing plate { { { 9.1, 61.3, 27.7, 49.1 }, { 12.9, 67.1, 37.3, 55.7 } }, { 2.9, 2.1, 7.7, 5.3 }, 0.6f, -0.05 };
    constexpr Voicing chamber { { { 14.3, 89.9, 39.7, 71.3 }, { 19.1, 97.1, 55.3, 79.9 } }, { 3.7, 2.7, 9.9, 7.1 }, 0.5f, 0.05 };
    constexpr Voicing room { { { 8.3, 49.3, 21.1, 37.9 }, { 11.3, 53.9, 29.9, 43.1 } }, { 2.3, 1.7, 5.9, 4.1 }, 0.45f, 0.1 };
    const Voicing& voicingOf (VintageVariant v) noexcept
    {
        return v == VintageVariant::plate ? plate : (v == VintageVariant::chamber ? chamber : (v == VintageVariant::room ? room : hall));
    }

    float grid (double v, double steps) noexcept { return static_cast<float> (std::round (v * steps) / steps); }
    constexpr double maxChorus = 10.0;   ///< internal samples at the early rate, MOD 100 %

    int powerOfTwoAtLeast (int n)
    {
        int s = 1;
        while (s < n)
            s <<= 1;
        return s;
    }
}

//==============================================================================
VintageReverb::Settings VintageReverb::Settings::from (const Shaping& s) noexcept
{
    const auto snap = [] (double v) { return std::round (v * 100.0) / 100.0; };   // (as the rooms')
    Settings r;
    r.decaySeconds = s.spaceDecaySeconds;
    r.preDelayMs = s.spacePreDelayMs;
    r.size = s.spaceSize;
    r.damping = s.spaceDamping;
    r.modulation = s.spaceModulation;
    r.width = s.spaceWidth;
    r.lowCutHz = snap (s.spaceLowCutHz);
    r.highCutHz = snap (s.spaceHighCutHz);
    r.variant = s.vintageVariant;
    r.era = s.vintageEra;
    r.diffusion = s.vintageDiffusion;
    return r;
}

bool VintageReverb::Settings::structurallyDifferent (const Settings& o) const noexcept
{
    return variant != o.variant || era != o.era || std::abs (size - o.size) > 1.0e-9;
}

//==============================================================================
void VintageReverb::Delay::allocate (int samples)
{
    const int size = powerOfTwoAtLeast (std::max (8, samples));
    buffer.assign (static_cast<std::size_t> (size), 0.0f);
    mask = size - 1;
    write = 0;
}

void VintageReverb::Delay::clear() noexcept
{
    std::fill (buffer.begin(), buffer.end(), 0.0f);
    write = 0;
}

float VintageReverb::Delay::tapLinear (double delay) const noexcept
{
    // Two adjacent taps crossfaded: the chorus moves between them.
    const auto i = static_cast<int> (delay);
    const auto t = static_cast<float> (delay - i);
    const float a = tap (i), b = tap (i + 1);
    return a + t * (b - a);
}

float VintageReverb::Svf::lowPass (float x, float g, float k) noexcept
{
    const float a1 = 1.0f / (1.0f + g * (g + k)), a2 = g * a1, a3 = g * a2;
    const float v3 = x - ic2;
    const float v1 = a1 * ic1 + a2 * v3;
    const float v2 = ic2 + a2 * ic1 + a3 * v3;
    ic1 = 2.0f * v1 - ic1;
    ic2 = 2.0f * v2 - ic2;
    return v2;
}

float VintageReverb::Svf::highPass (float x, float g, float k) noexcept
{
    const float a1 = 1.0f / (1.0f + g * (g + k)), a2 = g * a1, a3 = g * a2;
    const float v3 = x - ic2;
    const float v1 = a1 * ic1 + a2 * v3;
    const float v2 = ic2 + a2 * ic1 + a3 * v3;
    ic1 = 2.0f * v1 - ic1;
    ic2 = 2.0f * v2 - ic2;
    return x - k * v1 - v2;
}

void VintageReverb::Steep::design (double cutoffHz, double sampleRate) noexcept
{
    // Butterworth, 6th order: three sections at Q 0.518, 0.707, 1.932 (RBJ low-pass biquads).
    constexpr double qs[3] = { 0.51763809, 0.70710678, 1.93185165 };
    const double w0 = twoPi * std::min (cutoffHz, 0.45 * sampleRate) / sampleRate;
    const double cw = std::cos (w0), sw = std::sin (w0);
    for (std::size_t i = 0; i < 3; ++i)
    {
        const double alpha = sw / (2.0 * qs[i]);
        const double a0 = 1.0 + alpha;
        c[i] = { 0.5 * (1.0 - cw) / a0, (1.0 - cw) / a0, 0.5 * (1.0 - cw) / a0, -2.0 * cw / a0, (1.0 - alpha) / a0 };
    }
}

float VintageReverb::Steep::process (float in) noexcept
{
    double x = in;
    for (std::size_t i = 0; i < 3; ++i)
    {
        const auto& k = c[i];
        auto& s = z[i];
        const double y = k[0] * x + s[0];
        s[0] = k[1] * x - k[3] * y + s[1];
        s[1] = k[2] * x - k[4] * y;
        x = y;
    }
    return static_cast<float> (x);
}

float VintageReverb::quantise16 (float x) noexcept
{
    // The 16-bit delay memory: dithered (seeded), rounded, saturating.
    const float v = std::round (x * 32768.0f + 0.5f * static_cast<float> (rng.bipolar())) / 32768.0f;
    return std::clamp (v, -1.0f, 32767.0f / 32768.0f);
}

float VintageReverb::convert12 (float x) noexcept
{
    // A 12-bit converter that ranges its gain in four 6 dB steps: a quiet signal keeps its
    // resolution, a loud one its headroom; what is left is a gentle, level-dependent grain.
    const float a = std::abs (x);
    const float range = a > 0.5f ? 1.0f : (a > 0.25f ? 0.5f : (a > 0.125f ? 0.25f : 0.125f));
    const float stepSize = range / 2048.0f;
    return std::clamp (std::round (x / stepSize) * stepSize, -1.0f, 1.0f);
}

//==============================================================================
void VintageReverb::prepare (double sampleRate)
{
    hostRate = sampleRate;
    pre.allocate (static_cast<int> (0.26 * sampleRate) + 8);
    // The network at the higher era's rate, the longest variant at the largest SIZE.
    const double r = internalRate (VintageEra::later), sf = shaping::spaceSizeFactor (1.0);
    auto samples = [r, sf] (double ms) { return static_cast<int> (ms * sf * 0.001 * r) + 8; };
    for (std::size_t i = 0; i < 4; ++i)
        diffusers[i].allocate (samples (hall.diff[i]));
    const int chorus = static_cast<int> (2.0 * maxChorus * r / 20480.0) + 4;
    for (std::size_t h = 0; h < 2; ++h)
    {
        halves[h].ap1.allocate (samples (hall.half[h][0]));
        halves[h].line1.allocate (samples (hall.half[h][1]) + chorus);
        halves[h].ap2.allocate (samples (hall.half[h][2]));
        halves[h].line2.allocate (samples (hall.half[h][3]));
    }
    dcCoef = static_cast<float> (1.0 - std::exp (-twoPi * 18.0 / sampleRate));
    configure (current);
    reset();
}

void VintageReverb::reset() noexcept
{
    pre.clear();
    for (auto& d : diffusers)
        d.clear();
    for (auto& h : halves)
    {
        for (auto* d : { &h.ap1, &h.line1, &h.ap2, &h.line2 })
            d->clear();
        h.lp = h.out = 0.0f;
        h.offset = h.offsetTarget = h.offsetStep = 0.0;
        h.countdown = 0;
    }
    for (auto& f : lowCut)
        f = {};
    for (auto& f : highCut)
        f = {};
    antiAlias.clear();
    reconstructL.clear();
    reconstructR.clear();
    history = {};
    nextTime = 0.0;
    lastL = lastR = prevL = prevR = 0.0f;
    lastTickAge = 0.0;
    rng.reseed (0x56494e54ull);
    dcL = dcR = 0.0f;
    preSamples = preTarget;
    lowCutOct = lowCutTarget;
    highCutOct = highCutTarget;
    width = widthTarget;
    filterCountdown = 0;
    updateFilters();
}

void VintageReverb::configure (const Settings& s) noexcept
{
    current = s;
    rate = internalRate (current.era);
    step = hostRate / rate;
    const auto& v = voicingOf (current.variant);
    const double sf = shaping::spaceSizeFactor (current.size);
    auto samples = [this, sf] (double ms) { return std::max (1, static_cast<int> (std::round (ms * sf * 0.001 * rate))); };
    for (std::size_t i = 0; i < 4; ++i)
        diffLength[i] = samples (v.diff[i]);
    for (std::size_t h = 0; h < 2; ++h)
    {
        auto& half = halves[h];
        half.ap1Length = samples (v.half[h][0]);
        half.line1Length = samples (v.half[h][1]);
        half.ap2Length = samples (v.half[h][2]);
        half.line2Length = samples (v.half[h][3]);
    }
    tankGain = grid (v.tank, 32.0);
    apGain2 = grid (0.5, 32.0);
    // Each side reads both halves (our own tap points along the lines).
    auto tap = [this] (int half, int line, double fraction, float sign) {
        const auto& h = halves[static_cast<std::size_t> (half)];
        const double length = line == 0 ? h.line1Length : static_cast<double> (h.line2Length);
        return Tap { half, line, std::max (1, static_cast<int> (fraction * length)), sign };
    };
    taps[0] = { tap (0, 0, 0.08, 1.0f), tap (0, 0, 0.71, 1.0f), tap (0, 1, 0.43, -1.0f), tap (1, 0, 0.47, -1.0f), tap (1, 1, 0.29, -1.0f) };
    taps[1] = { tap (1, 0, 0.11, 1.0f), tap (1, 0, 0.66, 1.0f), tap (1, 1, 0.38, -1.0f), tap (0, 0, 0.52, -1.0f), tap (0, 1, 0.21, -1.0f) };
    // Anti-alias and reconstruction: steep, at 0.42 x the network's rate.
    antiAlias.design (0.42 * rate, hostRate);
    reconstructL.design (0.42 * rate, hostRate);
    reconstructR.design (0.42 * rate, hostRate);
    tune (current);
}

void VintageReverb::tune (const Settings& s) noexcept
{
    current.decaySeconds = std::clamp (s.decaySeconds, 0.2, 8.0);
    current.preDelayMs = std::clamp (s.preDelayMs, 0.0, 250.0);
    current.damping = s.damping;
    current.modulation = s.modulation;
    current.width = s.width;
    current.lowCutHz = s.lowCutHz;
    current.highCutHz = s.highCutHz;
    current.diffusion = std::clamp (s.diffusion, 0.0, 1.0);
    const auto& v = voicingOf (current.variant);
    // One pass of a half (its two allpasses and two lines) meets the decay gain twice (after
    // each line): each takes half of that pass's fall. On a 1/256 grid.
    const auto& h = halves[0];
    const double passSeconds = (h.ap1Length + h.line1Length + h.ap2Length + h.line2Length) / rate;
    decayGain = std::min (255.0f / 256.0f, grid (std::pow (10.0, -1.5 * passSeconds / current.decaySeconds), 256.0));
    dampCoef = grid (std::clamp (0.05 + 0.6 * current.damping + v.dark, 0.0, 0.9), 32.0);
    diffGain1 = grid (0.1 + 0.65 * current.diffusion, 32.0);
    diffGain2 = grid (0.08 + 0.55 * current.diffusion, 32.0);
    chorusDepth = maxChorus * rate / 20480.0 * std::pow (std::clamp (current.modulation, 0.0, 1.0), 1.3);
    preTarget = current.preDelayMs * 0.001 * hostRate;
    lowCutTarget = std::log2 (std::clamp (current.lowCutHz, 20.0, 2000.0));
    highCutTarget = std::log2 (std::clamp (current.highCutHz, 1000.0, std::min (20000.0, 0.45 * hostRate)));
    widthTarget = static_cast<float> (std::clamp (current.width, 0.0, 1.0));
}

void VintageReverb::updateFilters() noexcept
{
    lowCutOct += (lowCutTarget - lowCutOct) * 0.12;
    highCutOct += (highCutTarget - highCutOct) * 0.12;
    const double guard = 0.45 * hostRate;
    gLow = static_cast<float> (std::tan (std::numbers::pi * std::min (std::exp2 (lowCutOct), guard) / hostRate));
    gHigh = static_cast<float> (std::tan (std::numbers::pi * std::min (std::exp2 (highCutOct), guard) / hostRate));
}

float VintageReverb::networkTick (float x, float& left, float& right) noexcept
{
    x = convert12 (x);
    // Input diffusion: four allpasses on the 1/32 grid.
    for (std::size_t i = 0; i < diffusers.size(); ++i)
    {
        const float g = i < 2 ? diffGain1 : diffGain2;
        const float d = diffusers[i].tap (diffLength[i]);
        const float v = x + g * d;
        diffusers[i].push (quantise16 (v));
        x = d - g * v;
    }
    // The figure eight: each half takes the input and what the other half sent last.
    const float fromA = halves[0].out, fromB = halves[1].out;
    for (std::size_t i = 0; i < 2; ++i)
    {
        auto& h = halves[i];
        float a = x + (i == 0 ? fromB : fromA);
        const float d1 = h.ap1.tap (h.ap1Length);
        const float v1 = a - tankGain * d1;   // (a decay-diffusing allpass)
        h.ap1.push (quantise16 (v1));
        a = d1 + tankGain * v1;
        // The chorus: line 1 read between adjacent taps, towards seeded targets.
        if (--h.countdown <= 0)
        {
            h.countdown = static_cast<int> ((0.35 + 0.5 * (0.5 + 0.5 * rng.bipolar())) * rate);
            h.offsetTarget = chorusDepth * (0.5 + 0.5 * rng.bipolar());
            h.offsetStep = (h.offsetTarget - h.offset) / h.countdown;
        }
        h.offset += h.offsetStep;
        const float b = h.line1.tapLinear (h.line1Length + h.offset);
        h.line1.push (quantise16 (a));
        h.lp = b * (1.0f - dampCoef) + h.lp * dampCoef;
        const float c = h.lp * decayGain;
        const float d2 = h.ap2.tap (h.ap2Length);
        const float v2 = c + apGain2 * d2;
        h.ap2.push (quantise16 (v2));
        const float e = d2 - apGain2 * v2;
        h.out = h.line2.tap (h.line2Length) * decayGain;
        h.line2.push (quantise16 (e));
    }
    float out[2] = { 0.0f, 0.0f };
    for (std::size_t side = 0; side < 2; ++side)
        for (const auto& t : taps[side])
        {
            const auto& h = halves[static_cast<std::size_t> (t.half)];
            out[side] += t.sign * (t.line == 0 ? h.line1.tap (t.delay) : h.line2.tap (t.delay));
        }
    left = out[0];
    right = out[1];
    return 0.0f;
}

void VintageReverb::process (float inL, float inR, float& outL, float& outR) noexcept
{
    if (--filterCountdown <= 0)
    {
        filterCountdown = 16;
        updateFilters();
    }
    constexpr float k = 1.41421356f;
    const float l = highCut[0].lowPass (lowCut[0].highPass (inL, gLow, k), gHigh, k);
    const float r = highCut[1].lowPass (lowCut[1].highPass (inR, gLow, k), gHigh, k);
    pre.push (0.5f * (l + r));   // (a mono input, as the machines had)
    preSamples += std::clamp ((preTarget - preSamples) * 0.002, -0.25, 0.25);
    const float x = antiAlias.process (pre.tapLinear (std::max (0.0, preSamples) + 1.0));

    // Into the network's rate: a tick wherever its clock falls between the last two host
    // samples (band-limited first, so a straight line between them is enough).
    history[0] = history[1];
    history[1] = history[2];
    history[2] = history[3];
    history[3] = x;
    nextTime -= 1.0;
    lastTickAge += 1.0;
    while (nextTime <= 0.0)
    {
        const float in = history[2] + (history[3] - history[2]) * static_cast<float> (nextTime + 1.0);
        prevL = lastL;
        prevR = lastR;
        networkTick (in, lastL, lastR);
        lastTickAge = -nextTime;
        nextTime += step;
    }
    // ... and back: the network's output one tick late, between its last two values, then
    // the reconstruction filter.
    const auto f = static_cast<float> (std::clamp (lastTickAge / step, 0.0, 1.0));
    float yl = reconstructL.process (prevL + (lastL - prevL) * f);
    float yr = reconstructR.process (prevR + (lastR - prevR) * f);

    width += (widthTarget - width) * 0.001f;
    const float mid = 0.5f * (yl + yr), side = 0.5f * (yl - yr) * width;
    yl = mid + side;
    yr = mid - side;
    dcL += (yl - dcL) * dcCoef;
    dcR += (yr - dcR) * dcCoef;
    outL = (yl - dcL) * outputGain;
    outR = (yr - dcR) * outputGain;
}

} // namespace osp
