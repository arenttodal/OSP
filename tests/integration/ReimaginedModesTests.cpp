// REIMAGINED modes (KALEIDOSCOPE, TAPE FRAME, TOYBOX, MOSAIC, MIRAGE): the shared analysis,
// each engine through the whole instrument, exact bypass at 0 %, determinism, note
// lifecycle, mode switching, layer independence and sample-rate consistency.
#include "analysis/reimagined/ReimaginedAnalyzer.h"
#include "audio/utility/TestSignals.h"
#include "core/PitchMath.h"
#include "engine/InstrumentBuilder.h"
#include "engine/InstrumentEngine.h"
#include "io/AudioFileIO.h"
#include "support/TestHelpers.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <iostream>
#include <memory>
#include <numbers>
#include <sstream>

using namespace osp;
using Catch::Approx;

namespace
{
    constexpr std::array<ReimaginedMode, 5> allModes { ReimaginedMode::kaleidoscope, ReimaginedMode::tapeFrame, ReimaginedMode::toybox,
                                                       ReimaginedMode::mosaic, ReimaginedMode::mirage };
    constexpr std::array<ReimaginedMode, 4> newModes { ReimaginedMode::tapeFrame, ReimaginedMode::toybox, ReimaginedMode::mosaic,
                                                       ReimaginedMode::mirage };

    std::shared_ptr<InstrumentModel> vowelModel (double rate = 48000.0, double seconds = 2.0)
    {
        auto audio = testsignals::vowel (midiToHz (57), seconds, rate, 3);
        testsignals::applyFades (audio, 0.01, 0.05);
        return instrument::buildComplete (audio, test::analyse (audio), {}, false);
    }

    std::shared_ptr<InstrumentModel> pluckModel (double rate = 48000.0)
    {
        auto audio = testsignals::pluck (midiToHz (60), 2.5, rate, 5);
        return instrument::buildComplete (audio, test::analyse (audio), {}, false);
    }

    EngineSettings settingsFor (ReimaginedMode mode, double amount)
    {
        EngineSettings s;
        s.reimaginedRouting = ReimaginedRouting::perLayer;
        s.macros.space = 0.0;
        s.adsr.releaseSeconds = 0.2;
        for (auto& layer : s.layer)
        {
            layer.reimagined = amount;
            layer.reimaginedSettings.mode = mode;
        }
        return s;
    }

    struct Render
    {
        std::vector<float> l, r;
        double peak() const
        {
            double p = 0.0;
            for (std::size_t i = 0; i < l.size(); ++i)
                p = std::max ({ p, static_cast<double> (std::abs (l[i])), static_cast<double> (std::abs (r[i])) });
            return p;
        }
        double rms (std::size_t from = 0, std::size_t to = 0) const
        {
            to = to == 0 ? l.size() : std::min (to, l.size());
            double sum = 0.0;
            for (std::size_t i = from; i < to; ++i)
                sum += 0.5 * (static_cast<double> (l[i]) * l[i] + static_cast<double> (r[i]) * r[i]);
            return to > from ? std::sqrt (sum / static_cast<double> (to - from)) : 0.0;
        }
        bool finite() const
        {
            for (std::size_t i = 0; i < l.size(); ++i)
                if (! std::isfinite (l[i]) || ! std::isfinite (r[i]))
                    return false;
            return true;
        }
    };

    struct Event
    {
        double seconds;
        int note;
        int velocity;   ///< 0: note off
    };

