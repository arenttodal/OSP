#include "core/Fft.h"

#include <cmath>
#include <numbers>
#include <utility>

namespace osp
{

Fft::Fft (int order) : log2n (order), n (1 << order)
{
    bitReversed.resize (static_cast<std::size_t> (n));

    for (int i = 0; i < n; ++i)
    {
        int r = 0;
        for (int b = 0; b < log2n; ++b)
            if (i & (1 << b))
                r |= 1 << (log2n - 1 - b);
        bitReversed[static_cast<std::size_t> (i)] = r;
    }

    twiddles.resize (static_cast<std::size_t> (n / 2 > 0 ? n / 2 : 1));
    for (int k = 0; k < n / 2; ++k)
    {
        const double angle = -2.0 * std::numbers::pi * k / n;
        twiddles[static_cast<std::size_t> (k)] = { std::cos (angle), std::sin (angle) };
    }
}

int Fft::orderForSize (int minimumSize) noexcept
{
    int order = 0;
    while ((1 << order) < minimumSize)
        ++order;
    return order;
}

void Fft::inverse (std::complex<double>* data) const noexcept
{
    transform (data, true);
    const double scale = 1.0 / n;
    for (int i = 0; i < n; ++i)
        data[i] *= scale;
}

void Fft::transform (std::complex<double>* data, bool inverseTransform) const noexcept
{
    for (int i = 0; i < n; ++i)
    {
        const int j = bitReversed[static_cast<std::size_t> (i)];
        if (j > i)
            std::swap (data[i], data[j]);
    }

    for (int len = 2; len <= n; len <<= 1)
    {
        const int half = len / 2;
        const int step = n / len;

        for (int start = 0; start < n; start += len)
        {
            for (int k = 0; k < half; ++k)
            {
                auto w = twiddles[static_cast<std::size_t> (k * step)];
                if (inverseTransform)
                    w = std::conj (w);

                const auto a = data[start + k];
                const auto b = data[start + k + half] * w;
                data[start + k] = a + b;
                data[start + k + half] = a - b;
            }
        }
    }
}

} // namespace osp
