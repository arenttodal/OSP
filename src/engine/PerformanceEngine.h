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

    CHARACTER sets the spreads and how the outputs move together: AUTO is the
    calibration above (from the sample's own analysis); PLUCK, SYNTH and DRUM carry the
    round-robin generator's priors and trained models (rr-variation-model/2).

    TAKES (round robins): with 2..16 takes each note keeps that many fixed performances
    instead of inventing a new one every time. A note's takes are spread evenly (one
    stratum of the distribution per take, each axis independently shuffled, re-centred
    so the takes average to the recording) and are recomputed from the seed at note-on,
    so they cost no memory. A repeated note steps to its next take (CYCLE) or to any
    other take (RANDOM), never the same one twice in a row. The player's slow drift and
    DYNAMICS still apply on top, as they would to a sampled round-robin set.

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

    /** Most takes a note can keep. */
    static constexpr int maxTakes = 16;
    /** The take the last note played (-1 with endless takes; tests, diagnostics). */
    int lastTakePlayed() const noexcept { return lastPlayedTake; }

private:
    void advance (double timeSeconds, double memory) noexcept;

    /** A take's latent and independent values: force, colour, timing, then one per output. */
    static constexpr int takeDimensions = 12;
    std::uint64_t takePoolSeed (std::uint32_t reroll) const noexcept;
    static void takeValues (std::uint64_t poolSeed, int note, int take, int count, double (&z)[takeDimensions]) noexcept;
    int chooseTake (int note, int count, LifeTakeOrder order, std::uint64_t eventIndex) noexcept;

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
    // TAKES: the take each note played last (0xff: none yet).
    std::array<std::uint8_t, 128> lastTake {};
    int lastPlayedTake = -1;
};

} // namespace osp
