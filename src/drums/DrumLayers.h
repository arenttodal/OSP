#pragma once

#include <array>
#include <cstdint>
#include <string>

/**
    ANDOR Drums: several samples on one pad (pure C++, no JUCE, real-time safe).

    A pad holds 1..maxLayers sample layers. Its LayerMode says how a hit uses them:

      ROUND ROBIN  one layer per hit, chosen by the order (sequential, random, shuffle bag)
                   among the layers whose velocity range holds the hit
      VELOCITY     one layer per hit: the layer whose velocity range holds the hit (several
                   in range alternate by the order, as round robin)
      STACK        every layer in range sounds together

    Each layer has its own level, START / END and velocity range (non-destructive: the
    recording is never edited). LayerPicker makes the choice on the audio thread; it is
    reproducible from the kit's seed and the pad's hit count (CLAUDE.md rule 2).
*/
namespace osp::drums
{

constexpr int maxLayers = 16;

enum class LayerMode
{
    roundRobin,
    velocity,
    stack
};

enum class RoundRobinOrder
{
    sequential,   ///< 1, 2, 3, 1, 2, 3 ...
    random,       ///< any layer, never the same one twice in a row
    shuffle       ///< a shuffled bag: every layer once before any repeats (and no repeat across bags)
};

const char* toString (LayerMode mode) noexcept;
const char* toString (RoundRobinOrder order) noexcept;
LayerMode layerModeFromString (const std::string& text, LayerMode fallback = LayerMode::roundRobin) noexcept;
RoundRobinOrder roundRobinOrderFromString (const std::string& text, RoundRobinOrder fallback = RoundRobinOrder::shuffle) noexcept;

/** One layer's own settings. */
struct LayerParams
{
    double gainDb = 0.0;      ///< -60 (silent) .. +12
    double start = 0.0;       ///< 0..1 of the recording after its onset
    double end = 1.0;         ///< 0..1, above start (1 = to the end)
    int velocityLow = 1;      ///< 1..127, inclusive
    int velocityHigh = 127;

    bool operator== (const LayerParams&) const = default;
    bool covers (int velocity) const noexcept { return velocity >= velocityLow && velocity <= velocityHigh; }
};

/** How a pad uses its layers (the layers' sounds live elsewhere: index i is sound i). */
struct LayerPlan
{
    LayerMode mode = LayerMode::roundRobin;
    RoundRobinOrder order = RoundRobinOrder::shuffle;
    int count = 0;                                   ///< layers in use, 0..maxLayers
    std::array<LayerParams, maxLayers> layer {};

    bool operator== (const LayerPlan&) const = default;
};

/** Splits velocity 1..127 into `count` equal adjacent ranges over layers first..first+count-1
    (softest first). */
void spreadVelocityRanges (LayerPlan& plan, int first, int count) noexcept;

/** Full velocity range for layers first..first+count-1. */
void fullVelocityRanges (LayerPlan& plan, int first, int count) noexcept;

/** Chooses which layers a hit plays (one per pad; audio thread). */
class LayerPicker
{
public:
    void reset() noexcept;

    /** Writes the chosen layer indices to `out` and returns how many (0 only for an empty
        plan). `hitIndex` must grow with every hit of the pad (determinism). */
    int pick (const LayerPlan& plan, int velocity, std::uint64_t seed, std::uint64_t hitIndex,
              std::array<int, maxLayers>& out) noexcept;

    int lastPicked() const noexcept { return last; }

private:
    int last = -1;
    std::uint32_t bag = 0;   ///< shuffle: layers already played in this bag
};

} // namespace osp::drums
