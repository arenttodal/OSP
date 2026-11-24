#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <utility>

namespace osp::usermacro
{

/**
    USER MACROS (ANDOR ANALOG docs/USER_MACROS_V1_1.md; ANDOR OSP docs/research/osp-user-macros): the pure core, shared by both instruments - the linear mapping
    of one macro onto one target, the bounded compiled table the audio thread reads, and the
    lock-free hand-over of that table. No JUCE, no allocation, no state but what is passed in.

    Every value is in the destination's normalised (0..1) parameter domain: a mapping is an
    additive offset on the target's own (live) value, never a write to it.
*/
constexpr int macroCount = 6;
constexpr int mapsPerMacro = 8;
constexpr int maxMaps = macroCount * mapsPerMacro;   ///< 48

enum class Curve : std::uint8_t { linear, ease };

/** One assignment's shape (spec §6.2, made linear in schema 2). LOW and HIGH are where the
    target goes at macro 0 % and 100 %, a straight line between them, wherever the macro was when
    the assignment was made. The line is an offset on the target's own value: it is measured from
    anchorTarget (the target's value when the range was set), so turning the target's own knob
    moves the whole line with it. */
struct Mapping
{
    float anchorMacro = 0.0f;    ///< the macro's position when the assignment was made (a record: the line never bends there)
    float anchorTarget = 0.0f;   ///< b0: the target's normalised value the range is measured from
    float low = 0.0f;            ///< L: where the target goes at macro 0 (from b0)
    float high = 0.0f;           ///< H: where it goes at macro 1
    Curve curve = Curve::linear;
    bool enabled = true;
    bool operator== (const Mapping&) const = default;
};

/** The offset a mapping adds to its target at macro position `m` (0..1): a straight line from
    LOW at 0 % to HIGH at 100 % (EASE: an S over the whole travel, same ends). LOW above HIGH
    inverts it. */
inline double offsetOf (const Mapping& map, double m) noexcept
{
    if (! map.enabled)
        return 0.0;
    m = std::clamp (m, 0.0, 1.0);
    const double t = map.curve == Curve::ease ? m * m * (3.0 - 2.0 * m) : m;
    const double low = static_cast<double> (map.low), high = static_cast<double> (map.high);
    return low + (high - low) * t - static_cast<double> (map.anchorTarget);
}

/** A new assignment's default range: a straight line through the target's value now (b0) at the
    macro's position now (a), so making it never jumps, rising by up to a quarter of the travel
    over the macro's whole sweep (falling where there is no room to rise). Only a target at the
    very end of its travel with the macro part-way (no visible line through that point fits)
    starts its range from b0 at macro 0 % instead, as an assignment made at 0 % does. */
inline std::pair<float, float> defaultRange (float macroNow, float targetNow) noexcept
{
    constexpr double sweep = 0.25, least = 0.1, epsilon = 1.0e-6;
    const double a = std::clamp (static_cast<double> (macroNow), 0.0, 1.0);
    const double b0 = std::clamp (static_cast<double> (targetNow), 0.0, 1.0);
    // The steepest slope (up to `sweep`) whose line through (a, b0) stays inside 0..1.
    auto room = [a, b0] (bool up) {
        double s = sweep;
        if (a > epsilon)
            s = std::min (s, (up ? b0 : 1.0 - b0) / a);
        if (a < 1.0 - epsilon)
            s = std::min (s, (up ? 1.0 - b0 : b0) / (1.0 - a));
        return std::max (0.0, s);
    };
    const double up = room (true), down = room (false);
    const bool rise = up >= least || up >= down;
    const double s = rise ? up : down;
    if (s >= least)
    {
        const double sign = rise ? 1.0 : -1.0;
        return { static_cast<float> (std::clamp (b0 - sign * s * a, 0.0, 1.0)),
                 static_cast<float> (std::clamp (b0 + sign * s * (1.0 - a), 0.0, 1.0)) };
    }
    const double to = b0 + sweep <= 1.0 ? b0 + sweep : b0 - sweep;
    return { static_cast<float> (b0), static_cast<float> (std::clamp (to, 0.0, 1.0)) };
}

/** A schema-1 mapping (anchored: no movement on the side of the anchor it was made at 0 % or
    100 %, two straight pieces meeting at the anchor otherwise) as a linear one. Made at 0 % or
    100 %: exactly the same line (the unused end becomes the anchor's value). Made part-way:
    the same ends, a straight line between them. */
inline Mapping linearisedFromSchema1 (Mapping map) noexcept
{
    constexpr float epsilon = 1.0e-6f;
    if (map.anchorMacro <= epsilon)
        map.low = map.anchorTarget;
    else if (map.anchorMacro >= 1.0f - epsilon)
        map.high = map.anchorTarget;
    return map;
}

/** A sanitised mapping: finite, in range (a NaN or an infinity becomes the neutral value). */
inline Mapping sanitised (Mapping map) noexcept
{
    auto unit = [] (float v, float fallback) { return std::isfinite (v) ? std::clamp (v, 0.0f, 1.0f) : fallback; };
    map.anchorMacro = unit (map.anchorMacro, 0.0f);
    map.anchorTarget = unit (map.anchorTarget, 0.0f);
    map.low = unit (map.low, map.anchorTarget);
    map.high = unit (map.high, map.anchorTarget);
    return map;
}

/** Where a target lives (the audio thread's address for it). */
enum class TargetKind : std::uint8_t { synth, fx };

struct CompiledMap
{
    std::uint8_t macro = 0;               ///< 0..5
    TargetKind kind = TargetKind::synth;
    std::int16_t index = -1;              ///< the product's live-value index for the target
    Mapping map;
};

/** The table the audio thread reads: bounded, trivially copyable, active assignments only
    (an orphaned or disabled one is never compiled in). */
struct Snapshot
{
    int count = 0;
    std::array<CompiledMap, maxMaps> maps {};
    bool empty() const noexcept { return count == 0; }
};

/**
    A single-writer (message thread), single-reader (audio thread) triple buffer for a
    trivially copyable value: the writer never waits for the reader, the reader never waits
    and never allocates; the reader sees the latest complete value at its next read.
*/
template <typename T>
class TripleBuffer
{
public:
    /** Writer: hands over a new value (the previous unread one, if any, is dropped). */
    void publish (const T& value) noexcept
    {
        slots[static_cast<std::size_t> (back)] = value;
        back = middle.exchange (back | fresh, std::memory_order_acq_rel) & indexMask;
    }
    /** Reader: the latest value handed over (taking a newer one first if there is one). */
    const T& read() noexcept
    {
        if ((middle.load (std::memory_order_acquire) & fresh) != 0)
            front = middle.exchange (front, std::memory_order_acq_rel) & indexMask;
        return slots[static_cast<std::size_t> (front)];
    }

private:
    static constexpr int fresh = 4, indexMask = 3;
    std::array<T, 3> slots {};
    std::atomic<int> middle { 1 };
    int front = 0, back = 2;
};

} // namespace osp::usermacro
