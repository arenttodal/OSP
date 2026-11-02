#include "engine/BloomReverb.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace osp
{

namespace
{
    constexpr double twoPi = 2.0 * std::numbers::pi;
    // Our own diffuser lengths (ms at SIZE 50 %): spread, mutually prime-ish.
    constexpr double diffMs[BloomReverb::diffusers] = { 3.1, 4.7, 7.3, 11.1, 16.3, 23.9 };
    constexpr double postMs[BloomReverb::postDiffusers] = { 5.3, 8.9, 13.7, 21.1 };

    int powerOfTwoAtLeast (int n)
    {
        int s = 1;
        while (s < n)
            s <<= 1;
        return s;
    }
}

BloomReverb::Settings BloomReverb::Settings::from (const Shaping& s) noexcept
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
    r.variant = s.bloomVariant;
    r.sync = s.bloomSync;
    r.division = s.bloomDivision;
    r.timeMs = snap (s.bloomTimeMs);   // (0.01 ms: a parameter round trip's rounding never heard)
    r.diffusion = s.bloomDiffusion;
    return r;
}

double BloomReverb::timeSeconds (const Settings& s, double bpm) noexcept
{
    const double t = s.sync ? 60.0 / std::max (20.0, bpm) * shaping::echoDivisionQuarters (s.division) : 0.001 * s.timeMs;
    return std::clamp (t, 0.02, maxSeconds);
}

//==============================================================================
void BloomReverb::Delay::allocate (int samples)
{
    const int size = powerOfTwoAtLeast (std::max (8, samples));
    buffer.assign (static_cast<std::size_t> (size), 0.0f);
    mask = size - 1;
    write = 0;
}

void BloomReverb::Delay::clear() noexcept
{
    std::fill (buffer.begin(), buffer.end(), 0.0f);
    write = 0;
}

float BloomReverb::Delay::tapLinear (double delay) const noexcept
{
    const auto i = static_cast<int> (delay);
    const auto t = static_cast<float> (delay - i);
    const float a = tap (i), b = tap (i + 1);
    return a + t * (b - a);
}

float BloomReverb::Delay::tapCubic (double delay) const noexcept
{
    const auto i = static_cast<int> (delay);
    const auto f = static_cast<float> (delay - i);
    const float y0 = tap (i - 1), y1 = tap (i), y2 = tap (i + 1), y3 = tap (i + 2);
    const float c1 = 0.5f * (y2 - y0);
    const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
    const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
    return ((c3 * f + c2) * f + c1) * f + y1;
}

float BloomReverb::Svf::lowPass (float x, float g, float k) noexcept
{
    const float a1 = 1.0f / (1.0f + g * (g + k)), a2 = g * a1, a3 = g * a2;
    const float v3 = x - ic2;
    const float v1 = a1 * ic1 + a2 * v3;
    const float v2 = ic2 + a2 * ic1 + a3 * v3;
    ic1 = 2.0f * v1 - ic1;
    ic2 = 2.0f * v2 - ic2;
    return v2;
}

float BloomReverb::Svf::highPass (float x, float g, float k) noexcept
{
    const float a1 = 1.0f / (1.0f + g * (g + k)), a2 = g * a1, a3 = g * a2;
    const float v3 = x - ic2;
    const float v1 = a1 * ic1 + a2 * v3;
    const float v2 = ic2 + a2 * ic1 + a3 * v3;
    ic1 = 2.0f * v1 - ic1;
    ic2 = 2.0f * v2 - ic2;
    return x - k * v1 - v2;
}

float BloomReverb::limit (float x) noexcept
{
    // Transparent below full scale, a soft knee towards 2 above (the loop can never run away).
    const float a = std::abs (x);
    if (a <= 1.0f)
        return x;
    const float t = a - 1.0f;
    return std::copysign (1.0f + t / (1.0f + t), x);
}

//==============================================================================
void BloomReverb::prepare (double rate)
{
    sampleRate = rate;
    const double sf = shaping::spaceSizeFactor (1.0);
    auto samples = [rate] (double seconds) { return static_cast<int> (seconds * rate) + 8; };
    pre.allocate (samples (0.26));
    for (auto& s : sides)
    {
        s.line.allocate (samples (maxSeconds + 0.02));
        for (std::size_t i = 0; i < s.diff.size(); ++i)
            s.diff[i].allocate (samples (diffMs[i] * sf * 0.001 * 1.07));
        for (std::size_t i = 0; i < s.post.size(); ++i)
            s.post[i].allocate (samples (postMs[i] * sf * 0.001 * 1.07));
    }
    dcCoef = static_cast<float> (1.0 - std::exp (-twoPi * 18.0 / rate));
    bbdCoef = static_cast<float> (1.0 - std::exp (-twoPi * 3200.0 / rate));
    headCoef = static_cast<float> (1.0 - std::exp (-twoPi * 6500.0 / rate));
    tune (current);
    reset();
}

