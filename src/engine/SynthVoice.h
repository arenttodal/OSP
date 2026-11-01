#pragma once

#include "core/Prng.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numbers>

namespace osp::synth
{

/**
    SYNTHESIZE (Phase S1, docs/research/synthesize): a warm analog-style oscillator inside each
    sounding voice, blended with the voice's own sample playback by the source's SYNTHESIZE
    amount. It is not a second synthesizer: the voice that plays the sample owns it (note,
    velocity, lifetime, stealing, pan, filters and level are the voice's).

    Real-time safe throughout: no allocation, no tables, plain state per voice.
*/

/** How the sample and the oscillator share the voice at an amount (internal: the default is
    the one chosen after the listening comparison; the others stay for renders). */
enum class Blend : std::uint8_t
{
    hybrid,       ///< the sample stays whole up to about a third, the oscillator comes in gently, then the sample yields
    equalPower,   ///< PHRASE's: cos / sin
    additive      ///< the oscillator added, the sample ducked over the top half
};

/** The oscillator's level over a note (internal: A is the default). */
enum class Envelope : std::uint8_t
{
    gate,     ///< A: its own short attack, full while the key (or pedal) holds, a short release
    follow    ///< B: the recording's contour first, settling into full sustain over about 0.4 s
};

struct Gains
{
    float sample = 1.0f, synth = 0.0f;
};

/**
    The sample's and the oscillator's gains at a SYNTHESIZE amount (0..1). Exactly
    { 1, 0 } at 0. The oscillator is calibrated to the sample's reference loudness (see
    Voice::referenceLevel), so the gains are perceptual shares.
*/
inline Gains blendGains (double amount, Blend law) noexcept
{
    const double a = std::clamp (amount, 0.0, 1.0);
    if (! (a > 0.0))
        return {};
    constexpr double halfPi = 0.5 * std::numbers::pi;
    double s = 1.0, y = 0.0;
    switch (law)
    {
        case Blend::equalPower:
            s = std::cos (halfPi * a);
            y = std::sin (halfPi * a);
            break;
        case Blend::additive:
            y = a;
            s = a <= 0.5 ? 1.0 : 1.0 - (a - 0.5) * 2.0;
            break;
        case Blend::hybrid:
        {
            // The oscillator rises slowly at first (a^1.5: 10 % is about -30 dB, 25 % about
            // -18 dB, 50 % about -9 dB) so the low range adds body, not a second sound; the
            // sample stays whole up to 35 % and then gives way to nothing at 100 %. Their
            // summed power is then held at the sample's own (uncorrelated sources), so the
            // control changes the colour, not the loudness.
            y = std::pow (a, 1.5);
            s = a <= 0.35 ? 1.0 : std::cos (halfPi * (a - 0.35) / 0.65);
            const double power = s * s + y * y;
            const double norm = power > 0.0 ? 1.0 / std::sqrt (power) : 1.0;
            s *= norm;
            y *= norm;
            break;
        }
    }
    return { static_cast<float> (s), static_cast<float> (y) };
}

/** One voice's oscillator: a PolyBLEP saw, a 12 dB/oct low-pass and its envelope. */
class Voice
{
public:
    /**
        The saw's peak level: its RMS after the low-pass is about referenceRms (-16 dBFS). The
        voice scales it by its recording's loudest 20 ms RMS over referenceRms, so at 100 % the
        oscillator is about as loud as the recording's loudest part (checked by [synth]).
    */
    static constexpr float referenceLevel = 0.30f;
    static constexpr double referenceRms = 0.158;

    void prepare (double outputSampleRate) noexcept
    {
        sampleRate = outputSampleRate;
        attackCoef = static_cast<float> (1.0 - std::exp (-1.0 / (0.004 * sampleRate)));    // ~12 ms to 95 %
        releaseCoef = static_cast<float> (1.0 - std::exp (-1.0 / (0.030 * sampleRate)));   // ~140 ms to -40 dB, ~210 ms to -60 dB
        followUp = static_cast<float> (1.0 - std::exp (-1.0 / (0.005 * sampleRate)));
        followDown = static_cast<float> (1.0 - std::exp (-1.0 / (0.060 * sampleRate)));
        settleStep = 1.0f / static_cast<float> (0.4 * sampleRate);
    }

