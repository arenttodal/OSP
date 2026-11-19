#include "research/PedalReimaginedLab.h"

#include "analysis/Analyzer.h"
#include "engine/InstrumentBuilder.h"
#include "engine/InstrumentEngine.h"
#include "io/AudioFileIO.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <memory>
#include <sstream>

namespace osp::research
{

namespace
{
    constexpr double outRate = 48000.0;
    constexpr int block = 32;

    constexpr std::array<ReimaginedMode, 5> modes { ReimaginedMode::kaleidoscope, ReimaginedMode::tapeFrame, ReimaginedMode::toybox,
                                                    ReimaginedMode::mosaic, ReimaginedMode::mirage };

    const char* modeName (ReimaginedMode m)
    {
        switch (m)
        {
            case ReimaginedMode::kaleidoscope: return "kaleidoscope";
            case ReimaginedMode::tapeFrame: return "tape-frame";
            case ReimaginedMode::toybox: return "toybox";
            case ReimaginedMode::mosaic: return "mosaic";
            case ReimaginedMode::mirage: return "mirage";
        }
        return "?";
    }

    const char* pedalName (PedalLoopMode m)
    {
        return m == PedalLoopMode::normal ? "normal" : (m == PedalLoopMode::forward ? "forward" : "pingpong");
    }

    EngineSettings settingsFor (ReimaginedMode mode, double amount, PedalLoopMode pedal)
    {
        EngineSettings s;
        s.reimaginedRouting = ReimaginedRouting::perLayer;
        s.macros.space = 0.0;
        s.macros.motion = 0.0;
        s.adsr.releaseSeconds = 0.4;
        for (auto& l : s.layer)
        {
            l.reimagined = amount;
            l.reimaginedSettings.mode = mode;
            l.pedalLoop = pedal;
            l.loop = false;
        }
        return s;
    }

    AudioData render (const InstrumentModel& model, int note, ReimaginedMode mode, double amount, PedalLoopMode pedal)
    {
        auto engine = std::make_unique<InstrumentEngine>();
        engine->prepare (outRate, block, settingsFor (mode, amount, pedal));
        engine->setModel (&model, 0);
        AudioData out;
        out.sampleRate = outRate;
        out.channels.assign (2, {});
        const auto total = static_cast<std::size_t> (10.0 * outRate);
        std::array<float, block> l {}, r {};
        engine->setSustainPedal (true);
        bool on = false, off = false, up = false;
        for (std::size_t done = 0; done < total; done += block)
        {
            const double t = static_cast<double> (done) / outRate;
            if (! on && t >= 0.1)
                engine->noteOn (note, 100), on = true;
            if (! off && t >= 1.0)
                engine->noteOff (note), off = true;
            if (! up && t >= 8.0)
                engine->setSustainPedal (false), up = true;
            float* ch[2] = { l.data(), r.data() };
            engine->render (ch, 2, block);
            out.channels[0].insert (out.channels[0].end(), l.begin(), l.end());
            out.channels[1].insert (out.channels[1].end(), r.begin(), r.end());
        }
        return out;
    }

    std::vector<float> monoOf (const AudioData& a)
    {
        std::vector<float> m (a.channels[0].size());
        for (std::size_t i = 0; i < m.size(); ++i)
            m[i] = 0.5f * (a.channels[0][i] + a.channels[1][i]);
        return m;
    }

    double rmsDb (const std::vector<float>& x, double fromSec, double toSec)
    {
        const auto a = static_cast<std::size_t> (std::max (0.0, fromSec * outRate)), b = std::min (x.size(), static_cast<std::size_t> (std::max (0.0, toSec * outRate)));
        double sum = 0.0;
        for (auto i = a; i < b; ++i)
            sum += static_cast<double> (x[i]) * x[i];
        return 10.0 * std::log10 (std::max (1.0e-20, sum / static_cast<double> (std::max<std::size_t> (1, b > a ? b - a : 1))));
    }

