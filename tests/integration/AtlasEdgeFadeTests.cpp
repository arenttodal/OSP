// FADE IN / FADE OUT for phrase-derived One Shot notes (docs/research/note-atlas-refinements.md,
// Task C): a fade at each note's own audible start and end, in output time, on the recording
// only; bypassed with LOOP on and in Granular; 0 / 0 is the voice exactly as before.
// Measured on the mixed-duration fixture: abruptly cut saw notes of 20 ms .. 1 s.
#include "engine/AtlasSet.h"
#include "engine/InstrumentEngine.h"
#include "io/AudioFileIO.h"
#include "research/AtlasFixtures.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <tuple>
#include <vector>

using namespace osp;
using namespace osp::research;

namespace
{
    constexpr double rate = 48000.0;

    std::shared_ptr<const InstrumentSet> mixedSet (double fadeInMs, double fadeOutMs)
    {
        auto fixture = atlasfixtures::make ("mixed-duration");
        fixture.atlas.mapping.fadeInMs = fadeInMs;
        fixture.atlas.mapping.fadeOutMs = fadeOutMs;
        auto r = atlasfixtures::build (fixture);
        REQUIRE (r.set != nullptr);
        return r.set;
    }

    struct Settings
    {
        bool loop = false, reverse = false, granular = false;
        double start = 0.0;
    };

    /** Holds `key` for `seconds` from the first sample: left and right outputs. */
    std::pair<std::vector<float>, std::vector<float>> render (const InstrumentSet& set, int key, double seconds, Settings settings = {})
    {
        EngineSettings s;
        s.macros.life = 0.0;
        s.macros.motion = 0.0;
        s.macros.space = 0.0;
        s.macros.reimagined = 0.0;
        s.layer[0].loop = settings.loop;
        s.layer[0].reverse = settings.reverse;
        s.layer[0].start = settings.start;
        s.adsr.attackSeconds = 0.0;
        s.adsr.sustainLevel = 1.0;
        s.adsr.releaseSeconds = 0.05;
        if (settings.granular)
            s.sourceMode[0] = SourceMode::granular;
        InstrumentEngine engine;
        engine.prepare (rate, 64, s);
        engine.setInstrumentSet (&set);
        engine.noteOn (key, 100);
        std::vector<float> l, r, bl (64), br (64);
        for (int done = 0; done < static_cast<int> (seconds * rate); done += 64)
        {
            float* ch[2] = { bl.data(), br.data() };
            engine.render (ch, 2, 64);
            l.insert (l.end(), bl.begin(), bl.end());
            r.insert (r.end(), br.begin(), br.end());
        }
        return { l, r };
    }

    double rms (const std::vector<float>& x, double fromSeconds, double toSeconds)
    {
        const auto a = static_cast<std::size_t> (std::max (0.0, fromSeconds) * rate);
        const auto b = std::min (x.size(), static_cast<std::size_t> (toSeconds * rate));
        double sum = 0.0;
        for (auto i = a; i < b; ++i)
            sum += static_cast<double> (x[i]) * x[i];
        return b > a ? std::sqrt (sum / static_cast<double> (b - a)) : 0.0;
    }

    /** Where the output last rises above a small fraction of its peak (seconds). */
    double lastAudible (const std::vector<float>& x)
    {
        float peak = 0.0f;
        for (const float v : x)
            peak = std::max (peak, std::abs (v));
        for (auto i = x.size(); i-- > 0;)
            if (std::abs (x[i]) > 0.02f * peak)
                return static_cast<double> (i) / rate;
        return 0.0;
    }
} // namespace

TEST_CASE ("note fades: 0 / 0 is the voice exactly as before; LOOP on and Granular bypass any fade", "[integration][atlas][fades]")
{
    const auto plain = mixedSet (0.0, 0.0);
    const auto faded = mixedSet (8.0, 40.0);
    // 0 / 0: bit-identical to a set built without the fields (the same build twice).
    CHECK (render (*plain, 53, 0.5).first == render (*mixedSet (0.0, 0.0), 53, 0.5).first);
    // LOOP on: the fades are kept in the set but not applied.
    Settings loop;
    loop.loop = true;
    CHECK (render (*faded, 55, 1.2, loop).first == render (*plain, 55, 1.2, loop).first);
    // Granular: the grains keep their own envelopes.
    Settings granular;
    granular.granular = true;
    CHECK (render (*faded, 55, 1.2, granular).first == render (*plain, 55, 1.2, granular).first);
    // LOOP off: they apply.
    CHECK (render (*faded, 53, 0.5).first != render (*plain, 53, 0.5).first);
}

