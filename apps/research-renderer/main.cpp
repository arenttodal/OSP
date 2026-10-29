// research-renderer: headless analysis / rendering / corpus / benchmark tool.
// See README.md and docs/testing.md for usage.

#include "analysis/continuation/ContinuationAnalyzer.h"
#include "analysis/phrase/PhraseAnalysis.h"
#include "core/PitchMath.h"
#include "io/AnalysisJson.h"
#include "io/AudioFileIO.h"
#include "io/ContentHash.h"
#include "io/JsonUtil.h"
#include "io/NoteAtlasJson.h"
#include "midi/MidiFileIO.h"
#include "midi/MidiFixtures.h"
#include "engine/AtlasSet.h"
#include "engine/InstrumentBuilder.h"
#include "engine/InstrumentEngine.h"
#include "research/AtlasFixtures.h"
#include "research/Bakeoff.h"
#include "research/Experiment.h"
#include "research/Benchmark.h"
#include "research/CorpusIndex.h"
#include "research/CorpusRunner.h"
#include "research/Fixtures.h"
#include "research/PhraseFixtures.h"
#include "research/RenderMetrics.h"
#include "research/RenderSession.h"
#include "research/SourceAnalysis.h"
#include "research/TestSignalSet.h"
#include "research/UnifiedScenes.h"

#include <chrono>
#include <fstream>
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
  --register-test <dir>                       multi-pitch ground truth: hide each pitch, rebuild from the rest (spec §70)
  --continuation-report <file|dir>            stable region, jumps and release per file

UNIFIED ENGINE (docs/research/unified-andor)
  --unified <dir> [--set baseline|keydna|phase2|budget|atlas] [--with-keydna organic|expressive] [--scene <name>] [--repeats 3] [--no-audio]
                                              renders the baseline scenes or the KEY DNA demonstrations
                                              (src/research/UnifiedScenes.cpp): <scene>.wav, <scene>.json
                                              (digest), costs.json, summary.md (keydna: also keys.md;
                                              atlas: also atlas.md / atlas.json - memory, build time,
                                              lookup cost - and baseline A/B renders of the melody)

NOTE ATLAS (Phase 3; docs/research/unified-andor/phase-3-report.md)
  --atlas <file.andoratlas> (--midi <file.mid> | --fixture <name>) --output <out.wav>
        [--mode oneshot|granular] [--search <dir>] [--config <f>] [--seed <n>] [--block-size <n>]
        [--max-transpose <semitones>] [--no-fill] [--as-recorded] [--stage-a]
        Plays a prepared atlas (its notes placed by hand or by a later analysis) through engine C:
        the developer harness for real recordings. Its audio is found by content hash via the
        atlas's paths (relative to the atlas file) and --search. Fixtures use the lowest note as reference.
        --stage-a: the instrument as Stage A publishes it (Phase 4: playable, no continuation yet).
  --write-atlas-fixtures <dir>                writes the synthetic test atlases (WAV + .andoratlas)

