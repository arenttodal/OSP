#pragma once

#include "model/InstrumentSet.h"

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace osp
{

struct SetInput
{
    std::shared_ptr<const InstrumentModel> model;
    std::string filename;
};

/** A user correction from the Samples inspector: pins one file's place in the set. */
struct SetAssignment
{
    std::string filename;
    SampleRole role = SampleRole::roundRobin;
    std::optional<double> rootMidi;  ///< move to the pitch group nearest this root
    std::optional<int> layer;        ///< velocity layer (0 = softest)
};

/**
    Sample-set intelligence (spec §47): turns several dropped recordings into one
    instrument without any mapping.

    1. Pitch groups: recordings whose roots are within half a semitone belong together.
    2. Inside a group: dynamics words in file names (pp, mf, ff, soft, hard, ...) define
       velocity layers; otherwise a loudness gap of >= 4.5 dB splits layers; anything
       else is a round-robin take. A take much longer or shorter than its group is an
       alternate articulation (kept, not used for ordinary notes).
    3. Register model (spec §28): brightness relative to F0 as a function of pitch,
       fitted over the groups, so notes between and beyond the anchors get a plausible
       timbre instead of a plain transposition.

    Every decision keeps a confidence. Assignments from the user override the inference.
    Offline only (allocates).
*/
InstrumentSet inferSampleSet (const std::vector<SetInput>& inputs, const std::vector<SetAssignment>& assignments = {});

/** Dynamics word in a file name -> rough loudness rank (pp = 1 ... ff = 6), or nullopt. */
std::optional<double> dynamicsRankFromName (const std::string& filename);

} // namespace osp
