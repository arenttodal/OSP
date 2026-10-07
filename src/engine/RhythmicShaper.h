#pragma once

#include "engine/HostTiming.h"

#include <array>
#include <cstdint>

namespace osp
{

enum class ShaperRate { quarter, eighth, eighthTriplet, sixteenth, sixteenthTriplet, thirtySecond };
enum class ShaperTarget { volume, filter, both };

/** A step's curve: hold its value, fall (a struck, decaying hit), rise, dip and come
    back, pulse up and back, or a rounded move from start to end. */
enum class StepShape : std::uint8_t { hold, down, up, dip, pulse, soft };

struct ShaperStep
{
    float start = 1.0f, end = 1.0f;   ///< 0 closed .. 1 open
    StepShape shape = StepShape::hold;
    bool operator== (const ShaperStep&) const = default;
};
using ShaperPattern = std::array<ShaperStep, 16>;

struct ShaperParams
{
    int pattern = 3;                              ///< THREE
    ShaperRate rate = ShaperRate::sixteenth;
    ShaperTarget target = ShaperTarget::both;
    double smooth = 0.3;                          ///< 0 = crisp edges .. 1 = flowing
    bool custom = false;                          ///< play `customSteps` instead of the library pattern
    ShaperPattern customSteps {};                 ///< CUSTOM (the musician's own; empty = all open)
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
    - The MOVEMENT macro is the depth (DEPTH in the popup is the same control): 100 % takes
      the pattern's lowest point to closed - VOL gates to silence - and 10 % only lets the
      level breathe by 10 %. VOL dips the level, FILTER closes a gentle LP12
      (log cutoff 18 kHz .. 380 Hz), BOTH closes the filter fully but dips the level less,
      so closed steps are darker and quieter rather than off.
    - Never clicks: 2 ms anti-click ramps, 30 ms crossfades for pattern and rate changes,
      40 ms for target changes. At zero depth it is an exact bypass.

    The library patterns are immutable compiled-in data; CUSTOM is the musician's own 16
    steps, carried in the parameters. prepare() is the only non-real-time call.
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
    /** The lowest and highest value a pattern's curve reaches at a SMOOTH setting. */
    struct Span
    {
        float low = 0.0f, high = 1.0f;
    };
    static Span span (int pattern, double smooth) noexcept;
    static Span span (const ShaperPattern& steps, double smooth) noexcept;
    /** A library pattern's steps (the starting point of a CUSTOM pattern). */
    static ShaperPattern patternSteps (int pattern) noexcept;
    /** The steps `params` plays (the library pattern or CUSTOM). */
    static ShaperPattern stepsOf (const ShaperParams& params) noexcept;
    /** The pattern's modulation (0 closed .. 1 open) at a phase 0..1 of its cycle, stretched
        over its span so its lowest point is 0 and its highest 1 (at full depth the lowest
        point closes completely). Pure. */
    static float evaluate (int pattern, double phase, double smooth, Span span) noexcept;
    /** The same, finding the span itself (remembered per thread for the last pattern). */
    static float evaluate (int pattern, double phase, double smooth) noexcept;
    static float evaluate (const ShaperPattern& steps, double phase, double smooth, Span span) noexcept;
    /** What `params` plays at a phase (the display): library or CUSTOM, its own span. */
    static float evaluate (const ShaperParams& params, double phase) noexcept;

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
    /** The pattern as designed (its floors included), before it is stretched. */
    static float raw (const ShaperPattern& steps, double phase, double smooth) noexcept;
    double cyclePhase (double quarterNotes, ShaperRate rate) const noexcept;

    double sampleRate = 48000.0;
    ShaperParams params, previous;
    ShaperPattern playing {}, previousSteps {};
    Span currentSpan, previousSpan;
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
