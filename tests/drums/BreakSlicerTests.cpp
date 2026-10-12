// ANDOR Drums M6: the break slicer against a synthetic test collection (spec 7.14). Every
// break is generated (no third-party audio), so its true onsets are known exactly.
#include "drums/BreakSlicer.h"

#include "core/Prng.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

using namespace osp;
using namespace osp::drums;
using Catch::Approx;

namespace
{
    constexpr double rate = 44100.0;
    constexpr double pi = 3.14159265358979323846;

    enum class Hit
    {
        kick, snare, hatClosed, hatOpen, clap, crash, tom
    };

    struct Event
    {
        double time;
        Hit hit;
        double gain = 1.0;
    };

    struct Break
    {
        std::string name;
        double seconds = 4.0;
        std::vector<Event> events;
        double noiseDb = -200.0;   ///< a constant noise floor
        bool lofi = false;         ///< 8-bit, dull, noisy
        double overallGain = 1.0;
        double room = 0.0;         ///< a short diffuse tail on everything (acoustic)
        /** The F-measure required. Hats in the first second under a fresh crash are masked
            (the crash's own wash is as loud): known limitation, see docs/drums/m6-breaks.md. */
        double minF = 0.9;
    };

    void add (std::vector<float>& x, const Event& e, Prng& rng)
    {
        const auto at = static_cast<std::size_t> (std::lround (e.time * rate));
        // Every instrument fades out over its last 20 ms (no click where the synthesis stops).
        std::size_t length = 0;
        auto put = [&] (std::size_t i, double v) {
            const double fade = std::min (1.0, static_cast<double> (length - std::min (length, i)) / (0.02 * rate));
            if (at + i < x.size())
                x[at + i] += static_cast<float> (v * e.gain * fade);
        };
        auto noise = [&rng] { return rng.uniform (-1.0, 1.0); };
        switch (e.hit)
        {
            case Hit::kick:
            {
                double phase = 0.0;
                length = static_cast<std::size_t> (0.35 * rate);
                for (std::size_t i = 0; i < length; ++i)
                {
                    const double t = static_cast<double> (i) / rate;
                    phase += 2.0 * pi * (50.0 + 90.0 * std::exp (-t / 0.03)) / rate;
                    put (i, 0.9 * std::sin (phase) * std::exp (-t / 0.12) + (t < 0.002 ? 0.3 * noise() : 0.0));
                }
                break;
            }
            case Hit::tom:
                length = static_cast<std::size_t> (0.3 * rate);
                for (std::size_t i = 0; i < length; ++i)
                {
                    const double t = static_cast<double> (i) / rate;
                    put (i, 0.6 * std::sin (2.0 * pi * 140.0 * t) * std::exp (-t / 0.1));
                }
                break;
            case Hit::snare:
            {
                double prev = 0.0;
                length = static_cast<std::size_t> (0.25 * rate);
                for (std::size_t i = 0; i < length; ++i)
                {
                    const double t = static_cast<double> (i) / rate;
                    const double n = noise();
                    put (i, 0.5 * (n - 0.6 * prev) * std::exp (-t / 0.07) + 0.4 * std::sin (2.0 * pi * 190.0 * t) * std::exp (-t / 0.05));
                    prev = n;
                }
                break;
            }
            case Hit::clap:
                length = static_cast<std::size_t> (0.1 * rate);
                for (int burst = 0; burst < 3; ++burst)
                    for (std::size_t i = 0; i < static_cast<std::size_t> (0.06 * rate); ++i)
                    {
                        const double t = static_cast<double> (i) / rate;
                        put (i + static_cast<std::size_t> (burst * 0.011 * rate), 0.35 * noise() * std::exp (-t / (burst == 2 ? 0.03 : 0.006)));
                    }
                break;
            case Hit::hatClosed:
            case Hit::hatOpen:
            case Hit::crash:
            {
                const double decay = e.hit == Hit::hatClosed ? 0.025 : e.hit == Hit::hatOpen ? 0.2 : 1.6;
                length = static_cast<std::size_t> ((e.hit == Hit::crash ? 3.0 : 6.0 * decay) * rate);
                double prev = 0.0;
                for (std::size_t i = 0; i < length; ++i)
                {
                    const double t = static_cast<double> (i) / rate;
                    const double n = noise();
                    put (i, (e.hit == Hit::crash ? 0.25 : 0.3) * (n - prev) * std::exp (-t / decay));   // first difference: bright
                    prev = n;
                }
                break;
            }
        }
    }

