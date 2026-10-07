#pragma once

#include "engine/NoteShape.h"
#include "engine/ReimaginedModes.h"

#include <algorithm>
#include <cmath>

namespace osp
{

/**
    KALEIDOSCOPE: the original Reimagined algorithm, as one of the five modes. It refracts
    the recording into richer versions of itself:

      per voice   shorter, more varied continuation segments; harmonic saturation; a
                  slowly moving doubling head; granular continuation with octave / fifth
                  remapping; a drift of its own (InstrumentEngine::shapeFor, the voice's
                  native path)
      per layer   ReimaginedStage: sympathetic resonators tuned to the source, a remapped
                  bank a fifth above, wandering formants

    This header holds its amount mapping, unchanged from before the modes existed, plus
    FOCUS and SPREAD. Both are exactly neutral at 0.5 (every older session and preset):
    FOCUS moves where along the amount the abstract parts (saturation, doubling, grains,
    the fifth-above bank, the formants) arrive - lower keeps the source recognisable for
    longer, higher reinterprets sooner; SPREAD scales the width of the doubling, the grains'
    pan, the drift's stereo movement and the resonators' left / right split.
*/
namespace kaleidoscope
{
    /** The amount the abstract parts follow: the amount itself at FOCUS 0.5. */
    inline double abstraction (double amount, double focus) noexcept
    {
        if (focus == 0.5 || amount <= 0.0)
            return amount;
        // FOCUS 0 -> amount^2 (later), 1 -> sqrt (amount) (sooner).
        return std::pow (std::clamp (amount, 0.0, 1.0), std::exp2 (-2.0 * (std::clamp (focus, 0.0, 1.0) - 0.5)));
    }

    /** SPREAD as a width factor: 1 at 0.5 (the original), 0.25 .. 1.75. */
    inline float width (double spread) noexcept
    {
        return spread == 0.5 ? 1.0f : static_cast<float> (0.25 + 1.5 * std::clamp (spread, 0.0, 1.0));
    }

    /**
        The per-voice part (spec §12): `r` is the layer's amount (0 when the layer plays
        another mode), `motion` the MOVEMENT macro. Shorter, more varied continuation;
        harmonic saturation towards the far end; the doubling head; at the far end granular
        continuation with harmonic remapping takes over the sustain.
    */
    inline void shapeNote (NoteShape& shape, double r, double motion, const KaleidoscopeParams& params) noexcept
    {
        const double ra = abstraction (r, params.focus);
        shape.segmentScale = static_cast<float> ((1.0 - 0.7 * r * r) * (1.3 - 0.6 * motion));
        shape.saturation = static_cast<float> (0.7 * std::clamp ((ra - 0.5) / 0.5, 0.0, 1.0));
        shape.doubling = static_cast<float> (0.7 * std::clamp ((ra - 0.35) / 0.65, 0.0, 1.0));
        shape.granular = static_cast<float> (std::pow (std::clamp ((ra - 0.45) / 0.55, 0.0, 1.0), 1.2));
        shape.spread = width (params.spread);
    }
}

} // namespace osp
