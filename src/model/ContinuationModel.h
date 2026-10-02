#pragma once

#include <string>
#include <vector>

namespace osp
{

/**
    A place where playback can leave the recording at `fromFrame` and continue at
    `toFrame` (both in source frames) through a crossfade of `crossfadeFrames`. The
    two streams overlap for the whole crossfade: the outgoing one reads
    [from, from + crossfade), the incoming one [to, to + crossfade).
*/
struct ContinuationJump
{
    double fromFrame = 0.0;
    double toFrame = 0.0;
    double crossfadeFrames = 0.0;
    float correlation = 0.0f; ///< waveform correlation of the two crossfade windows, clamped to 0..1
    float score = 0.0f;       ///< match quality, 0..1 (higher is better)
};

/** Continuation strategies (Phase 3 experiment; the instrument uses multiLoop). */
enum class ContinuationStrategy
{
    off,        ///< play the recording once (baseline behaviour)
    naiveLoop,  ///< A: one loop over the whole stable region, 10 ms linear crossfade
    bestLoop,   ///< B: the single best-scoring loop with a correlation-aware crossfade
    multiLoop,  ///< C: random walk over several compatible jumps, never repeating immediately
    multiLoopMovement ///< D: C plus slow level/pitch/brightness drift
};

const char* toString (ContinuationStrategy strategy) noexcept;
bool parseContinuationStrategy (const std::string& text, ContinuationStrategy& out) noexcept;

/**
    How a source continues after its recorded material runs out (spec §36–43).

    Built offline by analyseContinuation(); immutable afterwards. Frames refer to one
    specific audio buffer (the original source, or a register layer whose timing matches
    it), see refineContinuationForLayer().
*/
struct ContinuationModel
{
    static constexpr int schemaVersion = 1;

    /** True if the source has a stable region that can be held indefinitely. */
    bool canSustain = false;
    std::string reason; ///< short diagnostic ("stable sustain", "decaying source", ...)

    double sustainStartFrame = 0.0; ///< first frame of the stable region
    double sustainEndFrame = 0.0;   ///< last frame of the stable region

    /** Jumps sorted by fromFrame. Every toFrame lies before jumps[backstop].fromFrame - minSegmentFrames. */
    std::vector<ContinuationJump> jumps;
    int backstop = -1;              ///< index of the jump that guarantees the walk never runs off the region
    double minSegmentFrames = 0.0;  ///< shortest stretch played between two jumps

    ContinuationJump naiveLoop;     ///< experiment A
    int bestLoop = -1;              ///< experiment B: index into jumps

    /** Release grafting (spec §43): exits from the stable region into the original ending. */
    bool hasRelease = false;
    double releaseFrame = 0.0;      ///< where the recording's own ending begins
    std::vector<ContinuationJump> graftExits; ///< sorted by fromFrame
    double tailSeconds = 0.0;       ///< length of the original ending (release -> sound end)

    /** Movement measured in the stable region; drives subtle drift (strategy D, MOTION). */
    double levelFluctuationDb = 0.0;
    double pitchFluctuationCents = 0.0;

    bool hasJumps() const noexcept { return canSustain && backstop >= 0 && ! jumps.empty(); }
};

} // namespace osp
