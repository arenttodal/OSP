#pragma once

#include <cmath>
#include <cstdint>

namespace osp
{

/**
    Deterministic pseudo random number generator (xoshiro256**, seeded via SplitMix64).

    Every stochastic process in the engine must draw from an instance of this class
    seeded from stored state (instrument seed + deterministic event counters). Never
    use std::random_device, std::rand or the std:: distributions: their outputs are
    not specified identically across standard libraries.

    Real-time safe: no allocation, no locks, trivially copyable.
*/
class Prng
{
public:
    explicit Prng (std::uint64_t seed = 0) noexcept { reseed (seed); }

    void reseed (std::uint64_t seed) noexcept
    {
        std::uint64_t s = seed;
        for (auto& word : state)
            word = splitMix64 (s);
    }

    std::uint64_t nextU64() noexcept
    {
        const auto result = rotl (state[1] * 5, 7) * 9;
        const auto t = state[1] << 17;
        state[2] ^= state[0];
        state[3] ^= state[1];
        state[1] ^= state[2];
        state[0] ^= state[3];
        state[2] ^= t;
        state[3] = rotl (state[3], 45);
        return result;
    }

    /** Uniform in [0, 1) with 53 bits of resolution. */
    double nextDouble() noexcept { return static_cast<double> (nextU64() >> 11) * 0x1.0p-53; }

    /** Uniform in [lo, hi). */
    double uniform (double lo, double hi) noexcept { return lo + (hi - lo) * nextDouble(); }

    /** Uniform in [-1, 1). */
    double bipolar() noexcept { return 2.0 * nextDouble() - 1.0; }

    /** Uniform integer in [0, n). n must be > 0. */
    std::uint64_t nextBelow (std::uint64_t n) noexcept { return nextU64() % n; }

    /** Standard normal deviate (Box-Muller, no cached second value so state stays simple). */
    double gaussian() noexcept
    {
        const double u1 = 1.0 - nextDouble(); // (0, 1]
        const double u2 = nextDouble();
        return std::sqrt (-2.0 * std::log (u1)) * std::cos (6.283185307179586 * u2);
    }

    /** Derives a child seed from a base seed and event identifiers (e.g. note, counter). */
    static std::uint64_t deriveSeed (std::uint64_t base, std::uint64_t a, std::uint64_t b = 0) noexcept
    {
        std::uint64_t x = base ^ 0x9E3779B97F4A7C15ull;
        x = splitMix64 (x) ^ a;
        x = splitMix64 (x) ^ b;
        return splitMix64 (x);
    }

    static std::uint64_t splitMix64 (std::uint64_t& x) noexcept
    {
        std::uint64_t z = (x += 0x9E3779B97F4A7C15ull);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return z ^ (z >> 31);
    }

private:
    static std::uint64_t rotl (std::uint64_t x, int k) noexcept { return (x << k) | (x >> (64 - k)); }

    std::uint64_t state[4] {};
};

} // namespace osp
