#include "audio/utility/TestSignals.h"
#include "io/AudioFileIO.h"
#include "io/ContentHash.h"
#include "research/CorpusIndex.h"
#include "support/TestHelpers.h"

#include <catch2/catch_test_macros.hpp>

#include <fstream>

using namespace osp;

TEST_CASE ("hashing: SHA-256 matches the standard test vectors", "[unit][hash]")
{
    CHECK (io::sha256OfBytes ("abc", 3) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    CHECK (io::sha256OfBytes ("", 0) == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");

    test::TempDir dir;
    std::ofstream (dir / "abc.bin", std::ios::binary) << "abc";
    CHECK (io::sha256OfFile (dir / "abc.bin").value() == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    CHECK_FALSE (io::sha256OfFile (dir / "missing.bin").has_value());
}

TEST_CASE ("corpus index: identity follows content, not names or locations", "[unit][hash][corpus]")
{
    test::TempDir dir;
    std::string error;
    std::filesystem::create_directories (dir / "sub");
    REQUIRE (io::writeAudioFile (dir / "a.wav", testsignals::sine (440.0, 0.2, 48000.0), io::SampleFormat::pcm16, error));
    std::filesystem::copy_file (dir / "a.wav", dir / "sub" / "renamed copy.WAV");
    REQUIRE (io::writeAudioFile (dir / "b.aif", testsignals::sine (220.0, 0.2, 48000.0), io::SampleFormat::pcm16, error));
    std::ofstream (dir / "notes.txt") << "x";
    std::ofstream (dir / ".DS_Store") << "x";

    const auto index = research::buildCorpusIndex (dir.path());
    REQUIRE (index.files.size() == 4); // hidden file ignored

    auto find = [&] (const std::string& rel) {
        for (const auto& e : index.files)
            if (e.relativePath == rel)
                return e;
        FAIL ("missing " << rel);
        return research::CorpusEntry {};
    };

    const auto a = find ("a.wav");
    const auto copy = find ("sub/renamed copy.WAV");
    const auto b = find ("b.aif");
    const auto txt = find ("notes.txt");

    CHECK (a.id.rfind ("sha256:", 0) == 0);
    CHECK (a.id == copy.id);
    CHECK (a.id != b.id);
    CHECK (copy.duplicateOf == "a.wav");
    CHECK (a.duplicateOf.empty());
    CHECK (a.supported);
    CHECK (copy.supported);
    CHECK (copy.extension == "wav");
    CHECK_FALSE (txt.supported);
    CHECK (a.folderName().find (a.id.substr (7, 8)) != std::string::npos);

    // Index output is deterministic.
    const auto again = research::buildCorpusIndex (dir.path());
    REQUIRE (again.files.size() == index.files.size());
    for (std::size_t i = 0; i < index.files.size(); ++i)
        CHECK (again.files[i].relativePath == index.files[i].relativePath);
}
