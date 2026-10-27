#include "andor/phrase/PhraseAnalysis.h"
#include "andor/phrase/PhraseFixtures.h"
#include "core/PitchMath.h"
#include "core/Prng.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>

using namespace osp;
using Catch::Approx;

namespace
{
    phrase::AnalysisResult analyseFixture (const std::string& name, phrase::PitchBackend backend = phrase::PitchBackend::yinViterbi)
    {
        const auto fx = phrase::makePhraseFixture (name);
        phrase::AnalysisSettings settings;
        settings.backend = backend;
        return phrase::analysePhrase (fx.audio, {}, settings);
    }

    phrase::EvaluationResult evaluateFixture (const std::string& name, phrase::PitchBackend backend = phrase::PitchBackend::yinViterbi)
    {
        const auto fx = phrase::makePhraseFixture (name);
        phrase::AnalysisSettings settings;
        settings.backend = backend;
        return phrase::evaluate (phrase::analysePhrase (fx.audio, {}, settings).atlas, fx.notes);
    }
}

TEST_CASE ("Phrase analysis: every generated fixture is found note for note", "[phrase][integration]")
{
    // The default backend on the standard fixtures (docs/andor-lab/phrase-analysis.md has the table).
    for (const auto& name : phrase::phraseFixtureNames())
    {
        INFO (name);
        const auto r = evaluateFixture (name);
        CHECK (r.matched == r.truthNotes);
        CHECK (r.octaveErrors == 0);
        CHECK (r.falseSplits == 0);
        CHECK (r.spurious == 0);
        CHECK (r.meanOnsetErrorMs < 15.0);
        CHECK (r.meanPitchErrorCents < 5.0);
    }
}

TEST_CASE ("Phrase analysis: the stress set stays solved by the default backend", "[phrase][integration]")
{
    // Held out in round 1 (results in docs/andor-lab/data/phrase-stress-round1.txt), fixed in
    // round 2: this guards the fixes, it is no longer an unseen test.
    for (const auto& name : phrase::phraseStressFixtureNames())
    {
        INFO (name);
        const auto r = evaluateFixture (name);
        CHECK (r.matched == r.truthNotes);
        CHECK (r.falseSplits == 0);
        CHECK (r.octaveErrors == 0);
    }
}

TEST_CASE ("Phrase analysis: the Viterbi backend beats plain YIN on noise and breath", "[phrase][integration]")
{
    // The backend decision in numbers: plain YIN fragments noisy and breathy tones.
    for (const auto* name : { "stress-very-noisy", "stress-breathy" })
    {
        INFO (name);
        const auto yin = evaluateFixture (name, phrase::PitchBackend::yin);
        const auto viterbi = evaluateFixture (name, phrase::PitchBackend::yinViterbi);
        CHECK (viterbi.f1() > yin.f1() + 0.3);
    }
}

TEST_CASE ("Phrase analysis: stable identity, expressive deviation and tuning are kept apart", "[phrase][unit]")
{
    SECTION ("out-of-tune notes keep their key and their cents")
    {
        const auto a = analyseFixture ("out-of-tune").atlas;
        REQUIRE (a.notes.size() == 5);
        const int keys[] = { 60, 62, 64, 65, 67 };
        const double cents[] = { 30.0, -40.0, 45.0, -20.0, 10.0 };
        for (std::size_t i = 0; i < 5; ++i)
        {
            CHECK (a.notes[i].rootMidi == keys[i]);
            CHECK (a.notes[i].centsOffset == Approx (cents[i]).margin (3.0));
        }
    }
    SECTION ("vibrato is measured, not mistaken for instability or new notes")
    {
        const auto a = analyseFixture ("vibrato").atlas;
        REQUIRE (a.notes.size() == 4);
        for (std::size_t i = 0; i < 3; ++i)
        {
            CHECK (a.notes[i].vibratoDepthCents == Approx (60.0).margin (15.0));
            CHECK (a.notes[i].vibratoRateHz == Approx (5.5).margin (0.6));
            CHECK (std::abs (a.notes[i].centsOffset) < 6.0);
            CHECK (a.notes[i].confidence > 0.8);
        }
        CHECK (a.notes[3].vibratoDepthCents == Approx (100.0).margin (25.0));
    }
    SECTION ("auto tuning finds a recording's global offset")
    {
        auto fx = phrase::makePhraseFixture ("clean-scale");
        // Re-pitch the whole fixture 25 cents sharp by resampling the analysis input.
        AudioData sharp = fx.audio;
        const double ratio = centsToRatio (25.0);
        auto& src = fx.audio.channels[0];
        auto& dst = sharp.channels[0];
        for (std::size_t i = 0; i < dst.size(); ++i)
        {
            const double pos = static_cast<double> (i) * ratio;
            const auto k = static_cast<std::size_t> (pos);
            const double f = pos - static_cast<double> (k);
            dst[i] = k + 1 < src.size() ? static_cast<float> ((1.0 - f) * src[k] + f * src[k + 1]) : 0.0f;
        }
        phrase::AnalysisSettings settings;
        settings.autoTuning = true;
        const auto a = phrase::analysePhrase (sharp, {}, settings).atlas;
        CHECK (a.analysis.tuningOffsetCents == Approx (25.0).margin (4.0));
        REQUIRE_FALSE (a.notes.empty());
        CHECK (a.notes[0].rootMidi == 60);
        CHECK (a.notes[0].centsOffset == Approx (25.0).margin (4.0));
    }
}

