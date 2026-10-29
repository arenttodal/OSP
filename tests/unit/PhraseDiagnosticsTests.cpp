// Unified engine Phase 4 (docs/research/unified-andor/phase-4-report.md): what the phrase
// analysis adds to ANDOR Lab's for OSP - analysis notes are marked as such, every note carries
// what the analysis was unsure about, and the recording as a whole is judged (monophonic,
// uncertain, unsuitable, silent) with the pitched stretches no note covers (possible missed notes).

#include "analysis/phrase/PhraseAnalysis.h"
#include "audio/utility/TestSignals.h"
#include "core/PitchMath.h"
#include "core/Prng.h"
#include "io/NoteAtlasJson.h"
#include "research/PhraseFixtures.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace osp;
using Catch::Approx;

namespace
{
    atlas::SourceAsset source()
    {
        atlas::SourceAsset s;
        s.contentHash = "sha256:1111111111111111111111111111111111111111111111111111111111111111";
        s.fileName = "phrase.wav";
        return s;
    }

    phrase::AnalysisResult analyse (const AudioData& audio, const phrase::AnalysisSettings& settings = {})
    {
        return phrase::analysePhrase (audio, source(), settings);
    }

    int countFlag (const atlas::NoteAtlas& a, bool atlas::Diagnostics::*flag)
    {
        int n = 0;
        for (const auto& note : a.notes)
            n += (note.diagnostics.*flag) ? 1 : 0;
        return n;
    }

    AudioData mixed (std::initializer_list<int> keys)
    {
        AudioData out;
        std::uint64_t seed = 3;
        for (int k : keys)
        {
            auto v = testsignals::vowel (midiToHz (k), 3.0, 48000.0, seed++, 0.35, 1);
            if (out.isEmpty())
                out = v;
            else
                for (std::size_t i = 0; i < out.channels[0].size(); ++i)
                    out.channels[0][i] += v.channels[0][i];
        }
        return out;
    }
}

TEST_CASE ("Phrase analysis (OSP): notes are analysis notes in a valid schema-2 atlas that round-trips", "[phrase][unit]")
{
    const auto fx = phrase::makePhraseFixture ("vocal-phrase");
    const auto r = analyse (fx.audio);
    REQUIRE (r.ok);
    const auto& a = r.atlas;
    CHECK (a.schemaVersion == atlas::currentSchemaVersion);
    CHECK (atlas::validate (a).ok());
    REQUIRE_FALSE (a.notes.empty());
    for (const auto& n : a.notes)
    {
        CHECK (n.origin == atlas::NoteOrigin::analysis);
        CHECK (n.analysisMethod == "yin-viterbi");
        CHECK_FALSE (n.corrections.any());
    }
    CHECK (a.analysis.suitability == "monophonic");
    CHECK (a.analysis.analysedSeconds == Approx (fx.audio.durationSeconds()).margin (0.01));
    CHECK (a.analysis.pitchedSeconds > 0.5 * a.analysis.analysedSeconds);

    // The diagnostics and the recording summary survive JSON exactly.
    auto marked = a;
    marked.notes[0].diagnostics.uncertainPitch = marked.notes[0].diagnostics.possibleSplit = true;
    marked.notes[1].diagnostics.possibleRepeat = true;   // Phase 4B
    marked.notes[1].diagnostics.repeatFrames = { marked.notes[1].attackStart + 4800, marked.notes[1].attackStart + 9600 };
    marked.analysis.uncovered.push_back ({ 1000, 9000 });
    std::string error;
    const auto back = io::noteAtlasFromString (io::noteAtlasToString (marked), error);
    REQUIRE (back.has_value());
    CHECK (back->notes[0].diagnostics == marked.notes[0].diagnostics);
    CHECK (back->notes[1].diagnostics == marked.notes[1].diagnostics);
    CHECK (back->analysis.suitability == "monophonic");
    CHECK (back->analysis.pitchedSeconds == marked.analysis.pitchedSeconds);
    REQUIRE (back->analysis.uncovered.size() == 1);
    CHECK (back->analysis.uncovered[0] == atlas::FrameRange { 1000, 9000 });
}

TEST_CASE ("Phrase analysis (OSP): clean, vibrato and legato phrases come out reliable, one note per played note", "[phrase][unit]")
{
    for (const char* name : { "clean-scale", "vibrato", "legato", "long-sustain", "stress-portamento" })
    {
        INFO (name);
        const auto fx = phrase::makePhraseFixture (name);
        const auto r = analyse (fx.audio);
        CHECK (r.atlas.notes.size() == fx.notes.size());   // vibrato is not split into notes
        for (const auto& n : r.atlas.notes)
        {
            INFO (n.id);
            CHECK_FALSE (n.diagnostics.any());
        }
        CHECK (r.atlas.analysis.suitability == "monophonic");
        CHECK (r.atlas.analysis.uncovered.empty());
    }
}