    /** Plays `events` through an engine; `between` runs between blocks (live changes). */
    Render play (const std::vector<const InstrumentModel*>& models, const EngineSettings& s, const std::vector<Event>& events, double seconds,
                 double rate = 48000.0, int block = 256, const std::function<void (InstrumentEngine&, double)>& between = {})
    {
        auto engine = std::make_unique<InstrumentEngine>();
        engine->prepare (rate, block, s);
        for (int i = 0; i < static_cast<int> (models.size()); ++i)
        {
            engine->setModel (models[static_cast<std::size_t> (i)], i);
            engine->setLayerSettings (i, s.layer[static_cast<std::size_t> (i)]);
        }
        Render out;
        std::vector<float> l (static_cast<std::size_t> (block)), r (static_cast<std::size_t> (block));
        const auto total = static_cast<int> (seconds * rate);
        std::size_t next = 0;
        for (int done = 0; done < total;)
        {
            const double now = done / rate;
            if (between)
                between (*engine, now);
            // Split blocks at events so timing never depends on the block size.
            int n = std::min (block, total - done);
            while (next < events.size() && static_cast<int> (std::lround (events[next].seconds * rate)) <= done)
            {
                const auto& e = events[next++];
                if (e.velocity > 0)
                    engine->noteOn (e.note, e.velocity);
                else
                    engine->noteOff (e.note);
            }
            if (next < events.size())
                n = std::min (n, std::max (1, static_cast<int> (std::lround (events[next].seconds * rate)) - done));
            float* ch[2] = { l.data(), r.data() };
            engine->render (ch, 2, n);
            out.l.insert (out.l.end(), l.begin(), l.begin() + n);
            out.r.insert (out.r.end(), r.begin(), r.begin() + n);
            done += n;
        }
        return out;
    }

    std::vector<Event> chord (double hold = 1.5)
    {
        return { { 0.0, 57, 100 }, { 0.0, 61, 90 }, { 0.0, 64, 95 }, { hold, 57, 0 }, { hold, 61, 0 }, { hold, 64, 0 } };
    }

    /** Fundamental of a steady stretch (autocorrelation peak, parabolic), Hz. */
    double fundamentalOf (const std::vector<float>& x, std::size_t from, std::size_t length, double rate, double lo, double hi)
    {
        const auto minLag = static_cast<int> (rate / hi), maxLag = static_cast<int> (rate / lo);
        std::vector<double> c (static_cast<std::size_t> (maxLag + 2), 0.0);
        for (int lag = minLag - 1; lag <= maxLag + 1; ++lag)
        {
            double sum = 0.0, e0 = 0.0, e1 = 0.0;
            for (std::size_t i = from; i < from + length; ++i)
            {
                sum += static_cast<double> (x[i]) * x[i + static_cast<std::size_t> (lag)];
                e0 += static_cast<double> (x[i]) * x[i];
                e1 += static_cast<double> (x[i + static_cast<std::size_t> (lag)]) * x[i + static_cast<std::size_t> (lag)];
            }
            c[static_cast<std::size_t> (lag)] = sum / std::sqrt (e0 * e1 + 1.0e-30);
        }
        int best = minLag;
        for (int lag = minLag; lag <= maxLag; ++lag)
            if (c[static_cast<std::size_t> (lag)] > c[static_cast<std::size_t> (best)])
                best = lag;
        const double a = c[static_cast<std::size_t> (best - 1)], b = c[static_cast<std::size_t> (best)], d = c[static_cast<std::size_t> (best + 1)];
        const double offset = std::abs (a - 2.0 * b + d) > 1.0e-12 ? 0.5 * (a - d) / (a - 2.0 * b + d) : 0.0;
        return rate / (best + offset);
    }
}

TEST_CASE ("reimagined analysis: a sustained vowel gets a spliced tape and harmonic frames", "[integration][reimagined-modes]")
{
    const auto model = vowelModel();
    REQUIRE (model->reimagined != nullptr);
    const auto& a = *model->reimagined;
    CHECK (a.tape.ready);
    CHECK (a.tape.splices.size() >= 2);   // a 2 s vowel lengthened by its own loops
    CHECK (a.tape.lengthFrames / a.tape.sampleRate > 11.0);
    for (std::size_t i = 1; i < a.tape.splices.size(); ++i)
        CHECK (a.tape.splices[i].frameStart > a.tape.splices[i - 1].frameStart);
    REQUIRE (a.mosaic.ready);
    CHECK (a.mosaic.frames.size() >= 16);
    CHECK (a.mosaic.frames.size() <= 32);
    CHECK (a.mosaic.fundamentalHz == Approx (midiToHz (57)).epsilon (0.02));
    CHECK (a.mosaic.sustains);
    // The vowel's partials are there (the first few carry most of the energy).
    const auto& f = a.mosaic.frames[static_cast<std::size_t> (a.mosaic.stableFrame)];
    CHECK (f.partial[0] + f.partial[1] + f.partial[2] > 0.01f);
}

