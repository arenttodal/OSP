// ANDOR Drums M7: classification (names, sounds, both), similarity grouping, auto-mapping.
// The sounds are synthesised with seeded variations (no third-party samples).
#include "drums/DrumClassifier.h"

#include "core/Prng.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdio>
#include <map>
#include <set>

using namespace osp;
using namespace osp::drums;
using Catch::Approx;

namespace
{
    constexpr double rate = 44100.0;
    constexpr double pi = 3.14159265358979323846;

    AudioData make (DrumCategory c, std::uint64_t seed, int variant = 0)
    {
        Prng rng (seed * 7919 + static_cast<std::uint64_t> (c) * 104729 + static_cast<std::uint64_t> (variant));
        auto r = [&rng] (double lo, double hi) { return rng.uniform (lo, hi); };
        double seconds = 0.5;
        switch (c)
        {
            case DrumCategory::cymbal: seconds = 3.0; break;
            case DrumCategory::openHat: seconds = 1.0; break;
            case DrumCategory::kick: seconds = variant == 1 ? 1.6 : 0.6; break;
            default: break;
        }
        AudioData a;
        a.sampleRate = rate;
        std::vector<float> x (static_cast<std::size_t> (seconds * rate), 0.0f);
        double phase = 0.0, prev = 0.0, prev2 = 0.0, lp1 = 0.0, lp2 = 0.0, hp = 0.0;
        const double gain = r (0.3, 0.9);
        // Per-sound parameters
        const double kickTop = r (110.0, 180.0), kickBottom = variant == 1 ? r (40.0, 50.0) : r (45.0, 65.0), kickDecay = variant == 1 ? r (0.5, 0.9) : r (0.1, 0.3);
        const double snareTone = r (170.0, 240.0), snareDecay = r (0.06, 0.15);
        const double tomPitch = r (110.0, 260.0), tomDecay = r (0.15, 0.35);
        const double rimPitch = r (500.0, 1400.0);
        const double hatDecay = c == DrumCategory::closedHat ? r (0.015, 0.045) : c == DrumCategory::openHat ? r (0.2, 0.45) : r (0.9, 1.8);
        const double clapGap = r (0.007, 0.012);
        const int clapBursts = 3 + static_cast<int> (rng.nextBelow (2));
        const double percPitch = r (250.0, 450.0);
        for (std::size_t i = 0; i < x.size(); ++i)
        {
            const double t = static_cast<double> (i) / rate;
            const double n = r (-1.0, 1.0);
            // Band-limited noise for snares (wide) and claps (around 1-2 kHz).
            lp1 += (c == DrumCategory::clap ? 0.25 : 0.6) * (n - lp1);
            lp2 += (c == DrumCategory::clap ? 0.25 : 0.6) * (lp1 - lp2);
            hp = lp2 - (c == DrumCategory::clap ? 0.92 : 0.8) * hp;
            const double band = lp2 - hp * 0.0;
            double v = 0.0;
            switch (c)
            {
                case DrumCategory::kick:
                    phase += 2.0 * pi * (kickBottom + (kickTop - kickBottom) * std::exp (-t / 0.03)) / rate;
                    v = std::sin (phase) * std::exp (-t / kickDecay) + (t < 0.002 ? 0.2 * n : 0.0);
                    break;
                case DrumCategory::snare:
                    v = 1.2 * (band - 0.7 * prev) * std::exp (-t / snareDecay) + 0.5 * std::sin (2.0 * pi * snareTone * t) * std::exp (-t / 0.05);
                    break;
                case DrumCategory::clap:
                    for (int b = 0; b < clapBursts; ++b)
                    {
                        const double tb = t - b * clapGap;
                        if (tb >= 0.0)
                            v += 2.0 * (band - 0.85 * prev) * std::exp (-tb / (b == clapBursts - 1 ? 0.06 : 0.004)) * 0.5;
                    }
                    break;
                case DrumCategory::closedHat:
                case DrumCategory::openHat:
                case DrumCategory::cymbal:
                    // Second difference of noise: bright, little low end.
                    v = 0.5 * (n - 2.0 * prev + prev2) * std::exp (-t / hatDecay);
                    break;
                case DrumCategory::tom:
                    phase += 2.0 * pi * tomPitch * (1.0 + 0.3 * std::exp (-t / 0.05)) / rate;
                    v = std::sin (phase) * std::exp (-t / tomDecay) + (t < 0.003 ? 0.15 * n : 0.0);
                    break;
                case DrumCategory::rim:
                    v = (std::sin (2.0 * pi * rimPitch * t) + 0.4 * std::sin (2.0 * pi * rimPitch * 2.7 * t)) * std::exp (-t / 0.012) + (t < 0.001 ? 0.5 * n : 0.0);
                    break;
                case DrumCategory::percussion:
                    if (variant % 2 == 0)   // a shaker: bright noise with a soft attack
                        v = 0.5 * (n - prev) * std::min (1.0, t / 0.025) * std::exp (-t / 0.08);
                    else                    // a conga-like tone
                        v = std::sin (2.0 * pi * percPitch * t) * std::exp (-t / 0.1) + (t < 0.002 ? 0.2 * n : 0.0);
                    break;
                case DrumCategory::unknown:
                    break;
            }
            prev2 = prev;
            prev = (c == DrumCategory::snare || c == DrumCategory::clap) ? band : n;
            const double fade = std::min (1.0, (seconds - t) / 0.02);
            x[i] = static_cast<float> (gain * v * fade);
        }
        a.channels.push_back (std::move (x));
        return a;
    }

