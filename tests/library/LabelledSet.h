#pragma once

// The Stage 6 evaluation set: synthetic recordings of every kind the spec names, each with
// the classification a person would give it. Synthetic, so the labels are certain and the
// set is reproducible (seeded); it is not a substitute for real recordings
// (docs/library/classification.md says what it does and does not show).

#include "audio/utility/TestSignals.h"
#include "core/Prng.h"
#include "library/Classifier.h"

#include <cmath>
#include <string>
#include <vector>

namespace osp::labelled
{

struct Item
{
    std::string kind;
    library::SoundClass label;
    AudioData audio;
};

inline AudioData blank (double seconds, double rate = 48000.0, int channels = 1)
{
    return testsignals::silence (seconds, rate, channels);
}

/** Adds `source` into `target` at `atSeconds` (channel 0 into every channel when mono). */
inline void mix (AudioData& target, const AudioData& source, double atSeconds, float gain = 1.0f)
{
    const auto at = static_cast<std::int64_t> (atSeconds * target.sampleRate);
    for (int c = 0; c < target.numChannels(); ++c)
    {
        const auto& from = source.channels[static_cast<std::size_t> (std::min (c, source.numChannels() - 1))];
        auto& to = target.channels[static_cast<std::size_t> (c)];
        for (std::size_t i = 0; i < from.size() && at + static_cast<std::int64_t> (i) < static_cast<std::int64_t> (to.size()); ++i)
            to[static_cast<std::size_t> (at) + i] += from[i] * gain;
    }
}

inline AudioData kick (double rate, double gain = 0.9)
{
    auto a = blank (0.45, rate);
    double phase = 0.0;
    for (std::size_t i = 0; i < a.channels[0].size(); ++i)
    {
        const double t = static_cast<double> (i) / rate;
        const double f = 50.0 + 110.0 * std::exp (-t / 0.03);
        phase += 2.0 * 3.14159265358979 * f / rate;
        a.channels[0][i] = static_cast<float> (gain * std::sin (phase) * std::exp (-t / 0.11));
    }
    return a;
}

inline AudioData snare (double rate, std::uint64_t seed, double gain = 0.7)
{
    auto a = blank (0.3, rate);
    Prng prng (seed);
    for (std::size_t i = 0; i < a.channels[0].size(); ++i)
    {
        const double t = static_cast<double> (i) / rate;
        const double noise = prng.bipolar();
        a.channels[0][i] = static_cast<float> (gain * (0.7 * noise * std::exp (-t / 0.07) + 0.4 * std::sin (2.0 * 3.14159265358979 * 190.0 * t) * std::exp (-t / 0.05)));
    }
    return a;
}

inline AudioData hat (double rate, std::uint64_t seed, double gain = 0.5)
{
    auto a = blank (0.12, rate);
    Prng prng (seed);
    float previous = 0.0f;
    for (std::size_t i = 0; i < a.channels[0].size(); ++i)
    {
        const double t = static_cast<double> (i) / rate;
        const auto noise = static_cast<float> (prng.bipolar());
        a.channels[0][i] = static_cast<float> (gain * (noise - previous) * 0.5 * std::exp (-t / 0.025));
        previous = noise;
    }
    return a;
}

/** A sustained tone with a slow (bowed) or quick attack and a natural release. */
inline AudioData held (double hz, double seconds, double rate, double attackSeconds, std::uint64_t seed, bool sawTone = false)
{
    auto a = sawTone ? testsignals::saw (hz, seconds, rate, 0.35, 2) : testsignals::vowel (hz, seconds, rate, seed, 0.4, 2);
    testsignals::applyFades (a, attackSeconds, std::min (0.6, seconds * 0.2));
    return a;
}

inline AudioData pad (double hz, double seconds, double rate)
{
    auto a = testsignals::saw (hz, seconds, rate, 0.2, 2);
    const auto b = testsignals::saw (hz * 1.004, seconds, rate, 0.2, 2);
    const auto c = testsignals::saw (hz * 1.5, seconds, rate, 0.12, 2);
    mix (a, b, 0.0);
    mix (a, c, 0.0);
    testsignals::applyFades (a, 1.2, 1.5);
    return a;
}

inline void addNoise (AudioData& a, double amplitude, std::uint64_t seed)
{
    Prng prng (seed);
    for (auto& c : a.channels)
        for (auto& v : c)
            v += static_cast<float> (amplitude * prng.bipolar());
}

/** The whole set: twelve kinds, several takes each. */
inline std::vector<Item> evaluationSet (double rate = 48000.0)
{
    using C = library::SoundClass;
    std::vector<Item> set;
    for (int take = 0; take < 4; ++take)
    {
        const auto seed = static_cast<std::uint64_t> (100 + take);
        // Drum hits (one each, a little air around them).
        {
            auto a = blank (0.7, rate);
            mix (a, take % 3 == 0 ? kick (rate) : (take % 3 == 1 ? snare (rate, seed) : hat (rate, seed)), 0.02);
            set.push_back ({ "drum hit", C::likelyOneShot, a });
        }
        set.push_back ({ "pluck", C::likelyOneShot, testsignals::pluck (110.0 * (take + 1), 1.8, rate, seed) });
        set.push_back ({ "bowed note", C::likelyOneShot, held (196.0 + 40.0 * take, 3.5, rate, 0.25, seed) });
        set.push_back ({ "sustained synth", C::likelyOneShot, held (130.81 * (take + 1), 4.0, rate, 0.01, seed, true) });
        set.push_back ({ "pad", C::likelyOneShot, pad (98.0 + 20.0 * take, 8.0, rate) });
        set.push_back ({ "vocal vowel", C::likelyOneShot, held (220.0 + 30.0 * take, 2.0, rate, 0.08, seed) });
        // A sung phrase: three notes with breaths between.
        {
            auto a = blank (3.6, rate, 2);
            for (int n = 0; n < 3; ++n)
                mix (a, held (220.0 * std::pow (2.0, (n * 2 + take) / 12.0), 0.8, rate, 0.06, seed + static_cast<std::uint64_t> (n)), 0.1 + n * 1.15);
            set.push_back ({ "vocal phrase", C::multipleEvents, a });
        }
        // A drum loop: two bars of kick, hat and snare at 100-130 bpm.
        {
            const double bpm = 100.0 + 10.0 * take;
            const double eighth = 30.0 / bpm;
            auto a = blank (16 * eighth + 0.05, rate);
            for (int s = 0; s < 16; ++s)
            {
                const auto hit = s % 4 == 0 ? kick (rate, 0.8) : (s % 4 == 2 ? snare (rate, seed + static_cast<std::uint64_t> (s), 0.6) : hat (rate, seed + static_cast<std::uint64_t> (s), 0.4));
                mix (a, hit, s * eighth);
            }
            set.push_back ({ "musical loop", C::likelyLoop, a });
        }
        // A song stem: a minute of notes that keep changing.
        {
            auto a = blank (60.0, rate, 2);
            Prng prng (seed);
            double t = 0.0;
            while (t < 59.0)
            {
                const double length = 0.4 + 1.2 * prng.nextDouble();
                mix (a, testsignals::pluck (110.0 * std::pow (2.0, std::floor (prng.nextDouble() * 24.0) / 12.0), length + 0.3, rate, prng.nextU64(), 0.5), t);
                t += length;
            }
            set.push_back ({ "song stem", C::longRecording, a });
        }
        // A field recording: a minute of room noise with things happening in it.
        {
            auto a = blank (70.0, rate);
            addNoise (a, 0.01, seed);
            Prng prng (seed + 7);
            for (double t = 1.0; t < 68.0; t += 1.5 + 4.0 * prng.nextDouble())
                mix (a, prng.nextDouble() < 0.5 ? snare (rate, prng.nextU64(), 0.3) : testsignals::pluck (300.0 + 500.0 * prng.nextDouble(), 0.6, rate, prng.nextU64(), 0.3), t);
            set.push_back ({ "field recording", C::longRecording, a });
        }
        // Several hits recorded one after another, unevenly.
        {
            auto a = blank (5.0, rate);
            Prng prng (seed + 11);
            double t = 0.05;
            for (int n = 0; n < 5; ++n)
            {
                mix (a, n % 2 == 0 ? kick (rate, 0.6 + 0.3 * prng.nextDouble()) : snare (rate, prng.nextU64(), 0.6), t);
                t += 0.55 + 0.4 * prng.nextDouble();
            }
            set.push_back ({ "multiple isolated events", C::multipleEvents, a });
        }
        // A noisy take of one note.
        {
            auto a = testsignals::pluck (146.83 * (take + 1), 2.0, rate, seed);
            addNoise (a, 0.02, seed + 3);
            set.push_back ({ "noisy recording", C::likelyOneShot, a });
        }
    }
    return set;
}

/** Harder cases, reported apart: where a person's answer is clear but the signals disagree. */
inline std::vector<Item> hardCases (double rate = 48000.0)
{
    using C = library::SoundClass;
    std::vector<Item> set;
    for (int take = 0; take < 2; ++take)
    {
        const auto seed = static_cast<std::uint64_t> (500 + take);
        // A strummed chord: three plucks 30-40 ms apart. One sound to a musician.
        {
            auto a = blank (2.5, rate);
            for (int n = 0; n < 3; ++n)
                mix (a, testsignals::pluck (146.83 * std::pow (2.0, (n * 4 + take) / 12.0), 2.2, rate, seed + static_cast<std::uint64_t> (n), 0.5), 0.05 + n * (0.03 + 0.01 * take));
            set.push_back ({ "strummed chord", C::likelyOneShot, a });
        }
        // A swung drum loop (the off-beats late by a third).
        {
            const double eighth = 30.0 / (95.0 + 10.0 * take);
            auto a = blank (16 * eighth + 0.05, rate);
            for (int s = 0; s < 16; ++s)
            {
                const double t = s * eighth + (s % 2 == 1 ? eighth * 0.33 : 0.0);
                mix (a, s % 4 == 0 ? kick (rate, 0.8) : hat (rate, seed + static_cast<std::uint64_t> (s), 0.4), t);
            }
            set.push_back ({ "swung loop", C::likelyLoop, a });
        }
        // A 90-second drone: one sound, very long.
        set.push_back ({ "long drone", C::likelyOneShot, pad (55.0 + 10.0 * take, 90.0, rate) });
        // A legato phrase: notes joined without breaths.
        {
            auto a = blank (3.2, rate, 2);
            for (int n = 0; n < 4; ++n)
                mix (a, held (220.0 * std::pow (2.0, (n * 3 + take) / 12.0), 0.85, rate, 0.02, seed + static_cast<std::uint64_t> (n)), 0.05 + n * 0.75);
            set.push_back ({ "legato phrase", C::multipleEvents, a });
        }
        // A quiet single hit in hiss (-45 dBFS hit, -66 dBFS noise).
        {
            auto a = blank (1.5, rate);
            addNoise (a, 0.0005, seed);
            mix (a, snare (rate, seed, 0.006), 0.3);
            set.push_back ({ "quiet hit in hiss", C::likelyOneShot, a });
        }
        // A swell: noise rising for two seconds, then cut (a reversed cymbal).
        {
            auto a = blank (2.2, rate);
            Prng prng (seed);
            for (std::size_t i = 0; i < static_cast<std::size_t> (2.0 * rate); ++i)
                a.channels[0][i] = static_cast<float> (0.5 * prng.bipolar() * std::pow (static_cast<double> (i) / (2.0 * rate), 3.0));
            set.push_back ({ "reverse swell", C::likelyOneShot, a });
        }
    }
    return set;
}

} // namespace osp::labelled
