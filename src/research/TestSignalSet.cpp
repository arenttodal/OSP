#include "research/TestSignalSet.h"

#include "audio/utility/TestSignals.h"
#include "core/PitchMath.h"
#include "io/AudioFileIO.h"

#include <cmath>
#include <cstring>
#include <fstream>

namespace osp::research
{

namespace
{
    void putBE32 (std::string& out, std::uint32_t v)
    {
        for (int shift = 24; shift >= 0; shift -= 8)
            out += static_cast<char> ((v >> shift) & 0xff);
    }

    void putBE16 (std::string& out, std::uint16_t v)
    {
        out += static_cast<char> ((v >> 8) & 0xff);
        out += static_cast<char> (v & 0xff);
    }

    /** IEEE 754 80-bit extended, as used for the AIFF sample rate. */
    void putExtended (std::string& out, double value)
    {
        int exponent = 0;
        const double mantissa = std::frexp (value, &exponent); // value = mantissa * 2^exponent, mantissa in [0.5, 1)
        const auto bits = static_cast<std::uint64_t> (std::ldexp (mantissa, 64));
        putBE16 (out, static_cast<std::uint16_t> (16382 + exponent));
        putBE32 (out, static_cast<std::uint32_t> (bits >> 32));
        putBE32 (out, static_cast<std::uint32_t> (bits & 0xffffffffu));
    }
}

bool writeAifcFloat32 (const std::filesystem::path& path, const AudioData& audio, std::string& error)
{
    const auto channels = static_cast<std::uint16_t> (audio.numChannels());
    const auto frames = static_cast<std::uint32_t> (audio.numFrames());

    std::string comm;
    putBE16 (comm, channels);
    putBE32 (comm, frames);
    putBE16 (comm, 32);
    putExtended (comm, audio.sampleRate);
    comm += "fl32";
    const std::string name = "IEEE 32-bit float";
    comm += static_cast<char> (name.size());
    comm += name;
    if ((name.size() + 1) % 2 != 0)
        comm += '\0';

    std::string ssnd;
    putBE32 (ssnd, 0); // offset
    putBE32 (ssnd, 0); // block size
    for (std::uint32_t i = 0; i < frames; ++i)
        for (int ch = 0; ch < channels; ++ch)
        {
            std::uint32_t bits = 0;
            const float sample = audio.channels[static_cast<std::size_t> (ch)][i];
            std::memcpy (&bits, &sample, sizeof (bits));
            putBE32 (ssnd, bits);
        }

    std::string body = "AIFC";
    body += "FVER";
    putBE32 (body, 4);
    putBE32 (body, 0xA2805140u);
    body += "COMM";
    putBE32 (body, static_cast<std::uint32_t> (comm.size()));
    body += comm;
    body += "SSND";
    putBE32 (body, static_cast<std::uint32_t> (ssnd.size()));
    body += ssnd;

    std::string file = "FORM";
    putBE32 (file, static_cast<std::uint32_t> (body.size()));
    file += body;

    std::ofstream stream (path, std::ios::binary);
    stream.write (file.data(), static_cast<std::streamsize> (file.size()));
    if (! stream)
    {
        error = "cannot write " + path.string();
        return false;
    }
    return true;
}

std::vector<GeneratedSignal> writeTestSignalSet (const std::filesystem::path& dir, std::string& error)
{
    namespace ts = testsignals;
    std::vector<GeneratedSignal> out;
    std::error_code ec;
    std::filesystem::create_directories (dir, ec);

    struct Item
    {
        std::string name;
        AudioData audio;
        io::SampleFormat format;
        double hz;
    };

    const double a4 = 440.0;
    const double c3 = midiToHz (48);
    const double a2 = midiToHz (45);
    const double e2 = midiToHz (40);
    const double a3 = midiToHz (57);

    std::vector<Item> items;
    items.push_back ({ "sine_A4_44k1_16bit_mono.wav", ts::sine (a4, 2.0, 44100.0, 0.5, 1), io::SampleFormat::pcm16, a4 });
    items.push_back ({ "sine_C3_48k_24bit_stereo.wav", ts::sine (c3, 2.0, 48000.0, 0.5, 2), io::SampleFormat::pcm24, c3 });
    items.push_back ({ "saw_A2_96k_24bit_stereo.aif", ts::saw (a2, 1.5, 96000.0, 0.5, 2), io::SampleFormat::pcm24, a2 });
    items.push_back ({ "saw_A2_88k2_24bit_mono.aiff", ts::saw (a2, 1.5, 88200.0, 0.5, 1), io::SampleFormat::pcm24, a2 });
    items.push_back ({ "pluck_E2_48k_24bit_mono.wav", ts::pluck (e2, 3.0, 48000.0, 3, 0.7, 1), io::SampleFormat::pcm24, e2 });
    items.push_back ({ "vowel_A3_48k_float_stereo.wav", ts::vowel (a3, 4.0, 48000.0, 11, 0.5, 2), io::SampleFormat::float32, a3 });
    items.push_back ({ "vibrato_A4_44k1_16bit_mono.wav", ts::vibratoSine (a4, 5.5, 40.0, 3.0, 44100.0), io::SampleFormat::pcm16, a4 });
    items.push_back ({ "tremolo_C3_48k_24bit_mono.aif", ts::tremoloSine (c3, 6.0, 12.0, 3.0, 48000.0), io::SampleFormat::pcm24, c3 });
    items.push_back ({ "noise_48k_24bit_stereo.wav", ts::whiteNoise (2.0, 48000.0, 0.3, 5, 2), io::SampleFormat::pcm24, 0.0 });
    items.push_back ({ "silence_44k1_16bit_mono.wav", ts::silence (1.0, 44100.0), io::SampleFormat::pcm16, 0.0 });
    items.push_back ({ "impulse_48k_24bit_mono.wav", ts::impulse (1.0, 48000.0), io::SampleFormat::pcm24, 0.0 });
    items.push_back ({ "tiny_A4_48k_float_mono.wav", ts::sine (a4, 0.01, 48000.0), io::SampleFormat::float32, 0.0 });

    for (auto& item : items)
    {
        ts::applyFades (item.audio, 0.005, 0.02);
        if (! io::writeAudioFile (dir / item.name, item.audio, item.format, error))
            return {};
        out.push_back ({ item.name, item.hz, true });
    }

    // AIFF-C 32-bit float (JUCE reads it but cannot write it).
    {
        auto audio = ts::sine (c3, 1.5, 44100.0, 0.5, 2);
        ts::applyFades (audio, 0.005, 0.02);
        if (! writeAifcFloat32 (dir / "sine_C3_44k1_float_stereo.aifc", audio, error))
            return {};
        out.push_back ({ "sine_C3_44k1_float_stereo.aifc", c3, true });
    }

    // A file with a WAV extension that is not a WAV.
    {
        std::ofstream broken (dir / "broken_header.wav", std::ios::binary);
        broken << "RIFF\x10\x00\x00\x00WAVEjunkjunkjunkjunk this is not audio";
        out.push_back ({ "broken_header.wav", 0.0, false });
    }
    // An unsupported type that should be listed and skipped.
    {
        std::ofstream notes (dir / "session_notes.txt");
        notes << "not audio\n";
        out.push_back ({ "session_notes.txt", 0.0, false });
    }
    return out;
}

} // namespace osp::research
