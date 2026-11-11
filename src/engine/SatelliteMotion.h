#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

namespace osp::satellite
{

/**
    SATELLITE: one card's own stereo motion (ANDOR ANALOG's per-card arpeggiator companion; the
    path and its timing are generic, so any ANDOR instrument with per-card streams can use them).

    A path is a function of the cycle position c (in cycles: sync, quarter notes / a division's
    length + PHASE; free, its own accumulator at RATE) giving a stereo place x in -1 (left) ..
    +1 (right) and, for ORBIT, a "rear" amount 0 (front) .. 1 (behind), which the renderer turns
    into a level and a gentle darkening - perceptual cues only, never claimed as binaural 3D.

    - PAN: a sine from the centre (OFFSET) towards the right, then the left.
    - ORBIT: the same left-right travel, plus the rear cue on the far half of the circle: it is
      heard as going round, not just side to side.
    - BOUNCE: a smoothed alternation between the two sides (a soft square: never a click); with
      NOTE, each new note of the card takes the other side and keeps it (a ping-pong arpeggio).

    DEPTH scales the travel (0: still at OFFSET); the travel never leaves the panorama: from an
    OFFSET off centre it is scaled by the room left (never clipped at an edge). WIDTH is the
    card's own stereo spread (its voices' spread around the travelling point; 0: one point).
*/
enum class Mode : int { pan = 0, orbit, bounce };
enum class Timing : int { sync = 0, free };
enum class Retrigger : int { cycle = 0, note };

struct Division
{
    const char* name;
    double beats;   ///< quarter notes per cycle
};

/** SYNC's lengths, slowest first (a pan is usually slow: four bars down to a 32nd). */
inline constexpr std::array<Division, 13> divisions { { { "4 BARS", 16.0 },
                                                        { "2 BARS", 8.0 },
                                                        { "1/1", 4.0 },
                                                        { "1/2.", 3.0 },
                                                        { "1/2", 2.0 },
                                                        { "1/2T", 4.0 / 3.0 },
                                                        { "1/4.", 1.5 },
                                                        { "1/4", 1.0 },
                                                        { "1/4T", 2.0 / 3.0 },
                                                        { "1/8", 0.5 },
                                                        { "1/8T", 1.0 / 3.0 },
                                                        { "1/16", 0.25 },
                                                        { "1/32", 0.125 } } };
inline constexpr int defaultDivision = 2;   ///< 1/1: one cycle a bar

inline constexpr double minRateHz = 0.02, maxRateHz = 10.0;
inline constexpr const char* modeNames[] = { "PAN", "ORBIT", "BOUNCE" };

struct Settings
{
    bool enabled = false;
    Mode mode = Mode::pan;
    Timing timing = Timing::sync;
    int division = defaultDivision;
    double rateHz = 0.25;   ///< FREE
    double depth = 0.6;     ///< 0..1
    double width = 1.0;     ///< 0..1: the card's own spread (1: as it is)
    double phase = 0.0;     ///< 0..1 of a cycle (0..360 degrees)
    double offset = 0.0;    ///< -1 .. +1: the centre of the travel
    Retrigger retrigger = Retrigger::cycle;

    bool operator== (const Settings&) const = default;

    Settings sanitised() const noexcept
    {
        auto finite = [] (double x, double fallback) { return std::isfinite (x) ? x : fallback; };
        Settings s = *this;
        s.mode = static_cast<int> (mode) >= 0 && static_cast<int> (mode) <= 2 ? mode : Mode::pan;
        s.timing = timing == Timing::free ? Timing::free : Timing::sync;
        s.division = std::clamp (division, 0, static_cast<int> (divisions.size()) - 1);
        s.rateHz = std::clamp (finite (rateHz, 0.25), minRateHz, maxRateHz);
        s.depth = std::clamp (finite (depth, 0.0), 0.0, 1.0);
        s.width = std::clamp (finite (width, 1.0), 0.0, 1.0);
        s.phase = finite (phase, 0.0) - std::floor (finite (phase, 0.0));
        s.offset = std::clamp (finite (offset, 0.0), -1.0, 1.0);
        s.retrigger = retrigger == Retrigger::note ? Retrigger::note : Retrigger::cycle;
        return s;
    }
};

/** Where a path is: its place and (ORBIT) how far behind. */
struct Point
{
    double x = 0.0;      ///< -1 .. +1
    double rear = 0.0;   ///< 0 .. 1
};

/** The path's shape at cycle position c (any real; whole cycles wrap). -1 .. +1. */
inline double shape (Mode mode, double c) noexcept
{
    const double turn = 2.0 * std::numbers::pi * (c - std::floor (c));
    if (mode == Mode::bounce)
    {
        // A soft square: tanh of a sine, normalised (the sides held, the crossings quick but
        // smooth - about a tenth of a cycle each).
        constexpr double k = 5.0;
        return std::tanh (k * std::sin (turn)) / std::tanh (k);
    }
    return std::sin (turn);
}

/** The travel's extent from OFFSET (DEPTH within the room left: never past an edge). */
inline double travel (const Settings& s) noexcept
{
    return s.depth * (1.0 - std::abs (s.offset));
}

/** The place at cycle position c. */
inline Point pointAt (const Settings& s, double c) noexcept
{
    Point p;
    p.x = std::clamp (s.offset + travel (s) * shape (s.mode, c), -1.0, 1.0);
    if (s.mode == Mode::orbit)
    {
        const double turn = 2.0 * std::numbers::pi * (c - std::floor (c));
        p.rear = s.depth * 0.5 * (1.0 - std::cos (turn));
    }
    return p;
}

/** BOUNCE with NOTE: the place of the n-th note (alternating sides from the right). */
inline double notePlace (const Settings& s, int n) noexcept
{
    return std::clamp (s.offset + travel (s) * ((n & 1) == 0 ? 1.0 : -1.0), -1.0, 1.0);
}

/** The cycle position for SYNC at a musical position (quarter notes). */
inline double syncCycle (const Settings& s, double ppq) noexcept
{
    const double beats = divisions[static_cast<std::size_t> (std::clamp (s.division, 0, static_cast<int> (divisions.size()) - 1))].beats;
    return ppq / beats + s.phase;
}

/** The equal-power law the instrument's voices use (unity at the centre, constant power). */
inline void gains (double x, float& left, float& right) noexcept
{
    const double angle = std::numbers::pi * 0.25 * (std::clamp (x, -1.0, 1.0) + 1.0);
    left = static_cast<float> (std::numbers::sqrt2 * std::cos (angle));
    right = static_cast<float> (std::numbers::sqrt2 * std::sin (angle));
}

} // namespace osp::satellite
