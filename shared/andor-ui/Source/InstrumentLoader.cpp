#include "InstrumentLoader.h"

#include "analysis/Analyzer.h"
#include "core/Fft.h"
#include "engine/AtlasSet.h"
#include "engine/InstrumentBuilder.h"
#include "io/AnalysisJson.h"
#include "io/AudioFileIO.h"
#include "io/ContentHash.h"
#include "io/NoteAtlasJson.h"
#include "model/PlaybackPreparation.h"
#include "model/RootChoice.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdlib>
#include <mutex>

namespace osp::plugin
{

namespace
{
    std::filesystem::path toPath (const juce::File& f)
    {
        return std::filesystem::path (f.getFullPathName().toStdString());
    }

    constexpr int waveformBuckets = 8192;   // under a period per bucket: peaks keep their texture
    constexpr int playbackZeroCrossings = 16; // must match the processor's EngineSettings
}

SampleStore::SampleStore (juce::File directory) : dir (std::move (directory)) {}

juce::File SampleStore::defaultDirectory()
{
    if (const char* overridePath = std::getenv ("OSP_SAMPLE_STORE"); overridePath != nullptr && *overridePath != 0)
        return juce::File (juce::String::fromUTF8 (overridePath));

#if JUCE_MAC
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
        .getChildFile ("Application Support/OSP/Samples");
#else
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("OSP/Samples");
#endif
}

std::optional<juce::File> SampleStore::find (const std::string& sha256Hex) const
{
    if (sha256Hex.empty() || ! dir.isDirectory())
        return std::nullopt;
    for (const auto& entry : juce::RangedDirectoryIterator (dir, false, juce::String (sha256Hex) + ".*"))
    {
        const auto file = entry.getFile();
        if (! file.getFileName().endsWith (".analysis.json"))
            return file;
    }
    return std::nullopt;
}

juce::File SampleStore::analysisCacheFor (const std::string& sha256Hex) const
{
    return dir.getChildFile (juce::String (sha256Hex) + ".analysis.json");
}

std::optional<juce::File> SampleStore::import (const juce::File& file, const std::string& sha256Hex, std::string& error)
{
    if (auto existing = find (sha256Hex))
        return existing;

    if (! dir.createDirectory())
    {
        error = "cannot create sample store at " + dir.getFullPathName().toStdString();
        return std::nullopt;
    }

    // Copy to a temporary name first so a crash never leaves a truncated file under the hash name.
    const auto target = dir.getChildFile (juce::String (sha256Hex) + file.getFileExtension().toLowerCase());
    const auto temp = target.getSiblingFile (target.getFileName() + ".partial");
    if (! file.copyFileTo (temp) || ! temp.moveFileTo (target))
    {
        temp.deleteFile();
        error = "cannot copy " + file.getFileName().toStdString() + " into the sample store";
        return std::nullopt;
    }
    return target;
}

namespace
{
    /** Steps shared by single files and sets: locate, hash, decode, store, analyse. */
    struct Imported
    {
        bool ok = false;
        std::string error;
        std::vector<std::string> warnings;
        std::string hash;
        std::string filename;
        std::string originalPath;
        std::string storedPath;
        AudioData audio;
        AnalysisData analysis;
    };