PHRASE ANALYSIS (Phase 4; ported from ANDOR Lab, docs/research/unified-andor/phase-4-report.md)
  --phrase-analyze <file> [--output-atlas <f.andoratlas>] [--output-contour <f.csv>] [--backend yin|yin-viterbi]
        [--detection fewer|normal|more] [--min-note <s>] [--auto-tuning]
        A monophonic recording -> a Note Atlas (prints the notes, their doubts and the recording's verdict).
  --phrase-eval [--backend yin|yin-viterbi|all] [--fixture <name> | --stress] [--output-json <f>] [--verbose]
        The generated phrases against their exact ground truth.
  --phrase-eval-file <file> --truth <truth.json> [--backend ..] [--verbose]
  --phrase-corpus <dir> [--output-json <f>] [--output-md <f>] [--onset-tolerance <s>] [--detection fewer|normal|more] [--verbose]
        Every <name>.wav (or .flac/.aif) with a <name>.truth.json beside it ({"notes": [{"start", "end", "midi"}]}),
        one row per recording: P/R/F, octave and pitch errors, false splits, missed, onset error, tuning bias,
        confidence of right vs wrong notes, how many wrong notes were flagged, analysis time.
        --onset-tolerance (default 0.05 s, the usual note-onset criterion) can be widened to tell
        attacks placed a little early or late (breath, slow speech) from real segmentation errors.
  --phrase-fixtures <dir>     write the generated phrases (WAV) and their ground truth (truth JSON)
  --prepare-bench <file|dir> [--repeat <n>] [--threads <n>] [--output-json <f>] [--output-md <f>]
        Preparation cost per recording, stage by stage: decode, hash, analysis, then the instrument
        build (copies, per-region analysis, playable model, continuation) and Stage A alone.

BENCHMARK
  --benchmark [--engine A|C] [--voices 24] [--seconds 20] [--sample-rate 48000] [--block-size 128] [--output-json <f>] [--no-retrigger] [--transpose <semitones>]

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

const std::set<std::string> flagOptions = { "--help", "--version", "--benchmark", "--no-audio", "--strict", "--no-retrigger", "--no-fill", "--as-recorded", "--stage-a",
                                            "--stress", "--verbose", "--auto-tuning", "--phrase-eval" };

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

/** --set atlas's extras: each atlas's memory and build time, how memory and lookup scale with
    the number of regions, and the melody through baselines A and B (one recorded note, resampled). */
int writeAtlasReport (const fs::path& dir, const std::vector<unified::Scene>& scenes, bool noAudio)
{
    namespace u = unified;
    std::string error;
    std::ostringstream md;
    auto out = json::object();
    md << "# Note Atlas: memory, preparation, lookup (" << u::platformTag() << ")\n\n"
       << "Every member owns a copy of its note (attackStart..sourceEnd) as OSP's playback buffers: memory is the "
          "members' approximateBytes (playback copies, transients, anchors, REIMAGINED data). Build: region copy, analysis, "
          "model and continuation per member, on one thread.\n\n"
       << "| Atlas | Notes | Region audio s | Member MB | MB per region second | Build ms | ms per note |\n|---|---|---|---|---|---|---|\n";
    std::set<std::string> atlases;
    for (const auto& scene : scenes)
    {
        for (const auto& layer : scene.layers)
            if (! layer.atlas.empty())
                atlases.insert (layer.atlas);
        if (! scene.swapAtlas.empty())
            atlases.insert (scene.swapAtlas);
    }
    auto rows = juce::var (juce::Array<juce::var>());
    auto row = [&] (const std::string& name, const atlasset::BuildReport& r) {
        const double seconds = static_cast<double> (r.regionFrames) / 48000.0;
        const double mb = static_cast<double> (r.memberBytes) / (1024.0 * 1024.0);
        md << "| " << name << " | " << r.notesUsed << " | " << json::round (seconds, 2) << " | " << json::round (mb, 2) << " | "
           << json::round (seconds > 0.0 ? mb / seconds : 0.0, 3) << " | " << json::round (r.buildMilliseconds, 1) << " | "
           << json::round (r.notesUsed > 0 ? r.buildMilliseconds / r.notesUsed : 0.0, 1) << " |\n";
        auto o = json::object();
        json::set (o, "atlas", json::str (name));
        json::set (o, "notes", r.notesUsed);
        json::set (o, "regionSeconds", json::number (seconds, 3));
        json::set (o, "memberBytes", static_cast<juce::int64> (r.memberBytes));
        json::set (o, "buildMilliseconds", json::number (r.buildMilliseconds, 2));
        rows.append (o);
    };
    for (const auto& a : atlases)
        row (a, u::atlasReport (a));
    // Scaling: more regions (one second each, stereo, 12 keys): memory, build time, lookup.
    md << "\n## Scaling with the number of regions\n\n"
       << "| Regions | Member MB | Build ms | Peak RSS after, MB | Lookup ns (groupFor, per key) |\n|---|---|---|---|---|\n";
    auto scaling = juce::var (juce::Array<juce::var>());
    for (int count : { 10, 50, 200 })
    {
        const auto name = "regions-" + std::to_string (count);
        const auto report = u::atlasReport (name);
        u::Scene probe;
        probe.layers = { u::LayerSpec { u::SourceId::vowelA3, name } };
        const auto* set = u::setsFor (probe)[0];
        double lookupNs = 0.0;
        if (set != nullptr)
        {
            // Every key, many times: the note-on path's group choice (members are then picked by
            // velocity and take within the group).
            volatile int sink = 0;
            const int rounds = 20000;
            const auto t0 = std::chrono::steady_clock::now();
            for (int r = 0; r < rounds; ++r)
                for (int key = 0; key < 128; ++key)
                    sink = sink + set->groupFor (key);
            lookupNs = std::chrono::duration<double, std::nano> (std::chrono::steady_clock::now() - t0).count() / (rounds * 128.0);
        }
        md << "| " << count << " | " << json::round (static_cast<double> (report.memberBytes) / (1024.0 * 1024.0), 2) << " | "
           << json::round (report.buildMilliseconds, 1) << " | " << json::round (u::peakResidentMegabytes(), 1) << " | " << json::round (lookupNs, 1) << " |\n";
        auto o = json::object();
        json::set (o, "regions", count);
        json::set (o, "memberBytes", static_cast<juce::int64> (report.memberBytes));
        json::set (o, "buildMilliseconds", json::number (report.buildMilliseconds, 2));
        json::set (o, "peakResidentMegabytes", json::number (u::peakResidentMegabytes(), 1));
        json::set (o, "lookupNanoseconds", json::number (lookupNs, 2));
        scaling.append (o);
    }
    json::set (out, "atlases", rows);
    json::set (out, "scaling", scaling);

    // Baselines A and B: the melody from one recorded note (the atlas's A3), resampled.
    if (const auto* melody = u::findScene (scenes, "atlas-oneshot-melody"))
    {
        const auto fixture = atlasfixtures::make ("sung-four");
        const auto& note = fixture.atlas.notes.front();
        const auto& audio = *fixture.audio.front().audio;
        auto region = AudioData::allocate (audio.numChannels(), note.sourceEnd - note.attackStart, audio.sampleRate);
        for (int c = 0; c < audio.numChannels(); ++c)
            std::copy (audio.channels[static_cast<std::size_t> (c)].begin() + note.attackStart, audio.channels[static_cast<std::size_t> (c)].begin() + note.sourceEnd,
                       region.channels[static_cast<std::size_t> (c)].begin());
        md << "\n## Baselines A and B (the melody from the recorded A3 alone)\n\n| Render | RMS dB | Hash |\n|---|---|---|\n";
        for (const auto engine : { EngineId::baselineA, EngineId::baselineB })
        {
            RenderConfig c;
            c.engine = engine;
            c.sampleRate = 48000.0;
            c.blockSize = 256;
            c.maxTailSeconds = 4.0;
            c.sampler.seed = 1;
            const auto rendered = renderSequence (region, note.midiPitch, melody->midi, c);
            const auto d = u::digest (rendered.audio);
            const std::string name = std::string ("atlas-melody-baseline-") + (engine == EngineId::baselineA ? "a" : "b");
            md << "| " << name << " | " << json::round (d.rmsDb, 2) << " | `" << d.hash << "` |\n";
            if (! noAudio && ! io::writeAudioFile (dir / (name + ".wav"), rendered.audio, io::SampleFormat::float32, error))
                return fail (exitProcessing, error);
        }
    }
    std::ofstream (dir / "atlas.md") << md.str();
    if (! json::writeFile (dir / "atlas.json", out, error))
        return fail (exitProcessing, error);
    return exitOk;
}

int commandUnified (const Args& args)
{
    namespace u = unified;
    const fs::path dir = args.get ("--unified");
    std::error_code ec;
    fs::create_directories (dir, ec);
    const auto set = args.get ("--set", "baseline");
    if (set != "baseline" && set != "keydna" && set != "phase2" && set != "budget" && set != "atlas")
        return fail (exitUsage, "--set is baseline, keydna, phase2, budget or atlas");
    auto scenes = set == "keydna" ? u::keyDnaScenes()
                : set == "phase2" ? u::phase2Scenes()
                : set == "budget" ? u::budgetScenes()
                : set == "atlas"  ? u::atlasScenes()
                                  : u::baselineScenes();
    // --with-keydna organic|expressive: the same scenes with that profile on every layer (cost and
    // sound of KEY DNA on the baseline's own material).
    if (args.has ("--with-keydna"))
    {
        const auto which = args.get ("--with-keydna");
        if (which != "organic" && which != "expressive")
            return fail (exitUsage, "--with-keydna is organic or expressive");
        const auto dna = which == "organic" ? u::organicKeyDna() : u::expressiveKeyDna();
        for (auto& scene : scenes)
            scene.config.keyDna = { dna, dna, dna };
    }
    const auto only = args.get ("--scene");
    const auto repeats = parseNumber (args.get ("--repeats", "3"));
    const auto platform = u::platformTag();
    std::string error;
    auto costs = juce::var (juce::Array<juce::var>());
    std::ostringstream md;
    md << "# Unified-engine scenes (" << platform << ")\n\n"
       << "| Scene | Seconds | RMS dB | Peak dB | Brightness | Width | Hash | Mean block % | Worst block % | Worst seen µs | Peak voices | Peak grains | Live grains |\n"
       << "|---|---|---|---|---|---|---|---|---|---|---|---|---|\n";
    int rendered = 0;
    for (const auto& scene : scenes)
    {
        if (! only.empty() && scene.name != only)
            continue;
        const auto out = u::render (scene);
        const auto d = u::digest (out.audio);
        if (! args.has ("--no-audio") && ! io::writeAudioFile (dir / (scene.name + ".wav"), out.audio, io::SampleFormat::float32, error))
            return fail (exitProcessing, error);
        if (! json::writeFile (dir / (scene.name + ".json"), u::digestToJson (scene, d, platform), error))
            return fail (exitProcessing, error);
        const auto cost = u::measure (scene, static_cast<int> (repeats.value_or (3.0)));
        auto c = json::object();
        json::set (c, "scene", json::str (scene.name));
        json::set (c, "renderedSeconds", json::number (cost.renderedSeconds, 3));
        json::set (c, "meanBlockMicros", json::number (cost.meanBlockMicros, 2));
        json::set (c, "p99BlockMicros", json::number (cost.p99BlockMicros, 2));
        json::set (c, "worstBlockMicros", json::number (cost.worstBlockMicros, 2));
        json::set (c, "meanBudgetPercent", json::number (cost.meanBudgetPercent, 3));
        json::set (c, "worstBudgetPercent", json::number (cost.worstBudgetPercent, 3));
        json::set (c, "peakVoices", cost.peakVoices);
        json::set (c, "peakGrains", cost.peakGrains);
        json::set (c, "peakLiveGrains", cost.peakLiveGrains);
        json::set (c, "worstObservedMicros", json::number (cost.worstObservedMicros, 2));
        json::set (c, "grainBudget", scene.config.engineSettings.grainBudget);
        json::set (c, "rmsDb", json::number (d.rmsDb, 3));
        costs.append (c);
        md << "| " << scene.name << " | " << json::round (d.seconds, 2) << " | " << json::round (d.rmsDb, 2) << " | " << json::round (d.peakDb, 2) << " | "
           << json::round (d.brightness, 3) << " | " << json::round (d.stereoWidth, 3) << " | `" << d.hash << "` | "
           << json::round (cost.meanBudgetPercent, 2) << " | " << json::round (cost.worstBudgetPercent, 2) << " | "
           << json::round (cost.worstObservedMicros, 0) << " | " << cost.peakVoices << " | " << cost.peakGrains << " | " << cost.peakLiveGrains << " |\n";
        std::cout << scene.name << ": " << json::round (d.seconds, 2) << " s, rms " << json::round (d.rmsDb, 2) << " dB, hash " << d.hash
                  << ", mean " << json::round (cost.meanBudgetPercent, 2) << " % of a " << scene.config.blockSize << "-sample block, "
                  << cost.peakGrains << " grains at most" << std::endl;
        ++rendered;
    }
    if (rendered == 0)
        return fail (exitUsage, "no scene named '" + only + "'");
    auto summary = json::object();
    json::set (summary, "platform", json::str (platform));
    json::set (summary, "peakResidentMegabytes", json::number (u::peakResidentMegabytes(), 1));
    // The engine's own footprint (its voices, layers and stages; the models are shared data).
    json::set (summary, "engineBytes", static_cast<juce::int64> (sizeof (InstrumentEngine)));
    json::set (summary, "voiceBytes", static_cast<juce::int64> (sizeof (InstrumentVoice)));
    json::set (summary, "scenes", costs);
    if (! json::writeFile (dir / "costs.json", summary, error))
        return fail (exitProcessing, error);
    if (set == "keydna" && only.empty())
    {
        // Each patch's keys on their own: the profile, and what the grains measurably did.
        const std::vector<int> keys { 36, 43, 48, 55, 60, 67, 72, 79, 84 };
        std::ostringstream km;
        km << "# KEY DNA: each key on its own (" << platform << ")\n\n"
           << "Each key played alone for 0.8 s with LIFE off (so only KEY DNA differs; the demonstration renders keep LIFE 30 %). "
              "Profile: POS offset (fraction of the recording), SIZE and DENS in octaves. Settings: the layer's POS, SIZE and DENS "
              "after the profile. Measured: the grains' mean read position (expected near POS), how many sound at once "
              "(expected near DENS x SIZE), level and brightness.\n";
        auto keysJson = juce::var (juce::Array<juce::var>());
        for (const char* patch : { "a-static", "b-organic", "c-expressive" })
        {
            const auto* scene = u::findScene (scenes, std::string ("keydna-") + patch + "-scale");
            if (scene == nullptr)
                continue;
            km << "\n## " << patch << "\n\n| Key | POS + | SIZE oct | DENS oct | POS | SIZE ms | DENS /s | Grain position | DENS x SIZE | Grains at once | RMS dB | Brightness |\n"
               << "|---|---|---|---|---|---|---|---|---|---|---|---|\n";
            for (const auto& m : u::measureKeys (*scene, keys))
            {
                km << "| " << m.key << " | " << json::round (m.posOffset, 3) << " | " << json::round (m.sizeOctaves, 3) << " | "
                   << json::round (m.densityOctaves, 3) << " | " << json::round (m.position, 3) << " | " << json::round (m.sizeMs, 0) << " | "
                   << json::round (m.density, 1) << " | " << json::round (m.meanGrainPosition, 3) << " | " << json::round (m.density * m.sizeMs / 1000.0, 2)
                   << " | " << json::round (m.meanGrains, 2) << " | " << json::round (m.rmsDb, 2) << " | " << json::round (m.brightness, 3) << " |\n";
                auto k = json::object();
                json::set (k, "patch", json::str (patch));
                json::set (k, "key", m.key);
                json::set (k, "posOffset", json::number (m.posOffset, 5));
                json::set (k, "sizeOctaves", json::number (m.sizeOctaves, 5));
                json::set (k, "densityOctaves", json::number (m.densityOctaves, 5));
                json::set (k, "spreadOffset", json::number (m.spreadOffset, 5));
                json::set (k, "position", json::number (m.position, 5));
                json::set (k, "sizeMs", json::number (m.sizeMs, 3));
                json::set (k, "density", json::number (m.density, 3));
                json::set (k, "meanGrainPosition", json::number (m.meanGrainPosition, 5));
                json::set (k, "meanGrains", json::number (m.meanGrains, 3));
                json::set (k, "rmsDb", json::number (m.rmsDb, 3));
                json::set (k, "brightness", json::number (m.brightness, 4));
                keysJson.append (k);
            }
        }
        std::ofstream (dir / "keys.md") << km.str();
        if (! json::writeFile (dir / "keys.json", keysJson, error))
            return fail (exitProcessing, error);
    }
    if (set == "atlas" && only.empty())
        if (const auto code = writeAtlasReport (dir, scenes, args.has ("--no-audio")); code != exitOk)
            return code;
    md << "\nPeak resident memory of the renderer: " << json::round (u::peakResidentMegabytes(), 1) << " MB. InstrumentEngine object: "
       << sizeof (InstrumentEngine) << " bytes (InstrumentVoice " << sizeof (InstrumentVoice) << " bytes each).\n";
    std::ofstream (dir / "summary.md") << md.str();
    std::cout << rendered << " scenes -> " << dir.string() << "\n";
    return exitOk;
}

int commandAtlas (const Args& args)
{
    if (! args.has ("--output"))
        return fail (exitUsage, "--output <file.wav> is required");
    if (args.has ("--midi") == args.has ("--fixture"))
        return fail (exitUsage, "give exactly one of --midi <file> or --fixture <name>");
    std::string error;
    const fs::path path = args.get ("--atlas");
    auto atlas = io::readNoteAtlas (path, error);
    if (! atlas)
        return fail (exitInput, error);
    if (args.has ("--max-transpose"))
    {
        const auto v = parseNumber (args.get ("--max-transpose"));
        if (! v || *v < 0.0)
            return fail (exitUsage, "invalid --max-transpose");
        atlas->mapping.maxTransposeSemitones = *v;
    }
    if (args.has ("--no-fill"))
        atlas->mapping.fill = false;
    if (args.has ("--as-recorded"))
        atlas->mapping.correctTuning = false;
    auto config = configFromArgs (args, error);
    if (! config)
        return fail (exitUsage, error);
    config->engine = EngineId::instrument;
    if (config->sampleRate <= 0.0)
        config->sampleRate = 48000.0;
    if (args.get ("--mode", "oneshot") == "granular")
        config->engineSettings.sourceMode[0] = SourceMode::granular;

    // The audio: by content hash through the atlas's own hints, then --search.
    std::vector<fs::path> search;
    if (args.has ("--search"))
        search.emplace_back (args.get ("--search"));
    std::vector<atlasset::SourceAudio> sources;
    for (const auto& src : atlas->sources)
    {
        const auto resolved = io::resolveSource (src, path.parent_path(), search);
        if (! resolved)
        {
            std::cerr << "missing: " << src.assetId << " (" << src.fileName << ", " << src.contentHash << ")\n";
            continue;
        }
        if (resolved->match != atlas::RelinkResult::exact)
            std::cerr << "warning: " << src.assetId << " found by its shape, not its exact content: " << resolved->path.string() << "\n";
        auto decoded = io::loadAudioFile (resolved->path);
        if (! decoded.ok)
            return fail (exitInput, decoded.error);
        sources.push_back ({ src.assetId, std::make_shared<const AudioData> (std::move (decoded.audio)) });
    }
    atlasset::BuildOptions options;
    options.instrument.interpolationZeroCrossings = config->engineSettings.interpolationZeroCrossings;
    options.instrument.seed = config->engineSettings.seed;
    // Phase 4: --stage-a plays the instrument as Stage A publishes it (no continuation yet).
    const auto built = args.has ("--stage-a") ? atlasset::buildPlayableSet (*atlas, sources, options).playable
                                              : atlasset::buildInstrumentSet (*atlas, sources, options);
    for (const auto& w : built.report.warnings)
        std::cerr << "warning: " << w << "\n";
    if (built.set == nullptr)
        return fail (exitProcessing, built.report.errors.empty() ? std::string ("the atlas could not be built") : built.report.errors.front());
    std::cout << "atlas " << atlas->atlasId << ": " << built.report.notesUsed << " notes (" << built.report.notesSkipped << " skipped), "
              << json::round (static_cast<double> (built.report.memberBytes) / (1024.0 * 1024.0), 2) << " MB, built in "
              << json::round (built.report.buildMilliseconds, 1) << " ms; FILL " << (atlas->mapping.fill ? "on" : "off") << ", max "
              << atlas->mapping.maxTransposeSemitones << " semitones, " << (atlas->mapping.correctTuning ? "in tune" : "as recorded") << "\n";

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
        int reference = 127;
        for (const auto& n : atlas->notes)
            if (n.included)
                reference = std::min (reference, n.playKey());
        auto fixture = fixtures::byName (args.get ("--fixture"), reference);
        if (! fixture)
            return fail (exitUsage, "unknown fixture '" + args.get ("--fixture") + "'");
        sequence = std::move (*fixture);
    }
    const auto output = renderSet (*built.set, sequence, *config);
    if (! io::writeAudioFile (args.get ("--output"), output.audio, io::SampleFormat::float32, error))
        return fail (exitProcessing, error);
    std::cout << "wrote " << args.get ("--output") << "\n";
    return exitOk;
}

