#include "andor/phrase/PhraseAnalysis.h"
#include "andor/phrase/PhraseEdits.h"
#include "andor/phrase/PhraseFixtures.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <set>

using namespace osp;
using Catch::Approx;

TEST_CASE ("Phrase edits: split and merge re-describe the audio", "[phrase][unit]")
{
    // The legato fixture's first two notes (D4, E4) are one region when merged; splitting
    // that region at the join gives the two pitches back.
    const auto fx = phrase::makePhraseFixture ("legato");
    auto a = phrase::analysePhrase (fx.audio, {}).atlas;
    REQUIRE (a.notes.size() == 6);
    const auto firstId = a.notes[0].id;
    REQUIRE (phrase::edit::mergeWithNext (a, firstId, fx.audio));
    REQUIRE (a.notes.size() == 5);
    CHECK (a.notes[0].id == firstId);
    CHECK (a.notes[0].corrections.merged);
    CHECK (validate (a).ok());

    const auto join = static_cast<std::int64_t> (0.7 * 48000.0);
    const auto newId = phrase::edit::split (a, firstId, join, fx.audio);
    REQUIRE_FALSE (newId.empty());
    REQUIRE (a.notes.size() == 6);
    CHECK (a.notes[0].rootMidi == 62);
    CHECK (a.notes[1].rootMidi == 64);
    CHECK (a.notes[1].id == newId);
    CHECK (a.notes[0].corrections.split);
    CHECK (a.notes[0].sourceEnd == join);
    CHECK (a.notes[1].attackStart == join);
    std::set<std::string> ids;
    for (const auto& n : a.notes)
        CHECK (ids.insert (n.id).second);
    CHECK (validate (a).ok());

    // Nonsense requests change nothing.
    const auto before = a.notes.size();
    CHECK (phrase::edit::split (a, firstId, a.notes[0].attackStart + 10, fx.audio).empty());
    CHECK (phrase::edit::split (a, "missing", join, fx.audio).empty());
    CHECK_FALSE (phrase::edit::mergeWithNext (a, a.notes.back().id, fx.audio));
    CHECK (a.notes.size() == before);
}

TEST_CASE ("Phrase edits: pitch, key, group, inclusion and markers", "[phrase][unit]")
{
    const auto fx = phrase::makePhraseFixture ("out-of-tune");   // C4 +30 ...
    auto a = phrase::analysePhrase (fx.audio, {}).atlas;
    const auto id = a.notes[0].id;

    REQUIRE (phrase::edit::setRoot (a, id, 48));   // an octave correction
    CHECK (a.notes[0].rootMidi == 48);
    CHECK (a.notes[0].midiPitch == Approx (48.30).margin (0.03));
    CHECK (a.notes[0].centsOffset == Approx (30.0).margin (3.0));
    CHECK (a.notes[0].corrections.pitch);
    // A later remap (re-rounding) keeps the user's root.
    atlas::remapNotes (a);
    CHECK (a.notes[0].rootMidi == 48);

    REQUIRE (phrase::edit::assignKey (a, id, 70));
    CHECK (atlas::notesByKey (a)[70].size() == 1);
    REQUIRE (phrase::edit::assignKey (a, id, -1));
    CHECK (atlas::notesByKey (a)[70].empty());
    REQUIRE (phrase::edit::setGroup (a, id, "lead"));
    CHECK (atlas::groupOf (a.notes[0]) == "lead");
    REQUIRE (phrase::edit::setIncluded (a, id, false));
    CHECK (atlas::notesByKey (a)[48].empty());

    auto& n = a.notes[1];
    const auto nid = n.id;
    REQUIRE (phrase::edit::moveMarker (a, nid, phrase::edit::Marker::sustainStart, n.sourceEnd + 1000));
    REQUIRE (phrase::edit::moveMarker (a, nid, phrase::edit::Marker::attackStart, 0));
    REQUIRE (phrase::edit::moveMarker (a, nid, phrase::edit::Marker::end, a.notes[1].attackStart + 1));
    CHECK (a.notes[1].corrections.boundaries);
    CHECK (validate (a).ok());
    CHECK_FALSE (phrase::edit::setRoot (a, "missing", 60));
    CHECK_FALSE (phrase::edit::setRoot (a, nid, 200));
}

TEST_CASE ("Phrase edits: re-analysis never silently replaces user edits", "[phrase][unit]")
{
    const auto fx = phrase::makePhraseFixture ("clean-scale");
    auto edited = phrase::analysePhrase (fx.audio, {}).atlas;
    REQUIRE (edited.notes.size() == 8);
    phrase::edit::setRoot (edited, edited.notes[2].id, 76);   // E4 -> E5 by hand
    phrase::edit::setIncluded (edited, edited.notes[5].id, false);
    const auto keptId = edited.notes[2].id;

    phrase::AnalysisSettings other;
    other.backend = phrase::PitchBackend::yin;
    const auto fresh = phrase::analysePhrase (fx.audio, {}, other).atlas;
    const auto merged = phrase::edit::mergeReanalysis (edited, fresh);
    REQUIRE (merged.notes.size() == 8);
    const auto* kept = merged.findNote (keptId);
    REQUIRE (kept != nullptr);
    CHECK (kept->rootMidi == 76);
    int excluded = 0;
    for (const auto& n : merged.notes)
        excluded += n.included ? 0 : 1;
    CHECK (excluded == 1);
    std::set<std::string> ids;
    for (const auto& n : merged.notes)
        CHECK (ids.insert (n.id).second);
    CHECK (validate (merged).ok());

    // Re-describing one note clears its pitch correction (the user asked for the analysis).
    auto again = merged;
    REQUIRE (phrase::edit::reanalyse (again, keptId, fx.audio));
    CHECK (again.findNote (keptId)->rootMidi == 64);
    CHECK_FALSE (again.findNote (keptId)->corrections.pitch);
}
