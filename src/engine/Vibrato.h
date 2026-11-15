#pragma once

#include <algorithm>
#include <array>
#include <cmath>

namespace osp::vibrato
{

/**
    The MOD WHEEL's VIBRATO (shared by ANDOR OSP and ANDOR ANALOG): a per-note sine that moves
    each voice's pitch - never a post-effect, so chords, the arpeggiator and every voice keep
    their own vibrato, each starting with its note.

    A note's vibrato waits DELAY, then grows over RISE to DEPTH (cents, the most it ever
    swings either side). How much of DEPTH plays is CONTROL's:
    - MOD WHEEL: the wheel (CC 1) sets it, 0 .. DEPTH;
    - ALWAYS ON: all of DEPTH, the wheel free for its routes;
    - ASSIGNED: AMOUNT (0..100 %, default 0: silent) - a knob nobody sees on the panel, made
      for routes: assign a macro, an LFO or an envelope to VIBRATO AMOUNT and that plays it.
    DEPTH and AMOUNT are modulation destinations (per voice). The wheel's own routes (CC 1 to
    any destination) are untouched: VIBRATO is one more thing the wheel can do, not a takeover.

    Pure C++, real-time safe, deterministic (each note's phase starts at 0).
*/
enum class Control : int { modWheel = 0, alwaysOn = 1, assigned = 2 };

struct Settings
{
    bool enabled = false;
    double depthCents = 25.0;     ///< 0..100 cents: the swing either side at full
    double rateHz = 5.5;          ///< 0.5..12 Hz
    double delaySeconds = 0.25;   ///< 0..2 s after the note starts
    double riseSeconds = 0.4;     ///< 0..3 s from nothing to full
    Control control = Control::modWheel;
    double amount = 0.0;          ///< 0..1 (ASSIGNED)
    /** Which of the instrument's sources take part (ANDOR OSP: layers A, B, C; ANDOR ANALOG:
        OSC 1, OSC 2, pitched C, SUB). */
    std::array<bool, 4> sources { true, true, true, false };

    static constexpr double maxDepthCents = 100.0;
};

/** How much of DEPTH plays (0..1) for CONTROL, before the note's delay and rise. */
inline double scaleFor (const Settings& s, double wheel, double amountOffset = 0.0) noexcept
{
    switch (s.control)
    {
        case Control::alwaysOn: return 1.0;
        case Control::assigned: return std::clamp (s.amount + amountOffset, 0.0, 1.0);
        case Control::modWheel:
        default: return std::clamp (wheel, 0.0, 1.0);
    }
}

/** A note's DELAY and RISE: 0 until DELAY, then a smooth (raised-cosine) rise to 1 over RISE. */
inline double onsetAt (const Settings& s, double seconds) noexcept
{
    const double t = seconds - std::max (0.0, s.delaySeconds);
    if (t <= 0.0)
        return 0.0;
    if (s.riseSeconds <= 1.0e-4 || t >= s.riseSeconds)
        return 1.0;
    return 0.5 - 0.5 * std::cos (3.14159265358979323846 * t / s.riseSeconds);
}

/** One voice's vibrato: started with its note, advanced at control rate. */
class Voice
{
public:
    void start() noexcept
    {
        phase = 0.0;
        age = 0.0;
        centsNow = 0.0;
    }

    /** Advances `seconds` and returns the pitch offset now (cents). `wheel` the mod wheel
        (0..1); the offsets are the routes' to VIBRATO DEPTH (cents) and AMOUNT (0..1). */
    double advance (const Settings& s, double seconds, double wheel, double depthOffsetCents = 0.0, double amountOffset = 0.0) noexcept
    {
        if (! s.enabled)
        {
            centsNow = 0.0;
            age += seconds;
            return 0.0;
        }
        const double depth = std::clamp (s.depthCents + depthOffsetCents, 0.0, Settings::maxDepthCents);
        const double scale = scaleFor (s, wheel, amountOffset);
        // The phase runs from the note's start (so every note's vibrato begins at its rest
        // point and grows from there), at the rate now.
        centsNow = depth * scale * onsetAt (s, age) * std::sin (2.0 * 3.14159265358979323846 * phase);
        phase += std::clamp (s.rateHz, 0.05, 40.0) * seconds;
        phase -= std::floor (phase);
        age += seconds;
        return centsNow;
    }

    double cents() const noexcept { return centsNow; }
    double seconds() const noexcept { return age; }

private:
    double phase = 0.0, age = 0.0, centsNow = 0.0;
};

} // namespace osp::vibrato