TEST_CASE ("note fades: FADE OUT reaches silence at the note's own end, in output time, transposed or not", "[integration][atlas][fades]")
{
    const auto plain = mixedSet (0.0, 0.0);
    const auto faded = mixedSet (0.0, 20.0);
    // F3's note is 0.25 s long: played at its own key it ends 0.25 s after note-on.
    {
        [[maybe_unused]] const auto [a, al] = render (*plain, 53, 0.4);
        [[maybe_unused]] const auto [b, bl] = render (*faded, 53, 0.4);
        // Before the fade: unchanged. Its last 3 ms: almost silent (the fade's end).
        CHECK (rms (b, 0.15, 0.22) == Catch::Approx (rms (a, 0.15, 0.22)).epsilon (0.02));
        CHECK (rms (b, 0.247, 0.2495) < 0.05 * rms (a, 0.247, 0.2495));
        // Halfway through the fade (10 ms before the end): about half.
        const double mid = rms (b, 0.239, 0.241) / std::max (1.0e-9, rms (a, 0.239, 0.241));
        CHECK (mid > 0.3);
        CHECK (mid < 0.7);
        // The fade ended the sound before the recording's end, not after it.
        CHECK (lastAudible (b) < 0.2495);
        CHECK (lastAudible (b) > 0.225);
    }
    // A semitone up (key 54 plays F3's note: FILL's tie goes to the lower key) it lasts about
    // 0.236 s; at SPEED 200 % 0.125 s. The fade is still 20 ms of output.
    {
        [[maybe_unused]] const auto [a, al] = render (*plain, 54, 0.4);
        [[maybe_unused]] const auto [b, bl] = render (*faded, 54, 0.4);
        const double end = 0.25 / std::pow (2.0, 1.0 / 12.0);
        const double mid = rms (b, end - 0.011, end - 0.009) / std::max (1.0e-9, rms (a, end - 0.011, end - 0.009));
        CHECK (mid > 0.3);
        CHECK (mid < 0.7);
        CHECK (lastAudible (b) < end);
    }
    {
        auto fast = [] (double fadeOutMs) {
            auto fixture = atlasfixtures::make ("mixed-duration");
            fixture.atlas.mapping.speed = 2.0;
            fixture.atlas.mapping.fadeOutMs = fadeOutMs;
            auto r = atlasfixtures::build (fixture);
            REQUIRE (r.set != nullptr);
            return r.set;
        };
        [[maybe_unused]] const auto [a, al] = render (*fast (0.0), 53, 0.3);
        [[maybe_unused]] const auto [b, bl] = render (*fast (20.0), 53, 0.3);
        const double end = lastAudible (a);   // (SPEED's own read ends the note about 0.125 s in)
        INFO ("SPEED 200 %: the note ends at " << end << " s");
        CHECK (end < 0.16);
        const double mid = rms (b, end - 0.011, end - 0.009) / std::max (1.0e-9, rms (a, end - 0.011, end - 0.009));
        CHECK (mid > 0.25);
        CHECK (mid < 0.75);
        CHECK (lastAudible (b) < end);
    }
}

TEST_CASE ("note fades: FADE IN rises from silence at the audible start (also under START and REVERSE); both channels alike", "[integration][atlas][fades]")
{
    const auto plain = mixedSet (0.0, 0.0);
    const auto faded = mixedSet (10.0, 0.0);
    const auto [a, ar] = render (*plain, 55, 0.3);
    const auto [b, br] = render (*faded, 55, 0.3);
    CHECK (rms (b, 0.0, 0.001) < 0.05 * rms (a, 0.0, 0.001));
    const double mid = rms (b, 0.0045, 0.0055) / std::max (1.0e-9, rms (a, 0.0045, 0.0055));
    CHECK (mid > 0.3);
    CHECK (mid < 0.7);
    CHECK (rms (b, 0.02, 0.2) == Catch::Approx (rms (a, 0.02, 0.2)).epsilon (0.01));
    // The same gain on both channels.
    for (std::size_t i = 0; i < 480; i += 7)
        if (std::abs (a[i]) > 1.0e-4f && std::abs (ar[i]) > 1.0e-4f)
            CHECK (b[i] / a[i] == Catch::Approx (br[i] / ar[i]).margin (1.0e-4));
    // REVERSE and START: from where the note really starts sounding.
    for (const bool reverse : { true, false })
    {
        Settings s;
        s.reverse = reverse;
        s.start = reverse ? 0.0 : 0.4;
        [[maybe_unused]] const auto [c, cr] = render (*plain, 55, 0.3, s);
        [[maybe_unused]] const auto [d, dr] = render (*faded, 55, 0.3, s);
        INFO ((reverse ? "REVERSE" : "START 40 %"));
        CHECK (rms (d, 0.0, 0.001) < 0.1 * std::max (1.0e-9, rms (c, 0.02, 0.1)));
        CHECK (rms (d, 0.02, 0.1) == Catch::Approx (rms (c, 0.02, 0.1)).epsilon (0.02));
    }
}