TEST_CASE ("Phrase analysis: boundaries are ordered and sensible", "[phrase][unit]")
{
    for (const auto& name : phrase::phraseFixtureNames())
    {
        INFO (name);
        const auto fx = phrase::makePhraseFixture (name);
        const auto a = phrase::analysePhrase (fx.audio, {}).atlas;
        CHECK (validate (a).ok());
        for (const auto& n : a.notes)
        {
            CHECK (n.sourceStart <= n.attackStart);
            CHECK (n.attackStart <= n.sustain.start);
            CHECK (n.sustain.start <= n.sustain.end);
            CHECK (n.sustain.end <= n.releaseStart);
            CHECK (n.releaseStart <= n.sourceEnd);
            CHECK (n.fadeInFrames >= 1);
            CHECK (n.fadeOutFrames >= 1);
            CHECK (n.fadeInFrames + n.fadeOutFrames <= n.sourceEnd - n.attackStart);
        }
    }
    SECTION ("legato notes join exactly; separated notes leave the silence out")
    {
        const auto legato = phrase::analysePhrase (phrase::makePhraseFixture ("legato").audio, {}).atlas;
        for (std::size_t i = 0; i + 1 < legato.notes.size(); ++i)
        {
            CHECK (legato.notes[i].sourceEnd == legato.notes[i + 1].attackStart);
            CHECK (legato.notes[i].articulation == atlas::Articulation::legato);
        }
        const auto gaps = phrase::analysePhrase (phrase::makePhraseFixture ("silent-gaps").audio, {}).atlas;
        for (std::size_t i = 0; i + 1 < gaps.notes.size(); ++i)
            CHECK (gaps.notes[i + 1].attackStart - gaps.notes[i].sourceEnd > 0.5 * 48000.0);
    }
    SECTION ("sustain regions sit inside the stable part of long notes")
    {
        const auto a = phrase::analysePhrase (phrase::makePhraseFixture ("long-sustain").audio, {}).atlas;
        REQUIRE (a.notes.size() == 1);
        const auto& n = a.notes[0];
        CHECK (static_cast<double> (n.sustain.length()) / 48000.0 > 5.0);
        CHECK (n.articulation == atlas::Articulation::sustained);
    }
    SECTION ("short notes are short, repeats are variations of one key")
    {
        const auto st = phrase::analysePhrase (phrase::makePhraseFixture ("staccato").audio, {}).atlas;
        for (const auto& n : st.notes)
            CHECK (n.articulation == atlas::Articulation::short_);
        const auto rep = phrase::analysePhrase (phrase::makePhraseFixture ("repeated").audio, {}).atlas;
        const auto keys = atlas::notesByKey (rep);
        REQUIRE (keys[55].size() == 8);
        for (std::size_t i = 0; i < 8; ++i)
            CHECK (rep.notes[static_cast<std::size_t> (keys[55][i])].variationIndex == static_cast<int> (i));
    }
}

