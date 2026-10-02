#pragma once

#include "model/InstrumentModel.h"

#include <memory>
#include <string>
#include <vector>

namespace osp
{

/** What a file is for in a dropped set (spec §47). */
enum class SampleRole
{
    pitchAnchor,   ///< the only/main take of its pitch
    velocityLayer, ///< one of several dynamics of the same pitch
    roundRobin,    ///< one of several equivalent takes of the same pitch and dynamic
    articulation   ///< a clearly different way of playing (kept, not used for normal notes)
};

const char* toString (SampleRole role) noexcept;

/** One file of a set, with its inferred (or user-corrected) place in the instrument. */
struct SetMember
{
    std::shared_ptr<const InstrumentModel> model;
    std::string filename;
    SampleRole role = SampleRole::pitchAnchor;
    int pitchGroup = 0;      ///< index into InstrumentSet::groups
    int layer = 0;           ///< velocity layer within the group (0 = softest)
    int take = 0;            ///< round-robin index within the layer
    double loudnessDb = 0.0; ///< max RMS (dBFS) of the recording, before playback levelling
    double confidence = 1.0; ///< how sure the inference is about role/layer, 0..1
    bool userAssigned = false;
};

struct PitchGroup
{
    double rootMidi = 60.0;
    int layers = 1;
    std::vector<int> members;          ///< indices into InstrumentSet::members
};

/**
    Several recordings that form one instrument (Phase 7). Immutable once built; the
    audio thread reads it through InstrumentEngine::setInstrumentSet(). A single file is
    a set with one member.
*/
struct InstrumentSet
{
    static constexpr int schemaVersion = 1;

    std::vector<SetMember> members;
    std::vector<PitchGroup> groups;    ///< sorted by rootMidi

    /** Register model (spec §28): spectral centroid in semitones re F0 as a linear
        function of MIDI pitch, fitted over the pitch groups. */
    bool hasRegisterModel = false;
    double brightnessSlope = 0.0;      ///< centroid/F0 change in semitones per semitone of pitch
    double brightnessIntercept = 0.0;

    /** Multi-velocity learning (spec §35): how one velocity layer differs from the next
        softer one, averaged over the pitch groups that have several layers. The engine
        uses it to make velocities between layers continuous. */
    bool hasDynamicsModel = false;
    double layerStepDb = 0.0;            ///< loudness per layer step
    double layerStepBrightnessSt = 0.0;  ///< spectral centroid per layer step (semitones)
    double layerStepAttackMs = 0.0;      ///< attack time per layer step (negative: harder is faster)

    /** The member that best represents the set (UI, analysis display). */
    int primary = 0;

    bool isValid() const noexcept { return ! members.empty() && members.front().model != nullptr && members.front().model->isValid(); }
};

} // namespace osp
