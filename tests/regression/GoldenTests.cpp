// Golden render regression tests.
//
// Each case renders a synthetic source through a fixture and compares robust signal
// metrics with tests/regression/golden/<case>.json. Byte-identical output is NOT
// required across platforms/compilers (the stored sample hash is informational);
// within one build, rendering twice must be bit-identical.
//
// Regenerate after an *intentional* DSP change:
//     OSP_UPDATE_GOLDEN=1 ./build/tests/osp_tests "[regression]"
// and explain the change in the commit message.

#include "audio/utility/TestSignals.h"
#include "core/PitchMath.h"
#include "io/JsonUtil.h"
#include "midi/MidiFixtures.h"
#include "research/RenderMetrics.h"
#include "research/RenderSession.h"
#include "support/TestHelpers.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cstdlib>
#include <functional>

using Catch::Approx;
using namespace osp;
using namespace osp::research;

namespace
{
    struct GoldenCase
    {
        std::string name;
        std::function<AudioData()> source;
        double sourceHz;
        std::string fixture;
        EngineId engine;
        std::uint64_t seed = 1;
        double outputRate = 48000.0;
    };

    std::vector<GoldenCase> goldenCases()
    {
        const double a3 = midiToHz (57);
        const double e2 = midiToHz (40);
        const double c4 = midiToHz (60);
        auto vowel = [a3] { return testsignals::vowel (a3, 3.0, 48000.0, 21); };
        auto pluck = [e2] { return testsignals::pluck (e2, 2.5, 44100.0, 8, 0.7, 1); };
        auto saw = [c4] {
            auto audio = testsignals::saw (c4, 2.0, 96000.0, 0.4, 2);
            testsignals::applyFades (audio, 0.003, 0.05);
            return audio;
        };
        return {
            { "vowel-repetition-a", vowel, a3, "repetition", EngineId::baselineA },
            { "vowel-repetition-b", vowel, a3, "repetition", EngineId::baselineB, 7 },
            { "vowel-register-a", vowel, a3, "register", EngineId::baselineA },
            { "vowel-chords-a", vowel, a3, "chords", EngineId::baselineA },
            { "pluck-dynamics-a", pluck, e2, "dynamics", EngineId::baselineA },
            { "pluck-melody-b", pluck, e2, "melody", EngineId::baselineB, 3 },
            { "saw96k-register-a-44k1", saw, c4, "register", EngineId::baselineA, 1, 44100.0 },
            { "saw96k-melody-a", saw, c4, "melody", EngineId::baselineA },
        };
    }

    std::filesystem::path goldenPath (const std::string& name)
    {
        return std::filesystem::path (OSP_SOURCE_DIR) / "tests" / "regression" / "golden" / (name + ".json");
    }

    bool updateMode()
    {
        const char* v = std::getenv ("OSP_UPDATE_GOLDEN");
        return v != nullptr && std::string (v) == "1";
    }
}

TEST_CASE ("golden renders: deterministic and within tolerance of stored metrics", "[regression][golden]")
{
    for (const auto& gc : goldenCases())
    {
        DYNAMIC_SECTION (gc.name)
        {
            const auto source = gc.source();
            const double rootMidi = hzToMidi (gc.sourceHz);
            const auto sequence = *fixtures::byName (gc.fixture, static_cast<int> (std::lround (rootMidi)));

            RenderConfig config;
            config.engine = gc.engine;
            config.sampleRate = gc.outputRate;
            config.sampler.seed = gc.seed;

            const auto first = renderSequence (source, rootMidi, sequence, config);
            const auto second = renderSequence (source, rootMidi, sequence, config);
            REQUIRE (first.audio.numFrames() == second.audio.numFrames());
            REQUIRE (test::maxDifference (first.audio, second.audio) == 0.0); // non-determinism

            MetricsContext context;
            context.expectedSampleRate = gc.outputRate;
            context.sequence = &sequence;
            context.sourceF0Hz = gc.sourceHz;
            context.rootMidi = rootMidi;
            const auto m = computeMetrics (first.audio, context);

            // Absolute safety checks, independent of any stored golden.
            for (const auto& issue : m.issues)
                INFO (issue.code << ": " << issue.message);
            CHECK (m.status() == "ok");
            CHECK (m.nanCount == 0);
            CHECK (m.infCount == 0);
            CHECK (m.clippedSamples == 0);
            CHECK (m.peakDbfs > -40.0);
            CHECK (m.maxAbsCentsError < 10.0);

            const auto path = goldenPath (gc.name);
            if (updateMode())
            {
                auto g = metricsToJson (m);
                json::set (g, "case", json::str (gc.name));
                json::set (g, "fixture", json::str (gc.fixture));
                json::set (g, "fixtureVersion", fixtures::fixtureVersion);
                json::set (g, "engine", json::str (engineName (gc.engine)));
                json::set (g, "seed", static_cast<juce::int64> (gc.seed));
                std::string error;
                REQUIRE (json::writeFile (path, g, error));
                WARN ("updated " << path.string());
                continue;
            }

            std::string error;
            const auto golden = json::readFile (path, error);
            INFO ("missing golden; run with OSP_UPDATE_GOLDEN=1 to create: " << path.string());
            REQUIRE (golden);

            CHECK (json::getInt (*golden, "fixtureVersion", 0) == fixtures::fixtureVersion);
            CHECK (static_cast<std::int64_t> (json::getDouble (*golden, "frames", 0.0)) == m.frames); // broken duration
            CHECK (m.peakDbfs == Approx (json::getDouble (*golden, "peakDbfs", 0.0)).margin (0.5));
            CHECK (m.rmsDbfs == Approx (json::getDouble (*golden, "rmsDbfs", 0.0)).margin (0.5)); // level change
            CHECK (m.centroidMeanHz == Approx (json::getDouble (*golden, "centroidMeanHz", 0.0)).epsilon (0.03));

            const auto* goldenNotes = (*golden)["notes"].getArray();
            REQUIRE (goldenNotes != nullptr);
            REQUIRE (static_cast<std::size_t> (goldenNotes->size()) == m.notes.size());
            for (std::size_t i = 0; i < m.notes.size(); ++i)
            {
                const double was = json::getDouble ((*goldenNotes)[static_cast<int> (i)], "detectedHz", 0.0);
                if (was > 0.0 && m.notes[i].detectedHz > 0.0)
                    CHECK (std::abs (centsBetween (was, m.notes[i].detectedHz)) < 5.0); // note frequency drift
            }

            if (json::getString (*golden, "sampleHash") != m.sampleHash)
                WARN (gc.name << ": output is not bit-identical to the stored golden (expected across platforms; "
                              "investigate if this is the reference platform)");
        }
    }
}