TEST_CASE ("reimagined analysis: odd input is reported, never fatal", "[integration][reimagined-modes]")
{
    const double rate = 48000.0;
    {
        const auto noise = testsignals::whiteNoise (1.0, rate, 0.3, 7);
        const auto model = instrument::buildComplete (noise, test::analyse (noise), {}, false);
        REQUIRE (model->reimagined != nullptr);
        CHECK_FALSE (model->reimagined->mosaic.ready);   // nothing harmonic to rebuild
        CHECK_FALSE (model->reimagined->mosaic.reason.empty());
    }
    {
        const auto silence = testsignals::silence (0.5, rate);
        const auto model = instrument::buildComplete (silence, test::analyse (silence), {}, false);
        REQUIRE (model->reimagined != nullptr);
        CHECK_FALSE (model->reimagined->mosaic.ready);
    }
    {
        const auto tiny = testsignals::sine (440.0, 0.01, rate, 0.5);
        const auto model = instrument::buildComplete (tiny, test::analyse (tiny), {}, false);
        REQUIRE (model->reimagined != nullptr);
        CHECK_FALSE (model->reimagined->tape.ready);
    }
}

TEST_CASE ("reimagined modes: at 0 % every mode is the plain recording, exactly", "[integration][reimagined-modes]")
{
    const auto model = vowelModel();
    const auto reference = play ({ model.get() }, settingsFor (ReimaginedMode::kaleidoscope, 0.0), chord(), 2.5);
    for (auto mode : newModes)
    {
        const auto out = play ({ model.get() }, settingsFor (mode, 0.0), chord(), 2.5);
        INFO (reimagined::modeName (mode));
        CHECK (test::maxDifference (AudioData { 48000.0, { reference.l } }, AudioData { 48000.0, { out.l } }) == 0.0);
        CHECK (test::maxDifference (AudioData { 48000.0, { reference.r } }, AudioData { 48000.0, { out.r } }) == 0.0);
    }
}

TEST_CASE ("reimagined modes: each mode plays, stays bounded and sounds like itself", "[integration][reimagined-modes]")
{
    const auto vowel = vowelModel();
    const auto pluck = pluckModel();
    for (const auto* model : { vowel.get(), pluck.get() })
    {
        const auto plain = play ({ model }, settingsFor (ReimaginedMode::kaleidoscope, 0.0), chord(), 2.5);
        std::array<Render, 5> renders;
        for (std::size_t m = 0; m < allModes.size(); ++m)
        {
            renders[m] = play ({ model }, settingsFor (allModes[m], 0.75), chord(), 2.5);
            INFO (reimagined::modeName (allModes[m]));
            CHECK (renders[m].finite());
            CHECK (renders[m].peak() < 2.0);
            // Audible, and at a level in the same family as the recording (no runaway, no collapse).
            const double ratio = renders[m].rms (0, 72000) / plain.rms (0, 72000);
            CHECK (ratio > 0.25);
            CHECK (ratio < 3.0);
        }
        // Different instruments, not variants of one chain: every pair differs clearly.
        for (std::size_t a = 0; a < renders.size(); ++a)
            for (std::size_t b = a + 1; b < renders.size(); ++b)
            {
                double diff = 0.0, energy = 0.0;
                for (std::size_t i = 0; i < 72000; ++i)
                {
                    const double d = renders[a].l[i] - renders[b].l[i];
                    diff += d * d;
                    energy += 0.5 * (static_cast<double> (renders[a].l[i]) * renders[a].l[i] + static_cast<double> (renders[b].l[i]) * renders[b].l[i]);
                }
                INFO (reimagined::modeName (allModes[a]) << " vs " << reimagined::modeName (allModes[b]));
                CHECK (diff / energy > 0.05);
            }
    }
}

