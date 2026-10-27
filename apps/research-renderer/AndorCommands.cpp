#include "AndorCommands.h"

#include "andor/io/NoteAtlasJson.h"
#include "andor/phrase/PhraseAnalysis.h"
#include "andor/phrase/PhraseFixtures.h"
#include "core/PitchMath.h"
#include "io/AudioFileIO.h"
#include "io/ContentHash.h"
#include "io/JsonUtil.h"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <iostream>

namespace fs = std::filesystem;

namespace osp::andorcli
{

const char* usageText = R"(
ANDOR LAB (docs/andor-lab)
  --phrase-analyze <file> [--output-atlas <f.andoratlas>] [--backend yin|yin-viterbi] [--auto-tuning]
        Analyse a monophonic phrase into a Note Atlas; prints the notes.
  --phrase-eval [--backend yin|yin-viterbi|all] [--fixture <name> | --stress] [--output-json <f>] [--verbose]
        Run the analysis on the generated ground-truth fixtures and print note metrics.
  --phrase-eval-file <file> --truth <truth.json> [--backend ..]
        The same against your own annotated recording (truth: {"notes":[{"start":s,"end":s,"midi":n}]}).
  --phrase-fixtures <dir>     write the generated phrases (WAV) and their ground truth (JSON)
)";

namespace
{
    using Values = std::map<std::string, std::string>;
    using Flags = std::set<std::string>;

    std::string get (const Values& v, const std::string& key, const std::string& fallback = {})
    {
        const auto it = v.find (key);
        return it == v.end() ? fallback : it->second;
    }

    int fail (int code, const std::string& message)
    {
        std::cerr << "error: " << message << "\n";
        return code;
    }

    std::vector<phrase::PitchBackend> backendsFrom (const std::string& text)
    {
        if (text == "yin")
            return { phrase::PitchBackend::yin };
        if (text == "all")
            return { phrase::PitchBackend::yin, phrase::PitchBackend::yinViterbi };
        return { phrase::PitchBackend::yinViterbi };
    }

    std::string fmt (double v, int decimals)
    {
        char buffer[64];
        std::snprintf (buffer, sizeof (buffer), "%.*f", decimals, v);
        return buffer;
    }

    void printNotes (const atlas::NoteAtlas& a)
    {
        const double sr = a.sources.empty() ? 48000.0 : a.sources[0].sampleRate;
        for (const auto& n : a.notes)
            std::cout << "  " << n.id << "  " << fmt (static_cast<double> (n.attackStart) / sr, 3) << "-" << fmt (static_cast<double> (n.sourceEnd) / sr, 3)
                      << " s  " << midiNoteName (n.rootMidi) << " " << (n.centsOffset >= 0 ? "+" : "") << fmt (n.centsOffset, 0) << "c"
                      << "  conf " << fmt (n.confidence, 2) << "  stab " << fmt (n.pitchStabilityCents, 0) << "c"
                      << (n.vibratoDepthCents > 0 ? "  vib " + fmt (n.vibratoDepthCents, 0) + "c@" + fmt (n.vibratoRateHz, 1) + "Hz" : std::string())
                      << "  " << atlas::toString (n.articulation) << (n.included ? "" : "  (excluded)") << "\n";
    }

    juce::var resultJson (const std::string& name, phrase::PitchBackend backend, const phrase::EvaluationResult& r, double seconds)
    {
        auto o = json::object();
        json::set (o, "fixture", json::str (name));
        json::set (o, "backend", phrase::toString (backend));
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
        json::set (o, "spurious", r.spurious);
        json::set (o, "onsetErrorMs", json::number (r.meanOnsetErrorMs, 1));
        json::set (o, "offsetErrorMs", json::number (r.meanOffsetErrorMs, 1));
        json::set (o, "pitchErrorCents", json::number (r.meanPitchErrorCents, 1));
        json::set (o, "analysisMs", json::number (1000.0 * seconds, 1));
        return o;
    }

    void printRow (const std::string& name, phrase::PitchBackend backend, const phrase::EvaluationResult& r, double seconds, double audioSeconds)
    {
        char line[400];
        std::snprintf (line, sizeof (line), "%-18s %-12s %3d/%-3d  P %.2f R %.2f F %.2f  oct %d pitch %d split %d miss %d spur %d  on %5.1f ms off %6.1f ms  %4.1f c  %6.1f ms (%.0fx)",
                       name.c_str(), phrase::toString (backend), r.matched, r.truthNotes, r.precision(), r.recall(), r.f1(), r.octaveErrors,
                       r.pitchErrors, r.falseSplits, r.missed, r.spurious, r.meanOnsetErrorMs, r.meanOffsetErrorMs, r.meanPitchErrorCents,
                       1000.0 * seconds, seconds > 0 ? audioSeconds / seconds : 0.0);
        std::cout << line << "\n";
    }