void BloomReverb::reset() noexcept
{
    pre.clear();
    for (auto& s : sides)
    {
        s.line.clear();
        for (auto& d : s.diff)
            d.clear();
        for (auto& d : s.post)
            d.clear();
        s.tone = s.bbd1 = s.bbd2 = s.head = s.out = 0.0f;
    }
    for (auto& f : lowCut)
        f = {};
    for (auto& f : highCut)
        f = {};
    rng.reseed (0x424c4f4dull);
    wowPhase = flutterPhase = wanderPhase = 0.0;
    delaySamples = delayTarget;
    preSamples = preTarget;
    lowCutOct = lowCutTarget;
    highCutOct = highCutTarget;
    width = widthTarget;
    dcL = dcR = 0.0f;
    filterCountdown = 0;
    updateFilters();
}

void BloomReverb::setTempo (double newBpm) noexcept
{
    if (std::abs (newBpm - bpm) > 1.0e-6 && newBpm > 0.0)
    {
        bpm = newBpm;
        computeLoop();
    }
}

void BloomReverb::tune (const Settings& s) noexcept
{
    current = s;
    current.decaySeconds = std::clamp (s.decaySeconds, 0.2, 8.0);
    current.preDelayMs = std::clamp (s.preDelayMs, 0.0, 250.0);
    current.diffusion = std::clamp (s.diffusion, 0.0, 1.0);
    const double sf = shaping::spaceSizeFactor (current.size);
    for (std::size_t i = 0; i < diffLength.size(); ++i)
        diffLength[i] = std::max (1, static_cast<int> (diffMs[i] * sf * 0.001 * sampleRate));
    for (std::size_t i = 0; i < postLength.size(); ++i)
        postLength[i] = std::max (1, static_cast<int> (postMs[i] * sf * 0.001 * sampleRate));
    computeLoop();
    preTarget = current.preDelayMs * 0.001 * sampleRate;
    lowCutTarget = std::log2 (std::clamp (current.lowCutHz, 20.0, 2000.0));
    highCutTarget = std::log2 (std::clamp (current.highCutHz, 1000.0, std::min (20000.0, 0.45 * sampleRate)));
    widthTarget = static_cast<float> (std::clamp (current.width, 0.0, 1.0));
}

void BloomReverb::computeLoop() noexcept
{
    // The diffusers delay the loop too (their lengths, at any gain): the line is read that
    // much earlier, so a repeat lands on its beat (down to the diffusers' own length).
    const double t = timeSeconds (current, bpm);
    double diffusersSamples = 0.0;
    for (const int d : diffLength)
        diffusersSamples += d;
    delayTarget = std::max (2.0, t * sampleRate - diffusersSamples);
    const bool wash = current.variant == BloomVariant::wash;
    feedback = static_cast<float> (std::min (0.97, std::pow (10.0, -3.0 * t / current.decaySeconds)));
    diffGain = static_cast<float> (wash ? 0.75 : 0.75 * current.diffusion);
    postGain = static_cast<float> (wash ? 0.35 + 0.35 * current.diffusion : 0.0);
    const double toneHz = 12000.0 * std::pow (1500.0 / 12000.0, std::clamp (current.damping, 0.0, 1.0));
    toneCoef = static_cast<float> (1.0 - std::exp (-twoPi * std::min (toneHz, 0.45 * sampleRate) / sampleRate));
    modDepth = 0.002 * sampleRate * std::pow (std::clamp (current.modulation, 0.0, 1.0), 1.3);
}

void BloomReverb::updateFilters() noexcept
{
    lowCutOct += (lowCutTarget - lowCutOct) * 0.12;
    highCutOct += (highCutTarget - highCutOct) * 0.12;
    const double guard = 0.45 * sampleRate;
    gLow = static_cast<float> (std::tan (std::numbers::pi * std::min (std::exp2 (lowCutOct), guard) / sampleRate));
    gHigh = static_cast<float> (std::tan (std::numbers::pi * std::min (std::exp2 (highCutOct), guard) / sampleRate));
}

