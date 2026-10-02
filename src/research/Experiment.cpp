#include "research/Experiment.h"

#include "core/Fft.h"
#include "core/PitchMath.h"
#include "core/Prng.h"
#include "engine/InstrumentBuilder.h"
#include "io/AudioFileIO.h"
#include "io/JsonUtil.h"
#include "midi/MidiFixtures.h"
#include "model/PlaybackPreparation.h"
#include "model/RootChoice.h"
#include "research/Fixtures.h"
#include "research/RenderConfig.h"
#include "research/RenderMetrics.h"
#include "research/RenderSession.h"
#include "research/SourceAnalysis.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <complex>
#include <map>
#include <numbers>

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

    void trimAndFade (AudioData& audio, double seconds)
    {
        const auto frames = std::min<std::int64_t> (audio.numFrames(), static_cast<std::int64_t> (seconds * audio.sampleRate));
        for (auto& ch : audio.channels)
            ch.resize (static_cast<std::size_t> (frames));
        const auto fade = std::min<std::int64_t> (frames, static_cast<std::int64_t> (0.03 * audio.sampleRate));
        for (auto& ch : audio.channels)
            for (std::int64_t i = 0; i < fade; ++i)
                ch[static_cast<std::size_t> (frames - 1 - i)] *= static_cast<float> (i) / static_cast<float> (fade);
    }

    struct NoteSpec
    {
        double offset = 0.0;
        double start = 0.0;
        double duration = 1.0;
        int velocity = 100;
    };

    struct SequenceSpec
    {
        std::string fixture;           // either a fixture name...
        std::vector<NoteSpec> notes;   // ...or explicit notes relative to the root
        std::vector<std::pair<double, bool>> pedal;
    };

    SequenceSpec parseSequence (const juce::var& v)
    {
        SequenceSpec spec;
        spec.fixture = json::getString (v, "fixture");
        if (const auto* notes = v["notes"].getArray())
            for (const auto& n : *notes)
                spec.notes.push_back ({ json::getDouble (n, "offset", 0.0), json::getDouble (n, "start", 0.0),
                                        json::getDouble (n, "duration", 1.0), json::getInt (n, "velocity", 100) });
        return spec;
    }

    MidiSequence makeSequence (const SequenceSpec& spec, double rootMidi)
    {
        const int reference = static_cast<int> (std::lround (rootMidi));
        if (! spec.fixture.empty())
            if (auto f = fixtures::byName (spec.fixture, fixtureReferenceNote (rootMidi)))
                return *f;
        MidiSequence seq;
        seq.name = "experiment";
        for (const auto& n : spec.notes)
        {
            const int note = std::clamp (reference + static_cast<int> (std::lround (n.offset)), 0, 127);
            seq.events.push_back ({ n.start, MidiEvent::Type::noteOn, note, std::clamp (n.velocity, 1, 127), 1 });
            seq.events.push_back ({ n.start + n.duration, MidiEvent::Type::noteOff, note, 0, 1 });
        }
        seq.sort();
        return seq;
    }

    struct Condition
    {
        std::string id;
        std::string label;
        juce::var settings; // engine + instrument block + sequence override
    };

    struct Variant
    {
        std::string id;
        std::string label;
        std::string note;
        SequenceSpec sequence;
        double tail = 6.0;
    };
}

