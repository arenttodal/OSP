// SYNTHESIZE (Phase S1): the oscillator on its own (tuning, aliasing, envelope), the blend laws,
// and the engine with it - off is exactly as before, the played pitch, a plucked note held by
// its oscillator, determinism, block size, no allocation, and the loudness across the amounts.

#include "audio/utility/TestSignals.h"
#include "engine/InstrumentBuilder.h"
#include "engine/InstrumentEngine.h"
#include "core/PitchMath.h"
#include "engine/SynthVoice.h"
#include "support/AllocationCounter.h"
#include "support/TestHelpers.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <complex>
#include <functional>
#include <numbers>
#include <vector>

using namespace osp;
using Catch::Approx;

namespace
{
    constexpr double rate = 48000.0;

    /** The frequency of a periodic signal, from its rising zero crossings (interpolated). */
    double measuredHz (const std::vector<float>& x, std::size_t from)
    {
        double first = -1.0, last = -1.0;
        int crossings = 0;
        for (std::size_t i = from + 1; i < x.size(); ++i)
            if (x[i - 1] < 0.0f && x[i] >= 0.0f)
            {
                const double at = static_cast<double> (i - 1) + x[i - 1] / (x[i - 1] - x[i]);
                if (first < 0.0)
                    first = at;
                else
                    ++crossings;
                last = at;
            }
        return crossings > 0 ? rate * crossings / (last - first) : 0.0;
    }

    double rms (const std::vector<float>& x, double fromSeconds, double toSeconds)
    {
        const auto a = static_cast<std::size_t> (fromSeconds * rate), b = std::min (x.size(), static_cast<std::size_t> (toSeconds * rate));
        double sum = 0.0;
        for (std::size_t i = a; i < b; ++i)
            sum += static_cast<double> (x[i]) * x[i];
        return std::sqrt (sum / static_cast<double> (std::max<std::size_t> (1, b - a)));
    }

    double toneLevel (const std::vector<float>& x, double hz, double fromSeconds, double toSeconds)
    {
        const auto a = static_cast<std::size_t> (fromSeconds * rate), b = std::min (x.size(), static_cast<std::size_t> (toSeconds * rate));
        double s = 0.0, c = 0.0;
        for (std::size_t i = a; i < b; ++i)
        {
            const double w = 2.0 * std::numbers::pi * hz * static_cast<double> (i) / rate;
            s += x[i] * std::sin (w);
            c += x[i] * std::cos (w);
        }
        return 2.0 * std::sqrt (s * s + c * c) / static_cast<double> (std::max<std::size_t> (1, b - a));
    }

    std::vector<float> oscillator (double hz, double seconds, double velocity = 0.8)
    {
        synth::Voice v;
        v.prepare (rate);
        v.start (7, velocity, synth::Envelope::gate);
        v.setPitch (hz);
        std::vector<float> out (static_cast<std::size_t> (seconds * rate));
        for (auto& s : out)
            s = v.next (0.0f);
        return out;
    }

    EngineSettings plainSettings()
    {
        EngineSettings s;
        s.macros.life = 0.0;
        s.macros.space = 0.0;
        s.macros.motion = 0.0;
        s.macros.reimagined = 0.0;
        s.shaping = Shaping::neutral();
        s.adsr.releaseSeconds = 0.1;
        return s;
    }

    std::shared_ptr<InstrumentModel> model (const AudioData& audio)
    {
        return instrument::buildComplete (audio, test::analyse (audio), {}, false);
    }

