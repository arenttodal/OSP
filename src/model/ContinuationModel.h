#pragma once

#include <cstddef>
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
    Pedal Loop 1.0 (docs/research/pedal-loop): one place in the body of a recording where a One
    Shot note may loop while the sustain pedal is down. Forward plays [start, end) and jumps from
    `end - crossfadeFrames` back to `start` (the destination aligned on the waveform); Ping-Pong
    turns at `end` and at `start`. Source frames; no audio is copied.
*/
struct PedalLoopRegion
{
    double startFrame = 0.0;
    double endFrame = 0.0;
    double crossfadeFrames = 0.0;   ///< Forward's jump crossfade
    float correlation = 0.0f;       ///< of that jump's windows, 0..1
    float levelDb = -120.0f;        ///< RMS over the region, dBFS (as recorded)
    /** The region's level trend, end minus start (dB; a decay is negative). Forward compensates
        it while looping, so a jump lands at the level the note had (no pulse each pass);
        shorter fluctuations stay. */
    float trendDb = 0.0f;

    /** Forward's jump: leaves at end - crossfade, lands at start. */
    ContinuationJump jump() const noexcept
    {
        ContinuationJump j;
        j.fromFrame = endFrame - crossfadeFrames;
        j.toFrame = startFrame;
        j.crossfadeFrames = crossfadeFrames;
        j.correlation = correlation;
        j.score = correlation * correlation;
        return j;
    }
};

/**
    Every Pedal Loop region of a source, built offline beside the continuation (immutable
    afterwards). A source that sustains offers its continuation's best loop first; a decaying
    one (a piano, a pluck) a few short regions along its decay, so a pedal pressed late loops
    near where the note is. Invalid (`reason` says why): the note plays as it would without
    the pedal loop.
*/
struct PedalLoopModel
{
    static constexpr int maxRegions = 8;
    bool valid = false;
    std::string reason = "not analysed";
    std::vector<PedalLoopRegion> regions;   ///< by startFrame; ends increase
    double soundEndFrame = 0.0;             ///< the last 10 ms within 30 dB of the loudest
    double levelHopFrames = 0.0;            ///< `levelDb` spacing
    std::vector<float> levelDb;             ///< the source's level (10 ms blocks), for late arming

    /** The source's level (dBFS) around `frame` (-120 outside). */
    float levelAt (double frame) const noexcept
    {
        if (levelHopFrames <= 0.0 || levelDb.empty() || frame < 0.0)
            return -120.0f;
        const auto i = static_cast<std::size_t> (frame / levelHopFrames);
        return i < levelDb.size() ? levelDb[i] : -120.0f;
    }
};

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

    /** Pedal Loop 1.0: where a One Shot note may loop under the sustain pedal. */
    PedalLoopModel pedal;

    bool hasJumps() const noexcept { return canSustain && backstop >= 0 && ! jumps.empty(); }
};

} // namespace osp
