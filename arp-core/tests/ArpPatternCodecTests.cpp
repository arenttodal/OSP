// andor_arp_core: the pure C++ `.andorpattern` codec. The fixtures were written by ANDOR OSP's JUCE
// codec (tests/unit/ArpPatternCodecCompatTests.cpp keeps them current), so reading them here proves
// a pattern saved by ANDOR OSP or ANDOR ANALOG opens unchanged in ANDOR ARP, which has no JUCE.

#include "PatternCorpus.h"

#include "engine/Arpeggiator.h"
#include "model/ArpPatternCodec.h"

#include <catch2/catch_test_macros.hpp>

#include <clocale>
#include <filesystem>
#include <fstream>
#include <locale>
#include <sstream>

using namespace osp;
using namespace osp::arppattern;

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

    /** Every event the pattern plays over a held chord for 16 steps (the musical interpretation). */
    std::string render (const Document& d)
    {
        Arpeggiator arp;
        arp.prepare (48000.0);
        arp.setSeed (7);
        arp.setCustomPattern (compile (d.pattern));
        Arpeggiator::Settings s;
        s.enabled = true;
        s.custom = true;
        s.rate = ArpRate::sixteenth;
        s.octaves = 2;
        HostTiming t;
        std::ostringstream out;
        const int block = 256;
        for (int b = 0; b < 16 * 1500 / block + 2; ++b)
        {
            arp.beginBlock (s, t, block);
            if (b == 0)
                for (int n : { 48, 55, 60, 64 })
                    arp.noteOn (0, n, 100, 1);
            arp.endBlock();
            for (int i = 0; i < arp.numEvents(); ++i)
            {
                const auto& e = arp.event (i);
                out << b * block + e.offset << ':' << static_cast<int> (e.kind) << ':' << int (e.note) << ':' << int (e.velocity) << ' ';
            }
        }
        return out.str();
    }
}

TEST_CASE ("Pattern codec: every corpus fixture written by ANDOR OSP reads back as the same document", "[unit][arp-codec]")
{
    for (const auto& want : andor_arp_test::patternCorpus())
    {
        const auto file = fixtureDir() / (andor_arp_test::fixtureStem (want) + ".andorpattern");
        INFO (file.string());
        REQUIRE (std::filesystem::exists (file));
        std::string error;
        const auto got = documentFromText (readText (file), error);
        INFO (error);
        REQUIRE (got.has_value());
        CHECK (*got == want);
        CHECK (compile (got->pattern) == compile (want.pattern));
        CHECK (render (*got) == render (want));
        CHECK_FALSE (render (want).empty());
    }
}

TEST_CASE ("Pattern codec: its own text round trips every corpus document exactly", "[unit][arp-codec]")
{
    for (const auto& d : andor_arp_test::patternCorpus())
    {
        std::string error;
        const auto text = documentToText (d);
        const auto again = documentFromText (text, error);
        INFO (d.id << ": " << error);
        REQUIRE (again.has_value());
        CHECK (*again == d);
        CHECK (documentToText (*again) == text);   // stable: saving again changes nothing
    }
}