int commandWriteAtlasFixtures (const Args& args)
{
    const fs::path dir = args.get ("--write-atlas-fixtures");
    std::error_code ec;
    fs::create_directories (dir, ec);
    std::string error;
    for (const auto& name : atlasfixtures::names())
    {
        auto fixture = atlasfixtures::make (name);
        for (std::size_t i = 0; i < fixture.audio.size(); ++i)
        {
            auto& src = fixture.atlas.sources[i];
            const auto file = dir / (name + (fixture.audio.size() > 1 ? "-" + std::to_string (i) : std::string()) + ".wav");
            if (! io::writeAudioFile (file, *fixture.audio[i].audio, io::SampleFormat::float32, error))
                return fail (exitProcessing, error);
            const auto hash = io::sha256OfFile (file);
            if (! hash)
                return fail (exitProcessing, "cannot read " + file.string());
            src.contentHash = io::contentId (*hash);   // the file is the asset now
            src.fileName = file.filename().string();
            src.relativePath = src.fileName;
            src.lastKnownPath.clear();
        }
        const auto atlasFile = dir / (name + io::noteAtlasExtension);
        if (! io::writeNoteAtlas (atlasFile, fixture.atlas, error))
            return fail (exitProcessing, error);
        std::cout << "wrote " << atlasFile.string() << "\n";
    }
    return exitOk;
}

