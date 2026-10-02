#include "engine/InstrumentBuilder.h"

#include "analysis/transient/TransientSeparation.h"

#include "audio/pitch/OfflinePitchShifter.h"
#include "core/PitchMath.h"
#include "engine/InstrumentEngine.h"
#include "model/RootChoice.h"
#include "analysis/spectrum/SpectralEnvelope.h"

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

    void addTransient (PitchLayer& layer, const AudioData& audio, double rootMidi, const PlaybackPreparation& prep, int zeroCrossings)
    {
        auto separation = separateOnsetTransient (audio, prep.startSeconds);
        if (separation.transient.isEmpty())
            return;
        layer.transient = makeSource (separation.transient, rootMidi, prep, zeroCrossings);
        layer.transientPeakSeconds = separation.peakSeconds;
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
    // Spreads at LIFE = 0.5 ("realistic"). Calibrated on the corpus' real repeated takes
    // (within-pitch standard deviations, docs/reports/performance-1.md): plucks/tagel
    // vary 3.5-5 dB in level, 1.4-3.6 st in centroid and 3.7-6.5 cents; part of that is
    // the player's dynamics, so the engine uses roughly half at realistic settings.
    PerformanceProfile p;
    const double t = c.transientTonal;
    const double s = std::max (c.sustainedHarmonic, c.expressiveSustain);
    p.gainDb = 1.2 + 0.8 * t;
    p.brightnessDb = 1.5 + 1.5 * t + 0.5 * s;
    p.bodyDb = 0.6 + 0.6 * t;
    p.transientDb = 1.0 + 2.5 * t;
    p.pitchCents = 1.5 + 1.5 * t + 1.0 * s + (a.pitch.detected ? std::min (3.0, 0.1 * a.pitch.stabilityCents) : 0.0);
    p.pitchSettleCents = 3.0 + 9.0 * c.expressiveSustain + 3.0 * t;
    p.startOffsetMs = 0.5 + 1.5 * (1.0 - t);
    p.decayDbPerSecond = 2.0 * t;
    p.pan = 0.03;
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

void findResonances (InstrumentModel& model, const AudioData& audio)
{
    // Body peaks: local maxima of the long-term spectral envelope (100 Hz .. 8 kHz) with
    // at least 2 dB prominence, a third of an octave apart, strongest first.
    const auto& a = model.analysis;
    const double f0 = a.pitch.detected ? a.pitch.fundamentalHz : 0.0;
    const auto envelope = computeSpectralEnvelope (audio.mixToMono(), audio.sampleRate, std::max (f0, 100.0) * 1.1);
    struct Peak { double hz, db; };
    std::vector<Peak> peaks;
    const auto& db = envelope.db;
    const int span = static_cast<int> (400.0 / SpectralEnvelope::stepCents); // +/- 4 semitones for prominence
    for (int i = 1; i + 1 < static_cast<int> (db.size()); ++i)
    {
        const double hz = SpectralEnvelope::minHz * std::pow (2.0, i * SpectralEnvelope::stepCents / 1200.0);
        if (hz < 100.0 || hz > 8000.0 || db[static_cast<std::size_t> (i)] < db[static_cast<std::size_t> (i - 1)] || db[static_cast<std::size_t> (i)] < db[static_cast<std::size_t> (i + 1)])
            continue;
        double lowest = db[static_cast<std::size_t> (i)];
        for (int k = std::max (0, i - span); k < std::min (static_cast<int> (db.size()), i + span); ++k)
            lowest = std::min (lowest, db[static_cast<std::size_t> (k)]);
        if (db[static_cast<std::size_t> (i)] - lowest >= 2.0)
            peaks.push_back ({ hz, db[static_cast<std::size_t> (i)] });
    }
    std::sort (peaks.begin(), peaks.end(), [] (const Peak& x, const Peak& y) { return x.db > y.db; });
    model.bodyPeaksHz.clear();
    for (const auto& p : peaks)
    {
        bool near = false;
        for (double hz : model.bodyPeaksHz)
            near = near || std::abs (std::log2 (p.hz / hz)) < 1.0 / 3.0;
        if (! near && model.bodyPeaksHz.size() < 3)
            model.bodyPeaksHz.push_back (p.hz);
    }
    // Resonators: the source's own first partials (sympathetic strings) plus body peaks.
    model.resonanceHz.clear();
    if (f0 > 0.0)
        for (int k = 1; k <= 4; ++k)
            model.resonanceHz.push_back (f0 * k);
    for (double hz : model.bodyPeaksHz)
        if (model.resonanceHz.size() < 6)
            model.resonanceHz.push_back (hz);
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
    // Brightness acts just above the source's own spectral centre of gravity, body just
    // above its fundamental, so both are audible on dark and bright sources alike.
    const double centroid = analysis.spectral.meanCentroidHz > 0.0 ? analysis.spectral.meanCentroidHz : 1500.0;
    const double f0 = analysis.pitch.detected ? analysis.pitch.fundamentalHz : 150.0;
    model->brightnessShelfHz = std::clamp (2.0 * centroid, 500.0, 6000.0);
    model->bodyShelfHz = std::clamp (1.5 * f0, 80.0, 500.0);
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
    addTransient (model->original, audio, model->rootMidi, model->playback, options.interpolationZeroCrossings);
    findResonances (*model, audio);
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
    if (audio.durationSeconds() > options.anchorMaxSeconds)
        return model;
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
        addTransient (layer, shifted, model->rootMidi + offset, model->playback, options.interpolationZeroCrossings);
        layer.continuation = refineContinuationForLayer (model->original.continuation, shifted, rootHz * semitonesToRatio (offset));
        model->anchors.push_back (std::move (layer));
    }
    return model;
}

InstrumentSet buildSet (const std::vector<SetSource>& sources, const InstrumentBuildOptions& options,
                        const std::vector<SetAssignment>& assignments, bool withContinuation)
{
    // One gain for the whole set: the loudest member reaches the usual target level.
    double loudest = -200.0, peak = -200.0;
    for (const auto& s : sources)
        if (s.analysis != nullptr)
        {
            loudest = std::max (loudest, s.analysis->envelope.maxRmsDbfs);
            peak = std::max (peak, s.analysis->envelope.peakDbfs);
        }
    double gainDb = 0.0;
    if (options.playback.normaliseLevel && loudest > -150.0)
    {
        gainDb = std::clamp (options.playback.targetMaxRmsDbfs - loudest, -options.playback.maxCutDb, options.playback.maxBoostDb);
        gainDb = std::min (gainDb, options.playback.peakCeilingDbfs - peak);
    }

    std::vector<SetInput> inputs;
    for (const auto& s : sources)
    {
        if (s.audio == nullptr || s.analysis == nullptr || s.audio->isEmpty())
            continue;
        auto memberOptions = options;
        auto onsetOnly = options.playback;
        onsetOnly.normaliseLevel = false;
        auto prep = preparePlayback (*s.analysis, onsetOnly);
        prep.gainDb = gainDb;
        memberOptions.preparationOverride = prep;
        for (const auto& a : assignments)
            if (a.filename == s.filename && a.rootMidi)
                memberOptions.rootOverrideMidi = *a.rootMidi; // a user's root correction retunes playback
        auto model = buildProvisional (*s.audio, *s.analysis, memberOptions);
        if (withContinuation)
            model = addContinuation (*model, *s.audio, memberOptions);
        model->stage = InstrumentModel::Stage::complete;
        inputs.push_back ({ std::move (model), s.filename });
    }
    return inferSampleSet (inputs, assignments);
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