    /** The worst 5 ms of high-passed energy (second difference) against the 40 ms on either
        side of it, between `fromSec` and `toSec` (dB): a click stands out by many dB. */
    double worstClickDb (const std::vector<float>& x, double fromSec, double toSec)
    {
        const auto w = static_cast<std::size_t> (0.005 * outRate);
        std::vector<double> e;   // energy per 5 ms
        for (auto i = static_cast<std::size_t> (fromSec * outRate); i + w < std::min (x.size(), static_cast<std::size_t> (toSec * outRate)); i += w)
        {
            double sum = 0.0;
            for (auto k = i; k < i + w; ++k)
            {
                const double d = k >= 2 ? static_cast<double> (x[k]) - 2.0 * x[k - 1] + x[k - 2] : 0.0;
                sum += d * d;
            }
            e.push_back (sum / static_cast<double> (w));
        }
        double worst = -99.0;
        for (std::size_t k = 8; k + 8 < e.size(); ++k)
        {
            double around = 0.0;
            for (std::size_t j = 1; j <= 8; ++j)
                around += e[k - j] + e[k + j];
            around /= 16.0;
            worst = std::max (worst, 10.0 * std::log10 (std::max (1.0e-24, e[k]) / std::max (1.0e-24, around)));
        }
        return worst;
    }

    struct Measured
    {
        bool finite = true;
        double peakDb = -120.0, heldDb = -120.0, clickDb = -99.0, riseDb = -99.0, quietDb = 0.0, pedalUpStepDb = 0.0, endDb = -120.0;
    };

    Measured measure (const AudioData& a)
    {
        Measured m;
        const auto x = monoOf (a);
        double peak = 0.0;
        for (const float v : x)
        {
            m.finite = m.finite && std::isfinite (v);
            peak = std::max (peak, static_cast<double> (std::abs (v)));
        }
        m.peakDb = 20.0 * std::log10 (std::max (1.0e-9, peak));
        m.heldDb = rmsDb (x, 7.0, 8.0);
        m.clickDb = worstClickDb (x, 1.2, 7.9);
        for (double t = 0.4; t + 0.01 < 7.9; t += 0.005)
            m.riseDb = std::max (m.riseDb, rmsDb (x, t, t + 0.01) - rmsDb (x, t - 0.03, t - 0.02));
        const double held = rmsDb (x, 1.2, 7.9);
        m.quietDb = 0.0;
        for (double t = 1.2; t + 0.05 <= 7.9; t += 0.05)
            m.quietDb = std::min (m.quietDb, rmsDb (x, t, t + 0.05) - held);
        m.pedalUpStepDb = rmsDb (x, 8.02, 8.07) - rmsDb (x, 7.93, 7.98);
        m.endDb = rmsDb (x, 9.5, 10.0);
        return m;
    }

    std::string fixed (double v, int digits = 1)
    {
        std::ostringstream s;
        s << std::fixed << std::setprecision (digits) << v;
        return s.str();
    }

