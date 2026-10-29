#include "engine/AtlasSet.h"

#include "analysis/Analyzer.h"
#include "engine/SampleSetInference.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <map>
#include <numbers>
#include <thread>
#include <tuple>

namespace osp::atlasset
{

namespace
{
    double msSince (std::chrono::steady_clock::time_point t)
    {
        return std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - t).count();
    }

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

struct PreparedAtlas
{
    struct Region
    {
        atlas::NoteEvent note;                            ///< a copy: Stage B outlives the caller's atlas
        std::string fileName;                             ///< its source's file name (member names)
        atlas::FrameRange range;
        std::shared_ptr<const AudioData> audio;           ///< the region copy
        InstrumentBuildOptions options;                   ///< root, start and the shared gain
        std::shared_ptr<const InstrumentModel> playable;  ///< Stage A's model
    };
    std::vector<Region> regions;                          ///< in member order
    bool fill = false;
    double fillLimitSemitones = 0.0;
    BuildReport report;                                   ///< Stage A's (warnings, skipped and cut notes)
};

namespace
{
    /** Runs work(i) for i in [0, count) on up to `threads` threads (this one included); stops
        handing out work once `cancel` is set. Each i is independent, so the result does not
        depend on the thread count. */
    template <typename Work>
    void forEachMember (std::size_t count, int threads, const std::atomic<bool>* cancel, std::atomic<float>* progress, Work&& work)
    {
        std::atomic<std::size_t> next { 0 }, done { 0 };
        auto run = [&] {
            for (;;)
            {
                if (cancel != nullptr && cancel->load (std::memory_order_relaxed))
                    return;
                const auto i = next.fetch_add (1);
                if (i >= count)
                    return;
                work (i);
                const auto finished = done.fetch_add (1) + 1;
                if (progress != nullptr)
                    progress->store (static_cast<float> (finished) / static_cast<float> (count), std::memory_order_relaxed);
            }
        };
        const auto extra = static_cast<std::size_t> (std::clamp (threads, 1, 8) - 1);
        std::vector<std::thread> pool;
        for (std::size_t t = 0; t < std::min (extra, count > 0 ? count - 1 : 0); ++t)
            pool.emplace_back (run);
        run();
        for (auto& t : pool)
            t.join();
    }

    bool cancelled (const BuildOptions& options)
    {
        return options.cancel != nullptr && options.cancel->load (std::memory_order_relaxed);
    }