    const DrumCategory all[] = { DrumCategory::kick, DrumCategory::snare, DrumCategory::clap, DrumCategory::closedHat, DrumCategory::openHat,
                                 DrumCategory::tom, DrumCategory::rim, DrumCategory::cymbal, DrumCategory::percussion };
} // namespace

TEST_CASE ("Classifier: sounds alone (no names), per category", "[drums][classify]")
{
    std::map<DrumCategory, std::map<DrumCategory, int>> confusion;
    int confidentWrong = 0, confident = 0, total = 0;
    for (const auto c : all)
        for (std::uint64_t seed = 1; seed <= 12; ++seed)
        {
            const auto audio = make (c, seed, static_cast<int> (seed % 2));
            const auto g = classify ("sample.wav", drumFeatures (audio));
            ++confusion[c][g.category];
            ++total;
            if (g.category != DrumCategory::unknown)
            {
                ++confident;
                confidentWrong += g.category != c ? 1 : 0;
            }
        }
    std::printf ("\n%-11s", "true\\guess");
    for (const auto g : all)
        std::printf ("%7.6s", toString (g));
    std::printf ("%8s\n", "unknown");
    for (const auto c : all)
    {
        std::printf ("%-11s", toString (c));
        for (const auto g : all)
            std::printf ("%7d", confusion[c][g]);
        std::printf ("%8d\n", confusion[c][DrumCategory::unknown]);
    }
    std::printf ("confident %d of %d, wrong %d\n", confident, total, confidentWrong);
    // The core kit is recognised; nothing much is claimed wrongly with confidence.
    for (const auto c : { DrumCategory::kick, DrumCategory::snare, DrumCategory::closedHat, DrumCategory::openHat, DrumCategory::cymbal })
    {
        INFO (toString (c));
        CHECK (confusion[c][c] >= 10);
    }
    for (const auto c : { DrumCategory::clap, DrumCategory::tom, DrumCategory::rim })
    {
        INFO (toString (c));
        CHECK (confusion[c][c] >= 8);
    }
    CHECK (static_cast<double> (confidentWrong) / static_cast<double> (confident) <= 0.12);
}

