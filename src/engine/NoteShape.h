#pragma once

#include <cstdint>

namespace osp
{

/**
    Everything that makes one note's performance different from another (spec §29),
    decided once at note-on by the engine (Performance + Dynamics) and applied by the
    voice. All-zero values mean "play the recording as it is".
*/
struct NoteShape
{
    float gain = 1.0f;                 ///< linear note gain (velocity, performance)
    double pitchCents = 0.0;           ///< static micro-pitch offset
    double pitchSettleCents = 0.0;     ///< initial offset that settles towards 0
    double pitchSettleSeconds = 0.08;
    float brightnessDb = 0.0f;         ///< high shelf (~3 kHz)
    float bodyDb = 0.0f;               ///< low shelf (~250 Hz)
    float transientDb = 0.0f;          ///< extra level at the onset, decaying
    float transientSeconds = 0.03f;
    float attackBrightnessDb = 0.0f;   ///< extra high shelf at the onset (excitation noise/bite), decaying with the transient
    float attackSoftenSeconds = 0.0f;  ///< extra linear fade-in (soft playing)
    float dampingDbPerSecond = 0.0f;   ///< extra decay (transient sources)
    float pan = 0.0f;                  ///< -1..1, small values only
    double startOffsetSeconds = 0.0;   ///< read further into the recording (>= 0)

    /** Continuation movement (strategy D / MOTION): slow drift amplitudes. */
    float driftLevelDb = 0.0f;
    float driftCents = 0.0f;
    float driftBrightnessDb = 0.0f;
    float driftRateHz = 0.12f;
    float driftPan = 0.0f;             ///< slow stereo movement amplitude (MOTION)
    float driftToneOctaves = 0.0f;     ///< DRIFT tone: CHARACTER cutoff wander (octaves)
    /** CHARACTER's response to this note's velocity (DYNAMICS x TONE): cutoff offset and
        filter-envelope depth scale. */
    float filterVelocityOctaves = 0.0f;
    float filterEnvelopeScale = 1.0f;

    /** Transient/body separation (spec §19): how much of the attack's high band comes from
        the recording at its original speed instead of the transposed read (0..1), and
        for how long. */
    float transientPreserve = 0.0f;
    float transientPreserveSeconds = 0.06f;
    /** Transient/body mixing (spec §34): level change of the separated transient only, dB. */
    float transientMixDb = 0.0f;

    /** Reimagined: a second read head just behind the first, gently moving (a living
        doubling: controlled instability, alternate sustain), mix 0..1. */
    float doubling = 0.0f;

    /** Reimagined: soft saturation drive (0 = clean) and continuation segment scale (<1 = shorter, more granular). */
    float saturation = 0.0f;
    float segmentScale = 1.0f;

    std::uint64_t seed = 1;            ///< drives the continuation walk and drift
};

} // namespace osp
