#include "engine/ArpCustomPattern.h"

#include <algorithm>

namespace osp::arp
{

const char* customMappingName (CustomMapping mapping) noexcept
{
    switch (mapping)
    {
        case CustomMapping::contour: return "CONTOUR";
        case CustomMapping::degrees: return "DEGREES";
    }
    return "CONTOUR";
}

int CustomPattern::lengthTicks() const noexcept
{
    long long total = 0;
    for (int i = 0; i < std::clamp (count, 0, maxPatternEvents); ++i)
        total += std::max<std::int32_t> (0, events[static_cast<std::size_t> (i)].duration);
    return static_cast<int> (std::min<long long> (total, static_cast<long long> (maxPatternSteps) * ticksPerStep + 1));
}

bool CustomPattern::valid() const noexcept
{
    if (count < 1 || count > maxPatternEvents || maxRank < 0 || maxRank > maxPatternRank)
        return false;
    if (static_cast<int> (mapping) < 0 || static_cast<int> (mapping) >= customMappingCount)
        return false;
    long long total = 0;
    bool anyNote = false;
    for (int i = 0; i < count; ++i)
    {
        const auto& e = events[static_cast<std::size_t> (i)];
        if (e.duration < minEventTicks)
            return false;
        if (! e.rest && (e.rank < 0 || e.rank > maxRank))
            return false;
        anyNote = anyNote || ! e.rest;
        total += e.duration;
    }
    return anyNote && total <= static_cast<long long> (maxPatternSteps) * ticksPerStep;
}

bool CustomPattern::operator== (const CustomPattern& other) const noexcept
{
    return count == other.count && maxRank == other.maxRank && mapping == other.mapping
        && std::equal (events.begin(), events.begin() + std::clamp (count, 0, maxPatternEvents), other.events.begin());
}

int contourIndex (int r, int R, int N) noexcept
{
    if (N <= 0)
        return -1;
    r = std::max (0, r);
    if (N > R)
        return std::min (r, N - 1);
    // The shape compressed onto the pool: round (rank / R * (N - 1)), half up, in integers.
    const int rank = std::min (r, R);
    if (R <= 0 || N == 1)
        return 0;
    const long long num = 2LL * rank * (N - 1) + R;
    return static_cast<int> (std::min<long long> (N - 1, num / (2LL * R)));
}

namespace
{
    /** The pool over `octaves` octaves in the arpeggiator's order (each octave's held notes
        ascending, then the next octave), notes above 127 left out. */
    int poolOf (const PoolNote* held, int count, int octaves, std::array<PoolNote, CustomPool::capacity>& out) noexcept
    {
        int n = 0;
        for (int o = 0; o < octaves; ++o)
            for (int i = 0; i < count && n < CustomPool::capacity; ++i)
                if (const int note = held[i].note + 12 * o; note <= 127)
                    out[static_cast<std::size_t> (n++)] = { static_cast<std::uint8_t> (note), held[i].velocity, held[i].channel };
        return n;
    }

    /** Ascending, one entry per pitch (the first found keeps its velocity). An insertion sort:
        stable, bounded, no allocation. */
    int distinctOf (std::array<PoolNote, CustomPool::capacity>& notes, int n) noexcept
    {
        for (int i = 1; i < n; ++i)
        {
            const auto moving = notes[static_cast<std::size_t> (i)];
            int j = i;
            for (; j > 0 && notes[static_cast<std::size_t> (j - 1)].note > moving.note; --j)
                notes[static_cast<std::size_t> (j)] = notes[static_cast<std::size_t> (j - 1)];
            notes[static_cast<std::size_t> (j)] = moving;
        }
        int kept = 0;
        for (int i = 0; i < n; ++i)
            if (kept == 0 || notes[static_cast<std::size_t> (kept - 1)].note != notes[static_cast<std::size_t> (i)].note)
                notes[static_cast<std::size_t> (kept++)] = notes[static_cast<std::size_t> (i)];
        return kept;
    }

