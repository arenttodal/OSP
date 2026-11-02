#include "core/Prng.h"
#include "engine/BloomReverb.h"
#include "engine/SpaceReverb.h"
#include "engine/VintageReverb.h"
#include "support/AllocationCounter.h"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cmath>
#include <functional>
#include <vector>

using namespace osp;

namespace
{
    constexpr double rate = 48000.0;

    template <typename Reverb>
    std::vector<float> render (Reverb& reverb, double seconds, const std::function<float (std::size_t)>& input, std::vector<float>* right = nullptr)
    {
        const auto n = static_cast<std::size_t> (seconds * rate);
        std::vector<float> out (n);
        if (right != nullptr)
            right->assign (n, 0.0f);
        for (std::size_t i = 0; i < n; ++i)
        {
            const float x = input (i);
            float l = 0.0f, r = 0.0f;
            reverb.process (x, x, l, r);
            out[i] = l;
            if (right != nullptr)
                (*right)[i] = r;
        }
        return out;
    }

    float impulseAt0 (std::size_t i) { return i == 0 ? 1.0f : 0.0f; }

    template <typename Reverb, typename Settings>
    std::vector<float> play (const Settings& s, double seconds, const std::function<float (std::size_t)>& input, std::vector<float>* right = nullptr)
    {
        Reverb reverb;
        reverb.prepare (rate);
        reverb.configure (s);
        reverb.reset();
        return render (reverb, seconds, input, right);
    }

    double energy (const std::vector<float>& x, double from, double to)
    {
        double e = 0.0;
        for (auto i = static_cast<std::size_t> (from * rate); i < static_cast<std::size_t> (to * rate) && i < x.size(); ++i)
            e += static_cast<double> (x[i]) * x[i];
        return e;
    }

    double rt60 (const std::vector<float>& x)
    {
        std::vector<double> edc (x.size());
        double acc = 0.0;
        for (std::size_t i = x.size(); i-- > 0;)
        {
            acc += static_cast<double> (x[i]) * x[i];
            edc[i] = acc;
        }
        double t5 = -1.0, t25 = -1.0;
        for (std::size_t i = 0; i < edc.size(); ++i)
        {
            const double db = 10.0 * std::log10 (std::max (1.0e-30, edc[i] / edc[0]));
            if (t5 < 0.0 && db <= -5.0)
                t5 = static_cast<double> (i) / rate;
            if (t25 < 0.0 && db <= -25.0)
            {
                t25 = static_cast<double> (i) / rate;
                break;
            }
        }
        return t25 > t5 && t5 >= 0.0 ? 3.0 * (t25 - t5) : -1.0;
    }

    /** Brightness: the second difference's energy against the signal's (higher: more top). */
    double brightness (const std::vector<float>& x, double from, double to)
    {
        double d = 0.0, e = 0.0;
        for (auto i = static_cast<std::size_t> (from * rate) + 2; i < static_cast<std::size_t> (to * rate) && i < x.size(); ++i)
        {
            const double v = static_cast<double> (x[i]) - 2.0 * x[i - 1] + x[i - 2];
            d += v * v;
            e += static_cast<double> (x[i]) * x[i];
        }
        return d / std::max (1.0e-30, e);
    }

    std::function<float (std::size_t)> noise (std::uint64_t seed, float level, double seconds = 1.0e9)
    {
        return [rng = Prng { seed }, level, last = static_cast<std::size_t> (seconds * rate)] (std::size_t i) mutable {
            const auto v = static_cast<float> (level * rng.bipolar());
            return i < last ? v : 0.0f;
        };
    }

    double hallLevel (double seconds)
    {
        SpaceReverb hall;
        hall.prepare (rate);
        SpaceReverb::Settings h;
        h.type = SpaceType::hall;
        h.decaySeconds = 3.0;
        h.lowCutHz = 20.0;
        h.highCutHz = 20000.0;
        h.damping = 0.0;
        hall.configure (h);
        hall.reset();
        return energy (render (hall, seconds, noise (9, 0.3f)), 2.0, seconds);
    }
}

