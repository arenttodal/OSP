#include "InstrumentLoader.h"

#include "analysis/Analyzer.h"
#include "core/Fft.h"
#include "engine/InstrumentBuilder.h"
#include "io/AnalysisJson.h"
#include "io/AudioFileIO.h"
#include "io/ContentHash.h"
#include "model/PlaybackPreparation.h"
#include "model/RootChoice.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace osp::plugin
{

namespace
{
    std::filesystem::path toPath (const juce::File& f)
    {
        return std::filesystem::path (f.getFullPathName().toStdString());
    }

    constexpr int waveformBuckets = 1024;
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
        for (std::size_t b = 0; b < buckets; ++b)
        {
            const auto start = b * mono.size() / buckets;
            const auto end = std::max (start + 1, (b + 1) * mono.size() / buckets);
            float lo = 0.0f, hi = 0.0f;
            for (auto i = start; i < end && i < mono.size(); ++i)
            {
                lo = std::min (lo, mono[i]);
                hi = std::max (hi, mono[i]);
            }
            instrument.peakMin[b] = lo;
            instrument.peakMax[b] = hi;
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
