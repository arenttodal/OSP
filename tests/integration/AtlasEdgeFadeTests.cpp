// FADE IN / FADE OUT for phrase-derived One Shot notes (docs/research/note-atlas-refinements.md,
// Task C): a fade at each note's own audible start and end, in output time, on the recording
// only; bypassed with LOOP on and in Granular; 0 / 0 is the voice exactly as before.
// Measured on the mixed-duration fixture: abruptly cut saw notes of 20 ms .. 1 s.
#include "engine/AtlasSet.h"
#include "engine/InstrumentEngine.h"
#include "research/AtlasFixtures.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
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