//==============================================================================
TEST_CASE ("vintage: DECAY sets the reverberation time in every variant", "[unit][vintage]")
{
    for (const auto variant : { VintageVariant::hall, VintageVariant::plate, VintageVariant::chamber, VintageVariant::room })
        for (const double decay : { 1.5, 4.0 })
        {
            VintageReverb::Settings s;
            s.variant = variant;
            s.decaySeconds = decay;
            s.lowCutHz = 20.0;
            s.highCutHz = 20000.0;
            s.damping = 0.0;
            s.preDelayMs = 0.0;
            const double t = rt60 (play<VintageReverb> (s, 3.0 * decay + 2.0, impulseAt0));
            INFO ("variant " << static_cast<int> (variant) << ", DECAY " << decay << " s: RT60 " << t << " s");
            CHECK (t > 0.7 * decay);
            CHECK (t < 1.4 * decay);
        }
}

TEST_CASE ("vintage: the early era has no air, the later one more; the variants differ", "[unit][vintage]")
{
    VintageReverb::Settings s;
    s.lowCutHz = 20.0;
    s.highCutHz = 20000.0;
    s.damping = 0.0;
    s.era = VintageEra::early;
    const double early = brightness (play<VintageReverb> (s, 3.0, noise (2, 0.3f)), 1.0, 3.0);
    s.era = VintageEra::later;
    const double later = brightness (play<VintageReverb> (s, 3.0, noise (2, 0.3f)), 1.0, 3.0);
    INFO ("brightness early " << early << ", later " << later);
    CHECK (early < 0.5 * later);
    s.era = VintageEra::early;
    s.variant = VintageVariant::hall;
    const auto hall = play<VintageReverb> (s, 1.0, impulseAt0);
    s.variant = VintageVariant::room;
    const auto room = play<VintageReverb> (s, 1.0, impulseAt0);
    double diff = 0.0;
    for (std::size_t i = 0; i < hall.size(); ++i)
        diff += std::abs (hall[i] - room[i]);
    CHECK (diff > 1.0);
}

TEST_CASE ("vintage: its short words are heard on a quiet tail (not a scaled copy of a loud one)", "[unit][vintage]")
{
    VintageReverb::Settings s;
    s.preDelayMs = 0.0;
    const auto loud = play<VintageReverb> (s, 2.0, [] (std::size_t i) { return i < 48 ? 0.8f : 0.0f; });
    const auto quiet = play<VintageReverb> (s, 2.0, [] (std::size_t i) { return i < 48 ? 0.0008f : 0.0f; });
    double err = 0.0, ref = 0.0;
    for (std::size_t i = 0; i < loud.size(); ++i)
    {
        const double scaled = 0.001 * loud[i];
        err += (quiet[i] - scaled) * (quiet[i] - scaled);
        ref += scaled * scaled;
    }
    const double db = 10.0 * std::log10 (err / ref);
    INFO ("quiet tail's deviation from the scaled loud one: " << db << " dB");
    CHECK (db > -40.0);   // (a float reverb would be near -140 dB)
}

TEST_CASE ("vintage: stable at the extremes, deterministic, no allocation, as loud as HALL", "[unit][vintage][realtime]")
{
    VintageReverb::Settings s;
    s.decaySeconds = 8.0;
    s.size = 1.0;
    s.diffusion = 1.0;
    s.modulation = 1.0;
    s.era = VintageEra::later;
    float peak = 0.0f;
    bool finite = true;
    for (const float x : play<VintageReverb> (s, 30.0, noise (3, 0.9f)))
    {
        finite = finite && std::isfinite (x);
        peak = std::max (peak, std::abs (x));
    }
    INFO ("peak " << peak);
    CHECK (finite);
    CHECK (peak < 6.0f);

    VintageReverb::Settings d;
    CHECK (play<VintageReverb> (d, 2.0, noise (4, 0.3f)) == play<VintageReverb> (d, 2.0, noise (4, 0.3f)));

    VintageReverb reverb;
    reverb.prepare (rate);
    reverb.configure (d);
    reverb.reset();
    auto in = noise (5, 0.3f);
    {
        osp::test::AllocationScope scope;
        float l = 0.0f, r = 0.0f;
        for (std::size_t i = 0; i < static_cast<std::size_t> (2.0 * rate); ++i)
        {
            const float x = in (i);
            reverb.process (x, x, l, r);
            if (i == 48000)
            {
                auto t = reverb.settings();
                t.decaySeconds = 5.0;
                t.diffusion = 0.2;
                reverb.tune (t);
            }
        }
        CHECK (scope.count() == 0);
    }

    VintageReverb::Settings level;
    level.decaySeconds = 3.0;
    level.lowCutHz = 20.0;
    level.highCutHz = 20000.0;
    level.damping = 0.0;
    level.era = VintageEra::later;
    const double db = 10.0 * std::log10 (energy (play<VintageReverb> (level, 6.0, noise (9, 0.3f)), 2.0, 6.0) / hallLevel (6.0));
    INFO ("VINTAGE against HALL: " << db << " dB");
    CHECK (std::abs (db) < 3.0);
}

