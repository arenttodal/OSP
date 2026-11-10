#include "research/PedalLoopLab.h"

#include "analysis/Analyzer.h"
#include "audio/utility/TestSignals.h"
#include "core/Prng.h"
#include "engine/InstrumentBuilder.h"
#include "engine/InstrumentEngine.h"
#include "io/AudioFileIO.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <functional>
#include <tuple>
#include <iomanip>
#include <memory>
#include <numbers>
#include <sstream>

namespace osp::research
{

namespace
{
    constexpr double outRate = 48000.0;
    constexpr int block = 16;   // events are placed within a third of a millisecond

    AudioData mono (std::vector<float> samples, double sr)
    {
        AudioData a;
        a.sampleRate = sr;
        a.channels.push_back (std::move (samples));
        return a;
    }

    double noise (Prng& rng) { return rng.uniform (-1.0, 1.0); }

    /** Harmonic tone with vibrato, bow noise and an attack/release; `body` adds slowly decaying
        sympathetic partials (the nyckelharpa's resonance strings). */
    AudioData bowed (double f0, double seconds, double sr, double vibratoCents, double attack, std::uint64_t seed, bool sympathetic)
    {
        Prng rng (seed);
        const auto n = static_cast<std::size_t> (seconds * sr);
        std::vector<float> x (n);
        double phase = 0.0, lp = 0.0;
        for (std::size_t i = 0; i < n; ++i)
        {
            const double t = static_cast<double> (i) / sr;
            const double vib = vibratoCents / 1200.0 * std::sin (2.0 * std::numbers::pi * 5.3 * t) * std::min (1.0, t / 0.6);
            phase += f0 * std::exp2 (vib) / sr;
            double s = 0.0;
            for (int k = 1; k <= 14; ++k)
                s += std::sin (2.0 * std::numbers::pi * k * phase) / (k * (1.0 + 0.08 * k));
            lp += 0.12 * (noise (rng) - lp);
            s = 0.45 * s + 0.05 * lp;
            if (sympathetic)
                for (const double m : { 1.5, 2.0, 3.0 })
                    s += 0.06 * std::sin (2.0 * std::numbers::pi * f0 * m * t) * std::exp (-t * 0.4) * std::min (1.0, t / 0.2);
            const double env = std::min (1.0, t / attack) * std::min (1.0, (seconds - t) / 0.25);
            x[i] = static_cast<float> (0.5 * s * env);
        }
        return mono (std::move (x), sr);
    }

    /** A struck string: stretched (inharmonic) partials, each decaying faster with height, and
        a short hammer noise. */
    AudioData piano (double f0, double seconds, double sr)
    {
        Prng rng (11);
        const auto n = static_cast<std::size_t> (seconds * sr);
        std::vector<float> x (n);
        const double b = 0.0004;
        for (std::size_t i = 0; i < n; ++i)
        {
            const double t = static_cast<double> (i) / sr;
            double s = 0.0;
            for (int k = 1; k <= 16; ++k)
            {
                const double fk = f0 * k * std::sqrt (1.0 + b * k * k);
                s += std::exp (-t * (0.7 + 0.35 * k)) * std::sin (2.0 * std::numbers::pi * fk * t) / std::pow (k, 1.1);
            }
            s += t < 0.006 ? 0.3 * noise (rng) * (1.0 - t / 0.006) : 0.0;
            x[i] = static_cast<float> (0.45 * s * std::min (1.0, t / 0.002));
        }
        return mono (std::move (x), sr);
    }

    AudioData organ (double f0, double seconds, double sr)
    {
        const auto n = static_cast<std::size_t> (seconds * sr);
        std::vector<float> x (n);
        for (std::size_t i = 0; i < n; ++i)
        {
            const double t = static_cast<double> (i) / sr;
            double s = 0.0;
            for (const auto& [m, a] : std::array<std::pair<double, double>, 5> { { { 0.5, 0.5 }, { 1.0, 0.8 }, { 2.0, 0.5 }, { 3.0, 0.3 }, { 4.0, 0.25 } } })
                s += a * std::sin (2.0 * std::numbers::pi * f0 * m * t);
            x[i] = static_cast<float> (0.18 * s * std::min (1.0, t / 0.01) * std::min (1.0, (seconds - t) / 0.08));
        }
        return mono (std::move (x), sr);
    }

