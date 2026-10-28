#pragma once

#include "engine/GranularSource.h"
#include "engine/Modulation.h"
#include "engine/NoteShape.h"

#include <array>
#include <cstdint>

namespace osp::keydna
{

/**
    KEY DNA: how a layer's granular voice differs from key to key (docs/research/unified-andor,
    Phase 1; the mathematics are ANDOR Lab's KeyDna, on OSP's own parameters and curves).

    A key's profile is decided once, from three things kept strictly apart:
      1 a drawn curve per parameter over the keyboard (pitch-dependent character: longer
        grains low, another place in the recording high, ...);
      2 a stable individuality per key from the seed (C4 always has *its* offset), between
        Smooth (neighbouring keys closely related) and Individual (each key its own);
      3 nothing per trigger or per grain: LIFE (per note) and the grain scheduler (per grain)
        keep their own randomness, untouched.

    It is a layer on the existing Granulator, not a second engine: the profile offsets the
    layer's own POS, SIZE, DENS, SPREAD and TUNE before modulation, LIFE and the grains, in
    the knobs' own ranges. Off (or neutral) means the layer plays exactly as without it.
*/
enum class Param
{
    position,   ///< POS: fraction of the recording
    size,       ///< SIZE: octaves of grain length
    density,    ///< DENS: octaves of grains per second
    spread,     ///< SPREAD: 0..1
    tune,       ///< TUNE: semitones
};
constexpr int paramCount = 5;
const char* paramName (Param p) noexcept;   ///< "POS", "SIZE", "DENS", "SPREAD", "TUNE"
const char* paramId (Param p) noexcept;     ///< "position", "size", "density", "spread", "tune"

/**
    The furthest a profile moves each parameter either way (offset +-1), in the parameter's own
    units: half its knob's travel (POS and SPREAD 0.5, SIZE 20..400 ms and DENS 4..40 /s in
    octaves as modulation counts them, TUNE 12 st). The result is always clamped to the knob.
*/
double halfRange (Param p) noexcept;

/** One parameter's part of the profile. */
struct ParamSettings
{
    /** The curve over the keys (x = note / 127), 0..1 with 0.5 as "no offset" (bipolar), so
        OSP's curve editor and codec serve it as they serve NOTE, VEL and DAMPEN. */
    mod::CurveTable curve {};
    double curveDepth = 0.0;   ///< -1..1: how far the curve moves the parameter (1: its half range at the curve's ends)
    double variation = 0.0;    ///< 0..1: how far each key's own individuality moves it
    bool operator== (const ParamSettings&) const = default;
};

/** A layer's KEY DNA. The defaults are neutral: off, flat curves, no variation. */
struct Settings
{
    bool enabled = false;
    double amount = 1.0;          ///< 0..1: scales the whole profile (0 = none)
    double individuality = 0.3;   ///< 0 Smooth (neighbours related) .. 1 Individual (independent keys)
    std::uint64_t seed = 1;       ///< the individuality's seed (its own stream, see Table::compute)
    std::array<ParamSettings, paramCount> params;

    Settings();
    ParamSettings& operator[] (Param p) noexcept { return params[static_cast<std::size_t> (p)]; }
    const ParamSettings& operator[] (Param p) const noexcept { return params[static_cast<std::size_t> (p)]; }
    /** True when nothing would move: off, amount 0, or every depth and variation 0. */
    bool isNeutral() const noexcept;
    bool operator== (const Settings&) const = default;
};

/** The flat curve (0.5 everywhere: no offset from the curve). */
mod::CurveTable flatCurve() noexcept;
/** A curve from points (x 0..1 over the keys, y 0..1 with 0.5 neutral), OSP's curve shapes. */
mod::CurveTable curveFrom (const mod::Curve& curve) noexcept;

/**
    The cached profile of all 128 keys for one layer. Computing it allocates nothing and
    takes some 1 300 PRNG draws, so the audio thread may recompute it when the settings change.
*/
class Table
{
public:
    /** Recomputes every key. Real-time safe. */
    void compute (const Settings& settings) noexcept;

    /** Whether any key has an offset (false: the layer plays as without KEY DNA). */
    bool active() const noexcept { return anyActive; }
    /** One key's offsets (clamped to 0..127), in the parameters' own units. */
    const KeyDnaOffsets& at (int key) const noexcept { return keys[index (key)]; }
    /** The stable individuality of a key and parameter, unit scale (-1..1), before depth and amount. */
    float individuality (int key, Param p) const noexcept { return variations[index (key) * paramCount + static_cast<std::size_t> (p)]; }
    /** The combined offset of a key and parameter, -1..1 (before halfRange). */
    float offset (int key, Param p) const noexcept { return units[index (key) * paramCount + static_cast<std::size_t> (p)]; }
    /** Correlation of neighbouring keys' individuality for one parameter (tests, a future display). */
    double neighbourCorrelation (Param p) const noexcept;

private:
    static std::size_t index (int key) noexcept { return static_cast<std::size_t> (key < 0 ? 0 : key > 127 ? 127 : key); }
    std::array<KeyDnaOffsets, 128> keys {};
    std::array<float, 128 * paramCount> variations {}, units {};
    bool anyActive = false;
};

/**
    A key's offsets on top of the layer's base granular settings (the knobs, START included):
    each moved parameter stays within its knob's range; a parameter without an offset is not
    touched at all (so a neutral profile is an exact identity).
*/
inline void apply (const KeyDnaOffsets& o, GranularParams& p) noexcept
{
    if (! o.active)
        return;
    if (o.position != 0.0)
        p.position = std::clamp (p.position + o.position, 0.0, 1.0);
    if (o.sizeOctaves != 0.0)
        p.sizeSeconds = std::clamp (p.sizeSeconds * std::exp2 (o.sizeOctaves), 0.02, 0.4);
    if (o.densityOctaves != 0.0)
        p.density = std::clamp (p.density * std::exp2 (o.densityOctaves), 4.0, 40.0);
    if (o.spread != 0.0)
        p.spread = std::clamp (p.spread + o.spread, 0.0, 1.0);
    if (o.tuneSemitones != 0.0)
        p.tuneSemitones = std::clamp (p.tuneSemitones + o.tuneSemitones, -12.0, 12.0);
}

} // namespace osp::keydna
