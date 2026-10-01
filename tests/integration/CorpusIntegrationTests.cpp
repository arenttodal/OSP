#include "io/JsonUtil.h"
#include "midi/MidiFixtures.h"
#include "research/CorpusRunner.h"
#include "research/TestSignalSet.h"
#include "support/TestHelpers.h"

#include <catch2/catch_test_macros.hpp>

using namespace osp;
using namespace osp::research;

TEST_CASE ("corpus: a mixed folder (including broken files) is fully processed", "[integration][corpus]")
{
    test::TempDir dir;
    std::string error;
    const auto signals = writeTestSignalSet (dir / "corpus", error);
    REQUIRE_FALSE (signals.empty());

    int decodable = 0, supported = 0;
    for (const auto& s : signals)
    {
        decodable += s.expectDecodable ? 1 : 0;
        supported += (s.filename.find (".txt") == std::string::npos) ? 1 : 0;
    }

    CorpusRunOptions options;
    options.corpusDir = dir / "corpus";
    options.reportsDir = dir / "reports";
    options.rendersDir = dir / "renders";
    options.fixtures = fixtures::profile ("quick").value();
    options.profileName = "quick";
    options.engines = { EngineId::baselineA, EngineId::baselineB };
    options.jobs = 3;

    const auto summary = runCorpus (options);
    CHECK (summary.totalFiles == static_cast<int> (signals.size()));
    CHECK (summary.unsupportedFiles == static_cast<int> (signals.size()) - supported);
    CHECK (summary.analysed == decodable);
    CHECK (summary.failed == supported - decodable); // the broken header
    CHECK (summary.rendersError == 0);
    CHECK (summary.rendersOk + summary.rendersWarning == decodable * 2 * 2);

    REQUIRE (std::filesystem::exists (summary.summaryJson));
    REQUIRE (std::filesystem::exists (summary.summaryMarkdown));
    REQUIRE (std::filesystem::exists (dir / "reports" / "index.json"));

    const auto parsed = json::readFile (summary.summaryJson, error);
    REQUIRE (parsed);
    CHECK (json::getInt (*parsed, "schemaVersion", 0) == 1);

    // Every analysed file got a versioned analysis report and renders for both engines.
    int reports = 0, wavs = 0;
    for (const auto& entry : std::filesystem::recursive_directory_iterator (dir / "reports"))
        if (entry.path().filename() == "analysis.json")
        {
            ++reports;
            const auto analysis = json::readFile (entry.path(), error);
            REQUIRE (analysis);
            CHECK (json::getInt (*analysis, "schemaVersion", 0) == 1);
        }
    for (const auto& entry : std::filesystem::recursive_directory_iterator (dir / "renders"))
        wavs += entry.path().extension() == ".wav" ? 1 : 0;
    CHECK (reports == decodable);
    CHECK (wavs == decodable * 2 * 2);

    // A single-threaded, metrics-only run reaches the same per-file outcome.
    options.jobs = 1;
    options.writeAudio = false;
    options.engines = { EngineId::baselineA };
    options.reportsDir = dir / "reports2";
    const auto again = runCorpus (options);
    CHECK (again.analysed == summary.analysed);
    CHECK (again.failed == summary.failed);
    CHECK (again.rendersError == 0);
    CHECK (again.pitchHigh == summary.pitchHigh);
}
