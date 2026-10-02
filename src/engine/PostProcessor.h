#pragma once

#include "engine/MovementBus.h"
#include "engine/ShelfFilter.h"
#include "engine/Shaping.h"
#include "engine/SpaceReverb.h"

#include <array>
#include <cstdint>
#include <vector>

namespace osp
{

struct InstrumentModel;
struct Macros;

/**
    Global stage after the voices (shaping system v1.0 §47):

      REIMAGINED a sympathetic resonator bank tuned to the source's partials and body
                 peaks (plus a bank a fifth above towards the far end), re-excited by
                 whatever is played.
      MOVEMENT   the bus part of MOVEMENT (MovementBus: drift's shared wander, tape,
                 chorus, pulse).
      SPACE      one of four curated ambiences (SpaceReverb) as a send: the macro is the
                 wet level; changing type crossfades two reverbs over 250 ms.

    (CHARACTER is a per-voice filter now, see CharacterFilter.) Macro values are
    smoothed, so moving one never clicks. prepare() allocates; the rest is real-time safe.
*/
class PostProcessor
{
public:
    void prepare (double sampleRate, int maximumBlockSize, std::uint64_t seed = 1);
    void reset() noexcept;

    /** Pick up the resonances of a newly published model (real-time safe). */
    void setModel (const InstrumentModel* model) noexcept;
    void setMacros (const Macros& macros) noexcept;
    void setShaping (const Shaping& shaping) noexcept;

    void process (float* left, float* right, int numSamples) noexcept;

    static constexpr int maxPeaks = 3;
    static constexpr int resonators = 6;

private:
    struct Biquad
    {
        float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
        float s1 = 0, s2 = 0;
        float process (float x) noexcept
        {
            const float y = b0 * x + s1;
            s1 = b1 * x - a1 * y + s2;
            s2 = b2 * x - a2 * y;
            return y;
        }
        void reset() noexcept { s1 = s2 = 0; }
    };

    void updateCoefficients() noexcept;
    static void peaking (Biquad& f, double sampleRate, double hz, double q, double gainDb) noexcept;
    static void bandpass (Biquad& f, double sampleRate, double hz, double t60Seconds) noexcept;

    double sampleRate = 48000.0;
    static constexpr int controlInterval = 32;
    int countdown = 0;

    // Targets and smoothed values
    double reimaginedTarget = 0.0, reimagined = 0.0;
    double appliedReimagined = -1.0;
    double motionTarget = 0.0, spaceTarget = 0.0, space = 0.0, spaceCoef = 0.001;
    Shaping shaping;

    // Model-derived
    std::array<double, resonators> resonatorHz {};
    int numResonators = 0;
    bool modelDirty = true;
    const InstrumentModel* model = nullptr;

    // Resonators (mono-summed excitation, stereo spread output)
    // First half: the source's partials and body; second half: a fifth above them
    // (harmonic remapping towards the Reimagined end, spec §12).
    std::array<Biquad, 2 * resonators> resonatorBank;
    float resonanceMix = 0.0f, remapMix = 0.0f;

    // MOVEMENT (bus part)
    MovementBus movement;

    // SPACE: two reverbs so a type change can crossfade
    std::array<SpaceReverb, 2> reverbs;
    int activeReverb = 0;
    int reverbFade = 0, reverbFadeLength = 1;
    SpaceType appliedType = SpaceType::plate;
    double appliedDecay = -1.0;
    bool spaceIdle = true;
};

} // namespace osp