    Imported importAndAnalyse (const LoadRequest& request, SampleStore& store)
    {
        Imported result;
        // 1. Locate the audio: store (by hash) first for recall, else the given file.
        juce::File source = request.file;
        std::string hash;
        if (! request.expectedHash.empty())
        {
            hash = request.expectedHash.rfind ("sha256:", 0) == 0 ? request.expectedHash.substr (7) : request.expectedHash;
            if (auto stored = store.find (hash))
                source = *stored;
            else if (request.originalPath.empty() || ! juce::File (request.originalPath).existsAsFile())
            {
                result.error = "sample " + (request.filename.empty() ? hash.substr (0, 12) : request.filename)
                               + " is not in the sample store and its original file is missing";
                return result;
            }
            else
                source = juce::File (request.originalPath);
        }

        if (! source.existsAsFile())
        {
            result.error = "file not found: " + source.getFullPathName().toStdString();
            return result;
        }

        const auto actualHash = io::sha256OfFile (toPath (source));
        if (! actualHash)
        {
            result.error = "cannot read " + source.getFileName().toStdString();
            return result;
        }
        if (! hash.empty() && *actualHash != hash)
            result.warnings.push_back ("the original file has changed since the session was saved");
        hash = *actualHash;

        // 2. Decode (fails cleanly for unsupported/malformed files before anything is stored).
        auto decoded = io::loadAudioFile (toPath (source));
        if (! decoded.ok)
        {
            result.error = decoded.error;
            return result;
        }

        // 3. Keep a managed copy so the session survives the original moving.
        std::string error;
        auto stored = store.import (source, hash, error);
        if (! stored)
            result.warnings.push_back (error);

        result.hash = hash;
        result.filename = request.filename.empty() ? request.file.getFileName().toStdString() : request.filename;
        result.originalPath = request.originalPath.empty() ? request.file.getFullPathName().toStdString() : request.originalPath;
        result.storedPath = stored ? stored->getFullPathName().toStdString() : std::string();

        // 4. Analysis: cached if compatible, else analyse and cache.
        const auto cache = store.analysisCacheFor (hash);
        std::optional<AnalysisData> analysis;
        if (cache.existsAsFile())
        {
            analysis = io::readAnalysis (toPath (cache), error);
            if (analysis && analysis->analyzer != analyzerVersion)
                analysis.reset(); // analyser improved since: re-analyse
        }
        if (! analysis)
        {
            analysis = Analyzer::analyse (decoded.audio);
            analysis->source.filename = result.filename;
            analysis->source.path = result.originalPath;
            analysis->source.contentHash = io::contentId (hash);
            analysis->source.format = decoded.info.formatName;
            analysis->source.bitDepth = decoded.info.bitDepth;
            analysis->source.isFloatingPoint = decoded.info.isFloatingPoint;
            analysis->source.channels = decoded.info.channels;
            analysis->source.truncated = decoded.truncated;
            analysis->warnings.insert (analysis->warnings.begin(), decoded.warnings.begin(), decoded.warnings.end());
            if (store.directory().isDirectory())
                io::writeAnalysis (toPath (cache), *analysis, error);
            // Build from exactly what the cache will give back on recall (rounded JSON),
            // so a reopened session derives the identical model and sounds identical.
            if (auto roundTripped = io::analysisFromJson (io::analysisToJson (*analysis), error))
                analysis = std::move (roundTripped);
        }
        result.analysis = std::move (*analysis);
        result.audio = std::move (decoded.audio);
        result.ok = true;
        return result;
    }

