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
    void noteStarted() noexcept
    {
        movement.noteStarted();
        movementB.noteStarted();
    }
    void setVoicesActive (bool active) noexcept
    {
        movement.setVoicesActive (active);
        movementB.setVoicesActive (active);
    }
    float shaperPhase() const noexcept { return movement.shaperPhase(); }
    /** MOVEMENT lane A's SHAPER handed over to a product's per-note shaping (opt-in; false by
        default, as in ANDOR OSP: MovementBus::setShaperHandoff). Lane B is never handed over.
        CLASSIC (process()) and CUSTOM (customMovement()) both run lane A, so both follow it. */
    void setShaperHandoff (bool handedOver) noexcept { movement.setShaperHandoff (handedOver); }
    double shaperHandoff() const noexcept { return movement.shaperHandoff(); }
    /** A lane's SHAPER playhead (0 MOVEMENT, 1 the second lane), -1 when idle. */
    float shaperPhase (int lane) const noexcept { return lane == 0 ? movement.shaperPhase() : movementB.shaperPhase(); }

    /**
        MOVEMENT's two lanes (a product that has them: ANDOR ANALOG). Lane A is MOVEMENT itself
        (setShaping's movement settings); lane B is a second, independent bus with its own
        settings (`bSettings`'s movement fields), in series after A: two pedals in a row, so
        amplitude movements multiply rather than add and two delay-based ones never comb
        against a dry copy. Each lane plays at the MOVEMENT macro times its LEVEL; switching a
        lane on or off crossfades it in or out (30 ms), and a lane that is off is not run.
        Lane B's TAPE and CHORUS never raise the level they are given (a slow guard, -6 dB at
        most), so a chorus after a chorus does not build up. Never called: one lane, exactly as
        before.
    */
    void setMovementLanes (bool aOn, double aLevel, bool bOn, double bLevel, const Shaping& bSettings) noexcept;

    static constexpr int maxPeaks = 3;

    /**
        A routed chain (opt-in: ANDOR ANALOG's FX routing, CUSTOM). The same processors as
        process(), one stage at a time over a run of samples, so a product can put them in
        another order. process() is not changed by any of this and is what plays until a
        product calls these instead; a product calls either, never both in one block.

        Per block: customControl() first, then each stage once over runs no longer than it
        returns (the control-rate updates - SPACE's retuning and crossfades - happen there,
        every 32 samples, as in process()). Each stage keeps its own smoothing, sleep and
        crossfades exactly as process() does.
          - customDrive / customMovement: in place (DRIVE untouched at 0, the lanes as set).
          - customEcho: ECHO's wet alone, its level (the ECHO macro) applied - what process()
            adds to its output. `input` scales what enters the delay (0: its tail drains).
          - customSpace: SPACE's wet alone, its level applied, and the dry gain process() puts
            on the dry signal (1 - 0.2 SPACE) per sample, so `dry * x + wet` is process()'s.
    */
    int customControl (int maxSamples) noexcept;
    void customReimagined (float* left, float* right, int numSamples) noexcept;
    void customDrive (float* left, float* right, int numSamples) noexcept;
    void customMovement (float* left, float* right, int numSamples) noexcept;
    void customEcho (const float* left, const float* right, float* wetLeft, float* wetRight, int numSamples, float input = 1.0f) noexcept;
    void customSpace (const float* left, const float* right, float* wetLeft, float* wetRight, float* dryGain, int numSamples, float input = 1.0f) noexcept;

    /** MOVEMENT's level gain, sample by sample, written into `buffer` (opt-in; null by default:
        nothing is written and nothing changes). Each sample MOVEMENT processes (process() or
        customMovement()) writes the next entry, up to `capacity`: PULSE's tremolo and SHAPER's
        VOL (both lanes multiplied), 1 for DRIFT, TAPE and CHORUS. ANDOR ANALOG multiplies its
        SUB, which never enters the effects, by it, so the sub pulses and gates with the rest.
        The caller fills the buffer with 1 first (a route without MOVEMENT writes nothing). */
    void beginMovementTrace (float* buffer, int capacity) noexcept
    {
        trace = buffer;
        traceCapacity = capacity;
        traceCursor = 0;
    }
    void endMovementTrace() noexcept { trace = nullptr; }

private:
    float* trace = nullptr;
    int traceCapacity = 0, traceCursor = 0;
    float laneGain = 1.0f;   ///< both lanes' level gain at the last processLanes
    void writeTrace() noexcept
    {
        if (trace != nullptr && traceCursor < traceCapacity)
            trace[traceCursor++] = lanes ? laneGain : movement.amplitude();
    }
    void updateCoefficients() noexcept;

    double sampleRate = 48000.0;
    static constexpr int controlInterval = 32;
    int countdown = 0;

    // Targets and smoothed values
    double motionTarget = 0.0, spaceTarget = 0.0, space = 0.0, spaceCoef = 0.001;
    Shaping shaping;

    ReimaginedStage reimaginedStage;

    // MOVEMENT (bus part): lane A, and lane B once a product sets lanes (setMovementLanes).
    MovementBus movement, movementB;
    struct Lane
    {
        bool on = true;
        double level = 1.0, mix = 1.0;
    };
    Lane laneA, laneB { false, 1.0, 0.0 };
    Shaping laneBSettings;
    bool lanes = false;
    double laneCoef = 0.001;
    double guardIn = 0.0, guardOut = 0.0, guardGain = 1.0, guardCoef = 0.0001, guardSmooth = 0.001;
    void applyMovementTargets() noexcept;
    void processLanes (float& l, float& r) noexcept;

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
