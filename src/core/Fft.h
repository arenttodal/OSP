#pragma once

#include <complex>
#include <vector>

namespace osp
{

/**
    Minimal radix-2 complex FFT in double precision.

    Used by offline analysis only. Tables are built in the constructor; transform()
    performs no allocation. Kept dependency-free so analysis can run in any context
    (tests, renderer, plugin worker thread) without JUCE.
*/
class Fft
{
public:
    explicit Fft (int order);

    int size() const noexcept { return n; }
    int order() const noexcept { return log2n; }

    /** In-place forward transform (e^{-i...}). data must have size() elements. */
    void forward (std::complex<double>* data) const noexcept { transform (data, false); }

    /** In-place inverse transform, scaled by 1/N. */
    void inverse (std::complex<double>* data) const noexcept;

    /** Smallest order such that (1 << order) >= minimumSize. */
    static int orderForSize (int minimumSize) noexcept;

private:
    void transform (std::complex<double>* data, bool inverse) const noexcept;

    int log2n = 0;
    int n = 1;
    std::vector<int> bitReversed;
    std::vector<std::complex<double>> twiddles;
};

} // namespace osp