//==============================================================================
// Phrase analysis (Phase 4)

std::vector<phrase::PitchBackend> phraseBackends (const std::string& text)
{
    if (text == "yin")
        return { phrase::PitchBackend::yin };
    if (text == "all")
        return { phrase::PitchBackend::yin, phrase::PitchBackend::yinViterbi };
    return { phrase::PitchBackend::yinViterbi };
}

std::string fixed (double v, int decimals)
{
    char buffer[64];
    std::snprintf (buffer, sizeof (buffer), "%.*f", decimals, v);
    return buffer;
}

std::string doubtsOf (const atlas::Diagnostics& d)
{
    std::string out;
    for (const auto& [flag, name] : { std::pair { d.uncertainPitch, "pitch?" }, { d.possibleOctaveError, "octave?" }, { d.uncertainBoundary, "boundary?" },
                                      { d.unvoiced, "unvoiced" }, { d.possibleSplit, "split?" }, { d.possibleRepeat, "repeat?" } })
        if (flag)
            out += std::string (out.empty() ? "" : " ") + name;
    return out;
}

void printPhraseNotes (const atlas::NoteAtlas& a)
{
    const double sr = a.sources.empty() ? 48000.0 : a.sources[0].sampleRate;
    for (const auto& n : a.notes)
        std::cout << "  " << n.id << "  " << fixed (static_cast<double> (n.attackStart) / sr, 3) << "-" << fixed (static_cast<double> (n.sourceEnd) / sr, 3)
                  << " s  " << midiNoteName (n.rootMidi) << " " << (n.centsOffset >= 0 ? "+" : "") << fixed (n.centsOffset, 0) << "c"
                  << "  conf " << fixed (n.confidence, 2) << "  stab " << fixed (n.pitchStabilityCents, 0) << "c"
                  << (n.vibratoDepthCents > 0 ? "  vib " + fixed (n.vibratoDepthCents, 0) + "c@" + fixed (n.vibratoRateHz, 1) + "Hz" : std::string())
                  << "  " << atlas::toString (n.articulation) << (n.included ? "" : "  (excluded)")
                  << (n.diagnostics.any() ? "  [" + doubtsOf (n.diagnostics) + "]" : std::string()) << "\n";
}

