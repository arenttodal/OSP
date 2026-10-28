#include "AndorCommands.h"

#include "analysis/Analyzer.h"
#include "analysis/pitch/YinPitchTracker.h"
#include "andor/dsp/OfflineRender.h"
#include "andor/io/NoteAtlasJson.h"
#include "andor/grain/GrainEngine.h"
#include "andor/phrase/PhraseEngine.h"
#include "midi/MidiFileIO.h"
#include "model/RootChoice.h"
#include "research/RenderSession.h"
#include "andor/phrase/PhraseAnalysis.h"
#include "andor/phrase/PhraseFixtures.h"
#include "core/PitchMath.h"
#include "io/AudioFileIO.h"
#include "io/ContentHash.h"
#include "io/JsonUtil.h"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace fs = std::filesystem;

namespace osp::andorcli
{

const char* usageText = R"(
ANDOR LAB (docs/andor-lab)
  --phrase-analyze <file> [--output-atlas <f.andoratlas>] [--output-contour <f.csv>] [--backend yin|yin-viterbi] [--auto-tuning]
        [--detection fewer|normal|more] [--min-note <seconds, default 0.25>]
        Analyse a monophonic phrase into a Note Atlas; prints the notes.
  --phrase-eval [--backend yin|yin-viterbi|all] [--fixture <name> | --stress] [--output-json <f>] [--verbose]
        Run the analysis on the generated ground-truth fixtures and print note metrics.
  --phrase-eval-file <file> --truth <truth.json> [--backend ..]
        The same against your own annotated recording (truth: {"notes":[{"start":s,"end":s,"midi":n}]}).
  --phrase-fixtures <dir>     write the generated phrases (WAV) and their ground truth (JSON)
  --phrase-render <file> (--midi <f.mid> | --demo melody|chords|hold|chromatic) --output <out.wav>
        [--atlas <f.andoratlas>] [--mode oneshot|gate|sustain] [--fill off|natural|balanced|wide|extended]
        [--round-robin sequential|random|shuffle] [--tuning corrected|original] [--character 0..1]
        [--synthetic 0..1] [--space 0..1] [--seed n]
        Analyse (or load an atlas for) a phrase and play MIDI through ANDOR Phrase.
  --andor-bench [--engine grain|phrase|all] [--seconds 3] [--preset typical|stress] [--output-json <f>]
        Callback timing over 44.1/48/96 kHz x blocks 64..512 x polyphony 1..16 (container numbers
        are not Mac numbers: report the hardware with them).
  --phrase-ab <file> --output-dir <dir> [--demo ..]
        The same demo through baseline A (plain resampler), baseline B (randomised) and Phrase.
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
        const auto detection = get (v, "--detection", "normal");
        if (detection != "fewer" && detection != "normal" && detection != "more")
            return fail (1, "--detection must be fewer, normal or more");
        settings.detection = detection == "fewer" ? phrase::Detection::fewer : (detection == "more" ? phrase::Detection::more : phrase::Detection::normal);
        if (v.count ("--min-note"))
            settings.minPieceSeconds = std::clamp (std::strtod (get (v, "--min-note").c_str(), nullptr), 0.0, 2.0);
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
        if (v.count ("--output-contour"))
        {
            // Frame-by-frame view of what segmentation sees (plotting, tuning on real recordings).
            std::ofstream out (get (v, "--output-contour"));
            if (! out)
                return fail (3, "cannot write " + get (v, "--output-contour"));
            out << "seconds,hz,midi,confidence,levelDb,voiced\n";
            for (std::size_t i = 0; i < result.contour.frames.size(); ++i)
            {
                const auto& f = result.contour.frames[i];
                out << fmt (result.contour.timeOf (i), 4) << ',' << fmt (f.hz, 3) << ',' << fmt (f.midi, 4) << ',' << fmt (f.confidence, 3) << ','
                    << fmt (f.levelDb, 2) << ',' << (f.voiced ? 1 : 0) << '\n';
            }
            std::cout << "wrote " << get (v, "--output-contour") << "\n";
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
        auto names = phrase::phraseFixtureNames();
        for (const auto& name : phrase::phraseStressFixtureNames())
            names.push_back (name);
        for (const auto& name : names)
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
    MidiSequence demoSequence (const std::string& name)
    {
        MidiSequence seq;
        seq.name = name;
        if (name == "chords")
        {
            const int chords[4][3] = { { 60, 64, 67 }, { 57, 60, 64 }, { 62, 65, 69 }, { 55, 59, 62 } };
            for (int c = 0; c < 4; ++c)
                for (int k : chords[c])
                    andor::addNote (seq, 1.6 * c, 1.4, k, 90);
        }
        else if (name == "hold")
        {
            andor::addNote (seq, 0.0, 8.0, 64, 100);
            andor::addNote (seq, 8.5, 6.0, 60, 90);
        }
        else if (name == "chromatic")
        {
            for (int k = 48; k <= 84; ++k)
                andor::addNote (seq, 0.25 * (k - 48), 0.22, k, 96);
        }
        else
        {
            // A short melody with repeats (round robin) and steps between recorded keys (fill).
            const int keys[] = { 60, 62, 64, 64, 65, 64, 62, 60, 67, 66, 64, 62, 60, 60 };
            for (int i = 0; i < 14; ++i)
                andor::addNote (seq, 0.4 * i, i == 13 ? 1.5 : 0.36, keys[i], 70 + (i * 13) % 50);
        }
        seq.sort();
        return seq;
    }

    std::optional<MidiSequence> sequenceFrom (const Values& v, std::string& error)
    {
        if (v.count ("--midi"))
        {
            auto midi = io::readMidiFile (get (v, "--midi"), error);
            if (! midi)
                return std::nullopt;
            return *midi;
        }
        return demoSequence (get (v, "--demo", "melody"));
    }

    phrase::PhraseParams phraseParamsFrom (const Values& v)
    {
        phrase::PhraseParams p;
        const auto mode = get (v, "--mode", "sustain");
        p.playMode = mode == "oneshot" ? phrase::PlayMode::oneShot : mode == "gate" ? phrase::PlayMode::gate : phrase::PlayMode::sustain;
        const auto fill = get (v, "--fill", "balanced");
        p.fill = fill == "off" ? phrase::FillMode::off : fill == "natural" ? phrase::FillMode::natural
               : fill == "wide" ? phrase::FillMode::wide : fill == "extended" ? phrase::FillMode::extended : phrase::FillMode::balanced;
        const auto rr = get (v, "--round-robin", "shuffle");
        p.roundRobin = rr == "sequential" ? phrase::RoundRobin::sequential : rr == "random" ? phrase::RoundRobin::random : phrase::RoundRobin::shuffle;
        p.tuning = get (v, "--tuning", "corrected") == "original" ? phrase::Tuning::original : phrase::Tuning::corrected;
        p.sustainCharacter = std::stod (get (v, "--character", "0"));
        p.synthetic = std::stod (get (v, "--synthetic", "0"));
        p.space = std::stod (get (v, "--space", "0"));
        p.seed = std::stoull (get (v, "--seed", std::to_string (p.seed)));
        return p;
    }

    struct LoadedPhrase
    {
        io::LoadResult audio;
        atlas::NoteAtlas atlas;
    };

    std::optional<LoadedPhrase> loadPhrase (const fs::path& file, const Values& v, std::string& error)
    {
        LoadedPhrase lp;
        lp.audio = io::loadAudioFile (file);
        if (! lp.audio.ok)
        {
            error = lp.audio.error;
            return std::nullopt;
        }
        if (v.count ("--atlas"))
        {
            auto a = io::readNoteAtlas (get (v, "--atlas"), error);
            if (! a)
                return std::nullopt;
            lp.atlas = *a;
        }
        else
        {
            const auto source = describeFile (file, error);
            if (! source)
                return std::nullopt;
            lp.atlas = phrase::analysePhrase (lp.audio.audio, *source).atlas;
        }
        return lp;
    }

    AudioData renderPhrase (const LoadedPhrase& lp, const MidiSequence& seq, const phrase::PhraseParams& params)
    {
        auto source = std::make_shared<const PlaybackSource> (lp.audio.audio, 60.0, phrase::phrasePadding());
        const auto model = phrase::buildPhraseModel (source, lp.atlas, 1);
        phrase::PhraseEngine engine;
        engine.prepare (48000.0, 128);
        engine.setModel (model.get());
        engine.setParams (params);
        return andor::renderOffline (engine, seq, 48000.0, 128, 3.0);
    }

    int phraseRender (const Values& v)
    {
        if (! v.count ("--output"))
            return fail (1, "--phrase-render needs --output <out.wav>");
        std::string error;
        const auto lp = loadPhrase (get (v, "--phrase-render"), v, error);
        if (! lp)
            return fail (2, error);
        const auto seq = sequenceFrom (v, error);
        if (! seq)
            return fail (2, error);
        const auto out = renderPhrase (*lp, *seq, phraseParamsFrom (v));
        if (! io::writeAudioFile (get (v, "--output"), out, io::SampleFormat::float32, error))
            return fail (3, error);
        std::cout << "rendered " << get (v, "--output") << " (" << lp->atlas.notes.size() << " notes in the atlas)\n";
        return 0;
    }


    /** Per-note pitch accuracy of a render against the keys that were played (objective
        companion to listening): mean |cents| and the share of notes within 10 cents. */
    std::string pitchReport (const AudioData& out, const MidiSequence& seq)
    {
        YinPitchTracker yin (out.sampleRate, 30.0, 3000.0, 0.15);
        const auto& x = out.channels[0];
        double sum = 0.0;
        int within = 0, measured = 0, silent = 0;
        for (const auto& e : seq.events)
        {
            if (e.type != MidiEvent::Type::noteOn)
                continue;
            double end = e.timeSeconds + 0.2;
            for (const auto& f : seq.events)
                if (f.type == MidiEvent::Type::noteOff && f.note == e.note && f.timeSeconds > e.timeSeconds)
                {
                    end = f.timeSeconds;
                    break;
                }
            std::vector<double> hz;
            for (double t = e.timeSeconds + 0.05; t < end - 0.02; t += 0.01)
            {
                const auto r = yin.analyse (x.data(), static_cast<std::int64_t> (x.size()), static_cast<std::int64_t> (t * out.sampleRate));
                if (r.hz > 0.0 && r.aperiodicity < 0.3)
                    hz.push_back (r.hz);
            }
            if (hz.empty())
            {
                ++silent;
                continue;
            }
            std::nth_element (hz.begin(), hz.begin() + static_cast<std::ptrdiff_t> (hz.size() / 2), hz.end());
            const double cents = 1200.0 * std::log2 (hz[hz.size() / 2] / midiToHz (e.note));
            sum += std::abs (cents);
            within += std::abs (cents) <= 10.0 ? 1 : 0;
            ++measured;
        }
        return "pitch: " + std::to_string (measured) + " notes measured, mean |error| " + fmt (measured > 0 ? sum / measured : 0.0, 1)
               + " c, " + std::to_string (within) + " within 10 c, " + std::to_string (silent) + " unpitched/silent";
    }


    struct BenchRow
    {
        std::string engine, preset;
        double rate = 0.0;
        int block = 0, voices = 0;
        double meanUs = 0.0, worstUs = 0.0, p99Us = 0.0, budgetUs = 0.0;
        int overruns = 0, blocks = 0;
        int peakGrains = 0, grainBudget = 0;
        std::uint64_t denied = 0;
        double realtime = 0.0;
    };

    template <typename Engine>
    BenchRow benchEngine (Engine& engine, double rate, int block, int voices, double seconds)
    {
        std::vector<float> l (static_cast<std::size_t> (block)), r (static_cast<std::size_t> (block));
        for (int k = 0; k < voices; ++k)
            engine.noteOn (48 + (k * 7) % 36, 0.8f);
        const int blocks = static_cast<int> (seconds * rate / block);
        std::vector<double> times;
        times.reserve (static_cast<std::size_t> (blocks));
        const auto start = std::chrono::steady_clock::now();
        for (int b = 0; b < blocks; ++b)
        {
            // Keep the notes alive and moving: retrigger one voice every 0.5 s.
            if (b > 0 && b % std::max (1, static_cast<int> (0.5 * rate / block)) == 0)
            {
                const int k = (b / std::max (1, static_cast<int> (0.5 * rate / block))) % std::max (1, voices);
                engine.noteOff (48 + (k * 7) % 36);
                engine.noteOn (48 + (k * 7) % 36, 0.7f);
            }
            const auto t0 = std::chrono::steady_clock::now();
            engine.render (l.data(), r.data(), block);
            times.push_back (std::chrono::duration<double, std::micro> (std::chrono::steady_clock::now() - t0).count());
        }
        const double total = std::chrono::duration<double> (std::chrono::steady_clock::now() - start).count();
        BenchRow row;
        row.rate = rate;
        row.block = block;
        row.voices = voices;
        row.blocks = blocks;
        row.budgetUs = 1.0e6 * block / rate;
        double sum = 0.0;
        for (double t : times)
        {
            sum += t;
            row.worstUs = std::max (row.worstUs, t);
            row.overruns += t > row.budgetUs ? 1 : 0;
        }
        row.meanUs = times.empty() ? 0.0 : sum / static_cast<double> (times.size());
        auto sorted = times;
        std::sort (sorted.begin(), sorted.end());
        row.p99Us = sorted.empty() ? 0.0 : sorted[std::min (sorted.size() - 1, static_cast<std::size_t> (0.99 * static_cast<double> (sorted.size())))];
        row.realtime = total > 0.0 ? seconds / total : 0.0;
        return row;
    }

    int andorBench (const Values& v)
    {
        const auto which = get (v, "--engine", "all");
        const double seconds = std::stod (get (v, "--seconds", "3"));
        const auto preset = get (v, "--preset", "typical");
        std::vector<BenchRow> rows;

        // A source shared by both engines: the generated vocal phrase.
        const auto fx = phrase::makePhraseFixture ("vocal-phrase");
        const auto atlas = phrase::analysePhrase (fx.audio, {}).atlas;

        auto print = [] (const BenchRow& row)
        {
            char line[300];
            std::snprintf (line, sizeof (line), "%-6s %6.0f Hz  block %3d  voices %2d  mean %7.1f us  p99 %7.1f us  worst %8.1f us  budget %7.1f us  load %5.1f%%  overruns %d/%d  grains %d/%d",
                           row.engine.c_str(), row.rate, row.block, row.voices, row.meanUs, row.p99Us, row.worstUs, row.budgetUs,
                           100.0 * row.meanUs / row.budgetUs, row.overruns, row.blocks, row.peakGrains, row.grainBudget);
            std::cout << line << std::endl;
        };
        for (double rate : { 44100.0, 48000.0, 96000.0 })
            for (int block : { 64, 128, 256, 512 })
                for (int voices : { 1, 4, 8, 12, 16 })
                {
                    if (which == "grain" || which == "all")
                    {
                        auto src = std::make_shared<const PlaybackSource> (fx.audio, 64.0, grain::grainPadding());
                        const auto model = grain::buildGrainModel (src, std::make_shared<const atlas::NoteAtlas> (atlas), 64.0, 1);
                        grain::GrainEngine e;
                        e.prepare (rate, block);
                        e.setModel (model.get());
                        grain::GrainParams p;
                        p.quality = grain::Quality::high;
                        if (preset == "stress")
                        {
                            p.sizeSeconds = 0.6;
                            p.density = 120.0;
                            p.pitchScatter = 2.0;
                        }
                        else
                        {
                            p.sizeSeconds = 0.15;
                            p.density = 30.0;
                        }
                        p.oscMix = 0.25;
                        p.space = 0.3;
                        p.echo = 0.2;
                        p.dna.variationAmount = 0.5;
                        p.mod[0] = { grain::ModSource::lfo1, grain::ModDest::position, 0.2 };
                        e.setParams (p);
                        auto row = benchEngine (e, rate, block, voices, seconds);
                        row.engine = "grain";
                        row.preset = preset;
                        row.peakGrains = e.peakGrains();
                        row.grainBudget = e.grainBudget();
                        row.denied = e.deniedGrains();
                        rows.push_back (row);
                        print (row);
                    }
                    if (which == "phrase" || which == "all")
                    {
                        auto src = std::make_shared<const PlaybackSource> (fx.audio, 60.0, phrase::phrasePadding());
                        const auto model = phrase::buildPhraseModel (src, atlas, 1);
                        phrase::PhraseEngine e;
                        e.prepare (rate, block);
                        e.setModel (model.get());
                        phrase::PhraseParams p;
                        p.polyphony = 16;
                        p.fill = phrase::FillMode::extended;
                        p.sustainCharacter = preset == "stress" ? 0.5 : 0.0;
                        p.synthetic = preset == "stress" ? 0.3 : 0.0;
                        p.tone = 0.8;
                        p.space = 0.3;
                        p.echo = 0.2;
                        e.setParams (p);
                        auto row = benchEngine (e, rate, block, voices, seconds);
                        row.engine = "phrase";
                        row.preset = preset;
                        rows.push_back (row);
                        print (row);
                    }
                }

        if (v.count ("--output-json"))
        {
            auto list = json::array();
            for (const auto& row : rows)
            {
                auto o = json::object();
                json::set (o, "engine", json::str (row.engine));
                json::set (o, "preset", json::str (row.preset));
                json::set (o, "sampleRate", row.rate);
                json::set (o, "block", row.block);
                json::set (o, "voices", row.voices);
                json::set (o, "meanUs", json::number (row.meanUs, 1));
                json::set (o, "p99Us", json::number (row.p99Us, 1));
                json::set (o, "worstUs", json::number (row.worstUs, 1));
                json::set (o, "budgetUs", json::number (row.budgetUs, 1));
                json::set (o, "loadPercent", json::number (100.0 * row.meanUs / row.budgetUs, 2));
                json::set (o, "overruns", row.overruns);
                json::set (o, "blocks", row.blocks);
                json::set (o, "peakGrains", row.peakGrains);
                json::set (o, "grainBudget", row.grainBudget);
                json::set (o, "deniedGrains", static_cast<juce::int64> (row.denied));
                list.append (o);
            }
            std::string error;
            if (! json::writeFile (get (v, "--output-json"), list, error))
                return fail (3, error);
        }
        return 0;
    }

    int phraseAb (const Values& v)
    {
        if (! v.count ("--output-dir"))
            return fail (1, "--phrase-ab needs --output-dir <dir>");
        std::string error;
        const fs::path file = get (v, "--phrase-ab");
        const auto lp = loadPhrase (file, v, error);
        if (! lp)
            return fail (2, error);
        const auto seq = sequenceFrom (v, error);
        if (! seq)
            return fail (2, error);
        const fs::path dir = get (v, "--output-dir");
        std::error_code ec;
        fs::create_directories (dir, ec);
        const auto stem = file.stem().string() + "-" + get (v, "--demo", "melody");

        // Baselines A and B treat the whole phrase as one sample at its single analysed root:
        // what a conventional sampler (and OSP) would do with it.
        const auto analysis = Analyzer::analyse (lp->audio.audio);
        const double root = chooseRoot (&analysis, std::nullopt).rootMidi;
        for (auto engine : { research::EngineId::baselineA, research::EngineId::baselineB })
        {
            research::RenderConfig config;
            config.engine = engine;
            config.maxTailSeconds = 3.0;
            const auto out = research::renderSequence (lp->audio.audio, root, *seq, config);
            const auto name = dir / (stem + (engine == research::EngineId::baselineA ? "-A-baseline.wav" : "-B-randomised.wav"));
            if (! io::writeAudioFile (name, out.audio, io::SampleFormat::float32, error))
                return fail (3, error);
            std::cout << "wrote " << name.string() << "\n    " << pitchReport (out.audio, *seq) << "\n";
        }
        const auto out = renderPhrase (*lp, *seq, phraseParamsFrom (v));
        const auto name = dir / (stem + "-C-phrase.wav");
        if (! io::writeAudioFile (name, out, io::SampleFormat::float32, error))
            return fail (3, error);
        std::cout << "wrote " << name.string() << "  (single root for A/B: " << midiNoteName (static_cast<int> (std::lround (root))) << ")\n    " << pitchReport (out, *seq) << "\n";
        return 0;
    }
}

const std::set<std::string>& flagOptions()
{
    static const std::set<std::string> flags { "--auto-tuning", "--verbose", "--phrase-eval", "--stress", "--andor-bench" };
    return flags;
}

bool handles (const Values& v, const Flags& flags)
{
    return flags.count ("--phrase-eval") || v.count ("--phrase-analyze") || v.count ("--phrase-eval-file") || v.count ("--phrase-fixtures")
           || v.count ("--phrase-render") || v.count ("--phrase-ab") || flags.count ("--andor-bench");
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
    if (v.count ("--phrase-render"))
        return phraseRender (v);
    if (v.count ("--phrase-ab"))
        return phraseAb (v);
    if (flags.count ("--andor-bench"))
        return andorBench (v);
    return 1;
}

} // namespace osp::andorcli
