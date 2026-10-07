#include "engine/EchoDelay.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace osp
{

namespace
{
    constexpr double twoPi = 2.0 * std::numbers::pi;

    /** tanh's Pade approximant: soft, symmetric, exact enough for a saturating loop. */
    inline float softClip (float x) noexcept
    {
        x = std::clamp (x, -3.0f, 3.0f);
        const float x2 = x * x;
        return x * (27.0f + x2) / (27.0f + 9.0f * x2);
    }

    int powerOfTwoAtLeast (int n)
    {
        int s = 1;
        while (s < n)
            s <<= 1;
        return s;
    }

    constexpr float svfK = 1.41421356f;   // Butterworth
}

EchoDelay::Settings EchoDelay::Settings::from (const Shaping& s) noexcept
{
    Settings e;
    e.type = s.echoType;
    e.sync = s.echoSync;
    e.division = s.echoDivision;
    e.timeMs = s.echoTimeMs;
    e.feedback = s.echoFeedback;
    e.tone = s.echoTone;
    e.age = s.echoAge;
    e.stereo = s.echoStereo;
    return e;
}

void EchoDelay::Line::allocate (int samples)
{
    const int size = powerOfTwoAtLeast (std::max (16, samples));
    buffer.assign (static_cast<std::size_t> (size), 0.0f);
    mask = size - 1;
    write = 0;
}

void EchoDelay::Line::clear() noexcept
{
    std::fill (buffer.begin(), buffer.end(), 0.0f);
    write = 0;
}

float EchoDelay::Line::read (double delay) const noexcept
{
    // Hermite: the moving read (wow, a changing time) stays clean.
    const auto i = static_cast<int> (delay);
    const auto f = static_cast<float> (delay - i);
    auto at = [this] (int d) { return buffer[static_cast<std::size_t> ((write - d) & mask)]; };
    const float y0 = at (i - 1), y1 = at (i), y2 = at (i + 1), y3 = at (i + 2);
    const float c1 = 0.5f * (y2 - y0);
    const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
    const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
    return ((c3 * f + c2) * f + c1) * f + y1;
}

void EchoDelay::Svf::process (float x, float g, float k, float& low, float& high) noexcept
{
    const float a1 = 1.0f / (1.0f + g * (g + k)), a2 = g * a1, a3 = g * a2;
    const float v3 = x - ic2;
    const float v1 = a1 * ic1 + a2 * v3;
    const float v2 = ic2 + a2 * ic1 + a3 * v3;
    ic1 = 2.0f * v1 - ic1;
    ic2 = 2.0f * v2 - ic2;
    low = v2;
    high = x - k * v1 - v2;
}

void EchoDelay::prepare (double rate)
{
    sampleRate = rate;
    for (auto& c : channels)
        c.line.allocate (static_cast<int> ((maxSeconds * 1.08 + 0.05) * rate) + 64);
    glide = 1.0 / (0.12 * rate);
    bumpCoef = static_cast<float> (1.0 - std::exp (-twoPi * 110.0 / rate));
    setSettings (settings);
    reset();
}

void EchoDelay::reset() noexcept
{
    for (auto& c : channels)
    {
        c.line.clear();
        c.lowCut = c.highCut = c.highCut2 = c.antiAlias = c.antiAlias2 = {};
        c.bump = c.last = 0.0f;
    }
    rng.reseed (0x4543484full);
    wowPhase = flutterPhase = chorusPhase = 0.0;
    wander = wanderFrom = wanderTo = 0.0f;
    wanderCountdown = 0;
    delaySamples = targetSamples;
    countdown = 0;
    quietRun = 0;
    updateCoefficients();
}

double EchoDelay::timeSeconds (const Settings& s, double bpm) noexcept
{
    const double seconds = s.sync ? shaping::echoDivisionQuarters (s.division) * 60.0 / std::clamp (bpm, 20.0, 400.0) : 0.001 * s.timeMs;
    return std::clamp (seconds, 0.02, maxSeconds);
}

void EchoDelay::setSettings (const Settings& s) noexcept
{
    settings = s;
    targetSamples = timeSeconds (settings, bpm) * sampleRate;
    feedback = std::clamp (settings.feedback, 0.0, 1.0);
}

void EchoDelay::setTempo (double newBpm) noexcept
{
    if (newBpm > 1.0 && std::abs (newBpm - bpm) > 1.0e-6)
    {
        bpm = newBpm;
        targetSamples = timeSeconds (settings, bpm) * sampleRate;
    }
}

void EchoDelay::updateCoefficients() noexcept
{
    const double rate = sampleRate;
    const double age = std::clamp (settings.age, 0.0, 1.0), tone = std::clamp (settings.tone, 0.0, 1.0);
    const double nyquistGuard = 0.45 * rate;
    auto g = [rate, nyquistGuard] (double hz) { return static_cast<float> (std::tan (std::numbers::pi * std::clamp (hz, 10.0, nyquistGuard) / rate)); };
    if (settings.type == EchoType::tape)
    {
        // Heads and tape: a low cut that rises with wear, a high cut that TONE opens.
        gLowCut = g (55.0 + 60.0 * age);
        gHighCut = g ((2200.0 + 9800.0 * tone * tone) * (1.0 - 0.35 * age));
        drive = static_cast<float> (1.0 + 1.6 * age + 1.4 * feedback * feedback);
    }
    else
    {
        // The clock follows the time: 8192 stages, bandwidth ~0.38 x the clock / 2.
        const double seconds = std::max (0.02, delaySamples / rate);
        const double clockHz = 8192.0 / (2.0 * seconds);
        gAlias = g (std::clamp (0.38 * clockHz, 900.0, 15000.0) * (0.7 + 0.55 * tone) * (1.0 - 0.25 * age));
        gHighCut = g (2500.0 + 9000.0 * tone);
        gLowCut = g (90.0);
        drive = static_cast<float> (1.4 + 1.2 * age + 1.0 * feedback * feedback);
    }
    sleepAfter = static_cast<int> (delaySamples * 1.1 + 0.25 * rate);
}

float EchoDelay::shape (Channel& c, float x) noexcept
{
    float low = 0.0f, high = 0.0f;
    if (settings.type == EchoType::tape)
    {
        // Record head: soft saturation with a little bias (even harmonics, tape's warmth).
        constexpr float bias = 0.06f;
        x = (softClip (x * drive + bias) - softClip (bias)) / drive;
        // Playback: the head bump (a few dB around 110 Hz), the low cut, the high cut.
        c.bump += (x - c.bump) * bumpCoef;
        x += 0.4f * c.bump;
        c.lowCut.process (x, gLowCut, svfK, low, high);
        x = high;
        c.highCut.process (x, gHighCut, 1.25f, low, high);
        return low;
    }
    // BBD: anti-alias (4th order at the clock's bandwidth), soft clip, tone, low cut.
    c.antiAlias.process (x, gAlias, 0.765f, low, high);
    c.antiAlias2.process (low, gAlias, 1.848f, low, high);
    x = softClip (low * drive) / drive;
    c.highCut.process (x, gHighCut, svfK, low, high);
    x = low;
    c.lowCut.process (x, gLowCut, svfK, low, high);
    return high;
}

double EchoDelay::advanceModulation() noexcept
{
    // Wow (a seeded random walk around a slow sine) and flutter for TAPE, the slow chorus
    // for BBD. Also run while the owner skips a silent echo, so it wakes in phase.
    if (--wanderCountdown <= 0)
    {
        wanderCountdown = wanderBlock;
        wanderFrom = wanderTo;
        wanderTo = std::clamp (0.75f * wanderTo + 0.5f * static_cast<float> (rng.bipolar()), -1.0f, 1.0f);
    }
    wander = wanderFrom + (wanderTo - wanderFrom) * (1.0f - static_cast<float> (wanderCountdown) / wanderBlock);
    const double age = std::clamp (settings.age, 0.0, 1.0);
    if (settings.type == EchoType::tape)
    {
        wowPhase += twoPi * 0.8 / sampleRate;
        flutterPhase += twoPi * 7.1 / sampleRate;
        if (wowPhase > twoPi)
            wowPhase -= twoPi;
        if (flutterPhase > twoPi)
            flutterPhase -= twoPi;
        // Depths as pitch deviation (wow 0.08..0.6 %, flutter 0.03..0.28 %).
        const double wow = (0.0008 + 0.0052 * age) * sampleRate / (twoPi * 0.8);
        const double flutter = (0.0003 + 0.0025 * age) * sampleRate / (twoPi * 7.1);
        return wow * (1.0 + 0.65 * std::sin (wowPhase) + 0.35 * wander) + flutter * (1.0 + std::sin (flutterPhase));
    }
    chorusPhase += twoPi * 0.45 / sampleRate;
    if (chorusPhase > twoPi)
        chorusPhase -= twoPi;
    const double depth = (0.0002 + 0.0028 * age) * sampleRate;
    return depth * (1.0 + 0.8 * std::sin (chorusPhase) + 0.2 * wander);
}

void EchoDelay::skip() noexcept
{
    delaySamples += std::clamp ((targetSamples - delaySamples) * glide * 8.0, -0.9, 0.9);
    advanceModulation();
}

void EchoDelay::process (float inL, float inR, float& outL, float& outR) noexcept
{
    if (--countdown <= 0)
    {
        countdown = 32;
        updateCoefficients();
    }
    // TIME glides (tape speed / BBD clock): the repeats bend in pitch while it moves.
    delaySamples += std::clamp ((targetSamples - delaySamples) * glide * 8.0, -0.9, 0.9);

    const double modulation = advanceModulation();
    const double d = std::max (4.0, delaySamples + modulation);
    const float fb = static_cast<float> (feedback * (settings.type == EchoType::tape ? 1.04 : 1.03));

    auto& L = channels[0];
    auto& R = channels[1];
    float wetL = 0.0f, wetR = 0.0f;
    switch (settings.stereo)
    {
        case EchoStereo::mono:
        {
            const float m = 0.5f * (inL + inR);
            const float y = L.line.read (d);
            L.line.push (shape (L, m + fb * y));
            wetL = wetR = y;
            break;
        }
        case EchoStereo::wide:
        {
            const float yL = L.line.read (d);
            const float yR = R.line.read (d * 1.06 + 0.004 * sampleRate);
            L.line.push (shape (L, inL + fb * yL));
            R.line.push (shape (R, inR + fb * yR));
            wetL = yL;
            wetR = yR;
            break;
        }
        case EchoStereo::pingPong:
        {
            // The sound enters on the left; every repeat crosses to the other side.
            const float m = 0.5f * (inL + inR);
            const float yL = L.line.read (d), yR = R.line.read (d);
            L.line.push (shape (L, m + fb * yR));
            R.line.push (shape (R, fb * yL));
            wetL = yL;
            wetR = yR;
            break;
        }
    }
    outL = 0.9f * wetL;
    outR = 0.9f * wetR;

    const bool quiet = inL == 0.0f && inR == 0.0f && std::abs (outL) < 1.0e-6f && std::abs (outR) < 1.0e-6f;
    quietRun = quiet ? quietRun + 1 : 0;
}

} // namespace osp