    /** One held note (`seconds`, released at `releaseAt`), rendered in `block`-sized blocks. */
    std::vector<float> play (const InstrumentModel& m, EngineSettings settings, int note, double seconds, double releaseAt, int block,
                             const std::function<void (InstrumentEngine&)>& setUp = {})
    {
        InstrumentEngine e;
        e.prepare (rate, block, settings);
        e.setModel (&m);
        if (setUp)
            setUp (e);
        std::vector<float> l (static_cast<std::size_t> (block)), r (static_cast<std::size_t> (block)), out;
        e.noteOn (note, 100);
        bool released = false;
        const auto total = static_cast<int> (seconds * rate);
        for (int done = 0; done < total; done += block)
        {
            if (! released && done >= static_cast<int> (releaseAt * rate))
            {
                e.noteOff (note);
                released = true;
            }
            const int n = std::min (block, total - done);
            float* ch[2] = { l.data(), r.data() };
            e.render (ch, 2, n);
            out.insert (out.end(), l.begin(), l.begin() + n);
        }
        return out;
    }
}

TEST_CASE ("synthesize: the blend laws - nothing at 0, the oscillator rising, the sample yielding", "[unit][synth]")
{
    for (auto law : { synth::Blend::hybrid, synth::Blend::equalPower, synth::Blend::additive })
    {
        const auto zero = synth::blendGains (0.0, law);
        CHECK (zero.sample == 1.0f);
        CHECK (zero.synth == 0.0f);
        float last = -1.0f;
        for (int i = 1; i <= 100; ++i)
        {
            const auto g = synth::blendGains (i / 100.0, law);
            CHECK (g.synth >= last);   // the oscillator only ever rises
            last = g.synth;
        }
        CHECK (synth::blendGains (1.0, law).sample == Approx (0.0f).margin (1.0e-6f));   // 100 %: the oscillator alone
    }
    // Hybrid: subtle at first (10 % about -30 dB, 25 % about -18 dB), the sample whole up to a
    // third, the summed power level throughout.
    CHECK (20.0 * std::log10 (synth::blendGains (0.10, synth::Blend::hybrid).synth) == Approx (-30.0).margin (1.0));
    CHECK (20.0 * std::log10 (synth::blendGains (0.25, synth::Blend::hybrid).synth) == Approx (-18.1).margin (1.0));
    CHECK (synth::blendGains (0.25, synth::Blend::hybrid).sample > 0.99f);
    for (int i = 1; i <= 100; ++i)
    {
        const auto g = synth::blendGains (i / 100.0, synth::Blend::hybrid);
        CHECK (g.sample * g.sample + g.synth * g.synth == Approx (1.0f).margin (1.0e-5f));
    }
}

TEST_CASE ("synthesize: the oscillator - in tune, band-limited, click-free in and out", "[unit][synth]")
{
    // Tuning: within a hundredth of a cent across the keyboard.
    for (const double hz : { 55.0, 220.0, 440.0, 1760.0 })
    {
        const auto x = oscillator (hz, 1.0);
        const double cents = 1200.0 * std::log2 (measuredHz (x, static_cast<std::size_t> (0.1 * rate)) / hz);
        CHECK (std::abs (cents) < 0.05);
    }
    // Aliasing at C8 (4186 Hz, the hardest case on the keyboard): what lies between the
    // harmonics stays more than 40 dB under the fundamental.
    {
        const double f0 = 4186.0;
        const auto x = oscillator (f0, 0.6);
        const double fundamental = toneLevel (x, f0, 0.1, 0.6);
        double worst = 0.0;
        for (double f = 300.0; f < 0.5 * rate - 300.0; f += 97.0)
        {
            const double k = f / f0;
            if (std::abs (k - std::round (k)) * f0 < 150.0)
                continue;   // a harmonic (or its window's skirt)
            worst = std::max (worst, toneLevel (x, f, 0.1, 0.6));
        }
        CHECK (20.0 * std::log10 (worst / fundamental) < -40.0);
    }
    // Envelope: from silence, no step at the start; the release falls away and ends.
    synth::Voice v;
    v.prepare (rate);
    v.start (3, 0.8, synth::Envelope::gate);
    v.setPitch (220.0);
    float previous = 0.0f, worstStep = 0.0f;
    for (int i = 0; i < static_cast<int> (0.3 * rate); ++i)
    {
        const float s = v.next (0.0f);
        if (i < 64)
            worstStep = std::max (worstStep, std::abs (s - previous));
        previous = s;
    }
    CHECK (worstStep < 0.01f);   // the first milliseconds rise from 0
    v.release();
    int samples = 0;
    while (! v.isSilent() && samples < static_cast<int> (rate))
    {
        v.next (0.0f);
        ++samples;
    }
    CHECK (v.isSilent());
    CHECK (samples > static_cast<int> (0.12 * rate));   // a release of about 120 .. 250 ms
    CHECK (samples < static_cast<int> (0.30 * rate));
}

