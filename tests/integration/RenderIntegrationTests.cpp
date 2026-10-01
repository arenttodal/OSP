#include "audio/utility/TestSignals.h"
#include "core/PitchMath.h"
#include "io/AnalysisJson.h"
#include "io/AudioFileIO.h"
#include "midi/MidiFixtures.h"
#include "research/Fixtures.h"
#include "research/RenderMetrics.h"
#include "research/RenderSession.h"
#include "research/SourceAnalysis.h"
#include "support/TestHelpers.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using Catch::Approx;
using namespace osp;
using namespace osp::research;

namespace
{
    RenderMetrics metricsFor (const RenderOutput& out, const MidiSequence& seq, const RootChoice& root)
    {
        MetricsContext context;
        context.expectedSampleRate = out.audio.sampleRate;
        context.sequence = &seq;
        context.sourceF0Hz = root.sourceF0Hz;
        context.rootMidi = root.rootMidi;
        return computeMetrics (out.audio, context);
    }
}

TEST_CASE ("render: file -> analysis -> fixture render -> metrics", "[integration]")
{
    test::TempDir dir;
    std::string error;
    REQUIRE (io::writeAudioFile (dir / "vowel.wav", testsignals::vowel (midiToHz (57), 3.0, 44100.0, 5), io::SampleFormat::pcm24, error));

    const auto source = loadAndAnalyse (dir / "vowel.wav");
    REQUIRE (source.ok);
    CHECK (source.analysis.source.contentHash.rfind ("sha256:", 0) == 0);
    CHECK (source.analysis.source.format == "WAV");

    const auto root = chooseRoot (&source.analysis, std::nullopt);
    CHECK (root.origin == "analysis");
    CHECK (root.rootMidi == Approx (57.0).margin (0.1));

    const auto standard = fixtures::profile ("standard").value();
    for (const auto& name : standard)
    {
        const auto seq = *fixtures::byName (name, fixtureReferenceNote (root.rootMidi));
        RenderConfig config;
        const auto out = renderSequence (source.audio, root.rootMidi, seq, config);
        const auto m = metricsFor (out, seq, root);
        INFO (name);
        CHECK (m.status() == "ok");
        CHECK (m.channels == 2);
        CHECK (m.sampleRate == 48000.0);
        CHECK (m.durationSeconds >= seq.endTimeSeconds());
        CHECK (m.durationSeconds <= seq.endTimeSeconds() + config.maxTailSeconds + 0.01);
        CHECK (m.nanCount == 0);
        CHECK (m.maxAbsCentsError < 10.0);
    }
}

TEST_CASE ("render: identical inputs give bit-identical output; block size does not matter", "[integration][determinism]")
{
    const auto source = testsignals::saw (110.0, 2.0, 48000.0, 0.5, 2);
    const auto seq = fixtures::melody (45);

    for (auto engine : { EngineId::baselineA, EngineId::baselineB })
    {
        RenderConfig config;
        config.engine = engine;
        const auto a = renderSequence (source, 45.0, seq, config);
        const auto b = renderSequence (source, 45.0, seq, config);
        REQUIRE (a.audio.numFrames() == b.audio.numFrames());
        CHECK (test::maxDifference (a.audio, b.audio) == 0.0);

        // Events are sample-accurate inside blocks, so the host block size cannot change the result.
        for (int block : { 32, 64, 512, 1024 })
        {
            config.blockSize = block;
            const auto c = renderSequence (source, 45.0, seq, config);
            CHECK (test::maxDifference (a.audio, c.audio) == 0.0);
        }
    }
}