TEST_CASE ("reimagined modes: deterministic and block-size independent", "[integration][reimagined-modes]")
{
    const auto model = vowelModel();
    for (auto mode : newModes)
    {
        const auto s = settingsFor (mode, 0.8);
        const auto a = play ({ model.get() }, s, chord(), 2.0, 48000.0, 512);
        const auto b = play ({ model.get() }, s, chord(), 2.0, 48000.0, 512);
        const auto c = play ({ model.get() }, s, chord(), 2.0, 48000.0, 37);
        INFO (reimagined::modeName (mode));
        CHECK (test::maxDifference (AudioData { 48000.0, { a.l } }, AudioData { 48000.0, { b.l } }) == 0.0);
        CHECK (test::maxDifference (AudioData { 48000.0, { a.l } }, AudioData { 48000.0, { c.l } }) < 1.0e-5);
    }
}

TEST_CASE ("reimagined modes: notes end with the envelope, nothing hangs", "[integration][reimagined-modes]")
{
    const auto model = vowelModel();
    for (auto mode : newModes)
    {
        auto engine = std::make_unique<InstrumentEngine>();
        const auto s = settingsFor (mode, 1.0);
        engine->prepare (48000.0, 256, s);
        engine->setModel (model.get());
        engine->setLayerSettings (0, s.layer[0]);
        std::vector<float> l (256), r (256);
        float* ch[2] = { l.data(), r.data() };
        for (int n : { 48, 57, 60, 64, 69, 84 })
            engine->noteOn (n, 100);
        for (int i = 0; i < 200; ++i)
            engine->render (ch, 2, 256);
        INFO (reimagined::modeName (mode));
        CHECK (engine->activeVoiceCount() > 0);
        for (int i = 0; i < engine->voiceSlots(); ++i)
            if (engine->voiceAt (i).isActive())
                CHECK (engine->voiceAt (i).reimaginedMode() == mode);
        engine->allNotesOff();
        for (int i = 0; i < 200; ++i)   // ~1 s: release 0.2 s
            engine->render (ch, 2, 256);
        CHECK (engine->activeVoiceCount() == 0);
    }
}

TEST_CASE ("reimagined modes: switching mode leaves sounding notes on theirs", "[integration][reimagined-modes]")
{
    const auto model = vowelModel();
    auto engine = std::make_unique<InstrumentEngine>();
    auto s = settingsFor (ReimaginedMode::tapeFrame, 0.7);
    engine->prepare (48000.0, 256, s);
    engine->setModel (model.get());
    engine->setLayerSettings (0, s.layer[0]);
    std::vector<float> l (256), r (256);
    float* ch[2] = { l.data(), r.data() };
    engine->noteOn (57, 100);
    engine->render (ch, 2, 256);
    s.layer[0].reimaginedSettings.mode = ReimaginedMode::mosaic;
    s.layer[0].reimaginedSettings.tapeFrame.age = 0.9;   // a sub-setting changes live, the mode does not
    engine->setLayerSettings (0, s.layer[0]);
    engine->noteOn (64, 100);
    for (int i = 0; i < 20; ++i)
        engine->render (ch, 2, 256);
    int tape = 0, mosaic = 0;
    for (int i = 0; i < engine->voiceSlots(); ++i)
    {
        const auto& v = engine->voiceAt (i);
        if (! v.isActive())
            continue;
        tape += v.note() == 57 && v.reimaginedMode() == ReimaginedMode::tapeFrame;
        mosaic += v.note() == 64 && v.reimaginedMode() == ReimaginedMode::mosaic;
    }
    CHECK (tape == 1);
    CHECK (mosaic == 1);
    for (float x : l)
        CHECK (std::isfinite (x));
}