TEST_CASE ("synthesize: off is exactly as before; on, it plays the key, with bend and without the root correction", "[unit][synth]")
{
    // A recording at C4 (261.6 Hz), played on A4.
    auto audio = testsignals::sine (midiToHz (60), 2.0, rate, 0.4, 1);
    testsignals::applyFades (audio, 0.01, 0.05);
    const auto m = model (audio);
    auto settings = plainSettings();
    const auto plain = play (*m, settings, 69, 1.0, 0.8, 256);
    settings.layer[0].synthesize = 0.0;
    CHECK (play (*m, settings, 69, 1.0, 0.8, 256) == plain);   // exactly, sample for sample

    settings.layer[0].synthesize = 1.0;
    const auto full = play (*m, settings, 69, 1.0, 0.8, 256);
    CHECK (measuredHz (full, static_cast<std::size_t> (0.2 * rate)) == Approx (440.0).epsilon (0.002));
    CHECK (play (*m, settings, 69, 1.0, 0.8, 256) == full);   // deterministic
    // Bend up two semitones: the oscillator follows.
    const auto bent = play (*m, settings, 69, 1.0, 0.8, 256, [] (InstrumentEngine& e) { e.setPitchOffsetSemitones (2.0); });
    CHECK (measuredHz (bent, static_cast<std::size_t> (0.2 * rate)) == Approx (440.0 * std::exp2 (2.0 / 12.0)).epsilon (0.002));
    // A root correction (a root override) makes the *sample* play the key: never applied twice.
    const auto corrected = play (*m, settings, 69, 1.0, 0.8, 256, [] (InstrumentEngine& e) { e.setLayerPitchOffsetSemitones (0, 3.0); });
    CHECK (measuredHz (corrected, static_cast<std::size_t> (0.2 * rate)) == Approx (440.0).epsilon (0.002));
    // The layer's TUNE is intentional: followed.
    settings.layer[0].tuneSemitones = -12.0;
    const auto tuned = play (*m, settings, 69, 1.0, 0.8, 256);
    CHECK (measuredHz (tuned, static_cast<std::size_t> (0.2 * rate)) == Approx (220.0).epsilon (0.002));
}

TEST_CASE ("synthesize: a plucked note held by its oscillator after the recording ends; released, it ends", "[unit][synth]")
{
    // A 0.3 s pluck (LOOP off: it plays once). Held for 1.5 s.
    auto audio = testsignals::pluck (midiToHz (57), 0.3, rate, 3);
    const auto m = model (audio);
    auto settings = plainSettings();
    settings.layer[0].loop = false;
    auto voices = [&] (double amount, double atSeconds) {
        settings.layer[0].synthesize = amount;
        InstrumentEngine e;
        e.prepare (rate, 256, settings);
        e.setModel (m.get());
        std::vector<float> l (256), r (256);
        e.noteOn (57, 100);
        for (int done = 0; done < static_cast<int> (atSeconds * rate); done += 256)
        {
            float* ch[2] = { l.data(), r.data() };
            e.render (ch, 2, 256);
        }
        return e.activeVoiceCount();
    };
    CHECK (voices (0.0, 1.0) == 0);   // as before: the recording ran out, the voice ended
    CHECK (voices (0.5, 1.0) == 1);   // held by its oscillator
    settings.layer[0].synthesize = 0.5;
    const auto held = play (*m, settings, 57, 2.0, 1.5, 256);
    CHECK (rms (held, 1.0, 1.4) > 0.003);                                 // sounding long after the pluck
    CHECK (toneLevel (held, midiToHz (57), 1.0, 1.4) > 0.005);            // at its pitch
    CHECK (rms (held, 1.85, 2.0) < 1.0e-5);                               // and gone after the release
    // Released and gone: no voice left.
    settings.layer[0].synthesize = 0.5;
    InstrumentEngine e;
    e.prepare (rate, 256, settings);
    e.setModel (m.get());
    std::vector<float> l (256), r (256);
    e.noteOn (57, 100);
    for (int done = 0; done < static_cast<int> (2.0 * rate); done += 256)
    {
        if (done == static_cast<int> (0.5 * rate) / 256 * 256)
            e.noteOff (57);
        float* ch[2] = { l.data(), r.data() };
        e.render (ch, 2, 256);
    }
    CHECK (e.activeVoiceCount() == 0);
}

