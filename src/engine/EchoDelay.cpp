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
    e.interval = s.echoInterval;
    e.motion = s.echoMotion;
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
    {
        c.line.allocate (static_cast<int> ((maxSeconds * 1.08 + 0.05) * rate) + 64);
        c.shifter.allocate (static_cast<int> (0.13 * rate) + 16);
        searchSpan = static_cast<int> (0.015 * rate);
        searchLength = std::max (32, static_cast<int> (0.005 * rate));
    }
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
        c.shifter.clear();
        c.clockFilter = c.reconstruction = {};
        c.held = c.toneState = 0.0f;
    }
    clockPhase = headPhase = 0.0;
    rng.reseed (0x4543484full);
    wowPhase = flutterPhase = chorusPhase = 0.0;
    wander = wanderFrom = wanderTo = 0.0f;
    wanderCountdown = 0;
    delaySamples = targetSamples;
    countdown = 0;
    quietRun = 0;
    updateCoefficients();
    window = windowTarget;
    for (auto& c : channels)
    {
        // A's grain starts (phase 0), B's is half way through.
        const double start = ratio > 1.0 ? window + 2.0 + 0.5 * searchSpan : 2.0 + 0.5 * searchSpan;
        c.headDelay[0] = start;
        c.headDelay[1] = start + (ratio > 1.0 ? -0.5 : 0.5) * window;
    }
}

double EchoDelay::intervalRatio (AscendInterval interval) noexcept
{
    switch (interval)
    {
        case AscendInterval::fifthUp:    return std::exp2 (7.0 / 12.0);
        case AscendInterval::fourthUp:   return std::exp2 (5.0 / 12.0);
        case AscendInterval::octaveDown: return 0.5;
        case AscendInterval::octaveUp:   break;
    }
    return 2.0;
}

double EchoDelay::clockHz (double age) noexcept
{
    return 13000.0 * std::pow (7.0 / 13.0, std::clamp (age, 0.0, 1.0));
}

