#pragma once

#include "core/Prng.h"
#include "engine/RhythmicShaper.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace osp
{

/**
    The "what kind?" settings behind the five macros (shaping system v1.0): each macro's
    big knob says how much, these say how. Plain data, copied to the engine whenever a
    setting changes (real-time safe). Defaults are the instrument's starting state.

    MOVEMENT (v2) keeps every mode's own settings (0..1), so switching modes and back
    restores them: DRIFT speed/pitch/tone, TAPE wow/flutter/wear, CHORUS rate/width/stereo,
    PULSE rate/shape/stereo, SHAPER pattern/rate/target/smooth. The mapping functions
    below give their musical ranges.
*/

enum class LifeMode { natural, loose, fray };
enum class VelocityCurve { soft, linear, hard };
enum class FilterType { lp24, lp12, hp12, bp12, tilt, off };   ///< off: research/tests only, not offered to musicians
enum class MovementMode { drift, tape, chorus, pulse, shaper };
enum class SpaceType { room, chamber, plate, spring };

struct Shaping
{
    // LIFE
    LifeMode lifeMode = LifeMode::natural;
    double lifePitchCents = 4.0;   ///< 0..15: largest micro-pitch difference between performances
    double lifeTone = 0.30;        ///< 0..1: spectral/colour variation
    double lifeAttack = 0.25;      ///< 0..1: onset/transient variation

    // DYNAMICS (attack and release live in the amplitude envelope)
    VelocityCurve velocityCurve = VelocityCurve::linear;
    double dynamicsTone = 0.35;    ///< 0..1: how much velocity moves CHARACTER (cutoff and envelope depth)

    // CHARACTER
    FilterType filterType = FilterType::lp24;
    double filterMinHz = 450.0;    ///< CHARACTER 0 % (may exceed max: reversed knob)
    double filterMaxHz = 18000.0;  ///< CHARACTER 100 %
    double resonance = 0.10;       ///< 0..0.9
    double drive = 0.12;           ///< 0..1
    double envAmount = 0.10;       ///< -1..1
    double envAttackSeconds = 0.005;
    double envDecaySeconds = 0.7;

    // MOVEMENT (each mode keeps its own settings)
    MovementMode movementMode = MovementMode::drift;
    double driftSpeed = 0.70, driftPitch = 0.25, driftTone = 0.4;      ///< ~0.21 Hz, 5 cents, gentle tone
    double tapeWow = 0.5, tapeFlutter = 0.35, tapeWear = 0.35;
    double chorusRate = 0.45, chorusWidth = 0.6, chorusStereo = 0.6;   ///< ~0.32 Hz, ~3 ms
    double pulseRate = 0.55, pulseShape = 0.3, pulseStereo = 0.3;      ///< ~0.9 Hz
    ShaperParams shaper;                                                ///< THREE, 1/16, BOTH, smooth 0.3

    // SPACE
    SpaceType spaceType = SpaceType::plate;
    double spaceDecaySeconds = 1.8;

    /** A transparent setting for research renders and tests that study other stages. */
    static Shaping neutral()
    {
        Shaping s;
        s.filterType = FilterType::off;
        s.dynamicsTone = 0.0;
        return s;
    }
};

/** What every voice reads at control rate: the settings plus the macro positions they shape. */
struct ShapingState
{
    Shaping shaping;
    double character = 0.9;   ///< CHARACTER macro, 0..1
    double dynamics = 0.65;   ///< DYNAMICS macro, 0..1
    double movement = 0.12;   ///< MOVEMENT macro, 0..1
    std::uint64_t seed = 1;
};

namespace shaping
{
    /** Logarithmic interpolation between two positive values (sub-1 ranges such as 0.01-0.8 Hz included). */
    inline double logLerp (double lo, double hi, double t) noexcept
    {
        const double a = std::log2 (std::max (lo, 1.0e-9)), b = std::log2 (std::max (hi, 1.0e-9));
        return std::exp2 (a + std::clamp (t, 0.0, 1.0) * (b - a));
    }

    /** CHARACTER position (0..1) -> cutoff in octaves re 1 Hz (log mapping between min and max). */
    inline double cutoffOctaves (const Shaping& s, double character) noexcept
    {
        const double lo = std::log2 (std::clamp (s.filterMinHz, 20.0, 20000.0));
        const double hi = std::log2 (std::clamp (s.filterMaxHz, 20.0, 20000.0));
        return lo + std::clamp (character, 0.0, 1.0) * (hi - lo);
    }

    /** Velocity after the DYNAMICS curve (1..127). */
    inline int curvedVelocity (VelocityCurve curve, int velocity) noexcept
    {
        const double v = std::clamp (velocity, 1, 127) / 127.0;
        const double gamma = curve == VelocityCurve::soft ? 0.6 : (curve == VelocityCurve::hard ? 1.7 : 1.0);
        return std::clamp (static_cast<int> (std::lround (127.0 * std::pow (v, gamma))), 1, 127);
    }

    // Movement parameter ranges per mode (A, B, C are 0..1).
    inline double driftSpeedHz (double a) noexcept { return logLerp (0.01, 0.8, a); }
    inline double driftPitchCents (double b) noexcept { return 20.0 * std::clamp (b, 0.0, 1.0); }
    inline double driftToneOctaves (double c) noexcept { return 1.5 * std::clamp (c, 0.0, 1.0); }

    /** A smooth wander in -1..1 that depends only on (seed, time): every voice that asks at
        the same moment gets the same value, whatever the block size (DRIFT's shared part). */
    inline double sharedWander (std::uint64_t seed, double seconds, double rateHz) noexcept
    {
        const double x = std::max (0.0, seconds) * std::max (0.01, rateHz);
        const auto i = static_cast<std::uint64_t> (x);
        const double f = x - static_cast<double> (i);
        auto lattice = [seed] (std::uint64_t k) { return Prng (Prng::deriveSeed (seed, 0x77616e64ull, k)).bipolar(); };
        const double s = f * f * (3.0 - 2.0 * f); // smoothstep between lattice points
        return lattice (i) + s * (lattice (i + 1) - lattice (i));
    }

    /** Filter envelope depth in octaves for ENV in -1..1 (perceptual: small values are subtle). */
    inline double envelopeOctaves (double amount) noexcept
    {
        const double a = std::clamp (amount, -1.0, 1.0);
        return 7.0 * (a < 0.0 ? -1.0 : 1.0) * std::pow (std::abs (a), 1.3);
    }
    inline double chorusRateHz (double a) noexcept { return logLerp (0.05, 3.0, a); }
    inline double chorusWidthMs (double b) noexcept { return logLerp (0.2, 18.0, b); }
    inline double pulseRateHz (double a) noexcept { return logLerp (0.05, 10.0, a); }
    inline double tapeWowHz (double a) noexcept { return logLerp (0.1, 1.0, a); }
    inline double tapeFlutterHz (double b) noexcept { return 3.0 + 9.0 * std::clamp (b, 0.0, 1.0); }

    /** SPACE decay range per type (seconds). */
    inline void decayRange (SpaceType type, double& lo, double& hi) noexcept
    {
        lo = 0.5;
        hi = 5.0;
        switch (type)
        {
            case SpaceType::room:    lo = 0.2; hi = 2.5; break;
            case SpaceType::chamber: lo = 0.5; hi = 5.0; break;
            case SpaceType::plate:   lo = 0.7; hi = 8.0; break;
            case SpaceType::spring:  lo = 0.4; hi = 5.0; break;
        }
    }
}

} // namespace osp
