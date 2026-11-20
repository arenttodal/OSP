// The `.andorpattern` codec, ANDOR OSP's JUCE side against the pure C++ codec ANDOR ARP uses
// (model/ArpPatternCodec). Both go through one document mapping; this test proves the text each
// writes is read identically by the other, and keeps the corpus fixtures that andor_arp_core's
// standalone tests read (arp-core/tests/fixtures) equal to what ANDOR OSP writes.
//
//     OSP_UPDATE_GOLDEN=1 ./build/tests/osp_tests "[arp-codec-compat]"   rewrites the fixtures

#include "../../arp-core/tests/PatternCorpus.h"

#include "io/ArpPatternJson.h"
#include "io/JsonUtil.h"
#include "model/ArpPatternCodec.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

using namespace osp;

namespace
{
    std::filesystem::path fixtureDir()
    {
        return std::filesystem::path (OSP_SOURCE_DIR) / "arp-core" / "tests" / "fixtures";
    }

    std::string readText (const std::filesystem::path& p)
    {
        std::ifstream in (p, std::ios::binary);
        std::stringstream s;
        s << in.rdbuf();
        return s.str();
    }
}

TEST_CASE ("Pattern codec compatibility: OSP's file text equals the corpus fixtures", "[unit][arp-codec-compat]")
{
    const bool update = std::getenv ("OSP_UPDATE_GOLDEN") != nullptr && std::string (std::getenv ("OSP_UPDATE_GOLDEN")) == "1";
    for (const auto& d : andor_arp_test::patternCorpus())
    {
        const auto file = fixtureDir() / (andor_arp_test::fixtureStem (d) + arpPatternExtension);
        INFO (file.string());
        if (update)
        {
            std::string error;
            REQUIRE (writeArpPattern (file, d, error));
            continue;
        }
        // writeArpPattern's exact bytes: the text and a newline, through
        // juce::File::replaceWithText, whose line feed is "\r\n" on every platform (JUCE's JSON
        // text already ends its lines with juce::newLine, "\r\n"). Strings never hold a raw
        // newline: JSON escapes it.
        std::string expected;
        for (const char c : arpPatternToString (d) + "\n")
            if (c == '\n')
                expected += "\r\n";
            else if (c != '\r')
                expected += c;
        CHECK (readText (file) == expected);
        std::string error;
        const auto read = readArpPattern (file, error);
        REQUIRE (read.has_value());
        CHECK (*read == d);
    }
}

TEST_CASE ("Pattern codec compatibility: each codec reads the other's text as the same document", "[unit][arp-codec-compat]")
{
    for (const auto& d : andor_arp_test::patternCorpus())
    {
        INFO (d.id);
        std::string error;

        // ANDOR ARP writes, ANDOR OSP reads.
        const auto fromPure = arpPatternFromString (arppattern::documentToText (d), error);
        INFO (error);
        REQUIRE (fromPure.has_value());
        CHECK (*fromPure == d);

        // ANDOR OSP writes, ANDOR ARP reads.
        const auto fromJuce = arppattern::documentFromText (arpPatternToString (d), error);
        REQUIRE (fromJuce.has_value());
        CHECK (*fromJuce == d);

        // The same JSON value, whichever side builds it.
        const auto parsed = json::parse (arppattern::documentToText (d), error);
        REQUIRE (parsed.has_value());
        CHECK (json::toString (*parsed) == arpPatternToString (d));
    }
}

TEST_CASE ("Pattern codec compatibility: both refuse a newer schema", "[unit][arp-codec-compat]")
{
    auto j = arppattern::documentToJson (arppattern::factoryPatterns().front());
    j.set ("schemaVersion", arppattern::JsonValue::makeInt (arppattern::Document::schemaVersion + 1));
    const auto text = arppattern::writeJson (j);
    std::string e1, e2;
    CHECK_FALSE (arpPatternFromString (text, e1).has_value());
    CHECK_FALSE (arppattern::documentFromText (text, e2).has_value());
    CHECK (e1 == e2);
}