TEST_CASE ("synthesize: block size, automation while held, chords and no allocation", "[unit][synth]")
{
    // Block size: on a steady sine (where the engine itself is block-size exact) the oscillator
    // adds no dependence of its own.
    auto tone = testsignals::sine (midiToHz (60), 2.0, rate, 0.4, 1);
    testsignals::applyFades (tone, 0.01, 0.05);
    const auto steady = model (tone);
    auto settings = plainSettings();
    settings.layer[0].synthesize = 0.6;
    {
        // (Held throughout: a note-off lands on a block boundary, which differs by block size.)
        const auto fine = play (*steady, settings, 64, 1.0, 2.0, 32);
        const auto coarse = play (*steady, settings, 64, 1.0, 2.0, 512);
        double worst = 0.0;
        for (std::size_t i = 0; i < std::min (fine.size(), coarse.size()); ++i)
            worst = std::max (worst, static_cast<double> (std::abs (fine[i] - coarse[i])));
        CHECK (worst < 1.0e-4);
    }
    auto audio = testsignals::vowel (midiToHz (60), 2.0, rate, 3);
    const auto m = model (audio);

    // Turned from 0 to 100 % and back on a held chord: no step larger than the music's own,
    // and no allocation on the way.
    settings.layer[0].synthesize = 0.0;
    InstrumentEngine e;
    e.prepare (rate, 256, settings);
    e.setModel (m.get());
    std::vector<float> l (256), r (256), out;
    for (int note : { 48, 52, 55, 60, 64, 67, 72, 76 })
        e.noteOn (note, 100);
    {
        float* ch[2] = { l.data(), r.data() };
        e.render (ch, 2, 256);   // (the first block, outside the measurement, as elsewhere)
    }
    out.reserve (400 * 256);
    int allocations = 0;
    {
        test::AllocationScope scope;
        for (int block = 0; block < 400; ++block)
        {
            e.setLayerSynthesize (0, block < 100 ? 0.0 : (block < 200 ? (block - 100) / 100.0 : (block < 300 ? 1.0 : (400 - block) / 100.0)));
            float* ch[2] = { l.data(), r.data() };
            e.render (ch, 2, 256);
            out.insert (out.end(), l.begin(), l.end());
        }
        allocations = scope.count();
    }
    CHECK (allocations == 0);
    float peakStep = 0.0f, peak = 0.0f;
    for (std::size_t i = 1; i < out.size(); ++i)
    {
        peakStep = std::max (peakStep, std::abs (out[i] - out[i - 1]));
        peak = std::max (peak, std::abs (out[i]));
    }
    CHECK (peak < 1.0f);           // eight voices, no clipping
    CHECK (peakStep < 0.25f * peak);
}