TEST_CASE ("note fades: fades longer than a short note are shortened in proportion, so a body is still heard", "[integration][atlas][fades]")
{
    const auto plain = mixedSet (0.0, 0.0);
    // D3's note is 50 ms; 30 + 50 ms of fades are cut to 80 % of it (15 + 25 ms).
    const auto faded = mixedSet (30.0, 50.0);
    [[maybe_unused]] const auto [a, al] = render (*plain, 50, 0.15);
    [[maybe_unused]] const auto [b, bl] = render (*faded, 50, 0.15);
    float peakA = 0.0f, peakB = 0.0f;
    for (std::size_t i = 0; i < a.size(); ++i)
    {
        peakA = std::max (peakA, std::abs (a[i]));
        peakB = std::max (peakB, std::abs (b[i]));
    }
    CHECK (peakB > 0.6f * peakA);                        // the body
    CHECK (rms (b, 0.0, 0.001) < 0.1 * rms (a, 0.0, 0.001));
    CHECK (lastAudible (b) < 0.05);
    // The 20 ms note too (16 ms of fades at most): still audible.
    [[maybe_unused]] const auto [c, cl] = render (*mixedSet (100.0, 500.0), 48, 0.1);
    [[maybe_unused]] const auto [e, el] = render (*plain, 48, 0.1);
    float peakC = 0.0f, peakE = 0.0f;
    for (std::size_t i = 0; i < c.size(); ++i)
    {
        peakC = std::max (peakC, std::abs (c[i]));
        peakE = std::max (peakE, std::abs (e[i]));
    }
    CHECK (peakC > 0.5f * peakE);
}