juce::var evaluationJson (const std::string& name, phrase::PitchBackend backend, const phrase::EvaluationResult& r, double seconds, double audioSeconds)
{
    auto o = json::object();
    json::set (o, "recording", json::str (name));
    json::set (o, "backend", json::str (phrase::toString (backend)));
    json::set (o, "seconds", json::number (audioSeconds, 2));
    json::set (o, "truth", r.truthNotes);
    json::set (o, "detected", r.detectedNotes);
    json::set (o, "matched", r.matched);
    json::set (o, "precision", json::number (r.precision(), 3));
    json::set (o, "recall", json::number (r.recall(), 3));
    json::set (o, "f1", json::number (r.f1(), 3));
    json::set (o, "octaveErrors", r.octaveErrors);
    json::set (o, "pitchErrors", r.pitchErrors);
    json::set (o, "falseSplits", r.falseSplits);
    json::set (o, "missed", r.missed);
    json::set (o, "missedFlagged", r.missedFlagged);
    json::set (o, "spurious", r.spurious);
    json::set (o, "onsetErrorMs", json::number (r.meanOnsetErrorMs, 1));
    json::set (o, "medianOnsetErrorMs", json::number (r.medianOnsetErrorMs, 1));
    json::set (o, "offsetErrorMs", json::number (r.meanOffsetErrorMs, 1));
    json::set (o, "pitchErrorCents", json::number (r.meanPitchErrorCents, 1));
    json::set (o, "signedPitchErrorCents", json::number (r.meanSignedPitchErrorCents, 1));
    json::set (o, "correctConfidence", json::number (r.correctConfidence, 3));
    json::set (o, "wrongConfidence", json::number (r.wrongConfidence, 3));
    json::set (o, "wrongDetected", r.wrongDetected);
    json::set (o, "wrongFlagged", r.wrongFlagged);
    json::set (o, "correctFlagged", r.correctFlagged);
    json::set (o, "analysisMs", json::number (1000.0 * seconds, 1));
    return o;
}

void printEvaluation (const std::string& name, phrase::PitchBackend backend, const phrase::EvaluationResult& r, double seconds, double audioSeconds)
{
    char line[512];
    std::snprintf (line, sizeof (line),
                   "%-26s %-11s %3d/%-3d P %.2f R %.2f F %.2f  oct %d pitch %d split %d miss %d spur %d  on %5.1f ms  %+5.1f c  conf %.2f/%.2f  flagged %d/%d (%d)  %6.0f ms (%.0fx)",
                   name.c_str(), phrase::toString (backend), r.matched, r.truthNotes, r.precision(), r.recall(), r.f1(), r.octaveErrors,
                   r.pitchErrors, r.falseSplits, r.missed, r.spurious, r.meanOnsetErrorMs, r.meanSignedPitchErrorCents, r.correctConfidence,
                   r.wrongConfidence, r.wrongFlagged, r.wrongDetected, r.correctFlagged, 1000.0 * seconds, seconds > 0 ? audioSeconds / seconds : 0.0);
    std::cout << line << "\n";
}

void accumulate (phrase::EvaluationResult& total, const phrase::EvaluationResult& r)
{
    total.truthNotes += r.truthNotes;
    total.detectedNotes += r.detectedNotes;
    total.matched += r.matched;
    total.octaveErrors += r.octaveErrors;
    total.pitchErrors += r.pitchErrors;
    total.falseSplits += r.falseSplits;
    total.missed += r.missed;
    total.missedFlagged += r.missedFlagged;
    total.spurious += r.spurious;
    total.meanOnsetErrorMs += r.meanOnsetErrorMs * r.matched;
    total.meanOffsetErrorMs += r.meanOffsetErrorMs * r.matched;
    total.meanPitchErrorCents += r.meanPitchErrorCents * r.matched;
    total.meanSignedPitchErrorCents += r.meanSignedPitchErrorCents * r.matched;
    total.correctConfidence += r.correctConfidence * r.matched;
    total.wrongConfidence += r.wrongConfidence * r.wrongDetected;
    total.wrongDetected += r.wrongDetected;
    total.wrongFlagged += r.wrongFlagged;
    total.correctFlagged += r.correctFlagged;
}

void finishTotal (phrase::EvaluationResult& total)
{
    if (total.matched > 0)
    {
        total.meanOnsetErrorMs /= total.matched;
        total.meanOffsetErrorMs /= total.matched;
        total.meanPitchErrorCents /= total.matched;
        total.meanSignedPitchErrorCents /= total.matched;
        total.correctConfidence /= total.matched;
    }
    if (total.wrongDetected > 0)
        total.wrongConfidence /= total.wrongDetected;
}

std::optional<atlas::SourceAsset> describeFile (const fs::path& file, std::string& error)
{
    const auto hash = io::sha256OfFile (file);
    if (! hash)
    {
        error = "cannot read " + file.string();
        return std::nullopt;
    }
    atlas::SourceAsset s;
    s.assetId = "src0";
    s.contentHash = io::contentId (*hash);
    s.fileName = file.filename().string();
    std::error_code ec;
    s.lastKnownPath = fs::absolute (file, ec).string();
    return s;
}

std::optional<phrase::AnalysisSettings> phraseSettingsFromArgs (const Args& args, std::string& error)
{
    phrase::AnalysisSettings settings;
    settings.backend = phraseBackends (args.get ("--backend", "yin-viterbi")).front();
    settings.autoTuning = args.has ("--auto-tuning");
    const auto detection = args.get ("--detection", "normal");
    if (detection != "fewer" && detection != "normal" && detection != "more")
    {
        error = "--detection must be fewer, normal or more";
        return std::nullopt;
    }
    settings.detection = detection == "fewer" ? phrase::Detection::fewer : (detection == "more" ? phrase::Detection::more : phrase::Detection::normal);
    if (args.has ("--min-note"))
        settings.minPieceSeconds = std::clamp (parseNumber (args.get ("--min-note")).value_or (0.25), 0.0, 2.0);
    return settings;
}

