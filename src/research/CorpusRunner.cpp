#include "research/CorpusRunner.h"

#include "core/PitchMath.h"
#include "io/AnalysisJson.h"
#include "io/AudioFileIO.h"
#include "io/JsonUtil.h"
#include "midi/MidiFixtures.h"
#include "research/CorpusIndex.h"
#include "research/Fixtures.h"
#include "research/RenderMetrics.h"
#include "research/RenderSession.h"
#include "research/SourceAnalysis.h"

#include <atomic>
#include <chrono>
#include <fstream>
#include <mutex>
#include <sstream>
#include <thread>

namespace osp::research
{

namespace
{
    struct RenderRecord
    {
        std::string fixture;
        std::string engine;
        std::string status;
        std::string wavPath;
        double durationSeconds = 0.0;
        double peakDbfs = 0.0;
        double maxAbsCentsError = 0.0;
        std::vector<std::string> issueCodes;
    };

    struct FileRecord
    {
        const CorpusEntry* entry = nullptr;
        std::string status = "pending";  // analysed | failed | unsupported | duplicate
        std::string error;
        AnalysisData analysis;
        RootChoice root;
        std::vector<RenderRecord> renders;
    };

    std::string renderBaseName (const std::string& fixture, EngineId engine)
    {
        return engine == EngineId::baselineA ? fixture : fixture + "." + engineName (engine);
    }

    void processFile (FileRecord& record, const CorpusRunOptions& options)
    {
        const auto& entry = *record.entry;
        const auto sourcePath = options.corpusDir / entry.relativePath;
        const auto folder = entry.folderName();

        auto source = loadAndAnalyse (sourcePath);
        if (! source.ok)
        {
            record.status = "failed";
            record.error = source.error;
            return;
        }

        source.analysis.source.path = entry.relativePath;
        record.analysis = source.analysis;
        record.status = "analysed";

        std::string error;
        if (! io::writeAnalysis (options.reportsDir / folder / "analysis.json", source.analysis, error))
            record.error = error;

        record.root = chooseRoot (&source.analysis, std::nullopt);
        const int reference = fixtureReferenceNote (record.root.rootMidi);

        for (const auto& fixtureName : options.fixtures)
        {
            const auto sequence = fixtures::byName (fixtureName, reference);
            if (! sequence)
                continue;

            for (const auto engine : options.engines)
            {
                RenderRecord render;
                render.fixture = fixtureName;
                render.engine = engineName (engine);
                try
                {
                    auto config = options.config;
                    config.engine = engine;
                    const auto output = renderSequence (source.audio, record.root.rootMidi, *sequence, config);

                    MetricsContext context;
                    context.expectedChannels = 2;
                    context.expectedSampleRate = output.audio.sampleRate;
                    context.sequence = &*sequence;
                    context.sourceF0Hz = record.root.sourceF0Hz;
                    context.rootMidi = record.root.rootMidi;
                    const auto metrics = computeMetrics (output.audio, context);

                    const auto base = renderBaseName (fixtureName, engine);
                    if (options.writeAudio)
                    {
                        const auto wav = options.rendersDir / folder / (base + ".wav");
                        if (io::writeAudioFile (wav, output.audio, io::SampleFormat::float32, error))
                            render.wavPath = wav.string();
                        else
                            render.issueCodes.push_back ("write-failed");
                    }

                    auto metricsJson = metricsToJson (metrics);
                    json::set (metricsJson, "fixture", json::str (fixtureName));
                    json::set (metricsJson, "fixtureVersion", fixtures::fixtureVersion);
                    json::set (metricsJson, "fixtureReferenceNote", reference);
                    json::set (metricsJson, "engine", json::str (render.engine));
                    json::set (metricsJson, "rootMidi", json::number (record.root.rootMidi, 3));
                    json::set (metricsJson, "rootOrigin", json::str (record.root.origin));
                    json::set (metricsJson, "seed", static_cast<juce::int64> (config.sampler.seed));
                    json::writeFile (options.reportsDir / folder / (base + ".metrics.json"), metricsJson, error);

                    render.status = metrics.status();
                    render.durationSeconds = metrics.durationSeconds;
                    render.peakDbfs = metrics.peakDbfs;
                    render.maxAbsCentsError = metrics.maxAbsCentsError;
                    for (const auto& issue : metrics.issues)
                        render.issueCodes.push_back (issue.code);
                }
                catch (const std::exception& e)
                {
                    render.status = "error";
                    render.issueCodes.push_back (std::string ("exception: ") + e.what());
                }
                record.renders.push_back (render);
            }
        }
    }