//==============================================================================
TEST_CASE ("bloom: the repeats land on the tempo's beat, and follow the tempo", "[unit][bloom]")
{
    BloomReverb::Settings s;
    s.diffusion = 0.0;
    s.modulation = 0.0;
    s.damping = 0.0;
    s.lowCutHz = 20.0;
    s.highCutHz = 20000.0;
    s.preDelayMs = 0.0;
    s.division = 6;   // 1/4
    for (const double bpm : { 120.0, 60.0 })
    {
        BloomReverb reverb;
        reverb.prepare (rate);
        reverb.configure (s);
        reverb.setTempo (bpm);
        reverb.reset();
        const auto out = render (reverb, 1.5 * 60.0 / bpm + 0.3, impulseAt0);
        std::size_t at = 0;
        for (std::size_t i = 1; i < out.size(); ++i)
            if (std::abs (out[i]) > std::abs (out[at]))
                at = i;
        const double seconds = static_cast<double> (at) / rate, expected = 60.0 / bpm;
        INFO ("tempo " << bpm << ": first repeat at " << seconds << " s (expected " << expected << ")");
        CHECK (std::abs (seconds - expected) < 0.004);
    }
}

TEST_CASE ("bloom: DIFFUSION smears the repeats into a wash; DECAY sets how long it lasts", "[unit][bloom]")
{
    auto crest = [] (const std::vector<float>& x, double from, double to) {
        float peak = 0.0f;
        for (auto i = static_cast<std::size_t> (from * rate); i < static_cast<std::size_t> (to * rate); ++i)
            peak = std::max (peak, std::abs (x[i]));
        const double rms = std::sqrt (energy (x, from, to) / ((to - from) * rate));
        return static_cast<double> (peak) / std::max (1.0e-12, rms);
    };
    BloomReverb::Settings s;
    s.division = 3;   // 1/8
    s.diffusion = 0.0;
    const double clean = crest (play<BloomReverb> (s, 3.0, impulseAt0), 0.2, 3.0);
    s.diffusion = 1.0;
    const double washed = crest (play<BloomReverb> (s, 3.0, impulseAt0), 0.2, 3.0);
    s.variant = BloomVariant::wash;
    const double wash = crest (play<BloomReverb> (s, 3.0, impulseAt0), 0.2, 3.0);
    INFO ("crest factor: clean " << clean << ", diffused " << washed << ", WASH " << wash);
    CHECK (washed < 0.5 * clean);
    CHECK (wash <= washed * 1.1);

    BloomReverb::Settings d;
    d.diffusion = 0.8;
    d.decaySeconds = 1.5;
    const auto shortTail = play<BloomReverb> (d, 6.0, noise (6, 0.3f, 1.0));
    d.decaySeconds = 6.0;
    const auto longTail = play<BloomReverb> (d, 6.0, noise (6, 0.3f, 1.0));
    const double fallShort = 10.0 * std::log10 (energy (shortTail, 4.0, 5.0) / energy (shortTail, 1.0, 2.0));
    const double fallLong = 10.0 * std::log10 (energy (longTail, 4.0, 5.0) / energy (longTail, 1.0, 2.0));
    INFO ("fall 1..2 s -> 4..5 s: DECAY 1.5 s " << fallShort << " dB, 6 s " << fallLong << " dB");
    CHECK (fallShort < -45.0);   // (60 dB per 1.5 s: far down in 3 s)
    CHECK (fallLong > -40.0);    // (60 dB per 6 s: about 30 dB in 3 s)
    CHECK (fallLong < -20.0);
}

