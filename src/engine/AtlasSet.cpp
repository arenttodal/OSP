#include "engine/AtlasSet.h"

#include "analysis/Analyzer.h"
#include "engine/SampleSetInference.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <map>
#include <numbers>
#include <tuple>

namespace osp::atlasset
{

namespace
{
    std::size_t sourceBytes (const PlaybackSource* s)
    {
        if (s == nullptr)
            return 0;
        return static_cast<std::size_t> (s->numChannels()) * static_cast<std::size_t> (s->numFrames() + 2 * s->padding()) * sizeof (float);
    }

    std::size_t modelBytes (const InstrumentModel& m)
    {
        std::size_t bytes = sourceBytes (m.original.source.get()) + sourceBytes (m.original.transient.get());
        for (const auto& a : m.anchors)
            bytes += sourceBytes (a.source.get()) + sourceBytes (a.transient.get());
        if (m.reimagined != nullptr)
            bytes += m.reimagined->tape.splices.size() * sizeof (ReimaginedAnalysis::TapeSplice)
                     + m.reimagined->mosaic.frames.size() * sizeof (ReimaginedAnalysis::MosaicFrame);
        return bytes;
    }

    /** Raised-cosine fade gain for frame i of n (0 -> 1). */
    float fadeGain (std::int64_t i, std::int64_t n)
    {
        if (n <= 0 || i >= n)
            return 1.0f;
        return static_cast<float> (0.5 - 0.5 * std::cos (std::numbers::pi * (static_cast<double> (i) + 0.5) / static_cast<double> (n)));
    }

