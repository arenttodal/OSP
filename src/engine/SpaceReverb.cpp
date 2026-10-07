#include "engine/SpaceReverb.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace osp
{

namespace
{
    constexpr double twoPi = 2.0 * std::numbers::pi;

    struct TypeDesign
    {
        double lineMs[8];        // the late network's delays
        double allpassMs[8];     // the allpass inside each line
        float allpassGain;
        double diffMs[4];        // input diffusion (left; the right side is 9 % longer)
        float diffGain;
        double bassRatio;        // reverberation time at the bottom / at the mids
        double crossLowHz, crossHighHz;
        double trebleBright, trebleDark;   // high / mid reverberation time at DAMP 0 and 100 %
        double modSamples48k;    // modulation depth at MOD 100 %
        float width;
        float erLevel, erFeed;   // early reflections heard / fed into the tail
        double erToneHz;
        float outputGain;
        float directDiffused;    // the diffused input heard at once (a plate answers immediately)
    };

    // ROOM: a small, furnished room; most of its identity is in the early reflections.
    constexpr TypeDesign room { { 9.7, 11.3, 12.9, 14.9, 16.7, 18.7, 20.9, 23.3 }, { 2.3, 2.9, 3.1, 3.7, 4.1, 4.3, 4.7, 5.3 }, 0.55f,
                                { 0.7, 1.1, 1.6, 2.3 }, 0.62f, 1.1, 220.0, 4500.0, 0.75, 0.25, 6.0, 0.8f, 0.85f, 0.35f, 7000.0, 0.79f, 0.0f };
    // HALL: a large hall for orchestra (21 000 m3 class): long lines, slow, soft build.
    constexpr TypeDesign hall { { 27.1, 31.7, 36.3, 41.9, 47.3, 53.9, 59.3, 66.7 }, { 3.7, 4.9, 6.1, 7.3, 8.3, 9.7, 10.9, 12.1 }, 0.62f,
                                { 2.9, 4.3, 6.1, 8.7 }, 0.72f, 1.22, 300.0, 3500.0, 0.66, 0.2, 14.0, 1.0f, 0.55f, 0.45f, 6000.0, 0.86f, 0.0f };
    // PLATE: dense from the first moment, bright, lean, the most motion.
    constexpr TypeDesign plate { { 11.9, 14.7, 17.3, 20.9, 24.1, 27.7, 31.3, 35.9 }, { 1.9, 2.7, 3.5, 4.3, 5.1, 5.9, 6.7, 7.7 }, 0.68f,
                                 { 1.3, 2.1, 3.4, 5.3 }, 0.76f, 0.8, 400.0, 7000.0, 0.92, 0.32, 22.0, 1.0f, 0.0f, 0.0f, 9000.0, 1.06f, 0.3f };

    const TypeDesign& designFor (SpaceType t) noexcept
    {
        return t == SpaceType::room ? room : (t == SpaceType::hall ? hall : plate);
    }

    // HALL's early reflections (ms after the pre-delay, left, right): the initial gap of a
    // large hall (~19 ms), then the strong lateral reflections that make the Berlin halls
    // envelop (terraces and side walls), alternating sides, thinning out towards 115 ms.
    struct Reflection
    {
        double ms;
        float left, right;
    };
    constexpr std::array<Reflection, 14> hallReflections { { { 18.7, 0.70f, 0.30f }, { 21.3, 0.28f, 0.66f }, { 26.9, 0.55f, 0.38f },
                                                             { 31.1, 0.36f, 0.58f }, { 37.7, 0.50f, 0.30f }, { 42.3, 0.27f, 0.48f },
                                                             { 49.9, 0.40f, 0.31f }, { 55.1, 0.30f, 0.41f }, { 63.7, 0.33f, 0.24f },
                                                             { 71.3, 0.22f, 0.32f }, { 82.9, 0.25f, 0.19f }, { 91.7, 0.17f, 0.24f },
                                                             { 103.1, 0.16f, 0.13f }, { 113.9, 0.11f, 0.14f } } };

    /** ROOM's early reflections from the image sources of a shoebox (5.2 x 4.1 x 2.9 m at
        SIZE 50 %): first and second order, each bounce keeping 78 % of the pressure,
        spreading as 1/r; times after the direct sound. Sorted, the first `maxCount`. */
    int roomReflections (double sizeFactor, double* ms, float* left, float* right, int maxCount) noexcept
    {
        const double L[3] = { 5.2 * sizeFactor, 4.1 * sizeFactor, 2.9 * std::sqrt (sizeFactor) };
        const double src[3] = { 1.7 * sizeFactor, 2.6 * sizeFactor, 1.4 * std::sqrt (sizeFactor) };
        const double lis[3] = { 3.6 * sizeFactor, 1.6 * sizeFactor, 1.3 * std::sqrt (sizeFactor) };
        auto distance = [&] (const double* p) {
            const double dx = p[0] - lis[0], dy = p[1] - lis[1], dz = p[2] - lis[2];
            return std::sqrt (dx * dx + dy * dy + dz * dz);
        };
        const double direct = distance (src);
        struct Image
        {
            double ms;
            float left, right;
        };
        std::array<Image, 48> images {};
        int count = 0;
        // Images: for each axis, mirror n times (-2..2); order = sum of |n| (1 or 2).
        for (int nx = -2; nx <= 2; ++nx)
            for (int ny = -2; ny <= 2; ++ny)
                for (int nz = -2; nz <= 2; ++nz)
                {
                    const int order = std::abs (nx) + std::abs (ny) + std::abs (nz);
                    if (order < 1 || order > 2 || count >= static_cast<int> (images.size()))
                        continue;
                    const int n[3] = { nx, ny, nz };
                    double p[3];
                    for (int a = 0; a < 3; ++a)
                    {
                        // Mirror image positions: even n shift by whole room lengths, odd ones mirror.
                        const int k = n[a];
                        p[a] = (k % 2 == 0) ? src[a] + k * L[a] : (k + 1) * L[a] - src[a];
                    }
                    const double r = distance (p);
                    const double gain = std::pow (0.78, order) * direct / std::max (r, 0.1);
                    // Lateral position (y) decides the side the reflection arrives from.
                    const double pan = std::clamp ((p[1] - lis[1]) / std::max (r, 0.1), -1.0, 1.0);
                    images[static_cast<std::size_t> (count++)] = { 1000.0 * (r - direct) / 343.0,
                                                                   static_cast<float> (gain * std::sqrt (0.5 * (1.0 + pan))),
                                                                   static_cast<float> (gain * std::sqrt (0.5 * (1.0 - pan))) };
                }
        std::sort (images.begin(), images.begin() + count, [] (const Image& a, const Image& b) { return a.ms < b.ms; });
        const int used = std::min (count, maxCount);
        for (int i = 0; i < used; ++i)
        {
            ms[i] = std::max (0.5, images[static_cast<std::size_t> (i)].ms);
            left[i] = images[static_cast<std::size_t> (i)].left;
            right[i] = images[static_cast<std::size_t> (i)].right;
        }
        return used;
    }

    int powerOfTwoAtLeast (int n)
    {
        int s = 1;
        while (s < n)
            s <<= 1;
        return s;
    }

    constexpr double lineRatesHz[8] = { 0.31, 0.43, 0.57, 0.67, 0.79, 0.89, 1.03, 1.17 };
    constexpr double maxSizeFactor = 1.66;
}

//==============================================================================
SpaceReverb::Settings SpaceReverb::Settings::from (const Shaping& s) noexcept
{
    Settings r;
    r.type = s.spaceType;
    r.decaySeconds = s.spaceDecaySeconds;
    r.preDelayMs = s.spacePreDelayMs;
    r.size = s.spaceSize;
    r.damping = s.spaceDamping;
    r.modulation = s.spaceModulation;
    r.width = s.spaceWidth;
    r.lowCutHz = s.spaceLowCutHz;
    r.highCutHz = s.spaceHighCutHz;
    return r;
}

bool SpaceReverb::Settings::structurallyDifferent (const Settings& other) const noexcept
{
    return type != other.type || std::abs (size - other.size) > 1.0e-4;
}

void SpaceReverb::Delay::allocate (int samples)
{
    const int size = powerOfTwoAtLeast (std::max (8, samples));
    buffer.assign (static_cast<std::size_t> (size), 0.0f);
    mask = size - 1;
    write = 0;
}

void SpaceReverb::Delay::clear() noexcept
{
    std::fill (buffer.begin(), buffer.end(), 0.0f);
    write = 0;
}

float SpaceReverb::Delay::tapLinear (double delay) const noexcept
{
    const auto i = static_cast<int> (delay);
    const auto t = static_cast<float> (delay - i);
    const float a = tap (i), b = tap (i + 1);
    return a + t * (b - a);
}

float SpaceReverb::Delay::tapCubic (double delay) const noexcept
{
    // Hermite through four neighbours: a moving read stays clear (linear would dull the tail).
    const auto i = static_cast<int> (delay);
    const auto f = static_cast<float> (delay - i);
    const float y0 = tap (i - 1), y1 = tap (i), y2 = tap (i + 1), y3 = tap (i + 2);
    const float c1 = 0.5f * (y2 - y0);
    const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
    const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
    return ((c3 * f + c2) * f + c1) * f + y1;
}

float SpaceReverb::Svf::lowPass (float x, float g, float k) noexcept
{
    const float a1 = 1.0f / (1.0f + g * (g + k)), a2 = g * a1, a3 = g * a2;
    const float v3 = x - ic2;
    const float v1 = a1 * ic1 + a2 * v3;
    const float v2 = ic2 + a2 * ic1 + a3 * v3;
    ic1 = 2.0f * v1 - ic1;
    ic2 = 2.0f * v2 - ic2;
    return v2;
}

float SpaceReverb::Svf::highPass (float x, float g, float k) noexcept
{
    const float a1 = 1.0f / (1.0f + g * (g + k)), a2 = g * a1, a3 = g * a2;
    const float v3 = x - ic2;
    const float v1 = a1 * ic1 + a2 * v3;
    const float v2 = ic2 + a2 * ic1 + a3 * v3;
    ic1 = 2.0f * v1 - ic1;
    ic2 = 2.0f * v2 - ic2;
    return x - k * v1 - v2;
}

//==============================================================================
void SpaceReverb::prepare (double rate)
{
    sampleRate = rate;
    const auto ms = [rate] (double m) { return static_cast<int> (m * 0.001 * rate) + 8; };
    preL.allocate (ms (260.0));
    preR.allocate (ms (260.0));
    er.allocate (ms (120.0 * maxSizeFactor + 4.0));
    for (auto& side : diff)
        for (auto& d : side)
            d.allocate (ms (8.7 * 1.09 * maxSizeFactor + 1.0));
    const int maxModulation = static_cast<int> (2.0 * 22.0 * rate / 48000.0) + 8;
    for (auto& l : net)
    {
        l.delay.allocate (ms (66.7 * maxSizeFactor) + maxModulation);
        l.allpass.allocate (ms (12.1 * maxSizeFactor));
    }
    springA.allocate (ms (60.0 * std::sqrt (maxSizeFactor)));
    springB.allocate (ms (60.0 * std::sqrt (maxSizeFactor)));
    dcCoef = static_cast<float> (1.0 - std::exp (-twoPi * 18.0 / rate));
    configure (current);
    reset();
}

void SpaceReverb::reset() noexcept
{
    for (auto* d : { &preL, &preR, &er, &springA, &springB })
        d->clear();
    for (auto& side : diff)
        for (auto& d : side)
            d.clear();
    for (std::size_t i = 0; i < net.size(); ++i)
    {
        auto& l = net[i];
        l.delay.clear();
        l.allpass.clear();
        l.lowState = l.highState = 0.0f;
        l.phase = 0.785 * static_cast<double> (i);
        l.sinValue = std::sin (l.phase);
        l.cosValue = std::cos (l.phase);
        l.wander = l.wanderFrom = l.wanderTo = 0.0f;
    }
    for (auto& f : lowCut)
        f = {};
    for (auto& f : highCut)
        f = {};
    springStateA.fill (0.0f);
    springStateB.fill (0.0f);
    springLow = springHigh = 0.0f;
    erToneL = erToneR = dcL = dcR = 0.0f;
    wanderRng.reseed (0x5350414345ull);
    modCountdown = resyncCountdown = 0;
    // A fresh instance starts at its settings (no glide from the last ones).
    preSamples = preTarget;
    lowCutOct = lowCutTarget;
    highCutOct = highCutTarget;
    modDepth = modDepthTarget;
    width = widthTarget;
    filterCountdown = 0;
    updateFilters();
}

void SpaceReverb::buildReflections() noexcept
{
    const double rate = sampleRate;
    std::array<double, maxTaps> ms {};
    erCount = 0;
    if (current.type == SpaceType::room)
    {
        erCount = roomReflections (sizeFactor, ms.data(), erGainL.data(), erGainR.data(), maxTaps);
    }
    else if (current.type == SpaceType::hall)
    {
        erCount = static_cast<int> (hallReflections.size());
        for (int i = 0; i < erCount; ++i)
        {
            const auto& r = hallReflections[static_cast<std::size_t> (i)];
            ms[static_cast<std::size_t> (i)] = r.ms * sizeFactor;
            erGainL[static_cast<std::size_t> (i)] = r.left;
            erGainR[static_cast<std::size_t> (i)] = r.right;
        }
    }
    for (int i = 0; i < erCount; ++i)
        erDelay[static_cast<std::size_t> (i)] = std::max (1, static_cast<int> (ms[static_cast<std::size_t> (i)] * 0.001 * rate));
}

void SpaceReverb::configure (const Settings& s) noexcept
{
    current = s;
    double lo, hi;
    shaping::decayRange (current.type, lo, hi);
    current.decaySeconds = std::clamp (current.decaySeconds, lo, hi);
    sizeFactor = shaping::spaceSizeFactor (current.size);
    const double rate = sampleRate;

    if (current.type == SpaceType::spring)
    {
        const double k = std::sqrt (sizeFactor);
        springLengthA = std::max (1, static_cast<int> (0.033 * k * rate));
        springLengthB = std::max (1, static_cast<int> (0.041 * k * rate));
        springCoef = 0.68f;
        springToneLow = static_cast<float> (1.0 - std::exp (-twoPi * 4200.0 / rate));
        springToneHigh = static_cast<float> (1.0 - std::exp (-twoPi * 160.0 / rate));
        typeWidth = 0.3f;
        outputGain = 0.46f;
        erCount = 0;
        erLevel = erFeed = direct = 0.0f;
    }
    else
    {
        const TypeDesign& d = designFor (current.type);
        for (std::size_t i = 0; i < net.size(); ++i)
        {
            net[i].length = std::max (4.0, d.lineMs[i] * sizeFactor * 0.001 * rate);
            net[i].allpassLength = std::max (1, static_cast<int> (d.allpassMs[i] * sizeFactor * 0.001 * rate));
            net[i].phaseStep = twoPi * lineRatesHz[i] * (current.type == SpaceType::plate ? 1.25 : 1.0) / rate;
            net[i].stepSin = std::sin (net[i].phaseStep);
            net[i].stepCos = std::cos (net[i].phaseStep);
        }
        for (std::size_t i = 0; i < diffLength.size(); ++i)
        {
            diffLength[i] = std::max (1, static_cast<int> (d.diffMs[i] * sizeFactor * 0.001 * rate));
            diffLengthR[i] = std::max (1, static_cast<int> (d.diffMs[i] * 1.09 * sizeFactor * 0.001 * rate));
        }
        diffGain = d.diffGain;
        loopAllpassGain = d.allpassGain;
        lowCoef = static_cast<float> (1.0 - std::exp (-twoPi * d.crossLowHz / rate));
        highCoef = static_cast<float> (1.0 - std::exp (-twoPi * d.crossHighHz / rate));
        erLevel = d.erLevel;
        erFeed = d.erFeed;
        erToneCoef = static_cast<float> (1.0 - std::exp (-twoPi * d.erToneHz / rate));
        typeWidth = d.width;
        outputGain = d.outputGain;
        direct = d.directDiffused;
        buildReflections();
    }
    tune (current);
}

void SpaceReverb::computeDecay() noexcept
{
    const double rate = sampleRate;
    const double rt = current.decaySeconds;
    if (current.type == SpaceType::spring)
    {
        // Dispersion makes the loop longer than its delay; the gain follows the delay only.
        springGain = static_cast<float> (std::pow (10.0, -3.0 * (springLengthA / rate) / rt));
        return;
    }
    const TypeDesign& d = designFor (current.type);
    const double treble = d.trebleBright + (d.trebleDark - d.trebleBright) * std::clamp (current.damping, 0.0, 1.0);
    const double rtLow = rt * d.bassRatio, rtHigh = rt * treble;
    for (auto& l : net)
    {
        // The allpass inside the loop adds its length on average (its phase turns 2 pi M).
        const double seconds = (l.length + l.allpassLength) / rate;
        auto gain = [seconds] (double t) { return static_cast<float> (std::min (0.9995, std::pow (10.0, -3.0 * seconds / t))); };
        l.gainLow = gain (rtLow);
        l.gainMid = gain (rt);
        l.gainHigh = gain (rtHigh);
    }
}

void SpaceReverb::tune (const Settings& s) noexcept
{
    double lo, hi;
    shaping::decayRange (current.type, lo, hi);
    current.decaySeconds = std::clamp (s.decaySeconds, lo, hi);
    current.preDelayMs = std::clamp (s.preDelayMs, 0.0, 250.0);
    current.damping = s.damping;
    current.modulation = s.modulation;
    current.width = s.width;
    current.lowCutHz = s.lowCutHz;
    current.highCutHz = s.highCutHz;
    computeDecay();
    preTarget = current.preDelayMs * 0.001 * sampleRate;
    lowCutTarget = std::log2 (std::clamp (current.lowCutHz, 20.0, 2000.0));
    highCutTarget = std::log2 (std::clamp (current.highCutHz, 1000.0, std::min (20000.0, 0.45 * sampleRate)));
    const double depth48 = current.type == SpaceType::spring ? 0.0 : designFor (current.type).modSamples48k;
    modDepthTarget = depth48 * sampleRate / 48000.0 * std::pow (std::clamp (current.modulation, 0.0, 1.0), 1.3);
    widthTarget = static_cast<float> (std::clamp (current.width, 0.0, 1.0)) * typeWidth;
}

void SpaceReverb::updateFilters() noexcept
{
    lowCutOct += (lowCutTarget - lowCutOct) * 0.12;
    highCutOct += (highCutTarget - highCutOct) * 0.12;
    const double nyquistGuard = 0.45 * sampleRate;
    gLow = static_cast<float> (std::tan (std::numbers::pi * std::min (std::exp2 (lowCutOct), nyquistGuard) / sampleRate));
    gHigh = static_cast<float> (std::tan (std::numbers::pi * std::min (std::exp2 (highCutOct), nyquistGuard) / sampleRate));
}

void SpaceReverb::advanceModulation() noexcept
{
    if (--modCountdown <= 0)
    {
        // A new point of each line's random walk every 64 samples (bounded, seeded).
        modCountdown = modulationBlock;
        for (auto& l : net)
        {
            l.wanderFrom = l.wanderTo;
            l.wanderTo = std::clamp (0.8f * l.wanderTo + 0.45f * static_cast<float> (wanderRng.bipolar()), -1.0f, 1.0f);
        }
    }
    const float t = 1.0f - static_cast<float> (modCountdown) / static_cast<float> (modulationBlock);
    const bool resync = --resyncCountdown <= 0;
    if (resync)
        resyncCountdown = 2048;
    for (auto& l : net)
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

void SpaceReverb::skip() noexcept
{
    advanceModulation();
}

float SpaceReverb::processSpring (float x) noexcept
{
    // Band-limit the drive into the tank (springs do not pass deep bass or air).
    springHigh += (x - springHigh) * springToneHigh;
    x -= springHigh;
    springLow += (x - springLow) * springToneLow;
    return springLow;
}

void SpaceReverb::process (float inL, float inR, float& outL, float& outR) noexcept
{
    // Input EQ (LOW CUT / HIGH CUT, 12 dB/oct each, Butterworth), coefficients every 16 samples.
    if (--filterCountdown <= 0)
    {
        filterCountdown = 16;
        updateFilters();
    }
    constexpr float k = 1.41421356f;
    float xL = highCut[0].lowPass (lowCut[0].highPass (inL, gLow, k), gHigh, k);
    float xR = highCut[1].lowPass (lowCut[1].highPass (inR, gLow, k), gHigh, k);

    // PRE-DELAY: glides when moved (at most a quarter sample per sample: no zipper, no jump).
    preL.push (xL);
    preR.push (xR);
    preSamples += std::clamp ((preTarget - preSamples) * 0.002, -0.25, 0.25);
    // The newest sample sits one behind the write position: PRE-DELAY 0 is that sample.
    const double pre = std::max (0.0, preSamples) + 1.0;
    xL = preL.tapLinear (pre);
    xR = preR.tapLinear (pre);

    float l = 0.0f, r = 0.0f;
    if (current.type == SpaceType::spring)
    {
        const float x = processSpring (0.5f * (xL + xR));
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
        advanceModulation();

        // Early reflections (mono source, the room's own left and right), slightly dulled.
        float eL = 0.0f, eR = 0.0f;
        if (erCount > 0)
        {
            er.push (0.5f * (xL + xR));
            for (int i = 0; i < erCount; ++i)
            {
                const auto is = static_cast<std::size_t> (i);
                const float t = er.tap (erDelay[is]);
                eL += erGainL[is] * t;
                eR += erGainR[is] * t;
            }
            erToneL += (eL - erToneL) * erToneCoef;
            erToneR += (eR - erToneR) * erToneCoef;
            eL = erToneL;
            eR = erToneR;
        }

        // Input diffusion: four Schroeder allpasses per side.
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
        const float dL = diffuse (diff[0], xL, diffLength) + erFeed * eL;
        const float dR = diffuse (diff[1], xR, diffLengthR) + erFeed * eR;

        // The late network: read (moving, cubic), the line's allpass, its two-band decay.
        std::array<float, lines> y {}, f {};
        for (std::size_t i = 0; i < net.size(); ++i)
        {
            auto& line = net[i];
            const double offset = modDepth * (1.0 + 0.6 * line.sinValue + 0.4 * static_cast<double> (line.wander));
            const float o = line.delay.tapCubic (line.length + offset);
            const float delayed = line.allpass.tap (line.allpassLength);
            const float u = o + loopAllpassGain * delayed;
            line.allpass.push (u);
            const float a = delayed - loopAllpassGain * u;
            y[i] = a;
            line.lowState += (a - line.lowState) * lowCoef;
            line.highState += (a - line.highState) * highCoef;
            f[i] = line.gainHigh * a + (line.gainMid - line.gainHigh) * line.highState + (line.gainLow - line.gainMid) * line.lowState;
        }
        // Fast Walsh-Hadamard transform: lossless 8x8 mixing.
        for (int len = 1; len < lines; len <<= 1)
            for (int i = 0; i < lines; i += 2 * len)
                for (int j = i; j < i + len; ++j)
                {
                    const auto a = static_cast<std::size_t> (j), b = static_cast<std::size_t> (j + len);
                    const float u = f[a], v = f[b];
                    f[a] = u + v;
                    f[b] = u - v;
                }
        constexpr float norm = 0.35355339f; // 1/sqrt(8)
        for (std::size_t i = 0; i < net.size(); ++i)
        {
            const float in = (i % 2 == 0 ? dL : dR) * ((i & 2u) == 0 ? 0.35f : -0.35f);
            net[i].delay.push (in + norm * f[i]);
        }
        l = 0.5f * (y[0] - y[2] + y[4] - y[6]) + erLevel * eL + direct * dL;
        r = 0.5f * (y[1] - y[3] + y[5] - y[7]) + erLevel * eR + direct * dR;
    }

    // WIDTH (mid / side), a DC guard, the type's level.
    width += (widthTarget - width) * 0.001f;
    const float mid = 0.5f * (l + r), side = 0.5f * (l - r) * width;
    l = mid + side;
    r = mid - side;
    dcL += (l - dcL) * dcCoef;
    dcR += (r - dcR) * dcCoef;
    outL = (l - dcL) * outputGain;
    outR = (r - dcR) * outputGain;
}

//==============================================================================
SpaceReverb::Portrait SpaceReverb::portrait (SpaceType t) noexcept
{
    Settings s;
    s.type = t;
    return portrait (s);
}

SpaceReverb::Portrait SpaceReverb::portrait (const Settings& s) noexcept
{
    Portrait p;
    p.preMs = std::clamp (s.preDelayMs, 0.0, 250.0);
    const double sf = shaping::spaceSizeFactor (s.size);
    if (s.type == SpaceType::spring)
    {
        p.spring = true;
        const double k = std::sqrt (sf);
        const double a = 33.0 * k, b = 41.0 * k;
        p.erMs = { a, b, 2 * a, 2 * b, 3 * a, 3 * b, 4 * a, 4 * b };
        p.erCount = 8;
        for (int i = 0; i < 8; ++i)
            p.erGain[static_cast<std::size_t> (i)] = 1.0f / (1.0f + 0.3f * static_cast<float> (i));
        p.erLevel = 0.7f;
        p.damping = 0.3f;
        p.width = 0.3f * static_cast<float> (std::clamp (s.width, 0.0, 1.0));
        p.diffusionMs = 6.0;
        return p;
    }
    const TypeDesign& d = designFor (s.type);
    if (s.type == SpaceType::room)
    {
        std::array<float, maxTaps> l {}, r {};
        p.erCount = roomReflections (sf, p.erMs.data(), l.data(), r.data(), maxTaps);
        for (int i = 0; i < p.erCount; ++i)
            p.erGain[static_cast<std::size_t> (i)] = std::min (1.0f, 0.5f * (l[static_cast<std::size_t> (i)] + r[static_cast<std::size_t> (i)]));
    }
    else if (s.type == SpaceType::hall)
    {
        p.erCount = static_cast<int> (hallReflections.size());
        for (int i = 0; i < p.erCount; ++i)
        {
            const auto& h = hallReflections[static_cast<std::size_t> (i)];
            p.erMs[static_cast<std::size_t> (i)] = h.ms * sf;
            p.erGain[static_cast<std::size_t> (i)] = std::max (h.left, h.right);
        }
    }
    p.erLevel = d.erLevel;
    const double treble = d.trebleBright + (d.trebleDark - d.trebleBright) * std::clamp (s.damping, 0.0, 1.0);
    p.damping = static_cast<float> (std::clamp (1.0 - treble, 0.0, 1.0));
    p.width = d.width * static_cast<float> (std::clamp (s.width, 0.0, 1.0));
    p.diffusionMs = d.diffMs[3] * sf;
    p.bassRatio = d.bassRatio;
    return p;
}

} // namespace osp