void BloomReverb::process (float inL, float inR, float& outL, float& outR) noexcept
{
    if (--filterCountdown <= 0)
    {
        filterCountdown = 16;
        updateFilters();
    }
    constexpr float k = 1.41421356f;
    const float l = highCut[0].lowPass (lowCut[0].highPass (inL, gLow, k), gHigh, k);
    const float r = highCut[1].lowPass (lowCut[1].highPass (inR, gLow, k), gHigh, k);
    pre.push (0.5f * (l + r));
    preSamples += std::clamp ((preTarget - preSamples) * 0.002, -0.25, 0.25);
    const float x = pre.tapLinear (std::max (0.0, preSamples) + 1.0);

    // The loop's time glides (a change bends the repeats, never clicks); MOD's slow wander,
    // and TAPE's wow and flutter, move where it is read.
    delaySamples += (delayTarget - delaySamples) * 0.0005;
    const bool tape = current.variant == BloomVariant::tape, vintage = current.variant == BloomVariant::vintage;
    wanderPhase += 0.21 / sampleRate;
    wowPhase += 0.55 / sampleRate;
    flutterPhase += 6.3 / sampleRate;
    for (auto* p : { &wanderPhase, &wowPhase, &flutterPhase })
        *p -= std::floor (*p);
    double offset = modDepth * (0.5 + 0.5 * std::sin (twoPi * wanderPhase));
    if (tape)
        offset += 0.0006 * sampleRate * (1.0 + std::sin (twoPi * wowPhase)) + 0.00008 * sampleRate * (1.0 + std::sin (twoPi * flutterPhase));

    for (std::size_t i = 0; i < sides.size(); ++i)
    {
        auto& s = sides[i];
        // (The right side reads a little apart: the wash is wide.)
        float y = s.line.tapCubic (delaySamples + offset + (i == 1 ? 0.37 * modDepth : 0.0));
        for (std::size_t d = 0; d < s.diff.size(); ++d)
        {
            const int length = i == 1 ? diffLength[d] + static_cast<int> (d) + 1 : diffLength[d];
            const float delayed = s.diff[d].tap (length);
            const float v = y + diffGain * delayed;
            s.diff[d].push (v);
            y = delayed - diffGain * v;
        }
        s.tone += (y - s.tone) * toneCoef;
        y = s.tone;
        if (vintage)
        {
            // A bucket brigade: band-limited twice, its compander's hiss, a soft limit.
            s.bbd1 += (y - s.bbd1) * bbdCoef;
            s.bbd2 += (s.bbd1 - s.bbd2) * bbdCoef;
            y = s.bbd2 + 0.00012f * static_cast<float> (rng.bipolar());
            y = y / (1.0f + 0.25f * std::abs (y));
        }
        else if (tape)
        {
            // Tape: the head's loss and its saturation.
            s.head += (y - s.head) * headCoef;
            y = s.head;
            y = y / (1.0f + 0.35f * std::abs (y));
        }
        s.out = y;
    }
    // Ping-pong: the input into the left, each side's repeat into the other.
    sides[0].line.push (limit (x + feedback * sides[1].out));
    sides[1].line.push (limit (feedback * sides[0].out));

    float wl = sides[0].out, wr = sides[1].out;
    if (postGain > 0.0f)
    {
        // WASH: a second diffusion after the loop (hardly a discrete repeat left).
        auto diffuse = [this] (std::array<Delay, postDiffusers>& chain, float v, int extra) {
            for (std::size_t d = 0; d < chain.size(); ++d)
            {
                const float delayed = chain[d].tap (postLength[d] + extra * static_cast<int> (d + 1));
                const float u = v + postGain * delayed;
                chain[d].push (u);
                v = delayed - postGain * u;
            }
            return v;
        };
        wl = diffuse (sides[0].post, wl, 0);
        wr = diffuse (sides[1].post, wr, 1);
    }

    width += (widthTarget - width) * 0.001f;
    const float mid = 0.5f * (wl + wr), side = 0.5f * (wl - wr) * width;
    wl = mid + side;
    wr = mid - side;
    dcL += (wl - dcL) * dcCoef;
    dcR += (wr - dcR) * dcCoef;
    outL = (wl - dcL) * outputGain;
    outR = (wr - dcR) * outputGain;
}

} // namespace osp
