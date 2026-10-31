#include "engine/KeyDna.h"

#include "core/Prng.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace osp::keydna
{

namespace
{
    /** The individuality's own streams (Rule 2): a root derived from the layer's KEY DNA seed
        with a tag no other stream uses, so nothing else's randomness moves when a profile is
        computed, and the profile never depends on what else was played. */
    constexpr std::uint64_t streamTag = 0x6b6579646e61ull;   // "keydna"
    constexpr std::uint64_t smoothStream = 0x01, individualStream = 0x100;
    constexpr int lattice = 12;   ///< the smooth field's random knots: one per octave
}

const char* paramName (Param p) noexcept
{
    switch (p)
    {
        case Param::position: return "POS";
        case Param::size: return "SIZE";
        case Param::density: return "DENS";
        case Param::spread: return "SPREAD";
        case Param::tune: return "TUNE";
        case Param::scatter: return "SCATTER";
        case Param::pan: return "PAN";
    }
    return "";
}

const char* paramId (Param p) noexcept
{
    switch (p)
    {
        case Param::position: return "position";
        case Param::size: return "size";
        case Param::density: return "density";
        case Param::spread: return "spread";
        case Param::tune: return "tune";
        case Param::scatter: return "scatter";
        case Param::pan: return "pan";
    }
    return "";
}

double halfRange (Param p) noexcept
{
    // The modulation system's domains for the same knobs (Modulation.cpp: grainSizeOctaves,
    // densityOctaves), so a full profile reaches as far as a full route would.
    switch (p)
    {
        case Param::position: return 0.5;
        case Param::size: return 0.5 * std::log2 (0.4 / 0.02);
        case Param::density: return 0.5 * std::log2 (40.0 / 4.0);
        case Param::spread: return 0.5;
        case Param::tune: return 12.0;
        case Param::scatter: return 6.0;
        case Param::pan: return 0.5;
    }
    return 0.0;
}

mod::CurveTable flatCurve() noexcept
{
    mod::CurveTable t;
    t.fill (0.5f);
    return t;
}

mod::CurveTable curveFrom (const mod::Curve& curve) noexcept
{
    return mod::compileCurve (curve);
}

Settings::Settings()
{
    for (auto& p : params)
        p.curve = flatCurve();
}

bool Settings::isNeutral() const noexcept
{
    if (! enabled || ! (amount > 0.0))
        return true;   // (with AMOUNT 0 the profile exists but is not heard)
    for (const auto& p : params)
    {
        if (p.curveDepth != 0.0 || p.variation != 0.0)
            return false;
        for (const float v : p.drawn)
            if (v <= 1.0f && v != 0.0f)
                return false;
    }
    return true;
}

void Table::compute (const Settings& s) noexcept
{
    // Individuality blends a smooth field (random values every octave, cosine-interpolated:
    // neighbours nearly equal) with independent per-key values. Neighbour correlation falls
    // monotonically from about 0.97 (Smooth) to 0 (Individual), so the control is predictable;
    // the blend is renormalised so the spread of values is the same at every setting.
    const double ind = std::clamp (s.individuality, 0.0, 1.0);
    const double a = 1.0 - ind, b = ind;
    const double norm = 1.0 / std::sqrt (a * a + b * b);
    // AMOUNT is applied live (keydna::apply); off, the profile is empty.
    const double amount = s.enabled ? 1.0 : 0.0;
    const auto root = Prng::deriveSeed (s.seed, streamTag, 0);
    anyActive = false;
    for (auto& k : keys)
        k = {};
    for (int p = 0; p < paramCount; ++p)
    {
        const auto& ps = s.params[static_cast<std::size_t> (p)];
        const double depth = std::clamp (ps.curveDepth, -1.0, 1.0);
        const double variation = std::clamp (ps.variation, 0.0, 1.0);
        const double range = halfRange (static_cast<Param> (p));
        std::array<double, 128 / lattice + 2> knots {};
        for (std::size_t j = 0; j < knots.size(); ++j)
            knots[j] = Prng (Prng::deriveSeed (root, smoothStream + static_cast<std::uint64_t> (p), j)).bipolar();
        for (int key = 0; key < 128; ++key)
        {
            const double x = static_cast<double> (key) / lattice;
            const auto j = static_cast<std::size_t> (x);
            const double f = x - static_cast<double> (j);
            const double w = 0.5 - 0.5 * std::cos (std::numbers::pi * f);
            const double smooth = knots[j] + (knots[j + 1] - knots[j]) * w;
            const double white = Prng (Prng::deriveSeed (root, individualStream + static_cast<std::uint64_t> (p), static_cast<std::uint64_t> (key))).bipolar();
            const double individual = std::clamp ((a * smooth + b * white) * norm, -1.0, 1.0);
            const double curve = 2.0 * mod::curveAt (ps.curve, static_cast<double> (key) / 127.0) - 1.0;
            // A flat curve (0.5) or a zero depth / variation gives exactly 0: no offset at all.
            double unit = 0.0;
            if (depth != 0.0)
                unit += depth * curve;
            if (variation != 0.0)
                unit += variation * individual;
            // A key drawn by hand plays what was drawn (its own value, the curve and the
            // variation set aside there).
            if (const float drawn = ps.drawn[static_cast<std::size_t> (key)]; drawn <= 1.0f)
                unit = static_cast<double> (drawn);
            unit = std::clamp (unit, -1.0, 1.0) * amount;   // x 1 exactly when on
            const auto i = static_cast<std::size_t> (key) * paramCount + static_cast<std::size_t> (p);
            variations[i] = static_cast<float> (individual);
            units[i] = static_cast<float> (unit);
            if (unit == 0.0)
                continue;
            auto& k = keys[static_cast<std::size_t> (key)];
            const double value = unit * range;
            switch (static_cast<Param> (p))
            {
                case Param::position: k.position = value; break;
                case Param::size: k.sizeOctaves = value; break;
                case Param::density: k.densityOctaves = value; break;
                case Param::spread: k.spread = value; break;
                case Param::tune: k.tuneSemitones = value; break;
                case Param::scatter: k.scatterSemitones = value; break;
                case Param::pan: k.pan = value; break;
            }
            k.active = true;
            anyActive = true;
        }
    }
}

double Table::neighbourCorrelation (Param p) const noexcept
{
    const auto col = static_cast<std::size_t> (p);
    double mean = 0.0;
    for (std::size_t k = 0; k < 128; ++k)
        mean += variations[k * paramCount + col];
    mean /= 128.0;
    double num = 0.0, den = 0.0;
    for (std::size_t k = 0; k < 128; ++k)
    {
        const double d = variations[k * paramCount + col] - mean;
        den += d * d;
        if (k + 1 < 128)
            num += d * (variations[(k + 1) * paramCount + col] - mean);
    }
    return den > 0.0 ? num / den : 0.0;
}

} // namespace osp::keydna
