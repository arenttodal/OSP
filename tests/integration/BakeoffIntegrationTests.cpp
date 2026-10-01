#include "audio/utility/TestSignals.h"
#include "core/PitchMath.h"
#include "io/AudioFileIO.h"
#include "io/JsonUtil.h"
#include "research/Bakeoff.h"
#include "support/TestHelpers.h"

#include <catch2/catch_test_macros.hpp>

#include <fstream>
#include <set>

using namespace osp;
using namespace osp::research;

TEST_CASE ("bake-off: blind clips, key, scoring", "[integration][bakeoff]")
{
    test::TempDir dir;
    std::string error;
    REQUIRE (io::writeAudioFile (dir / "corpus" / "vowel.wav", testsignals::vowel (midiToHz (57), 2.5, 48000.0, 9), io::SampleFormat::pcm24, error));

    BakeoffPlan plan;
    plan.name = "test";
    plan.corpusRoot = dir / "corpus";
    plan.sources = { { "vocal", "vowel.wav" }, { "missing", "nope.wav" } };
    plan.offsets = { -12.0, 12.0 };
    plan.clipSeconds = 1.0;

    const auto summary = runBakeoff (plan, dir / "out");
    CHECK (summary.groups == 2);
    CHECK (summary.clips == 6);
    CHECK (summary.errors.size() == 1); // the missing source is reported, not fatal

    const auto key = json::readFile (summary.keyFile, error);
    REQUIRE (key);
    const auto* clips = (*key)["clips"].getArray();
    REQUIRE (clips != nullptr);
    std::set<std::string> engines;
    juce::var ratings = json::object();
    auto items = json::array();
    for (const auto& c : *clips)
    {
        engines.insert (json::getString (c, "engine"));
        CHECK (std::filesystem::exists (summary.clipsDir / (json::getString (c, "id") + ".wav")));
        // Every engine must land on the requested pitch.
        CHECK (std::abs (json::getDouble (c, "pitchErrorCents", 999.0)) < 20.0);
        auto r = json::object();
        json::set (r, "clip", c["id"]);
        json::set (r, "identity", json::getString (c, "engine") == "A" ? 5 : 3);
        json::set (r, "beauty", 4);
        json::set (r, "artifacts", 4);
        json::set (r, "best", json::getString (c, "engine") == "A");
        items.append (r);
    }
    CHECK (engines == std::set<std::string> { "A", "B", "C" });

    // The listener-facing file never names engines.
    std::ifstream listening (dir / "out" / "listening.json");
    const std::string text ((std::istreambuf_iterator<char> (listening)), std::istreambuf_iterator<char>());
    CHECK (text.find ("\"engine\"") == std::string::npos);

    // Same seed -> same names and order.
    const auto again = runBakeoff (plan, dir / "out2");
    const auto key2 = json::readFile (again.keyFile, error);
    REQUIRE (key2);
    CHECK (json::getString ((*key)["clips"][0], "id") == json::getString ((*key2)["clips"][0], "id"));

    json::set (ratings, "ratings", items);
    REQUIRE (json::writeFile (dir / "ratings.json", ratings, error));
    std::string report;
    REQUIRE (scoreBakeoff (dir / "out", dir / "ratings.json", report, error));
    CHECK (report.find ("| A | 2 | 5 |") != std::string::npos);
    CHECK (std::filesystem::exists (dir / "out" / "score.md"));
}
