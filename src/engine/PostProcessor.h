#pragma once

#include "engine/DriveProcessor.h"
#include "engine/EchoDelay.h"
#include "engine/MovementBus.h"
#include "engine/ReimaginedStage.h"
#include "engine/ShelfFilter.h"
#include "engine/Shaping.h"
#include "engine/AbyssReverb.h"
#include "engine/BloomReverb.h"
#include "engine/VintageReverb.h"
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
      DRIVE      the shared saturation (DriveProcessor: TUBE, TAPE, CRUNCH) on the mixed
                 instrument; out of the signal path entirely at 0 %. Before MOVEMENT, ECHO
                 and SPACE, so the modulation, the repeats and the room hear the driven sound.
      MOVEMENT   the bus part of MOVEMENT (MovementBus: drift's shared wander, tape,
                 chorus, pulse).
      ECHO       a tape or bucket-brigade delay (EchoDelay) as a send in parallel with
                 SPACE: the ECHO macro is its level; it hears the dry sound only.
      SPACE      one of four rooms (SpaceReverb) as a send: the macro is the wet level;
                 changing type or size crossfades two reverbs over 250 ms.

    (CHARACTER is a per-voice filter now, see CharacterFilter.) Macro values are
    smoothed, so moving one never clicks. prepare() allocates; the rest is real-time safe.
*/
class PostProcessor
{
public:
    void prepare (double sampleRate, int maximumBlockSize, std::uint64_t seed = 1);
    void reset() noexcept;

    bool driveActive() const noexcept { return drive.active(); }

    /** Pick up the resonances of a newly published model (real-time safe). */
    void setModel (const InstrumentModel* model) noexcept;
    /** Overrides the Reimagined amount set by setMacros (per-layer amounts, mixed). */
    void setReimagined (double amount) noexcept { reimaginedStage.setAmount (amount); }
    void setMacros (const Macros& macros) noexcept;
    void setShaping (const Shaping& shaping) noexcept;

    void process (float* left, float* right, int numSamples) noexcept;

    // MOVEMENT's SHAPER clock and display (the host tempo also times ECHO).
    void setTiming (const HostTiming& timing) noexcept;
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

    // SPACE: two slots so a type (or engine) change can crossfade; each slot holds a room
    // and an ABYSS (both prepared, so switching never allocates) and plays its engine.
    std::array<SpaceReverb, 2> reverbs;
    std::array<AbyssReverb, 2> abysses;
    std::array<VintageReverb, 2> vintages;
    std::array<BloomReverb, 2> blooms;
    std::array<SpaceEngine, 2> slotEngine { SpaceEngine::rooms, SpaceEngine::rooms };
    int activeReverb = 0;
    int reverbFade = 0, reverbFadeLength = 1;
    SpaceReverb::Settings appliedSpace;
    AbyssReverb::Settings appliedAbyss;
    VintageReverb::Settings appliedVintage;
    BloomReverb::Settings appliedBloom;
    SpaceEngine appliedEngine = SpaceEngine::rooms;
    void processSlot (int slot, float l, float r, float& wl, float& wr) noexcept
    {
        const auto s = static_cast<std::size_t> (slot);
        switch (slotEngine[s])
        {
            case SpaceEngine::abyss: abysses[s].process (l, r, wl, wr); break;
            case SpaceEngine::vintage: vintages[s].process (l, r, wl, wr); break;
            case SpaceEngine::bloom: blooms[s].process (l, r, wl, wr); break;
            case SpaceEngine::rooms: reverbs[s].process (l, r, wl, wr); break;
        }
    }
    void startSlot (int slot) noexcept;
    void updateAbyss() noexcept;
    void updateVintage() noexcept;
    void updateBloom() noexcept;
    double lastWantedSize = 0.5;
    int sizeSettled = 0;
    bool spaceIdle = true;
    // Asleep: nothing has come in and the tail has been below -120 dBFS for a quarter
    // second, so the reverb is not run (its clock still moves); the first sound wakes it.
    bool reverbAsleep = false;
    int quietRun = 0, sleepAfter = 12000;

    // DRIVE
    DriveProcessor drive;
    double driveAmount = 0.0;

    // ECHO
    EchoDelay echo;
    double echoTarget = 0.0, echoLevel = 0.0;
    bool echoIdle = true, echoAsleep = false;
};

} // namespace osp
