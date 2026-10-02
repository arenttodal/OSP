#include "InstrumentLoader.h"

#include "analysis/Analyzer.h"
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

LoadResult loadInstrument (const LoadRequest& request, SampleStore& store, std::uint64_t generation)
{
    LoadResult result;
    try
    {
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

        // 4. Analysis: cached if compatible, else analyse and cache.
        auto instrument = std::make_shared<LoadedInstrument>();
        instrument->generation = generation;
        instrument->contentHash = io::contentId (hash);
        instrument->filename = request.filename.empty() ? request.file.getFileName().toStdString() : request.filename;
        instrument->originalPath = request.originalPath.empty() ? request.file.getFullPathName().toStdString() : request.originalPath;
        instrument->storedPath = stored ? stored->getFullPathName().toStdString() : std::string();

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
            analysis->source.filename = instrument->filename;
            analysis->source.path = instrument->originalPath;
            analysis->source.contentHash = instrument->contentHash;
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
        instrument->analysis = std::move (*analysis);

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
        instrument->durationSeconds = decoded.audio.durationSeconds();

        // 6. Waveform overview.
        const auto mono = decoded.audio.mixToMono();
        const auto buckets = static_cast<std::size_t> (std::min<std::int64_t> (waveformBuckets, std::max<std::int64_t> (1, decoded.audio.numFrames())));
        instrument->peakMin.assign (buckets, 0.0f);
        instrument->peakMax.assign (buckets, 0.0f);
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
            instrument->peakMin[b] = lo;
            instrument->peakMax[b] = hi;
        }

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

} // namespace osp::plugin