TEST_CASE ("render: output sample rate changes nothing but the rate", "[integration]")
{
    const auto source = testsignals::sine (220.0, 3.0, 44100.0);
    const auto seq = fixtures::registerSweep (57);
    RootChoice root;
    root.rootMidi = 57.0;
    root.sourceF0Hz = 220.0;

    for (double rate : { 44100.0, 48000.0, 88200.0, 96000.0 })
    {
        RenderConfig config;
        config.sampleRate = rate;
        const auto out = renderSequence (source, 57.0, seq, config);
        const auto m = metricsFor (out, seq, root);
        INFO (rate);
        CHECK (m.sampleRate == rate);
        CHECK (m.status() == "ok");
        int evaluated = 0;
        for (const auto& n : m.notes)
            if (n.evaluated)
            {
                ++evaluated;
                CHECK (std::abs (n.centsError) < 5.0);
            }
        CHECK (evaluated >= 4);
    }
}

TEST_CASE ("render: stored analysis gives the same root as fresh analysis", "[integration]")
{
    test::TempDir dir;
    std::string error;
    REQUIRE (io::writeAudioFile (dir / "s.wav", testsignals::saw (midiToHz (50) * centsToRatio (17.0), 1.0, 48000.0), io::SampleFormat::float32, error));
    const auto fresh = loadAndAnalyse (dir / "s.wav");
    REQUIRE (fresh.ok);
    REQUIRE (io::writeAnalysis (dir / "a.json", fresh.analysis, error));
    const auto stored = io::readAnalysis (dir / "a.json", error);
    REQUIRE (stored);
    CHECK (chooseRoot (&*stored, std::nullopt).rootMidi == Approx (chooseRoot (&fresh.analysis, std::nullopt).rootMidi).margin (1e-4));
    CHECK (chooseRoot (&*stored, 62.0).origin == "override");
}

TEST_CASE ("render: unpitched sources still render, with a labelled fallback root", "[integration]")
{
    const auto noise = testsignals::whiteNoise (1.0, 48000.0, 0.2, 1, 2);
    const auto analysis = test::analyse (noise);
    const auto root = chooseRoot (&analysis, std::nullopt);
    CHECK (root.origin == "fallback");
    CHECK (root.rootMidi == 60.0);
    const auto seq = fixtures::chords (60);
    const auto out = renderSequence (noise, root.rootMidi, seq, RenderConfig {});
    const auto m = metricsFor (out, seq, root);
    CHECK (m.status() != "error");
    CHECK (m.notes.empty()); // no pitch claims without a known source F0
}

TEST_CASE ("render: safety checks catch corrupted output", "[integration]")
{
    auto audio = testsignals::sine (440.0, 0.5, 48000.0, 0.5, 2);
    audio.channels[0][100] = std::numeric_limits<float>::quiet_NaN();
    audio.channels[1][200] = std::numeric_limits<float>::infinity();
    audio.channels[1][300] = 1.5f;
    MetricsContext context;
    context.expectedSampleRate = 48000.0;
    auto m = computeMetrics (audio, context);
    CHECK (m.status() == "error");
    CHECK (m.nanCount == 1);
    CHECK (m.infCount == 1);
    CHECK (m.clippedSamples == 1);

    context.expectedChannels = 1;
    m = computeMetrics (testsignals::sine (440.0, 0.5, 48000.0, 0.5, 2), context);
    bool channelIssue = false;
    for (const auto& i : m.issues)
        channelIssue = channelIssue || i.code == "channel-count";
    CHECK (channelIssue);

    context.expectedChannels = 2;
    m = computeMetrics (testsignals::sine (440.0, 0.5, 48000.0, 0.0001, 2), context);
    bool silent = false;
    for (const auto& i : m.issues)
        silent = silent || i.code == "near-silent";
    CHECK (silent);

    auto dc = testsignals::sine (440.0, 0.5, 48000.0, 0.3, 2);
    for (auto& ch : dc.channels)
        for (auto& s : ch)
            s += 0.1f;
    m = computeMetrics (dc, context);
    bool dcIssue = false;
    for (const auto& i : m.issues)
        dcIssue = dcIssue || i.code == "dc-offset";
    CHECK (dcIssue);

    context.expectedSampleRate = 44100.0;
    m = computeMetrics (testsignals::sine (440.0, 0.5, 48000.0, 0.3, 2), context);
    CHECK (m.status() == "error");
}