int commandPhraseAnalyze (const Args& args)
{
    const fs::path file = args.get ("--phrase-analyze");
    const auto loaded = io::loadAudioFile (file);
    if (! loaded.ok)
        return fail (exitInput, loaded.error);
    std::string error;
    const auto source = describeFile (file, error);
    if (! source)
        return fail (exitInput, error);
    const auto settings = phraseSettingsFromArgs (args, error);
    if (! settings)
        return fail (exitUsage, error);
    const auto result = phrase::analysePhrase (loaded.audio, *source, *settings);
    for (const auto& w : result.warnings)
        std::cerr << "warning: " << w << "\n";
    std::cout << file.filename().string() << ": " << result.atlas.notes.size() << " notes in " << fixed (1000.0 * result.analysisSeconds, 0) << " ms ("
              << phrase::toString (settings->backend) << "); recording: " << result.atlas.analysis.suitability << ", "
              << fixed (result.atlas.analysis.pitchedSeconds, 1) << " of " << fixed (result.atlas.analysis.analysedSeconds, 1) << " s pitched\n";
    printPhraseNotes (result.atlas);
    if (args.has ("--output-atlas"))
    {
        if (! io::writeNoteAtlas (args.get ("--output-atlas"), result.atlas, error))
            return fail (exitProcessing, error);
        std::cout << "wrote " << args.get ("--output-atlas") << "\n";
    }
    if (args.has ("--output-contour"))
    {
        // Frame by frame what segmentation sees (plots, tuning on real recordings).
        std::ofstream out (args.get ("--output-contour"));
        if (! out)
            return fail (exitProcessing, "cannot write " + args.get ("--output-contour"));
        out << "seconds,hz,midi,confidence,levelDb,voiced\n";
        for (std::size_t i = 0; i < result.contour.frames.size(); ++i)
        {
            const auto& f = result.contour.frames[i];
            out << fixed (result.contour.timeOf (i), 4) << ',' << fixed (f.hz, 3) << ',' << fixed (f.midi, 4) << ',' << fixed (f.confidence, 3) << ','
                << fixed (f.levelDb, 2) << ',' << (f.voiced ? 1 : 0) << '\n';
        }
    }
    return exitOk;
}

int commandPhraseEval (const Args& args)
{
    auto names = phrase::phraseFixtureNames();
    if (args.has ("--fixture"))
        names = { args.get ("--fixture") };
    if (args.has ("--stress"))
        names = phrase::phraseStressFixtureNames();
    auto report = juce::var (juce::Array<juce::var>());
    atlas::SourceAsset src;
    src.contentHash = "sha256:fixture";
    for (auto backend : phraseBackends (args.get ("--backend", "all")))
    {
        phrase::EvaluationResult total;
        double totalSeconds = 0.0, totalAudio = 0.0;
        for (const auto& name : names)
        {
            const auto fx = phrase::makePhraseFixture (name);
            if (fx.audio.isEmpty())
                return fail (exitUsage, "unknown fixture '" + name + "'");
            phrase::AnalysisSettings settings;
            settings.backend = backend;
            const auto result = phrase::analysePhrase (fx.audio, src, settings);
            const auto r = phrase::evaluate (result.atlas, fx.notes);
            printEvaluation (name, backend, r, result.analysisSeconds, fx.audio.durationSeconds());
            if (args.has ("--verbose"))
                printPhraseNotes (result.atlas);
            report.append (evaluationJson (name, backend, r, result.analysisSeconds, fx.audio.durationSeconds()));
            accumulate (total, r);
            totalSeconds += result.analysisSeconds;
            totalAudio += fx.audio.durationSeconds();
        }
        finishTotal (total);
        printEvaluation ("TOTAL", backend, total, totalSeconds, totalAudio);
        report.append (evaluationJson ("TOTAL", backend, total, totalSeconds, totalAudio));
        std::cout << "\n";
    }
    std::string error;
    if (args.has ("--output-json") && ! json::writeFile (args.get ("--output-json"), report, error))
        return fail (exitProcessing, error);
    return exitOk;
}

std::optional<std::vector<phrase::TruthNote>> readTruth (const fs::path& file, std::string& error)
{
    const auto truthJson = json::readFile (file, error);
    if (! truthJson)
        return std::nullopt;
    std::vector<phrase::TruthNote> truth;
    if (const auto* list = (*truthJson)["notes"].getArray())
        for (const auto& n : *list)
            truth.push_back ({ json::getDouble (n, "start", 0.0), json::getDouble (n, "end", 0.0), json::getDouble (n, "midi", 60.0) });
    return truth;
}

int commandPhraseEvalFile (const Args& args)
{
    const fs::path file = args.get ("--phrase-eval-file");
    if (! args.has ("--truth"))
        return fail (exitUsage, "--phrase-eval-file needs --truth <truth.json>");
    const auto loaded = io::loadAudioFile (file);
    if (! loaded.ok)
        return fail (exitInput, loaded.error);
    std::string error;
    const auto truth = readTruth (args.get ("--truth"), error);
    if (! truth)
        return fail (exitInput, error);
    const auto source = describeFile (file, error);
    if (! source)
        return fail (exitInput, error);
    for (auto backend : phraseBackends (args.get ("--backend", "all")))
    {
        phrase::AnalysisSettings settings;
        settings.backend = backend;
        const auto result = phrase::analysePhrase (loaded.audio, *source, settings);
        printEvaluation (file.filename().string(), backend, phrase::evaluate (result.atlas, *truth), result.analysisSeconds, loaded.audio.durationSeconds());
        if (args.has ("--verbose"))
            printPhraseNotes (result.atlas);
    }
    return exitOk;
}

