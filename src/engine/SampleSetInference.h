#pragma once

#include "model/InstrumentSet.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
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
    /** ZONES: the keys the file plays (inclusive MIDI notes); unset = automatic. */
    std::optional<int> keyLow, keyHigh;
};

/**
    How a set is inferred (docs/research/multisample-mapping). The defaults are the original
    rules, which sets saved before automatic mapping keep; `automatic()` is what files imported
    together get since: takes cycle in file order and only dynamics words make velocity layers.
    Stored on the set (InstrumentSet::cycleTakes / loudnessLayers).
*/
struct SetInferenceOptions
{
    bool cycleTakes = false;
    bool loudnessLayers = true;
    /** A file of several separate attacks (five plucks in one recording) is cut into one take
        per attack (splitIntoTakes) when files are imported together. Off for sets saved before
        it (they keep their members); on for files imported since. */
    bool splitTakes = false;
    static SetInferenceOptions automatic() noexcept { return { true, false, true }; }
    static SetInferenceOptions of (const InstrumentSet& set) noexcept { return { set.cycleTakes, set.loudnessLayers, set.splitTakes }; }
};

/**
    The takes in one recording: where each of several separate attacks begins and ends (source
    frames, [start, end)), e.g. a file of five plucks of the same string. An attack counts when
    the level jumps at least 20 dB within 20 ms from a floor it had fallen to in the 300 ms
    before, to within 15 dB of the recording's loudest moment, at least 0.3 s after the one
    before; each take starts 5 ms before its attack. Empty when the recording is one take (or a
    piece would be shorter than 0.3 s). Deterministic; offline only (allocates).
*/
std::vector<std::pair<std::int64_t, std::int64_t>> splitIntoTakes (const AudioData& audio);
/** Frames [start, end) of `audio` as a recording of its own, with 1 ms fades at both cuts. */
AudioData takeOf (const AudioData& audio, std::int64_t start, std::int64_t end);
/** The name a take of `filename` is known by in the set ("Pluck.wav" -> "Pluck #2.wav"). */
std::string takeName (const std::string& filename, int take);

/**
    Sample-set intelligence (spec §47): turns several dropped recordings into one
    instrument without any mapping.

    1. Pitch groups: recordings whose roots are within half a semitone belong together.
    2. Inside a group: dynamics words in file names (pp, mf, ff, soft, hard, ...) define
       velocity layers; otherwise a loudness gap of >= 4.5 dB splits layers (unless
       `options.loudnessLayers` is off); anything else is a round-robin take (numbered in
       input order with `options.cycleTakes`). A take much longer or shorter than its group is an
       alternate articulation (kept, not used for ordinary notes).
    3. Register model (spec §28): brightness relative to F0 as a function of pitch,
       fitted over the groups, so notes between and beyond the anchors get a plausible
       timbre instead of a plain transposition.

    Every decision keeps a confidence. Assignments from the user override the inference.
    Offline only (allocates).
*/
InstrumentSet inferSampleSet (const std::vector<SetInput>& inputs, const std::vector<SetAssignment>& assignments = {},
                              const SetInferenceOptions& options = {});

/**
    The set's derived figures from its members' places (each member's pitch group, layer, take
    and role already set): the levelling reference per (group, layer), the multi-velocity and
    register models, and the primary member. inferSampleSet ends with it; sets built from
    explicit places (a Note Atlas, engine/AtlasSet) call it the same way. Offline only.
*/
void finaliseSampleSet (InstrumentSet& set);
/** A member for `model` with its loudness figures (place: group 0, layer 0, take 0). */
SetMember makeSetMember (std::shared_ptr<const InstrumentModel> model, std::string filename);

/** Dynamics word in a file name -> rough loudness rank (pp = 1 ... ff = 6), or nullopt. */
std::optional<double> dynamicsRankFromName (const std::string& filename);

} // namespace osp
