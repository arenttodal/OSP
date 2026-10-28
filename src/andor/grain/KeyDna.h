#pragma once

#include <array>
#include <cstdint>

namespace osp::grain
{

/**
    KEY DNA (docs/andor-lab/grain.md): how ANDOR Grain's synthesis varies across the keyboard.
    Each MIDI key gets a reproducible profile made of three things kept strictly apart:

      1 a drawn curve per parameter (pitch-dependent character: longer grains low, ...)
      2 a stable per-key individuality from the seed (C4 always has *its* offset)
      3 nothing random per trigger or per grain: those live in the voice and the scheduler

    Profiles are offsets in each parameter's normalised domain (0..1 knob travel), added to
    the base value. Everything here is fixed-size: computing the 128 x 7 table allocates
    nothing, so the audio thread may recompute it when a seed or amount changes.
*/
enum class DnaParam
{
    size,
    density,
    position,
    positionScatter,
    pitchScatter,
    tone,
    spread,
};
constexpr int numDnaParams = 7;
const char* dnaParamName (DnaParam) noexcept;   ///< "SIZE", "DENSITY", ...
const char* dnaParamId (DnaParam) noexcept;     ///< "size", "density", ... (state)

/** A curve over the keyboard: up to 16 points (key 0..127, value -1..1), linear between them. */
struct DnaCurve
{
    static constexpr int maxPoints = 16;
    struct Point
    {
        float key = 0.0f;
        float value = 0.0f;
    };
    std::array<Point, maxPoints> points {};
    int count = 0;   ///< 0 = flat at 0

    /** Value at a key (flat outside the first / last point). */
    float evaluate (float key) const noexcept;
    /** Adds a point keeping keys sorted (replaces one within half a key). False when full. */
    bool addPoint (float key, float value) noexcept;
    void removePoint (int index) noexcept;
    void clear() noexcept { count = 0; }
    bool isFlat() const noexcept;
    bool operator== (const DnaCurve& other) const noexcept;
};

struct KeyDnaSettings
{
    std::array<DnaCurve, numDnaParams> curves {};
    double curveAmount = 1.0;        ///< scales every curve, 0..1
    double variationAmount = 0.0;    ///< scales the per-key individuality, 0..1
    double individuality = 0.3;      ///< Smooth (0: neighbours closely related) <-> Individual (1)
    std::uint64_t seed = 1;
};

/**
    The cached 128 x 7 profile. `offset (key, p)` is curveAmount * curve + variationAmount *
    variation, both in normalised units (1.0 = the whole knob travel; the engine applies half
    of it each way, see GrainEngine).
*/
class KeyDnaTable
{
public:
    /** Recomputes everything. Real-time safe (no allocation); about 1000 PRNG draws. */
    void compute (const KeyDnaSettings& settings) noexcept;

    float curve (int key, DnaParam p) const noexcept { return curves[index (key, p)]; }
    /** The stable individuality, unit scale (-1..1), before variationAmount. */
    float variation (int key, DnaParam p) const noexcept { return variations[index (key, p)]; }
    float offset (int key, DnaParam p) const noexcept { return offsets[index (key, p)]; }

    /** The correlation between neighbouring keys' variation of one parameter (tests, display). */
    double neighbourCorrelation (DnaParam p) const noexcept;

private:
    static std::size_t index (int key, DnaParam p) noexcept
    {
        return static_cast<std::size_t> ((key < 0 ? 0 : key > 127 ? 127 : key) * numDnaParams + static_cast<int> (p));
    }
    std::array<float, 128 * numDnaParams> curves {}, variations {}, offsets {};
};

} // namespace osp::grain