    AudioData hit (double sr)
    {
        Prng rng (5);
        const auto n = static_cast<std::size_t> (0.8 * sr);
        std::vector<float> x (n);
        double lp = 0.0;
        for (std::size_t i = 0; i < n; ++i)
        {
            const double t = static_cast<double> (i) / sr;
            lp += 0.35 * (noise (rng) - lp);
            x[i] = static_cast<float> (0.8 * lp * std::exp (-t * 18.0) + 0.3 * std::sin (2.0 * std::numbers::pi * 110.0 * t) * std::exp (-t * 9.0));
        }
        return mono (std::move (x), sr);
    }

    AudioData phrase (double sr)
    {
        // Three sung notes run together (a short phrase): G3, A3, C4, gliding between them.
        AudioData out;
        out.sampleRate = sr;
        for (const double f : { 196.0, 220.0, 261.6 })
        {
            auto v = testsignals::vowel (f, 0.9, sr, static_cast<std::uint64_t> (f), 0.45, 1);
            testsignals::applyFades (v, 0.04, 0.04);
            if (out.channels.empty())
                out.channels.resize (1);
            out.channels[0].insert (out.channels[0].end(), v.channels[0].begin(), v.channels[0].end());
        }
        return out;
    }

    // -------------------------------------------------------------------------------------
    // One render

    struct Event
    {
        enum class Kind { enter, turn, jump, exit } kind;
        std::size_t sample;
    };

    struct Performance
    {
        std::string name;
        std::vector<std::pair<double, std::function<void (InstrumentEngine&)>>> at;   ///< seconds -> action
        double seconds = 7.0;
    };

    struct Render
    {
        AudioData audio;
        std::vector<Event> events;
        int voicesLeft = 0;
    };

    Render render (const std::array<const InstrumentModel*, 3>& models, int layers, PedalLoopMode mode, double turn, float correlation, bool loop,
                   const Performance& p)
    {
        EngineSettings s;
        s.macros.space = 0.0;
        s.macros.motion = 0.0;
        s.macros.reimagined = 0.0;
        s.adsr.releaseSeconds = 0.3;
        s.pedalTurnSeconds = turn;
        s.pedalTurnCorrelation = correlation;
        for (auto& l : s.layer)
        {
            l.pedalLoop = mode;
            l.loop = loop;
        }
        auto engine = std::make_unique<InstrumentEngine>();
        engine->prepare (outRate, block, s);
        for (int l = 0; l < layers; ++l)
            engine->setModel (models[static_cast<std::size_t> (l)], l);
        engine->setMixSlots (layers);

        Render out;
        out.audio.sampleRate = outRate;
        out.audio.channels.assign (2, {});
        const auto total = static_cast<std::size_t> (p.seconds * outRate);
        std::array<float, block> l {}, r {};
        std::array<bool, InstrumentEngine::voiceSlots()> wasLooping {};
        std::array<double, InstrumentEngine::voiceSlots()> wasDir {};
        std::array<int, InstrumentEngine::voiceSlots()> wasJumps {};
        std::size_t next = 0;
        for (std::size_t done = 0; done < total; done += block)
        {
            while (next < p.at.size() && p.at[next].first * outRate <= static_cast<double> (done))
                p.at[next++].second (*engine);
            l.fill (0.0f);
            r.fill (0.0f);
            float* ch[2] = { l.data(), r.data() };
            const int n = static_cast<int> (std::min<std::size_t> (block, total - done));
            engine->render (ch, 2, n);
            out.audio.channels[0].insert (out.audio.channels[0].end(), l.begin(), l.begin() + n);
            out.audio.channels[1].insert (out.audio.channels[1].end(), r.begin(), r.begin() + n);
            for (int v = 0; v < InstrumentEngine::voiceSlots(); ++v)
            {
                const auto& voice = engine->voiceAt (v);
                const auto k = static_cast<std::size_t> (v);
                const bool looping = voice.isActive() && voice.pedalLooping();
                const auto at = done + static_cast<std::size_t> (n);
                if (looping && ! wasLooping[k])
                    out.events.push_back ({ Event::Kind::enter, at });
                else if (! looping && wasLooping[k] && voice.isActive())
                    out.events.push_back ({ Event::Kind::exit, at });
                else if (looping && voice.readDirection() != wasDir[k])
                    out.events.push_back ({ Event::Kind::turn, at });
                else if (looping && voice.jumpCount() != wasJumps[k])
                    out.events.push_back ({ Event::Kind::jump, at });
                wasLooping[k] = looping;
                wasDir[k] = voice.readDirection();
                wasJumps[k] = voice.jumpCount();
            }
        }
        out.voicesLeft = engine->activeVoiceCount();
        return out;
    }