TEST_CASE ("reimagined modes: every layer its own mode, no cross-talk", "[integration][reimagined-modes]")
{
    const auto a = vowelModel();
    const auto b = pluckModel();
    auto both = settingsFor (ReimaginedMode::tapeFrame, 0.8);
    both.layer[1].reimaginedSettings.mode = ReimaginedMode::mosaic;
    both.blend = 0.5;
    auto onlyA = both;
    onlyA.layer[1].levelDb = LayerSettings::minLevelDb;
    auto onlyB = both;
    onlyB.layer[0].levelDb = LayerSettings::minLevelDb;
    const auto ab = play ({ a.get(), b.get() }, both, chord(), 2.0);
    const auto ra = play ({ a.get(), b.get() }, onlyA, chord(), 2.0);
    const auto rb = play ({ a.get(), b.get() }, onlyB, chord(), 2.0);
    double worst = 0.0;
    for (std::size_t i = 0; i < ab.l.size(); ++i)
        worst = std::max (worst, std::abs (static_cast<double> (ab.l[i]) - ra.l[i] - rb.l[i]));
    CHECK (worst < 1.0e-5 * std::max (1.0, ab.peak()));
}

TEST_CASE ("reimagined modes: pitch is the same at every sample rate", "[integration][reimagined-modes]")
{
    for (auto mode : newModes)
    {
        std::array<double, 4> hz {};
        const std::array<double, 4> rates { 44100.0, 48000.0, 88200.0, 96000.0 };
        for (std::size_t k = 0; k < rates.size(); ++k)
        {
            const double rate = rates[k];
            const auto model = vowelModel (48000.0);   // the recording's own rate stays; the host's changes
            auto s = settingsFor (mode, 0.6);
            s.shaping = Shaping::neutral();
            s.macros.life = 0.0;
            s.macros.motion = 0.0;
            const auto out = play ({ model.get() }, s, { { 0.0, 69, 100 } }, 1.2, rate, 256);
            std::vector<float> mono (out.l.size());
            for (std::size_t i = 0; i < mono.size(); ++i)
                mono[i] = 0.5f * (out.l[i] + out.r[i]);
            hz[k] = fundamentalOf (mono, static_cast<std::size_t> (0.5 * rate), static_cast<std::size_t> (0.2 * rate), rate, 300.0, 600.0);
        }
        INFO (reimagined::modeName (mode) << ": " << hz[0] << " " << hz[1] << " " << hz[2] << " " << hz[3]);
        for (double f : hz)
            CHECK (std::abs (1200.0 * std::log2 (f / 440.0)) < 15.0);   // on A4 (vowel at A3 played an octave up)
        for (double f : hz)
            CHECK (std::abs (1200.0 * std::log2 (f / hz[1])) < 3.0);
    }
}

TEST_CASE ("reimagined modes: a source that cannot be rebuilt plays the recording", "[integration][reimagined-modes]")
{
    auto noise = testsignals::whiteNoise (1.5, 48000.0, 0.3, 11);
    testsignals::applyFades (noise, 0.01, 0.05);
    const auto model = instrument::buildComplete (noise, test::analyse (noise), {}, false);
    REQUIRE_FALSE (model->reimagined->mosaic.ready);
    const auto plain = play ({ model.get() }, settingsFor (ReimaginedMode::kaleidoscope, 0.0), chord (1.0), 1.5);
    const auto out = play ({ model.get() }, settingsFor (ReimaginedMode::mosaic, 0.9), chord (1.0), 1.5);
    CHECK (test::maxDifference (AudioData { 48000.0, { plain.l } }, AudioData { 48000.0, { out.l } }) == 0.0);
}

TEST_CASE ("reimagined modes: KALEIDOSCOPE FOCUS and SPREAD at 50 % are the original", "[integration][reimagined-modes]")
{
    const auto model = vowelModel();
    auto neutral = settingsFor (ReimaginedMode::kaleidoscope, 0.8);
    const auto a = play ({ model.get() }, neutral, chord(), 2.0);
    auto moved = neutral;
    moved.layer[0].reimaginedSettings.kaleidoscope.focus = 0.9;
    moved.layer[0].reimaginedSettings.kaleidoscope.spread = 0.1;
    const auto b = play ({ model.get() }, moved, chord(), 2.0);
    // The neutral path is the one every older session plays (null-tested against the
    // pre-mode build in the plugin's reimagined-migration test); moving them changes it.
    CHECK (test::maxDifference (AudioData { 48000.0, { a.l } }, AudioData { 48000.0, { b.l } }) > 1.0e-3);
    double side = 0.0, sideMoved = 0.0;
    for (std::size_t i = 0; i < a.l.size(); ++i)
    {
        side += std::pow (static_cast<double> (a.l[i]) - a.r[i], 2.0);
        sideMoved += std::pow (static_cast<double> (b.l[i]) - b.r[i], 2.0);
    }
    CHECK (sideMoved < side);   // SPREAD low: narrower
}

