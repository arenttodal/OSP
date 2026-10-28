// The unified-engine baseline, plugin side (docs/research/unified-andor, Phase 0): sessions
// with non-default granular settings, saved and reopened through ANDOR OSP's own parameters
// and state, rendered and pinned before KEY DNA touched the granulator. The engine-side
// scenes are in tests/regression/UnifiedBaselineTests.cpp.
//
// Exact sample hashes are compared on the platform that stored them (their "platform");
// elsewhere level, peak and brightness must match closely. Regenerate only after an
// intentional DSP change: OSP_UPDATE_GOLDEN=1 osp_plugin_tests "[unified-baseline]".
// Included at the end of PluginProcessorTests.cpp (it reuses the Reimagined scenes' renderer).

#include "core/PlatformTag.h"

namespace
{
    std::vector<ReimaginedScene> unifiedPluginScenes()
    {
        using V = std::vector<std::pair<const char*, float>>;
        return {
            { "plugin-granular-textured", 1,
              V { { "layerA.sourceMode", 1.0f }, { "layerA.granular.position", 72.0f }, { "layerA.granular.size", 320.0f },
                  { "layerA.granular.density", 32.0f }, { "layerA.granular.tune", 7.0f }, { "layerA.granular.spread", 60.0f },
                  { "layerA.start", 10.0f }, { "layerA.reverse", 1.0f }, { "layerA.follow", 0.0f }, { "life", 80.0f }, { "life.mode", 1.0f },
                  { "reimagined", 40.0f }, { "space", 35.0f } },
              false, true },
            { "plugin-granular-two-layers", 2,
              V { { "layerA.sourceMode", 1.0f }, { "layerA.granular.position", 30.0f }, { "layerA.granular.size", 80.0f },
                  { "layerA.granular.density", 25.0f }, { "layerA.granular.spread", 40.0f }, { "layerB.sourceMode", 1.0f },
                  { "layerB.granular.position", 60.0f }, { "layerB.granular.size", 250.0f }, { "layerB.granular.density", 8.0f },
                  { "layerB.granular.tune", -12.0f }, { "ab.blend", 0.5f }, { "life", 60.0f }, { "life.mode", 2.0f } },
              false, true },
            { "plugin-three-layers-mixed", 3,
              V { { "layerA.sourceMode", 1.0f }, { "layerC.sourceMode", 1.0f }, { "layerC.granular.position", 85.0f },
                  { "layerC.granular.size", 40.0f }, { "layerC.granular.density", 36.0f }, { "layerC.level", -3.0f }, { "layerB.pan", -40.0f } },
              false, true },
        };
    }
}

TEST_CASE ("plugin: granular sessions render as they did before KEY DNA (unified baseline)", "[plugin][unified-baseline]")
{
    const bool update = juce::SystemStats::getEnvironmentVariable ("OSP_UPDATE_GOLDEN", {}) == "1";
    const auto wavDir = juce::SystemStats::getEnvironmentVariable ("OSP_UNIFIED_WAV_DIR", {});
    const auto platform = juce::String (osp::platformTag());
    const auto dir = juce::File (OSP_SOURCE_DIR).getChildFile ("tests/audio/unified-baseline");
    for (const auto& scene : unifiedPluginScenes())
    {
        DYNAMIC_SECTION (scene.name)
        {
            TempDir tmp;
            const auto result = renderScene (scene, tmp.dir);
            const auto again = renderScene (scene, tmp.dir);
            REQUIRE (again.hash == result.hash);   // deterministic
            CHECK (result.rmsDb > -60.0);
            const auto file = dir.getChildFile (juce::String (scene.name) + ".json");
            if (wavDir.isNotEmpty())
            {
                std::string error;
                const auto wav = juce::File (wavDir).getChildFile (juce::String (scene.name) + ".wav");
                CHECK (io::writeAudioFile (std::filesystem::path (wav.getFullPathName().toStdString()), result.audio, io::SampleFormat::float32, error));
            }
            if (update)
            {
                auto* o = new juce::DynamicObject();
                o->setProperty ("schemaVersion", 1);
                o->setProperty ("scene", scene.name);
                o->setProperty ("platform", platform);
                o->setProperty ("sampleHash", result.hash);
                o->setProperty ("rmsDb", result.rmsDb);
                o->setProperty ("peakDb", result.peakDb);
                o->setProperty ("brightness", result.brightness);
                dir.createDirectory();
                REQUIRE (file.replaceWithText (juce::JSON::toString (juce::var (o))));
                continue;
            }
            const auto reference = juce::JSON::parse (file);
            INFO ("missing reference " << file.getFullPathName() << " (OSP_UPDATE_GOLDEN=1 writes it)");
            REQUIRE (reference.isObject());
            INFO (scene.name << ": rms " << result.rmsDb << " dB, peak " << result.peakDb << " dB, brightness " << result.brightness << ", hash "
                             << result.hash);
            if (reference["platform"].toString() == platform)
                CHECK (reference["sampleHash"].toString() == result.hash);
            else if (reference["sampleHash"].toString() != result.hash)
                WARN (scene.name << ": not bit-identical to the " << reference["platform"].toString() << " baseline (this is " << platform << ")");
            CHECK (result.rmsDb == Approx (static_cast<double> (reference["rmsDb"])).margin (0.05));
            CHECK (result.peakDb == Approx (static_cast<double> (reference["peakDb"])).margin (0.1));
            CHECK (result.brightness == Approx (static_cast<double> (reference["brightness"])).epsilon (0.01));
        }
    }
}
