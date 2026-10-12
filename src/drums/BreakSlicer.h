#pragma once

#include "core/AudioData.h"
#include "drums/DrumKitEngine.h"

#include <cstdint>
#include <vector>

/**
    ANDOR Drums' break slicer (spec Part 7; pure C++, deterministic, background threads).

    Onsets come from a multi-band spectral flux: the positive change of log magnitude in four
    bands (kick, body, snare/clap, hats), each normalised by its own level, so a quiet hat
    over a loud kick and a ghost note after a backbeat are still seen. Peaks above an
    adaptive median threshold are kept at least a minimum separation apart. Every onset is
    then aligned to where its attack begins in the waveform (minus a short pre-roll), so a
    slice never cuts away the start of its transient.

    Slices are regions of the original recording (no audio is copied): each runs from its
    onset to the next one (the groove's own timing), the last one until its sound has died.
*/
namespace osp::drums
{

struct SliceSettings
{
    double sensitivity = 0.5;            ///< 0..1: higher finds softer hits
    double minSeparationSeconds = 0.045; ///< two onsets never closer than this
    double preRollSeconds = 0.002;       ///< kept before the attack's start
    double maxSliceSeconds = 4.0;        ///< the last slice (and any slice) at most this long
};

/** One region of the break. Times are the recording's frames (end exclusive). */
struct BreakSlice
{
    std::int64_t start = 0;              ///< where the slice plays from (the onset minus the pre-roll)
    std::int64_t end = 0;
    std::int64_t onset = 0;              ///< the original timing: where the hit is in the groove
    double strength = 0.0;               ///< onset strength 0..1 (louder hits higher); a velocity for the groove
    DrumCategory category = DrumCategory::unknown;
    double confidence = 0.0;             ///< of the category (0: not classified)
    bool layered = false;                ///< several instruments at once (e.g. kick + hat): uncertain
    int pad = -1;                        ///< destination pad in the import plan (-1: stays in the tray)
    bool manual = false;                 ///< added or moved by the user

    bool operator== (const BreakSlice&) const = default;
};

/** The onset strength curve and its frame times (for display and tests). */
struct OnsetCurve
{
    double hopSeconds = 0.0;
    std::vector<float> value;            ///< combined, normalised flux per hop
};

OnsetCurve onsetCurve (const AudioData& audio);

/** Detected onsets (aligned attack starts, before the pre-roll), in frames, with strengths. */
struct Onset
{
    std::int64_t frame = 0;
    double strength = 0.0;
};
std::vector<Onset> detectBreakOnsets (const AudioData& audio, const SliceSettings& settings = {});

/** The break cut at its onsets. */
std::vector<BreakSlice> sliceBreak (const AudioData& audio, const SliceSettings& settings = {});

// Manual editing (message thread). Slices stay sorted and contiguous; edits keep each
// slice's plan (pad, category) where the slice survives.

/** Adds a slice boundary at `frame` (snapped back to the attack start within 15 ms when
    there is one). Returns the new slice's index, or -1 if too close to another boundary. */
int addSliceAt (std::vector<BreakSlice>& slices, const AudioData& audio, std::int64_t frame, const SliceSettings& settings = {});
/** Removes slice `index`: the previous slice (if any) extends over it. */
bool removeSlice (std::vector<BreakSlice>& slices, int index);
/** Moves the start of slice `index` (and the end of the previous one) to `frame`, kept
    between its neighbours. */
bool moveSliceStart (std::vector<BreakSlice>& slices, int index, std::int64_t frame, std::int64_t totalFrames);

/** A tempo estimate (BPM, 70..180) from the onsets: the beat grid that best fits them. 0 if
    there are too few onsets. */
double estimateTempo (const std::vector<BreakSlice>& slices, double sampleRate);

} // namespace osp::drums