// Audition (hidden): renders every mode on real recordings for listening and review.
//   OSP_AUDITION_DIR=<out> OSP_AUDITION_SAMPLES="a.wav;b.wav" ./osp_tests "[reimagined-audition]"
TEST_CASE ("reimagined modes: audition renders", "[.][reimagined-audition]")
{
    const char* dir = std::getenv ("OSP_AUDITION_DIR");
    const char* list = std::getenv ("OSP_AUDITION_SAMPLES");
    if (dir == nullptr || list == nullptr)
        return;
    std::stringstream paths (list);
    std::string path;
    while (std::getline (paths, path, ';'))
    {
        auto loaded = io::loadAudioFile (path, { 30.0, 2 });
        if (! loaded.ok)
            continue;
        const auto model = instrument::buildComplete (loaded.audio, test::analyse (loaded.audio), {}, false);
        const int root = static_cast<int> (std::lround (model->rootMidi));
        const std::vector<Event> phrase {
            { 0.0, root, 100 }, { 0.0, root + 4, 90 }, { 0.0, root + 7, 95 }, { 3.0, root, 0 }, { 3.0, root + 4, 0 }, { 3.0, root + 7, 0 },
            { 3.6, root - 12, 110 }, { 5.0, root - 12, 0 }, { 5.2, root + 12, 80 }, { 6.0, root + 12, 0 }, { 6.1, root + 19, 70 }, { 6.8, root + 19, 0 },
            { 7.0, root, 60 }, { 7.4, root, 0 }, { 7.5, root, 120 }, { 9.5, root, 0 }
        };
        const auto stem = std::filesystem::path (path).stem().string();
        if (const auto* a = model->reimagined.get())
            std::cout << stem << ": root " << model->rootMidi << " | tape " << a->tape.reason << ", " << a->tape.splices.size() << " splices, "
                      << a->tape.lengthFrames / std::max (1.0, a->tape.sampleRate) << " s | mosaic " << a->mosaic.reason << ", f0 "
                      << a->mosaic.fundamentalHz << " B " << a->mosaic.inharmonicity << " residual " << a->mosaic.residualShare << " frames "
                      << a->mosaic.frames.size() << " body " << a->mosaic.bodyFrame << " stable " << a->mosaic.stableFrame
                      << (a->mosaic.sustains ? " sustains" : " decays") << "\n";
        if (std::getenv ("OSP_AUDITION_ANALYSIS_ONLY") != nullptr)
            continue;
        for (auto mode : allModes)
            for (double amount : { 0.25, 0.5, 0.75, 1.0 })
            {
                auto s = settingsFor (mode, amount);
                const auto out = play ({ model.get() }, s, phrase, 11.0);
                AudioData audio { 48000.0, { out.l, out.r } };
                std::string name = stem.substr (std::min<std::size_t> (9, stem.size())) + "-" + reimagined::modeName (mode) + "-" + std::to_string (static_cast<int> (amount * 100));
                std::replace (name.begin(), name.end(), ' ', '_');
                std::string error;
                io::writeAudioFile (std::filesystem::path (dir) / (name + ".wav"), audio, io::SampleFormat::float32, error);
                std::cout << name << " peak " << out.peak() << " rms " << out.rms() << "\n";
            }
    }
}