    void computeOverview (LoadedInstrument& instrument, const AudioData& audio)
    {
        const auto mono = audio.mixToMono();
        const auto buckets = static_cast<std::size_t> (std::min<std::int64_t> (waveformBuckets, std::max<std::int64_t> (1, audio.numFrames())));
        instrument.peakMin.assign (buckets, 0.0f);
        instrument.peakMax.assign (buckets, 0.0f);
        instrument.peakRms.assign (buckets, 0.0f);
        instrument.peakBright.assign (buckets, 0.0f);
        for (std::size_t b = 0; b < buckets; ++b)
        {
            const auto start = b * mono.size() / buckets;
            const auto end = std::max (start + 1, (b + 1) * mono.size() / buckets);
            float lo = 0.0f, hi = 0.0f;
            double energy = 0.0, motion = 0.0;
            std::size_t n = 0;
            for (auto i = start; i < end && i < mono.size(); ++i, ++n)
            {
                lo = std::min (lo, mono[i]);
                hi = std::max (hi, mono[i]);
                energy += static_cast<double> (mono[i]) * mono[i];
                if (i > 0)
                {
                    const double d = static_cast<double> (mono[i]) - mono[i - 1];
                    motion += d * d;
                }
            }
            instrument.peakMin[b] = lo;
            instrument.peakMax[b] = hi;
            const double rms = std::sqrt (energy / static_cast<double> (std::max<std::size_t> (1, n)));
            instrument.peakRms[b] = static_cast<float> (rms);
            // Brightness: the first difference's RMS against the signal's (about 2 pi f / sr
            // for a sine), on a log scale from dull (0.02) to bright (0.6).
            const double ratio = std::sqrt (motion / std::max (energy, 1.0e-20));
            instrument.peakBright[b] = rms > 1.0e-5
                                           ? static_cast<float> (std::clamp ((std::log2 (std::max (ratio, 1.0e-6)) - std::log2 (0.02)) / (std::log2 (0.6) - std::log2 (0.02)), 0.0, 1.0))
                                           : 0.0f;
        }
        // Onsets (the display's transient detail): how far the level just here (the loudest
        // bucket of ~3 ms, longer than a low note's period) rises above the ~12 ms before it,
        // in dB: 0 at no rise, 1 at 18 dB or more.
        instrument.peakFlux.assign (buckets, 0.0f);
        {
            const double bucketSeconds = audio.durationSeconds() / static_cast<double> (buckets);
            auto span = [bucketSeconds] (double seconds) { return static_cast<std::size_t> (std::clamp (seconds / std::max (bucketSeconds, 1.0e-9), 1.0, 256.0)); };
            const auto here = span (0.003), before = span (0.012);
            std::vector<double> db (buckets);
            for (std::size_t b = 0; b < buckets; ++b)
                db[b] = 20.0 * std::log10 (std::max (1.0e-6, static_cast<double> (instrument.peakRms[b])));
            for (std::size_t b = here + 1; b < buckets; ++b)
            {
                double now = -120.0, earlier = -120.0;
                for (std::size_t k = 0; k < here; ++k)
                    now = std::max (now, db[b - k]);
                for (std::size_t k = here; k < here + before && k <= b; ++k)
                    earlier = std::max (earlier, db[b - k]);
                instrument.peakFlux[b] = static_cast<float> (std::clamp ((now - earlier) / 18.0, 0.0, 1.0));
            }
        }
        instrument.durationSeconds = audio.durationSeconds();

        // The sound's average spectrum on a log frequency axis (CHARACTER's display): up to
        // 48 Hann frames of 4096 across the recording, power-averaged, peak at 0 dB.
        instrument.spectrumDb.assign (LoadedInstrument::spectrumBins, -90.0f);
        const int order = 12, size = 1 << order;
        if (static_cast<std::int64_t> (mono.size()) >= size && audio.sampleRate > 0.0)
        {
            const Fft fft (order);
            std::vector<std::complex<double>> frame (static_cast<std::size_t> (size));
            std::vector<double> power (static_cast<std::size_t> (size / 2), 0.0);
            const auto frames = std::min<std::size_t> (48, mono.size() / static_cast<std::size_t> (size / 2));
            const auto span = mono.size() - static_cast<std::size_t> (size);
            for (std::size_t f = 0; f < frames; ++f)
            {
                const auto start = frames > 1 ? f * span / (frames - 1) : 0;
                for (int i = 0; i < size; ++i)
                {
                    const double w = 0.5 - 0.5 * std::cos (2.0 * 3.141592653589793 * i / size);
                    frame[static_cast<std::size_t> (i)] = { w * mono[start + static_cast<std::size_t> (i)], 0.0 };
                }
                fft.forward (frame.data());
                for (std::size_t k = 0; k < power.size(); ++k)
                    power[k] += std::norm (frame[k]);
            }
            double peak = 1.0e-30;
            std::vector<double> bins (LoadedInstrument::spectrumBins, 0.0);
            for (int b = 0; b < LoadedInstrument::spectrumBins; ++b)
            {
                // Bin b covers 20 Hz * 1000^(b/bins) .. the next one.
                const double lo = 20.0 * std::pow (1000.0, static_cast<double> (b) / LoadedInstrument::spectrumBins);
                const double hi = 20.0 * std::pow (1000.0, static_cast<double> (b + 1) / LoadedInstrument::spectrumBins);
                const auto k0 = static_cast<std::size_t> (std::clamp (lo * size / audio.sampleRate, 1.0, static_cast<double> (power.size() - 1)));
                const auto k1 = static_cast<std::size_t> (std::clamp (hi * size / audio.sampleRate, static_cast<double> (k0 + 1), static_cast<double> (power.size())));
                double sum = 0.0;
                for (auto k = k0; k < k1; ++k)
                    sum = std::max (sum, power[k]);
                bins[static_cast<std::size_t> (b)] = sum;
                peak = std::max (peak, sum);
            }
            for (int b = 0; b < LoadedInstrument::spectrumBins; ++b)
                instrument.spectrumDb[static_cast<std::size_t> (b)] = static_cast<float> (std::max (-90.0, 10.0 * std::log10 (std::max (1.0e-30, bins[static_cast<std::size_t> (b)] / peak))));
        }
    }
}

LoadResult loadInstrument (const LoadRequest& request, SampleStore& store, std::uint64_t generation)
{
    LoadResult result;
    try
    {
        auto imported = importAndAnalyse (request, store);
        result.warnings = imported.warnings;
        if (! imported.ok)
        {
            result.error = imported.error;
            return result;
        }
        auto instrument = std::make_shared<LoadedInstrument>();
        instrument->generation = generation;
        instrument->contentHash = io::contentId (imported.hash);
        instrument->filename = imported.filename;
        instrument->originalPath = imported.originalPath;
        instrument->storedPath = imported.storedPath;
        instrument->analysis = std::move (imported.analysis);
        auto& decoded = imported;

        // 5. Playback data.
        auto root = chooseRoot (&instrument->analysis, std::nullopt);
        if (request.savedPlayback)
        {
            root.rootMidi = request.savedPlayback->rootMidi;
            root.origin = request.savedPlayback->rootOrigin;
        }
        instrument->analysisRootMidi = root.rootMidi;
        instrument->rootOrigin = root.origin;
        // The instrument starts notes at the sound (not at sample 0) and plays every
        // source at a comparable level; both are non-destructive (corpus-run-1 findings).
        PlaybackOptions playbackOptions;
        playbackOptions.startAtOnset = true;
        playbackOptions.normaliseLevel = true;
        auto preparation = preparePlayback (instrument->analysis, playbackOptions);
        if (request.savedPlayback)
            preparation = { request.savedPlayback->startSeconds, request.savedPlayback->gainDb };
        instrument->startSeconds = preparation.startSeconds;
        instrument->playbackGainDb = preparation.gainDb;
        InstrumentBuildOptions options;
        options.interpolationZeroCrossings = playbackZeroCrossings;
        options.rootOverrideMidi = root.rootMidi;
        options.preparationOverride = preparation;
        instrument->model = instrument::buildProvisional (decoded.audio, instrument->analysis, options);
        instrument->loadId = generation;
        instrument->character = describeCharacter (instrument->analysis);
        computeOverview (*instrument, decoded.audio);

        result.instrument = std::move (instrument);
        result.audio = std::make_shared<const AudioData> (std::move (decoded.audio));
    }
    catch (const std::exception& e)
    {
        result.error = std::string ("unexpected error while loading: ") + e.what();
    }
    catch (...)
    {
        result.error = "unexpected error while loading";
    }
    return result;
}

LoadResult refineInstrument (const LoadedInstrument& base, std::shared_ptr<const AudioData> audio, std::uint64_t generation)
{
    LoadResult result;
    try
    {
        if (audio == nullptr || base.model == nullptr)
        {
            result.error = "nothing to refine";
            return result;
        }
        InstrumentBuildOptions options;
        options.interpolationZeroCrossings = playbackZeroCrossings;
        auto instrument = std::make_shared<LoadedInstrument> (base);
        instrument->generation = generation;
        if (base.model->stage == InstrumentModel::Stage::provisional)
            instrument->model = instrument::addContinuation (*base.model, *audio, options);
        else
            instrument->model = instrument::addAnchors (*base.model, *audio, options);
        result.instrument = std::move (instrument);
        result.audio = std::move (audio);
    }
    catch (const std::exception& e)
    {
        result.error = std::string ("unexpected error while preparing the instrument: ") + e.what();
    }
    catch (...)
    {
        result.error = "unexpected error while preparing the instrument";
    }
    return result;
}

LoadResult loadInstrumentSet (const SetLoadRequest& request, SampleStore& store, std::uint64_t generation)
{
    LoadResult result;
    try
    {
        std::vector<Imported> imported;
        for (const auto& file : request.files)
        {
            auto one = importAndAnalyse (file, store);
            result.warnings.insert (result.warnings.end(), one.warnings.begin(), one.warnings.end());
            if (one.ok)
                imported.push_back (std::move (one));
            else
                result.warnings.push_back (one.error);
        }
        if (imported.empty())
        {
            result.error = "none of the files could be loaded";
            return result;
        }
        std::vector<instrument::SetSource> sources;
        for (const auto& one : imported)
            sources.push_back ({ &one.audio, &one.analysis, one.filename });
        InstrumentBuildOptions options;
        options.interpolationZeroCrossings = playbackZeroCrossings;
        auto set = std::make_shared<InstrumentSet> (instrument::buildSet (sources, options, request.assignments, true));
        if (! set->isValid())
        {
            result.error = "the files could not be combined into an instrument";
            return result;
        }

        auto instrument = std::make_shared<LoadedInstrument>();
        instrument->generation = generation;
        instrument->loadId = generation;
        instrument->assignments = request.assignments;
        // Member identities in the set's member order (inference keeps input order).
        for (const auto& member : set->members)
            for (const auto& one : imported)
                if (one.filename == member.filename)
                {
                    instrument->memberFiles.push_back ({ io::contentId (one.hash), one.filename, one.originalPath });
                    break;
                }
        const auto& primaryMember = set->members[static_cast<std::size_t> (set->primary)];
        const Imported* primary = &imported.front();
        for (const auto& one : imported)
            if (one.filename == primaryMember.filename)
                primary = &one;
        instrument->contentHash = io::contentId (primary->hash);
        instrument->filename = std::to_string (set->members.size()) + " samples";
        instrument->originalPath = primary->originalPath;
        instrument->storedPath = primary->storedPath;
        instrument->analysis = primary->analysis;
        instrument->analysisRootMidi = primaryMember.model->rootMidi;
        instrument->rootOrigin = "set";
        instrument->startSeconds = primaryMember.model->playback.startSeconds;
        instrument->playbackGainDb = primaryMember.model->playback.gainDb;
        instrument->model = primaryMember.model;
        instrument->set = std::move (set);
        instrument->character = describeCharacter (primary->analysis);
        computeOverview (*instrument, primary->audio);
        result.instrument = std::move (instrument);
    }
    catch (const std::exception& e)
    {
        result.error = std::string ("unexpected error while loading the set: ") + e.what();
    }
    catch (...)
    {
        result.error = "unexpected error while loading the set";
    }
    return result;
}

namespace
{
    /** The analysis side of an atlas's status (counts, verdict, version). */
    void describeAtlas (AtlasStatus& status, const atlas::NoteAtlas& a)
    {
        status.analysed = a.analysis.version > 0;
        status.olderAnalysis = status.analysed && a.analysis.version < phrase::analysisVersion;
        status.suitability = a.analysis.suitability;
        for (const auto& n : a.notes)
        {
            if (! n.included)
            {
                ++status.leftOut;
                continue;
            }
            ++status.notes;
            status.doubtfulNotes += n.diagnostics.any() ? 1 : 0;
        }
    }

