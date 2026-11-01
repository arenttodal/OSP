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

/** The oscillator's waveform (SYNTH page: WAVE). Saw is S1's. */
enum class Wave : std::uint8_t { saw, square, triangle, sine };

/** What the oscillator's pitch follows (SYNTH page: PITCH). */
enum class PitchSource : std::uint8_t
{
    keys,        ///< the played key exactly (S1): equal temperament, the voice's intentional bends
    recording    ///< the recording as heard at the voice's read position: its tuning and vibrato
};

/**
    One source's SYNTH page (Phase S2). The defaults are S1's oscillator exactly: a saw at the
    played octave, one oscillator, S1's tone, envelope A, the keys' pitch.
*/
struct Settings
{
    static constexpr int minOctave = -2, maxOctave = 1;
    static constexpr double maxDetuneCents = 50.0;

    Wave wave = Wave::saw;
    int octave = 0;              ///< -2..+1 octaves from the played pitch
    double detuneCents = 0.0;    ///< 0..50: two oscillators this far apart (0: one oscillator)
    double tone = 0.5;           ///< 0 dark .. 1 bright: the low-pass from 1/4 to 4 x S1's cutoff
    Envelope shape = Envelope::gate;
    PitchSource pitch = PitchSource::keys;

    bool operator== (const Settings&) const = default;
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

/** One voice's oscillator: a band-limited wave (one, or two detuned), a 12 dB/oct low-pass
    and its envelope. */
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
        gainCoef = static_cast<float> (1.0 - std::exp (-1.0 / (0.007 * sampleRate)));
    }

    /** A new note: the phase from the note's seed (free-running in chords, the same every
        render), the envelope from silence. `velocity` 0..1 opens the tone a little. */
    void start (std::uint64_t seed, double velocity, Envelope mode) noexcept
    {
        Prng rng (Prng::deriveSeed (seed, 0x73796e7468ull, 0));   // "synth"
        phase = rng.uniform (0.0, 1.0);
        phase2 = rng.uniform (0.0, 1.0);   // (after the first: S1's phase unchanged)
        gain1 = 1.0f;
        gain2 = 0.0f;
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

    /** The pitch (Hz) and, from it, the tone's cutoff (control rate). `settings` (S2): the wave,
        octave, detune and tone, live; the defaults are S1's arithmetic exactly. */
    void setPitch (double hz, const Settings& settings = {}) noexcept
    {
        wave = settings.wave;
        if (settings.octave != 0)
        {
            hz *= std::exp2 (static_cast<double> (std::clamp (settings.octave, Settings::minOctave, Settings::maxOctave)));
            while (hz < 20.0 && hz > 0.0)
                hz *= 2.0;   // a sub-octave under a low note stays audible
        }
        const double detune = std::clamp (settings.detuneCents, 0.0, Settings::maxDetuneCents);
        if (detune > 0.0)
        {
            // Two oscillators either side of the pitch, each at -3 dB (uncorrelated: the same power).
            const double spread = std::exp2 (0.5 * detune / 1200.0);
            increment = std::clamp (hz / spread, 1.0, 0.45 * sampleRate) / sampleRate;
            increment2 = std::clamp (hz * spread, 1.0, 0.45 * sampleRate) / sampleRate;
            target1 = target2 = 0.70710678f;
        }
        else
        {
            increment = std::clamp (hz, 1.0, 0.45 * sampleRate) / sampleRate;
            increment2 = increment;
            target1 = 1.0f;
            target2 = 0.0f;
        }
        double tone = brightness;
        if (settings.tone != 0.5)
            tone *= std::exp2 ((std::clamp (settings.tone, 0.0, 1.0) - 0.5) * 4.0);
        const double cutoff = std::clamp (hz * tone, 250.0, std::min (12000.0, 0.45 * sampleRate));
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
        double v = waveAt (phase, increment);
        phase += increment;
        if (phase >= 1.0)
            phase -= 1.0;
        if (target2 > 0.0f || gain2 > 0.0f)
        {
            // DETUNE: the second oscillator, its share eased in or out over about 20 ms.
            gain1 += gainCoef * (target1 - gain1);
            gain2 += gainCoef * (target2 - gain2);
            if (std::abs (target2 - gain2) < 1.0e-5f)
            {
                gain1 = target1;
                gain2 = target2;
            }
            v = gain1 * v + gain2 * waveAt (phase2, increment2);
            phase2 += increment2;
            if (phase2 >= 1.0)
                phase2 -= 1.0;
        }

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
    /** PolyBLEP: a step smoothed over one sample either side (removes most aliasing). */
    static double blep (double t, double dt) noexcept
    {
        if (t < dt)
        {
            const double x = t / dt;
            return x + x - x * x - 1.0;
        }
        if (t > 1.0 - dt)
        {
            const double x = (t - 1.0) / dt;
            return x * x + x + x + 1.0;
        }
        return 0.0;
    }

    /** One sample of the wave at phase `t` (0..1). Each wave is scaled so that, after the
        low-pass, it is about as loud as the saw (S1's reference): square 0.57, triangle 0.96,
        sine 0.78 (checked by [synth]). */
    double waveAt (double t, double dt) const noexcept
    {
        switch (wave)
        {
            case Wave::square:
                return 0.57 * ((t < 0.5 ? 1.0 : -1.0) + blep (t, dt) - blep (t + 0.5 - (t >= 0.5 ? 1.0 : 0.0), dt));
            case Wave::triangle:
                // Its corners' harmonics fall at 12 dB per octave: the low-pass does the rest.
                return 0.96 * (1.0 - 4.0 * std::abs (t - 0.5));
            case Wave::sine:
                return 0.78 * std::sin (2.0 * std::numbers::pi * t);
            case Wave::saw:
                break;
        }
        // S1's saw, unchanged.
        return 2.0 * t - 1.0 - blep (t, dt);
    }

    double sampleRate = 48000.0;
    double phase = 0.0, increment = 0.0;
    double phase2 = 0.0, increment2 = 0.0;
    float gain1 = 1.0f, gain2 = 0.0f, target1 = 1.0f, target2 = 0.0f, gainCoef = 0.0f;
    Wave wave = Wave::saw;
    double brightness = 6.0;
    float f1 = 1.0f, f2 = 0.0f, f3 = 0.0f, ic1 = 0.0f, ic2 = 0.0f;
    float level = 0.0f, attackCoef = 0.0f, releaseCoef = 0.0f;
    float follower = 0.0f, followerPeak = 0.0f, followUp = 0.0f, followDown = 0.0f;
    float settle = 0.0f, settleStep = 0.0f;
    Envelope envelopeMode = Envelope::gate;
    bool gate = false, running = false;
};

} // namespace osp::synth
