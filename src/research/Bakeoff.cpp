#include "research/Bakeoff.h"

#include "analysis/Analyzer.h"
#include "analysis/spectrum/SpectralEnvelope.h"
#include "audio/pitch/OfflinePitchShifter.h"
#include "core/PitchMath.h"
#include "core/Prng.h"
#include "io/AudioFileIO.h"
#include "io/JsonUtil.h"
#include "model/PlaybackPreparation.h"
#include "research/RenderSession.h"
#include "research/SourceAnalysis.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <map>
#include <sstream>

namespace osp::research
{

namespace
{
    std::string hexId (Prng& rng)
    {
        static constexpr char digits[] = "0123456789abcdef";
        auto v = rng.nextU64();
        std::string id;
        for (int i = 0; i < 8; ++i, v >>= 4)
            id += digits[v & 0xf];
        return id;
    }

    std::string offsetLabel (double offset)
    {
        const auto semis = static_cast<int> (std::lround (offset));
        return (semis > 0 ? "+" : "") + std::to_string (semis);
    }

    AudioData slice (const AudioData& audio, double startSeconds, double seconds)
    {
        AudioData out;
        out.sampleRate = audio.sampleRate;
        const auto a = std::min<std::int64_t> (audio.numFrames(), static_cast<std::int64_t> (startSeconds * audio.sampleRate));
        const auto b = std::min<std::int64_t> (audio.numFrames(), a + static_cast<std::int64_t> (seconds * audio.sampleRate));
        for (const auto& ch : audio.channels)
            out.channels.emplace_back (ch.begin() + a, ch.begin() + b);
        return out;
    }

    double rmsOf (const AudioData& audio)
    {
        double sum = 0.0;
        std::size_t n = 0;
        for (const auto& ch : audio.channels)
            for (float s : ch)
            {
                sum += static_cast<double> (s) * s;
                ++n;
            }
        return n > 0 ? std::sqrt (sum / static_cast<double> (n)) : 0.0;
    }

    double peakOf (const AudioData& audio)
    {
        double peak = 0.0;
        for (const auto& ch : audio.channels)
            for (float s : ch)
                peak = std::max (peak, static_cast<double> (std::abs (s)));
        return peak;
    }

    /** Seconds until the render last exceeds -60 dB re its peak (how long it actually sounds). */
    double soundingSeconds (const AudioData& audio)
    {
        const double gate = peakOf (audio) * 1.0e-3;
        std::int64_t last = 0;
        for (const auto& ch : audio.channels)
            for (std::int64_t i = static_cast<std::int64_t> (ch.size()) - 1; i > last; --i)
                if (std::abs (ch[static_cast<std::size_t> (i)]) > gate)
                {
                    last = i;
                    break;
                }
        return static_cast<double> (last) / audio.sampleRate;
    }

    struct Correlation
    {
        double correlation = 1.0;
        double width = 0.0;
    };

    Correlation stereoOf (const AudioData& audio)
    {
        if (audio.numChannels() < 2)
            return {};
        double ll = 0, rr = 0, lr = 0, mm = 0, ss = 0;
        const auto& l = audio.channels[0];
        const auto& r = audio.channels[1];
        for (std::size_t i = 0; i < l.size(); ++i)
        {
            ll += l[i] * l[i];
            rr += r[i] * r[i];
            lr += l[i] * r[i];
            mm += 0.25 * (l[i] + r[i]) * (l[i] + r[i]);
            ss += 0.25 * (l[i] - r[i]) * (l[i] - r[i]);
        }
        Correlation c;
        c.correlation = (ll > 0 && rr > 0) ? lr / std::sqrt (ll * rr) : 1.0;
        c.width = (mm + ss) > 0 ? ss / (mm + ss) : 0.0;
        return c;
    }