double repetitionScore (const AudioData& audio, double fromSeconds, double* lagSeconds)
{
    if (audio.isEmpty())
        return 0.0;
    const auto mono = audio.mixToMono();
    const double sr = audio.sampleRate;
    const Fft fft (11);
    const int n = fft.size();
    const int hop = static_cast<int> (0.02 * sr);
    constexpr int bands = 24;
    std::array<int, bands + 1> edges {};
    for (int b = 0; b <= bands; ++b)
        edges[static_cast<std::size_t> (b)] = std::clamp (static_cast<int> (60.0 * std::pow (12000.0 / 60.0, static_cast<double> (b) / bands) * n / sr), 1, n / 2);

    std::vector<double> window (static_cast<std::size_t> (n));
    for (int i = 0; i < n; ++i)
        window[static_cast<std::size_t> (i)] = 0.5 - 0.5 * std::cos (2.0 * std::numbers::pi * i / n);
    std::vector<std::complex<double>> buf (static_cast<std::size_t> (n));
    std::vector<std::array<double, bands>> frames;
    for (auto start = static_cast<std::int64_t> (fromSeconds * sr); start + n < static_cast<std::int64_t> (mono.size()); start += hop)
    {
        for (int i = 0; i < n; ++i)
            buf[static_cast<std::size_t> (i)] = { mono[static_cast<std::size_t> (start + i)] * window[static_cast<std::size_t> (i)], 0.0 };
        fft.forward (buf.data());
        std::array<double, bands> f {};
        for (int b = 0; b < bands; ++b)
        {
            double sum = 0.0;
            for (int k = edges[static_cast<std::size_t> (b)]; k < std::max (edges[static_cast<std::size_t> (b)] + 1, edges[static_cast<std::size_t> (b + 1)]); ++k)
                sum += std::norm (buf[static_cast<std::size_t> (k)]);
            f[static_cast<std::size_t> (b)] = 10.0 * std::log10 (sum + 1.0e-10);
        }
        frames.push_back (f);
    }
    const int t = static_cast<int> (frames.size());
    if (t < 100)
        return 0.0;

    // Remove slow trends (2 s moving average) so only fine structure, which a loop
    // repeats exactly, contributes; then z-normalise each band.
    const int half = static_cast<int> (1.0 / 0.02);
    std::vector<std::array<double, bands>> z (frames.size());
    for (int b = 0; b < bands; ++b)
    {
        double sum = 0.0;
        int count = 0;
        int lo = 0, hi = -1;
        std::vector<double> hp (static_cast<std::size_t> (t));
        for (int i = 0; i < t; ++i)
        {
            while (hi < std::min (t - 1, i + half))
            {
                ++hi;
                sum += frames[static_cast<std::size_t> (hi)][static_cast<std::size_t> (b)];
                ++count;
            }
            while (lo < i - half)
            {
                sum -= frames[static_cast<std::size_t> (lo)][static_cast<std::size_t> (b)];
                --count;
                ++lo;
            }
            hp[static_cast<std::size_t> (i)] = frames[static_cast<std::size_t> (i)][static_cast<std::size_t> (b)] - sum / count;
        }
        double mean = 0.0, var = 0.0;
        for (double v : hp)
            mean += v;
        mean /= t;
        for (double v : hp)
            var += (v - mean) * (v - mean);
        const double sd = std::sqrt (var / t) + 1.0e-9;
        for (int i = 0; i < t; ++i)
            z[static_cast<std::size_t> (i)][static_cast<std::size_t> (b)] = (hp[static_cast<std::size_t> (i)] - mean) / sd;
    }

    double best = 0.0;
    int bestLag = 0;
    const int minLag = static_cast<int> (0.5 / 0.02);
    const int maxLag = std::min (static_cast<int> (20.0 / 0.02), t / 2);
    for (int lag = minLag; lag <= maxLag; ++lag)
    {
        double s = 0.0;
        for (int i = 0; i + lag < t; ++i)
            for (int b = 0; b < bands; ++b)
                s += z[static_cast<std::size_t> (i)][static_cast<std::size_t> (b)] * z[static_cast<std::size_t> (i + lag)][static_cast<std::size_t> (b)];
        s /= static_cast<double> (t - lag) * bands;
        if (s > best)
        {
            best = s;
            bestLag = lag;
        }
    }
    if (lagSeconds != nullptr)
        *lagSeconds = bestLag * 0.02;
    return best;
}

double discontinuityDb (const AudioData& audio, double fromSeconds, double toSeconds, double* atSeconds)
{
    if (audio.isEmpty())
        return 0.0;
    const double sr = audio.sampleRate;
    const int window = std::max (8, static_cast<int> (0.003 * sr));
    const auto a = std::max<std::int64_t> (2, static_cast<std::int64_t> (fromSeconds * sr));
    const auto b = std::min<std::int64_t> (audio.numFrames(), static_cast<std::int64_t> (toSeconds * sr));
    std::vector<double> energies;
    for (auto start = a; start + window <= b; start += window)
    {
        double e = 0.0;
        for (const auto& ch : audio.channels)
            for (auto i = start; i < start + window; ++i)
            {
                const double d = static_cast<double> (ch[static_cast<std::size_t> (i)]) - 2.0 * ch[static_cast<std::size_t> (i - 1)] + ch[static_cast<std::size_t> (i - 2)];
                e += d * d;
            }
        energies.push_back (e);
    }
    if (energies.size() < 10)
        return 0.0;
    auto sorted = energies;
    std::nth_element (sorted.begin(), sorted.begin() + static_cast<std::ptrdiff_t> (sorted.size() / 2), sorted.end());
    const double median = std::max (1.0e-20, sorted[sorted.size() / 2]);
    const auto peak = std::max_element (energies.begin(), energies.end());
    if (atSeconds != nullptr)
        *atSeconds = (static_cast<double> (a) + static_cast<double> (peak - energies.begin()) * window) / sr;
    return 10.0 * std::log10 (*peak / median);
}

