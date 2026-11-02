#include "engine/AbyssReverb.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace osp
{

namespace
{
    constexpr double twoPi = 2.0 * std::numbers::pi;

    // Our own lengths (ms at SIZE 50 %): stage 1 a geometric series 31..101 ms, stage 2
    // longer and sparse, the right side 6.1 % longer; the diffusers short and spread.
    double stage1Ms (int i) noexcept { return 31.0 * std::pow (101.0 / 31.0, static_cast<double> (i) / 15.0); }
    constexpr double stage2Ms[AbyssReverb::stage2Lines] = { 127.3, 149.9, 173.1, 199.7 };
    constexpr double rightStretch = 1.061;
    constexpr double diffMs[4] = { 3.1, 5.3, 7.9, 11.7 };
    constexpr double diffStretch = 1.08;
    constexpr double maxLengthFactor = 2.5;
    constexpr double modSamples48k = 30.0;   ///< the moving reads' depth at MOD 100 %
    constexpr double windowSeconds = 0.9;    ///< GRAVITY's window at SIZE 50 %

    double lineRateHz (int i) noexcept { return 0.13 * std::pow (7.4, static_cast<double> (i) / 15.0); }

    int powerOfTwoAtLeast (int n)
    {
        int s = 1;
        while (s < n)
            s <<= 1;
        return s;
    }
}

//==============================================================================
AbyssReverb::Settings AbyssReverb::Settings::from (const Shaping& s) noexcept
{
    // (The EQ's corners snapped to 0.01 Hz, as the rooms': a parameter round trip's rounding
    // is never heard, so a recalled session plays bit-identically.)
    const auto snap = [] (double v) { return std::round (v * 100.0) / 100.0; };
    Settings r;
    r.decaySeconds = s.spaceDecaySeconds;
    r.preDelayMs = s.spacePreDelayMs;
    r.size = s.spaceSize;
    r.damping = s.spaceDamping;
    r.modulation = s.spaceModulation;
    r.width = s.spaceWidth;
    r.lowCutHz = snap (s.spaceLowCutHz);
    r.highCutHz = snap (s.spaceHighCutHz);
    r.gravity = s.abyssGravity;
    r.freeze = s.abyssFreeze;
    r.resonance = s.abyssResonance;
    return r;
}

bool AbyssReverb::Settings::structurallyDifferent (const Settings& other) const noexcept
{
    return std::abs (size - other.size) > 1.0e-9;
}

double AbyssReverb::sizeFactor (double size) noexcept
{
    const double s = std::clamp (size, 0.0, 1.0);
    // Geometric either side of the middle: 0.35x at 0 %, 1x at 50 %, 2.5x at 100 %.
    return s < 0.5 ? std::pow (0.35, 1.0 - s / 0.5) : std::pow (maxLengthFactor, (s - 0.5) / 0.5);
}

double AbyssReverb::effectiveDecay (double decaySeconds, double gravity) noexcept
{
    const double rt = std::clamp (decaySeconds, 0.2, 8.0);
    const double g = std::clamp (gravity, -1.0, 1.0);
    return g >= 0.0 ? rt * (1.0 + 2.0 * g) : rt * (1.0 + 0.75 * g);
}

//==============================================================================
void AbyssReverb::Delay::allocate (int samples)
{
    const int size = powerOfTwoAtLeast (std::max (8, samples));
    buffer.assign (static_cast<std::size_t> (size), 0.0f);
    mask = size - 1;
    write = 0;
}

void AbyssReverb::Delay::clear() noexcept
{
    std::fill (buffer.begin(), buffer.end(), 0.0f);
    write = 0;
}

float AbyssReverb::Delay::tapLinear (double delay) const noexcept
{
    const auto i = static_cast<int> (delay);
    const auto t = static_cast<float> (delay - i);
    const float a = tap (i), b = tap (i + 1);
    return a + t * (b - a);
}

float AbyssReverb::Delay::tapCubic (double delay) const noexcept
{
    const auto i = static_cast<int> (delay);
    const auto f = static_cast<float> (delay - i);
    const float y0 = tap (i - 1), y1 = tap (i), y2 = tap (i + 1), y3 = tap (i + 2);
    const float c1 = 0.5f * (y2 - y0);
    const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
    const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
    return ((c3 * f + c2) * f + c1) * f + y1;
}

float AbyssReverb::Svf::lowPass (float x, float g, float k) noexcept
{
    const float a1 = 1.0f / (1.0f + g * (g + k)), a2 = g * a1, a3 = g * a2;
    const float v3 = x - ic2;
    const float v1 = a1 * ic1 + a2 * v3;
    const float v2 = ic2 + a2 * ic1 + a3 * v3;
    ic1 = 2.0f * v1 - ic1;
    ic2 = 2.0f * v2 - ic2;
    return v2;
}

float AbyssReverb::Svf::highPass (float x, float g, float k) noexcept
{
    const float a1 = 1.0f / (1.0f + g * (g + k)), a2 = g * a1, a3 = g * a2;
    const float v3 = x - ic2;
    const float v1 = a1 * ic1 + a2 * v3;
    const float v2 = ic2 + a2 * ic1 + a3 * v3;
    ic1 = 2.0f * v1 - ic1;
    ic2 = 2.0f * v2 - ic2;
    return x - k * v1 - v2;
}

float AbyssReverb::ceiling (float x) noexcept
{
    // Transparent below full scale; above it a soft knee towards 2 (continuous slope), so a
    // frozen or resonant space can never run away.
    const float a = std::abs (x);
    if (a <= 1.0f)
        return x;
    const float t = a - 1.0f;
    return std::copysign (1.0f + t / (1.0f + t), x);
}

//==============================================================================
void AbyssReverb::prepare (double rate)
{
    sampleRate = rate;
    const auto ms = [rate] (double m) { return static_cast<int> (m * 0.001 * rate) + 8; };
    preL.allocate (ms (260.0));
    preR.allocate (ms (260.0));
    const double windowMax = windowSeconds * std::sqrt (maxLengthFactor) * 1000.0;
    windowL.allocate (ms (windowMax));
    windowR.allocate (ms (windowMax));
    for (auto& side : diff)
        for (auto& d : side)
            d.allocate (ms (diffMs[3] * diffStretch * maxLengthFactor));
    const int maxModulation = static_cast<int> (2.0 * modSamples48k * rate / 48000.0) + 8;
    for (int i = 0; i < stage1Lines; ++i)
        net1[static_cast<std::size_t> (i)].delay.allocate (ms (stage1Ms (i) * maxLengthFactor) + maxModulation);
    for (auto& side : net2)
        for (int i = 0; i < stage2Lines; ++i)
            side[static_cast<std::size_t> (i)].delay.allocate (ms (stage2Ms[i] * rightStretch * maxLengthFactor));
    dcCoef = static_cast<float> (1.0 - std::exp (-twoPi * 18.0 / rate));
    freezeStep = static_cast<float> (1.0 / (0.06 * rate));
    configure (current);
    reset();
}

void AbyssReverb::clearNetwork() noexcept
{
    for (auto* d : { &preL, &preR, &windowL, &windowR })
        d->clear();
    for (auto& side : diff)
        for (auto& d : side)
            d.clear();
    for (auto& l : net1)
    {
        l.delay.clear();
        l.lowState = l.highState = 0.0f;
    }
    for (auto& side : net2)
        for (auto& l : side)
        {
            l.delay.clear();
            l.lowState = l.highState = 0.0f;
        }
    for (auto& f : lowCut)
        f = {};
    for (auto& f : highCut)
        f = {};
    dcL = dcR = 0.0f;
}

void AbyssReverb::reset() noexcept
{
    clearNetwork();
    for (std::size_t i = 0; i < net1.size(); ++i)
    {
        auto& l = net1[i];
        l.phase = 0.39 * static_cast<double> (i);
        l.sinValue = std::sin (l.phase);
        l.cosValue = std::cos (l.phase);
        l.wander = l.wanderFrom = l.wanderTo = 0.0f;
    }
    wanderRng.reseed (0x41425953ull);
    modCountdown = resyncCountdown = 0;
    guards = 0;
    // A fresh instance starts at its settings.
    preSamples = preTarget;
    lowCutOct = lowCutTarget;
    highCutOct = highCutTarget;
    modDepth = modDepthTarget;
    width = widthTarget;
    windowMix = windowMixTarget;
    diffGain = diffGainTarget;
    freezeNow = freezeTarget;
    filterCountdown = 0;
    updateFilters();
}

void AbyssReverb::configure (const Settings& s) noexcept
{
    current = s;
    lengthFactor = sizeFactor (current.size);
    const double rate = sampleRate;
    for (int i = 0; i < stage1Lines; ++i)
    {
        auto& l = net1[static_cast<std::size_t> (i)];
        l.length = std::max (4.0, std::round (stage1Ms (i) * lengthFactor * 0.001 * rate));
        l.phaseStep = twoPi * lineRateHz (i) / rate;
        l.stepSin = std::sin (l.phaseStep);
        l.stepCos = std::cos (l.phaseStep);
    }
    for (int side = 0; side < 2; ++side)
        for (int i = 0; i < stage2Lines; ++i)
            net2[static_cast<std::size_t> (side)][static_cast<std::size_t> (i)].length
                = std::max (4.0, std::round (stage2Ms[i] * (side == 1 ? rightStretch : 1.0) * lengthFactor * 0.001 * rate));
    for (std::size_t i = 0; i < diffLength.size(); ++i)
    {
        diffLength[i] = std::max (1, static_cast<int> (diffMs[i] * lengthFactor * 0.001 * rate));
        diffLengthR[i] = std::max (1, static_cast<int> (diffMs[i] * diffStretch * lengthFactor * 0.001 * rate));
    }
    // GRAVITY's window: 16 taps over ~0.9 s (with SIZE), each 2.3 dB louder than the last,
    // normalised to unit power (uncorrelated taps add in power).
    const double window = windowSeconds * std::sqrt (lengthFactor) * rate;
    double power = 0.0;
    for (int k = 0; k < gravityTaps; ++k)
    {
        windowDelay[static_cast<std::size_t> (k)] = std::max (1, static_cast<int> (window * (k + 1) / gravityTaps));
        const double w = std::exp (4.0 * k / (gravityTaps - 1.0));
        windowGain[static_cast<std::size_t> (k)] = static_cast<float> (w);
        power += w * w;
    }
    for (auto& w : windowGain)
        w = static_cast<float> (w / std::sqrt (power));
    // The bands: the lows below ~250 Hz, the highs above ~3 kHz (a larger space: lower).
    lowCoef = static_cast<float> (1.0 - std::exp (-twoPi * 250.0 / std::sqrt (lengthFactor) / rate));
    highCoef = static_cast<float> (1.0 - std::exp (-twoPi * 3000.0 / std::sqrt (lengthFactor) / rate));
    tune (current);
}

void AbyssReverb::computeDecay() noexcept
{
    const double rate = sampleRate;
    const double g = std::clamp (current.gravity, -1.0, 1.0);
    const double rt = effectiveDecay (current.decaySeconds, g);
    const double resonance = std::clamp (current.resonance, 0.0, 1.0);
    // The highs' share: DAMP darkens, positive GRAVITY darkens further.
    const double treble = std::clamp (0.8 - 0.6 * std::clamp (current.damping, 0.0, 1.0) - 0.25 * std::max (0.0, g), 0.08, 1.0);
    const double rtLow = rt * 1.3, rtMid = rt * (1.0 + 2.0 * resonance), rtHigh = rt * treble;
    auto set = [rate, rtLow, rtMid, rtHigh] (Line& l) {
        const double seconds = l.length / rate;
        auto gain = [seconds] (double t) { return static_cast<float> (std::min (0.9995, std::pow (10.0, -3.0 * seconds / t))); };
        l.gainLow = gain (rtLow);
        l.gainMid = gain (rtMid);
        l.gainHigh = gain (rtHigh);
    };
    for (auto& l : net1)
        set (l);
    for (auto& side : net2)
        for (auto& l : side)
            set (l);
}

void AbyssReverb::tune (const Settings& s) noexcept
{
    current.decaySeconds = std::clamp (s.decaySeconds, 0.2, 8.0);
    current.preDelayMs = std::clamp (s.preDelayMs, 0.0, 250.0);
    current.damping = s.damping;
    current.modulation = s.modulation;
    current.width = s.width;
    current.lowCutHz = s.lowCutHz;
    current.highCutHz = s.highCutHz;
    current.gravity = std::clamp (s.gravity, -1.0, 1.0);
    current.freeze = s.freeze;
    current.resonance = std::clamp (s.resonance, 0.0, 1.0);
    computeDecay();
    preTarget = current.preDelayMs * 0.001 * sampleRate;
    lowCutTarget = std::log2 (std::clamp (current.lowCutHz, 20.0, 2000.0));
    highCutTarget = std::log2 (std::clamp (current.highCutHz, 1000.0, std::min (20000.0, 0.45 * sampleRate)));
    modDepthTarget = modSamples48k * sampleRate / 48000.0 * std::pow (std::clamp (current.modulation, 0.0, 1.0), 1.3);
    widthTarget = static_cast<float> (std::clamp (current.width, 0.0, 1.0));
    windowMixTarget = static_cast<float> (std::max (0.0, -current.gravity));
    diffGainTarget = static_cast<float> (0.72 - 0.35 * current.resonance);
    freezeTarget = current.freeze ? 1.0f : 0.0f;
}

void AbyssReverb::updateFilters() noexcept
{
    lowCutOct += (lowCutTarget - lowCutOct) * 0.12;
    highCutOct += (highCutTarget - highCutOct) * 0.12;
    const double nyquistGuard = 0.45 * sampleRate;
    gLow = static_cast<float> (std::tan (std::numbers::pi * std::min (std::exp2 (lowCutOct), nyquistGuard) / sampleRate));
    gHigh = static_cast<float> (std::tan (std::numbers::pi * std::min (std::exp2 (highCutOct), nyquistGuard) / sampleRate));
}

void AbyssReverb::advanceModulation() noexcept
{
    if (--modCountdown <= 0)
    {
        modCountdown = modulationBlock;
        for (auto& l : net1)
        {
            l.wanderFrom = l.wanderTo;
            l.wanderTo = std::clamp (0.8f * l.wanderTo + 0.45f * static_cast<float> (wanderRng.bipolar()), -1.0f, 1.0f);
        }
    }
    const float t = 1.0f - static_cast<float> (modCountdown) / static_cast<float> (modulationBlock);
    const bool resync = --resyncCountdown <= 0;
    if (resync)
        resyncCountdown = 2048;
    for (auto& l : net1)
    {
        l.wander = l.wanderFrom + (l.wanderTo - l.wanderFrom) * t;
        l.phase += l.phaseStep;
        if (resync)
        {
            l.phase = std::fmod (l.phase, twoPi);
            l.sinValue = std::sin (l.phase);
            l.cosValue = std::cos (l.phase);
        }
        else
        {
            const double s1 = l.sinValue * l.stepCos + l.cosValue * l.stepSin;
            l.cosValue = l.cosValue * l.stepCos - l.sinValue * l.stepSin;
            l.sinValue = s1;
        }
    }
    modDepth += (modDepthTarget - modDepth) * 0.0005;
}

void AbyssReverb::skip() noexcept
{
    advanceModulation();
}

void AbyssReverb::process (float inL, float inR, float& outL, float& outR) noexcept
{
    if (--filterCountdown <= 0)
    {
        filterCountdown = 16;
        updateFilters();
    }
    constexpr float k = 1.41421356f;
    float xL = highCut[0].lowPass (lowCut[0].highPass (inL, gLow, k), gHigh, k);
    float xR = highCut[1].lowPass (lowCut[1].highPass (inR, gLow, k), gHigh, k);

    preL.push (xL);
    preR.push (xR);
    preSamples += std::clamp ((preTarget - preSamples) * 0.002, -0.25, 0.25);
    const double pre = std::max (0.0, preSamples) + 1.0;
    xL = preL.tapLinear (pre);
    xR = preR.tapLinear (pre);

    // GRAVITY below 0: the input through its rising window (crossfaded in as it moves).
    windowL.push (xL);
    windowR.push (xR);
    windowMix += (windowMixTarget - windowMix) * 0.0005f;
    if (windowMix > 1.0e-4f)
    {
        float wL = 0.0f, wR = 0.0f;
        for (int t = 0; t < gravityTaps; ++t)
        {
            const auto i = static_cast<std::size_t> (t);
            wL += windowGain[i] * windowL.tap (windowDelay[i]);
            wR += windowGain[i] * windowR.tap (windowDelay[i] + 3 * t + 1);   // (the right a little apart)
        }
        xL += windowMix * (wL - xL);
        xR += windowMix * (wR - xR);
    }

    // FREEZE: the input leaves the loop as the loop turns lossless.
    freezeNow = freezeTarget > freezeNow ? std::min (freezeTarget, freezeNow + freezeStep) : std::max (freezeTarget, freezeNow - freezeStep);
    const float feed = 1.0f - freezeNow;

    // Input diffusion: four Schroeder allpasses per side.
    diffGain += (diffGainTarget - diffGain) * 0.001f;
    auto diffuse = [this] (std::array<Delay, diffusers>& chain, float x, const std::array<int, diffusers>& lengths) {
        for (std::size_t i = 0; i < chain.size(); ++i)
        {
            const float delayed = chain[i].tap (lengths[i]);
            const float v = x + diffGain * delayed;
            chain[i].push (v);
            x = delayed - diffGain * v;
        }
        return x;
    };
    const float dL = feed * diffuse (diff[0], xL, diffLength);
    const float dR = feed * diffuse (diff[1], xR, diffLengthR);

    advanceModulation();

    // A line's two-band decay, opened to lossless by FREEZE.
    auto decay = [this] (Line& line, float a) {
        line.lowState += (a - line.lowState) * lowCoef;
        line.highState += (a - line.highState) * highCoef;
        const float f = line.gainHigh * a + (line.gainMid - line.gainHigh) * line.highState + (line.gainLow - line.gainMid) * line.lowState;
        return f + freezeNow * (a - f);
    };

    // Stage 1: 16 moving lines, Householder mixed (x - 2/N sum: lossless).
    std::array<float, stage1Lines> y {}, f {};
    float sum = 0.0f;
    for (std::size_t i = 0; i < net1.size(); ++i)
    {
        auto& line = net1[i];
        const double offset = modDepth * (1.0 + 0.6 * line.sinValue + 0.4 * static_cast<double> (line.wander));
        y[i] = line.delay.tapCubic (line.length + offset);
        f[i] = decay (line, y[i]);
        sum += f[i];
    }
    const float h1 = 2.0f / static_cast<float> (stage1Lines) * sum;
    for (std::size_t i = 0; i < net1.size(); ++i)
    {
        const float in = (i % 2 == 0 ? dL : dR) * ((i & 2u) == 0 ? 0.25f : -0.25f);
        net1[i].delay.push (ceiling (in + f[i] - h1));
    }

    // Stage 2: per side, four longer lines fed by that side's half of stage 1.
    float out2[2] = { 0.0f, 0.0f };
    for (std::size_t side = 0; side < 2; ++side)
    {
        float feed2 = 0.0f;
        for (std::size_t i = side; i < net1.size(); i += 2)
            feed2 += ((i & 2u) == 0 ? 1.0f : -1.0f) * y[i];
        feed2 *= 0.18f * feed;
        auto& lines = net2[side];
        std::array<float, stage2Lines> z {}, g {};
        float s2 = 0.0f;
        for (std::size_t i = 0; i < lines.size(); ++i)
        {
            z[i] = lines[i].delay.tap (static_cast<int> (lines[i].length));
            g[i] = decay (lines[i], z[i]);
            s2 += g[i];
        }
        const float h2 = 0.5f * s2;
        for (std::size_t i = 0; i < lines.size(); ++i)
            lines[i].delay.push (ceiling ((i % 2 == 0 ? feed2 : -feed2) + g[i] - h2));
        out2[side] = 0.5f * (z[0] - z[1] + z[2] - z[3]);
    }

    float l = 0.0f, r = 0.0f;
    for (std::size_t i = 0; i < net1.size(); i += 2)
    {
        const float sign = (i & 2u) == 0 ? 1.0f : -1.0f;
        l += sign * y[i];
        r += sign * y[i + 1];
    }
    l = 0.3f * l + out2[0];
    r = 0.3f * r + out2[1];

    if (! std::isfinite (l) || ! std::isfinite (r))
    {
        // Never expected; if it ever happens the space is cleared, not left screaming.
        ++guards;
        clearNetwork();
        l = r = 0.0f;
    }

    width += (widthTarget - width) * 0.001f;
    const float mid = 0.5f * (l + r), side = 0.5f * (l - r) * width;
    l = mid + side;
    r = mid - side;
    dcL += (l - dcL) * dcCoef;
    dcR += (r - dcR) * dcCoef;
    outL = (l - dcL) * outputGain;
    outR = (r - dcR) * outputGain;
}

} // namespace osp
