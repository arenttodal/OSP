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

/** A layer's GRANULAR settings (POS, SIZE, DENS, TUNE, SPREAD). */
struct GranularParams
{
    double position = 0.5;        ///< 0..1 across the recording: where grains are taken
    double sizeSeconds = 0.15;    ///< grain length, 0.02..0.4 s
    double density = 14.0;        ///< grains per second, 4..40
    double tuneSemitones = 0.0;   ///< grain pitch on top of the played note, -12..12
    double spread = 0.2;          ///< 0..1: how far around POS grains are taken
    bool reverse = false;         ///< layer REVERSE: every grain reads backwards
    bool follow = true;           ///< layer FOLLOW off: grains from quiet parts are lifted (levelContour)
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
    void setParams (const GranularParams& params) noexcept { settings = params; }
    void stopSpawning() noexcept { spawning = false; }
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
    };

    void spawn (double step) noexcept;
    float read (int channel, double pos) const noexcept;

    const PlaybackSource* src = nullptr;
    const EnvelopeAnalysis* contour = nullptr;
    double contourSeconds = 0.0;
    GranularParams settings;
    double sampleRate = 48000.0;
    Prng rng;
    std::array<Grain, maxGrains> grains {};
    int activeGrains = 0;
    int countdown = 0;
    bool spawning = false;
    bool stereo = false;
};

} // namespace osp