TEST_CASE ("Phrase analysis (OSP): doubts are flagged - a far-off tuning, same-key joins", "[phrase][unit]")
{
    // +30, -40, +45, -20, +10 cents: the +45 note is almost between two keys.
    const auto tune = analyse (phrase::makePhraseFixture ("out-of-tune").audio).atlas;
    REQUIRE (tune.notes.size() == 5);
    int far = 0;
    for (const auto& n : tune.notes)
    {
        CHECK (n.diagnostics.uncertainPitch == (std::abs (n.centsOffset) > 42.0));
        far += std::abs (n.centsOffset) > 42.0 ? 1 : 0;
    }
    CHECK (far == 1);
    // Repeated notes joined by 12 dB dips, split as DETECTION MORE does: each pair may be one
    // note cut in two. (At NORMAL these slow dips do not split: Phase 4B, below.)
    phrase::AnalysisSettings more;
    more.detection = phrase::Detection::more;
    const auto rep = analyse (phrase::makePhraseFixture ("repeated").audio, more).atlas;
    CHECK (countFlag (rep, &atlas::Diagnostics::possibleSplit) > 0);
    for (const auto& n : rep.notes)
        if (n.diagnostics.possibleSplit)
            CHECK (n.diagnostics.uncertainBoundary);
}

TEST_CASE ("Phrase analysis (OSP): polyphonic, noisy and silent material is reported, not presented as an instrument", "[phrase][unit]")
{
    const auto two = analyse (mixed ({ 57, 64 }));
    CHECK (two.atlas.analysis.suitability == "unsuitable");
    bool warned = false;
    for (const auto& w : two.warnings)
        warned = warned || w.find ("monophonic") != std::string::npos;
    CHECK (warned);
    for (const auto& n : two.atlas.notes)
        CHECK ((n.diagnostics.uncertainPitch || n.diagnostics.unvoiced || n.diagnostics.possibleOctaveError));
    CHECK (analyse (mixed ({ 57, 61, 64 })).atlas.analysis.suitability == "unsuitable");

    Prng rng (5);
    auto noise = AudioData::allocate (1, 48000 * 3, 48000.0);
    for (auto& x : noise.channels[0])
        x = static_cast<float> (0.3 * (rng.nextDouble() * 2.0 - 1.0));
    const auto n = analyse (noise);
    CHECK (n.ok);
    CHECK (n.atlas.notes.empty());
    CHECK (n.atlas.analysis.suitability == "unsuitable");

    const auto quiet = analyse (AudioData::allocate (1, 48000 * 2, 48000.0));
    CHECK (quiet.ok);
    CHECK (quiet.atlas.analysis.suitability == "silent");
}

TEST_CASE ("Phrase analysis (OSP): pitched sound that no note covers is reported as possible missed notes", "[phrase][unit]")
{
    const auto fx = phrase::makePhraseFixture ("clean-scale");
    phrase::AnalysisSettings settings;
    settings.minNoteSeconds = 1.0;   // every played note is shorter: none is kept
    const auto r = analyse (fx.audio, settings);
    CHECK (r.atlas.notes.empty());
    CHECK (r.atlas.analysis.uncovered.size() >= fx.notes.size() - 1);
    for (const auto& u : r.atlas.analysis.uncovered)
    {
        CHECK (u.length() >= static_cast<std::int64_t> (0.1 * 48000.0) - 512);
        CHECK (u.end <= fx.audio.numFrames());
    }
    bool warned = false;
    for (const auto& w : r.warnings)
        warned = warned || w.find ("missed") != std::string::npos;
    CHECK (warned);
}

TEST_CASE ("Phrase analysis (OSP Phase 4B): a slow dip is kept in one note and offered as a split; a sharp re-attack splits", "[phrase][unit]")
{
    // "repeated": four separated G3s, then four legato G3s joined by 12 dB dips that fall and rise
    // over 60 ms - the shape of a bow change or a swell as much as of a soft re-articulation.
    const auto fx = phrase::makePhraseFixture ("repeated");
    const auto normal = analyse (fx.audio).atlas;
    REQUIRE (normal.notes.size() == 5);
    const auto& held = normal.notes.back();
    CHECK (held.diagnostics.possibleRepeat);
    REQUIRE (held.diagnostics.repeatFrames.size() == 3);
    for (std::size_t k = 0; k < 3; ++k)
    {
        // Each offered split sits at the join (truth: the next note's start), within 30 ms.
        const double at = static_cast<double> (held.diagnostics.repeatFrames[k]) / fx.audio.sampleRate;
        CHECK (at == Approx (fx.notes[5 + k].startSeconds).margin (0.03));
    }
    for (std::size_t i = 0; i + 1 < normal.notes.size(); ++i)
        CHECK_FALSE (normal.notes[i].diagnostics.possibleRepeat);
    const auto evaluated = phrase::evaluate (normal, fx.notes);
    CHECK (evaluated.missed == 3);
    CHECK (evaluated.missedFlagged == 3);

    // DETECTION: MORE splits them as before Phase 4B; FEWER keeps them whole and quiet.
    phrase::AnalysisSettings more, fewer;
    more.detection = phrase::Detection::more;
    fewer.detection = phrase::Detection::fewer;
    CHECK (analyse (fx.audio, more).atlas.notes.size() == 8);
    const auto quiet = analyse (fx.audio, fewer).atlas;
    CHECK (quiet.notes.size() == 5);

    // The same joins with a level that comes back within 8 ms (a re-bow, a re-pluck): new notes.
    const auto attacks = phrase::makePhraseFixture ("repeated-attacks");
    const auto split = analyse (attacks.audio).atlas;
    CHECK (split.notes.size() == attacks.notes.size());
    for (const auto& n : split.notes)
        CHECK_FALSE (n.diagnostics.possibleRepeat);

    // Beating (a slow regular 14 dB swing) is the sound of one note: not split, not doubted.
    const auto beat = analyse (phrase::makePhraseFixture ("beating").audio).atlas;
    REQUIRE (beat.notes.size() == 1);
    CHECK_FALSE (beat.notes[0].diagnostics.possibleRepeat);
}
