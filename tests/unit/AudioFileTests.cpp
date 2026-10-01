#include "audio/utility/TestSignals.h"
#include "io/AudioFileIO.h"
#include "research/TestSignalSet.h"
#include "support/TestHelpers.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <fstream>

using Catch::Approx;
using namespace osp;

namespace
{
    double toleranceFor (io::SampleFormat format)
    {
        switch (format)
        {
            case io::SampleFormat::pcm16: return 1.0 / 32768.0 + 1e-7;
            case io::SampleFormat::pcm24: return 1.0 / 8388608.0 + 1e-7;
            case io::SampleFormat::float32: return 0.0;
        }
        return 0.0;
    }
}

TEST_CASE ("audio files: WAV/AIFF round trip across rates, depths and channel counts", "[unit][io]")
{
    test::TempDir dir;
    const auto extension = GENERATE (std::string (".wav"), std::string (".aif"));
    const auto rate = GENERATE (44100.0, 48000.0, 88200.0, 96000.0);
    const auto channels = GENERATE (1, 2);
    const auto format = GENERATE (io::SampleFormat::pcm16, io::SampleFormat::pcm24, io::SampleFormat::float32);

    if (extension == ".aif" && format == io::SampleFormat::float32)
        return; // JUCE writes integer AIFF only; float AIFF-C decoding is tested separately

    auto audio = testsignals::saw (220.0, 0.25, rate, 0.6, channels);
    if (channels == 2)
        for (auto& s : audio.channels[1])
            s *= -0.5f; // make channels distinguishable

    const auto path = dir / ("x" + extension);
    std::string error;
    REQUIRE (io::writeAudioFile (path, audio, format, error));

    const auto loaded = io::loadAudioFile (path);
    REQUIRE (loaded.ok);
    CHECK (loaded.info.formatName == (extension == ".wav" ? "WAV" : "AIFF"));
    CHECK (loaded.info.sampleRate == rate);
    CHECK (loaded.info.channels == channels);
    CHECK (loaded.info.isFloatingPoint == (format == io::SampleFormat::float32));
    CHECK (loaded.audio.numChannels() == channels);
    CHECK (loaded.audio.numFrames() == audio.numFrames());
    CHECK (test::maxDifference (loaded.audio, audio) <= toleranceFor (format));
}

TEST_CASE ("audio files: AIFF-C 32-bit float decodes", "[unit][io]")
{
    test::TempDir dir;
    const auto audio = testsignals::sine (130.81, 0.5, 44100.0, 0.5, 2);
    std::string error;
    REQUIRE (research::writeAifcFloat32 (dir / "f.aifc", audio, error));
    const auto loaded = io::loadAudioFile (dir / "f.aifc");
    REQUIRE (loaded.ok);
    CHECK (loaded.info.isFloatingPoint);
    CHECK (loaded.info.sampleRate == 44100.0);
    CHECK (test::maxDifference (loaded.audio, audio) == 0.0);
}

TEST_CASE ("audio files: loading never modifies the source file", "[unit][io]")
{
    test::TempDir dir;
    std::string error;
    REQUIRE (io::writeAudioFile (dir / "s.wav", testsignals::sine (440.0, 0.2, 48000.0), io::SampleFormat::pcm24, error));
    const auto before = std::filesystem::last_write_time (dir / "s.wav");
    const auto size = std::filesystem::file_size (dir / "s.wav");
    REQUIRE (io::loadAudioFile (dir / "s.wav").ok);
    CHECK (std::filesystem::last_write_time (dir / "s.wav") == before);
    CHECK (std::filesystem::file_size (dir / "s.wav") == size);
}

TEST_CASE ("audio files: bad input fails cleanly with a message", "[unit][io]")
{
    test::TempDir dir;

    auto missing = io::loadAudioFile (dir / "nope.wav");
    CHECK_FALSE (missing.ok);
    CHECK (missing.error.find ("not found") != std::string::npos);

    std::ofstream (dir / "notes.txt") << "hello";
    auto unsupported = io::loadAudioFile (dir / "notes.txt");
    CHECK_FALSE (unsupported.ok);
    CHECK (unsupported.error.find ("unsupported") != std::string::npos);

    std::ofstream (dir / "garbage.wav", std::ios::binary) << "RIFF1234WAVEfmt this is not a wave file at all";
    auto garbage = io::loadAudioFile (dir / "garbage.wav");
    CHECK_FALSE (garbage.ok);
    CHECK_FALSE (garbage.error.empty());

    std::ofstream (dir / "empty.aif", std::ios::binary);
    CHECK_FALSE (io::loadAudioFile (dir / "empty.aif").ok);

    // Header-only WAV (no frames).
    std::string error;
    auto silenceAudio = testsignals::sine (440.0, 0.0, 48000.0);
    CHECK_FALSE (io::writeAudioFile (dir / "zero.wav", silenceAudio, io::SampleFormat::pcm16, error));
}

TEST_CASE ("audio files: content is detected regardless of extension", "[unit][io]")
{
    test::TempDir dir;
    std::string error;
    REQUIRE (io::writeAudioFile (dir / "really_aiff.aif", testsignals::sine (440.0, 0.2, 48000.0), io::SampleFormat::pcm16, error));
    std::filesystem::rename (dir / "really_aiff.aif", dir / "mislabelled.wav");
    const auto loaded = io::loadAudioFile (dir / "mislabelled.wav");
    REQUIRE (loaded.ok);
    CHECK (loaded.info.formatName == "AIFF");
}

TEST_CASE ("audio files: very long files are truncated with a warning, not rejected", "[unit][io]")
{
    test::TempDir dir;
    std::string error;
    REQUIRE (io::writeAudioFile (dir / "long.wav", testsignals::sine (440.0, 3.0, 8000.0), io::SampleFormat::pcm16, error));
    io::LoadOptions options;
    options.maxSeconds = 1.0;
    const auto loaded = io::loadAudioFile (dir / "long.wav", options);
    REQUIRE (loaded.ok);
    CHECK (loaded.truncated);
    CHECK (loaded.audio.numFrames() == 8000);
    CHECK_FALSE (loaded.warnings.empty());
}