ExperimentSummary runExperiment (const std::filesystem::path& planFile, const std::filesystem::path& outputDir,
                                 const std::function<void (const std::string&)>& log, std::string& error)
{
    ExperimentSummary summary;
    const auto plan = json::readFile (planFile, error);
    if (! plan)
        return summary;
    const auto& p = *plan;
    const auto name = json::getString (p, "name", "experiment");
    const std::filesystem::path corpusRoot = json::getString (p, "corpusRoot", "research/corpus");
    const auto seed = static_cast<std::uint64_t> (json::getDouble (p, "seed", 1.0));
    const double outputRate = json::getDouble (p, "outputSampleRate", 44100.0);
    const double listeningRms = json::getDouble (p, "listeningRmsDbfs", -20.0);
    const auto format = json::getString (p, "format", "flac");
    const bool trimToCommon = json::getBool (p, "trimToCommon", true);
    const double maxSeconds = json::getDouble (p, "maxSeconds", 90.0);

    std::vector<Condition> conditions;
    if (const auto* list = p["conditions"].getArray())
        for (const auto& c : *list)
            conditions.push_back ({ json::getString (c, "id"), json::getString (c, "label"), c });
    std::vector<Variant> variants;
    if (const auto* list = p["variants"].getArray())
        for (const auto& v : *list)
            variants.push_back ({ json::getString (v, "id"), json::getString (v, "label"), json::getString (v, "note"),
                                  parseSequence (v["sequence"]), json::getDouble (v, "tail", 6.0) });
    else
        variants.push_back ({ "main", "", "", parseSequence (p["sequence"]), json::getDouble (p, "tail", 6.0) });

    if (conditions.size() < 2 || ! p["sources"].isArray())
    {
        error = "experiment plan needs sources and at least two conditions";
        return summary;
    }

    std::error_code ec;
    const auto clipsDir = outputDir / "clips";
    std::filesystem::create_directories (clipsDir, ec);
    summary.keyFile = outputDir / "key.json";
    Prng rng (Prng::deriveSeed (seed, 0x657870, 0));

    auto keyClips = json::array();
    auto groups = json::array();
    auto sections = json::array();

    for (const auto& s : *p["sources"].getArray())
    {
        const auto rel = json::getString (s, "path");
        const auto sectionId = json::getString (s, "id", rel);
        const auto path = std::filesystem::path (rel).is_absolute() ? std::filesystem::path (rel) : corpusRoot / rel;
        auto source = loadAndAnalyse (path);
        if (! source.ok)
        {
            summary.errors.push_back (rel + ": " + source.error);
            if (log)
                log ("skipped " + rel + ": " + source.error);
            continue;
        }
        const auto root = chooseRoot (&source.analysis, std::nullopt);
        PlaybackOptions prepared;
        prepared.startAtOnset = true;
        prepared.normaliseLevel = true;
        const auto preparation = preparePlayback (source.analysis, prepared);

        auto section = json::object();
        json::set (section, "id", json::str (sectionId));
        json::set (section, "title", json::str (json::getString (s, "label", sectionId)));
        json::set (section, "note", json::str ("Source: " + path.filename().string() + " (" + source.analysis.pitch.noteName + ")"));
        sections.append (section);

        // Models are shared by conditions that need the same build (with or without anchors).
        std::map<bool, std::shared_ptr<InstrumentModel>> models;

        for (const auto& variant : variants)
        {
            struct Rendered
            {
                const Condition* condition;
                AudioData audio;
                double ms;
                double sounding;
            };
            std::vector<Rendered> rendered;
            for (const auto& c : conditions)
            {
                RenderConfig config;
                config.sampleRate = outputRate;
                config.playback = prepared;
                config.sampler.seed = seed;
                config.engineSettings.seed = seed;
                config.maxTailSeconds = variant.tail;
                if (const auto engine = parseEngine (json::getString (c.settings, "engine", "C")))
                    config.engine = *engine;
                applyInstrumentBlock (c.settings["instrument"], config);
                auto spec = variant.sequence;
                if (c.settings["sequence"].isObject())
                    spec = parseSequence (c.settings["sequence"]);
                const auto sequence = makeSequence (spec, root.rootMidi);

                const auto t0 = std::chrono::steady_clock::now();
                RenderOutput out;
                if (config.engine == EngineId::instrument)
                {
                    auto& model = models[config.anchors];
                    if (! model)
                    {
                        InstrumentBuildOptions options;
                        options.rootOverrideMidi = root.rootMidi;
                        options.seed = seed;
                        model = instrument::buildComplete (source.audio, source.analysis, options, config.anchors);
                    }
                    out = renderInstrument (*model, sequence, config);
                }
                else
                {
                    out = renderSequence (source.audio, root.rootMidi, sequence, config, preparation);
                }
                const double ms = std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - t0).count();
                const double sounding = soundingSeconds (out.audio);
                rendered.push_back ({ &c, std::move (out.audio), ms, sounding });
            }

            double length = maxSeconds;
            for (const auto& r : rendered)
                length = trimToCommon ? std::min (length, std::max (0.2, r.sounding + 0.05)) : length;

            const std::string groupId = sectionId + (variant.id == "main" ? "" : " " + variant.id);
            std::vector<std::string> ids;
            for (auto& r : rendered)
            {
                trimAndFade (r.audio, trimToCommon ? length : std::min (maxSeconds, r.sounding + 0.05));
                const double rms = rmsOf (r.audio);
                double gain = rms > 0.0 ? dbToGain (listeningRms) / rms : 1.0;
                const double peak = peakOf (r.audio) * gain;
                if (peak > dbToGain (-1.0))
                    gain *= dbToGain (-1.0) / peak;
                for (auto& ch : r.audio.channels)
                    for (auto& v : ch)
                        v = static_cast<float> (v * gain);

                const auto id = hexId (rng);
                std::string writeError;
                io::writeAudioFile (clipsDir / (id + "." + format), r.audio, io::SampleFormat::pcm16, writeError);
                if (! writeError.empty())
                    summary.errors.push_back (writeError);

                double lag = 0.0;
                const double repetition = r.audio.durationSeconds() > 12.0 ? repetitionScore (r.audio, 4.0, &lag) : 0.0;
                auto item = json::object();
                json::set (item, "id", json::str (id));
                json::set (item, "group", json::str (groupId));
                json::set (item, "section", json::str (sectionId));
                json::set (item, "variant", json::str (variant.id));
                json::set (item, "source", json::str (rel));
                json::set (item, "condition", json::str (r.condition->id));
                json::set (item, "conditionLabel", json::str (r.condition->label));
                json::set (item, "seconds", json::number (r.audio.durationSeconds(), 3));
                json::set (item, "naturalSeconds", json::number (r.sounding, 3));
                json::set (item, "renderMs", json::number (r.ms, 1));
                json::set (item, "levelGainDb", json::number (gainToDb (gain), 2));
                json::set (item, "repetitionScore", json::number (repetition, 3));
                json::set (item, "repetitionLagSeconds", json::number (lag, 2));
                double clickAt = 0.0;
                const double spike = discontinuityDb (r.audio, 1.5, std::max (2.0, r.audio.durationSeconds() - 4.0), &clickAt);
                json::set (item, "seamSpikeDb", json::number (spike, 1));
                json::set (item, "seamSpikeAtSeconds", json::number (clickAt, 2));
                keyClips.append (item);
                ids.push_back (id);
                ++summary.clips;
            }

            // Listener order is shuffled per group (Fisher-Yates with the experiment seed).
            for (std::size_t i = ids.size(); i > 1; --i)
                std::swap (ids[i - 1], ids[static_cast<std::size_t> (rng.nextBelow (i))]);
            auto group = json::object();
            json::set (group, "id", json::str (groupId));
            json::set (group, "section", json::str (sectionId));
            json::set (group, "label", json::str (variant.label.empty() ? json::getString (p, "groupLabel", json::getString (s, "label", sectionId)) : variant.label));
            json::set (group, "note", json::str (variant.note));
            auto clipIds = json::array();
            for (const auto& id : ids)
                clipIds.append (json::str (id));
            json::set (group, "clips", clipIds);
            groups.append (group);
            ++summary.groups;
            if (log)
                log (groupId + ": " + std::to_string (rendered.size()) + " clips, " + juce::String (length, 1).toStdString() + " s");
        }
    }

    auto key = json::object();
    json::set (key, "schemaVersion", 1);
    json::set (key, "name", json::str (name));
    json::set (key, "seed", static_cast<juce::int64> (seed));
    json::set (key, "clips", keyClips);
    json::writeFile (summary.keyFile, key, error);

    auto listening = json::object();
    json::set (listening, "schemaVersion", 1);
    json::set (listening, "name", json::str (name));
    for (const char* field : { "tab", "title", "heading", "bestLabel", "bestHint" })
        if (json::has (p, field))
            json::set (listening, field, p[field]);
    json::set (listening, "intro", p["intro"]);
    json::set (listening, "scales", p["scales"]);
    json::set (listening, "best", json::getBool (p, "best", true));
    json::set (listening, "ext", json::str (format));
    json::set (listening, "sections", sections);
    json::set (listening, "groups", groups);
    json::writeFile (outputDir / "listening.json", listening, error);
    return summary;
}

} // namespace osp::research