    /** The set from the prepared regions and one model per region: places, roles, FILL and
        levelling. Stage A and Stage B both build through here, so they differ only in the models. */
    std::shared_ptr<InstrumentSet> assemble (const PreparedAtlas& prepared, const std::vector<std::shared_ptr<const InstrumentModel>>& models)
    {
        auto set = std::make_shared<InstrumentSet>();
        std::map<int, int> groupOfKey;   // play key -> group index
        std::map<std::pair<int, int>, int> takes;   // (group, layer) -> next take
        for (std::size_t i = 0; i < prepared.regions.size(); ++i)
        {
            const auto& r = prepared.regions[i];
            const auto& n = r.note;
            auto m = makeSetMember (models[i], r.fileName + "#" + n.id);
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
        set->fill = prepared.fill;
        set->fillLimitSemitones = prepared.fillLimitSemitones;
        finaliseSampleSet (*set);
        return set;
    }

    BuildResult finish (std::shared_ptr<InstrumentSet> set, BuildReport report, std::chrono::steady_clock::time_point t0)
    {
        BuildResult result;
        report.notesUsed = static_cast<int> (set->members.size());
        report.memberBytes = approximateBytes (*set);
        report.ok = set->isValid();
        report.buildMilliseconds = msSince (t0);
        if (report.ok)
            result.set = std::move (set);
        else
            report.errors.push_back ("the built set is not playable");
        result.report = std::move (report);
        return result;
    }

    std::vector<const atlas::NoteEvent*> includedInMemberOrder (const atlas::NoteAtlas& atlas)
    {
        // Included notes in key, layer and variation order: members, groups and takes follow it.
        std::vector<const atlas::NoteEvent*> notes;
        for (const auto& n : atlas.notes)
            if (n.included)
                notes.push_back (&n);
        std::stable_sort (notes.begin(), notes.end(), [] (const atlas::NoteEvent* a, const atlas::NoteEvent* b) {
            return std::make_tuple (a->playKey(), a->velocityLayer, a->variationIndex, a->sourceStart)
                   < std::make_tuple (b->playKey(), b->velocityLayer, b->variationIndex, b->sourceStart);
        });
        return notes;
    }
}

double regionSeconds (const atlas::NoteAtlas& atlas, const BuildOptions& options)
{
    double seconds = 0.0;
    for (const auto* n : includedInMemberOrder (atlas))
        if (const auto* src = atlas.findSource (n->sourceId); src != nullptr && src->sampleRate > 0.0)
            seconds += static_cast<double> (memberRange (*n, src->sampleRate, options).length()) / src->sampleRate;
    return seconds;
}

StagedBuild buildPlayableSet (const atlas::NoteAtlas& atlas, const std::vector<SourceAudio>& sources, const BuildOptions& options)
{
    const auto t0 = std::chrono::steady_clock::now();
    StagedBuild staged;
    auto& report = staged.playable.report;
    report.playableOnly = true;

    const auto validation = atlas::validate (atlas);
    report.errors = validation.errors;
    report.warnings = validation.warnings;
    if (! validation.ok())
        return staged;

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
        return staged;

    // Copy each region, within the capacity limit (never silently: skipped and cut notes are
    // listed and the report says the build is partial).
    auto prepared = std::make_shared<PreparedAtlas>();
    double totalSeconds = 0.0;
    const auto copyStart = std::chrono::steady_clock::now();
    for (const auto* n : includedInMemberOrder (atlas))
    {
        const auto* src = atlas.findSource (n->sourceId);
        const auto& audio = *audioFor (n->sourceId);
        const auto range = memberRange (*n, src->sampleRate, options);
        const double seconds = static_cast<double> (range.length()) / src->sampleRate;
        if (totalSeconds + seconds > options.maxTotalSeconds)
        {
            ++report.notesSkipped;
            report.skippedNoteIds.push_back (n->id);
            report.warnings.push_back ("note " + n->id + ": skipped (the atlas's region audio would pass the "
                                       + std::to_string (static_cast<int> (options.maxTotalSeconds)) + " s limit)");
            continue;
        }
        if (range.length() < n->playRange().length())
        {
            report.cutNoteIds.push_back (n->id);
            report.warnings.push_back ("note " + n->id + ": longer than " + std::to_string (options.maxRegionSeconds) + " s, cut");
        }
        totalSeconds += seconds;
        PreparedAtlas::Region r;
        r.note = *n;
        r.fileName = src->fileName;
        r.range = range;
        r.audio = std::make_shared<const AudioData> (copyRegion (audio, *n, range, options.edgeFadeFrames));
        report.regionFrames += range.length();
        prepared->regions.push_back (std::move (r));
    }
    report.copyMilliseconds = msSince (copyStart);
    report.partial = ! report.skippedNoteIds.empty() || ! report.cutNoteIds.empty();
    if (report.partial)
        report.warnings.push_back ("partial instrument: " + std::to_string (report.skippedNoteIds.size()) + " note(s) left out, "
                                   + std::to_string (report.cutNoteIds.size()) + " cut (the atlas keeps them all)");
    if (prepared->regions.empty())
    {
        report.errors.push_back ("no playable note");
        return staged;
    }

    // Each region's analysis (independent: in parallel when allowed).
    std::vector<AnalysisData> analyses (prepared->regions.size());
    const auto analysisStart = std::chrono::steady_clock::now();
    forEachMember (analyses.size(), options.threads, options.cancel, nullptr,
                   [&] (std::size_t i) { analyses[i] = Analyzer::analyse (*prepared->regions[i].audio); });
    report.analysisMilliseconds = msSince (analysisStart);
    if (cancelled (options))
    {
        report.cancelled = true;
        report.errors.push_back ("cancelled");
        return staged;
    }

    // One playback gain for the whole atlas (its loudest region reaches the usual level), as
    // for sample sets: notes and layers keep their recorded loudness differences.
    double loudest = -200.0, peak = -200.0;
    for (const auto& a : analyses)
    {
        loudest = std::max (loudest, a.envelope.maxRmsDbfs);
        peak = std::max (peak, a.envelope.peakDbfs);
    }
    const auto& playback = options.instrument.playback;
    double gainDb = 0.0;
    if (playback.normaliseLevel && loudest > -150.0)
    {
        gainDb = std::clamp (playback.targetMaxRmsDbfs - loudest, -playback.maxCutDb, playback.maxBoostDb);
        gainDb = std::min (gainDb, playback.peakCeilingDbfs - peak);
    }

    const auto provisionalStart = std::chrono::steady_clock::now();
    std::vector<std::shared_ptr<const InstrumentModel>> models;
    for (std::size_t i = 0; i < prepared->regions.size(); ++i)
    {
        auto& r = prepared->regions[i];
        r.options = options.instrument;
        // Start exactly at the region (attackStart: the atlas placed it), one shared gain.
        r.options.preparationOverride = PlaybackPreparation { 0.0, gainDb };
        const double cents = atlas.mapping.correctTuning ? r.note.centsOffset / 100.0 : 0.0;
        r.options.rootOverrideMidi = static_cast<double> (r.note.playKey()) + cents;
        r.playable = instrument::buildProvisional (*r.audio, analyses[i], r.options);
        models.push_back (r.playable);
    }
    report.provisionalMilliseconds = msSince (provisionalStart);
    prepared->fill = atlas.mapping.fill;
    prepared->fillLimitSemitones = std::max (0.0, atlas.mapping.maxTransposeSemitones);

    const auto finaliseStart = std::chrono::steady_clock::now();
    auto set = assemble (*prepared, models);
    report.finaliseMilliseconds = msSince (finaliseStart);
    staged.playable = finish (std::move (set), report, t0);
    prepared->report = staged.playable.report;
    if (staged.playable.set != nullptr)
        staged.pending = std::move (prepared);
    return staged;
}

BuildResult enhanceSet (const PreparedAtlas& prepared, const BuildOptions& options)
{
    const auto t0 = std::chrono::steady_clock::now();
    auto report = prepared.report;
    report.playableOnly = false;
    std::vector<std::shared_ptr<const InstrumentModel>> models (prepared.regions.size());
    forEachMember (models.size(), options.threads, options.cancel, options.progress, [&] (std::size_t i) {
        const auto& r = prepared.regions[i];
        auto model = instrument::addContinuation (*r.playable, *r.audio, r.options);
        model->stage = InstrumentModel::Stage::complete;
        models[i] = std::move (model);
    });
    report.continuationMilliseconds = msSince (t0);
    if (cancelled (options))
    {
        BuildResult result;
        report.ok = false;
        report.cancelled = true;
        report.errors.push_back ("cancelled");
        result.report = std::move (report);
        return result;
    }
    const auto finaliseStart = std::chrono::steady_clock::now();
    auto set = assemble (prepared, models);
    report.finaliseMilliseconds += msSince (finaliseStart);
    auto result = finish (std::move (set), report, t0);
    // The whole preparation: Stage A's time plus this stage's.
    result.report.buildMilliseconds += prepared.report.buildMilliseconds;
    return result;
}

BuildResult buildInstrumentSet (const atlas::NoteAtlas& atlas, const std::vector<SourceAudio>& sources, const BuildOptions& options)
{
    const auto t0 = std::chrono::steady_clock::now();
    auto staged = buildPlayableSet (atlas, sources, options);
    if (staged.pending == nullptr)
        return staged.playable;
    if (options.withContinuation)
        return enhanceSet (*staged.pending, options);
    // A playable-only build that is not followed by Stage B is finished as it is.
    std::vector<std::shared_ptr<const InstrumentModel>> models;
    for (const auto& r : staged.pending->regions)
    {
        auto model = std::make_shared<InstrumentModel> (*r.playable);
        model->stage = InstrumentModel::Stage::complete;
        models.push_back (std::move (model));
    }
    auto report = staged.playable.report;
    report.playableOnly = false;
    return finish (assemble (*staged.pending, models), report, t0);
}

} // namespace osp::atlasset