    std::string mdEscape (std::string text)
    {
        std::string out;
        for (char c : text)
            out += c == '|' ? std::string ("\\|") : std::string (1, c);
        return out;
    }
}

CorpusRunSummary runCorpus (const CorpusRunOptions& options)
{
    const auto started = std::chrono::steady_clock::now();
    CorpusRunSummary summary;
    std::mutex logMutex;
    auto log = [&] (const std::string& line) {
        if (options.log)
        {
            const std::lock_guard<std::mutex> lock (logMutex);
            options.log (line);
        }
    };

    const auto index = buildCorpusIndex (options.corpusDir);
    std::string error;
    json::writeFile (options.reportsDir / "index.json", corpusIndexToJson (index), error);

    std::vector<FileRecord> records (index.files.size());
    for (std::size_t i = 0; i < index.files.size(); ++i)
    {
        records[i].entry = &index.files[i];
        if (! index.files[i].supported)
            records[i].status = "unsupported";
        else if (! index.files[i].duplicateOf.empty())
            records[i].status = "duplicate";
        else if (! index.files[i].error.empty())
        {
            records[i].status = "failed";
            records[i].error = index.files[i].error;
        }
    }

    std::atomic<std::size_t> next { 0 };
    std::atomic<int> done { 0 };
    const auto total = static_cast<int> (records.size());

    auto worker = [&] {
        while (true)
        {
            const auto i = next.fetch_add (1);
            if (i >= records.size())
                return;
            auto& record = records[i];
            if (record.status == "pending")
            {
                try
                {
                    processFile (record, options);
                }
                catch (const std::exception& e)
                {
                    record.status = "failed";
                    record.error = std::string ("unexpected error: ") + e.what();
                }
                catch (...)
                {
                    record.status = "failed";
                    record.error = "unexpected unknown error";
                }
            }

            std::ostringstream line;
            line << "[" << ++done << "/" << total << "] " << record.entry->relativePath << ": " << record.status;
            if (record.status == "analysed")
                line << " root " << (record.analysis.pitch.midiNote >= 0 ? record.analysis.pitch.noteName : "?") << " ("
                     << record.analysis.pitch.confidenceLevel << ")";
            if (! record.error.empty())
                line << " - " << record.error;
            log (line.str());
        }
    };

    const int jobs = std::max (1, options.jobs);
    std::vector<std::thread> threads;
    for (int j = 1; j < jobs; ++j)
        threads.emplace_back (worker);
    worker();
    for (auto& t : threads)
        t.join();

    // Summary
    auto files = json::array();
    std::ostringstream md;
    md << "# Corpus run summary\n\n"
       << "Profile: `" << options.profileName << "`  \n"
       << "Engines: ";
    for (std::size_t e = 0; e < options.engines.size(); ++e)
        md << (e ? ", " : "") << "`" << engineName (options.engines[e]) << "`";
    md << "  \nFixtures: ";
    for (std::size_t f = 0; f < options.fixtures.size(); ++f)
        md << (f ? ", " : "") << options.fixtures[f];
    md << "\n\n| File | Status | Root | Conf. | Level | Dur (s) | SR | Ch | Renders | Issues |\n"
       << "|---|---|---|---|---|---|---|---|---|---|\n";

    for (const auto& r : records)
    {
        ++summary.totalFiles;
        if (r.entry->supported)
            ++summary.supportedFiles;
        else
            ++summary.unsupportedFiles;
        if (r.status == "duplicate")
            ++summary.duplicateFiles;
        if (r.status == "analysed")
            ++summary.analysed;
        if (r.status == "failed")
            ++summary.failed;

        auto item = json::object();
        json::set (item, "id", json::str (r.entry->id));
        json::set (item, "path", json::str (r.entry->relativePath));
        json::set (item, "folder", json::str (r.entry->folderName()));
        json::set (item, "status", json::str (r.status));
        if (! r.error.empty())
            json::set (item, "error", json::str (r.error));
        if (r.status == "duplicate")
            json::set (item, "duplicateOf", json::str (r.entry->duplicateOf));

        std::string renderCell;
        std::string issueCell;
        if (r.status == "analysed")
        {
            const auto& p = r.analysis.pitch;
            if (p.confidenceLevel == "high") ++summary.pitchHigh;
            else if (p.confidenceLevel == "moderate") ++summary.pitchModerate;
            else if (p.confidenceLevel == "low") ++summary.pitchLow;
            else ++summary.pitchNone;

            json::set (item, "durationSeconds", json::number (r.analysis.source.durationSeconds, 3));
            json::set (item, "sampleRate", json::number (r.analysis.source.sampleRate, 1));
            json::set (item, "channels", r.analysis.source.channels);
            json::set (item, "rootNote", p.midiNote >= 0 ? json::str (p.noteName) : juce::var());
            json::set (item, "rootHz", p.midiNote >= 0 ? json::number (p.fundamentalHz, 3) : juce::var());
            json::set (item, "pitchConfidence", json::number (p.confidence, 3));
            json::set (item, "pitchLevel", json::str (p.confidenceLevel));
            json::set (item, "rootOrigin", json::str (r.root.origin));
            json::set (item, "warnings", json::stringArray (r.analysis.warnings));

            auto renders = json::array();
            int ok = 0, warn = 0, err = 0;
            std::vector<std::string> codes;
            for (const auto& render : r.renders)
            {
                auto ri = json::object();
                json::set (ri, "fixture", json::str (render.fixture));
                json::set (ri, "engine", json::str (render.engine));
                json::set (ri, "status", json::str (render.status));
                json::set (ri, "durationSeconds", json::number (render.durationSeconds, 3));
                json::set (ri, "peakDbfs", json::number (render.peakDbfs, 2));
                json::set (ri, "maxAbsCentsError", json::number (render.maxAbsCentsError, 2));
                json::set (ri, "issues", json::stringArray (render.issueCodes));
                if (! render.wavPath.empty())
                    json::set (ri, "wav", json::str (render.wavPath));
                renders.append (ri);
                if (render.status == "ok") { ++ok; ++summary.rendersOk; }
                else if (render.status == "warning") { ++warn; ++summary.rendersWarning; }
                else { ++err; ++summary.rendersError; }
                for (const auto& code : render.issueCodes)
                    if (std::find (codes.begin(), codes.end(), code) == codes.end())
                        codes.push_back (code);
            }
            json::set (item, "renders", renders);

            renderCell = std::to_string (ok) + " ok / " + std::to_string (warn) + " warn / " + std::to_string (err) + " err";
            for (const auto& code : codes)
                issueCell += (issueCell.empty() ? "" : ", ") + code;
            if (! r.analysis.warnings.empty())
                issueCell += (issueCell.empty() ? "" : "; ") + std::to_string (r.analysis.warnings.size()) + " analysis warning(s)";

            md << "| " << mdEscape (r.entry->relativePath) << " | " << r.status << " | "
               << (p.midiNote >= 0 ? p.noteName + " (" + std::to_string (static_cast<int> (std::lround (p.centsOffset))) + "c)" : "?")
               << " | " << json::round (p.confidence, 2) << " | " << p.confidenceLevel << " | "
               << json::round (r.analysis.source.durationSeconds, 2) << " | " << r.analysis.source.sampleRate << " | "
               << r.analysis.source.channels << " | " << renderCell << " | " << mdEscape (issueCell) << " |\n";
        }
        else
        {
            md << "| " << mdEscape (r.entry->relativePath) << " | " << r.status << " | | | | | | | | "
               << mdEscape (r.error.empty() ? r.entry->duplicateOf : r.error) << " |\n";
        }
        files.append (item);
    }

    summary.wallSeconds = std::chrono::duration<double> (std::chrono::steady_clock::now() - started).count();

    auto root = json::object();
    json::set (root, "schemaVersion", 1);
    json::set (root, "corpus", json::str (options.corpusDir.string()));
    json::set (root, "profile", json::str (options.profileName));
    json::set (root, "fixtures", json::stringArray (options.fixtures));
    std::vector<std::string> engineNames;
    for (auto e : options.engines)
        engineNames.push_back (engineName (e));
    json::set (root, "engines", json::stringArray (engineNames));
    json::set (root, "seed", static_cast<juce::int64> (options.config.sampler.seed));
    json::set (root, "outputSampleRate", json::number (options.config.sampleRate, 1));
    auto counts = json::object();
    json::set (counts, "totalFiles", summary.totalFiles);
    json::set (counts, "supportedFiles", summary.supportedFiles);
    json::set (counts, "unsupportedFiles", summary.unsupportedFiles);
    json::set (counts, "duplicateFiles", summary.duplicateFiles);
    json::set (counts, "analysed", summary.analysed);
    json::set (counts, "failed", summary.failed);
    json::set (counts, "pitchHigh", summary.pitchHigh);
    json::set (counts, "pitchModerate", summary.pitchModerate);
    json::set (counts, "pitchLow", summary.pitchLow);
    json::set (counts, "pitchNone", summary.pitchNone);
    json::set (counts, "rendersOk", summary.rendersOk);
    json::set (counts, "rendersWarning", summary.rendersWarning);
    json::set (counts, "rendersError", summary.rendersError);
    json::set (root, "counts", counts);
    json::set (root, "wallSeconds", json::number (summary.wallSeconds, 2));
    json::set (root, "files", files);

    summary.summaryJson = options.reportsDir / "summary.json";
    summary.summaryMarkdown = options.reportsDir / "summary.md";
    json::writeFile (summary.summaryJson, root, error);

    std::ostringstream header;
    header << "\n**Files:** " << summary.totalFiles << " total, " << summary.analysed << " analysed, " << summary.failed
           << " failed, " << summary.unsupportedFiles << " unsupported, " << summary.duplicateFiles << " duplicate  \n"
           << "**Pitch confidence:** " << summary.pitchHigh << " high, " << summary.pitchModerate << " moderate, "
           << summary.pitchLow << " low, " << summary.pitchNone << " none  \n"
           << "**Renders:** " << summary.rendersOk << " ok, " << summary.rendersWarning << " warning, " << summary.rendersError
           << " error  \n**Wall time:** " << json::round (summary.wallSeconds, 1) << " s\n";

    std::error_code ec;
    std::filesystem::create_directories (options.reportsDir, ec);
    std::ofstream (summary.summaryMarkdown) << md.str() << header.str();

    return summary;
}

} // namespace osp::research