    AudioData render (const Break& b, std::uint64_t seed = 1)
    {
        AudioData a;
        a.sampleRate = rate;
        double seconds = b.seconds;
        for (const auto& e : b.events)
            seconds = std::max (seconds, e.time + 0.6);
        std::vector<float> x (static_cast<std::size_t> (seconds * rate), 0.0f);
        Prng rng (seed);
        for (const auto& e : b.events)
            add (x, e, rng);
        if (b.room > 0.0)
        {
            // A short diffuse tail (feedback comb), like a small room.
            const auto d = static_cast<std::size_t> (0.023 * rate);
            for (std::size_t i = d; i < x.size(); ++i)
                x[i] += static_cast<float> (b.room * x[i - d]);
        }
        const double noiseGain = std::pow (10.0, b.noiseDb / 20.0);
        double lp = 0.0;
        for (auto& v : x)
        {
            double s = v * b.overallGain + noiseGain * rng.uniform (-1.0, 1.0);
            if (b.lofi)
            {
                lp += 0.35 * (s - lp);   // dull
                s = std::round (lp * 127.0) / 127.0;
            }
            v = static_cast<float> (s);
        }
        a.channels.push_back (std::move (x));
        return a;
    }

    /** The true onsets: events closer than 30 ms are one hit (played together). */
    std::vector<double> truth (const Break& b)
    {
        std::vector<double> t;
        for (const auto& e : b.events)
            t.push_back (e.time);
        std::sort (t.begin(), t.end());
        std::vector<double> merged;
        for (double v : t)
            if (merged.empty() || v - merged.back() > 0.03)
                merged.push_back (v);
        return merged;
    }

    struct Score
    {
        int hits = 0, misses = 0, extras = 0;
        double worstLateMs = 0.0, worstEarlyMs = 0.0;
        double f() const { return hits == 0 ? 0.0 : 2.0 * hits / (2.0 * hits + misses + extras); }
    };

    Score score (const Break& b, const std::vector<BreakSlice>& slices)
    {
        Score s;
        const auto want = truth (b);
        std::vector<bool> used (slices.size(), false);
        for (double t : want)
        {
            int best = -1;
            double bestErr = 0.02;   // 20 ms tolerance
            for (std::size_t i = 0; i < slices.size(); ++i)
            {
                const double err = std::abs (static_cast<double> (slices[i].onset) / rate - t);
                if (! used[i] && err < bestErr)
                {
                    best = static_cast<int> (i);
                    bestErr = err;
                }
            }
            if (best < 0)
            {
                ++s.misses;
                continue;
            }
            used[static_cast<std::size_t> (best)] = true;
            ++s.hits;
            // The slice must start at or before the transient (never cut its beginning).
            const double startMs = (static_cast<double> (slices[static_cast<std::size_t> (best)].start) / rate - t) * 1000.0;
            s.worstLateMs = std::max (s.worstLateMs, startMs);
            s.worstEarlyMs = std::min (s.worstEarlyMs, startMs);
        }
        for (bool u : used)
            s.extras += u ? 0 : 1;
        return s;
    }

    std::vector<Event> pattern (double bpm, int bars, const std::string& kick, const std::string& snare, const std::string& hats, double swing = 0.5,
                                double ghostGain = 0.0)
    {
        // 16 steps per bar; 'x' = hit, 'g' = ghost (snare), 'o' = open hat.
        std::vector<Event> e;
        const double step = 60.0 / bpm / 4.0;
        for (int bar = 0; bar < bars; ++bar)
            for (int i = 0; i < 16; ++i)
            {
                double t = 0.05 + (bar * 16 + i) * step;
                if (i % 2 == 1)
                    t += (swing - 0.5) * 2.0 * step;   // swung off-beats
                const auto k = static_cast<std::size_t> (i);
                if (kick[k] == 'x')
                    e.push_back ({ t, Hit::kick });
                if (snare[k] == 'x')
                    e.push_back ({ t, Hit::snare });
                if (snare[k] == 'g')
                    e.push_back ({ t, Hit::snare, ghostGain });
                if (hats[k] == 'x')
                    e.push_back ({ t, Hit::hatClosed, 0.6 });
                if (hats[k] == 'o')
                    e.push_back ({ t, Hit::hatOpen, 0.6 });
            }
        return e;
    }