    // -------------------------------------------------------------------------------------
    // Measurements

    std::vector<float> monoOf (const AudioData& a)
    {
        std::vector<float> m (a.channels[0].size());
        for (std::size_t i = 0; i < m.size(); ++i)
            m[i] = 0.5f * (a.channels[0][i] + a.channels[1][i]);
        return m;
    }

    double rmsDb (const std::vector<float>& x, double from, double to)
    {
        const auto a = static_cast<std::size_t> (std::max (0.0, from)), b = std::min (x.size(), static_cast<std::size_t> (std::max (0.0, to)));
        double sum = 0.0;
        for (auto i = a; i < b; ++i)
            sum += static_cast<double> (x[i]) * x[i];
        return 10.0 * std::log10 (std::max (1.0e-20, sum / static_cast<double> (std::max<std::size_t> (1, b > a ? b - a : 1))));
    }

    /** High-passed energy (second difference) in [a, b) against the same around it (20-60 ms
        away on both sides), in dB: a click stands out; a smooth turn reads near 0. */
    double clickDb (const std::vector<float>& x, std::size_t a, std::size_t b)
    {
        auto d2 = [&] (std::size_t i) { return i < 2 || i >= x.size() ? 0.0 : static_cast<double> (x[i]) - 2.0 * x[i - 1] + x[i - 2]; };
        auto energy = [&] (std::size_t from, std::size_t to) {
            double sum = 0.0;
            std::size_t count = 0;
            for (auto i = from; i < to && i < x.size(); ++i, ++count)
                sum += d2 (i) * d2 (i);
            return count > 0 ? sum / static_cast<double> (count) : 0.0;
        };
        const auto ms = static_cast<std::size_t> (outRate / 1000.0);
        const double local = energy (a, b);
        const double around = 0.5 * (energy (a > 60 * ms ? a - 60 * ms : 0, a > 20 * ms ? a - 20 * ms : 0) + energy (b + 20 * ms, b + 60 * ms));
        return 10.0 * std::log10 (std::max (1.0e-24, local) / std::max (1.0e-24, around));
    }

    struct Measured
    {
        bool finite = true;
        double peakDb = -120.0, dc = 0.0, lastSecondDb = -120.0;
        int turns = 0, jumps = 0, enters = 0, exits = 0;
        double clickMedian = 0.0, clickMax = -99.0;
        double enterStep = 0.0, exitStep = 0.0;   ///< the worst |level after - level before| (dB)
    };

