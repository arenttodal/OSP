#pragma once

#include "core/Prng.h"
#include "engine/NoteShape.h"
#include "model/InstrumentModel.h"

#include <cstdint>

namespace osp
{

/**
    The Performance Engine (spec §29–33, Phase 4): "if this sound were performed
    again, what could change?"

    A performance is a bounded vector of correlated deviations: force, tone colour and
    timing are three latent factors that move several outputs together (more force:
    louder, brighter, stronger transient, a slightly sharp start, less damping), plus a
    small independent part per output. The latents have memory: they follow an
    Ornstein-Uhlenbeck process in time, so successive notes drift like a player's state
    rather than jumping independently. Repetition awareness alternates the attack on
    fast repeated notes and reduces pitch drift there.

    LIFE scales everything: 0 = identical repeats, 0.5 = realistic variation calibrated
    on the corpus' real round-robin sets, 1 = creative but related reinterpretations.

    Deterministic: the state depends only on the seed and the sequence of note events
    (times, notes, velocities). Real-time safe: no allocation.
*/
class PerformanceEngine
{
public:
    void reset (std::uint64_t seed) noexcept;

    /**
        Advances the hidden state to `timeSeconds` and returns this note's performance
        deviations, scaled by `life` (0..1). Adds onto `shape` (which may already carry
        velocity/dynamics contributions).
    */
    void perform (NoteShape& shape, int note, int velocity, double timeSeconds, std::uint64_t eventIndex,
                  const PerformanceProfile& profile, const SourceCharacter& character, double life) noexcept;

    /** Latest latent values (tests, diagnostics). */
    double force() const noexcept { return latentForce; }
    double colour() const noexcept { return latentColour; }
    int repeatCount() const noexcept { return repeats; }

    /** Time constant of the player's slow state drift (seconds). */
    static constexpr double memorySeconds = 4.0;

private:
    void advance (double timeSeconds) noexcept;

    Prng rng;
    std::uint64_t baseSeed = 1;
    double latentForce = 0.0, latentColour = 0.0, latentTiming = 0.0;
    double lastTime = -1.0;
    int lastNote = -1;
    int repeats = 0;
    double lastInterval = 10.0;
    int alternation = 1;
};

} // namespace osp
