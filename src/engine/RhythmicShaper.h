#pragma once

#include "engine/HostTiming.h"

#include <array>
#include <cstdint>

namespace osp
{

enum class ShaperRate { quarter, eighth, eighthTriplet, sixteenth, sixteenthTriplet, thirtySecond };
enum class ShaperTarget { volume, filter, both };

struct ShaperParams
{
    int pattern = 3;                              ///< THREE
    ShaperRate rate = ShaperRate::sixteenth;
    ShaperTarget target = ShaperTarget::both;
    double smooth = 0.3;                          ///< 0 = crisp edges .. 1 = flowing
};

/**
    MOVEMENT's SHAPER (MOVEMENT v2): a curated, host-synchronised 16-step pattern that
    shapes the volume and/or a light rhythmic low-pass of the whole instrument.

    - Phase comes from the host's PPQ while the transport runs (sample-accurate inside the
      block, so the groove never depends on the buffer size). With the transport stopped
      a local clock starts at the pattern's beginning on the first note and stops after the
      instrument has been silent for a while.
    - Each step has a start and end value and a shape (hold, down, up, dip, pulse, soft);
      SMOOTH rounds the shapes and widens the hand-over into the next step.
    - The MOVEMENT macro is the depth. VOL dips the level, FILTER closes a gentle LP12
      (log cutoff 18 kHz .. 380 Hz), BOTH closes the filter fully but dips the level less,
      so closed steps are darker and quieter rather than off.
    - Never clicks: 2 ms anti-click ramps, 30 ms crossfades for pattern and rate changes,
      40 ms for target changes. At zero depth it is an exact bypass.

    Patterns are immutable compiled-in data; prepare() is the only non-real-time call.
*/
class RhythmicShaper
{
public:
    static constexpr int steps = 16;
    static constexpr int patternCount = 12;
    static const char* patternName (int pattern) noexcept;
    static const char* rateName (ShaperRate rate) noexcept;
    /** A step's length in quarter notes (1/16 = 0.25, 1/8T = 1/3 ...). */
    static double stepQuarterNotes (ShaperRate rate) noexcept;
    /** The pattern's modulation (0 closed .. 1 open) at a phase 0..1 of its cycle. Pure. */
    static float evaluate (int pattern, double phase, double smooth) noexcept;

    void prepare (double sampleRate) noexcept;
    void reset() noexcept;
    void setParams (const ShaperParams& params) noexcept;
    void setTiming (const HostTiming& timing) noexcept;
    /** A note started (local clock: the pattern starts with the first note). */
    void noteStarted() noexcept { pendingStart = true; }
    void setVoicesActive (bool active) noexcept { voicesActive = active; }

    /** Advances the clock by one sample (call every sample, whatever the mode). */
    void tick() noexcept;
    /** Shapes one sample at depth `amount` (0..1, the MOVEMENT macro). */
    void process (float& left, float& right, double amount) noexcept;

    /** Where the pattern is (0..1), for the display; -1 while the clock is idle. */
    float displayPhase() const noexcept { return running ? static_cast<float> (phase) : -1.0f; }

private:
    double cyclePhase (double quarterNotes, ShaperRate rate) const noexcept;

    double sampleRate = 48000.0;
    ShaperParams params, previous;
    int fadeRemaining = 0, fadeLength = 1;

    // Clock
    HostTiming host;
    double hostPpq = 0.0, localPpq = 0.0, ppqNow = 0.0;
    bool usingHost = false, localRunning = false, pendingStart = false, voicesActive = false, running = false;
    int idleSamples = 0, idleLimit = 1;
    double phase = 0.0;

    // Depth and anti-click state
    double volumeDepth = 0.0, filterDepth = 0.0, depthCoef = 0.001;
    double gain = 1.0, closeOctaves = 0.0, rampCoef = 0.01;

    // Rhythmic LP12 (TPT state variable filter), coefficients every few samples.
    double g = 1.0, k = 1.3;
    std::array<double, 2> s1 {}, s2 {};
    int coefCountdown = 0;
    double openHz = 18000.0;
};

} // namespace osp
