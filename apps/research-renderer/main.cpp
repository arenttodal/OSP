// research-renderer: headless analysis / rendering / corpus / benchmark tool.
// See README.md and docs/testing.md for usage.

#include "analysis/continuation/ContinuationAnalyzer.h"
#include "core/PitchMath.h"
#include "io/AnalysisJson.h"
#include "io/AudioFileIO.h"
#include "io/JsonUtil.h"
#include "midi/MidiFileIO.h"
#include "midi/MidiFixtures.h"
#include "research/Bakeoff.h"
#include "research/Experiment.h"
#include "research/Benchmark.h"
#include "research/CorpusIndex.h"
#include "research/CorpusRunner.h"
#include "research/Fixtures.h"
#include "research/RenderMetrics.h"
#include "research/RenderSession.h"
#include "research/SourceAnalysis.h"
#include "research/TestSignalSet.h"

#include <chrono>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;
using namespace osp;
using namespace osp::research;

namespace
{

enum ExitCode
{
    exitOk = 0,
    exitUsage = 1,
    exitInput = 2,
    exitProcessing = 3,
    exitRenderErrors = 4
};

const char* usageText = R"(research-renderer - OSP Phase 0 research harness

ANALYSIS
  --analyze <file> [--output-analysis <report.json>]
        Analyse a WAV/AIFF file. Prints the JSON report to stdout unless an output path is given.

RENDERING
  --source <file> (--midi <file.mid> | --fixture <name>) --output <out.wav>
        [--analysis <report.json>]   use a stored analysis instead of re-analysing (root comes from it)
        [--root <note>]              override root, e.g. A3, F#2, 57, 57.3
        [--engine A|B|C]             A = baseline resampler (default), B = randomised baseline, C = OSP engine
        [--config <config.json>]     research/configs/*.json
        [--seed <n>] [--sample-rate <hz>|source] [--block-size <n>]
        [--continuation <strategy>] [--pitch-character tape|natural]   (engine C)
        [--metrics <metrics.json>]   write render metrics (also printed as a summary)
        [--start file|onset]         start notes at sample 0 (default) or just before the analysed onset
        [--level raw|normalise]      play at recorded level (default) or match levels (max RMS -16 dBFS)
  Fixtures: repetition, dynamics, register, melody, chords, long-hold, long-chord, repeated-sustains
        (generated at the source root; --fixture-reference <note> to override)

CORPUS
  --corpus <dir> [--profile quick|standard|sustain|full] [--engines A,B] [--start ..] [--level ..]
        [--reports <dir>] [--renders <dir>] [--jobs <n>] [--no-audio] [--config <f>] [--seed <n>] [--strict]
  --index <dir> [--output-index <index.json>]

FIXTURES / TEST DATA
  --write-fixtures <dir>          write the standard MIDI fixtures (reference C4)
  --generate-test-signals <dir>   write synthetic ground-truth sources (+ a broken and an unsupported file)

PITCH BAKE-OFF (Phase 2)
  --bakeoff <plan.json> [--output <dir>]      render blind A/B/C clips + key.json + listening.json
  --bakeoff-score <dir> --ratings <file>      join ratings with the key -> score.md / score.json
  --experiment <plan.json> [--output <dir>]   blind listening experiment (any engines/settings), see docs/testing.md

BENCHMARK
  --benchmark [--voices 24] [--seconds 20] [--sample-rate 48000] [--block-size 128] [--output-json <f>] [--no-retrigger]

  --help, --version

Exit codes: 0 ok, 1 usage, 2 unreadable input, 3 processing/output failure,
            4 render completed but has errors (NaN/Inf/channel/sample-rate), or --strict corpus failures.
)";

struct Args
{
    std::map<std::string, std::string> values;
    std::set<std::string> flags;

    bool has (const std::string& key) const { return values.count (key) > 0 || flags.count (key) > 0; }
    std::string get (const std::string& key, const std::string& fallback = {}) const
    {
        const auto it = values.find (key);
        return it == values.end() ? fallback : it->second;
    }
};

const std::set<std::string> flagOptions = { "--help", "--version", "--benchmark", "--no-audio", "--strict", "--no-retrigger" };

std::optional<Args> parseArgs (int argc, char** argv, std::string& error)
{
    Args args;
    for (int i = 1; i < argc; ++i)
    {
        std::string key = argv[i];
        if (key.rfind ("--", 0) != 0)
        {
            error = "unexpected argument '" + key + "'";
            return std::nullopt;
        }
        if (flagOptions.count (key))
        {
            args.flags.insert (key);
            continue;
        }
        if (i + 1 >= argc)
        {
            error = "missing value for " + key;
            return std::nullopt;
        }
        args.values[key] = argv[++i];
    }
    return args;
}

int fail (int code, const std::string& message)
{
    std::cerr << "error: " << message << "\n";
    return code;
}

std::optional<double> parseNumber (const std::string& text)
{
    try
    {
        std::size_t used = 0;
        const double v = std::stod (text, &used);
        if (used != text.size())
            return std::nullopt;
        return v;
    }
    catch (...)
    {
        return std::nullopt;
    }
}

/** Applies --config, --engine, --seed, --sample-rate, --block-size. */
std::optional<RenderConfig> configFromArgs (const Args& args, std::string& error)
{
    RenderConfig config;
    if (args.has ("--config"))
    {
        auto loaded = loadRenderConfig (args.get ("--config"), error);
        if (! loaded)
            return std::nullopt;
        config = *loaded;
    }
    if (args.has ("--engine"))
    {
        const auto engine = parseEngine (args.get ("--engine"));
        if (! engine)
        {
            error = "unknown engine '" + args.get ("--engine") + "' (use A, B or C)";
            return std::nullopt;
        }
        config.engine = *engine;
    }
    if (args.has ("--seed"))
    {
        const auto seed = parseNumber (args.get ("--seed"));
        if (! seed || *seed < 0)
        {
            error = "invalid --seed";
            return std::nullopt;
        }
        config.sampler.seed = static_cast<std::uint64_t> (*seed);
        config.engineSettings.seed = config.sampler.seed;
    }
    if (args.has ("--sample-rate"))
    {
        const auto text = args.get ("--sample-rate");
        if (text == "source")
            config.sampleRate = 0.0;
        else if (const auto rate = parseNumber (text); rate && *rate >= 8000.0 && *rate <= 384000.0)
            config.sampleRate = *rate;
        else
        {
            error = "invalid --sample-rate '" + text + "'";
            return std::nullopt;
        }
    }
    if (args.has ("--continuation"))
    {
        if (! parseContinuationStrategy (args.get ("--continuation"), config.engineSettings.continuation))
        {
            error = "invalid --continuation (off, naive-loop, best-loop, multi-loop, multi-loop-movement)";
            return std::nullopt;
        }
    }
    if (args.has ("--pitch-character"))
    {
        const auto pc = args.get ("--pitch-character");
        if (pc != "tape" && pc != "natural")
        {
            error = "invalid --pitch-character (tape or natural)";
            return std::nullopt;
        }
        config.engineSettings.pitchCharacter = pc == "natural" ? PitchCharacter::natural : PitchCharacter::tape;
        config.anchors = pc == "natural";
    }
    if (args.has ("--start"))
    {
        const auto mode = args.get ("--start");
        if (mode != "file" && mode != "onset")
        {
            error = "invalid --start '" + mode + "' (file or onset)";
            return std::nullopt;
        }
        config.playback.startAtOnset = mode == "onset";
    }
    if (args.has ("--level"))
    {
        const auto mode = args.get ("--level");
        if (mode != "raw" && mode != "normalise" && mode != "normalize")
        {
            error = "invalid --level '" + mode + "' (raw or normalise)";
            return std::nullopt;
        }
        config.playback.normaliseLevel = mode != "raw";
    }
    if (args.has ("--block-size"))
    {
        const auto size = parseNumber (args.get ("--block-size"));
        if (! size || *size < 1 || *size > 8192)
        {
            error = "invalid --block-size";
            return std::nullopt;
        }
        config.blockSize = static_cast<int> (*size);
    }
    return config;
}

void printAnalysisSummary (const AnalysisData& a, std::ostream& out)
{
    const auto& p = a.pitch;
    out << a.source.filename << ": " << a.source.format << " " << a.source.bitDepth << (a.source.isFloatingPoint ? "f" : "")
        << "-bit, " << a.source.sampleRate << " Hz, " << a.source.channels << " ch, " << json::round (a.source.durationSeconds, 3) << " s\n";
    if (p.midiNote >= 0)
        out << "  root: " << p.noteName << " " << (p.centsOffset >= 0 ? "+" : "") << json::round (p.centsOffset, 1) << "c ("
            << json::round (p.fundamentalHz, 2) << " Hz), confidence " << json::round (p.confidence, 2) << " [" << p.confidenceLevel << "]\n";
    else
        out << "  root: ? (no pitch detected)\n";
    out << "  envelope: onset " << json::round (a.envelope.onsetSeconds, 3) << " s, attack " << json::round (a.envelope.attackSeconds, 3)
        << " s, peak " << json::round (a.envelope.peakDbfs, 1) << " dBFS" << (a.envelope.endsWhileSounding ? ", ends while sounding" : "") << "\n";
    out << "  spectrum: centroid " << json::round (a.spectral.meanCentroidHz, 0) << " Hz, flatness " << json::round (a.spectral.meanFlatness, 3)
        << ", harmonic ratio " << json::round (a.spectral.harmonicEnergyRatio, 2) << "\n";
    if (! a.stereo.isMono)
        out << "  stereo: correlation " << json::round (a.stereo.correlation, 3) << ", width " << json::round (a.stereo.width, 3) << "\n";
    for (const auto& w : a.warnings)
        out << "  warning: " << w << "\n";
}

int commandAnalyze (const Args& args)
{
    const fs::path input = args.get ("--analyze");
    auto source = loadAndAnalyse (input);
    if (! source.ok)
        return fail (exitInput, source.error);

    if (args.has ("--output-analysis"))
    {
        std::string error;
        if (! io::writeAnalysis (args.get ("--output-analysis"), source.analysis, error))
            return fail (exitProcessing, error);
        printAnalysisSummary (source.analysis, std::cout);
        std::cout << "wrote " << args.get ("--output-analysis") << "\n";
    }
    else
    {
        std::cout << json::toString (io::analysisToJson (source.analysis)) << "\n";
    }
    return exitOk;
}

int commandRender (const Args& args)
{
    if (! args.has ("--output"))
        return fail (exitUsage, "--output <file.wav> is required for rendering");
    if (args.has ("--midi") == args.has ("--fixture"))
        return fail (exitUsage, "give exactly one of --midi <file> or --fixture <name>");

    std::string error;
    const auto config = configFromArgs (args, error);
    if (! config)
        return fail (exitUsage, error);

    const fs::path sourcePath = args.get ("--source");
    AnalysedSource source;
    if (args.has ("--analysis"))
    {
        auto loaded = io::loadAudioFile (sourcePath);
        if (! loaded.ok)
            return fail (exitInput, loaded.error);
        auto analysis = io::readAnalysis (args.get ("--analysis"), error);
        if (! analysis)
            return fail (exitInput, error);
        source.ok = true;
        source.audio = std::move (loaded.audio);
        source.analysis = std::move (*analysis);
    }
    else
    {
        source = loadAndAnalyse (sourcePath);
        if (! source.ok)
            return fail (exitInput, source.error);
    }

    std::optional<double> rootOverride;
    if (args.has ("--root"))
    {
        rootOverride = parseNoteSpec (args.get ("--root"));
        if (! rootOverride)
            return fail (exitUsage, "cannot parse --root '" + args.get ("--root") + "' (examples: A3, F#2, 57, 57.25)");
    }
    const auto root = chooseRoot (&source.analysis, rootOverride);

    MidiSequence sequence;
    if (args.has ("--midi"))
    {
        auto midi = io::readMidiFile (args.get ("--midi"), error);
        if (! midi)
            return fail (exitInput, error);
        sequence = std::move (*midi);
    }
    else
    {
        int reference = fixtureReferenceNote (root.rootMidi);
        if (args.has ("--fixture-reference"))
        {
            const auto ref = parseNoteSpec (args.get ("--fixture-reference"));
            if (! ref)
                return fail (exitUsage, "cannot parse --fixture-reference");
            reference = static_cast<int> (std::lround (*ref));
        }
        auto fixture = fixtures::byName (args.get ("--fixture"), reference);
        if (! fixture)
            return fail (exitUsage, "unknown fixture '" + args.get ("--fixture") + "'");
        sequence = std::move (*fixture);
    }

    if (sequence.events.empty())
        std::cerr << "warning: MIDI sequence contains no note events\n";

    const auto preparation = preparePlayback (source.analysis, config->playback);
    const auto output = renderWithEngine (source.audio, source.analysis, root.rootMidi, sequence, *config, preparation);

    MetricsContext context;
    context.expectedChannels = 2;
    context.expectedSampleRate = output.audio.sampleRate;
    context.sequence = &sequence;
    context.sourceF0Hz = root.sourceF0Hz;
    context.rootMidi = root.rootMidi;
    const auto metrics = computeMetrics (output.audio, context);

    if (! io::writeAudioFile (args.get ("--output"), output.audio, io::SampleFormat::float32, error))
        return fail (exitProcessing, error);

    if (args.has ("--metrics"))
    {
        auto metricsJson = metricsToJson (metrics);
        json::set (metricsJson, "engine", json::str (engineName (config->engine)));
        json::set (metricsJson, "rootMidi", json::number (root.rootMidi, 3));
        json::set (metricsJson, "rootOrigin", json::str (root.origin));
        json::set (metricsJson, "seed", static_cast<juce::int64> (config->sampler.seed));
        json::set (metricsJson, "startAtOnset", config->playback.startAtOnset);
        json::set (metricsJson, "normaliseLevel", config->playback.normaliseLevel);
        json::set (metricsJson, "startSeconds", json::number (preparation.startSeconds, 4));
        json::set (metricsJson, "playbackGainDb", json::number (preparation.gainDb, 2));
        if (! json::writeFile (args.get ("--metrics"), metricsJson, error))
            return fail (exitProcessing, error);
    }

    std::cout << "rendered " << args.get ("--output") << " (" << engineName (config->engine) << ", root "
              << json::round (root.rootMidi, 2) << " [" << root.origin << "], " << json::round (metrics.durationSeconds, 2)
              << " s, peak " << json::round (metrics.peakDbfs, 1) << " dBFS, status " << metrics.status() << ")\n";
    for (const auto& issue : metrics.issues)
        std::cout << "  " << issue.severity << " [" << issue.code << "] " << issue.message << "\n";

    return metrics.status() == "error" ? exitRenderErrors : exitOk;
}

int commandCorpus (const Args& args)
{
    std::string error;
    const auto config = configFromArgs (args, error);
    if (! config)
        return fail (exitUsage, error);

    CorpusRunOptions options;
    options.corpusDir = args.get ("--corpus");
    if (! fs::is_directory (options.corpusDir))
        return fail (exitInput, "corpus directory not found: " + options.corpusDir.string());

    options.profileName = args.get ("--profile", "standard");
    const auto profile = fixtures::profile (options.profileName);
    if (! profile)
        return fail (exitUsage, "unknown profile '" + options.profileName + "' (quick, standard, sustain, full)");
    options.fixtures = *profile;
    options.config = *config;
    options.reportsDir = args.get ("--reports", "research/reports");
    options.rendersDir = args.get ("--renders", "research/renders");
    options.writeAudio = ! args.has ("--no-audio");

    if (args.has ("--engines"))
    {
        options.engines.clear();
        std::stringstream list (args.get ("--engines"));
        std::string item;
        while (std::getline (list, item, ','))
        {
            const auto engine = parseEngine (item);
            if (! engine)
                return fail (exitUsage, "unknown engine '" + item + "'");
            options.engines.push_back (*engine);
        }
    }
    else
        options.engines = { config->engine };

    const auto jobs = parseNumber (args.get ("--jobs", "0"));
    options.jobs = jobs && *jobs >= 1 ? static_cast<int> (*jobs) : std::max (1u, std::thread::hardware_concurrency());
    options.log = [] (const std::string& line) { std::cout << line << std::endl; };

    const auto summary = runCorpus (options);
    std::cout << "\n" << summary.totalFiles << " files: " << summary.analysed << " analysed, " << summary.failed << " failed, "
              << summary.unsupportedFiles << " unsupported, " << summary.duplicateFiles << " duplicate\n"
              << "pitch: " << summary.pitchHigh << " high, " << summary.pitchModerate << " moderate, " << summary.pitchLow
              << " low, " << summary.pitchNone << " none\n"
              << "renders: " << summary.rendersOk << " ok, " << summary.rendersWarning << " warning, " << summary.rendersError << " error\n"
              << "summary: " << summary.summaryMarkdown.string() << " (" << json::round (summary.wallSeconds, 1) << " s)\n";

    if (args.has ("--strict") && (summary.failed > 0 || summary.rendersError > 0))
        return exitRenderErrors;
    return exitOk;
}

int commandIndex (const Args& args)
{
    const fs::path dir = args.get ("--index");
    if (! fs::is_directory (dir))
        return fail (exitInput, "directory not found: " + dir.string());
    const auto index = buildCorpusIndex (dir);
    const auto output = args.get ("--output-index", (dir / "index.json").string());
    std::string error;
    if (! json::writeFile (output, corpusIndexToJson (index), error))
        return fail (exitProcessing, error);
    int supported = 0;
    for (const auto& e : index.files)
        supported += e.supported ? 1 : 0;
    std::cout << "indexed " << index.files.size() << " files (" << supported << " supported) -> " << output << "\n";
    return exitOk;
}

int commandBenchmark (const Args& args)
{
    BenchmarkOptions options;
    if (const auto v = parseNumber (args.get ("--voices", "24")))
        options.voices = std::clamp (static_cast<int> (*v), 1, SamplerSettings::maxPolyphony);
    if (const auto v = parseNumber (args.get ("--seconds", "20")))
        options.seconds = std::max (0.5, *v);
    if (const auto v = parseNumber (args.get ("--sample-rate", "48000")))
        options.sampleRate = *v;
    if (const auto v = parseNumber (args.get ("--block-size", "128")))
        options.blockSize = std::max (1, static_cast<int> (*v));
    options.denseRetriggers = ! args.has ("--no-retrigger");

    const auto r = runBenchmark (options);
    std::cout << "benchmark: " << options.voices << " voices, " << options.sampleRate << " Hz, block " << options.blockSize
              << (options.denseRetriggers ? ", dense retriggers" : "") << "\n"
              << "  rendered " << json::round (r.renderedSeconds, 2) << " s in " << json::round (r.wallSeconds, 3) << " s ("
              << json::round (r.realtimeFactor, 1) << "x realtime)\n"
              << "  block budget " << json::round (r.budgetMicros, 1) << " us; mean " << json::round (r.meanBlockMicros, 1)
              << " us (" << json::round (r.meanBudgetPercent, 1) << "%), p99 " << json::round (r.p99BlockMicros, 1) << " us, worst "
              << json::round (r.worstBlockMicros, 1) << " us (" << json::round (r.worstBudgetPercent, 1) << "%)\n"
              << "  active voices: mean " << json::round (r.meanActiveVoices, 1) << ", peak " << r.peakActiveVoices
              << " (includes voices fading out after being stolen)\n";

    if (args.has ("--output-json"))
    {
        std::string error;
        if (! json::writeFile (args.get ("--output-json"), benchmarkToJson (options, r), error))
            return fail (exitProcessing, error);
    }
    return exitOk;
}

} // namespace

