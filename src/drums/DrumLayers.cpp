#include "drums/DrumLayers.h"

#include "core/Prng.h"

#include <algorithm>
#include <cstdlib>

namespace osp::drums
{

namespace
{
    constexpr std::uint64_t pickSalt = 0x4c41594552ull;   // "LAYER"
}

const char* toString (LayerMode mode) noexcept
{
    switch (mode)
    {
        case LayerMode::velocity: return "velocity";
        case LayerMode::stack: return "stack";
        case LayerMode::roundRobin: break;
    }
    return "roundRobin";
}

const char* toString (RoundRobinOrder order) noexcept
{
    switch (order)
    {
        case RoundRobinOrder::sequential: return "sequential";
        case RoundRobinOrder::random: return "random";
        case RoundRobinOrder::shuffle: break;
    }
    return "shuffle";
}

LayerMode layerModeFromString (const std::string& text, LayerMode fallback) noexcept
{
    for (auto m : { LayerMode::roundRobin, LayerMode::velocity, LayerMode::stack })
        if (text == toString (m))
            return m;
    return fallback;
}

RoundRobinOrder roundRobinOrderFromString (const std::string& text, RoundRobinOrder fallback) noexcept
{
    for (auto o : { RoundRobinOrder::sequential, RoundRobinOrder::random, RoundRobinOrder::shuffle })
        if (text == toString (o))
            return o;
    return fallback;
}

void spreadVelocityRanges (LayerPlan& plan, int first, int count) noexcept
{
    first = std::clamp (first, 0, maxLayers);
    count = std::clamp (count, 0, maxLayers - first);
    for (int i = 0; i < count; ++i)
    {
        auto& p = plan.layer[static_cast<std::size_t> (first + i)];
        p.velocityLow = 1 + 127 * i / count;
        p.velocityHigh = 127 * (i + 1) / count;
    }
}

void fullVelocityRanges (LayerPlan& plan, int first, int count) noexcept
{
    first = std::clamp (first, 0, maxLayers);
    count = std::clamp (count, 0, maxLayers - first);
    for (int i = 0; i < count; ++i)
    {
        plan.layer[static_cast<std::size_t> (first + i)].velocityLow = 1;
        plan.layer[static_cast<std::size_t> (first + i)].velocityHigh = 127;
    }
}

void LayerPicker::reset() noexcept
{
    last = -1;
    bag = 0;
}

int LayerPicker::pick (const LayerPlan& plan, int velocity, std::uint64_t seed, std::uint64_t hitIndex,
                       std::array<int, maxLayers>& out) noexcept
{
    const int count = std::clamp (plan.count, 0, maxLayers);
    if (count == 0)
        return 0;
    velocity = std::clamp (velocity, 1, 127);

    // The layers whose velocity range holds the hit; none: the nearest range (a hit always sounds).
    std::array<int, maxLayers> candidates {};
    int n = 0;
    for (int i = 0; i < count; ++i)
        if (plan.layer[static_cast<std::size_t> (i)].covers (velocity))
            candidates[static_cast<std::size_t> (n++)] = i;
    if (n == 0)
    {
        int best = 0, bestDistance = 1000;
        for (int i = 0; i < count; ++i)
        {
            const auto& p = plan.layer[static_cast<std::size_t> (i)];
            const int d = velocity < p.velocityLow ? p.velocityLow - velocity : velocity - p.velocityHigh;
            if (d < bestDistance)
            {
                best = i;
                bestDistance = d;
            }
        }
        candidates[0] = best;
        n = 1;
    }

    if (plan.mode == LayerMode::stack)
    {
        std::copy_n (candidates.begin(), n, out.begin());
        return n;
    }

    int chosen = candidates[0];
    if (n > 1)
    {
        Prng rng (Prng::deriveSeed (seed, hitIndex, pickSalt));
        switch (plan.order)
        {
            case RoundRobinOrder::sequential:
            {
                // The first candidate after the last one played (in layer order, wrapping).
                chosen = candidates[0];
                for (int i = 0; i < n; ++i)
                    if (candidates[static_cast<std::size_t> (i)] > last)
                    {
                        chosen = candidates[static_cast<std::size_t> (i)];
                        break;
                    }
                break;
            }
            case RoundRobinOrder::random:
            {
                int k = static_cast<int> (rng.nextBelow (static_cast<std::uint64_t> (n)));
                if (candidates[static_cast<std::size_t> (k)] == last)
                    k = (k + 1 + static_cast<int> (rng.nextBelow (static_cast<std::uint64_t> (n - 1)))) % n;
                chosen = candidates[static_cast<std::size_t> (k)];
                break;
            }
            case RoundRobinOrder::shuffle:
            {
                std::array<int, maxLayers> open {};
                int m = 0;
                for (int i = 0; i < n; ++i)
                    if ((bag & (1u << candidates[static_cast<std::size_t> (i)])) == 0)
                        open[static_cast<std::size_t> (m++)] = candidates[static_cast<std::size_t> (i)];
                if (m == 0)
                {
                    // A new bag: everything again, except the layer just played.
                    bag = 0;
                    for (int i = 0; i < n; ++i)
                        if (candidates[static_cast<std::size_t> (i)] != last)
                            open[static_cast<std::size_t> (m++)] = candidates[static_cast<std::size_t> (i)];
                }
                chosen = open[static_cast<std::size_t> (rng.nextBelow (static_cast<std::uint64_t> (m)))];
                bag |= 1u << chosen;
                break;
            }
        }
    }
    last = chosen;
    out[0] = chosen;
    return 1;
}

} // namespace osp::drums