int commandPhraseCorpus (const Args& args)
{
    const fs::path dir = args.get ("--phrase-corpus");
    std::vector<fs::path> files;
    std::error_code ec;
    for (const auto& e : fs::directory_iterator (dir, ec))
    {
        const auto ext = e.path().extension().string();
        if ((ext == ".wav" || ext == ".flac" || ext == ".aif" || ext == ".aiff") && fs::exists (fs::path (e.path()).replace_extension (".truth.json")))
            files.push_back (e.path());
    }
    std::sort (files.begin(), files.end());
    if (files.empty())
        return fail (exitInput, "no <name>.wav with a <name>.truth.json in " + dir.string());
    auto rows = juce::var (juce::Array<juce::var>());
    std::ostringstream md;
    md << "| Recording | s | Truth | Found | Matched | P | R | F | Octave | Pitch | Splits | Missed (offered as a split) | Spurious | Onset ms (median) | Tuning bias c | Conf right / wrong | Wrong flagged | Verdict | Analysis ms |\n"
       << "|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|\n";
    phrase::EvaluationResult total;
    double totalSeconds = 0.0, totalAudio = 0.0;
    std::string error;
    const auto settings = phraseSettingsFromArgs (args, error);
    if (! settings)
        return fail (exitUsage, error);
    for (const auto& file : files)
    {
        const auto loaded = io::loadAudioFile (file);
        if (! loaded.ok)
        {
            std::cerr << "skipped " << file.string() << ": " << loaded.error << "\n";
            continue;
        }
        const auto truth = readTruth (fs::path (file).replace_extension (".truth.json"), error);
        const auto source = describeFile (file, error);
        if (! truth || ! source)
        {
            std::cerr << "skipped " << file.string() << ": " << error << "\n";
            continue;
        }
        const auto result = phrase::analysePhrase (loaded.audio, *source, *settings);
        phrase::EvaluationSettings tolerance;
        tolerance.onsetToleranceSeconds = std::stod (args.get ("--onset-tolerance", "0.05"));
        const auto r = phrase::evaluate (result.atlas, *truth, tolerance);
        const auto name = file.stem().string();
        printEvaluation (name, phrase::PitchBackend::yinViterbi, r, result.analysisSeconds, loaded.audio.durationSeconds());
        if (args.has ("--verbose"))
            printPhraseNotes (result.atlas);
        auto row = evaluationJson (name, phrase::PitchBackend::yinViterbi, r, result.analysisSeconds, loaded.audio.durationSeconds());
        json::set (row, "suitability", json::str (result.atlas.analysis.suitability));
        json::set (row, "uncovered", static_cast<int> (result.atlas.analysis.uncovered.size()));
        rows.append (row);
        md << "| " << name << " | " << fixed (loaded.audio.durationSeconds(), 1) << " | " << r.truthNotes << " | " << r.detectedNotes << " | " << r.matched << " | "
           << fixed (r.precision(), 2) << " | " << fixed (r.recall(), 2) << " | " << fixed (r.f1(), 2) << " | " << r.octaveErrors << " | " << r.pitchErrors << " | "
           << r.falseSplits << " | " << r.missed << " (" << r.missedFlagged << ") | " << r.spurious << " | " << fixed (r.meanOnsetErrorMs, 0) << " (" << fixed (r.medianOnsetErrorMs, 0) << ") | "
           << fixed (r.meanSignedPitchErrorCents, 1) << " | " << fixed (r.correctConfidence, 2) << " / " << fixed (r.wrongConfidence, 2) << " | "
           << r.wrongFlagged << "/" << r.wrongDetected << " | " << result.atlas.analysis.suitability << " | " << fixed (1000.0 * result.analysisSeconds, 0) << " |\n";
        accumulate (total, r);
        totalSeconds += result.analysisSeconds;
        totalAudio += loaded.audio.durationSeconds();
    }
    finishTotal (total);
    printEvaluation ("TOTAL", phrase::PitchBackend::yinViterbi, total, totalSeconds, totalAudio);
    md << "| **all** | " << fixed (totalAudio, 1) << " | " << total.truthNotes << " | " << total.detectedNotes << " | " << total.matched << " | "
       << fixed (total.precision(), 2) << " | " << fixed (total.recall(), 2) << " | " << fixed (total.f1(), 2) << " | " << total.octaveErrors << " | "
       << total.pitchErrors << " | " << total.falseSplits << " | " << total.missed << " (" << total.missedFlagged << ") | " << total.spurious << " | " << fixed (total.meanOnsetErrorMs, 0)
       << " | " << fixed (total.meanSignedPitchErrorCents, 1) << " | " << fixed (total.correctConfidence, 2) << " / " << fixed (total.wrongConfidence, 2)
       << " | " << total.wrongFlagged << "/" << total.wrongDetected << " | | " << fixed (1000.0 * totalSeconds, 0) << " |\n";
    rows.append (evaluationJson ("TOTAL", phrase::PitchBackend::yinViterbi, total, totalSeconds, totalAudio));
    if (args.has ("--output-json") && ! json::writeFile (args.get ("--output-json"), rows, error))
        return fail (exitProcessing, error);
    if (args.has ("--output-md"))
        std::ofstream (args.get ("--output-md")) << md.str();
    return exitOk;
}

/** Phase 4: where preparing a recording's instrument spends its time, stage by stage (decode,
    content hash, phrase analysis, region copies, per-region analysis, playable model,
    continuation), and what Stage A (playable, no continuation) costs against the full build. */
int commandPrepareBench (const Args& args)
{
    const fs::path in = args.get ("--prepare-bench");
    std::vector<fs::path> files;
    std::error_code ec;
    if (fs::is_directory (in, ec))
    {
        for (const auto& e : fs::directory_iterator (in, ec))
        {
            const auto ext = e.path().extension().string();
            if (ext == ".wav" || ext == ".flac" || ext == ".aif" || ext == ".aiff")
                files.push_back (e.path());
        }
        std::sort (files.begin(), files.end());
    }
    else
        files.push_back (in);
    const int repeat = std::clamp (static_cast<int> (parseNumber (args.get ("--repeat", "1")).value_or (1.0)), 1, 20);
    const int threads = std::clamp (static_cast<int> (parseNumber (args.get ("--threads", "1")).value_or (1.0)), 1, 8);
    auto ms = [] (auto t) { return std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - t).count(); };
    auto rows = juce::var (juce::Array<juce::var>());
    std::ostringstream md;
    md << "| Recording | Audio s | Notes | Decode ms | Hash ms | Analysis ms | Copy ms | Region analysis ms | Playable ms | Continuation ms | "
          "Full build ms | Stage A ms | Full MB | Stage A MB |\n"
       << "|---|---|---|---|---|---|---|---|---|---|---|---|---|---|\n";
    std::string error;
    for (const auto& file : files)
    {
        // Each figure is the median of --repeat runs.
        std::vector<double> decode, hash, analysis, copy, regionAnalysis, playable, continuation, full, stageA;
        atlasset::BuildReport fullReport, aReport;
        int notes = 0;
        double seconds = 0.0;
        for (int i = 0; i < repeat; ++i)
        {
            auto t = std::chrono::steady_clock::now();
            auto loaded = io::loadAudioFile (file);
            decode.push_back (ms (t));
            if (! loaded.ok)
                break;
            seconds = loaded.audio.durationSeconds();
            t = std::chrono::steady_clock::now();
            const auto source = describeFile (file, error);
            hash.push_back (ms (t));
            if (! source)
                break;
            const auto result = phrase::analysePhrase (loaded.audio, *source, {});
            analysis.push_back (result.analysisSeconds * 1000.0);
            notes = static_cast<int> (std::count_if (result.atlas.notes.begin(), result.atlas.notes.end(), [] (const auto& n) { return n.included; }));
            auto audio = std::make_shared<const AudioData> (std::move (loaded.audio));
            atlasset::BuildOptions options;
            options.threads = threads;
            t = std::chrono::steady_clock::now();
            const auto built = atlasset::buildInstrumentSet (result.atlas, { { source->assetId, audio } }, options);
            const double fullMs = ms (t);
            fullReport = built.report;
            copy.push_back (built.report.copyMilliseconds);
            regionAnalysis.push_back (built.report.analysisMilliseconds);
            playable.push_back (built.report.provisionalMilliseconds);
            continuation.push_back (built.report.continuationMilliseconds);
            full.push_back (fullMs);
            t = std::chrono::steady_clock::now();
            const auto a = atlasset::buildPlayableSet (result.atlas, { { source->assetId, audio } }, options);
            stageA.push_back (ms (t));
            aReport = a.playable.report;
        }
        if (full.empty())
        {
            std::cerr << "skipped " << file.string() << "\n";
            continue;
        }
        auto median = [] (std::vector<double> v) { std::sort (v.begin(), v.end()); return v.empty() ? 0.0 : v[v.size() / 2]; };
        const auto mb = [] (std::size_t b) { return static_cast<double> (b) / (1024.0 * 1024.0); };
        const auto name = file.filename().string();
        md << "| " << name << " | " << json::round (seconds, 2) << " | " << notes << " | " << json::round (median (decode), 1) << " | "
           << json::round (median (hash), 1) << " | " << json::round (median (analysis), 1) << " | " << json::round (median (copy), 1) << " | "
           << json::round (median (regionAnalysis), 1) << " | " << json::round (median (playable), 1) << " | " << json::round (median (continuation), 1)
           << " | " << json::round (median (full), 1) << " | " << json::round (median (stageA), 1) << " | " << json::round (mb (fullReport.memberBytes), 2)
           << " | " << json::round (mb (aReport.memberBytes), 2) << " |\n";
        auto o = json::object();
        json::set (o, "recording", json::str (name));
        json::set (o, "audioSeconds", json::number (seconds, 3));
        json::set (o, "notes", notes);
        for (const auto& [key, v] : { std::pair { "decodeMs", &decode }, { "hashMs", &hash }, { "analysisMs", &analysis }, { "copyMs", &copy },
                                      { "regionAnalysisMs", &regionAnalysis }, { "playableMs", &playable }, { "continuationMs", &continuation },
                                      { "fullBuildMs", &full }, { "stageAMs", &stageA } })
            json::set (o, key, json::number (median (*v), 2));
        json::set (o, "fullBytes", static_cast<double> (fullReport.memberBytes));
        json::set (o, "stageABytes", static_cast<double> (aReport.memberBytes));
        rows.getArray()->add (o);
        std::cout << name << ": " << notes << " notes, " << json::round (seconds, 1) << " s; analysis " << json::round (median (analysis), 1)
                  << " ms, build " << json::round (median (full), 1) << " ms (copy " << json::round (median (copy), 1) << ", region analysis "
                  << json::round (median (regionAnalysis), 1) << ", playable " << json::round (median (playable), 1) << ", continuation "
                  << json::round (median (continuation), 1) << "), Stage A " << json::round (median (stageA), 1) << " ms\n";
    }
    if (args.has ("--output-json"))
    {
        auto summary = json::object();
        json::set (summary, "schemaVersion", 1);
        json::set (summary, "kind", json::str ("osp-prepare-bench"));
        json::set (summary, "repeat", repeat);
        json::set (summary, "threads", threads);
        json::set (summary, "recordings", rows);
        if (! json::writeFile (args.get ("--output-json"), summary, error))
            return fail (exitProcessing, error);
    }
    if (args.has ("--output-md"))
        std::ofstream (args.get ("--output-md")) << md.str();
    return exitOk;
}