    void writeWav (const AudioData& a, const std::filesystem::path& path)
    {
        std::filesystem::create_directories (path.parent_path());
        std::string error;
        io::writeAudioFile (path, a, io::SampleFormat::pcm24, error);
    }
}

PedalReimaginedResult runPedalReimaginedLab (const std::vector<PedalLabSource>& sources, const std::filesystem::path& outDir, bool writeAudio)
{
    PedalReimaginedResult result;
    std::ostringstream md;
    md << "# PEDAL LOOP x REIMAGINED: listening matrix\n\n"
       << "One performance per render: the pedal down, the note at 0.1 s, the key up at 1 s, the pedal up at 8 s, 10 s in all "
       << "(One Shot, LOOP off). A = REIMAGINED off with the pedal loop, B = 50 %, C = 100 %, D = 100 % with Normal (the "
       << "ordinary pedal: the note ends with the recording).\n\n"
       << "Columns: held = level 7-8 s (dBFS); click = the worst 5 ms of high-passed energy over the held loop against the "
       << "40 ms around it (dB; a click stands out by 15 dB or more); rise = the largest jump of the 10 ms level (a repeated "
       << "attack); quiet = the quietest 50 ms against the held level; up = the level step at pedal up; end = level 9.5-10 s.\n\n";
    for (const auto& source : sources)
    {
        const auto analysis = Analyzer::analyse (source.audio);
        const auto model = instrument::buildComplete (source.audio, analysis, {}, false);
        const int note = source.note >= 0 ? source.note : static_cast<int> (std::lround (model->rootMidi));
        const auto& pedalModel = model->original.continuation.pedal;
        md << "## " << source.name << (source.standIn ? " (synthetic stand-in)" : "") << "\n\n"
           << "Pedal regions: " << (pedalModel.valid ? std::to_string (pedalModel.regions.size()) : "none (" + pedalModel.reason + ")") << "\n\n"
           << "| render | held | click | rise | quiet | up | end | peak |\n|---|---|---|---|---|---|---|---|\n";
        auto row = [&] (const std::string& name, ReimaginedMode mode, double amount, PedalLoopMode pedal) {
            const auto audio = render (*model, note, mode, amount, pedal);
            const auto m = measure (audio);
            result.allFinite = result.allFinite && m.finite;
            ++result.renders;
            md << "| " << name << " | " << fixed (m.heldDb) << " | " << fixed (m.clickDb) << " | " << fixed (m.riseDb) << " | " << fixed (m.quietDb) << " | "
               << fixed (m.pedalUpStepDb) << " | " << fixed (m.endDb) << " | " << fixed (m.peakDb) << (m.finite ? "" : " NOT FINITE") << " |\n";
            if (writeAudio)
                writeWav (audio, outDir / source.name / (name + ".wav"));
        };
        for (const auto pedal : { PedalLoopMode::forward, PedalLoopMode::pingPong })
            row (std::string ("A-off-") + pedalName (pedal), ReimaginedMode::kaleidoscope, 0.0, pedal);
        for (const auto mode : modes)
        {
            for (const auto pedal : { PedalLoopMode::forward, PedalLoopMode::pingPong })
            {
                row (std::string (modeName (mode)) + "-B-050-" + pedalName (pedal), mode, 0.5, pedal);
                row (std::string (modeName (mode)) + "-C-100-" + pedalName (pedal), mode, 1.0, pedal);
            }
            row (std::string (modeName (mode)) + "-D-100-normal", mode, 1.0, PedalLoopMode::normal);
        }
        md << "\n";
    }
    result.markdown = md.str();
    return result;
}

std::string runPedalReimaginedBenchmark (const PedalLabSource& source, int repeats)
{
    const auto analysis = Analyzer::analyse (source.audio);
    const auto model = instrument::buildComplete (source.audio, analysis, {}, false);
    const int root = static_cast<int> (std::lround (model->rootMidi));
    const double window = std::max (0.5, std::min (2.0, 0.6 * source.audio.durationSeconds()));
    std::ostringstream md;
    md << "Source: " << source.name << "; 16 notes held under the pedal, REIMAGINED 100 %, 48 kHz, " << fixed (window, 2)
       << " s per run (every voice sounding), median of " << repeats << " runs. Share of one core.\n\n"
       << "| mode | Normal | Forward | Ping-Pong | Forward / Normal |\n|---|---|---|---|---|\n";
    for (const auto mode : modes)
    {
        std::array<double, 3> share {};
        for (int p = 0; p < 3; ++p)
        {
            std::vector<double> runs;
            for (int rep = 0; rep < repeats; ++rep)
            {
                auto s = settingsFor (mode, 1.0, static_cast<PedalLoopMode> (p));
                s.polyphony = 64;
                s.voicesPerKey = 64;
                auto engine = std::make_unique<InstrumentEngine>();
                const int blockSize = 128;
                engine->prepare (outRate, blockSize, s);
                engine->setModel (model.get(), 0);
                engine->setSustainPedal (true);
                for (int v = 0; v < 16; ++v)
                    engine->noteOn (root - 12 + (v * 7) % 24, 90);
                std::vector<float> l (blockSize), r (blockSize);
                float* ch[2] = { l.data(), r.data() };
                const auto total = static_cast<int> (window * outRate);
                const auto t0 = std::chrono::steady_clock::now();
                for (int done = 0; done < total; done += blockSize)
                    engine->render (ch, 2, blockSize);
                const double secs = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
                runs.push_back (secs / window);
            }
            std::sort (runs.begin(), runs.end());
            share[static_cast<std::size_t> (p)] = runs[runs.size() / 2];
        }
        md << "| " << modeName (mode) << " | " << fixed (100.0 * share[0], 2) << " % | " << fixed (100.0 * share[1], 2) << " % | " << fixed (100.0 * share[2], 2)
           << " % | " << fixed (share[1] / std::max (1.0e-9, share[0]), 3) << " |\n";
    }
    return md.str();
}

} // namespace osp::research