    Measured measure (const Render& r, double turnSeconds)
    {
        Measured m;
        const auto x = monoOf (r.audio);
        double peak = 0.0, sum = 0.0;
        for (const float v : x)
        {
            m.finite = m.finite && std::isfinite (v);
            peak = std::max (peak, static_cast<double> (std::abs (v)));
            sum += v;
        }
        m.peakDb = 20.0 * std::log10 (std::max (1.0e-9, peak));
        m.dc = sum / static_cast<double> (std::max<std::size_t> (1, x.size()));
        m.lastSecondDb = rmsDb (x, static_cast<double> (x.size()) - outRate, static_cast<double> (x.size()));
        std::vector<double> clicks;
        const double ms = outRate / 1000.0;
        for (const auto& e : r.events)
        {
            const double at = static_cast<double> (e.sample);
            if (e.kind == Event::Kind::turn || e.kind == Event::Kind::jump)
            {
                (e.kind == Event::Kind::turn ? m.turns : m.jumps)++;
                // A turn is recorded where it ends (its crossfade lies before it); a jump where it starts.
                const double span = e.kind == Event::Kind::turn ? turnSeconds * outRate + 2.0 * ms : 0.0;
                const auto a = static_cast<std::size_t> (std::max (0.0, at - span - 2.0 * ms));
                const auto b = static_cast<std::size_t> (at + (e.kind == Event::Kind::jump ? 45.0 * ms : 2.0 * ms));
                clicks.push_back (clickDb (x, a, b));
            }
            else
            {
                (e.kind == Event::Kind::enter ? m.enters : m.exits)++;
                const double step = std::abs (rmsDb (x, at + 60.0 * ms, at + 110.0 * ms) - rmsDb (x, at - 60.0 * ms, at - 10.0 * ms));
                (e.kind == Event::Kind::enter ? m.enterStep : m.exitStep) = std::max (e.kind == Event::Kind::enter ? m.enterStep : m.exitStep, step);
            }
        }
        if (! clicks.empty())
        {
            std::sort (clicks.begin(), clicks.end());
            m.clickMedian = clicks[clicks.size() / 2];
            m.clickMax = clicks.back();
        }
        return m;
    }

    std::string fixed (double v, int digits = 1)
    {
        std::ostringstream s;
        s << std::fixed << std::setprecision (digits) << v;
        return s.str();
    }

