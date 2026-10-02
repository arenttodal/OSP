#include "audio/utility/TestSignals.h"
#include "io/AnalysisJson.h"
#include "io/JsonUtil.h"
#include "research/RenderConfig.h"
#include "support/TestHelpers.h"

#include <catch2/catch_approx.hpp>
#include <fstream>
#include <catch2/catch_test_macros.hpp>

using Catch::Approx;
using namespace osp;

TEST_CASE ("JSON: analysis report round-trips", "[unit][json]")
{
    auto analysis = test::analyse (testsignals::vowel (220.0, 2.0, 48000.0, 3));
    analysis.source.filename = "vowel [take 2].wav";
    analysis.source.contentHash = "sha256:abc";
    analysis.warnings.push_back ("a \"quoted\" warning");

    const auto json = io::analysisToJson (analysis);
    const auto text = json::toString (json);
    std::string error;
    const auto parsed = json::parse (text, error);
    REQUIRE (parsed);
    const auto back = io::analysisFromJson (*parsed, error);
    REQUIRE (back);

    CHECK (back->schemaVersion == analysisSchemaVersion);
    CHECK (back->source.filename == "vowel [take 2].wav");
    CHECK (back->pitch.midiNote == analysis.pitch.midiNote);
    CHECK (back->pitch.fundamentalHz == Approx (analysis.pitch.fundamentalHz).margin (0.001));
    CHECK (back->pitch.confidenceLevel == analysis.pitch.confidenceLevel);
    CHECK (back->pitch.vibrato.has_value() == analysis.pitch.vibrato.has_value());
    CHECK (back->envelope.peakDbfs == Approx (analysis.envelope.peakDbfs).margin (0.01));
    CHECK (back->spectral.meanCentroidHz == Approx (analysis.spectral.meanCentroidHz).margin (0.1));
    CHECK (back->stereo.width == Approx (analysis.stereo.width).margin (1e-4));
    CHECK (back->warnings == analysis.warnings);
    CHECK (back->pitch.trackHz.values.size() == analysis.pitch.trackHz.values.size());
    CHECK (back->envelope.rmsDb.values.size() == analysis.envelope.rmsDb.values.size());
    CHECK (back->envelope.rmsDb.hopSeconds == Approx (analysis.envelope.rmsDb.hopSeconds));
}

TEST_CASE ("JSON: schema version is enforced", "[unit][json]")
{
    std::string error;
    auto noVersion = json::object();
    CHECK_FALSE (io::analysisFromJson (noVersion, error));
    CHECK (error.find ("schemaVersion") != std::string::npos);

    auto future = json::object();
    json::set (future, "schemaVersion", analysisSchemaVersion + 1);
    CHECK_FALSE (io::analysisFromJson (future, error));
    CHECK (error.find ("newer") != std::string::npos);

    CHECK_FALSE (json::parse ("{ not json", error));
}

TEST_CASE ("JSON: undetected pitch is written as null, not a fabricated value", "[unit][json]")
{
    const auto analysis = test::analyse (testsignals::silence (0.5, 48000.0));
    const auto json = io::analysisToJson (analysis);
    CHECK (json["pitch"]["fundamentalHz"].isVoid());
    CHECK (json["pitch"]["midiNote"].isVoid());
    CHECK_FALSE (static_cast<bool> (json["pitch"]["detected"]));
}

TEST_CASE ("JSON: numeric arrays are compact and strings are untouched", "[unit][json]")
{
    auto obj = json::object();
    json::set (obj, "values", json::floatArray ({ 1.0f, 2.5f, -3.25f }, 2));
    json::set (obj, "name", json::str ("odd [1,  2] name"));
    const auto text = json::toString (obj);
    CHECK (text.find ("[1.0, 2.5, -3.25]") != std::string::npos);
    CHECK (text.find ("\"odd [1,  2] name\"") != std::string::npos);
}

TEST_CASE ("JSON: render config loads with defaults for missing fields", "[unit][json]")
{
    test::TempDir dir;
    auto cfg = json::object();
    json::set (cfg, "schemaVersion", 1);
    json::set (cfg, "engine", json::str ("B"));
    auto sampler = json::object();
    json::set (sampler, "releaseSeconds", 0.5);
    json::set (cfg, "sampler", sampler);
    std::string error;
    REQUIRE (json::writeFile (dir / "c.json", cfg, error));

    const auto config = research::loadRenderConfig (dir / "c.json", error);
    REQUIRE (config);
    CHECK (config->engine == research::EngineId::baselineB);
    CHECK (config->sampler.adsr.releaseSeconds == Approx (0.5));
    CHECK (config->sampler.polyphony == 24);
    CHECK (config->effectiveSamplerSettings().randomization.enabled);

    json::set (cfg, "engine", json::str ("Z"));
    REQUIRE (json::writeFile (dir / "bad.json", cfg, error));
    CHECK_FALSE (research::loadRenderConfig (dir / "bad.json", error));
}

TEST_CASE ("JSON: committed research configs parse", "[unit][json]")
{
    for (const auto* name : { "baseline-a.json", "baseline-b.json", "prepared-a.json" })
    {
        std::string error;
        const auto config = research::loadRenderConfig (std::filesystem::path (OSP_SOURCE_DIR) / "research" / "configs" / name, error);
        INFO (error);
        CHECK (config.has_value());
    }
}

TEST_CASE ("JSON: an instrument block can set the shaping (popup) settings", "[unit][json]")
{
    test::TempDir dir;
    const std::string text = R"({ "schemaVersion": 1, "engine": "C", "instrument": {
        "attackSeconds": 0.12,
        "shaping": {
            "life": { "mode": "fray", "pitchCents": 9 },
            "dynamics": { "curve": "hard", "tone": 0.5 },
            "character": { "type": "bp12", "minHz": 3000, "maxHz": 200, "resonance": 0.6 },
            "movement": { "mode": "chorus", "a": 0.2 },
            "space": { "type": "spring", "decaySeconds": 2.5 } } } })";
    std::string error;
    {
        std::ofstream out (dir / "s.json");
        out << text;
    }
    const auto config = research::loadRenderConfig (dir / "s.json", error);
    REQUIRE (config);
    const auto& s = config->engineSettings.shaping;
    CHECK (config->engineSettings.adsr.attackSeconds == Approx (0.12));
    CHECK (s.lifeMode == LifeMode::fray);
    CHECK (s.lifePitchCents == Approx (9.0));
    CHECK (s.lifeTone == Approx (Shaping().lifeTone)); // unspecified: default
    CHECK (s.velocityCurve == VelocityCurve::hard);
    CHECK (s.dynamicsTone == Approx (0.5));
    CHECK (s.filterType == FilterType::bp12);
    CHECK (s.filterMinHz == Approx (3000.0));
    CHECK (s.filterMaxHz == Approx (200.0));
    CHECK (s.resonance == Approx (0.6));
    CHECK (s.movementMode == MovementMode::chorus);
    CHECK (s.movementA == Approx (0.2));
    CHECK (s.spaceType == SpaceType::spring);
    CHECK (s.spaceDecaySeconds == Approx (2.5));

    {
        std::ofstream out (dir / "n.json");
        out << R"({ "schemaVersion": 1, "instrument": { "shaping": { "preset": "neutral" } } })";
    }
    const auto neutral = research::loadRenderConfig (dir / "n.json", error);
    REQUIRE (neutral);
    CHECK (neutral->engineSettings.shaping.filterType == FilterType::off);
}