double EchoDelay::shifterWindow (double delaySeconds) noexcept
{
    return std::clamp (delaySeconds / 8.0, 0.03, 0.05);
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
    if (settings.type == EchoType::ascend)
    {
        const double clock = clockHz (age);
        clockStep = clock / rate;
        gClock = g (0.5 * clock);
        gReconstruction = g (0.45 * clock);
        toneCoef = static_cast<float> (1.0 - std::exp (-twoPi * 2000.0 * std::pow (2.5, tone) / rate));
        gLowCut = g (70.0);
        ratio = intervalRatio (settings.interval);
        windowTarget = shifterWindow (delaySamples / rate) * rate;
        drive = 1.0f;
    }
    else if (settings.type == EchoType::tape)
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
    if (settings.type == EchoType::ascend)
        return 0.0;   // the shifter is the motion: a wobbling line under it only blurs the interval
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

EchoDelay::Tap EchoDelay::tap (Channel& c, double delay) noexcept
{
    const float y = c.line.read (delay);
    if (settings.type != EchoType::ascend)
        return { y, y };
    // Reconstruction after the clock's hold, then TONE's low-pass before the shifter (or
    // every pass climbs into fizz), then the shifter.
    float low = 0.0f, high = 0.0f;
    c.reconstruction.process (y, gReconstruction, svfK, low, high);
    c.toneState += (low - c.toneState) * toneCoef;
    const float filtered = c.toneState;
    const float shifted = shift (c, filtered, spliceA, spliceB);
    switch (settings.motion)
    {
        case AscendMotion::steady: return { shifted, filtered };
        case AscendMotion::late:   return { filtered, shifted };
        case AscendMotion::rise:   break;
    }
    return { shifted, shifted };
}

float EchoDelay::shift (Channel& c, float x, bool wrapA, bool wrapB) noexcept
{
    // Two heads half a grain apart move through the window at the interval's speed; each
    // fades in and out on a Hann window (sin^2 and cos^2: the pair sums to 1), so a head
    // only jumps back while it is silent.
    c.shifter.push (x);
    if (wrapA)
        c.headDelay[0] = splice (c, c.headDelay[1]);
    if (wrapB)
        c.headDelay[1] = splice (c, c.headDelay[0]);
    const auto s = static_cast<float> (std::sin (std::numbers::pi * headPhase));
    const float gainA = s * s;
    const double limit = static_cast<double> (c.shifter.mask) - 4.0;
    const float y = gainA * c.shifter.read (std::clamp (c.headDelay[0], 1.5, limit))
                    + (1.0f - gainA) * c.shifter.read (std::clamp (c.headDelay[1], 1.5, limit));
    for (auto& d : c.headDelay)
        d += 1.0 - ratio;
    return y;
}

double EchoDelay::splice (const Channel& c, double otherDelay) const noexcept
{
    // Going up, a head starts far back (a window plus the span) and closes in on the write
    // point; going down it starts at the write point and falls back. Within the span, take
    // the start whose next few milliseconds best match the other head's.
    const bool up = ratio > 1.0;
    const int base = up ? static_cast<int> (window) + 2 + searchSpan : 2;
    const int other = static_cast<int> (std::lround (otherDelay));
    const int direction = up ? -1 : 1;   // the way the material ahead of a head lies
    const auto& line = c.shifter;
    auto at = [&line] (int d) { return line.buffer[static_cast<std::size_t> ((line.write - d) & line.mask)]; };
    auto score = [&] (int k, int stride) {
        const int candidate = up ? base - k : base + k;
        float sum = 0.0f;
        for (int j = 0; j < searchLength; j += stride)
            sum += at (other + direction * j) * at (candidate + direction * j);
        return sum;
    };
    // Coarse (every third start, every fourth sample: TONE has taken everything above
    // 5 kHz away, so nothing aliases the match), then exact around the best.
    int best = 0;
    float bestScore = 0.0f;
    for (int k = 0; k <= searchSpan; k += 3)
        if (const float v = score (k, 4); v > bestScore)
        {
            bestScore = v;
            best = k;
        }
    const int centre = best;
    bestScore = score (centre, 1);
    for (int k = std::max (0, centre - 2); k <= std::min (searchSpan, centre + 2); ++k)
        if (const float v = score (k, 1); v > bestScore)
        {
            bestScore = v;
            best = k;
        }
    const double frac = otherDelay - std::floor (otherDelay);
    return (up ? base - best : base + best) + frac;
}

void EchoDelay::write (Channel& c, float x) noexcept
{
    if (settings.type != EchoType::ascend)
    {
        c.line.push (shape (c, x));
        return;
    }
    // The CLOCK: a half-clock anti-alias filter (gentle: a little fold-over is the grain),
    // a hold at the clock rate, a soft ceiling, a low cut so nothing gathers below.
    float low = 0.0f, high = 0.0f;
    c.clockFilter.process (x, gClock, svfK, low, high);
    if (clockPhase + clockStep >= 1.0)
        c.held = low;
    c.lowCut.process (softClip (c.held), gLowCut, svfK, low, high);
    c.line.push (high);
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
    const bool ascend = settings.type == EchoType::ascend;
    const float fb = static_cast<float> (feedback * (ascend ? 0.85 : settings.type == EchoType::tape ? 1.04 : 1.03));
    if (ascend)
    {
        window += (windowTarget - window) * 0.001;
        const double before = headPhase;
        headPhase += std::abs (1.0 - ratio) / window;
        spliceB = before < 0.5 && headPhase >= 0.5;
        spliceA = headPhase >= 1.0;
        headPhase -= std::floor (headPhase);
    }

    auto& L = channels[0];
    auto& R = channels[1];
    float wetL = 0.0f, wetR = 0.0f;
    switch (settings.stereo)
    {
        case EchoStereo::mono:
        {
            const float m = 0.5f * (inL + inR);
            const auto y = tap (L, d);
            write (L, m + fb * y.back);
            wetL = wetR = y.out;
            break;
        }
        case EchoStereo::wide:
        {
            const auto yL = tap (L, d);
            const auto yR = tap (R, d * 1.06 + 0.004 * sampleRate);
            write (L, inL + fb * yL.back);
            write (R, inR + fb * yR.back);
            wetL = yL.out;
            wetR = yR.out;
            break;
        }
        case EchoStereo::pingPong:
        {
            // The sound enters on the left; every repeat crosses to the other side.
            const float m = 0.5f * (inL + inR);
            const auto yL = tap (L, d), yR = tap (R, d);
            write (L, m + fb * yR.back);
            write (R, fb * yL.back);
            wetL = yL.out;
            wetR = yR.out;
            break;
        }
    }
    if (ascend)
    {
        clockPhase += clockStep;
        clockPhase -= std::floor (clockPhase);
    }
    const float gain = ascend ? ascendGain : 0.9f;
    outL = gain * wetL;
    outR = gain * wetR;

    const bool quiet = inL == 0.0f && inR == 0.0f && std::abs (outL) < 1.0e-6f && std::abs (outR) < 1.0e-6f;
    quietRun = quiet ? quietRun + 1 : 0;
}

} // namespace osp