    std::vector<Break> collection()
    {
        std::vector<Break> c;
        {
            Break b;
            b.name = "clean electronic";
            b.events = pattern (120.0, 2, "x.......x.......", "....x.......x...", "x.x.x.x.x.x.x.x.");
            c.push_back (b);
        }
        {
            Break b;
            b.name = "acoustic (room, noise floor)";
            b.events = pattern (96.0, 2, "x......x..x.....", "....x.......x...", "x.x.x.x.x.x.x.xo");
            b.noiseDb = -54.0;
            b.room = 0.25;
            c.push_back (b);
        }
        {
            Break b;
            b.name = "funk 16ths";
            b.events = pattern (100.0, 2, "x..x......x..x..", "....x..x.x..x...", "xxxxxxxxxxxxxxxx");
            c.push_back (b);
        }
        {
            Break b;
            b.name = "lo-fi (8-bit, dull, noisy)";
            b.events = pattern (90.0, 2, "x.....x...x.....", "....x.......x...", "x.x.x.x.x.x.x.x.");
            b.lofi = true;
            b.noiseDb = -42.0;
            c.push_back (b);
        }
        {
            Break b;
            b.name = "ghost notes";
            b.events = pattern (95.0, 2, "x.........x.....", "..g.x.g..g..x.g.", "x...x...x...x...", 0.5, 0.12);
            c.push_back (b);
        }
        {
            Break b;
            b.name = "kick and hat together";
            b.events = pattern (110.0, 2, "x...x...x...x...", "....x.......x...", "x.x.x.x.x.x.x.x.");
            c.push_back (b);
        }
        {
            Break b;
            b.name = "fast hats (32nds)";
            b.seconds = 3.0;
            for (int i = 0; i < 32; ++i)
                b.events.push_back ({ 0.05 + i * 60.0 / 110.0 / 8.0, Hit::hatClosed, i % 2 == 0 ? 0.7 : 0.4 });
            b.events.push_back ({ 0.05, Hit::kick });
            b.events.push_back ({ 0.05 + 60.0 / 110.0, Hit::snare });
            c.push_back (b);
        }
        {
            Break b;
            b.name = "swing";
            b.events = pattern (92.0, 2, "x.....x...x.....", "....x.......x...", "x.x.x.x.x.x.x.x.", 0.66);
            c.push_back (b);
        }
        {
            Break b;
            b.name = "quiet transients (-30 dB)";
            b.events = pattern (120.0, 2, "x.......x.......", "....x.......x...", "x.x.x.x.x.x.x.x.");
            b.overallGain = 0.03;
            b.noiseDb = -80.0;
            c.push_back (b);
        }
        {
            Break b;
            b.name = "long cymbal tails";
            b.events = pattern (100.0, 2, "x.......x.x.....", "....x.......x...", "..x...x...x...x.");
            b.events.push_back ({ 0.05, Hit::crash });
            b.events.push_back ({ 0.05 + 2.4, Hit::crash });
            b.minF = 0.85;
            c.push_back (b);
        }
        return c;
    }
} // namespace

TEST_CASE ("Break slicer: the test collection (spec 7.14), onsets found and never cut", "[drums][breaks]")
{
    std::printf ("\n%-30s %5s %5s %5s %6s %10s\n", "break", "hits", "miss", "extra", "F", "start ms");
    double worstF = 1.0;
    for (const auto& b : collection())
    {
        const auto audio = render (b);
        const auto slices = sliceBreak (audio);
        const auto s = score (b, slices);
        std::printf ("%-30s %5d %5d %5d %6.3f %4.1f..%4.1f\n", b.name.c_str(), s.hits, s.misses, s.extras, s.f(), s.worstEarlyMs, s.worstLateMs);
        worstF = std::min (worstF, s.f());
        if (std::getenv ("SLICER_DEBUG") != nullptr && s.f() < 0.95)
        {
            std::printf ("  truth:");
            for (double t : truth (b))
                std::printf (" %.3f", t);
            std::printf ("\n  found:");
            for (const auto& sl : slices)
                std::printf (" %.3f(%.2f)", static_cast<double> (sl.onset) / rate, sl.strength);
            std::printf ("\n");
        }
        INFO (b.name);
        CHECK (s.f() >= b.minF);
        CHECK (s.worstLateMs <= 1.0);     // never after the attack begins (pre-roll 2 ms)
        CHECK (s.worstEarlyMs >= -15.0);  // and not far before it
    }
    std::printf ("worst F %.3f\n", worstF);
}

TEST_CASE ("Break slicer: slices are contiguous regions of the original, in order, deterministic", "[drums][breaks]")
{
    const auto b = collection().front();
    const auto audio = render (b);
    const auto slices = sliceBreak (audio);
    REQUIRE (slices.size() >= 8);
    for (std::size_t i = 0; i < slices.size(); ++i)
    {
        CHECK (slices[i].start < slices[i].end);
        CHECK (slices[i].start <= slices[i].onset);
        if (i + 1 < slices.size())
            CHECK (slices[i].end == slices[i + 1].start);   // the groove's own timing
        CHECK (slices[i].strength > 0.0);
        CHECK (slices[i].pad == -1);
    }
    CHECK (sliceBreak (audio) == slices);
    // The kicks are the loudest hits; the hats softer.
    double kickStrength = 0.0;
    for (const auto& s : slices)
        if (std::abs (static_cast<double> (s.onset) / rate - 0.05) < 0.02)
            kickStrength = s.strength;
    CHECK (kickStrength == Approx (1.0).margin (0.15));
}

