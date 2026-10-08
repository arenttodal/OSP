#include "engine/DriveProcessor.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace osp
{

namespace
{
    constexpr double pi = std::numbers::pi;

    /** tanh's [3/2] Pade approximant, clamped where its slope reaches 0 (C1 there): the soft
        saturator every circuit is built from. */
    inline float softClip (float x) noexcept
    {
        x = std::clamp (x, -3.0f, 3.0f);
        const float x2 = x * x;
        return x * (27.0f + x2) / (27.0f + 9.0f * x2);
    }
    inline double softClipSlope (double x) noexcept
    {
        x = std::clamp (x, -3.0, 3.0);
        const double d = 27.0 + 9.0 * x * x;
        return 9.0 * (x * x - 9.0) * (x * x - 9.0) / (d * d);
    }
    /** A softer saturator: x / sqrt (1 + x^2) (TAPE: approaches its limit slowly, so the
        harmonics stay rounded even when driven hard). */
    inline float algebraic (float x) noexcept { return x / std::sqrt (1.0f + x * x); }
    inline double algebraicSlope (double x) noexcept { return 1.0 / std::pow (1.0 + x * x, 1.5); }
    /** A harder knee than tanh, still smooth: x / (1 + x^4)^(1/4) (CRUNCH). */
    inline float knee (float x) noexcept
    {
        const float x2 = x * x;
        return x / std::sqrt (std::sqrt (1.0f + x2 * x2));
    }
    inline double smoothstep (double x) noexcept
    {
        x = std::clamp (x, 0.0, 1.0);
        return x * x * (3.0 - 2.0 * x);
    }
    inline double lerp (double a, double b, double t) noexcept { return a + (b - a) * t; }
    inline double fromDb (double db) noexcept { return std::pow (10.0, db / 20.0); }

    /** Output compensation per circuit (dB at DRIVE 0, 10, .. 100 %), on top of the
        saturation's own ceiling (see voice()). Measured on program material at the
        reference level (K-weighted RMS of a vowel, saw and pluck chord; the [drive-measure]
        test): DRIVE then adds about 0.1 dB per 10 %, density rather than loudness. */
    constexpr std::array<std::array<double, 11>, 3> compensationDb { {
        { 0.0, 0.2, 0.4, 0.6, 0.1, -2.7, -5.0, -6.7, -8.1, -10.0, -12.3 },   // TUBE
        { 0.0, 0.3, 0.6, 0.9, 1.3, 2.0, 1.5, 0.5, -0.2, -0.7, -1.3 },        // TAPE
        { 0.0, 0.0, -0.1, 0.0, -1.5, -3.6, -4.2, -4.3, -5.0, -6.6, -9.3 },   // CRUNCH
    } };
    /** BODY moves the loudness too (the clean share, the bass and the second stage): the
        extra correction at BODY 0 and 1, blended towards 0 at BODY 0.5. */
    constexpr std::array<std::array<std::array<double, 11>, 2>, 3> bodyCompensationDb { {
        { { { 0.0, 0.0, 0.0, 0.0, -0.1, -0.2, -0.4, -0.8, -1.3, -1.5, -1.9 }, { 0.0, 0.0, 0.0, 0.0, 0.0, 0.1, 0.4, 0.9, 1.4, 1.8, 2.1 } } },   // TUBE: BODY 0, 1
        { { { 0.0, 0.0, -0.1, -0.2, -0.3, -0.5, -0.8, -1.3, -1.9, -2.5, -2.9 }, { 0.0, 0.1, 0.1, 0.2, 0.4, 0.8, 1.1, 1.6, 2.2, 2.8, 3.4 } } },   // TAPE
        { { { 0.0, -0.1, -0.1, -0.2, -0.3, -0.4, -1.0, -1.9, -2.9, -3.9, -4.6 }, { 0.0, 0.0, 0.1, 0.2, 0.3, 0.5, 1.1, 2.2, 4.1, 6.6, 9.7 } } },   // CRUNCH
    } };
    double interpolate (const std::array<double, 11>& table, double amount) noexcept
    {
        const double pos = std::clamp (amount, 0.0, 1.0) * 10.0;
        const auto i = std::min (9, static_cast<int> (pos));
        return lerp (table[static_cast<std::size_t> (i)], table[static_cast<std::size_t> (i + 1)], pos - i);
    }
    double compensation (DriveMode mode, double amount, double body) noexcept
    {
        const auto m = static_cast<std::size_t> (mode);
        const double side = body < 0.5 ? interpolate (bodyCompensationDb[m][0], amount) : interpolate (bodyCompensationDb[m][1], amount);
        return interpolate (compensationDb[m], amount) + side * std::min (1.0, std::abs (body - 0.5) * 2.0);
    }

    // The half-band design (Valenzuela & Constantinides; as in Laurent de Soras' HIIR):
    // an elliptic polyphase allpass pair from the stop-band attenuation and transition width.
    void transitionParameters (double transition, double& k, double& q) noexcept
    {
        k = std::tan ((1.0 - 2.0 * transition) * pi / 4.0);
        k *= k;
        const double kksqrt = std::pow (1.0 - k * k, 0.25);
        const double e = 0.5 * (1.0 - kksqrt) / (1.0 + kksqrt);
        const double e2 = e * e, e4 = e2 * e2;
        q = e * (1.0 + e4 * (2.0 + e4 * (15.0 + 150.0 * e4)));
    }
    int filterOrder (double attenuationDb, double q) noexcept
    {
        const double attn = std::pow (10.0, -attenuationDb / 10.0);
        const double a = attn / (1.0 - attn);
        int order = static_cast<int> (std::ceil (std::log (a * a / 16.0) / std::log (q)));
        if ((order & 1) == 0)
            ++order;
        return std::max (order, 3);
    }
    double accumulatedNumerator (double q, int order, int c) noexcept
    {
        double result = 0.0, term = 0.0;
        int sign = 1;
        for (int i = 0; i < 64; ++i, sign = -sign)
        {
            term = std::pow (q, i * (i + 1)) * std::sin ((2 * i + 1) * c * pi / order) * sign;
            result += term;
            if (std::abs (term) < 1.0e-100)
                break;
        }
        return result;
    }
    double accumulatedDenominator (double q, int order, int c) noexcept
    {
        double result = 0.0, term = 0.0;
        int sign = -1;
        for (int i = 1; i < 64; ++i, sign = -sign)
        {
            term = std::pow (q, i * i) * std::cos (2 * i * c * pi / order) * sign;
            result += term;
            if (std::abs (term) < 1.0e-100)
                break;
        }
        return result;
    }
}

DriveProcessor::Settings DriveProcessor::Settings::from (const Shaping& s, double amount) noexcept
{
    Settings d;
    d.amount = std::clamp (amount, 0.0, 1.0);
    d.mode = s.driveMode;
    d.tone = std::clamp (s.driveTone, 0.0, 1.0);
    d.body = std::clamp (s.driveBody, 0.0, 1.0);
    return d;
}

int DriveProcessor::designHalfband (double attenuationDb, double transition, float* coefficients, int maxCount) noexcept
{
    double k = 0.0, q = 0.0;
    transitionParameters (transition, k, q);
    const int order = filterOrder (attenuationDb, q);
    const int count = std::min ((order - 1) / 2, maxCount);
    for (int index = 0; index < count; ++index)
    {
        const int c = index + 1;
        const double num = accumulatedNumerator (q, order, c) * std::pow (q, 0.25);
        const double den = accumulatedDenominator (q, order, c) + 0.5;
        const double ww = num / den;
        const double wwsq = ww * ww;
        const double x = std::sqrt ((1.0 - wwsq * k) * (1.0 - wwsq / k)) / (1.0 + wwsq);
        coefficients[index] = static_cast<float> ((1.0 - x) / (1.0 + x));
    }
    return count;
}

//==============================================================================
float DriveProcessor::AllpassPath::process (float x) noexcept
{
    for (int i = 0; i < count; ++i)
    {
        const auto n = static_cast<std::size_t> (i);
        const float y = c[n] * (x - y1[n]) + x1[n];
        x1[n] = x;
        y1[n] = y;
        x = y;
    }
    return x;
}

void DriveProcessor::AllpassPath::reset() noexcept
{
    x1.fill (0.0f);
    y1.fill (0.0f);
}

void DriveProcessor::Halfband::set (const float* coefficients, int count) noexcept
{
    even.count = odd.count = 0;
    for (int i = 0; i < count; ++i)
    {
        auto& path = (i & 1) == 0 ? even : odd;
        if (path.count < maxSections)
            path.c[static_cast<std::size_t> (path.count++)] = coefficients[i];
    }
    reset();
}

void DriveProcessor::Halfband::up (float x, float& out0, float& out1) noexcept
{
    out0 = even.process (x);
    out1 = odd.process (x);
}

float DriveProcessor::Halfband::down (float in0, float in1) noexcept
{
    return 0.5f * (even.process (in1) + odd.process (in0));
}

void DriveProcessor::Halfband::reset() noexcept
{
    even.reset();
    odd.reset();
}

DriveProcessor::Coefficients DriveProcessor::shelf (bool high, double hz, double gainDb, double rate) noexcept
{
    // A first-order shelf (bilinear, prewarped): high: (G s + 1) / (s + 1), low: (s + G) / (s + 1).
    const double g = fromDb (gainDb);
    const double k = std::tan (pi * std::clamp (hz, 10.0, 0.45 * rate) / rate);
    const double den0 = 1.0 / k + 1.0, den1 = 1.0 - 1.0 / k;
    const double num0 = high ? g / k + 1.0 : 1.0 / k + g;
    const double num1 = high ? 1.0 - g / k : g - 1.0 / k;
    return { static_cast<float> (num0 / den0), static_cast<float> (num1 / den0), static_cast<float> (den1 / den0) };
}

DriveProcessor::Coefficients DriveProcessor::inverse (const Coefficients& c) noexcept
{
    // 1 / H: swap numerator and denominator (both minimum-phase, so the inverse is stable).
    return { 1.0f / c.b0, c.a1 / c.b0, c.b1 / c.b0 };
}

//==============================================================================
DriveProcessor::Voicing DriveProcessor::voice (DriveMode mode, double amount, double tone, double body, double baseRate, double inner) noexcept
{
    amount = std::clamp (amount, 0.0, 1.0);
    tone = std::clamp (tone, 0.0, 1.0);
    body = std::clamp (body, 0.0, 1.0);
    // The first half of the knob is the nuanced half: the drive gain grows slowly at first.
    const double curve = std::pow (amount, 1.2);
    const double late = smoothstep ((amount - 0.3) / 0.7);   // the second stage joins from ~30 %
    Voicing v;
    v.mode = mode;
    double lowHz = 130.0, lowDb = 0.0, highHz = 2800.0, highDb = 0.0, smoothHz = 12000.0, maxDb = 34.0;
    switch (mode)
    {
        case DriveMode::tube:
            maxDb = 34.0;
            v.w2 = static_cast<float> (std::min (1.0, late * (0.6 + 0.5 * body)));
            v.g2 = static_cast<float> (1.0 + 1.0 * late);
            v.bias = static_cast<float> (0.1 + 0.15 * amount + 0.06 * (body - 0.5));
            // Driven harder, the operating point shifts with the level: the clipping stays
            // asymmetric however loud the note, so the even (warm) harmonics remain.
            v.biasDrift = static_cast<float> (0.16 * curve * (0.7 + 0.6 * body));
            // The bass has its own gentle, symmetric stage (a complementary split at 150 Hz),
            // so a low note cannot modulate the asymmetric stage that colours the rest.
            v.splitG = static_cast<float> (std::tan (pi * 150.0 / inner));
            v.lowDrive = static_cast<float> (0.25 + 0.4 * body);
            lowHz = 150.0;
            lowDb = -10.0 + 6.0 * body;
            highHz = 2800.0;
            highDb = lerp (5.0, -4.0, tone);
            v.dry = static_cast<float> (0.16 * (1.0 - body));
            smoothHz = lerp (6500.0, 16000.0, tone);
            break;
        case DriveMode::tape:
            maxDb = 22.0;
            v.w2 = static_cast<float> (late * 0.6 * (0.6 + 0.8 * body));
            v.g2 = static_cast<float> (1.0 + 1.2 * late);
            v.bias = static_cast<float> (0.03 * amount);
            v.feedback = static_cast<float> (std::min (0.4, 0.12 * smoothstep (amount / 0.1) + 0.25 * body * amount));
            v.push = static_cast<float> (0.9 * body * std::min (1.0, 2.0 * amount));
            {
                // The lag (highs saturating and softening first) comes in with DRIVE: none
                // when barely driven.
                const double lagHz = lerp (8000.0, 14000.0, tone);
                const double full = 1.0 - std::exp (-2.0 * pi * lagHz / inner);
                v.lag = static_cast<float> (lerp (1.0, full, std::sqrt (curve)));
            }
            lowHz = 90.0;
            lowDb = -4.0 + 4.0 * body;
            highHz = 3200.0;
            highDb = lerp (8.0, 1.0, tone);
            v.dry = static_cast<float> (0.08 * (1.0 - body));
            smoothHz = lerp (7000.0, 15000.0, tone);
            break;
        case DriveMode::crunch:
            maxDb = 38.0;
            v.w2 = 1.0f;
            v.g2 = static_cast<float> (1.0 + (2.0 + 0.6 * body) * late);
            v.bias = static_cast<float> (0.12 + 0.18 * amount);
            v.biasDrift = static_cast<float> (0.08 * curve);
            v.mid = static_cast<float> ((0.9 + 0.5 * (1.0 - body)) * curve);
            {
                const double g = std::tan (pi * 1100.0 / inner);
                v.midG = static_cast<float> (g);
                v.midK = 1.4f;   // Q ~0.7
            }
            lowHz = 200.0;
            lowDb = -14.0 + 8.0 * body;
            highHz = 3500.0;
            highDb = lerp (3.0, -5.0, tone);
            v.dry = static_cast<float> (0.2 * (1.0 - body));
            smoothHz = lerp (5500.0, 13000.0, tone);
            break;
    }
    // (Below ~8 % the stages also ease out of their own curvature: transparent at the start.)
    v.g1 = static_cast<float> (fromDb (maxDb * curve) * lerp (0.25, 1.0, smoothstep (amount / 0.08)));
    if (mode == DriveMode::tape)
    {
        v.biasOffset = algebraic (v.bias);
        v.biasNorm = static_cast<float> (1.0 / algebraicSlope (v.bias));
    }
    else
    {
        v.biasOffset = softClip (v.bias);
        v.biasNorm = static_cast<float> (1.0 / softClipSlope (v.bias));
    }
    v.levelAttack = static_cast<float> (1.0 - std::exp (-1.0 / (0.02 * inner)));
    v.levelRelease = static_cast<float> (1.0 - std::exp (-1.0 / (0.12 * inner)));
    v.couple = static_cast<float> (1.0 - std::exp (-2.0 * pi * 5.0 / inner));
    // The dry share only matters once the stages saturate.
    v.dry *= static_cast<float> (smoothstep (amount / 0.4));
    // A saturator's ceiling: once driven, its output stops growing with g1. The static
    // compensation restores that (up to the table's measured correction).
    const double ceiling = std::max (0.0, 20.0 * std::log10 (v.g1 * referenceLevel * 1.2));
    v.comp = static_cast<float> (fromDb (ceiling + compensation (mode, amount, body)));
    v.preLow = shelf (false, lowHz, lowDb, baseRate);
    v.postLow = inverse (v.preLow);
    v.preHigh = shelf (true, highHz, highDb, baseRate);
    v.postHigh = inverse (v.preHigh);
    v.smoothMix = static_cast<float> (curve);
    v.smoothCoef = static_cast<float> (1.0 - std::exp (-2.0 * pi * std::min (smoothHz, 0.45 * baseRate) / baseRate));
    return v;
}

float DriveProcessor::shapeInner (const Voicing& v, Channel& c, float u, float push) noexcept
{
    float shaped = u;
    switch (v.mode)
    {
        case DriveMode::tube:
        {
            // Complementary split: hi = 2nd-order high-pass, lo = the rest (exact sum).
            constexpr float k = 1.41421356f;
            const float a1 = 1.0f / (1.0f + v.splitG * (v.splitG + k)), a2 = v.splitG * a1, a3 = v.splitG * a2;
            const float v3 = u - c.splitIc2;
            const float v1 = a1 * c.splitIc1 + a2 * v3;
            const float v2 = c.splitIc2 + a2 * c.splitIc1 + a3 * v3;
            c.splitIc1 = 2.0f * v1 - c.splitIc1;
            c.splitIc2 = 2.0f * v2 - c.splitIc2;
            const float hi = u - k * v1 - v2;
            const float lo = u - hi;
            const float bass = softClip (v.lowDrive * v.g1 * lo) / v.lowDrive;   // driven units
            const float x = v.g1 * hi;
            c.level += (std::abs (x) > c.level ? v.levelAttack : v.levelRelease) * (std::abs (x) - c.level);
            const float b = std::min (2.0f, v.bias + v.biasDrift * c.level);
            float a = (softClip (x + b) - softClip (b)) * v.biasNorm;
            // The coupling capacitor between the stages takes the asymmetry's DC away.
            c.couple += v.couple * (a - c.couple);
            a -= c.couple;
            const float s2 = softClip (v.g2 * a) / v.g2;
            shaped = (a + v.w2 * (s2 - a) + bass) / v.g1;
            break;
        }
        case DriveMode::tape:
        {
            // Magnetisation follows the field with a lag and a little memory of itself:
            // smooth, dense saturation, highs taken first; transients pushed harder.
            const float g = v.g1 * (1.0f + v.push * push);
            const float t = (algebraic (g * u + v.feedback * c.magnet + v.bias) - v.biasOffset) * v.biasNorm;
            c.magnet += v.lag * (t - c.magnet);
            const float m = c.magnet * (1.0f - v.feedback);   // driven units (small-signal gain g)
            const float s2 = algebraic (v.g2 * m) / v.g2;
            shaped = (m + v.w2 * (s2 - m)) / g;
            break;
        }
        case DriveMode::crunch:
        {
            const float x = v.g1 * u;
            c.level += (std::abs (x) > c.level ? v.levelAttack : v.levelRelease) * (std::abs (x) - c.level);
            const float b = std::min (2.0f, v.bias + v.biasDrift * c.level);
            const float a = (softClip (0.7f * x + b) - softClip (b)) * v.biasNorm / 0.7f;
            // Mid-forward: a band around 1.1 kHz pushed into the hard-knee stage (TPT SVF).
            const float a1 = 1.0f / (1.0f + v.midG * (v.midG + v.midK)), a2 = v.midG * a1, a3 = v.midG * a2;
            const float v3 = a - c.bandIc2;
            const float v1 = a1 * c.bandIc1 + a2 * v3;
            const float v2 = c.bandIc2 + a2 * c.bandIc1 + a3 * v3;
            c.bandIc1 = 2.0f * v1 - c.bandIc1;
            c.bandIc2 = 2.0f * v2 - c.bandIc2;
            const float m = a + v.mid * v1;
            const float k = knee (v.g2 * m) / v.g2;
            shaped = softClip (1.25f * k) / (1.25f * v.g1);
            break;
        }
    }
    return shaped + v.dry * (u - shaped);
}

//==============================================================================
void DriveProcessor::Channel::reset() noexcept
{
    preLow.reset();
    preHigh.reset();
    postLow.reset();
    postHigh.reset();
    up1.reset();
    up2.reset();
    down2.reset();
    down1.reset();
    couple = magnet = smooth = level = 0.0f;
    bandIc1 = bandIc2 = 0.0f;
    splitIc1 = splitIc2 = 0.0f;
    dcX = dcY = 0.0f;
}

void DriveProcessor::Core::apply (const Voicing& v) noexcept
{
    voicing = v;
    for (auto& c : channels)
    {
        auto set = [] (FirstOrder& f, const Coefficients& k) {
            f.b0 = k.b0;
            f.b1 = k.b1;
            f.a1 = k.a1;
        };
        set (c.preLow, v.preLow);
        set (c.preHigh, v.preHigh);
        set (c.postLow, v.postLow);
        set (c.postHigh, v.postHigh);
    }
}

void DriveProcessor::Core::reset() noexcept
{
    for (auto& c : channels)
        c.reset();
}

void DriveProcessor::prepare (double rate)
{
    sampleRate = rate;
    // 4x up to 48 kHz (and between), 2x from 88.2 kHz: the core always runs at 176-192 kHz.
    factor = rate >= 80000.0 ? 2 : 4;
    innerRate = rate * factor;
    // Stage 1 (the base rate's band edge): 90 dB, flat to ~0.42 of the base rate. Stage 2 has
    // only the lower half to keep: a short filter.
    stage1Count = designHalfband (90.0, 0.04, stage1.data(), 2 * maxSections);
    stage2Count = designHalfband (80.0, 0.146, stage2.data(), 2 * maxSections);
    for (auto& core : cores)
        for (auto& c : core.channels)
        {
            c.up1.set (stage1.data(), stage1Count);
            c.down1.set (stage1.data(), stage1Count);
            c.up2.set (stage2.data(), stage2Count);
            c.down2.set (stage2.data(), stage2Count);
        }
    smoothCoef = 1.0 - std::exp (-static_cast<double> (controlInterval) / (0.03 * rate));
    dcCoef = static_cast<float> (1.0 - 2.0 * pi * 6.0 / rate);
    auto coef = [rate] (double seconds) { return static_cast<float> (1.0 - std::exp (-1.0 / (seconds * rate))); };
    fastAttack = coef (0.0004);
    fastRelease = coef (0.025);
    slowAttack = coef (0.03);
    slowRelease = coef (0.3);
    reset();
}

void DriveProcessor::reset() noexcept
{
    for (auto& core : cores)
        core.reset();
    envFast = envSlow = 0.0f;
    fadingCore = -1;
    modeFade = 1.0f;
    countdown = 0;
    // A reset starts from the settings as they are (a bounce never ramps in).
    current = target;
    activeCore = static_cast<int> (target.mode);
    engaged = target.amount > 0.0;
    engage = engaged ? 1.0f : 0.0f;
    if (engaged)
        cores[static_cast<std::size_t> (activeCore)].apply (voice (current.mode, current.amount, current.tone, current.body, sampleRate, innerRate));
}

void DriveProcessor::setSettings (const Settings& settings) noexcept
{
    target = settings;
    target.amount = std::clamp (target.amount, 0.0, 1.0);
}

void DriveProcessor::updateControl() noexcept
{
    const double k = smoothCoef;
    current.amount += (target.amount - current.amount) * k;
    if (target.amount == 0.0 && current.amount < 1.0e-4)
        current.amount = 0.0;
    current.tone += (target.tone - current.tone) * k;
    current.body += (target.body - current.body) * k;
    // A new circuit fades in over 50 ms from a clean start (one change at a time).
    if (fadingCore < 0 && static_cast<int> (target.mode) != activeCore)
    {
        fadingCore = activeCore;
        activeCore = static_cast<int> (target.mode);
        cores[static_cast<std::size_t> (activeCore)].reset();
        modeFade = 0.0f;
        modeFadeStep = static_cast<float> (1.0 / (0.05 * sampleRate));
    }
    auto refresh = [this] (int index) {
        cores[static_cast<std::size_t> (index)].apply (voice (static_cast<DriveMode> (index), current.amount, current.tone, current.body, sampleRate, innerRate));
    };
    refresh (activeCore);
    if (fadingCore >= 0)
        refresh (fadingCore);
    // In while DRIVE is up; out (10 ms) once it has come down to nothing.
    const bool wanted = target.amount > 0.0 || current.amount > 2.0e-3;
    engageStep = static_cast<float> ((wanted ? 1.0 : -1.0) / (0.01 * sampleRate));
}

float DriveProcessor::runCore (Core& core, int channel, float x, float push) noexcept
{
    auto& v = core.voicing;
    auto& c = core.channels[static_cast<std::size_t> (channel)];
    const float e = c.preHigh.process (c.preLow.process (x));
    float y;
    float h0, h1;
    c.up1.up (e, h0, h1);
    if (factor == 4)
    {
        float q0, q1, q2, q3;
        c.up2.up (h0, q0, q1);
        c.up2.up (h1, q2, q3);
        q0 = shapeInner (v, c, q0, push);
        q1 = shapeInner (v, c, q1, push);
        q2 = shapeInner (v, c, q2, push);
        q3 = shapeInner (v, c, q3, push);
        // (Two statements: the decimator is stateful, and argument order is unspecified.)
        const float d0 = c.down2.down (q0, q1);
        const float d1 = c.down2.down (q2, q3);
        y = c.down1.down (d0, d1);
    }
    else
    {
        h0 = shapeInner (v, c, h0, push);
        h1 = shapeInner (v, c, h1, push);
        y = c.down1.down (h0, h1);
    }
    y = c.postLow.process (c.postHigh.process (y));
    // The top end smoothed a little more the harder it is driven (none at low amounts).
    c.smooth += v.smoothCoef * (y - c.smooth);
    y += v.smoothMix * (c.smooth - y);
    // DC blocker (the asymmetric stages make some).
    const float out = y - c.dcX + dcCoef * c.dcY;
    c.dcX = y;
    c.dcY = out;
    return out * v.comp;
}

void DriveProcessor::process (float& left, float& right) noexcept
{
    if (! engaged)
    {
        if (target.amount <= 0.0)
            return;   // not in the signal path
        engaged = true;
        engage = 0.0f;
        current.amount = 0.0;   // rises from transparent
        current.tone = target.tone;
        current.body = target.body;
        activeCore = static_cast<int> (target.mode);
        fadingCore = -1;
        cores[static_cast<std::size_t> (activeCore)].reset();
        envFast = envSlow = 0.0f;
        countdown = 0;
    }
    if (--countdown <= 0)
    {
        countdown = controlInterval;
        updateControl();
    }

    // TAPE's transient detector, linked (both channels push together).
    const float level = std::max (std::abs (left), std::abs (right));
    envFast += (level > envFast ? fastAttack : fastRelease) * (level - envFast);
    envSlow += (level > envSlow ? slowAttack : slowRelease) * (level - envSlow);
    const float push = std::clamp (envFast / (envSlow + 1.0e-4f) - 1.0f, 0.0f, 2.5f);

    auto& a = cores[static_cast<std::size_t> (activeCore)];
    float yl = runCore (a, 0, left, push);
    float yr = runCore (a, 1, right, push);
    if (fadingCore >= 0)
    {
        auto& b = cores[static_cast<std::size_t> (fadingCore)];
        const float ol = runCore (b, 0, left, push), orr = runCore (b, 1, right, push);
        yl = ol + modeFade * (yl - ol);
        yr = orr + modeFade * (yr - orr);
        modeFade += modeFadeStep;
        if (modeFade >= 1.0f)
        {
            modeFade = 1.0f;
            fadingCore = -1;
        }
    }
    engage = std::clamp (engage + engageStep, 0.0f, 1.0f);
    left += engage * (yl - left);
    right += engage * (yr - right);
    if (engage <= 0.0f && target.amount <= 0.0)
        engaged = false;   // out of the path again
}

double DriveProcessor::transfer (const Settings& s, double x, double push) noexcept
{
    if (s.amount <= 0.0)
        return x;   // bypassed: the identity
    auto v = voice (s.mode, s.amount, s.tone, s.body, 48000.0, 192000.0);
    v.lag = 1.0f;   // steady state
    Channel c;
    const auto u = static_cast<float> (x * referenceLevel);
    // The level the bias follows: a tone of this peak (its mean level), held.
    c.level = static_cast<float> (2.0 / std::numbers::pi) * std::abs (v.g1 * u);
    v.levelAttack = v.levelRelease = 0.0f;
    float y = 0.0f;
    // TAPE's loop settles over a few passes; the others are memoryless here.
    for (int i = 0; i < (s.mode == DriveMode::tape ? 40 : 1); ++i)
        y = shapeInner (v, c, u, static_cast<float> (push));
    return static_cast<double> (y * v.comp) / referenceLevel;
}

} // namespace osp
