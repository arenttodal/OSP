#pragma once

#include "engine/Modulation.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace osp::dampen
{

/** The order is the parameter's choice order (saved in sessions): append only. */
enum class Mode
{
    softShelf = 0,   ///< a high shelf, down to -18 dB at full damping
    lowPass          ///< 12 dB/oct, from 20 kHz down to the minimum cutoff (log scale)
};

constexpr double maxShelfCutDb = 18.0;
constexpr double topHz = 20000.0;
constexpr double minShelfHz = 1000.0, maxShelfHz = 8000.0;
constexpr double minCutoffLowHz = 300.0, minCutoffHighHz = 12000.0;

/**
    DAMPEN: each note's own high-frequency damping, by the key it sounds (the curve over MIDI
    notes, 0 = untouched, 1 = full damping) times AMOUNT. One layer's settings; the voice reads
    them live at control rate. Bypassed by default, so a layer without it sounds exactly as
    before.
*/
struct Settings
{
    bool enabled = false;
    Mode mode = Mode::softShelf;
    double amount = 1.0;          ///< AMOUNT 0..1
    double shelfHz = 3000.0;      ///< Soft Shelf's corner, 1..8 kHz
    double minCutoffHz = 2000.0;  ///< Low-Pass at full damping, 300 Hz .. 12 kHz
    mod::CurveTable curve {};     ///< damping over the keys (x = note / 127)
};

/** The starting curve: nothing below C4, rising to full damping at the top of the keyboard. */
mod::Curve defaultCurve() noexcept;
Settings defaultSettings() noexcept;

/** The damping (0..1) a note takes from the curve and AMOUNT (before modulation). */
inline double noteDamping (const Settings& s, int note) noexcept
{
    return std::clamp (mod::curveAt (s.curve, std::clamp (note, 0, 127) / 127.0), 0.0, 1.0);
}

/** Low-Pass's cutoff at a damping (0 = 20 kHz, 1 = the minimum), on a log scale. */
inline double lowPassHz (double damping, double minCutoffHz) noexcept
{
    return topHz * std::pow (std::clamp (minCutoffHz, 20.0, topHz) / topHz, std::clamp (damping, 0.0, 1.0));
}

/**
    The voice's filter: one biquad per channel (transposed direct form II), its coefficients
    set at control rate only when the damping or the settings move. At damping 0 it is skipped
    (the signal untouched). Real-time safe.
*/
class Filter
{
public:
    void reset() noexcept { l1 = l2 = r1 = r2 = 0.0f; }
    bool active() const noexcept { return on; }

    void setup (Mode mode, double sampleRate, double damping, double shelfHz, double minCutoffHz) noexcept
    {
        on = damping > 1.0e-4;
        if (! on)
            return;
        const double nyquistSafe = 0.45 * sampleRate;
        double b0, b1, b2, a0, a1, a2;
        if (mode == Mode::softShelf)
        {
            // RBJ high shelf, slope 1.
            const double a = std::pow (10.0, -maxShelfCutDb * damping / 40.0);
            const double w0 = 2.0 * std::numbers::pi * std::min (shelfHz, nyquistSafe) / sampleRate;
            const double cosw = std::cos (w0), alpha = std::sin (w0) / 2.0 * std::sqrt (2.0);
            const double sq = 2.0 * std::sqrt (a) * alpha;
            b0 = a * ((a + 1) + (a - 1) * cosw + sq);
            b1 = -2 * a * ((a - 1) + (a + 1) * cosw);
            b2 = a * ((a + 1) + (a - 1) * cosw - sq);
            a0 = (a + 1) - (a - 1) * cosw + sq;
            a1 = 2 * ((a - 1) - (a + 1) * cosw);
            a2 = (a + 1) - (a - 1) * cosw - sq;
        }
        else
        {
            // RBJ low-pass, Q 0.707 (Butterworth).
            const double w0 = 2.0 * std::numbers::pi * std::min (lowPassHz (damping, minCutoffHz), nyquistSafe) / sampleRate;
            const double cosw = std::cos (w0), alpha = std::sin (w0) / std::numbers::sqrt2;
            b0 = (1.0 - cosw) / 2.0;
            b1 = 1.0 - cosw;
            b2 = (1.0 - cosw) / 2.0;
            a0 = 1.0 + alpha;
            a1 = -2.0 * cosw;
            a2 = 1.0 - alpha;
        }
        c0 = static_cast<float> (b0 / a0);
        c1 = static_cast<float> (b1 / a0);
        c2 = static_cast<float> (b2 / a0);
        c3 = static_cast<float> (a1 / a0);
        c4 = static_cast<float> (a2 / a0);
    }

    void process (float& l, float& r) noexcept
    {
        if (! on)
            return;
        const float yl = c0 * l + l1;
        l1 = c1 * l - c3 * yl + l2;
        l2 = c2 * l - c4 * yl;
        const float yr = c0 * r + r1;
        r1 = c1 * r - c3 * yr + r2;
        r2 = c2 * r - c4 * yr;
        l = yl;
        r = yr;
    }

private:
    bool on = false;
    float c0 = 1.0f, c1 = 0.0f, c2 = 0.0f, c3 = 0.0f, c4 = 0.0f;
    float l1 = 0.0f, l2 = 0.0f, r1 = 0.0f, r2 = 0.0f;
};

} // namespace osp::dampen
