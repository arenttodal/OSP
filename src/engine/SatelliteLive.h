#pragma once

#include "engine/SatelliteMotion.h"

namespace osp
{

/**
    SATELLITE as one layer's voices read it for the chunk being rendered (InstrumentEngine writes
    it once a chunk on the audio thread, before the layer's voices render; they only read it).
    The place glides from x0 to x1 across the chunk (and ORBIT's rear cue from rear0 to rear1);
    `amount` is the 20 ms on / off glide (already in x0 / x1 / rear and width). Inactive: the
    voices take their original path, bit for bit.
*/
struct SatelliteLive
{
    bool active = false;
    bool bounceNote = false;   ///< BOUNCE with NOTE: each voice keeps its own side (amount x its place)
    float amount = 0.0f;
    float x0 = 0.0f, x1 = 0.0f, rear0 = 0.0f, rear1 = 0.0f;
    float width = 1.0f;        ///< the voices' own spread kept (1) .. one point (0)
};

} // namespace osp