TEST_CASE ("Pattern codec: newer schemas, other kinds and invalid patterns are refused, not misread", "[unit][arp-codec]")
{
    const auto d = arppattern::factoryPatterns().front();
    std::string error;

    auto json = documentToJson (d);
    json.set ("schemaVersion", JsonValue::makeInt (Document::schemaVersion + 1));
    CHECK_FALSE (documentFromJson (json, error).has_value());
    CHECK (error.find ("newer") != std::string::npos);

    json = documentToJson (d);
    json.set ("schemaVersion", JsonValue::makeInt (0));
    CHECK_FALSE (documentFromJson (json, error).has_value());

    json = documentToJson (d);
    json.set ("kind", JsonValue::makeString ("andor.something-else"));
    CHECK_FALSE (documentFromJson (json, error).has_value());

    auto broken = d;
    broken.pattern.events.clear();
    CHECK_FALSE (documentFromJson (documentToJson (broken), error).has_value());
    broken = d;
    broken.pattern.events[0].duration = arp::minEventTicks - 1;
    CHECK_FALSE (documentFromJson (documentToJson (broken), error).has_value());

    CHECK_FALSE (documentFromText ("{ not json", error).has_value());
    CHECK_FALSE (documentFromText ("[1, 2]", error).has_value());
    CHECK_FALSE (documentFromText (documentToText (d) + " x", error).has_value());   // text after the value

    // Unknown fields are ignored (a newer minor addition still opens).
    json = documentToJson (d);
    json.set ("futureField", JsonValue::makeString ("ignored"));
    const auto withExtra = documentFromJson (json, error);
    REQUIRE (withExtra.has_value());
    CHECK (*withExtra == d);
}

TEST_CASE ("Pattern codec: JSON details (escapes, surrogate pairs, BOM, nesting, numbers)", "[unit][arp-codec]")
{
    std::string error;
    const auto v = parseJson (R"({"a":"é🎹\n\"","b":-0.5e1,"c":[1,2.25,true,null],"a":"last"})", error);
    REQUIRE (v.has_value());
    CHECK ((*v)["a"].text == "last");   // a repeated key: the last wins
    CHECK ((*v)["b"].number == -5.0);
    CHECK_FALSE ((*v)["b"].integer);
    CHECK ((*v)["c"].items.size() == 4);
    CHECK ((*v)["c"].items[0].integer);

    const auto s = parseJson (R"("é🎹")", error);
    REQUIRE (s.has_value());
    CHECK (s->text == "\xC3\xA9\xF0\x9F\x8E\xB9");

    CHECK (parseJson ("\xEF\xBB\xBF{}", error).has_value());
    CHECK_FALSE (parseJson (std::string (100, '[') + std::string (100, ']'), error).has_value());
    CHECK_FALSE (parseJson ("01x", error).has_value());
    CHECK_FALSE (parseJson ("\"a\nb\"", error).has_value());   // a raw control character

    // Numbers: six decimals, never a decimal comma, never "-0".
    auto o = JsonValue::makeObject();
    o.set ("x", JsonValue::makeDouble (1.0 / 3.0));
    o.set ("y", JsonValue::makeDouble (2.0));
    o.set ("z", JsonValue::makeDouble (-0.0000001));
    o.set ("i", JsonValue::makeInt (-42));
    const auto text = writeJson (o);
    CHECK (text.find ("0.333333") != std::string::npos);
    CHECK (text.find ("2.0") != std::string::npos);
    CHECK (text.find ("-0.0") == std::string::npos);
    CHECK (text.find ("-42") != std::string::npos);
}

TEST_CASE ("Pattern codec: a host running with a decimal-comma locale reads and writes the same text", "[unit][arp-codec]")
{
    const auto d = andor_arp_test::patternCorpus().back();
    const auto before = documentToText (d);

    // Live and DAWs run in the user's locale. The C library's and the C++ global locale are both
    // switched where the system has a decimal-comma locale; the text must not change.
    const char* names[] = { "de_DE.UTF-8", "de_DE.utf8", "fr_FR.UTF-8", "nb_NO.UTF-8" };
    bool switched = false;
    std::locale previous;
    for (const char* name : names)
    {
        try
        {
            std::locale l (name);
            previous = std::locale::global (l);
            std::setlocale (LC_ALL, name);
            switched = true;
            break;
        }
        catch (...)
        {
        }
    }
    const auto during = documentToText (d);
    std::string error;
    const auto read = documentFromText (before, error);
    if (switched)
    {
        std::locale::global (previous);
        std::setlocale (LC_ALL, "C");
    }
    else
        WARN ("no decimal-comma locale installed: checked in the classic locale only");
    CHECK (during == before);
    REQUIRE (read.has_value());
    CHECK (*read == d);
}
