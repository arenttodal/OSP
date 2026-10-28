// The unified-engine baseline (docs/research/unified-andor, Phase 0): OSP's existing One
// Shot and Granular playback, headless, pinned before KEY DNA (and every later phase)
// touched the granulator.
//
// Each scene (src/research/UnifiedScenes.cpp) is rendered and compared with
// tests/regression/golden/unified/<scene>.json:
//  - on the platform that stored the digest (its "platform", e.g. linux-x86_64-gcc) the
//    samples must be bit-identical (the exact hash);
//  - elsewhere other compilers and FPUs round differently, so the level, peak, brightness
//    and width must match closely and a differing hash is reported, not failed.
// Rendering twice in one build must always be bit-identical.
//
// The digests were captured from the build before KEY DNA existed. Regenerate only after an
// intentional DSP change, and explain it in the commit message:
//     OSP_UPDATE_GOLDEN=1 ./build/tests/osp_tests "[unified-baseline]"

#include "io/JsonUtil.h"
#include "research/UnifiedScenes.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cstdlib>
#include <filesystem>

using Catch::Approx;
using namespace osp;
using namespace osp::research;

namespace
{
    std::filesystem::path unifiedGoldenPath (const std::string& name)
    {
        return std::filesystem::path (OSP_SOURCE_DIR) / "tests" / "regression" / "golden" / "unified" / (name + ".json");
    }

    bool updateGolden()
    {
        const char* v = std::getenv ("OSP_UPDATE_GOLDEN");
        return v != nullptr && std::string (v) == "1";
    }
}

TEST_CASE ("unified baseline: OSP's One Shot and Granular playback render as they did before KEY DNA", "[regression][unified-baseline]")
{
    const auto scenes = unified::baselineScenes();
    REQUIRE (scenes.size() >= 15);
    const auto platform = unified::platformTag();
    for (const auto& scene : scenes)
    {
        DYNAMIC_SECTION (scene.name)
        {
            const auto first = unified::render (scene);
            const auto again = unified::render (scene);
            const auto d = unified::digest (first.audio);
            // Deterministic: the same inputs give the same samples.
            REQUIRE (unified::digest (again.audio).hash == d.hash);
            // Not silent, not clipped: a scene that renders nothing proves nothing.
            CHECK (d.rmsDb > -60.0);
            CHECK (d.peakDb < 0.0);

            const auto path = unifiedGoldenPath (scene.name);
            if (updateGolden())
            {
                std::filesystem::create_directories (path.parent_path());
                std::string error;
                REQUIRE (json::writeFile (path, unified::digestToJson (scene, d, platform), error));
                continue;
            }
            std::string error;
            const auto stored = json::readFile (path, error);
            INFO ("missing or unreadable golden " << path.string() << " (" << error << "); run with OSP_UPDATE_GOLDEN=1");
            REQUIRE (stored.has_value());
            const auto& golden = *stored;
            INFO (scene.name << ": rms " << d.rmsDb << " dB, peak " << d.peakDb << " dB, brightness " << d.brightness << ", width "
                             << d.stereoWidth << ", hash " << d.hash);
            const auto storedPlatform = json::getString (golden, "platform");
            if (storedPlatform == platform)
                CHECK (json::getString (golden, "sampleHash") == d.hash);
            else if (json::getString (golden, "sampleHash") != d.hash)
                WARN (scene.name << ": not bit-identical to the " << storedPlatform << " baseline (this is " << platform
                                 << "); comparing level, peak, brightness and width");
            CHECK (d.seconds == Approx (json::getDouble (golden, "seconds", -1.0e9)).margin (1.0e-6));
            CHECK (d.rmsDb == Approx (json::getDouble (golden, "rmsDb", -1.0e9)).margin (0.05));
            CHECK (d.peakDb == Approx (json::getDouble (golden, "peakDb", -1.0e9)).margin (0.1));
            CHECK (d.brightness == Approx (json::getDouble (golden, "brightness", -1.0e9)).epsilon (0.01));
            CHECK (d.stereoWidth == Approx (json::getDouble (golden, "stereoWidth", -1.0e9)).epsilon (0.02).margin (1.0e-4));
        }
    }
}