    struct Clip
    {
        std::string engine;
        AudioData audio;
        double renderMs = 0.0;
        double soundingSeconds = 0.0;
    };
}

std::optional<BakeoffPlan> loadBakeoffPlan (const std::filesystem::path& path, std::string& error)
{
    const auto parsed = json::readFile (path, error);
    if (! parsed)
        return std::nullopt;
    const auto& root = *parsed;
    if (json::getInt (root, "schemaVersion", 1) > BakeoffPlan::schemaVersion)
    {
        error = "bake-off plan schemaVersion is newer than supported";
        return std::nullopt;
    }

    BakeoffPlan plan;
    plan.name = json::getString (root, "name", plan.name);
    if (json::has (root, "corpusRoot"))
        plan.corpusRoot = json::getString (root, "corpusRoot");
    plan.clipSeconds = json::getDouble (root, "clipSeconds", plan.clipSeconds);
    plan.outputSampleRate = json::getDouble (root, "outputSampleRate", plan.outputSampleRate);
    plan.listeningRmsDbfs = json::getDouble (root, "listeningRmsDbfs", plan.listeningRmsDbfs);
    plan.seed = static_cast<std::uint64_t> (json::getDouble (root, "seed", static_cast<double> (plan.seed)));

    if (const auto* offsets = root["offsets"].getArray())
    {
        plan.offsets.clear();
        for (const auto& o : *offsets)
            plan.offsets.push_back (static_cast<double> (o));
    }
    if (const auto* engines = root["engines"].getArray())
    {
        plan.engines.clear();
        for (const auto& e : *engines)
        {
            const auto name = e.toString().toUpperCase().toStdString();
            if (name != "A" && name != "B" && name != "C")
            {
                error = "unknown bake-off engine '" + name + "' (A, B or C)";
                return std::nullopt;
            }
            plan.engines.push_back (name);
        }
    }
    if (const auto* sources = root["sources"].getArray())
        for (const auto& s : *sources)
            plan.sources.push_back ({ json::getString (s, "family", "source"), json::getString (s, "path") });

    if (plan.sources.empty() || plan.offsets.empty() || plan.engines.empty())
    {
        error = "bake-off plan needs sources, offsets and engines";
        return std::nullopt;
    }
    return plan;
}

BakeoffSummary runBakeoff (const BakeoffPlan& plan, const std::filesystem::path& outputDir,
                           const std::function<void (const std::string&)>& log)
{
    BakeoffSummary summary;
    summary.clipsDir = outputDir / "clips";
    summary.keyFile = outputDir / "key.json";
    std::error_code ec;
    std::filesystem::create_directories (summary.clipsDir, ec);

    Prng rng (Prng::deriveSeed (plan.seed, 0x6261, 0)); // names and orders depend only on the seed

    auto keyClips = json::array();
    auto groups = json::array();

    PlaybackOptions playbackOptions;
    playbackOptions.startAtOnset = true;
    playbackOptions.normaliseLevel = true;

    for (const auto& src : plan.sources)
    {
        const auto path = std::filesystem::path (src.path).is_absolute() ? std::filesystem::path (src.path)
                                                                          : plan.corpusRoot / src.path;
        auto source = loadAndAnalyse (path);
        if (! source.ok)
        {
            summary.errors.push_back (src.path + ": " + source.error);
            if (log)
                log ("skipped " + src.path + ": " + source.error);
            continue;
        }

        const auto root = chooseRoot (&source.analysis, std::nullopt);
        const auto preparation = preparePlayback (source.analysis, playbackOptions);
        const double f0 = root.sourceF0Hz > 0.0 ? root.sourceF0Hz : midiToHz (root.rootMidi);
        const int reference = static_cast<int> (std::lround (root.rootMidi));
        const auto sourceExcerpt = slice (source.audio, preparation.startSeconds, plan.clipSeconds);
        const auto sourceEnvelope = computeSpectralEnvelope (sourceExcerpt.mixToMono(), source.audio.sampleRate, f0 * 1.1);
        const auto sourceStereo = stereoOf (sourceExcerpt);

        for (const double offset : plan.offsets)
        {
            const int note = std::clamp (reference + static_cast<int> (std::lround (offset)), 0, 127);
            std::vector<Clip> clips;

            for (const auto& engine : plan.engines)
            {
                RenderConfig config;
                config.sampleRate = plan.outputSampleRate;
                config.playback = playbackOptions;
                config.maxTailSeconds = 1.0;

                MidiSequence sequence;
                sequence.name = "bakeoff";
                const int playNote = engine == "A" ? note : reference;
                sequence.events.push_back ({ 0.0, MidiEvent::Type::noteOn, playNote, 100, 1 });
                sequence.events.push_back ({ plan.clipSeconds, MidiEvent::Type::noteOff, playNote, 0, 1 });

                const auto t0 = std::chrono::steady_clock::now();
                RenderOutput output;
                if (engine == "A")
                    output = renderSequence (source.audio, root.rootMidi, sequence, config, preparation);
                else
                {
                    StretchShiftOptions shift;
                    shift.semitones = note - reference;
                    shift.preserveFormants = engine == "C";
                    shift.formantBaseHz = f0;
                    shift.seed = plan.seed;
                    const auto shifted = stretchShiftOffline (source.audio, shift);
                    output = renderSequence (shifted, root.rootMidi, sequence, config, preparation);
                }
                const double ms = std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - t0).count();
                clips.push_back ({ engine, std::move (output.audio), ms, 0.0 });
                clips.back().soundingSeconds = soundingSeconds (clips.back().audio);
            }

            // Common length so duration does not reveal the engine; then fade and level-match.
            double length = plan.clipSeconds + 0.25;
            for (const auto& c : clips)
                length = std::min (length, std::max (0.1, c.soundingSeconds));

            auto groupClips = json::array();
            std::vector<std::string> ids;
            const std::string groupId = src.family + " " + offsetLabel (offset);

            for (auto& c : clips)
            {
                c.audio = slice (c.audio, 0.0, length);
                const auto frames = c.audio.numFrames();
                const auto fade = std::min<std::int64_t> (frames, static_cast<std::int64_t> (0.03 * c.audio.sampleRate));
                for (auto& ch : c.audio.channels)
                    for (std::int64_t i = 0; i < fade; ++i)
                        ch[static_cast<std::size_t> (frames - 1 - i)] *= static_cast<float> (i) / static_cast<float> (fade);

                const double rms = rmsOf (c.audio);
                double gain = rms > 0.0 ? dbToGain (plan.listeningRmsDbfs) / rms : 1.0;
                const double peak = peakOf (c.audio) * gain;
                if (peak > dbToGain (-1.0))
                    gain *= dbToGain (-1.0) / peak;
                for (auto& ch : c.audio.channels)
                    for (auto& s : ch)
                        s = static_cast<float> (s * gain);

                const auto id = hexId (rng);
                std::string error;
                io::writeAudioFile (summary.clipsDir / (id + ".wav"), c.audio, io::SampleFormat::pcm16, error);
                if (! error.empty())
                    summary.errors.push_back (error);

                // Guard-rail metrics (kept in the key, never shown to listeners).
                const auto analysis = Analyzer::analyse (c.audio);
                const double expectedHz = midiToHz (note);
                const auto envelope = computeSpectralEnvelope (c.audio.mixToMono(), c.audio.sampleRate, expectedHz * 1.1);
                const auto stereo = stereoOf (c.audio);

                auto item = json::object();
                json::set (item, "id", json::str (id));
                json::set (item, "group", json::str (groupId));
                json::set (item, "family", json::str (src.family));
                json::set (item, "source", json::str (src.path));
                json::set (item, "sourceRoot", json::str (source.analysis.pitch.noteName));
                json::set (item, "offsetSemitones", json::number (offset, 1));
                json::set (item, "engine", json::str (c.engine));
                json::set (item, "clipSeconds", json::number (length, 3));
                json::set (item, "naturalSeconds", json::number (c.soundingSeconds, 3));
                json::set (item, "renderMs", json::number (c.renderMs, 1));
                json::set (item, "expectedHz", json::number (expectedHz, 2));
                json::set (item, "detectedHz", json::number (analysis.pitch.fundamentalHz, 2));
                json::set (item, "pitchErrorCents", analysis.pitch.fundamentalHz > 0.0
                                                        ? json::number (centsBetween (expectedHz, analysis.pitch.fundamentalHz), 1)
                                                        : juce::var());
                json::set (item, "pitchConfidence", json::number (analysis.pitch.confidence, 3));
                json::set (item, "envelopeShiftSemitones", json::number (envelopeShiftSemitones (sourceEnvelope, envelope), 2));
                json::set (item, "centroidHz", json::number (analysis.spectral.meanCentroidHz, 0));
                json::set (item, "stereoCorrelation", json::number (stereo.correlation, 3));
                json::set (item, "sourceStereoCorrelation", json::number (sourceStereo.correlation, 3));
                json::set (item, "stereoWidth", json::number (stereo.width, 3));
                json::set (item, "sourceStereoWidth", json::number (sourceStereo.width, 3));
                keyClips.append (item);
                ids.push_back (id);
                ++summary.clips;
            }

            // Listening order within the group is shuffled (deterministically).
            for (std::size_t i = ids.size(); i > 1; --i)
                std::swap (ids[i - 1], ids[static_cast<std::size_t> (rng.nextBelow (i))]);
            for (const auto& id : ids)
                groupClips.append (json::str (id));

            auto group = json::object();
            json::set (group, "id", json::str (groupId));
            json::set (group, "family", json::str (src.family));
            json::set (group, "offsetSemitones", json::number (offset, 1));
            json::set (group, "sourceRoot", json::str (source.analysis.pitch.noteName));
            json::set (group, "clips", groupClips);
            groups.append (group);
            ++summary.groups;

            if (log)
                log (groupId + ": " + std::to_string (clips.size()) + " clips, " + juce::String (length, 2).toStdString() + " s");
        }
    }