TEST_CASE ("synthesize: loudness across the amounts stays within a few dB of the recording", "[unit][synth]")
{
    // A sustained vowel at C4, levelled as every source is (-16 dBFS loudest RMS).
    auto audio = testsignals::vowel (midiToHz (60), 2.0, rate, 3);
    const auto m = model (audio);
    auto settings = plainSettings();
    const double reference = rms (play (*m, settings, 60, 1.5, 1.2, 256), 0.3, 1.1);
    for (const double amount : { 0.1, 0.25, 0.5, 0.75, 1.0 })
    {
        settings.layer[0].synthesize = amount;
        const double level = rms (play (*m, settings, 60, 1.5, 1.2, 256), 0.3, 1.1);
        const double db = 20.0 * std::log10 (level / reference);
        INFO ("SYNTHESIZE " << amount << ": " << db << " dB");
        CHECK (std::abs (db) < 3.0);
    }
}

TEST_CASE ("synthesize: CPU at 1 / 8 / 16 / 32 voices, layers, Granular and KEY DNA", "[.][synthbench]")
{
    // On demand: osp_tests "[synthbench]". The engine alone (no plugin effects), 48 kHz, 256-sample
    // blocks, notes held; % of real time per scenario with SYNTHESIZE 0 and 100 % (median of 5).
    auto audio = testsignals::vowel (midiToHz (60), 3.0, rate, 3);
    const auto m = model (audio);
    struct Scenario { const char* name; int voices, layers; bool granular, keyDna; };
    const Scenario scenarios[] = { { "1 voice", 1, 1, false, false },        { "8 voices", 8, 1, false, false },
                                   { "16 voices", 16, 1, false, false },     { "32 voices", 32, 1, false, false },
                                   { "8 notes x 3 layers", 8, 3, false, false }, { "8 voices Granular", 8, 1, true, false },
                                   { "8 voices Granular + KEY DNA", 8, 1, true, true },
                                   { "8 notes x 3 layers Granular + KEY DNA", 8, 3, true, true } };
    std::printf ("%-40s %12s %12s %10s\n", "scenario", "off (% RT)", "100 % (% RT)", "added");
    for (const auto& sc : scenarios)
    {
        double cost[2] {};
        for (int on = 0; on < 2; ++on)
        {
            std::vector<double> runs;
            for (int run = 0; run < 5; ++run)
            {
                auto settings = plainSettings();
                settings.polyphony = 32;
                InstrumentEngine e;
                for (int l = 0; l < sc.layers; ++l)
                    settings.layer[static_cast<std::size_t> (l)].synthesize = on ? 1.0 : 0.0;
                e.prepare (rate, 256, settings);
                for (int l = 0; l < sc.layers; ++l)
                {
                    e.setModel (m.get(), l);
                    if (sc.granular)
                    {
                        e.setSourceMode (l, SourceMode::granular);
                        GranularParams g;
                        g.density = 30.0;
                        g.sizeSeconds = 0.12;
                        g.spread = 0.3;
                        e.setGranular (l, g);
                    }
                    if (sc.keyDna)
                    {
                        keydna::Settings k;
                        k.enabled = true;
                        k[keydna::Param::position].variation = 0.5;
                        k[keydna::Param::size].variation = 0.4;
                        k[keydna::Param::density].variation = 0.4;
                        k[keydna::Param::density].curveDepth = 0.6;
                        e.setLayerKeyDna (l, k);
                    }
                }
                for (int v = 0; v < sc.voices; ++v)
                    e.noteOn (36 + (v * 7) % 48, 100);
                std::vector<float> l (256), r (256);
                float* ch[2] = { l.data(), r.data() };
                e.render (ch, 2, 256);
                const int blocks = static_cast<int> (2.0 * rate / 256.0);
                const auto t0 = std::chrono::steady_clock::now();
                for (int b = 0; b < blocks; ++b)
                    e.render (ch, 2, 256);
                const double seconds = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
                runs.push_back (100.0 * seconds / (blocks * 256.0 / rate));
            }
            std::sort (runs.begin(), runs.end());
            cost[on] = runs[2];
        }
        std::printf ("%-40s %12.3f %12.3f %+9.1f%%\n", sc.name, cost[0], cost[1], 100.0 * (cost[1] / cost[0] - 1.0));
    }
}
