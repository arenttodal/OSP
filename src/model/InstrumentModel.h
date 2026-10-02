#pragma once

#include "model/AnalysisData.h"
#include "model/ContinuationModel.h"
#include "model/PlaybackPreparation.h"
#include "model/PlaybackSource.h"

#include <cmath>
#include <memory>
#include <string>
#include <vector>

namespace osp
{

/** How notes away from the root are made (Phase 2 decision, docs/reports/pitch-bakeoff-1.md). */
enum class PitchCharacter
{
    tape,    ///< A: resample the recording (vintage-sampler character; default)
    natural  ///< B: resample the nearest register anchor (timing kept, built offline)
};

/** One playable rendition of the source: the original, or a register anchor (spec §27). */
struct PitchLayer
{
    double offsetSemitones = 0.0;   ///< transposition baked into this layer's audio
    std::shared_ptr<const PlaybackSource> source; ///< rootMidi already includes the offset; shared between model stages
    ContinuationModel continuation; ///< jump points aligned for this layer's audio
};

/**
    Internal source-behaviour descriptors (spec §8): soft memberships, never a hard type.
    They steer how strongly each engine acts (e.g. MOTION is almost inactive on plucks).
*/
struct SourceCharacter
{
    double transientTonal = 0.0;
    double sustainedHarmonic = 0.0;
    double expressiveSustain = 0.0;
    double periodicModulation = 0.0;
    double noiseComponent = 0.0;
};

/**
    Per-source calibration of the Performance Engine (spec §29–32, §80): how far one
    performance may differ from the next at LIFE = 1, in the source's own units.
*/
struct PerformanceProfile
{
    double gainDb = 1.5;             ///< level spread (1 sigma at force extremes)
    double brightnessDb = 2.5;       ///< high-shelf spread
    double bodyDb = 1.0;             ///< low-shelf spread
    double transientDb = 3.0;        ///< attack emphasis spread
    double pitchCents = 6.0;         ///< static micro-pitch spread
    double pitchSettleCents = 12.0;  ///< initial pitch offset that settles
    double startOffsetMs = 3.0;      ///< how far into the attack a performance may start
    double decayDbPerSecond = 3.0;   ///< damping spread (transient sources)
    double pan = 0.06;               ///< stereo bias spread
};

/** How velocity changes a performance (spec §34, Phase 5). */
struct DynamicsProfile
{
    double rangeDb = 24.0;           ///< level range across velocity at DYNAMICS = 1
    double brightnessDb = 9.0;       ///< high-shelf change soft -> hard
    double bodyDb = 2.5;             ///< low-shelf change
    double transientDb = 6.0;        ///< attack emphasis soft -> hard
    double attackSoftenMs = 25.0;    ///< extra attack time at the softest velocity
    double pitchTransientCents = 10.0; ///< hard notes start slightly sharp and settle
    double dampingDbPerSecond = 4.0; ///< soft notes decay faster (transient sources)
};

/**
    The immutable instrument the audio thread plays (spec §59). Built off the audio
    thread in stages; each stage is a complete, playable model.
*/
struct InstrumentModel
{
    static constexpr int schemaVersion = 1;

    enum class Stage
    {
        provisional = 1, ///< decoded + root + onset: plays like the baseline sampler
        continued = 2,   ///< + continuation model (sustain, release graft)
        complete = 3     ///< + register anchors and remaining descriptors
    };

    Stage stage = Stage::provisional;
    AnalysisData analysis;
    double rootMidi = 60.0;
    PlaybackPreparation playback;
    SourceCharacter character;
    PerformanceProfile performance;
    DynamicsProfile dynamics;

    PitchLayer original;               ///< offset 0
    std::vector<PitchLayer> anchors;   ///< natural-character register anchors (offset != 0), may be empty

    /** Layer that should play `note` (fractional MIDI). */
    const PitchLayer& layerFor (double note, PitchCharacter pitchCharacter) const noexcept
    {
        if (pitchCharacter == PitchCharacter::tape || anchors.empty())
            return original;
        const double wanted = note - rootMidi;
        const PitchLayer* best = &original;
        for (const auto& layer : anchors)
            if (std::abs (layer.offsetSemitones - wanted) < std::abs (best->offsetSemitones - wanted))
                best = &layer;
        return *best;
    }

    bool isValid() const noexcept { return original.source != nullptr && original.source->isValid(); }
};

} // namespace osp