TEST_CASE ("Classifier: file names first, abbreviations only as whole words", "[drums][classify]")
{
    struct Case
    {
        const char* name;
        DrumCategory want;
    };
    const Case cases[] = {
        { "Kick_01.wav", DrumCategory::kick },          { "BD 808 long.wav", DrumCategory::kick },     { "KickSoft02.aif", DrumCategory::kick },
        { "snare-rimshot-dry.wav", DrumCategory::snare }, { "SD_Tight.wav", DrumCategory::snare },        { "Clap Big.wav", DrumCategory::clap },
        { "HH closed 3.wav", DrumCategory::closedHat }, { "OpenHat_02.wav", DrumCategory::openHat },     { "Hihat open.wav", DrumCategory::openHat },
        { "OH-long.wav", DrumCategory::openHat },        { "Tom Low.wav", DrumCategory::tom },           { "Hi Tom.wav", DrumCategory::tom },
        { "Rim.wav", DrumCategory::rim },                { "Crash 18in.wav", DrumCategory::cymbal },     { "Ride bell.wav", DrumCategory::cymbal },
        { "Shaker loop one.wav", DrumCategory::percussion }, { "Cowbell.wav", DrumCategory::percussion },
    };
    for (const auto& c : cases)
    {
        const auto g = classifyName (c.name);
        INFO (c.name);
        CHECK (g.category == c.want);
        CHECK (g.confidence >= 0.6);
    }
    // Nothing in these names says what they are.
    for (const char* name : { "abdomen.wav", "Take 3.wav", "sound_final_v2.wav", "chorus.wav", "hdrumloop.wav" })
    {
        INFO (name);
        CHECK (classifyName (name).confidence < 0.6);
    }
    // A name naming two things is not trusted alone.
    CHECK (classifyName ("kick and snare.wav").confidence == Approx (0.5));
}

TEST_CASE ("Classifier: name and sound together; disagreement lowers confidence; unsure is Unknown", "[drums][classify]")
{
    const auto kick = drumFeatures (make (DrumCategory::kick, 3));
    const auto hat = drumFeatures (make (DrumCategory::closedHat, 3));
    const auto agree = classify ("Kick_01.wav", kick);
    CHECK (agree.category == DrumCategory::kick);
    CHECK (agree.confidence >= 0.9);
    // The name says snare, the sound is clearly a hat: the name wins (reliable clue), unsure.
    const auto disagree = classify ("Snare_09.wav", hat);
    CHECK (disagree.category == DrumCategory::snare);
    CHECK (disagree.confidence < 0.6);
    CHECK (disagree.reason.find ("suggests") != std::string::npos);
    // No name, no sound: unknown.
    CHECK (classify ("x.wav", DrumFeatures {}).category == DrumCategory::unknown);
}

TEST_CASE ("Classifier: a kick and a hat in one hit is layered and uncertain", "[drums][classify]")
{
    auto kick = make (DrumCategory::kick, 5);
    const auto hat = make (DrumCategory::closedHat, 5);
    for (std::size_t i = 0; i < hat.channels[0].size() && i < kick.channels[0].size(); ++i)
        kick.channels[0][i] += 1.5f * hat.channels[0][i];
    const auto g = classifyFeatures (drumFeatures (kick));
    CHECK (g.layered);
    CHECK (g.confidence < 0.7);
    CHECK_FALSE (classifyFeatures (drumFeatures (make (DrumCategory::kick, 5))).layered);
}