    auto key = json::object();
    json::set (key, "schemaVersion", BakeoffPlan::schemaVersion);
    json::set (key, "name", json::str (plan.name));
    json::set (key, "seed", static_cast<juce::int64> (plan.seed));
    json::set (key, "engines", json::stringArray (plan.engines));
    auto engineInfo = json::object();
    json::set (engineInfo, "A", json::str ("bandlimited resampling (baseline sampler)"));
    json::set (engineInfo, "B", json::str ("Signalsmith Stretch 1.4.0, plain transposition"));
    json::set (engineInfo, "C", json::str ("Signalsmith Stretch 1.4.0, formant compensation with analysed F0"));
    json::set (key, "engineInfo", engineInfo);
    json::set (key, "clips", keyClips);
    std::string error;
    json::writeFile (summary.keyFile, key, error);

    // Listener-facing data: groups and clip ids only, no engine names.
    auto listening = json::object();
    json::set (listening, "schemaVersion", 1);
    json::set (listening, "name", json::str (plan.name));
    json::set (listening, "groups", groups);
    json::writeFile (outputDir / "listening.json", listening, error);

    return summary;
}

bool scoreBakeoff (const std::filesystem::path& outputDir, const std::filesystem::path& ratingsFile, std::string& report,
                   std::string& error)
{
    const auto key = json::readFile (outputDir / "key.json", error);
    if (! key)
        return false;
    const auto ratings = json::readFile (ratingsFile, error);
    if (! ratings)
        return false;

    std::map<std::string, juce::var> clipById;
    if (const auto* clips = (*key)["clips"].getArray())
        for (const auto& c : *clips)
            clipById[json::getString (c, "id")] = c;

    struct Tally
    {
        int n = 0, best = 0;
        double identity = 0, beauty = 0, artifacts = 0;
    };
    // key: dimension ("all", family, band) -> engine -> tally
    std::map<std::string, std::map<std::string, Tally>> table;

    int used = 0;
    if (const auto* items = (*ratings)["ratings"].getArray())
        for (const auto& r : *items)
        {
            const auto it = clipById.find (json::getString (r, "clip"));
            if (it == clipById.end())
                continue;
            const auto& clip = it->second;
            const auto engine = json::getString (clip, "engine");
            const double offset = json::getDouble (clip, "offsetSemitones", 0.0);
            const std::string band = std::abs (offset) < 0.5 ? "0 (root)" : (std::abs (offset) <= 12.5 ? "±12" : "±24");
            for (const auto& dim : { std::string ("all"), "family: " + json::getString (clip, "family"), "offset " + band })
            {
                auto& t = table[dim][engine];
                ++t.n;
                t.identity += json::getDouble (r, "identity", 0.0);
                t.beauty += json::getDouble (r, "beauty", 0.0);
                t.artifacts += json::getDouble (r, "artifacts", 0.0);
                t.best += json::getBool (r, "best", false) ? 1 : 0;
            }
            ++used;
        }

    if (used == 0)
    {
        error = "no ratings matched clips in the key";
        return false;
    }

    std::ostringstream md;
    md << "# Bake-off score: " << json::getString (*key, "name") << "\n\n"
       << used << " ratings. Scores are means on 1–5 (artifacts: 5 = none); wins = times chosen best in its group.\n\n";
    auto score = json::object();
    for (const auto& [dim, engines] : table)
    {
        md << "## " << dim << "\n\n| Engine | n | Identity | Beauty | Artifacts | Wins |\n|---|---|---|---|---|---|\n";
        auto dimJson = json::object();
        for (const auto& [engine, t] : engines)
        {
            const auto mean = [&] (double v) { return t.n > 0 ? json::round (v / t.n, 2) : 0.0; };
            md << "| " << engine << " | " << t.n << " | " << mean (t.identity) << " | " << mean (t.beauty) << " | "
               << mean (t.artifacts) << " | " << t.best << " |\n";
            auto e = json::object();
            json::set (e, "n", t.n);
            json::set (e, "identity", mean (t.identity));
            json::set (e, "beauty", mean (t.beauty));
            json::set (e, "artifacts", mean (t.artifacts));
            json::set (e, "wins", t.best);
            json::set (dimJson, engine.c_str(), e);
        }
        md << "\n";
        json::set (score, dim.c_str(), dimJson);
    }

    report = md.str();
    std::ofstream (outputDir / "score.md") << report;
    return json::writeFile (outputDir / "score.json", score, error);
}

} // namespace osp::research
