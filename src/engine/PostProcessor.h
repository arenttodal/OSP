#pragma once

#include "engine/MovementBus.h"
#include "engine/ReimaginedStage.h"
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

      REIMAGINED the shared ReimaginedStage (resonators and wandering formants), fed
                 the layers' power-weighted amount - legacy routing; with per-layer
                 routing every layer has its own stage before the mix and this one is
                 set to 0.
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
    /** Overrides the Reimagined amount set by setMacros (per-layer amounts, mixed). */
    void setReimagined (double amount) noexcept { reimaginedStage.setAmount (amount); }
    void setMacros (const Macros& macros) noexcept;
    void setShaping (const Shaping& shaping) noexcept;

    void process (float* left, float* right, int numSamples) noexcept;

    // MOVEMENT's SHAPER clock and display.
    void setTiming (const HostTiming& timing) noexcept { movement.setTiming (timing); }
    void noteStarted() noexcept { movement.noteStarted(); }
    void setVoicesActive (bool active) noexcept { movement.setVoicesActive (active); }
    float shaperPhase() const noexcept { return movement.shaperPhase(); }

    static constexpr int maxPeaks = 3;

private:
    void updateCoefficients() noexcept;

    double sampleRate = 48000.0;
    static constexpr int controlInterval = 32;
    int countdown = 0;

    // Targets and smoothed values
    double motionTarget = 0.0, spaceTarget = 0.0, space = 0.0, spaceCoef = 0.001;
    Shaping shaping;

    ReimaginedStage reimaginedStage;

    // MOVEMENT (bus part)
    MovementBus movement;

    // SPACE: two reverbs so a type change can crossfade
    std::array<SpaceReverb, 2> reverbs;
    int activeReverb = 0;
    int reverbFade = 0, reverbFadeLength = 1;
    SpaceType appliedType = SpaceType::plate;
    double appliedDecay = -1.0;
    bool spaceIdle = true;
    // Asleep: nothing has come in and the tail has been below -120 dBFS for a quarter
    // second, so the reverb is not run (its clock still moves); the first sound wakes it.
    bool reverbAsleep = false;
    int quietRun = 0, sleepAfter = 12000;
};

} // namespace osp