int main (int argc, char** argv)
{
    std::string error;
    const auto parsed = parseArgs (argc, argv, error);
    if (! parsed)
    {
        std::cerr << "error: " << error << "\n\n" << usageText;
        return exitUsage;
    }
    const auto& args = *parsed;

    try
    {
        if (args.has ("--help") || argc == 1)
        {
            std::cout << usageText;
            return argc == 1 ? exitUsage : exitOk;
        }
        if (args.has ("--version"))
        {
            std::cout << "research-renderer " << OSP_VERSION_STRING << " (analysis schema " << analysisSchemaVersion
                      << ", fixtures v" << fixtures::fixtureVersion << ")\n";
            return exitOk;
        }
        if (args.has ("--analyze"))
            return commandAnalyze (args);
        if (args.has ("--source"))
            return commandRender (args);
        if (args.has ("--corpus"))
            return commandCorpus (args);
        if (args.has ("--index"))
            return commandIndex (args);
        if (args.has ("--benchmark"))
            return commandBenchmark (args);
        if (args.has ("--bakeoff"))
        {
            const auto plan = loadBakeoffPlan (args.get ("--bakeoff"), error);
            if (! plan)
                return fail (exitInput, error);
            const fs::path output = args.get ("--output", "research/bakeoff/" + plan->name);
            const auto summary = runBakeoff (*plan, output, [] (const std::string& line) { std::cout << line << std::endl; });
            for (const auto& e : summary.errors)
                std::cerr << "warning: " << e << "\n";
            std::cout << summary.clips << " clips in " << summary.groups << " groups -> " << output.string()
                      << " (engine key: " << summary.keyFile.string() << ")\n";
            return summary.clips > 0 ? exitOk : exitProcessing;
        }
        if (args.has ("--continuation-report"))
        {
            // One line per file: can it sustain, the stable region, jump quality, release.
            const fs::path target = args.get ("--continuation-report");
            std::vector<fs::path> files;
            if (fs::is_directory (target))
            {
                for (const auto& entry : fs::recursive_directory_iterator (target))
                    if (entry.is_regular_file() && io::isSupportedAudioExtension (entry.path()))
                        files.push_back (entry.path());
                std::sort (files.begin(), files.end());
            }
            else
                files.push_back (target);
            for (const auto& file : files)
            {
                auto source = loadAndAnalyse (file);
                if (! source.ok)
                {
                    std::cout << file.filename().string() << ": " << source.error << "\n";
                    continue;
                }
                const auto t0 = std::chrono::steady_clock::now();
                const auto c = analyseContinuation (source.audio, source.analysis);
                const double ms = std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - t0).count();
                const double sr = source.audio.sampleRate;
                double meanR = 0.0;
                for (const auto& j : c.jumps)
                    meanR += j.correlation;
                if (! c.jumps.empty())
                    meanR /= static_cast<double> (c.jumps.size());
                std::cout << file.filename().string() << ": " << (c.canSustain ? "SUSTAIN" : "one-shot") << " (" << c.reason << ")"
                          << " region " << juce::String (c.sustainStartFrame / sr, 2) << "-" << juce::String (c.sustainEndFrame / sr, 2) << " s"
                          << ", jumps " << c.jumps.size() << " mean r " << juce::String (meanR, 3)
                          << ", release " << (c.hasRelease ? "yes tail " + juce::String (c.tailSeconds, 2).toStdString() + " s" : std::string ("no"))
                          << ", exits " << c.graftExits.size() << ", " << juce::String (ms, 0) << " ms\n";
            }
            return exitOk;
        }
        if (args.has ("--experiment"))
        {
            const fs::path planFile = args.get ("--experiment");
            const fs::path output = args.get ("--output", "research/experiments/runs/" + planFile.stem().string());
            const auto summary = runExperiment (planFile, output, [] (const std::string& line) { std::cout << line << std::endl; }, error);
            if (! error.empty() && summary.clips == 0)
                return fail (exitInput, error);
            for (const auto& e : summary.errors)
                std::cerr << "warning: " << e << "\n";
            std::cout << summary.clips << " clips in " << summary.groups << " groups -> " << output.string() << "\n";
            return summary.clips > 0 ? exitOk : exitProcessing;
        }
        if (args.has ("--bakeoff-score"))
        {
            if (! args.has ("--ratings"))
                return fail (exitUsage, "--bakeoff-score needs --ratings <file>");
            std::string report;
            if (! scoreBakeoff (args.get ("--bakeoff-score"), args.get ("--ratings"), report, error))
                return fail (exitInput, error);
            std::cout << report;
            return exitOk;
        }
        if (args.has ("--write-fixtures"))
        {
            std::vector<std::string> written;
            if (! writeFixtureFiles (args.get ("--write-fixtures"), written, error))
                return fail (exitProcessing, error);
            for (const auto& w : written)
                std::cout << "wrote " << w << "\n";
            return exitOk;
        }
        if (args.has ("--generate-test-signals"))
        {
            const auto signals = writeTestSignalSet (args.get ("--generate-test-signals"), error);
            if (signals.empty())
                return fail (exitProcessing, error);
            for (const auto& s : signals)
                std::cout << "wrote " << s.filename << "\n";
            return exitOk;
        }
    }
    catch (const std::exception& e)
    {
        return fail (exitProcessing, std::string ("unexpected error: ") + e.what());
    }

    std::cerr << "error: no command given\n\n" << usageText;
    return exitUsage;
}