TEST_CASE ("KEY DNA demonstrations: A is the unmodified Granulator, B and C differ, all as stored", "[regression][unified-baseline][keydna]")
{
    // The Phase 1 demonstration renders (research-renderer --unified <dir> --set keydna): pinned
    // like the baseline, so later phases can tell when they change.
    const auto scenes = unified::keyDnaScenes();
    REQUIRE (scenes.size() == 15);
    const auto platform = unified::platformTag();
    for (const auto& scene : scenes)
    {
        DYNAMIC_SECTION (scene.name)
        {
            const auto d = unified::digest (unified::render (scene).audio);
            REQUIRE (unified::digest (unified::render (scene).audio).hash == d.hash);
            auto plain = scene;
            plain.config.keyDna = {};
            const auto withoutDna = unified::digest (unified::render (plain).audio).hash;
            if (scene.name.find ("-a-static-") != std::string::npos)
                CHECK (d.hash == withoutDna);   // A: exactly the Granulator without KEY DNA
            else
                CHECK (d.hash != withoutDna);   // B and C: the profile is heard
            const auto path = std::filesystem::path (OSP_SOURCE_DIR) / "tests" / "regression" / "golden" / "keydna" / (scene.name + ".json");
            if (updateGolden())
            {
                std::filesystem::create_directories (path.parent_path());
                std::string error;
                REQUIRE (json::writeFile (path, unified::digestToJson (scene, d, platform), error));
                continue;
            }
            std::string error;
            const auto stored = json::readFile (path, error);
            INFO ("missing golden " << path.string() << "; run with OSP_UPDATE_GOLDEN=1");
            REQUIRE (stored.has_value());
            const auto& golden = *stored;
            if (json::getString (golden, "platform") == platform)
                CHECK (json::getString (golden, "sampleHash") == d.hash);
            else if (json::getString (golden, "sampleHash") != d.hash)
                WARN (scene.name << ": not bit-identical to the " << json::getString (golden, "platform") << " render (this is " << platform << ")");
            CHECK (d.rmsDb == Approx (json::getDouble (golden, "rmsDb", -1.0e9)).margin (0.05));
            CHECK (d.peakDb == Approx (json::getDouble (golden, "peakDb", -1.0e9)).margin (0.1));
            CHECK (d.brightness == Approx (json::getDouble (golden, "brightness", -1.0e9)).epsilon (0.01));
        }
    }
}

TEST_CASE ("Phase 2 demonstrations: the grain options, live KEY DNA AMOUNT and the budget, as stored", "[regression][phase2-demos]")
{
    // research-renderer --unified <dir> --set phase2, pinned like the KEY DNA demonstrations.
    const auto scenes = unified::phase2Scenes();
    REQUIRE (scenes.size() == 16);
    const auto platform = unified::platformTag();
    for (const auto& scene : scenes)
    {
        DYNAMIC_SECTION (scene.name)
        {
            const auto d = unified::digest (unified::render (scene).audio);
            REQUIRE (unified::digest (unified::render (scene).audio).hash == d.hash);
            const auto path = std::filesystem::path (OSP_SOURCE_DIR) / "tests" / "regression" / "golden" / "phase2" / (scene.name + ".json");
            if (updateGolden())
            {
                std::filesystem::create_directories (path.parent_path());
                std::string error;
                REQUIRE (json::writeFile (path, unified::digestToJson (scene, d, platform), error));
                continue;
            }
            std::string error;
            const auto stored = json::readFile (path, error);
            INFO ("missing golden " << path.string() << "; run with OSP_UPDATE_GOLDEN=1");
            REQUIRE (stored.has_value());
            const auto& golden = *stored;
            if (json::getString (golden, "platform") == platform)
                CHECK (json::getString (golden, "sampleHash") == d.hash);
            else if (json::getString (golden, "sampleHash") != d.hash)
                WARN (scene.name << ": not bit-identical to the " << json::getString (golden, "platform") << " render (this is " << platform << ")");
            CHECK (d.rmsDb == Approx (json::getDouble (golden, "rmsDb", -1.0e9)).margin (0.05));
            CHECK (d.peakDb == Approx (json::getDouble (golden, "peakDb", -1.0e9)).margin (0.1));
            CHECK (d.brightness == Approx (json::getDouble (golden, "brightness", -1.0e9)).epsilon (0.01));
        }
    }
}

TEST_CASE ("Phase 3 demonstrations: Note Atlases through OSP's voices, as stored", "[regression][phase3-demos]")
{
    // research-renderer --unified <dir> --set atlas, pinned like the Phase 2 demonstrations
    // (the 50-region atlas is a cost scenario only: it takes a while to build).
    const auto scenes = unified::atlasScenes();
    REQUIRE (scenes.size() == 22);
    const auto platform = unified::platformTag();
    for (const auto& scene : scenes)
    {
        if (scene.name == "atlas-regions-50-chord8")
            continue;
        DYNAMIC_SECTION (scene.name)
        {
            const auto d = unified::digest (unified::render (scene).audio);
            REQUIRE (unified::digest (unified::render (scene).audio).hash == d.hash);
            const auto path = std::filesystem::path (OSP_SOURCE_DIR) / "tests" / "regression" / "golden" / "phase3" / (scene.name + ".json");
            if (updateGolden())
            {
                std::filesystem::create_directories (path.parent_path());
                std::string error;
                REQUIRE (json::writeFile (path, unified::digestToJson (scene, d, platform), error));
                continue;
            }
            std::string error;
            const auto stored = json::readFile (path, error);
            INFO ("missing golden " << path.string() << "; run with OSP_UPDATE_GOLDEN=1");
            REQUIRE (stored.has_value());
            const auto& golden = *stored;
            if (json::getString (golden, "platform") == platform)
                CHECK (json::getString (golden, "sampleHash") == d.hash);
            else if (json::getString (golden, "sampleHash") != d.hash)
                WARN (scene.name << ": not bit-identical to the " << json::getString (golden, "platform") << " render (this is " << platform << ")");
            CHECK (d.rmsDb == Approx (json::getDouble (golden, "rmsDb", -1.0e9)).margin (0.05));
            CHECK (d.peakDb == Approx (json::getDouble (golden, "peakDb", -1.0e9)).margin (0.1));
            CHECK (d.brightness == Approx (json::getDouble (golden, "brightness", -1.0e9)).epsilon (0.01));
        }
    }
}
