#include "TutorialSounds.h"

#include "audio/utility/TestSignals.h"
#include "core/Prng.h"
#include "io/AudioFileIO.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <functional>

namespace osp::plugin::tutorial
{

namespace
{
    constexpr double rate = 48000.0;
    constexpr double twoPi = 6.283185307179586;

    double hzOf (double midi) { return 440.0 * std::pow (2.0, (midi - 69.0) / 12.0); }

    /** A bowed-string-like note: a bright harmonic series through a few fixed body resonances,
        a slow bowed attack, vibrato that arrives after the attack, a little bow noise and a
        bow pressure that wanders slowly. Mono. */
    AudioData bowedNote (double midi, double seconds, std::uint64_t seed)
    {
        AudioData out;
        out.sampleRate = rate;
        const auto frames = static_cast<std::size_t> (seconds * rate);
        out.channels.assign (1, std::vector<float> (frames, 0.0f));
        auto& x = out.channels.front();
        const double f0 = hzOf (midi);
        const int harmonics = std::min (48, static_cast<int> ((0.45 * rate) / f0));
        // The body: resonances (Hz) and their widths in octaves.
        const double peaks[] = { 290.0, 460.0, 1050.0, 2600.0 };
        const double gains[] = { 1.0, 0.8, 0.55, 0.45 };
        std::vector<double> amp (static_cast<std::size_t> (harmonics + 1), 0.0), phase (amp.size(), 0.0);
        for (int k = 1; k <= harmonics; ++k)
        {
            const double f = f0 * k;
            double body = 0.18;
            for (int p = 0; p < 4; ++p)
            {
                const double d = std::log2 (f / peaks[p]) / 0.45;
                body += gains[p] * std::exp (-0.5 * d * d);
            }
            amp[static_cast<std::size_t> (k)] = body / std::pow (static_cast<double> (k), 1.05);
        }
        Prng prng (seed);
        double pressure = 0.0, pressureTarget = 0.0, noiseLp = 0.0;
        const double attack = 0.22, release = 0.35;
        for (std::size_t i = 0; i < frames; ++i)
        {
            const double t = static_cast<double> (i) / rate;
            if (i % 2400 == 0)
                pressureTarget = prng.uniform (-1.0, 1.0);
            pressure += (pressureTarget - pressure) * 0.0004;
            const double vibratoDepth = 14.0 * std::clamp ((t - 0.45) / 0.7, 0.0, 1.0);   // cents
            const double cents = vibratoDepth * std::sin (twoPi * 5.3 * t) + 3.0 * pressure;
            const double f = f0 * std::pow (2.0, cents / 1200.0);
            const double rise = std::clamp (t / attack, 0.0, 1.0);
            const double fall = std::clamp ((seconds - t) / release, 0.0, 1.0);
            const double env = rise * rise * (3.0 - 2.0 * rise) * fall * (1.0 + 0.06 * pressure);
            double s = 0.0;
            for (int k = 1; k <= harmonics; ++k)
            {
                auto& ph = phase[static_cast<std::size_t> (k)];
                ph += twoPi * f * k / rate;
                if (ph > twoPi)
                    ph -= twoPi;
                s += amp[static_cast<std::size_t> (k)] * std::sin (ph);
            }
            // Bow noise: band-limited, strongest in the attack.
            const double n = prng.uniform (-1.0, 1.0);
            noiseLp += (n - noiseLp) * 0.25;
            const double noise = noiseLp * (0.012 + 0.03 * (1.0 - rise));
            x[i] = static_cast<float> (0.22 * env * s + env * noise);
        }
        testsignals::applyFades (out, 0.002, 0.02);
        return out;
    }

    struct Spec
    {
        juce::String filename;
        std::function<AudioData()> make;
    };

    std::vector<Spec> specs (Example e)
    {
        switch (e)
        {
            case Example::bowed:
                return { { "Tutorial Bowed Note C3.wav", [] { return bowedNote (48.0, 4.5, 0x0b0e); } } };
            case Example::pluck:
                return { { "Tutorial Pluck A3.wav", [] { return testsignals::pluck (hzOf (57.0), 1.6, rate, 0x91c4, 0.7, 1); } } };
            case Example::voice:
                return { { "Tutorial Voice A3.wav", [] { return testsignals::vowel (hzOf (57.0), 5.0, rate, 0x7a3, 0.5, 2); } } };
            case Example::layered:
            {
                auto bowed = specs (Example::bowed);
                auto voice = specs (Example::voice);
                bowed.push_back (voice.front());
                return bowed;
            }
            case Example::pluckSet:
            {
                std::vector<Spec> set;
                const std::pair<const char*, double> roots[] = { { "G3", 55.0 }, { "C4", 60.0 }, { "G4", 67.0 }, { "D5", 74.0 } };
                std::uint64_t seed = 0x5e7;
                for (const auto& [name, midi] : roots)
                    for (int take = 1; take <= 2; ++take)
                    {
                        const auto s = ++seed;
                        const double m = midi;
                        set.push_back ({ juce::String ("Tutorial Pluck Set/Pluck_") + name + "_0" + juce::String (take) + ".wav",
                                         [s, m] { return testsignals::pluck (hzOf (m), 1.4, rate, s, 0.7, 1); } });
                    }
                return set;
            }
        }
        return {};
    }
} // namespace

juce::String idOf (Example e)
{
    switch (e)
    {
        case Example::bowed: return "bowed";
        case Example::pluck: return "pluck";
        case Example::voice: return "voice";
        case Example::layered: return "layered";
        case Example::pluckSet: return "pluckSet";
    }
    return {};
}

std::optional<Example> fromId (const juce::String& id)
{
    for (auto e : { Example::bowed, Example::pluck, Example::voice, Example::layered, Example::pluckSet })
        if (idOf (e) == id)
            return e;
    return std::nullopt;
}

juce::String nameOf (Example e)
{
    switch (e)
    {
        case Example::bowed: return "the bowed note";
        case Example::pluck: return "the plucked note";
        case Example::voice: return "the voice";
        case Example::layered: return "the layered example";
        case Example::pluckSet: return "the pluck set";
    }
    return {};
}

juce::File folder()
{
    const auto overridden = juce::SystemStats::getEnvironmentVariable ("OSP_TUTORIAL_DIR", {});
    if (overridden.isNotEmpty())
        return juce::File (overridden);
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("ANDOR").getChildFile ("OSP").getChildFile ("Tutorial Sounds");
}

juce::Array<juce::File> files (Example e, juce::String& error)
{
    juce::Array<juce::File> out;
    for (const auto& spec : specs (e))
    {
        const auto file = folder().getChildFile (spec.filename);
        if (! file.existsAsFile())
        {
            if (! file.getParentDirectory().createDirectory())
            {
                error = "could not create " + file.getParentDirectory().getFullPathName();
                return {};
            }
            std::string message;
            if (! io::writeAudioFile (std::filesystem::path (file.getFullPathName().toStdString()), spec.make(), io::SampleFormat::pcm24, message))
            {
                error = juce::String (message);
                return {};
            }
        }
        out.add (file);
    }
    return out;
}

} // namespace osp::plugin::tutorial
