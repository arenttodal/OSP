#pragma once

#include "core/Prng.h"
#include "engine/NoteShape.h"
#include "engine/Shaping.h"
#include "model/InstrumentModel.h"

#include <array>
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

    The LIFE popup (shaping system v1.0) says what kind of variation: PITCH, TONE and
    ATTACK scale the calibrated pitch, colour and onset spreads (1x at their defaults);
    NATURAL is the correlated player above, LOOSE loosens the correlation and the memory
    (a less consistent player), FRAY adds rarer, larger outliers (a worn instrument).
    A repeat guard keeps a repeated note from sounding like either of its last two
    plays, so neither near-identical repeats nor an A-B-A-B loop can appear.

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
                  const PerformanceProfile& profile, const SourceCharacter& character, double life,
                  const Shaping& settings = Shaping {}) noexcept;

    /** Repeat guard: smallest RMS distance (in calibrated spreads) between plays of a note. */
    static constexpr double minimumDistance = 0.12;
    /** How many repeats the guard had to re-draw (tests, diagnostics). */
    int guardedRepeats() const noexcept { return guarded; }

    /** Latest latent values (tests, diagnostics). */
    double force() const noexcept { return latentForce; }
    double colour() const noexcept { return latentColour; }
    int repeatCount() const noexcept { return repeats; }

    /** Time constant of the player's slow state drift (seconds). */
    static constexpr double memorySeconds = 4.0;

private:
    void advance (double timeSeconds, double memory) noexcept;

    /** A note's independent part, in units of the calibrated spreads. */
    struct Variation
    {
        double gain = 0, bright = 0, body = 0, transient = 0, pitch = 0, settle = 0, damping = 0, start = 0, pan = 0;
        double distance (const Variation& o) const noexcept;
        Variation scaled (double pitch, double tone, double attack) const noexcept;
    };

    Prng rng;
    std::uint64_t baseSeed = 1;
    double latentForce = 0.0, latentColour = 0.0, latentTiming = 0.0;
    double lastTime = -1.0;
    int lastNote = -1;
    int repeats = 0;
    double lastInterval = 10.0;
    int alternation = 1;
    // The last two plays of the repeated note (for the guard).
    std::array<Variation, 2> previous {};
    int previousCount = 0;
    int guarded = 0;
};

} // namespace osp
