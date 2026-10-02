#include "engine/InstrumentBuilder.h"

#include "audio/pitch/OfflinePitchShifter.h"
#include "core/PitchMath.h"
#include "engine/InstrumentEngine.h"
#include "model/RootChoice.h"

#include <algorithm>
#include <cmath>

namespace osp::instrument
{

namespace
{
    double ramp (double x, double lo, double hi) { return std::clamp ((x - lo) / (hi - lo), 0.0, 1.0); }

    std::shared_ptr<const PlaybackSource> makeSource (const AudioData& audio, double rootMidi, const PlaybackPreparation& prep,
                                                      int zeroCrossings)
    {
        return std::make_shared<const PlaybackSource> (audio, rootMidi, InstrumentEngine::requiredSourcePaddingFor (zeroCrossings),
                                                       prep.startSeconds, prep.gainDb);
    }
}

SourceCharacter estimateCharacter (const AnalysisData& a, const ContinuationModel* continuation)
{
    SourceCharacter c;
    const auto& env = a.envelope;
    const double decay = -env.decaySlopeDbPerSecond; // positive when decaying
    const double decaying = ramp (decay, 3.0, 15.0);
    const double sharpAttack = 1.0 - ramp (env.attackSeconds, 0.01, 0.12);
    const bool sustains = continuation != nullptr ? continuation->canSustain : decay < 6.0;
    const double tonal = a.pitch.detected ? std::clamp (a.pitch.confidence, 0.0, 1.0) : 0.2;

    c.transientTonal = std::clamp (decaying * (0.4 + 0.6 * sharpAttack) * (0.5 + 0.5 * tonal), 0.0, 1.0);
    c.sustainedHarmonic = std::clamp ((sustains ? 1.0 - decaying : 0.2 * (1.0 - decaying)) * (0.3 + 0.7 * tonal), 0.0, 1.0);
    const double vibrato = a.pitch.vibrato ? std::clamp (a.pitch.vibrato->strength, 0.0, 1.0) : 0.0;
    const double tremolo = env.tremolo ? std::clamp (env.tremolo->strength, 0.0, 1.0) : 0.0;
    const double instability = ramp (a.pitch.stabilityCents, 5.0, 30.0);
    c.expressiveSustain = std::clamp (c.sustainedHarmonic * std::max ({ vibrato, instability, ramp (env.sustainFluctuationDb, 0.5, 3.0) }), 0.0, 1.0);
    c.periodicModulation = std::max (vibrato, tremolo);
    c.noiseComponent = std::clamp (ramp (a.spectral.meanFlatness, 0.05, 0.4) + 0.5 * (1.0 - a.spectral.periodicity), 0.0, 1.0);
    return c;
}

PerformanceProfile calibratePerformance (const SourceCharacter& c, const AnalysisData& a)
{
    // Hand-tuned starting points (spec §80): transient sources vary mostly in attack,
    // brightness and decay; sustained sources in pitch settling, brightness and movement.
    PerformanceProfile p;
    const double t = c.transientTonal;
    const double s = std::max (c.sustainedHarmonic, c.expressiveSustain);
    p.gainDb = 1.0 + 0.8 * t;
    p.brightnessDb = 1.5 + 1.5 * t + 0.5 * s;
    p.bodyDb = 0.6 + 0.6 * t;
    p.transientDb = 1.0 + 3.0 * t;
    p.pitchCents = 2.0 + 3.0 * s + (a.pitch.detected ? std::min (4.0, 0.15 * a.pitch.stabilityCents) : 0.0);
    p.pitchSettleCents = 4.0 + 10.0 * c.expressiveSustain + 3.0 * t;
    p.startOffsetMs = 1.0 + 2.0 * (1.0 - t);
    p.decayDbPerSecond = 3.0 * t;
    p.pan = 0.04;
    return p;
}

DynamicsProfile calibrateDynamics (const SourceCharacter& c, const AnalysisData&)
{
    DynamicsProfile d;
    const double t = c.transientTonal;
    d.rangeDb = 22.0 + 6.0 * t;
    d.brightnessDb = 7.0 + 4.0 * t;
    d.bodyDb = 2.0 + 1.0 * t;
    d.transientDb = 3.0 + 6.0 * t;
    d.attackSoftenMs = 10.0 + 30.0 * (1.0 - t);
    d.pitchTransientCents = 4.0 + 10.0 * t;
    d.dampingDbPerSecond = 6.0 * t;
    return d;
}

std::shared_ptr<InstrumentModel> buildProvisional (const AudioData& audio, const AnalysisData& analysis, const InstrumentBuildOptions& options)
{
    auto model = std::make_shared<InstrumentModel>();
    model->stage = InstrumentModel::Stage::provisional;
    model->analysis = analysis;
    model->rootMidi = chooseRoot (&analysis, options.rootOverrideMidi).rootMidi;
    model->playback = options.preparationOverride ? *options.preparationOverride : preparePlayback (analysis, options.playback);
    model->character = estimateCharacter (analysis, nullptr);
    model->performance = calibratePerformance (model->character, analysis);
    model->dynamics = calibrateDynamics (model->character, analysis);
    model->original.offsetSemitones = 0.0;
    model->original.source = makeSource (audio, model->rootMidi, model->playback, options.interpolationZeroCrossings);
    model->original.continuation.reason = "not analysed yet";
    return model;
}

std::shared_ptr<InstrumentModel> addContinuation (const InstrumentModel& base, const AudioData& audio, const InstrumentBuildOptions& options)
{
    auto model = std::make_shared<InstrumentModel> (base);
    model->stage = InstrumentModel::Stage::continued;
    model->original.continuation = analyseContinuation (audio, model->analysis, options.continuation);
    model->character = estimateCharacter (model->analysis, &model->original.continuation);
    model->performance = calibratePerformance (model->character, model->analysis);
    model->dynamics = calibrateDynamics (model->character, model->analysis);
    return model;
}

std::shared_ptr<InstrumentModel> addAnchors (const InstrumentModel& base, const AudioData& audio, const InstrumentBuildOptions& options)
{
    auto model = std::make_shared<InstrumentModel> (base);
    model->stage = InstrumentModel::Stage::complete;
    model->anchors.clear();
    const double rootHz = midiToHz (model->rootMidi);
    for (const double offset : options.anchorOffsets)
    {
        if (std::abs (offset) < 0.5)
            continue;
        StretchShiftOptions shift;
        shift.semitones = offset;
        shift.seed = options.seed;
        const auto shifted = stretchShiftOffline (audio, shift);
        PitchLayer layer;
        layer.offsetSemitones = offset;
        layer.source = makeSource (shifted, model->rootMidi + offset, model->playback, options.interpolationZeroCrossings);
        layer.continuation = refineContinuationForLayer (model->original.continuation, shifted, rootHz * semitonesToRatio (offset));
        model->anchors.push_back (std::move (layer));
    }
    return model;
}

std::shared_ptr<InstrumentModel> buildComplete (const AudioData& audio, const AnalysisData& analysis, const InstrumentBuildOptions& options,
                                                bool withAnchors)
{
    auto model = addContinuation (*buildProvisional (audio, analysis, options), audio, options);
    if (withAnchors)
        model = addAnchors (*model, audio, options);
    else
        model->stage = InstrumentModel::Stage::complete;
    return model;
}

} // namespace osp::instrument
