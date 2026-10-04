#pragma once

#include "core/Prng.h"
#include "engine/RhythmicShaper.h"
#include "engine/Shaping.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace osp
{

/**
    MOVEMENT on the summed instrument (shaping system v1.0 §32-38, §48):

      DRIFT   (the voices do most of it) a small shared pitch wander of the whole
              instrument, so chords breathe together a little.
      TAPE    one tape machine for everything: wow (slow, partly irregular), flutter
              (3-12 Hz) and wear (level instability, bandwidth movement, saturation).
      CHORUS  a modest bucket-brigade-style chorus: two modulated taps, darkened wet
              path, stereo phase offset.
      PULSE   amplitude/stereo modulation, sine to rounded square (free-running).
      SHAPER  host-synchronised rhythmic patterns on volume and/or a light low-pass
              (RhythmicShaper).

    The amount is smoothed (~50 ms); switching mode crossfades the two over 60 ms. All
    randomness comes from a seeded Prng advanced per sample, so renders are identical
    whatever the block size. prepare() allocates; process() is real-time safe.
*/
class MovementBus
{
public:
    void prepare (double sampleRate, std::uint64_t seed);
    void reset() noexcept;

    /** The movement settings (every mode's own) and the MOVEMENT amount. */
    void setTargets (const Shaping& settings, double amount) noexcept;
    void process (float& left, float& right) noexcept;

    // SHAPER's clock (fed once per block / on note-ons by the engine).
    void setTiming (const HostTiming& timing) noexcept { shaper.setTiming (timing); }
    void noteStarted() noexcept { shaper.noteStarted(); }
    void setVoicesActive (bool active) noexcept { shaper.setVoicesActive (active); }
    /** SHAPER's pattern position (0..1) for the display, -1 when idle or not in SHAPER. */
    float shaperPhase() const noexcept { return mode == MovementMode::shaper ? shaper.displayPhase() : -1.0f; }

private:
    struct Noise
    {
        // Smoothed random walk: a new target every `period` samples, one-pole towards it.
        double value = 0.0, target = 0.0, coef = 0.001;
        int countdown = 0, period = 1;
        double next (Prng& random) noexcept
        {
            if (--countdown <= 0)
            {
                countdown = period;
                target = random.bipolar();
            }
            value += (target - value) * coef;
            return value;
        }
        void setRate (double hz, double rate) noexcept
        {
            period = std::max (1, static_cast<int> (rate / std::max (0.01, hz)));
            coef = 1.0 - std::exp (-2.0 * 3.14159265358979 * std::max (0.01, hz) / rate);
        }
    };

    void render (MovementMode mode, float inL, float inR, float& outL, float& outR) noexcept;
    float readLine (const std::vector<float>& line, double delaySamples) const noexcept;

    double sampleRate = 48000.0;
    std::uint64_t baseSeed = 1;
    Prng rng;

    MovementMode mode = MovementMode::drift, previousMode = MovementMode::drift;
    int fadeRemaining = 0, fadeLength = 1;
    double amountTarget = 0.0, amount = 0.0, amountCoef = 0.001;
    Shaping settings;
    RhythmicShaper shaper;

    std::vector<float> lineL, lineR;   // shared delay line (tape, drift, chorus read it)
    int write = 0, lineMask = 0;

    // Modulation state
    double wowPhase = 0.0, flutterPhase = 0.0, chorusPhase = 0.0, pulsePhase = 0.0;
    Noise wowNoise, flutterNoise, wearLevel, wearTone, driftNoise;
    float tapeLowL = 0.0f, tapeLowR = 0.0f, chorusLowL = 0.0f, chorusLowR = 0.0f;
};

} // namespace osp
