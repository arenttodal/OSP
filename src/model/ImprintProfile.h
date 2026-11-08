#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace osp::imprint
{

/**
    SYNTHESIS S2, the spectral imprint's descriptor (docs/research/synthesize/s2-imprint-*.md):
    a recording's own spectral envelope (its resonant body, formants, bow and breath colour), at
    rest and over time, so the SYNTH oscillator can carry it. Built offline with the model
    (engine/SpectralImprint: analyse), immutable, a few kB; never persisted (rebuilt with the
    model from the same audio), so a saved patch always reproduces it.
*/

/** The descriptor's frequency grid: 50 Hz .. about 11.4 kHz in sixth-octave steps. */
constexpr double gridMinHz = 50.0;
constexpr double gridStepOctaves = 1.0 / 6.0;
constexpr int gridPoints = 48;

/** How strongly the profile follows the played pitch (0: fixed resonances, the body stays
    where the recording has it; 1: they move with the key, as a resampled sample's do). */
constexpr double defaultFormantFollow = 0.0;

/** The most an imprint boosts or cuts any band (dB): no enormous boosts in empty regions. */
constexpr double maxBandDb = 18.0;

/** The production band layout (the descriptor carries its values ready for it). */
constexpr int standardBands = 24;

struct Profile
{
    static constexpr int version = 1;

    bool valid = false;
    double referenceHz = 0.0;        ///< the recording's pitch (0: none detected; resonances then stay fixed)
    double frameSeconds = 0.05;      ///< time between frames
    int attackEnd = 0;               ///< frames [0, attackEnd) are the attack
    int sustainBegin = 0, sustainEnd = 0;   ///< frames the held sound settles in: [begin, end)
    float confidence = 0.0f;         ///< 0..1: how much there was to read (energy, frames)
    /** The imprint at rest (gridPoints dB): the sustain's envelope, its broad tilt mostly
        removed (the resonances kept), bounded, mean 0. */
    std::vector<float> staticDb;
    /** The same for every frame (frameCount x gridPoints, row-major). */
    std::vector<float> framesDb;
    /** Both ready for the standard layout (standardBands perceptual bands, 80 Hz-12 kHz), each
        band's value averaged over its width: the voice reads these with no arithmetic. */
    std::vector<float> bandStaticDb, bandFramesDb;

    int frameCount() const noexcept { return static_cast<int> (framesDb.size() / gridPoints); }
    const float* frame (int i) const noexcept { return framesDb.data() + static_cast<std::size_t> (i) * gridPoints; }
    std::size_t bytes() const noexcept
    {
        return sizeof (*this) + sizeof (float) * (staticDb.size() + framesDb.size() + bandStaticDb.size() + bandFramesDb.size());
    }

    /** A row averaged over `octaves` around `hz`: what one band can render of it. */
    static float over (const float* row, double hz, double octaves) noexcept
    {
        float s = 0.0f;
        for (int k = -2; k <= 2; ++k)
            s += at (row, hz * std::exp2 (0.25 * k * octaves));
        return 0.2f * s;
    }

    /** A row's value at `hz` (linear in log frequency, held at the ends). */
    static float at (const float* row, double hz) noexcept
    {
        const double x = std::log2 (std::max (hz, 1.0) / gridMinHz) / gridStepOctaves;
        if (x <= 0.0)
            return row[0];
        if (x >= gridPoints - 1)
            return row[gridPoints - 1];
        const int i = static_cast<int> (x);
        const float f = static_cast<float> (x - i);
        return row[i] + f * (row[i + 1] - row[i]);
    }
};

} // namespace osp::imprint