    bool contains (const std::array<PoolNote, CustomPool::capacity>& sorted, int n, int note) noexcept
    {
        const auto end = sorted.begin() + n;
        const auto it = std::lower_bound (sorted.begin(), end, note, [] (const PoolNote& p, int v) { return p.note < v; });
        return it != end && it->note == note;
    }
}

void planCustomPool (const PoolNote* held, int count, int octaves, int maxRank, CustomPool& out) noexcept
{
    count = std::clamp (count, 0, 64);
    octaves = std::clamp (octaves, 1, 4);
    maxRank = std::clamp (maxRank, 0, maxPatternRank);
    out.degreeCount = poolOf (held, count, octaves, out.degree);
    out.baseCount = 0;
    out.positionCount = 0;
    out.baseOctaves = octaves;
    out.compressed = false;
    if (out.degreeCount == 0)
        return;

    std::array<PoolNote, CustomPool::capacity> full;
    const int fullCount = distinctOf (full = out.degree, out.degreeCount);

    // The fewest octaves whose distinct notes hold every rank.
    const int needed = maxRank + 1;
    for (int b = 1; b <= octaves; ++b)
    {
        out.baseCount = distinctOf (out.base, poolOf (held, count, b, out.base));
        out.baseOctaves = b;
        if (out.baseCount >= needed)
            break;
    }
    out.compressed = out.baseCount < needed;

    // Every octave shift (contiguous from the base upwards) at which each note the gesture uses
    // is a note of the full pool.
    for (int shift = 0; shift <= 36; shift += 12)
    {
        bool fits = true;
        for (int r = 0; r <= maxRank && fits; ++r)
            fits = contains (full, fullCount, out.base[static_cast<std::size_t> (contourIndex (r, maxRank, out.baseCount))].note + shift);
        if (! fits)
            break;
        out.positions[static_cast<std::size_t> (out.positionCount++)] = static_cast<std::int8_t> (shift);
    }
    if (out.positionCount == 0)
        out.positions[static_cast<std::size_t> (out.positionCount++)] = 0;
}

PoolNote customNote (const CustomPool& pool, CustomMapping mapping, int rank, int maxRank, std::int64_t cycle) noexcept
{
    PoolNote none;
    none.note = 255;
    rank = std::max (0, rank);
    cycle = std::max<std::int64_t> (0, cycle);
    if (mapping == CustomMapping::degrees)
    {
        if (pool.degreeCount <= 0)
            return none;
        return pool.degree[static_cast<std::size_t> (rank % pool.degreeCount)];
    }
    if (pool.baseCount <= 0 || pool.positionCount <= 0)
        return none;
    auto n = pool.base[static_cast<std::size_t> (contourIndex (rank, maxRank, pool.baseCount))];
    n.note = static_cast<std::uint8_t> (n.note + pool.positions[static_cast<std::size_t> (cycle % pool.positionCount)]);
    return n;
}

} // namespace osp::arp

namespace osp::arp
{

void CustomPatternExchange::publish (int card, const CustomPattern* pattern) noexcept
{
    if (card < 0 || card >= cards)
        return;
    auto& slot = slots[static_cast<std::size_t> (card)];
    auto& b = slot.buffers[static_cast<std::size_t> (slot.writeIndex)];
    b.present = pattern != nullptr && pattern->valid();
    if (b.present)
        b.pattern = *pattern;
    slot.published = b;
    slot.writeIndex = slot.middle.exchange (slot.writeIndex | dirty, std::memory_order_acq_rel) & indexMask;
}

const CustomPattern* CustomPatternExchange::current (int card) const noexcept
{
    if (card < 0 || card >= cards)
        return nullptr;
    const auto& b = slots[static_cast<std::size_t> (card)].published;
    return b.present ? &b.pattern : nullptr;
}

} // namespace osp::arp
