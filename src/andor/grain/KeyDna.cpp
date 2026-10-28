#include "andor/grain/KeyDna.h"

#include "core/Prng.h"

#include <algorithm>
#include <cmath>

namespace osp::grain
{

const char* dnaParamName (DnaParam p) noexcept
{
    switch (p)
    {
        case DnaParam::size: return "SIZE";
        case DnaParam::density: return "DENSITY";
        case DnaParam::position: return "POSITION";
        case DnaParam::positionScatter: return "SCATTER";
        case DnaParam::pitchScatter: return "PITCH SCATTER";
        case DnaParam::tone: return "TONE";
        case DnaParam::spread: return "SPREAD";
    }
    return "";
}

const char* dnaParamId (DnaParam p) noexcept
{
    switch (p)
    {
        case DnaParam::size: return "size";
        case DnaParam::density: return "density";
        case DnaParam::position: return "position";
        case DnaParam::positionScatter: return "positionScatter";
        case DnaParam::pitchScatter: return "pitchScatter";
        case DnaParam::tone: return "tone";
        case DnaParam::spread: return "spread";
    }
    return "";
}

float DnaCurve::evaluate (float key) const noexcept
{
    if (count <= 0)
        return 0.0f;
    if (key <= points[0].key)
        return points[0].value;
    for (int i = 1; i < count; ++i)
    {
        const auto& a = points[static_cast<std::size_t> (i - 1)];
        const auto& b = points[static_cast<std::size_t> (i)];
        if (key <= b.key)
        {
            const float span = b.key - a.key;
            return span > 0.0f ? a.value + (b.value - a.value) * (key - a.key) / span : b.value;
        }
    }
    return points[static_cast<std::size_t> (count - 1)].value;
}

bool DnaCurve::addPoint (float key, float value) noexcept
{
    key = std::clamp (key, 0.0f, 127.0f);
    value = std::clamp (value, -1.0f, 1.0f);
    for (int i = 0; i < count; ++i)
        if (std::abs (points[static_cast<std::size_t> (i)].key - key) < 0.5f)
        {
            points[static_cast<std::size_t> (i)].value = value;
            return true;
        }
    if (count >= maxPoints)
        return false;
    int at = count;
    while (at > 0 && points[static_cast<std::size_t> (at - 1)].key > key)
    {
        points[static_cast<std::size_t> (at)] = points[static_cast<std::size_t> (at - 1)];
        --at;
    }
    points[static_cast<std::size_t> (at)] = { key, value };
    ++count;
    return true;
}

void DnaCurve::removePoint (int index) noexcept
{
    if (index < 0 || index >= count)
        return;
    for (int i = index; i + 1 < count; ++i)
        points[static_cast<std::size_t> (i)] = points[static_cast<std::size_t> (i + 1)];
    --count;
}

bool DnaCurve::isFlat() const noexcept
{
    for (int i = 0; i < count; ++i)
        if (points[static_cast<std::size_t> (i)].value != 0.0f)
            return false;
    return true;
}

bool DnaCurve::operator== (const DnaCurve& other) const noexcept
{
    if (count != other.count)
        return false;
    for (int i = 0; i < count; ++i)
        if (points[static_cast<std::size_t> (i)].key != other.points[static_cast<std::size_t> (i)].key
            || points[static_cast<std::size_t> (i)].value != other.points[static_cast<std::size_t> (i)].value)
            return false;
    return true;
}

void KeyDnaTable::compute (const KeyDnaSettings& s) noexcept
{
    // Individuality blends a smooth field (random values every octave, cosine-interpolated:
    // neighbours nearly equal) with independent per-key values. Neighbour correlation falls
    // monotonically from about 0.97 (Smooth) to 0 (Individual), so the control is predictable.
    // The blend is renormalised so the spread of values stays the same at every setting.
    constexpr int lattice = 12;
    const double ind = std::clamp (s.individuality, 0.0, 1.0);
    const double a = 1.0 - ind, b = ind;
    const double norm = 1.0 / std::sqrt (a * a + b * b);
    for (int p = 0; p < numDnaParams; ++p)
    {
        std::array<double, 128 / lattice + 2> knots {};
        for (std::size_t j = 0; j < knots.size(); ++j)
        {
            Prng rng (Prng::deriveSeed (s.seed, static_cast<std::uint64_t> (p) + 1, j));
            knots[j] = rng.bipolar();
        }
        for (int key = 0; key < 128; ++key)
        {
            const double x = static_cast<double> (key) / lattice;
            const auto j = static_cast<std::size_t> (x);
            const double f = x - static_cast<double> (j);
            const double w = 0.5 - 0.5 * std::cos (3.141592653589793 * f);
            const double smooth = knots[j] + (knots[j + 1] - knots[j]) * w;
            Prng rng (Prng::deriveSeed (s.seed, 1000 + static_cast<std::uint64_t> (p), static_cast<std::uint64_t> (key)));
            const double white = rng.bipolar();
            const auto i = index (key, static_cast<DnaParam> (p));
            variations[i] = static_cast<float> (std::clamp ((a * smooth + b * white) * norm, -1.0, 1.0));
            curves[i] = s.curves[static_cast<std::size_t> (p)].evaluate (static_cast<float> (key));
            offsets[i] = static_cast<float> (std::clamp (s.curveAmount, 0.0, 1.0) * curves[i]
                                             + std::clamp (s.variationAmount, 0.0, 1.0) * variations[i]);
        }
    }
}

double KeyDnaTable::neighbourCorrelation (DnaParam p) const noexcept
{
    double mean = 0.0;
    for (int k = 0; k < 128; ++k)
        mean += variations[index (k, p)];
    mean /= 128.0;
    double num = 0.0, den = 0.0;
    for (int k = 0; k < 128; ++k)
    {
        const double d = variations[index (k, p)] - mean;
        den += d * d;
        if (k + 1 < 128)
            num += d * (variations[index (k + 1, p)] - mean);
    }
    return den > 0.0 ? num / den : 0.0;
}

} // namespace osp::grain
