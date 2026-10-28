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

namespace
{
    /** A hand-made contour: pieces of (seconds, start pitch, end pitch, level dB, voiced). */
    struct Piece
    {
        double seconds, fromMidi, toMidi;
        float levelDb = -12.0f;
        bool voiced = true;
    };

    phrase::PitchContour contourOf (const std::vector<Piece>& pieces, phrase::Preprocessed& pre)
    {
        phrase::PitchContour c;
        c.hopSeconds = 0.005;
        for (const auto& p : pieces)
        {
            const auto n = static_cast<int> (std::lround (p.seconds / c.hopSeconds));
            for (int i = 0; i < n; ++i)
            {
                phrase::PitchFrame f;
                f.voiced = p.voiced;
                f.midi = p.voiced ? p.fromMidi + (p.toMidi - p.fromMidi) * i / std::max (1, n - 1) : 0.0;
                f.hz = p.voiced ? midiToHz (f.midi) : 0.0;
                f.confidence = p.voiced ? 0.95 : 0.0;
                f.levelDb = p.levelDb;
                c.frames.push_back (f);
            }
        }
        pre.sampleRate = 48000.0;
        pre.peakDb = -12.0f;
        pre.silenceThresholdDb = -57.0f;
        pre.noiseFloorDb = -100.0f;
        for (const auto& f : c.frames)
            pre.levelDb.push_back (f.levelDb);
        pre.analysedFrames = static_cast<std::int64_t> (c.frames.size()) * 240;
        return c;
    }

    std::vector<phrase::Segment> segmentsOf (const std::vector<Piece>& pieces, const phrase::AnalysisSettings& settings = {})
    {
        phrase::Preprocessed pre;
        const auto c = contourOf (pieces, pre);
        return phrase::segment (c, pre, settings);
    }

    constexpr float silent = -120.0f;
}

TEST_CASE ("Phrase analysis: scoops, falls and octave slips belong to their note", "[phrase][unit]")
{
    SECTION ("a slide up into the first note of a phrase is that note's attack")
    {
        const auto segs = segmentsOf ({ { 0.2, 0, 0, silent, false }, { 0.08, 76.3, 76.4 }, { 0.08, 76.4, 77.9 }, { 1.2, 78, 78 }, { 0.2, 0, 0, silent, false } });
        REQUIRE (segs.size() == 1);
        CHECK (segs[0].begin == 40);   // the scoop's first frame
    }
    SECTION ("a breathy tail that the tracker hears an octave down stays with the note")
    {
        const auto segs = segmentsOf ({ { 0.2, 0, 0, silent, false }, { 1.2, 72, 72 }, { 0.3, 59.8, 59.8, -30.0f }, { 0.2, 0, 0, silent, false } });
        CHECK (segs.size() == 1);
    }
    SECTION ("an octave slip across a short dropout joins the neighbouring note")
    {
        const auto segs = segmentsOf ({ { 0.2, 0, 0, silent, false }, { 0.6, 72, 72 }, { 0.04, 0, 0, -40.0f, false }, { 0.07, 60.1, 60.1 },
                                        { 0.15, 0, 0, silent, false }, { 0.6, 65, 65 }, { 0.2, 0, 0, silent, false } });
        CHECK (segs.size() == 2);
    }
    SECTION ("short notes with a steady pitch of their own are kept, whatever MIN NOTE says")
    {
        phrase::AnalysisSettings settings;
        settings.minPieceSeconds = 0.4;
        const auto segs = segmentsOf ({ { 0.2, 0, 0, silent, false }, { 0.5, 72, 72 }, { 0.12, 74, 74 }, { 0.5, 76, 76 }, { 0.2, 0, 0, silent, false } }, settings);
        CHECK (segs.size() == 3);
    }
    SECTION ("breath swells under a held tone do not re-articulate it; a fall to near silence does")
    {
        std::vector<Piece> swells { { 0.2, 0, 0, silent, false } };
        for (int k = 0; k < 6; ++k)
        {
            swells.push_back ({ 0.2, 72, 72, -12.0f });
            swells.push_back ({ 0.06, 72, 72, -19.0f });   // a 7 dB sag (the ney's measure ~6 dB against its median level)
        }
        swells.push_back ({ 0.2, 0, 0, silent, false });
        CHECK (segmentsOf (swells).size() == 1);

        const auto repeated = segmentsOf ({ { 0.2, 0, 0, silent, false }, { 0.5, 72, 72 }, { 0.05, 72, 72, -35.0f }, { 0.5, 72, 72 }, { 0.2, 0, 0, silent, false } });
        CHECK (repeated.size() == 2);
    }
}

TEST_CASE ("Phrase analysis: DETECTION and MIN NOTE are settings like any other", "[phrase][unit]")
{
    phrase::AnalysisSettings normal, fewer, more;
    fewer.detection = phrase::Detection::fewer;
    more.detection = phrase::Detection::more;
    CHECK (fewer.effective().rearticulationDb > normal.effective().rearticulationDb);
    CHECK (more.effective().rearticulationDb < normal.effective().rearticulationDb);
    // A semitone step is a new note at every level.
    CHECK (fewer.effective().pitchChangeSemitones < 0.75);
    CHECK (more.effective().pitchChangeSemitones < normal.effective().pitchChangeSemitones);
    // Cached analyses are keyed by the digest: both settings must change it.
    CHECK (fewer.digest() != normal.digest());
    auto shorter = normal;
    shorter.minPieceSeconds = 0.1;
    CHECK (shorter.digest() != normal.digest());

    // A shallower re-articulation splits at MORE, not at NORMAL.
    const std::vector<Piece> rebowed { { 0.2, 0, 0, silent, false }, { 0.5, 60, 60 }, { 0.05, 60, 60, -19.0f }, { 0.5, 60, 60 }, { 0.2, 0, 0, silent, false } };
    CHECK (segmentsOf (rebowed).size() == 1);
    CHECK (segmentsOf (rebowed, more).size() == 2);
}
