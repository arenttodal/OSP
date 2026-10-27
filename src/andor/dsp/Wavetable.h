#pragma once

#include <array>
#include <vector>

namespace osp::andor
{

/**
    Band-limited periodic sources (docs/andor-lab/grain.md "Oscillator"): additive tables per
    octave ("mipmaps"), each holding only the harmonics that stay below 0.45 x the sample rate
    for every fundamental of its octave, so nothing aliases. Reads cross-fade between two
    neighbouring octaves (no timbre step when the pitch glides), and between two neighbouring
    shapes (a continuous SHAPE control). prepare() allocates; reads are real-time safe.
*/
class WavetableBank
{
public:
    enum Shape { sine, triangle, saw, square, numShapes };
    static constexpr int tableSize = 2048;
    static constexpr int numLevels = 11;          ///< level L serves fundamentals below 20 Hz * 2^(L+1)
    static constexpr double baseHz = 20.0;

    void prepare (double sampleRate);
    bool isPrepared() const noexcept { return ! tables.empty(); }
    double preparedRate() const noexcept { return rate; }

    /** Highest harmonic stored at a level (diagnostics, tests). */
    int harmonicsAt (int level) const noexcept { return harmonics[static_cast<std::size_t> (level)]; }

    /**
        One sample of `shapePosition` (0 sine .. 3 square, fractional = blend of neighbours) at
        `phase` (0..1) for a fundamental of `hz`.
    */
    float read (double shapePosition, double phase, double hz) const noexcept;

private:
    float lookup (int shape, int level, double phase) const noexcept;
    double rate = 0.0;
    std::array<int, numLevels> harmonics {};
    std::vector<float> tables;   // [shape][level][tableSize + 1]
};

/** Phase accumulator over a WavetableBank. */
class WavetableOscillator
{
public:
    void reset (double phase01 = 0.0) noexcept { phase = phase01; }
    float next (const WavetableBank& bank, double shapePosition, double hz, double sampleRate) noexcept
    {
        const float v = bank.read (shapePosition, phase, hz);
        phase += hz / sampleRate;
        phase -= static_cast<double> (static_cast<long long> (phase));
        return v;
    }

private:
    double phase = 0.0;
};

} // namespace osp::andor
