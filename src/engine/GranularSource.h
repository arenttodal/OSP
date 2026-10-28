#pragma once

#include "core/Prng.h"
#include "model/AnalysisData.h"
#include "model/PlaybackSource.h"

#include <array>
#include <cstdint>

namespace osp
{

/** How a layer plays its recording. */
enum class SourceMode { oneShot, granular };

/** A grain's envelope. HANN is the original (by rotation, the default path); the others
    are tables, level-matched to it. Saved as a choice index: append only. */
enum class GrainWindow
{
    hann,        ///< the original raised cosine
    soft,        ///< Tukey: flat middle half, cosine shoulders (fuller, less pulsing)
    percussive   ///< a quick rise and an exponential fall (each grain a small attack)
};

/** A layer's GRANULAR settings (POS, SIZE, DENS, TUNE, SPREAD; SCATTER, PAN, WINDOW). */
struct GranularParams
{
    double position = 0.5;        ///< 0..1 across the recording: where grains are taken
    double sizeSeconds = 0.15;    ///< grain length, 0.02..0.4 s
    double density = 14.0;        ///< grains per second, 4..40
    double tuneSemitones = 0.0;   ///< grain pitch on top of the played note, -12..12
    double spread = 0.2;          ///< 0..1: how far around POS grains are taken
    bool reverse = false;         ///< layer REVERSE: every grain reads backwards
    bool follow = true;           ///< layer FOLLOW off: grains from quiet parts are lifted (levelContour)
    // Phase 2 options, neutral by default (exactly the original grains). Their randomness is
    // the grains' own second stream, so turning one on never moves the original choices.
    double scatterSemitones = 0.0;   ///< SCATTER: each grain's pitch moves randomly within +-this, 0..12
    double pan = 0.0;                ///< PAN: each grain's place in the stereo field, randomly within +-this, 0..1
    GrainWindow window = GrainWindow::hann;
};

/**
    Granular source mode of one voice (A/B layer system): instead of reading the
    recording through, the voice plays a stream of Hann-windowed grains taken around POS,
    each read at the note's pitch (times TUNE) with cubic interpolation. It sustains for
    as long as the note is held; after note-off no grain starts and the playing ones
    finish. Settings are read live (the caller passes them at control rate).

    Deterministic (seeded per note), real-time safe: a fixed pool of grains, no
    allocation; reads never leave the recording.
*/
class GranularSource
{
public:
    static constexpr int maxGrains = 24;

    /** `envelope` / `durationSeconds`: the recording's loudness contour (FOLLOW off), may be null. */
    void start (const PlaybackSource& source, const GranularParams& params, double outputSampleRate, std::uint64_t seed,
                const EnvelopeAnalysis* envelope = nullptr, double durationSeconds = 0.0) noexcept;
    void setParams (const GranularParams& params) noexcept
    {
        settings = params;
        updateNorm();
    }
    void stopSpawning() noexcept { spawning = false; }
    /** The grain budget's share for this stream: at most this many grains at once (1..24;
        24, the pool, is unlimited). Once a note has been capped its level follows the grains it
        may actually play, gliding over about half a grain (no step when the share changes). */
    void setGrainCap (int cap) noexcept;
    int grainCap() const noexcept { return cap; }
    /** Grains this stream plays at once at its settings (DENS x SIZE, plus one for the spawn
        jitter; 1..24): what it asks of the budget. */
    int grainDemand() const noexcept;
    bool isFinished() const noexcept { return ! spawning && activeGrains == 0; }
    int grainCount() const noexcept { return activeGrains; }

    /** One output sample. `step`: recording frames per output sample at the note's pitch. */
    void render (float& left, float& right, double step) noexcept;

    /** What a grain is doing, for the display: where it reads (0..1 of the recording), how
        loud its window is now (0..1) and a stable random lane (0..1) to scatter it vertically. */
    struct GrainView
    {
        float position = 0.0f, level = 0.0f, lane = 0.5f;
    };
    /** Writes up to `max` playing grains; returns how many. */
    int collect (GrainView* out, int max) const noexcept;

private:
    struct Grain
    {
        double position = 0.0, ratio = 1.0;
        double c = 1.0, s = 0.0, cd = 1.0, sd = 0.0;   // Hann window by rotation
        double direction = 1.0;                       // -1: REVERSE
        float gain = 1.0f;                            // FOLLOW off: lift from the loudness contour
        int remaining = 0;
        float lane = 0.5f;
        bool active = false;
        // PAN and the table windows (only for grains that use them).
        float gainL = 1.0f, gainR = 1.0f;
        float windowPhase = 0.0f, windowStep = 0.0f;
        GrainWindow window = GrainWindow::hann;
        bool shaped = false;   ///< panned or table-windowed: rendered by the general loop
    };

    void spawn (double step) noexcept;
    /** render() while a panned or table-windowed grain plays. */
    void renderShaped (float& left, float& right, double step) noexcept;
    /** The level correction for overlapping grains (it depends only on the settings). */
    void updateNorm (bool snap = false) noexcept;
    double expectedOverlap() const noexcept;
    void glideNorm() noexcept;

    const PlaybackSource* src = nullptr;
    const EnvelopeAnalysis* contour = nullptr;
    double contourSeconds = 0.0;
    GranularParams settings;
    double sampleRate = 48000.0;
    Prng rng;
    Prng optionRng;   ///< SCATTER and PAN's own stream (never the grain-position stream)
    std::array<Grain, maxGrains> grains {};
    int activeGrains = 0;
    int shapedGrains = 0;
    int cap = maxGrains;
    // Under a budget (only once this note has been capped; otherwise the original law, exactly):
    // the overlap the level is normalised for, gliding to the admitted one.
    bool budgeted = false;
    double overlapNow = 0.0, overlapTarget = 0.0, overlapGlide = 1.0, askedLevel = 1.0;
    int countdown = 0;
    bool spawning = false;
    bool stereo = false;
    float norm = 1.0f;
};

} // namespace osp