// CPU (hidden): % of one core in real time at 48 kHz, 128-sample blocks, 16 voices.
TEST_CASE ("reimagined modes: CPU", "[.][reimagined-cpu]")
{
    const auto vowel = vowelModel (48000.0, 3.0);
    struct Case
    {
        const char* name;
        std::array<ReimaginedMode, 3> modes;
        int layers;
        bool granular;
    };
    const std::vector<Case> cases {
        { "1 x KALEIDOSCOPE", { ReimaginedMode::kaleidoscope }, 1, false },
        { "1 x TAPE FRAME", { ReimaginedMode::tapeFrame }, 1, false },
        { "1 x TOYBOX", { ReimaginedMode::toybox }, 1, false },
        { "1 x MOSAIC", { ReimaginedMode::mosaic }, 1, false },
        { "1 x MIRAGE", { ReimaginedMode::mirage }, 1, false },
        { "MOSAIC + TAPE + TOYBOX", { ReimaginedMode::mosaic, ReimaginedMode::tapeFrame, ReimaginedMode::toybox }, 3, false },
        { "3 x MOSAIC", { ReimaginedMode::mosaic, ReimaginedMode::mosaic, ReimaginedMode::mosaic }, 3, false },
        { "Granular + MOSAIC", { ReimaginedMode::mosaic }, 1, true },
        { "Granular + TAPE FRAME", { ReimaginedMode::tapeFrame }, 1, true },
        { "Granular + KALEIDOSCOPE", { ReimaginedMode::kaleidoscope }, 1, true },
    };
    {
        const auto& a = *vowel->reimagined;
        std::size_t analysisBytes = sizeof (ReimaginedAnalysis) + a.tape.splices.capacity() * sizeof (ReimaginedAnalysis::TapeSplice)
                                    + a.mosaic.frames.capacity() * sizeof (ReimaginedAnalysis::MosaicFrame);
        std::cout << "memory: voice " << sizeof (InstrumentVoice) << " B (engines inline: tape " << sizeof (TapeFrameEngine) << ", toybox "
                  << sizeof (ToyboxEngine) << ", mosaic " << sizeof (MosaicEngine) << ", mirage " << sizeof (MirageEngine) << "), "
                  << InstrumentEngine::voiceSlots() << " voice slots; analysis per recording " << analysisBytes << " B ("
                  << a.tape.splices.size() << " splices, " << a.mosaic.frames.size() << " frames)\n";
        auto audio = testsignals::vowel (midiToHz (57), 3.0, 48000.0, 3);
        const auto analysis = test::analyse (audio);
        const auto base = instrument::buildComplete (audio, analysis, {}, false);
        const auto t0 = std::chrono::steady_clock::now();
        for (int i = 0; i < 5; ++i)
            (void) analyseReimagined (audio, analysis, base->original.continuation, base->original.source->startFrame());
        std::cout << "analysis time (3 s recording): " << 1000.0 * std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count() / 5.0 << " ms\n";
    }
    for (const auto& c : cases)
    {
        auto s = settingsFor (ReimaginedMode::kaleidoscope, 1.0);
        for (int l = 0; l < 3; ++l)
        {
            s.layer[static_cast<std::size_t> (l)].reimaginedSettings.mode = c.modes[static_cast<std::size_t> (l)];
            if (c.granular)
                s.sourceMode[static_cast<std::size_t> (l)] = SourceMode::granular;
        }
        s.polyphony = 16;
        auto engine = std::make_unique<InstrumentEngine>();
        engine->prepare (48000.0, 128, s);
        for (int l = 0; l < c.layers; ++l)
        {
            engine->setModel (vowel.get(), l);
            engine->setLayerSettings (l, s.layer[static_cast<std::size_t> (l)]);
        }
        for (int n = 0; n < 16; ++n)
            engine->noteOn (45 + 2 * n, 100);
        std::vector<float> l (128), r (128);
        float* ch[2] = { l.data(), r.data() };
        const int blocks = static_cast<int> (4.0 * 48000.0 / 128);
        const auto t0 = std::chrono::steady_clock::now();
        for (int b = 0; b < blocks; ++b)
            engine->render (ch, 2, 128);
        const double seconds = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
        std::cout << c.name << ": " << 100.0 * seconds / 4.0 << " % of one core (" << engine->musicalVoiceCount() << " voices)\n";
    }
}