TEST_CASE ("note fades: listening renders and CPU measurements (OSP_NOTE_ATLAS_RENDERS)", "[.][note-atlas-renders]")
{
    // Test 1 (fast bass, fades A-D), Test 2 (round-robin pizzicato x16, LIFE off and normal),
    // Test 4 (LOOP on vs off with fades set). Synthetic fixtures: no musical judgement claimed.
    const char* dir = std::getenv ("OSP_NOTE_ATLAS_RENDERS");
    REQUIRE (dir != nullptr);
    std::filesystem::create_directories (dir);
    auto write = [&] (const std::string& name, const std::vector<float>& l, const std::vector<float>& r) {
        auto audio = AudioData::allocate (2, static_cast<std::int64_t> (l.size()), rate);
        audio.channels[0] = l;
        audio.channels[1] = r;
        std::string error;
        CHECK (io::writeAudioFile (std::filesystem::path (dir) / name, audio, io::SampleFormat::pcm24, error));
    };
    auto setWith = [] (const char* fixture, double in, double out) {
        auto f = atlasfixtures::make (fixture);
        f.atlas.mapping.fadeInMs = in;
        f.atlas.mapping.fadeOutMs = out;
        auto r = atlasfixtures::build (f);
        REQUIRE (r.set != nullptr);
        return r.set;
    };
    // A sequence of notes on one layer: (key, start seconds, length seconds).
    auto play = [] (const InstrumentSet& set, const std::vector<std::tuple<int, double, double>>& notes, double seconds, bool loop, double life) {
        EngineSettings s;
        s.macros.life = life;
        s.macros.motion = 0.0;
        s.macros.space = 0.0;
        s.macros.reimagined = 0.0;
        s.layer[0].loop = loop;
        InstrumentEngine engine;
        engine.prepare (rate, 64, s);
        engine.setInstrumentSet (&set);
        std::vector<float> l, r, bl (64), br (64);
        const int total = static_cast<int> (seconds * rate);
        for (int done = 0; done < total; done += 64)
        {
            for (const auto& [key, at, len] : notes)
            {
                const int on = static_cast<int> (at * rate), off = static_cast<int> ((at + len) * rate);
                if (on >= done && on < done + 64)
                    engine.noteOn (key, 100);
                if (off >= done && off < done + 64)
                    engine.noteOff (key);
            }
            float* ch[2] = { bl.data(), br.data() };
            engine.render (ch, 2, 64);
            l.insert (l.end(), bl.begin(), bl.end());
            r.insert (r.end(), br.begin(), br.end());
        }
        return std::pair { l, r };
    };
    // Test 1: the fast bass line played as written, 8th notes at 140 BPM.
    std::vector<std::tuple<int, double, double>> line;
    const int keys[] = { 36, 39, 41, 43, 46, 43, 41, 39, 36, 36, 43, 41 };
    for (int i = 0; i < 12; ++i)
        line.push_back ({ keys[i], 0.1 + i * 0.214, 0.2 });
    const std::pair<double, double> fades[] = { { 0, 0 }, { 1, 5 }, { 2, 15 }, { 5, 30 } };
    const char* names[] = { "A-0-0", "B-1-5", "C-2-15", "D-5-30" };
    for (int f = 0; f < 4; ++f)
    {
        const auto [l, r] = play (*setWith ("fast-bass", fades[f].first, fades[f].second), line, 3.0, false, 0.0);
        write (std::string ("test1-fast-bass-") + names[f] + ".wav", l, r);
    }
    // Test 2: the same key 16 times, LOOP off: the recorded takes in turn.
    std::vector<std::tuple<int, double, double>> repeats;
    for (int i = 0; i < 16; ++i)
        repeats.push_back ({ 48, 0.1 + i * 0.3, 0.15 });
    for (const double life : { 0.0, 0.5 })
    {
        const auto [l, r] = play (*setWith ("pizzicato", 0.0, 0.0), repeats, 5.2, false, life);
        write (life == 0.0 ? "test2-pizzicato-rr-life-off.wav" : "test2-pizzicato-rr-life-normal.wav", l, r);
    }
    // Test 4: fades set, LOOP on (bypassed) then off (applied), a held note.
    for (const bool loop : { true, false })
    {
        const auto [l, r] = play (*setWith ("mixed-duration", 20.0, 200.0), { { 55, 0.1, 1.5 } }, 2.0, loop, 0.0);
        write (loop ? "test4-loop-on-fades-bypassed.wav" : "test4-loop-off-fades.wav", l, r);
    }

    // CPU: render time per second of audio, best of three, 8 and 24 voices on the bass set.
    auto measure = [&] (int voices, double in, double out, bool loop) {
        const auto set = setWith ("repeated-bass", in, out);
        double best = 1.0e9;
        for (int run = 0; run < 3; ++run)
        {
            EngineSettings s;
            s.macros.life = 0.3;
            s.layer[0].loop = loop;
            InstrumentEngine engine;
            engine.prepare (rate, 256, s);
            engine.setInstrumentSet (set.get());
            std::vector<float> bl (256), br (256);
            const auto t0 = std::chrono::steady_clock::now();
            for (int b = 0; b < static_cast<int> (rate / 256); ++b)
            {
                if (b % 4 == 0)
                    for (int v = 0; v < voices / 8; ++v)
                        engine.noteOn (30 + (b / 4 + v) % 12, 100);
                float* ch[2] = { bl.data(), br.data() };
                engine.render (ch, 2, 256);
            }
            best = std::min (best, std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - t0).count());
        }
        return best;
    };
    std::ofstream report (std::filesystem::path (dir) / "cpu.txt");
    report << "ms of render per 1 s of 48 kHz audio (best of 3), repeated-bass set, LIFE 30 %\n";
    for (const int v : { 8, 24 })
    {
        report << v << " voices: fades 0/0 " << measure (v, 0, 0, false) << " ms; fades 2/15 " << measure (v, 2, 15, false)
               << " ms; LOOP on (bypassed) " << measure (v, 2, 15, true) << " ms\n";
    }
}