TEST_CASE ("Grouping: alike takes become one pad's round robins; different sounds stay apart", "[drums][classify]")
{
    std::vector<GroupItem> items;
    auto add = [&items] (const std::string& name, const AudioData& audio) {
        GroupItem it;
        it.name = name;
        it.features = drumFeatures (audio);
        it.guess = classify (name, it.features);
        items.push_back (it);
    };
    // Three takes of the same kick (small variations of one design).
    for (int take = 0; take < 3; ++take)
    {
        auto k = make (DrumCategory::kick, 21, 0);
        for (auto& v : k.channels[0])
            v *= static_cast<float> (1.0 - 0.1 * take);
        add ("Kick_0" + std::to_string (take + 1) + ".wav", k);
    }
    add ("Kick 808 long.wav", make (DrumCategory::kick, 22, 1));   // a different kick: its own pad
    add ("Snare_01.wav", make (DrumCategory::snare, 23));
    add ("Snare_02.wav", make (DrumCategory::snare, 23));
    add ("HH_01.wav", make (DrumCategory::closedHat, 24));
    const auto groups = groupItems (items);
    REQUIRE (groups.size() == 4);
    CHECK (groups[0] == std::vector<int> { 0, 1, 2 });   // kicks first (kit order)
    CHECK (groups[1] == std::vector<int> { 3 });
    CHECK (groups[2] == std::vector<int> { 4, 5 });
    CHECK (groups[3] == std::vector<int> { 6 });
    CHECK (nameStem ("Kick_03.wav") == "kick");
    CHECK (nameStem ("Snare RR2 v3.aif") == "snare");
}

TEST_CASE ("Auto-mapping: a pad of the same category first, then free pads; overflow to the tray", "[drums][classify]")
{
    std::vector<GroupItem> items (12);
    std::vector<std::vector<int>> groups;
    const DrumCategory cats[] = { DrumCategory::snare, DrumCategory::kick, DrumCategory::unknown, DrumCategory::tom, DrumCategory::tom,
                                  DrumCategory::rim, DrumCategory::clap, DrumCategory::cymbal, DrumCategory::percussion, DrumCategory::openHat,
                                  DrumCategory::closedHat, DrumCategory::percussion };
    for (int i = 0; i < 12; ++i)
    {
        items[static_cast<std::size_t> (i)].guess.category = cats[i];
        groups.push_back ({ i });
    }
    const std::array<DrumCategory, maxPads> pads { DrumCategory::kick, DrumCategory::snare, DrumCategory::closedHat, DrumCategory::openHat, DrumCategory::clap,
                                                   DrumCategory::tom, DrumCategory::rim, DrumCategory::percussion, DrumCategory::cymbal };
    const auto map = mapGroupsToPads (groups, items, pads, { 0, 1, 2, 3, 4, 5, 6, 7, 8 });
    CHECK (map[0] == 1);   // snare -> the snare pad
    CHECK (map[1] == 0);   // kick -> the kick pad
    CHECK (map[3] == 5);   // the first tom -> the tom pad
    CHECK (map[7] == 8);   // cymbal
    int tray = 0;
    std::set<int> used;
    for (int p : map)
    {
        tray += p < 0 ? 1 : 0;
        if (p >= 0)
            CHECK (used.insert (p).second);   // never two groups on one pad
    }
    CHECK (tray == 3);   // 12 groups, 9 pads: three wait in the tray (never discarded)
    // Only some pads free (a kit in use): the rest go to the tray.
    const auto partial = mapGroupsToPads (groups, items, pads, { 7, 8 });
    CHECK (std::count_if (partial.begin(), partial.end(), [] (int p) { return p >= 0; }) == 2);
}

TEST_CASE ("Classifier: feature dump (diagnostics)", "[.][dump]")
{
    for (const auto c : all)
        for (std::uint64_t seed = 1; seed <= 4; ++seed)
        {
            const auto f = drumFeatures (make (c, seed, static_cast<int> (seed % 2)));
            std::printf ("%-10s dur %.3f att %.4f cen %6.0f flat %.3f tail %6.0f low %.2f lm %.2f mid %.2f high %.2f lpk %4.0f prom %4.1f bursts %d\n",
                         toString (c), f.durationSeconds, f.attackSeconds, f.centroidHz, f.flatness, f.tailCentroidHz, f.low(), f.bands[2], f.bands[3], f.high(),
                         f.lowPeakHz, f.lowPeakProminence, f.bursts);
        }
}