int commandPhraseFixtures (const Args& args)
{
    const fs::path dir = args.get ("--phrase-fixtures");
    std::error_code ec;
    fs::create_directories (dir, ec);
    auto names = phrase::phraseFixtureNames();
    for (const auto& name : phrase::phraseStressFixtureNames())
        names.push_back (name);
    for (const auto& name : names)
    {
        const auto fx = phrase::makePhraseFixture (name);
        std::string error;
        if (! io::writeAudioFile (dir / (name + ".wav"), fx.audio, io::SampleFormat::pcm24, error))
            return fail (exitProcessing, error);
        auto truth = json::object();
        json::set (truth, "fixture", json::str (name));
        json::set (truth, "description", json::str (fx.description));
        auto notes = juce::var (juce::Array<juce::var>());
        for (const auto& n : fx.notes)
        {
            auto o = json::object();
            json::set (o, "start", json::number (n.startSeconds, 4));
            json::set (o, "end", json::number (n.endSeconds, 4));
            json::set (o, "midi", json::number (n.midi, 3));
            notes.append (o);
        }
        json::set (truth, "notes", notes);
        if (! json::writeFile (dir / (name + ".truth.json"), truth, error))
            return fail (exitProcessing, error);
    }
    std::cout << "wrote " << names.size() << " phrases to " << dir.string() << "\n";
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
    options.transposeSemitones = std::stoi (args.get ("--transpose", "0"));
    if (args.has ("--engine"))
    {
        const auto engine = parseEngine (args.get ("--engine"));
        if (! engine || *engine == EngineId::baselineB)
            return fail (exitUsage, "--benchmark supports --engine A or C");
        options.instrumentEngine = *engine == EngineId::instrument;
    }

    const auto r = runBenchmark (options);
    std::cout << "benchmark: " << options.voices << " voices, " << options.sampleRate << " Hz, block " << options.blockSize
              << (options.denseRetriggers ? ", dense retriggers" : "") << (options.instrumentEngine ? ", engine C" : ", baseline A") << "\n"
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
        if (args.has ("--unified"))
            return commandUnified (args);
        if (args.has ("--atlas"))
            return commandAtlas (args);
        if (args.has ("--phrase-analyze"))
            return commandPhraseAnalyze (args);
        if (args.has ("--phrase-eval"))
            return commandPhraseEval (args);
        if (args.has ("--phrase-eval-file"))
            return commandPhraseEvalFile (args);
        if (args.has ("--phrase-corpus"))
            return commandPhraseCorpus (args);
        if (args.has ("--prepare-bench"))
            return commandPrepareBench (args);
        if (args.has ("--phrase-fixtures"))
            return commandPhraseFixtures (args);
        if (args.has ("--write-atlas-fixtures"))
            return commandWriteAtlasFixtures (args);
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
                          << ", exits " << c.graftExits.size() << ", fluctuation " << juce::String (c.levelFluctuationDb, 2) << " dB / "
                          << juce::String (c.pitchFluctuationCents, 1) << " cents, " << juce::String (ms, 0) << " ms\n";
                const auto ch = instrument::estimateCharacter (source.analysis, &c);
                std::cout << "    character: transient " << juce::String (ch.transientTonal, 2) << ", sustained "
                          << juce::String (ch.sustainedHarmonic, 2) << ", expressive " << juce::String (ch.expressiveSustain, 2)
                          << ", modulation " << juce::String (ch.periodicModulation, 2) << ", noise " << juce::String (ch.noiseComponent, 2)
                          << "; attack " << juce::String (1000.0 * source.analysis.envelope.attackSeconds, 0) << " ms, decay "
                          << juce::String (source.analysis.envelope.decaySlopeDbPerSecond, 1) << " dB/s\n";
            }
            return exitOk;
        }
        if (args.has ("--register-test"))
        {
            std::vector<fs::path> files;
            for (const auto& entry : fs::directory_iterator (args.get ("--register-test")))
                if (entry.is_regular_file() && io::isSupportedAudioExtension (entry.path()))
                    files.push_back (entry.path());
            std::sort (files.begin(), files.end());
            std::cout << registerGroundTruth (files);
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