    /** A new note: the phase from the note's seed (free-running in chords, the same every
        render), the envelope from silence. `velocity` 0..1 opens the tone a little. */
    void start (std::uint64_t seed, double velocity, Envelope mode) noexcept
    {
        Prng rng (Prng::deriveSeed (seed, 0x73796e7468ull, 0));   // "synth"
        phase = rng.uniform (0.0, 1.0);
        brightness = 4.0 + 4.0 * std::clamp (velocity, 0.0, 1.0);
        envelopeMode = mode;
        level = 0.0f;
        gate = true;
        follower = followerPeak = 0.0f;
        settle = 0.0f;
        ic1 = ic2 = 0.0f;
        running = true;
    }

    void release() noexcept { gate = false; }
    void stop() noexcept { running = false; }
    bool isRunning() const noexcept { return running; }
    /** After its release: nothing left to hear. */
    bool isSilent() const noexcept { return ! gate && level < 1.0e-3f; }   // -60 dB

    /** The pitch (Hz) and, from it, the tone's cutoff (control rate). */
    void setPitch (double hz) noexcept
    {
        increment = std::clamp (hz, 1.0, 0.45 * sampleRate) / sampleRate;
        const double cutoff = std::clamp (hz * brightness, 250.0, std::min (12000.0, 0.45 * sampleRate));
        const double g = std::tan (std::numbers::pi * cutoff / sampleRate);
        constexpr double k = 1.0 / 0.6;   // Q 0.6: no resonant peak, a soft knee
        const double a1 = 1.0 / (1.0 + g * (g + k));
        f1 = static_cast<float> (a1);
        f2 = static_cast<float> (g * a1);
        f3 = static_cast<float> (g * g * a1);
    }

    /** One sample at the reference level, its envelope applied. `sampleLevel` (|l| + |r|) / 2
        of the recording drives envelope B. */
    float next (float sampleLevel) noexcept
    {
        // PolyBLEP saw: the naive ramp with each wrap's step smoothed over one sample either
        // side, which removes most of the aliasing at a few operations per sample.
        const double t = phase, dt = increment;
        double v = 2.0 * t - 1.0;
        if (t < dt)
        {
            const double x = t / dt;
            v -= x + x - x * x - 1.0;
        }
        else if (t > 1.0 - dt)
        {
            const double x = (t - 1.0) / dt;
            v -= x * x + x + x + 1.0;
        }
        phase += dt;
        if (phase >= 1.0)
            phase -= 1.0;

        // TPT state-variable low-pass (Zavalishin): stable under fast cutoff changes.
        const float v0 = static_cast<float> (v);
        const float v3 = v0 - ic2;
        const float v1 = f1 * ic1 + f2 * v3;
        const float v2 = ic2 + f2 * ic1 + f3 * v3;
        ic1 = 2.0f * v1 - ic1;
        ic2 = 2.0f * v2 - ic2;

        float target = gate ? 1.0f : 0.0f;
        if (envelopeMode == Envelope::follow && gate)
        {
            // B: the recording's own contour (normalised to its peak so far), handing over to
            // full sustain as the note settles.
            follower += (sampleLevel > follower ? followUp : followDown) * (sampleLevel - follower);
            followerPeak = std::max (followerPeak, follower);
            const float contour = followerPeak > 1.0e-6f ? follower / followerPeak : 0.0f;
            settle = std::min (1.0f, settle + settleStep);
            target = contour + (1.0f - contour) * settle;
        }
        level += (target > level ? attackCoef : releaseCoef) * (target - level);
        return v2 * level * referenceLevel;
    }

private:
    double sampleRate = 48000.0;
    double phase = 0.0, increment = 0.0;
    double brightness = 6.0;
    float f1 = 1.0f, f2 = 0.0f, f3 = 0.0f, ic1 = 0.0f, ic2 = 0.0f;
    float level = 0.0f, attackCoef = 0.0f, releaseCoef = 0.0f;
    float follower = 0.0f, followerPeak = 0.0f, followUp = 0.0f, followDown = 0.0f;
    float settle = 0.0f, settleStep = 0.0f;
    Envelope envelopeMode = Envelope::gate;
    bool gate = false, running = false;
};

} // namespace osp::synth
