#pragma once

#include <vector>

namespace osp
{

/**
    Bandlimited fractional-position reader (Kaiser-windowed sinc).

    This is pitch branch "A — resampling": reading a source at an arbitrary rate.
    When the read increment (source samples per output sample) exceeds 1 the kernel
    is stretched so its cutoff follows the new Nyquist limit, which suppresses
    aliasing when transposing up or downsampling.

    The kernel table is built once in the constructor (off the audio thread). The
    read functions perform no allocation; they expect the source to be padded with
    at least maxReach() zeros on both sides so no bounds checks are needed.
*/
class SincInterpolator
{
public:
    /**
        @param zeroCrossings  half-width of the kernel in zero crossings at unity rate
                              (quality: 8 = draft, 16 = high).
        @param maxStretch     largest increment for which the kernel is fully stretched.
                              Larger increments keep the cutoff but reduce the window
                              width proportionally (graceful degradation, bounded cost).
    */
    explicit SincInterpolator (int zeroCrossings = 16, double maxStretch = 16.0);

    int zeroCrossings() const noexcept { return numZeroCrossings; }

    /** maxReach() of an interpolator constructed with these arguments. */
    static int maxReachFor (int zeroCrossings, double maxStretch = 16.0) noexcept;

    /** Furthest any read reaches away from its read position, in source samples. */
    int maxReach() const noexcept { return reach; }

    /** How far a read at this increment reaches (<= maxReach()). */
    int reachFor (double increment) const noexcept;

    /** Per-read kernel state, computed once per output sample and shared by all channels. */
    struct Kernel
    {
        static constexpr int maxTaps = 1100;
        float weights[maxTaps];
        int firstIndex = 0;
        int numTaps = 0;
    };

    /** Computes kernel weights for reading at `position` with `increment` source samples per output sample. */
    void computeKernel (double position, double increment, Kernel& kernel) const noexcept;

    /**
        Fast path for increment <= 1 (no stretch): the kernel then depends only on the
        fractional position, so it is interpolated from a precomputed polyphase table
        (2048 phases) instead of being evaluated tap by tap. Equivalent to
        computeKernel (position, 1.0, ...) within table precision. Used by engine C; the
        baselines keep computeKernel so their golden renders never change.
    */
    void computeKernelUnity (double position, Kernel& kernel) const noexcept;

    /**
        Precomputes polyphase tables for stretched kernels (increments 1..maxTableStretch on
        a semitone grid). Allocates; call off the audio thread. Engine C only.
    */
    void prepareStretchTables();

    /**
        Any increment, as cheaply as possible: computeKernelUnity for increments <= 1, the
        stretch tables (if prepared) up to 4, otherwise computeKernel. Table kernels use the
        next stretch up on the semitone grid, so the cutoff is at most 6 % lower than
        computeKernel's (never higher: no extra aliasing).
    */
    void computeKernelFast (double position, double increment, Kernel& kernel) const noexcept;

    static constexpr double maxTableStretch = 4.0;

    /** Applies a computed kernel to one (padded) channel. `data` points at source sample 0. */
    static float apply (const Kernel& kernel, const float* data) noexcept
    {
        const float* p = data + kernel.firstIndex;
        float sum = 0.0f;
        for (int i = 0; i < kernel.numTaps; ++i)
            sum += kernel.weights[i] * p[i];
        return sum;
    }

    /** Cutoff relative to the (lower of source/output) Nyquist frequency. */
    static constexpr double rolloff = 0.96;

private:
    static float lookup (const std::vector<float>& table, double index) noexcept;

    int numZeroCrossings;
    double maxStretchFactor;
    int reach;
    static constexpr int sincResolution = 512;    // sinc table entries per zero crossing
    static constexpr int windowResolution = 4096; // window table entries over [0, 1]
    std::vector<float> sincTable;                 // sinc(u), u in [0, zeroCrossings]
    std::vector<float> windowTable;               // kaiser(r), r in [0, 1]
    static constexpr int polyphaseResolution = 2048;
    std::vector<float> polyphase;                 // (resolution + 1) rows x (2 * zeroCrossings) taps

    static constexpr int stretchSteps = 24;       // semitone levels above 1 (up to x4)
    static constexpr int stretchResolution = 1024;
    struct StretchTable
    {
        int reach = 0;                            // taps = 2 * reach
        std::vector<float> rows;                  // (stretchResolution + 1) rows
    };
    std::vector<StretchTable> stretchTables;      // index k: stretch 2^(k / 12), k = 1..stretchSteps
};

} // namespace osp