    /** The layer's instrument for an atlas set (Stage A or B): metadata from its primary member. */
    std::shared_ptr<LoadedInstrument> makeAtlasInstrument (const atlasset::BuildResult& built, std::shared_ptr<const atlas::NoteAtlas> atlas,
                                                           std::vector<LoadedInstrument::MemberFile> sources, std::uint64_t generation,
                                                           const AudioData* overviewAudio, std::shared_ptr<AtlasStatus> status)
    {
        const auto& set = *built.set;
        const auto& primary = set.members[static_cast<std::size_t> (set.primary)];
        auto instrument = std::make_shared<LoadedInstrument>();
        instrument->generation = generation;
        instrument->loadId = generation;
        const auto* primarySource = atlas->findSource (primary.sourceAssetId);
        instrument->contentHash = primarySource != nullptr ? primarySource->contentHash : std::string();
        instrument->filename = std::to_string (set.members.size()) + " notes" + (primarySource != nullptr ? " · " + primarySource->fileName : std::string());
        instrument->originalPath = primarySource != nullptr ? primarySource->lastKnownPath : std::string();
        instrument->analysis = primary.model->analysis;
        instrument->analysisRootMidi = primary.model->rootMidi;
        instrument->rootOrigin = "atlas";
        instrument->startSeconds = 0.0;
        instrument->playbackGainDb = primary.model->playback.gainDb;
        instrument->model = primary.model;
        instrument->set = built.set;
        instrument->atlas = std::move (atlas);
        instrument->atlasSources = std::move (sources);
        instrument->character = describeCharacter (instrument->analysis);
        const auto& r = built.report;
        status->playableOnly = r.playableOnly;
        status->partial = r.partial;
        status->skippedNoteIds = r.skippedNoteIds;
        status->cutNoteIds = r.cutNoteIds;
        status->regionSeconds = primarySource != nullptr && primarySource->sampleRate > 0.0 ? static_cast<double> (r.regionFrames) / primarySource->sampleRate : 0.0;
        status->memberBytes = r.memberBytes;
        status->buildMilliseconds = r.buildMilliseconds;
        instrument->atlasStatus = std::move (status);
        if (overviewAudio != nullptr)
            computeOverview (*instrument, *overviewAudio);
        return instrument;
    }
}

LoadResult loadInstrumentAtlas (const AtlasLoadRequest& request, SampleStore& store, std::uint64_t generation)
{
    LoadResult result;
    try
    {
        const auto& atlas = request.atlas;
        if (const auto report = atlas::validate (atlas); ! report.ok())
        {
            result.error = "the Note Atlas is not valid: " + report.errors.front();
            return result;
        }
        std::vector<atlasset::SourceAudio> sources;
        const AudioData* primaryAudio = nullptr;
        // The instrument keeps the atlas as found: a source relinked through its hints keeps
        // its new location (and, found by shape only, its new fingerprint), as io::relinkSource.
        auto resolvedAtlas = std::make_shared<atlas::NoteAtlas> (atlas);
        std::vector<LoadedInstrument::MemberFile> instrumentSources;
        for (const auto& src : atlas.sources)
        {
            const bool used = std::any_of (atlas.notes.begin(), atlas.notes.end(), [&] (const auto& n) { return n.included && n.sourceId == src.assetId; });
            if (! used)
                continue;
            const auto describe = src.assetId + ": " + (src.fileName.empty() ? std::string ("(unnamed)") : src.fileName) + " (" + src.contentHash + ")";
            // 1. By content hash in the managed store, then the atlas's own hints.
            const auto hash = src.contentHash.rfind ("sha256:", 0) == 0 ? src.contentHash.substr (7) : src.contentHash;
            std::optional<std::filesystem::path> found;
            bool exact = false, fromStore = false;
            if (auto stored = store.find (hash))
            {
                found = toPath (*stored);
                exact = fromStore = true;
            }
            else
            {
                std::vector<std::filesystem::path> search;
                for (const auto& f : request.searchFolders)
                    search.emplace_back (f);
                if (auto resolved = io::resolveSource (src, std::filesystem::path (request.atlasFolder), search))
                {
                    found = resolved->path;
                    exact = resolved->match == atlas::RelinkResult::exact;
                }
            }
            if (! found)
            {
                result.missingSources.push_back (describe);
                continue;
            }
            // 2. Decode and check the shape: positions are frames.
            auto decoded = io::loadAudioFile (*found);
            if (! decoded.ok)
            {
                result.missingSources.push_back (describe + ": " + decoded.error);
                continue;
            }
            if (decoded.audio.numFrames() != src.numFrames || std::abs (decoded.audio.sampleRate - src.sampleRate) > 1.0e-6
                || std::min (decoded.audio.numChannels(), 2) != src.numChannels)
            {
                result.missingSources.push_back (describe + ": the file found does not match the recording (rate, channels or length)");
                continue;
            }
            if (! exact)
                result.warnings.push_back ("source " + describe + " was found by its shape, not its exact content");
            // 3. Keep a managed copy (by its own hash) so the session survives the file moving.
            const auto fileHash = io::sha256OfFile (*found);
            if (fileHash)
            {
                std::string error;
                if (! store.import (juce::File (juce::String (found->string())), *fileHash, error))
                    result.warnings.push_back (error);
            }
            auto kept = std::find_if (resolvedAtlas->sources.begin(), resolvedAtlas->sources.end(), [&] (const auto& s) { return s.assetId == src.assetId; });
            if (! fromStore)
            {
                std::error_code ec;
                kept->lastKnownPath = std::filesystem::absolute (*found, ec).string();
            }
            if (! exact && fileHash)
                kept->contentHash = io::contentId (*fileHash);
            instrumentSources.push_back ({ fileHash ? io::contentId (*fileHash) : src.contentHash, src.fileName, kept->lastKnownPath });
            auto audio = std::make_shared<const AudioData> (std::move (decoded.audio));
            if (primaryAudio == nullptr)
                primaryAudio = audio.get();
            sources.push_back ({ src.assetId, std::move (audio) });
        }
        if (! result.missingSources.empty())
        {
            result.error = "audio missing for the Note Atlas: " + result.missingSources.front()
                           + (result.missingSources.size() > 1 ? " (and " + std::to_string (result.missingSources.size() - 1) + " more)" : std::string());
            return result;
        }

        atlasset::BuildOptions options;
        options.instrument.interpolationZeroCrossings = playbackZeroCrossings;
        options.maxTotalSeconds = std::max (0.0, request.capacitySeconds);
        options.threads = request.threads;
        auto staged = atlasset::buildPlayableSet (atlas, sources, options);
        result.warnings.insert (result.warnings.end(), staged.playable.report.warnings.begin(), staged.playable.report.warnings.end());
        if (staged.playable.set == nullptr)
        {
            const auto& errors = staged.playable.report.errors;
            result.error = errors.empty() ? std::string ("the Note Atlas could not be built") : errors.front();
            return result;
        }
        const AudioData* overviewAudio = nullptr;
        const auto& primaryMember = staged.playable.set->members[static_cast<std::size_t> (staged.playable.set->primary)];
        for (const auto& s : sources)
            if (s.assetId == primaryMember.sourceAssetId)
                overviewAudio = s.audio.get();
        auto status = std::make_shared<AtlasStatus>();
        describeAtlas (*status, *resolvedAtlas);
        auto instrument = makeAtlasInstrument (staged.playable, std::move (resolvedAtlas), std::move (instrumentSources), generation,
                                               overviewAudio, std::move (status));
        result.atlasPending = std::move (staged.pending);
        result.instrument = std::move (instrument);
    }
    catch (const std::exception& e)
    {
        result.error = std::string ("unexpected error while loading the Note Atlas: ") + e.what();
    }
    catch (...)
    {
        result.error = "unexpected error while loading the Note Atlas";
    }
    return result;
}

LoadResult enhanceInstrumentAtlas (const LoadedInstrument& base, const atlasset::PreparedAtlas& pending, std::uint64_t generation,
                                   const std::atomic<bool>* cancel, int threads)
{
    LoadResult result;
    try
    {
        atlasset::BuildOptions options;
        options.threads = threads;
        options.cancel = cancel;
        const auto built = atlasset::enhanceSet (pending, options);
        if (built.set == nullptr)
        {
            result.error = built.report.cancelled ? std::string ("cancelled")
                                                  : (built.report.errors.empty() ? std::string ("the Note Atlas could not be finished") : built.report.errors.front());
            return result;
        }
        // The same instrument (same load, places and level), every member now with continuation.
        auto instrument = std::make_shared<LoadedInstrument> (base);
        instrument->generation = generation;
        const auto& primary = built.set->members[static_cast<std::size_t> (built.set->primary)];
        instrument->set = built.set;
        instrument->model = primary.model;
        instrument->analysis = primary.model->analysis;
        instrument->playbackGainDb = primary.model->playback.gainDb;
        auto status = std::make_shared<AtlasStatus> (base.atlasStatus != nullptr ? *base.atlasStatus : AtlasStatus {});
        status->playableOnly = false;
        status->memberBytes = built.report.memberBytes;
        status->buildMilliseconds = built.report.buildMilliseconds;
        instrument->atlasStatus = std::move (status);
        result.instrument = std::move (instrument);
    }
    catch (const std::exception& e)
    {
        result.error = std::string ("unexpected error while finishing the Note Atlas: ") + e.what();
    }
    catch (...)
    {
        result.error = "unexpected error while finishing the Note Atlas";
    }
    return result;
}

LoadResult loadAnalysedRecording (const AnalyseRequest& request, SampleStore& store, phrase::AnalysisQueue& queue,
                                  const io::PhraseAnalysisCache& cache, std::uint64_t generation, const std::atomic<bool>* cancel)
{
    LoadResult result;
    auto cancelled = [cancel] { return cancel != nullptr && cancel->load(); };
    try
    {
        // 1. The asset: hashed, kept in the store, decoded once (the analysis and both build
        //    stages use this one decode).
        const auto path = toPath (request.file);
        const auto hash = io::sha256OfFile (path);
        if (! hash)
        {
            result.error = "cannot read " + request.file.getFullPathName().toStdString();
            return result;
        }
        std::string error;
        if (! store.import (request.file, *hash, error))
            result.warnings.push_back (error);
        auto decoded = io::loadAudioFile (path);
        if (! decoded.ok)
        {
            result.error = decoded.error;
            return result;
        }
        atlas::SourceAsset source;
        source.assetId = "src0";
        source.origin = atlas::AssetOrigin::fileImport;
        source.contentHash = io::contentId (*hash);
        source.fileName = request.file.getFileName().toStdString();
        source.lastKnownPath = request.file.getFullPathName().toStdString();
        source.sampleRate = decoded.audio.sampleRate;
        source.numChannels = std::min (decoded.audio.numChannels(), 2);
        source.numFrames = decoded.audio.numFrames();
        auto audio = std::make_shared<const AudioData> (std::move (decoded.audio));

        // 2. Its analysis: stored (same audio, version and settings), else analysed now.
        auto status = std::make_shared<AtlasStatus>();
        auto analysed = cache.find (source, request.settings);
        std::vector<std::string> analysisWarnings;
        if (analysed)
            status->fromCache = true;
        else
        {
            struct Wait
            {
                std::mutex m;
                std::condition_variable cv;
                std::optional<phrase::AnalysisResult> result;
            };
            auto wait = std::make_shared<Wait>();
            const auto id = queue.submit ({ audio, source, request.settings },
                                          [wait] (phrase::AnalysisQueue::JobId, const phrase::AnalysisResult& r) {
                                              const std::lock_guard<std::mutex> lock (wait->m);
                                              wait->result = r;
                                              wait->cv.notify_all();
                                          },
                                          &error);
            if (! id)
            {
                result.error = error;
                return result;
            }
            std::unique_lock<std::mutex> lock (wait->m);
            while (! wait->result)
            {
                if (cancelled() || queue.status (*id).state == phrase::AnalysisQueue::State::cancelled)
                {
                    lock.unlock();
                    queue.cancel (*id);
                    result.error = "cancelled";
                    return result;
                }
                wait->cv.wait_for (lock, std::chrono::milliseconds (20));
            }
            const auto r = std::move (*wait->result);
            lock.unlock();
            if (! r.ok)
            {
                result.error = r.error.empty() ? std::string ("the analysis failed") : r.error;
                return result;
            }
            analysisWarnings = r.warnings;
            status->analysisMilliseconds = r.analysisSeconds * 1000.0;
            analysed = r.atlas;
            if (! cache.store (*analysed, request.settings, error))
                result.warnings.push_back (error);
        }
        describeAtlas (*status, *analysed);
        status->warnings = analysisWarnings;
        result.warnings.insert (result.warnings.end(), analysisWarnings.begin(), analysisWarnings.end());

        // 3. A recording a monophonic analysis does not suit stays playable as recorded.
        const char* reason = status->suitability == "silent"       ? "the recording is silent"
                           : status->suitability == "unsuitable"   ? "it does not look monophonic and pitched (polyphonic, noisy or unpitched)"
                           : status->notes == 0                    ? "no pitched notes were found"
                                                                   : nullptr;
        if (reason != nullptr)
        {
            LoadRequest single;
            single.file = request.file;
            auto fallback = loadInstrument (single, store, generation);
            fallback.warnings.insert (fallback.warnings.begin(), std::string ("kept as one sample: ") + reason);
            return fallback;
        }
        if (cancelled())
        {
            result.error = "cancelled";
            return result;
        }

        // 4. Stage A of its instrument (Stage B follows on the loader).
        atlasset::BuildOptions options;
        options.instrument.interpolationZeroCrossings = playbackZeroCrossings;
        options.maxTotalSeconds = std::max (0.0, request.capacitySeconds);
        options.threads = request.threads;
        options.cancel = cancel;
        const auto assetId = analysed->sources.front().assetId;
        auto staged = atlasset::buildPlayableSet (*analysed, { { assetId, audio } }, options);
        result.warnings.insert (result.warnings.end(), staged.playable.report.warnings.begin(), staged.playable.report.warnings.end());
        if (staged.playable.set == nullptr)
        {
            const auto& errors = staged.playable.report.errors;
            result.error = errors.empty() ? std::string ("the analysed recording could not be built") : errors.front();
            return result;
        }
        std::vector<LoadedInstrument::MemberFile> sources { { source.contentHash, source.fileName, source.lastKnownPath } };
        result.instrument = makeAtlasInstrument (staged.playable, std::make_shared<const atlas::NoteAtlas> (std::move (*analysed)),
                                                 std::move (sources), generation, audio.get(), std::move (status));
        result.atlasPending = std::move (staged.pending);
    }
    catch (const std::exception& e)
    {
        result.error = std::string ("unexpected error while analysing the recording: ") + e.what();
    }
    catch (...)
    {
        result.error = "unexpected error while analysing the recording";
    }
    return result;
}

LoadResult reassignInstrumentSet (const LoadedInstrument& base, const std::vector<SetAssignment>& assignments, std::uint64_t generation)
{
    LoadResult result;
    if (base.set == nullptr)
    {
        result.error = "not a multi-sample instrument";
        return result;
    }
    std::vector<SetInput> inputs;
    for (const auto& m : base.set->members)
    {
        auto model = m.model;
        for (const auto& a : assignments)
            if (a.filename == m.filename && a.rootMidi && std::abs (*a.rootMidi - model->rootMidi) > 0.01)
            {
                // A root correction retunes playback, not only the grouping.
                auto retuned = std::make_shared<InstrumentModel> (*model);
                retuned->rootMidi = *a.rootMidi;
                retuned->original.source = std::make_shared<const PlaybackSource> (model->original.source->withRootMidi (*a.rootMidi));
                model = std::move (retuned);
            }
        inputs.push_back ({ std::move (model), m.filename });
    }
    auto instrument = std::make_shared<LoadedInstrument> (base);
    instrument->generation = generation;
    instrument->assignments = assignments;
    auto set = std::make_shared<InstrumentSet> (inferSampleSet (inputs, assignments));
    // Keep member identities aligned with the (unchanged) member order.
    instrument->set = std::move (set);
    result.instrument = std::move (instrument);
    return result;
}

} // namespace osp::plugin
