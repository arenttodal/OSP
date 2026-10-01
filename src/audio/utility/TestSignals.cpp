#include "audio/utility/TestSignals.h"

#include "core/PitchMath.h"
#include "core/Prng.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace osp::testsignals
{

namespace
{
    constexpr double twoPi = 2.0 * std::numbers::pi;

    std::int64_t framesFor (double seconds, double sampleRate)
    {
        return std::max<std::int64_t> (0, static_cast<std::int64_t> (std::llround (seconds * sampleRate)));
    }

    void duplicateChannel0 (AudioData& audio)
    {
        for (int ch = 1; ch < audio.numChannels(); ++ch)
            audio.channels[static_cast<std::size_t> (ch)] = audio.channels[0];
    }
}

AudioData sine (double frequencyHz, double seconds, double sampleRate, double amplitude, int channels)
{
    auto audio = AudioData::allocate (channels, framesFor (seconds, sampleRate), sampleRate);
    auto& x = audio.channels[0];
    for (std::size_t i = 0; i < x.size(); ++i)
        x[i] = static_cast<float> (amplitude * std::sin (twoPi * frequencyHz * static_cast<double> (i) / sampleRate));
    duplicateChannel0 (audio);
    return audio;
}

AudioData saw (double frequencyHz, double seconds, double sampleRate, double amplitude, int channels)
{
    auto audio = AudioData::allocate (channels, framesFor (seconds, sampleRate), sampleRate);
    auto& x = audio.channels[0];
    const int harmonics = std::max (1, static_cast<int> (0.45 * sampleRate / frequencyHz));
    const double norm = amplitude * 2.0 / std::numbers::pi * 0.6;
    for (std::size_t i = 0; i < x.size(); ++i)
    {
        const double phase = twoPi * frequencyHz * static_cast<double> (i) / sampleRate;
        double sum = 0.0;
        for (int k = 1; k <= harmonics; ++k)
            sum += std::sin (k * phase) / k;
        x[i] = static_cast<float> (norm * sum);
    }
    duplicateChannel0 (audio);
    return audio;
}

AudioData whiteNoise (double seconds, double sampleRate, double amplitude, std::uint64_t seed, int channels)
{
    auto audio = AudioData::allocate (channels, framesFor (seconds, sampleRate), sampleRate);
    Prng rng (seed);
    for (auto& ch : audio.channels)
        for (auto& s : ch)
            s = static_cast<float> (amplitude * rng.bipolar());
    return audio;
}

AudioData silence (double seconds, double sampleRate, int channels)
{
    return AudioData::allocate (channels, framesFor (seconds, sampleRate), sampleRate);
}

AudioData impulse (double seconds, double sampleRate, double atSeconds, double amplitude, int channels)
{
    auto audio = AudioData::allocate (channels, framesFor (seconds, sampleRate), sampleRate);
    const auto index = static_cast<std::size_t> (std::llround (atSeconds * sampleRate));
    for (auto& ch : audio.channels)
        if (index < ch.size())
            ch[index] = static_cast<float> (amplitude);
    return audio;
}

AudioData vibratoSine (double frequencyHz, double rateHz, double depthCents, double seconds, double sampleRate,
                       double amplitude, int channels)
{
    auto audio = AudioData::allocate (channels, framesFor (seconds, sampleRate), sampleRate);
    auto& x = audio.channels[0];
    double phase = 0.0;
    for (std::size_t i = 0; i < x.size(); ++i)
    {
        const double t = static_cast<double> (i) / sampleRate;
        const double f = frequencyHz * centsToRatio (depthCents * std::sin (twoPi * rateHz * t));
        x[i] = static_cast<float> (amplitude * std::sin (phase));
        phase += twoPi * f / sampleRate;
    }
    duplicateChannel0 (audio);
    return audio;
}

AudioData tremoloSine (double frequencyHz, double rateHz, double depthDb, double seconds, double sampleRate,
                       double amplitude, int channels)
{
    auto audio = AudioData::allocate (channels, framesFor (seconds, sampleRate), sampleRate);
    auto& x = audio.channels[0];
    for (std::size_t i = 0; i < x.size(); ++i)
    {
        const double t = static_cast<double> (i) / sampleRate;
        const double gainDb = 0.5 * depthDb * (std::sin (twoPi * rateHz * t) - 1.0);
        x[i] = static_cast<float> (amplitude * dbToGain (gainDb) * std::sin (twoPi * frequencyHz * t));
    }
    duplicateChannel0 (audio);
    return audio;
}

AudioData pluck (double frequencyHz, double seconds, double sampleRate, std::uint64_t seed, double amplitude,
                 int channels)
{
    auto audio = AudioData::allocate (channels, framesFor (seconds, sampleRate), sampleRate);
    Prng rng (seed);

    for (int ch = 0; ch < channels; ++ch)
    {
        // Karplus-Strong: delay line + two-point average (0.5 sample delay) + first-order
        // all-pass for the fractional part of the period, so tuning is accurate.
        const double period = sampleRate / frequencyHz - 0.5;
        auto delay = static_cast<int> (std::floor (period));
        double frac = period - delay;
        if (frac < 0.1)
        {
            delay -= 1;
            frac += 1.0;
        }
        const double apCoeff = (1.0 - frac) / (1.0 + frac);

        std::vector<double> line (static_cast<std::size_t> (std::max (delay, 2)));
        for (auto& v : line)
            v = rng.bipolar();
        for (std::size_t i = 1; i < line.size(); ++i) // soften the excitation slightly
            line[i] = 0.6 * line[i] + 0.4 * line[i - 1];

        double previousOut = 0.0;
        double apIn1 = 0.0;
        double apOut1 = 0.0;
        std::size_t index = 0;
        auto& x = audio.channels[static_cast<std::size_t> (ch)];

        for (std::size_t i = 0; i < x.size(); ++i)
        {
            const double out = line[index];
            const double averaged = 0.998 * 0.5 * (out + previousOut);
            previousOut = out;
            const double ap = apCoeff * averaged + apIn1 - apCoeff * apOut1;
            apIn1 = averaged;
            apOut1 = ap;
            line[index] = ap;
            index = (index + 1) % line.size();
            x[i] = static_cast<float> (amplitude * out);
        }
    }
    return audio;
}

AudioData vowel (double frequencyHz, double seconds, double sampleRate, std::uint64_t seed, double amplitude,
                 int channels)
{
    auto audio = AudioData::allocate (channels, framesFor (seconds, sampleRate), sampleRate);
    Prng rng (seed);

    // Formants roughly of an "ah" vowel.
    constexpr double formantHz[] = { 730.0, 1090.0, 2440.0 };
    constexpr double formantWidth[] = { 90.0, 110.0, 170.0 };
    constexpr double formantGain[] = { 1.0, 0.5, 0.25 };

    const int harmonics = std::max (1, static_cast<int> (0.45 * sampleRate / frequencyHz));
    std::vector<double> harmonicGain (static_cast<std::size_t> (harmonics + 1), 0.0);
    double total = 0.0;
    for (int k = 1; k <= harmonics; ++k)
    {
        const double f = k * frequencyHz;
        double g = 0.02 / k;
        for (int i = 0; i < 3; ++i)
            g += formantGain[i] * std::exp (-0.5 * std::pow ((f - formantHz[i]) / formantWidth[i], 2.0));
        harmonicGain[static_cast<std::size_t> (k)] = g;
        total += g;
    }

    for (int ch = 0; ch < channels; ++ch)
    {
        auto& x = audio.channels[static_cast<std::size_t> (ch)];
        const double detune = ch == 0 ? 1.0 : centsToRatio (3.0);
        std::vector<double> phases (static_cast<std::size_t> (harmonics + 1));
        for (auto& p : phases)
            p = twoPi * rng.nextDouble();

        double vibPhase = 0.0;
        for (std::size_t i = 0; i < x.size(); ++i)
        {
            const double t = static_cast<double> (i) / sampleRate;
            const double vibDepth = std::min (1.0, t / 0.6) * 18.0; // vibrato fades in
            const double f0 = frequencyHz * detune * centsToRatio (vibDepth * std::sin (vibPhase));
            vibPhase += twoPi * 5.2 / sampleRate;

            double sum = 0.0;
            for (int k = 1; k <= harmonics; ++k)
            {
                auto& p = phases[static_cast<std::size_t> (k)];
                p += twoPi * k * f0 / sampleRate;
                if (p > twoPi)
                    p -= twoPi;
                sum += harmonicGain[static_cast<std::size_t> (k)] * std::sin (p);
            }
            const double attack = std::min (1.0, t / 0.12);
            x[i] = static_cast<float> (amplitude * attack * sum / total * 2.5);
        }
    }
    applyFades (audio, 0.0, 0.08);
    return audio;
}

void applyFades (AudioData& audio, double fadeInSeconds, double fadeOutSeconds)
{
    const auto n = audio.numFrames();
    const auto fadeIn = std::min<std::int64_t> (n, static_cast<std::int64_t> (fadeInSeconds * audio.sampleRate));
    const auto fadeOut = std::min<std::int64_t> (n, static_cast<std::int64_t> (fadeOutSeconds * audio.sampleRate));
    for (auto& ch : audio.channels)
    {
        for (std::int64_t i = 0; i < fadeIn; ++i)
            ch[static_cast<std::size_t> (i)] *= static_cast<float> (i) / static_cast<float> (fadeIn);
        for (std::int64_t i = 0; i < fadeOut; ++i)
            ch[static_cast<std::size_t> (n - 1 - i)] *= static_cast<float> (i) / static_cast<float> (fadeOut);
    }
}

} // namespace osp::testsignals