TEST_CASE ("Break slicer: sensitivity and minimum separation", "[drums][breaks]")
{
    Break b;
            b.name = "ghosts";
    b.events = pattern (95.0, 2, "x.........x.....", "..g.x.g..g..x.g.", "x...x...x...x...", 0.5, 0.06);
    const auto audio = render (b);
    SliceSettings low, high;
    low.sensitivity = 0.0;
    high.sensitivity = 1.0;
    CHECK (sliceBreak (audio, low).size() < sliceBreak (audio, high).size());   // the ghosts only when sensitive
    SliceSettings wide;
    wide.minSeparationSeconds = 0.3;
    for (const auto& s : { sliceBreak (audio, wide) })
        for (std::size_t i = 1; i < s.size(); ++i)
            CHECK (static_cast<double> (s[i].onset - s[i - 1].onset) / rate >= 0.3 * 0.5);
}

TEST_CASE ("Break slicer: manual add, delete and move keep the slices contiguous", "[drums][breaks]")
{
    const auto b = collection().front();
    const auto audio = render (b);
    auto slices = sliceBreak (audio);
    const auto n = slices.size();
    // Add a boundary in the middle of the first slice (no attack there: exactly where clicked).
    const auto mid = (slices[0].start + slices[0].end) / 2;
    const int added = addSliceAt (slices, audio, mid);
    REQUIRE (added == 1);
    CHECK (slices.size() == n + 1);
    CHECK (slices[0].end == slices[1].start);
    CHECK (slices[1].manual);
    CHECK (addSliceAt (slices, audio, slices[1].start + 10) == -1);   // too close to a boundary
    // A click 8 ms after a real attack snaps back to it.
    auto again = sliceBreak (audio);
    const auto onset2 = again[2].onset;
    REQUIRE (removeSlice (again, 2));
    const int snapped = addSliceAt (again, audio, onset2 + static_cast<std::int64_t> (0.008 * rate));
    REQUIRE (snapped == 2);
    CHECK (std::abs (static_cast<double> (again[2].onset - onset2)) / rate < 0.003);
    // Delete: the previous slice takes over.
    const auto end2 = again[2].end;
    REQUIRE (removeSlice (again, 2));
    CHECK (again[1].end == end2);
    // Move a start: the previous slice's end follows, neighbours limit it.
    REQUIRE (moveSliceStart (again, 2, again[2].start - 500, audio.numFrames()));
    CHECK (again[1].end == again[2].start);
    REQUIRE (moveSliceStart (again, 2, 0, audio.numFrames()));
    CHECK (again[2].start > again[1].start);
}

TEST_CASE ("Break slicer: tempo estimate", "[drums][breaks]")
{
    for (double bpm : { 90.0, 120.0, 100.0 })
    {
        Break b;
            b.name = "tempo";
        b.events = pattern (bpm, 2, "x.......x.x.....", "....x.......x...", "x.x.x.x.x.x.x.x.");
        b.seconds = 0.05 + 2.0 * 4.0 * 60.0 / bpm + 0.3;
        const auto audio = render (b);
        INFO (bpm);
        CHECK (estimateTempo (sliceBreak (audio), rate) == Approx (bpm).margin (1.0));
    }
}

TEST_CASE ("Break slicer: silence, noise and tiny files never crash", "[drums][breaks]")
{
    AudioData empty;
    empty.sampleRate = rate;
    CHECK (sliceBreak (empty).empty());
    AudioData silence;
    silence.sampleRate = rate;
    silence.channels.push_back (std::vector<float> (static_cast<std::size_t> (rate), 0.0f));
    CHECK (sliceBreak (silence).empty());
    AudioData tiny;
    tiny.sampleRate = rate;
    tiny.channels.push_back (std::vector<float> (10, 0.5f));
    CHECK_NOTHROW (sliceBreak (tiny));
    AudioData noise;
    noise.sampleRate = rate;
    Prng rng (3);
    noise.channels.push_back (std::vector<float> (static_cast<std::size_t> (rate * 2.0)));
    for (auto& v : noise.channels[0])
        v = static_cast<float> (rng.uniform (-0.3, 0.3));
    CHECK (sliceBreak (noise).size() < 6);   // steady noise is not a groove
}