    const char* modeName (PedalLoopMode m)
    {
        return m == PedalLoopMode::normal ? "Normal" : (m == PedalLoopMode::forward ? "Forward" : "Ping-Pong");
    }
}

std::vector<PedalLabSource> pedalLabStandIns (double sr)
{
    std::vector<PedalLabSource> s;
    s.push_back ({ "piano", piano (220.0, 4.0, sr), true });
    s.push_back ({ "nyckelharpa", bowed (293.7, 3.5, sr, 10.0, 0.09, 3, true), true });
    s.push_back ({ "cello", bowed (130.8, 3.5, sr, 18.0, 0.18, 4, false), true });
    {
        auto v = testsignals::vowel (196.0, 2.5, sr, 21, 0.45, 2);
        testsignals::applyFades (v, 0.05, 0.3);
        s.push_back ({ "vocal-vowel", std::move (v), true });
    }
    s.push_back ({ "vocal-phrase", phrase (sr), true });
    {
        auto g = testsignals::pluck (196.0, 2.5, sr, 9, 0.7);
        testsignals::applyFades (g, 0.0, 1.0);
        s.push_back ({ "guitar", std::move (g), true });
    }
    s.push_back ({ "organ", organ (261.6, 3.0, sr), true });
    s.push_back ({ "percussive", hit (sr), true });
    return s;
}

PedalLabResult runPedalLoopLab (const std::vector<PedalLabSource>& sources, const std::filesystem::path& outDir, bool writeAudio)
{
    PedalLabResult result;
    std::ostringstream md;
    md << "| Source | Render | Mode | Turn ms | Finite | Peak dBFS | Enter / turns / jumps / exit | Click median / max dB | Level step enter / exit dB | Last s dB | Voices left |\n";
    md << "|---|---|---|---|---|---|---|---|---|---|---|\n";
    std::ostringstream regionsMd;
    regionsMd << "| Source | Stand-in | Pedal loop | Regions | First region (s) | Continuation |\n|---|---|---|---|---|---|\n";

    std::vector<std::shared_ptr<const InstrumentModel>> models;
    for (const auto& src : sources)
    {
        const auto analysis = Analyzer::analyse (src.audio);
        models.push_back (instrument::buildComplete (src.audio, analysis, {}, false));
    }
    for (std::size_t si = 0; si < sources.size(); ++si)
    {
        const auto& src = sources[si];
        const auto& model = *models[si];
        const auto& pedal = model.original.continuation.pedal;
        const double sr = model.original.source->sampleRate();
        regionsMd << "| " << src.name << " | " << (src.standIn ? "yes" : "no") << " | " << (pedal.valid ? "valid" : "none") << " (" << pedal.reason << ") | "
                  << pedal.regions.size() << " | "
                  << (pedal.regions.empty() ? std::string ("-") : fixed (pedal.regions.front().startFrame / sr, 3) + " - " + fixed (pedal.regions.front().endFrame / sr, 3))
                  << " | " << model.original.continuation.reason << " |\n";

        const int note = src.note >= 0 ? src.note : static_cast<int> (std::lround (model.rootMidi));
        const double soundEnd = pedal.valid ? pedal.soundEndFrame / sr : src.audio.durationSeconds();
        const double lastLeave = pedal.regions.empty() ? soundEnd : pedal.regions.back().endFrame / sr;
        auto on = [note] (int offset = 0) { return [note, offset] (InstrumentEngine& e) { e.noteOn (note + offset, 100); }; };
        auto off = [note] (int offset = 0) { return [note, offset] (InstrumentEngine& e) { e.noteOff (note + offset); }; };
        auto pedalAt = [] (bool down) { return [down] (InstrumentEngine& e) { e.setSustainPedal (down); }; };

        std::vector<Performance> performances;
        performances.push_back ({ "H-key-up-under-pedal", { { 0.0, pedalAt (true) }, { 0.05, on() }, { 1.0, off() }, { 6.0, pedalAt (false) } }, 7.0 });
        performances.push_back ({ "D-G-pedal-first-released-with-key-held", { { 0.0, pedalAt (true) }, { 0.05, on() }, { 5.0, pedalAt (false) }, { 6.5, off() } }, 8.0 });
        performances.push_back ({ "E-pedal-halfway", { { 0.0, on() }, { 0.5 * soundEnd, pedalAt (true) }, { 0.5 * soundEnd + 0.5, off() }, { 6.0, pedalAt (false) } }, 7.0 });
        const double late = std::min (lastLeave + 0.05, std::max (0.1, soundEnd - 0.08));
        performances.push_back ({ "F-pedal-near-the-end", { { 0.0, on() }, { late, pedalAt (true) }, { late + 3.5, pedalAt (false) }, { late + 4.5, off() } }, late + 6.0 });
        performances.push_back ({ "J-chord", { { 0.0, pedalAt (true) }, { 0.05, on (0) }, { 0.05, on (4) }, { 0.05, on (7) }, { 0.05, on (12) },
                                               { 1.0, off (0) }, { 1.0, off (4) }, { 1.0, off (7) }, { 1.0, off (12) }, { 6.0, pedalAt (false) } }, 7.0 });

        const std::array<const InstrumentModel*, 3> one { &model, nullptr, nullptr };
        auto emit = [&] (const std::string& renderName, PedalLoopMode mode, double turn, float corr, bool loop, const Performance& p,
                         const std::array<const InstrumentModel*, 3>& ms, int layers) {
            const auto r = render (ms, layers, mode, turn, corr, loop, p);
            const auto m = measure (r, turn);
            ++result.renders;
            result.allFinite = result.allFinite && m.finite;
            md << "| " << src.name << " | " << renderName << " | " << modeName (mode) << " | " << (mode == PedalLoopMode::pingPong ? fixed (turn * 1000.0, 0) : "-")
               << " | " << (m.finite ? "yes" : "**NO**") << " | " << fixed (m.peakDb) << " | " << m.enters << " / " << m.turns << " / " << m.jumps << " / " << m.exits
               << " | " << (m.turns + m.jumps > 0 ? fixed (m.clickMedian) + " / " + fixed (m.clickMax) : std::string ("-")) << " | "
               << (m.enters > 0 ? fixed (m.enterStep) : std::string ("-")) << " / " << (m.exits > 0 ? fixed (m.exitStep) : std::string ("-")) << " | "
               << fixed (m.lastSecondDb) << " | " << r.voicesLeft << " |\n";
            if (writeAudio)
            {
                const auto dir = outDir / src.name;
                std::filesystem::create_directories (dir);
                std::string error;
                io::writeAudioFile (dir / (renderName + ".wav"), r.audio, io::SampleFormat::pcm24, error);
            }
        };
        for (const auto& p : performances)
            for (const auto mode : { PedalLoopMode::normal, PedalLoopMode::forward, PedalLoopMode::pingPong })
            {
                const std::string name = p.name + "-" + (mode == PedalLoopMode::normal ? "normal" : (mode == PedalLoopMode::forward ? "forward" : "pingpong"));
                emit (name, mode, 0.008, 0.5f, true, p, one, 1);
            }
        // Ping-Pong's turnaround: A a plain reflection, B mirror crossfades of 4, 8, 15 ms, C the
        // 8 ms crossfade's gains assuming uncorrelated (0, equal power) or identical (1, linear) audio.
        for (const auto& [label, turn, corr] : std::vector<std::tuple<std::string, double, float>> {
                 { "A-reflect", 0.0, 0.5f }, { "B-4ms", 0.004, 0.5f }, { "B-15ms", 0.015, 0.5f }, { "C-8ms-equal-power", 0.008, 0.0f }, { "C-8ms-linear", 0.008, 1.0f } })
            emit ("turn-" + label, PedalLoopMode::pingPong, turn, corr, true, performances.front(), one, 1);
        // LOOP off (Forward then loops even a source the continuation holds).
        emit ("H-loop-off-forward", PedalLoopMode::forward, 0.008, 0.5f, false, performances.front(), one, 1);
    }
    // Two layers at once (the first two sources): each note's loop apart, one pedal.
    if (models.size() >= 2)
    {
        const std::array<const InstrumentModel*, 3> two { models[0].get(), models[1].get(), nullptr };
        const int note = static_cast<int> (std::lround (models[0]->rootMidi));
        Performance p { "H-key-up-under-pedal", { { 0.0, [] (InstrumentEngine& e) { e.setSustainPedal (true); } }, { 0.05, [note] (InstrumentEngine& e) { e.noteOn (note, 100); } },
                                                 { 1.0, [note] (InstrumentEngine& e) { e.noteOff (note); } }, { 6.0, [] (InstrumentEngine& e) { e.setSustainPedal (false); } } },
                        7.0 };
        for (const auto mode : { PedalLoopMode::normal, PedalLoopMode::pingPong })
        {
            const auto r = render (two, 2, mode, 0.008, 0.5f, true, p);
            const auto m = measure (r, 0.008);
            ++result.renders;
            result.allFinite = result.allFinite && m.finite;
            md << "| " << sources[0].name << " + " << sources[1].name << " | layered-H | " << modeName (mode) << " | 8 | " << (m.finite ? "yes" : "**NO**") << " | "
               << fixed (m.peakDb) << " | " << m.enters << " / " << m.turns << " / " << m.jumps << " / " << m.exits << " | "
               << (m.turns + m.jumps > 0 ? fixed (m.clickMedian) + " / " + fixed (m.clickMax) : std::string ("-")) << " | - | " << fixed (m.lastSecondDb) << " | "
               << r.voicesLeft << " |\n";
            if (writeAudio)
            {
                std::filesystem::create_directories (outDir / "layered");
                std::string error;
                io::writeAudioFile (outDir / "layered" / (std::string ("H-") + (mode == PedalLoopMode::normal ? "normal" : "pingpong") + ".wav"), r.audio,
                                    io::SampleFormat::pcm24, error);
            }
        }
    }
    result.markdown = "## Sources and their loop regions\n\n" + regionsMd.str() + "\n## Renders\n\n" + md.str();
    return result;
}

std::string runPedalLoopBenchmark (const PedalLabSource& source, int repeats)
{
    const auto analysis = Analyzer::analyse (source.audio);
    const auto model = instrument::buildComplete (source.audio, analysis, {}, false);
    const int root = static_cast<int> (std::lround (model->rootMidi));
    // The window every voice sounds through in every mode (the recording still reading in Normal).
    const double window = std::max (0.5, std::min (2.0, 0.6 * source.audio.durationSeconds()));
    struct Case
    {
        std::string name;
        double rate;
        int voices, layers;
        bool imprint, pump;
    };
    std::vector<Case> cases;
    for (const double rate : { 44100.0, 48000.0, 96000.0 })
        for (const int voices : { 1, 8, 16, 32 })
            cases.push_back ({ "One Shot", rate, voices, 1, false, false });
    cases.push_back ({ "3 layers", 48000.0, 16, 3, false, false });
    cases.push_back ({ "One Shot + Imprint", 48000.0, 16, 1, true, false });
    cases.push_back ({ "pedal pumped every 20 ms", 48000.0, 16, 1, false, true });

    std::ostringstream md;
    md << "Source: " << source.name << " (" << fixed (source.audio.durationSeconds(), 2) << " s); " << fixed (window, 2)
       << " s rendered per run, every voice sounding throughout; median of " << repeats << " runs. "
       << "Share of one core (render time / audio time).\n\n";
    md << "| Case | Rate | Voices (x layers) | Normal | Forward | Ping-Pong | Ping-Pong / Normal |\n|---|---|---|---|---|---|---|\n";
    for (const auto& c : cases)
    {
        std::array<double, 3> share {};
        for (int m = 0; m < 3; ++m)
        {
            std::vector<double> runs;
            for (int rep = 0; rep < repeats; ++rep)
            {
                EngineSettings s;
                s.macros.space = 0.0;
                s.macros.motion = 0.0;
                s.polyphony = 64;
                s.voicesPerKey = 64;
                for (auto& l : s.layer)
                {
                    l.pedalLoop = static_cast<PedalLoopMode> (m);
                    l.synthesize = c.imprint ? 0.5 : 0.0;
                    l.synth.mode = c.imprint ? synth::Mode::imprint : synth::Mode::analog;
                    l.synth.imprint = c.imprint ? 1.0 : 0.0;
                }
                auto engine = std::make_unique<InstrumentEngine>();
                const int blockSize = 128;
                engine->prepare (c.rate, blockSize, s);
                for (int l = 0; l < c.layers; ++l)
                    engine->setModel (model.get(), l);
                engine->setMixSlots (c.layers);
                engine->setSustainPedal (true);
                for (int v = 0; v < c.voices; ++v)
                    engine->noteOn (root - 12 + (v * 7) % 24, 90);
                std::vector<float> l (blockSize), r (blockSize);
                float* ch[2] = { l.data(), r.data() };
                const auto total = static_cast<int> (window * c.rate);
                bool down = true;
                const auto t0 = std::chrono::steady_clock::now();
                for (int done = 0, b = 0; done < total; done += blockSize, ++b)
                {
                    if (c.pump && b % std::max (1, static_cast<int> (0.02 * c.rate / blockSize)) == 0)
                        engine->setSustainPedal (down = ! down);
                    engine->render (ch, 2, blockSize);
                }
                const double seconds = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
                runs.push_back (seconds / window);
            }
            std::sort (runs.begin(), runs.end());
            share[static_cast<std::size_t> (m)] = runs[runs.size() / 2];
        }
        md << "| " << c.name << " | " << fixed (c.rate / 1000.0, 1) << " kHz | " << c.voices << (c.layers > 1 ? " x " + std::to_string (c.layers) : std::string())
           << " | " << fixed (100.0 * share[0]) << " % | " << fixed (100.0 * share[1]) << " % | " << fixed (100.0 * share[2]) << " % | "
           << fixed (share[2] / std::max (1.0e-9, share[0]), 2) << " |\n";
    }
    return md.str();
}

} // namespace osp::research