    int phraseEval (const Values& v, const Flags& flags)
    {
        const auto backends = backendsFrom (get (v, "--backend", "all"));
        std::vector<std::string> names = phrase::phraseFixtureNames();
        if (v.count ("--fixture"))
            names = { get (v, "--fixture") };
        if (flags.count ("--stress"))
            names = phrase::phraseStressFixtureNames();
        auto report = json::array();
        for (auto backend : backends)
        {
            phrase::EvaluationResult total;
            double totalSeconds = 0.0, totalAudio = 0.0;
            for (const auto& name : names)
            {
                const auto fx = phrase::makePhraseFixture (name);
                if (fx.audio.isEmpty())
                    return fail (1, "unknown fixture '" + name + "'");
                phrase::AnalysisSettings settings;
                settings.backend = backend;
                const auto result = phrase::analysePhrase (fx.audio, {}, settings);
                const auto r = phrase::evaluate (result.atlas, fx.notes);
                printRow (name, backend, r, result.analysisSeconds, fx.audio.durationSeconds());
                if (flags.count ("--verbose"))
                    printNotes (result.atlas);
                report.append (resultJson (name, backend, r, result.analysisSeconds));
                total.truthNotes += r.truthNotes;
                total.detectedNotes += r.detectedNotes;
                total.matched += r.matched;
                total.octaveErrors += r.octaveErrors;
                total.pitchErrors += r.pitchErrors;
                total.falseSplits += r.falseSplits;
                total.missed += r.missed;
                total.spurious += r.spurious;
                total.meanOnsetErrorMs += r.meanOnsetErrorMs * r.matched;
                total.meanOffsetErrorMs += r.meanOffsetErrorMs * r.matched;
                total.meanPitchErrorCents += r.meanPitchErrorCents * r.matched;
                totalSeconds += result.analysisSeconds;
                totalAudio += fx.audio.durationSeconds();
            }
            if (total.matched > 0)
            {
                total.meanOnsetErrorMs /= total.matched;
                total.meanOffsetErrorMs /= total.matched;
                total.meanPitchErrorCents /= total.matched;
            }
            printRow ("TOTAL", backend, total, totalSeconds, totalAudio);
            report.append (resultJson ("TOTAL", backend, total, totalSeconds));
            std::cout << "\n";
        }
        if (v.count ("--output-json"))
        {
            std::string error;
            if (! json::writeFile (get (v, "--output-json"), report, error))
                return fail (3, error);
        }
        return 0;
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

    int phraseAnalyze (const Values& v, const Flags& flags)
    {
        const fs::path file = get (v, "--phrase-analyze");
        const auto loaded = io::loadAudioFile (file);
        if (! loaded.ok)
            return fail (2, loaded.error);
        std::string error;
        const auto source = describeFile (file, error);
        if (! source)
            return fail (2, error);
        phrase::AnalysisSettings settings;
        settings.backend = backendsFrom (get (v, "--backend", "yin-viterbi")).front();
        settings.autoTuning = flags.count ("--auto-tuning") > 0;
        const auto result = phrase::analysePhrase (loaded.audio, *source, settings);
        for (const auto& w : result.warnings)
            std::cerr << "warning: " << w << "\n";
        std::cout << file.filename().string() << ": " << result.atlas.notes.size() << " notes in " << fmt (1000.0 * result.analysisSeconds, 0)
                  << " ms (" << phrase::toString (settings.backend) << ")\n";
        printNotes (result.atlas);
        if (v.count ("--output-atlas"))
        {
            if (! io::writeNoteAtlas (get (v, "--output-atlas"), result.atlas, error))
                return fail (3, error);
            std::cout << "wrote " << get (v, "--output-atlas") << "\n";
        }
        return 0;
    }

    int phraseEvalFile (const Values& v)
    {
        const fs::path file = get (v, "--phrase-eval-file");
        if (! v.count ("--truth"))
            return fail (1, "--phrase-eval-file needs --truth <truth.json>");
        const auto loaded = io::loadAudioFile (file);
        if (! loaded.ok)
            return fail (2, loaded.error);
        std::string error;
        const auto truthJson = json::readFile (get (v, "--truth"), error);
        if (! truthJson)
            return fail (2, error);
        std::vector<phrase::TruthNote> truth;
        if (const auto* list = (*truthJson)["notes"].getArray())
            for (const auto& n : *list)
                truth.push_back ({ json::getDouble (n, "start", 0.0), json::getDouble (n, "end", 0.0), json::getDouble (n, "midi", 60.0) });
        for (auto backend : backendsFrom (get (v, "--backend", "all")))
        {
            phrase::AnalysisSettings settings;
            settings.backend = backend;
            const auto result = phrase::analysePhrase (loaded.audio, {}, settings);
            printRow (file.filename().string(), backend, phrase::evaluate (result.atlas, truth), result.analysisSeconds, loaded.audio.durationSeconds());
        }
        return 0;
    }

    int phraseFixtures (const Values& v)
    {
        const fs::path dir = get (v, "--phrase-fixtures");
        std::error_code ec;
        fs::create_directories (dir, ec);
        for (const auto& name : phrase::phraseFixtureNames())
        {
            const auto fx = phrase::makePhraseFixture (name);
            std::string error;
            if (! io::writeAudioFile (dir / (name + ".wav"), fx.audio, io::SampleFormat::pcm24, error))
                return fail (3, error);
            auto truth = json::object();
            json::set (truth, "fixture", json::str (name));
            json::set (truth, "description", json::str (fx.description));
            auto notes = json::array();
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
                return fail (3, error);
            std::cout << "wrote " << (dir / (name + ".wav")).string() << "\n";
        }
        return 0;
    }
}

const std::set<std::string>& flagOptions()
{
    static const std::set<std::string> flags { "--auto-tuning", "--verbose", "--phrase-eval", "--stress" };
    return flags;
}

bool handles (const Values& v, const Flags& flags)
{
    return flags.count ("--phrase-eval") || v.count ("--phrase-analyze") || v.count ("--phrase-eval-file") || v.count ("--phrase-fixtures");
}

int run (const Values& v, const Flags& flags)
{
    if (flags.count ("--phrase-eval"))
        return phraseEval (v, flags);
    if (v.count ("--phrase-analyze"))
        return phraseAnalyze (v, flags);
    if (v.count ("--phrase-eval-file"))
        return phraseEvalFile (v);
    if (v.count ("--phrase-fixtures"))
        return phraseFixtures (v);
    return 1;
}

} // namespace osp::andorcli