    /** The note's frames (its source's channels, at most two) with its fades and the anti-click edges. */
    AudioData copyRegion (const AudioData& source, const atlas::NoteEvent& note, atlas::FrameRange range, std::int64_t edgeFade)
    {
        const int channels = std::min (source.numChannels(), 2);
        const auto frames = range.length();
        auto out = AudioData::allocate (channels, frames, source.sampleRate);
        const auto fadeIn = std::min (frames / 2, std::max (note.fadeInFrames, edgeFade));
        // The fade-out ends at the note's end; when the copy was cut short, at the cut.
        const auto fadeOut = std::min (frames / 2, std::max (range.end == note.sourceEnd ? note.fadeOutFrames : 0, edgeFade));
        for (int c = 0; c < channels; ++c)
        {
            const auto& in = source.channels[static_cast<std::size_t> (c)];
            auto& dst = out.channels[static_cast<std::size_t> (c)];
            for (std::int64_t i = 0; i < frames; ++i)
            {
                float v = in[static_cast<std::size_t> (range.start + i)];
                v *= fadeGain (i, fadeIn);
                v *= fadeGain (frames - 1 - i, fadeOut);
                dst[static_cast<std::size_t> (i)] = v;
            }
        }
        return out;
    }
}

std::size_t approximateBytes (const InstrumentSet& set)
{
    std::size_t bytes = 0;
    for (const auto& m : set.members)
        if (m.model != nullptr)
            bytes += modelBytes (*m.model);
    return bytes;
}

atlas::FrameRange memberRange (const atlas::NoteEvent& note, double sampleRate, const BuildOptions& options)
{
    auto range = note.playRange();
    const auto limit = static_cast<std::int64_t> (std::max (0.01, options.maxRegionSeconds) * sampleRate);
    if (range.length() > limit)
        range.end = range.start + limit;
    return range;
}

BuildResult buildInstrumentSet (const atlas::NoteAtlas& atlas, const std::vector<SourceAudio>& sources, const BuildOptions& options)
{
    const auto t0 = std::chrono::steady_clock::now();
    BuildResult result;
    auto& report = result.report;

    const auto validation = atlas::validate (atlas);
    report.errors = validation.errors;
    report.warnings = validation.warnings;
    if (! validation.ok())
        return result;

    // Every source an included note plays must be here, and be the audio the atlas describes:
    // positions are frames, so a different length or rate would play the wrong notes.
    auto audioFor = [&sources] (const std::string& id) -> const AudioData* {
        for (const auto& s : sources)
            if (s.assetId == id && s.audio != nullptr && ! s.audio->isEmpty())
                return s.audio.get();
        return nullptr;
    };
    for (const auto& src : atlas.sources)
    {
        const bool used = std::any_of (atlas.notes.begin(), atlas.notes.end(), [&] (const auto& n) { return n.included && n.sourceId == src.assetId; });
        if (! used)
            continue;
        const auto* audio = audioFor (src.assetId);
        if (audio == nullptr)
        {
            report.missingSources.push_back (src.assetId);
            report.errors.push_back ("source " + src.assetId + " (" + src.fileName + ", " + src.contentHash + "): audio missing");
            continue;
        }
        if (audio->numFrames() != src.numFrames || std::abs (audio->sampleRate - src.sampleRate) > 1.0e-6
            || std::min (audio->numChannels(), 2) != src.numChannels)
            report.errors.push_back ("source " + src.assetId + " (" + src.fileName + "): the audio does not match the atlas (rate, channels or length)");
    }
    if (! report.errors.empty())
        return result;

    // Included notes in key, layer and variation order: members, groups and takes follow it.
    std::vector<const atlas::NoteEvent*> notes;
    for (const auto& n : atlas.notes)
        if (n.included)
            notes.push_back (&n);
    std::stable_sort (notes.begin(), notes.end(), [] (const atlas::NoteEvent* a, const atlas::NoteEvent* b) {
        return std::make_tuple (a->playKey(), a->velocityLayer, a->variationIndex, a->sourceStart)
               < std::make_tuple (b->playKey(), b->velocityLayer, b->variationIndex, b->sourceStart);
    });

    // Copy and analyse each region, within the memory limit.
    struct Region
    {
        const atlas::NoteEvent* note;
        AudioData audio;
        AnalysisData analysis;
        atlas::FrameRange range;
    };
    std::vector<Region> regions;
    double totalSeconds = 0.0;
    for (const auto* n : notes)
    {
        const auto* src = atlas.findSource (n->sourceId);
        const auto& audio = *audioFor (n->sourceId);
        const auto range = memberRange (*n, src->sampleRate, options);
        if (range.length() < n->playRange().length())
            report.warnings.push_back ("note " + n->id + ": longer than " + std::to_string (options.maxRegionSeconds) + " s, cut");
        const double seconds = static_cast<double> (range.length()) / src->sampleRate;
        if (totalSeconds + seconds > options.maxTotalSeconds)
        {
            ++report.notesSkipped;
            report.warnings.push_back ("note " + n->id + ": skipped (the atlas's region audio would pass the "
                                       + std::to_string (static_cast<int> (options.maxTotalSeconds)) + " s limit)");
            continue;
        }
        totalSeconds += seconds;
        Region r { n, copyRegion (audio, *n, range, options.edgeFadeFrames), {}, range };
        r.analysis = Analyzer::analyse (r.audio);
        report.regionFrames += range.length();
        regions.push_back (std::move (r));
    }
    if (regions.empty())
    {
        report.errors.push_back ("no playable note");
        return result;
    }

    // One playback gain for the whole atlas (its loudest region reaches the usual level), as
    // for sample sets: notes and layers keep their recorded loudness differences.
    double loudest = -200.0, peak = -200.0;
    for (const auto& r : regions)
    {
        loudest = std::max (loudest, r.analysis.envelope.maxRmsDbfs);
        peak = std::max (peak, r.analysis.envelope.peakDbfs);
    }
    const auto& playback = options.instrument.playback;
    double gainDb = 0.0;
    if (playback.normaliseLevel && loudest > -150.0)
    {
        gainDb = std::clamp (playback.targetMaxRmsDbfs - loudest, -playback.maxCutDb, playback.maxBoostDb);
        gainDb = std::min (gainDb, playback.peakCeilingDbfs - peak);
    }

    auto set = std::make_shared<InstrumentSet>();
    std::map<int, int> groupOfKey;   // play key -> group index
    std::map<std::pair<int, int>, int> takes;   // (group, layer) -> next take
    for (auto& r : regions)
    {
        const auto& n = *r.note;
        auto memberOptions = options.instrument;
        // Start exactly at the region (attackStart: the atlas placed it), one shared gain.
        memberOptions.preparationOverride = PlaybackPreparation { 0.0, gainDb };
        const double cents = atlas.mapping.correctTuning ? n.centsOffset / 100.0 : 0.0;
        memberOptions.rootOverrideMidi = static_cast<double> (n.playKey()) + cents;
        auto model = instrument::buildProvisional (r.audio, r.analysis, memberOptions);
        if (options.withContinuation)
            model = instrument::addContinuation (*model, r.audio, memberOptions);
        model->stage = InstrumentModel::Stage::complete;

        const auto* src = atlas.findSource (n.sourceId);
        auto m = makeSetMember (std::move (model), src->fileName + "#" + n.id);
        m.regionId = n.id;
        m.sourceAssetId = n.sourceId;
        m.regionStart = r.range.start;
        m.regionEnd = r.range.end;
        m.userAssigned = n.origin == atlas::NoteOrigin::user || n.corrections.any();
        m.confidence = std::clamp (n.origin == atlas::NoteOrigin::user ? 1.0 : n.confidence, 0.0, 1.0);
        const int key = n.playKey();
        auto [it, added] = groupOfKey.emplace (key, static_cast<int> (set->groups.size()));
        if (added)
        {
            PitchGroup g;
            g.rootMidi = static_cast<double> (key);
            set->groups.push_back (g);
        }
        m.pitchGroup = it->second;
        m.layer = n.velocityLayer;
        m.take = takes[{ m.pitchGroup, m.layer }]++;
        set->groups[static_cast<std::size_t> (m.pitchGroup)].members.push_back (static_cast<int> (set->members.size()));
        set->members.push_back (std::move (m));
    }
    // Roles and layer counts from the places (no inference).
    for (auto& g : set->groups)
    {
        int layers = 1;
        for (int id : g.members)
            layers = std::max (layers, set->members[static_cast<std::size_t> (id)].layer + 1);
        g.layers = layers;
        for (int id : g.members)
        {
            auto& m = set->members[static_cast<std::size_t> (id)];
            m.role = layers > 1 ? SampleRole::velocityLayer : (g.members.size() > 1 ? SampleRole::roundRobin : SampleRole::pitchAnchor);
        }
    }
    // Groups were created in key order (the notes are sorted by key).
    set->fill = atlas.mapping.fill;
    set->fillLimitSemitones = std::max (0.0, atlas.mapping.maxTransposeSemitones);
    finaliseSampleSet (*set);

    report.notesUsed = static_cast<int> (set->members.size());
    report.memberBytes = approximateBytes (*set);
    report.ok = set->isValid();
    report.buildMilliseconds = std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - t0).count();
    if (report.ok)
        result.set = std::move (set);
    else
        report.errors.push_back ("the built set is not playable");
    return result;
}

} // namespace osp::atlasset