TEST_CASE ("bloom: every variant is stable and distinct, deterministic, no allocation, about as loud as HALL", "[unit][bloom][realtime]")
{
    std::vector<std::vector<float>> outs;
    for (const auto variant : { BloomVariant::clean, BloomVariant::vintage, BloomVariant::tape, BloomVariant::wash })
    {
        BloomReverb::Settings s;
        s.variant = variant;
        s.decaySeconds = 8.0;
        s.diffusion = 1.0;
        s.modulation = 1.0;
        s.division = 0;   // 1/16: the most passes through the loop
        float peak = 0.0f;
        bool finite = true;
        const auto out = play<BloomReverb> (s, 20.0, noise (7, 0.9f));
        for (const float x : out)
        {
            finite = finite && std::isfinite (x);
            peak = std::max (peak, std::abs (x));
        }
        INFO ("variant " << static_cast<int> (variant) << ": peak " << peak);
        CHECK (finite);
        CHECK (peak < 4.0f);
        BloomReverb::Settings v;
        v.variant = variant;
        outs.push_back (play<BloomReverb> (v, 2.0, noise (8, 0.3f, 0.5)));
    }
    for (std::size_t a = 0; a < outs.size(); ++a)
        for (std::size_t b = a + 1; b < outs.size(); ++b)
            CHECK (outs[a] != outs[b]);

    BloomReverb::Settings d;
    d.variant = BloomVariant::tape;
    CHECK (play<BloomReverb> (d, 2.0, noise (4, 0.3f)) == play<BloomReverb> (d, 2.0, noise (4, 0.3f)));

    BloomReverb reverb;
    reverb.prepare (rate);
    reverb.configure (d);
    reverb.reset();
    auto in = noise (5, 0.3f);
    {
        osp::test::AllocationScope scope;
        float l = 0.0f, r = 0.0f;
        for (std::size_t i = 0; i < static_cast<std::size_t> (2.0 * rate); ++i)
        {
            const float x = in (i);
            reverb.process (x, x, l, r);
            if (i == 48000)
            {
                auto t = reverb.settings();
                t.sync = false;
                t.timeMs = 610.0;
                t.variant = BloomVariant::wash;
                reverb.tune (t);
                reverb.setTempo (97.0);
            }
        }
        CHECK (scope.count() == 0);
    }

    BloomReverb::Settings level;
    level.variant = BloomVariant::wash;
    level.decaySeconds = 3.0;
    level.lowCutHz = 20.0;
    level.highCutHz = 20000.0;
    level.damping = 0.0;
    const double db = 10.0 * std::log10 (energy (play<BloomReverb> (level, 6.0, noise (9, 0.3f)), 2.0, 6.0) / hallLevel (6.0));
    INFO ("BLOOM (WASH) against HALL: " << db << " dB");
    CHECK (std::abs (db) < 3.0);
}

TEST_CASE ("vintage and bloom: what they cost against HALL", "[unit][vintage][bloom]")
{
    auto cost = [] (auto& reverb) {
        auto in = noise (5, 0.3f);
        float l = 0.0f, r = 0.0f, sink = 0.0f;
        const auto t0 = std::chrono::steady_clock::now();
        for (std::size_t i = 0; i < static_cast<std::size_t> (5.0 * rate); ++i)
        {
            const float x = in (i);
            reverb.process (x, x, l, r);
            sink += l;
        }
        CHECK (std::isfinite (sink));
        return std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - t0).count();
    };
    SpaceReverb hall;
    hall.prepare (rate);
    SpaceReverb::Settings h;
    h.type = SpaceType::hall;
    hall.configure (h);
    hall.reset();
    VintageReverb vintage;
    vintage.prepare (rate);
    VintageReverb::Settings v;
    v.era = VintageEra::later;
    vintage.configure (v);
    vintage.reset();
    BloomReverb bloom;
    bloom.prepare (rate);
    BloomReverb::Settings b;
    b.variant = BloomVariant::wash;
    bloom.configure (b);
    bloom.reset();
    const double hallMs = cost (hall), vintageMs = cost (vintage), bloomMs = cost (bloom);
    WARN ("CPU, 5 s at 48 kHz: HALL " << hallMs << " ms, VINTAGE (later) " << vintageMs << " ms (x" << vintageMs / hallMs << "), BLOOM (wash) "
                                       << bloomMs << " ms (x" << bloomMs / hallMs << ")");
}