TEST_CASE ("Phrase analysis: fails beautifully on odd input", "[phrase][unit]")
{
    SECTION ("empty")
    {
        const auto r = phrase::analysePhrase (AudioData {}, {});
        CHECK (r.ok);
        CHECK (r.atlas.notes.empty());
        CHECK_FALSE (r.warnings.empty());
    }
    SECTION ("digital silence")
    {
        const auto r = phrase::analysePhrase (AudioData::allocate (2, 48000 * 3, 48000.0), {});
        CHECK (r.ok);
        CHECK (r.atlas.notes.empty());
        REQUIRE_FALSE (r.warnings.empty());
        CHECK (r.warnings[0].find ("silent") != std::string::npos);
    }
    SECTION ("white noise has no notes")
    {
        auto noise = AudioData::allocate (1, 48000 * 3, 48000.0);
        Prng rng (9);
        for (auto& s : noise.channels[0])
            s = static_cast<float> (0.2 * rng.gaussian());
        const auto r = phrase::analysePhrase (noise, {});
        CHECK (r.ok);
        CHECK (r.atlas.notes.empty());
    }
    SECTION ("a tiny file (10 ms) and a single sample")
    {
        auto tiny = AudioData::allocate (1, 480, 48000.0);
        for (std::size_t i = 0; i < 480; ++i)
            tiny.channels[0][i] = static_cast<float> (0.3 * std::sin (0.05 * static_cast<double> (i)));
        CHECK (phrase::analysePhrase (tiny, {}).ok);
        CHECK (phrase::analysePhrase (AudioData::allocate (1, 1, 48000.0), {}).ok);
    }
    SECTION ("a recording longer than the analysis limit is analysed in part, with a warning")
    {
        auto fx = phrase::makePhraseFixture ("clean-scale");
        phrase::AnalysisSettings settings;
        settings.maxAnalysisSeconds = 2.0;
        const auto r = phrase::analysePhrase (fx.audio, {}, settings);
        CHECK (r.ok);
        CHECK_FALSE (r.warnings.empty());
        for (const auto& n : r.atlas.notes)
            CHECK (n.sourceEnd <= 2 * 48000 + 4800);
        CHECK (r.atlas.sources[0].numFrames == fx.audio.numFrames());   // the source is described whole
    }
    SECTION ("cancelling stops the analysis")
    {
        std::atomic<bool> cancel { true };
        const auto r = phrase::analysePhrase (phrase::makePhraseFixture ("vocal-phrase").audio, {}, {}, &cancel);
        CHECK_FALSE (r.ok);
        CHECK (r.cancelled);
    }
}

TEST_CASE ("Phrase analysis: deterministic, rate- and channel-independent", "[phrase][unit]")
{
    const auto fx = phrase::makePhraseFixture ("vocal-phrase");
    const auto a = phrase::analysePhrase (fx.audio, {}).atlas;
    const auto b = phrase::analysePhrase (fx.audio, {}).atlas;
    REQUIRE (a.notes.size() == b.notes.size());
    for (std::size_t i = 0; i < a.notes.size(); ++i)
    {
        CHECK (a.notes[i].attackStart == b.notes[i].attackStart);
        CHECK (a.notes[i].sourceEnd == b.notes[i].sourceEnd);
        CHECK (a.notes[i].midiPitch == b.notes[i].midiPitch);
    }

    // A stereo copy (same signal both sides) analyses like the mono original.
    AudioData stereo = fx.audio;
    stereo.channels.push_back (stereo.channels[0]);
    const auto s = phrase::analysePhrase (stereo, {}).atlas;
    REQUIRE (s.notes.size() == a.notes.size());
    CHECK (s.sources[0].numChannels == 2);
    for (std::size_t i = 0; i < a.notes.size(); ++i)
        CHECK (s.notes[i].rootMidi == a.notes[i].rootMidi);

    // 44.1 kHz finds the same notes (times scale with the rate).
    const auto fx44 = phrase::makePhraseFixture ("vocal-phrase", 44100.0);
    const auto r44 = phrase::evaluate (phrase::analysePhrase (fx44.audio, {}).atlas, fx44.notes);
    CHECK (r44.matched == r44.truthNotes);
    const auto fx96 = phrase::makePhraseFixture ("clean-scale", 96000.0);
    const auto r96 = phrase::evaluate (phrase::analysePhrase (fx96.audio, {}).atlas, fx96.notes);
    CHECK (r96.matched == r96.truthNotes);
}

TEST_CASE ("Phrase analysis: the generated atlas describes its source and method", "[phrase][unit]")
{
    atlas::SourceAsset src;
    src.contentHash = "sha256:0123456789abcdef0123456789abcdef";
    src.fileName = "take.wav";
    const auto fx = phrase::makePhraseFixture ("clean-scale");
    const auto a = phrase::analysePhrase (fx.audio, src).atlas;
    REQUIRE (a.sources.size() == 1);
    CHECK (a.sources[0].assetId == "src0");
    CHECK (a.sources[0].sampleRate == 48000.0);
    CHECK (a.sources[0].numFrames == fx.audio.numFrames());
    CHECK (a.analysis.method == "yin-viterbi");
    CHECK (a.analysis.version == phrase::analysisVersion);
    CHECK_FALSE (a.analysis.settingsDigest.empty());
    CHECK (a.atlasId.find ("atlas:") == 0);
    for (const auto& n : a.notes)
    {
        CHECK (n.sourceId == "src0");
        CHECK (n.analysisMethod == "yin-viterbi");
        CHECK_FALSE (n.corrections.any());
    }
}
