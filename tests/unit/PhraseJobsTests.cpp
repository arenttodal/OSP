// Phase 4: phrase analysis as background jobs (bounded concurrency and memory, shared
// simultaneous requests, cancellation, progress), the on-disk cache (content hash + analysis
// version + settings digest), and re-analysis that keeps ids and never overwrites corrections.

#include "analysis/phrase/AnalysisJobs.h"
#include "analysis/phrase/PhraseEdits.h"
#include "io/PhraseAnalysisCache.h"
#include "research/PhraseFixtures.h"

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <mutex>
#include <set>
#include <thread>

using namespace osp;

namespace
{
    /** A recording from a buffer (no file anywhere): the entry point takes any asset. */
    atlas::SourceAsset bufferAsset (const std::string& hash)
    {
        atlas::SourceAsset s;
        s.assetId = "src0";
        s.contentHash = hash;
        s.fileName = "snapshot";
        s.origin = atlas::AssetOrigin::bufferSnapshot;
        return s;
    }

    std::shared_ptr<const AudioData> phraseAudio (const char* name)
    {
        return std::make_shared<const AudioData> (phrase::makePhraseFixture (name).audio);
    }

    std::filesystem::path scratchFolder (const char* name)
    {
        auto p = std::filesystem::temp_directory_path() / ("osp-phase4-" + std::string (name));
        std::error_code ec;
        std::filesystem::remove_all (p, ec);
        return p;
    }
}

TEST_CASE ("Analysis jobs: results arrive for every request; identical requests share one analysis", "[phrase][jobs][unit]")
{
    phrase::AnalysisQueue queue (2);
    CHECK (queue.workers() == 2);
    std::mutex m;
    std::map<phrase::AnalysisQueue::JobId, phrase::AnalysisResult> results;
    auto collect = [&] (phrase::AnalysisQueue::JobId id, const phrase::AnalysisResult& r) {
        const std::lock_guard<std::mutex> lock (m);
        results[id] = r;
    };
    const auto audio = phraseAudio ("clean-scale");
    const auto a = queue.submit ({ audio, bufferAsset ("sha256:aaaa"), {} }, collect);
    const auto b = queue.submit ({ audio, bufferAsset ("sha256:aaaa"), {} }, collect);   // same key
    const auto c = queue.submit ({ phraseAudio ("vibrato"), bufferAsset ("sha256:cccc"), {} }, collect);
    REQUIRE (a);
    REQUIRE (b);
    REQUIRE (c);
    queue.waitUntilIdle();
    REQUIRE (results.size() == 3);
    CHECK (queue.status (*a).state == phrase::AnalysisQueue::State::finished);
    CHECK (queue.status (*a).progress == 1.0f);
    // One analysis served both: the very same timing, the same notes.
    CHECK (results[*a].analysisSeconds == results[*b].analysisSeconds);
    CHECK (results[*a].atlas.notes.size() == results[*b].atlas.notes.size());
    CHECK (results[*a].atlas.notes.size() == 8);
    CHECK (results[*c].ok);
    // A different analysis setting is another job.
    phrase::AnalysisSettings fewer;
    fewer.detection = phrase::Detection::fewer;
    CHECK (phrase::AnalysisQueue::keyOf (bufferAsset ("sha256:aaaa"), fewer) != phrase::AnalysisQueue::keyOf (bufferAsset ("sha256:aaaa"), {}));
}

TEST_CASE ("Analysis jobs: a cancelled job never reports a result; unusable requests are refused", "[phrase][jobs][unit]")
{
    phrase::AnalysisQueue queue (1);
    std::atomic<int> calls { 0 };
    auto count = [&] (phrase::AnalysisQueue::JobId, const phrase::AnalysisResult&) { ++calls; };
    const auto audio = phraseAudio ("long-sustain");
    // The first occupies the worker; the second is cancelled while still queued.
    const auto first = queue.submit ({ audio, bufferAsset ("sha256:1111"), {} }, count);
    const auto second = queue.submit ({ audio, bufferAsset ("sha256:2222"), {} }, count);
    REQUIRE (first);
    REQUIRE (second);
    queue.cancel (*second);
    CHECK (queue.status (*second).state == phrase::AnalysisQueue::State::cancelled);
    // Cancelling a running one stops it at its next check.
    queue.cancel (*first);
    queue.waitUntilIdle();
    CHECK (calls.load() == 0);
    CHECK (queue.status (*first).state == phrase::AnalysisQueue::State::cancelled);

    std::string error;
    CHECK_FALSE (queue.submit ({ nullptr, bufferAsset ("sha256:x"), {} }, count, &error));
    CHECK_FALSE (error.empty());
    CHECK_FALSE (queue.submit ({ audio, bufferAsset (""), {} }, count, &error));   // no identity
    CHECK (error.find ("hash") != std::string::npos);
}

TEST_CASE ("Analysis jobs: the queue's memory bound refuses more audio than it may hold", "[phrase][jobs][unit]")
{
    const auto audio = phraseAudio ("clean-scale");
    phrase::AnalysisQueue queue (1, audio->durationSeconds() * 1.5);
    std::atomic<int> done { 0 };
    auto count = [&] (phrase::AnalysisQueue::JobId, const phrase::AnalysisResult&) { ++done; };
    REQUIRE (queue.submit ({ audio, bufferAsset ("sha256:a1"), {} }, count));
    std::string error;
    CHECK_FALSE (queue.submit ({ audio, bufferAsset ("sha256:a2"), {} }, count, &error));
    CHECK (error.find ("full") != std::string::npos);
    // The same recording still attaches (it costs nothing more).
    CHECK (queue.submit ({ audio, bufferAsset ("sha256:a1"), {} }, count));
    queue.waitUntilIdle();
    CHECK (done.load() == 2);
    // Room again once it is done.
    CHECK (queue.submit ({ audio, bufferAsset ("sha256:a2"), {} }, count));
    queue.waitUntilIdle();
}

TEST_CASE ("Analysis cache: stored by content hash, analysis version and settings; never a stale entry", "[phrase][jobs][unit]")
{
    const auto folder = scratchFolder ("cache");
    io::PhraseAnalysisCache cache (folder);
    const auto fx = phrase::makePhraseFixture ("legato");
    auto source = bufferAsset ("sha256:feedfeed");
    const auto analysed = phrase::analysePhrase (fx.audio, source);
    REQUIRE (analysed.ok);
    CHECK_FALSE (cache.find (source, {}));
    std::string error;
    REQUIRE (cache.store (analysed.atlas, {}, error));

    // Reopened under another name and path: the same analysis, the caller's record.
    auto renamed = source;
    renamed.fileName = "take 3.wav";
    renamed.lastKnownPath = "/somewhere/take 3.wav";
    const auto found = cache.find (renamed, {});
    REQUIRE (found);
    CHECK (found->notes.size() == analysed.atlas.notes.size());
    CHECK (found->sources.front().fileName == "take 3.wav");
    CHECK (found->sources.front().numFrames == analysed.atlas.sources.front().numFrames);
    for (std::size_t i = 0; i < found->notes.size(); ++i)
    {
        CHECK (found->notes[i].id == analysed.atlas.notes[i].id);
        CHECK (found->notes[i].attackStart == analysed.atlas.notes[i].attackStart);
        CHECK (found->notes[i].rootMidi == analysed.atlas.notes[i].rootMidi);
        CHECK (found->notes[i].diagnostics == analysed.atlas.notes[i].diagnostics);
    }
    // Other settings or other audio: not this entry.
    phrase::AnalysisSettings more;
    more.detection = phrase::Detection::more;
    CHECK_FALSE (cache.find (source, more));
    CHECK_FALSE (cache.find (bufferAsset ("sha256:other"), {}));
    // An entry from another analysis version under this key is ignored.
    auto old = analysed.atlas;
    old.analysis.version = phrase::analysisVersion - 1;
    REQUIRE (cache.store (old, {}, error));
    CHECK_FALSE (cache.find (source, {}));
    std::error_code ec;
    std::filesystem::remove_all (folder, ec);
}

TEST_CASE ("Re-analysis: untouched notes keep their ids; corrections stay and disagreements are reported", "[phrase][jobs][unit]")
{
    const auto fx = phrase::makePhraseFixture ("clean-scale");
    const auto source = bufferAsset ("sha256:0123");
    auto edited = phrase::analysePhrase (fx.audio, source).atlas;
    REQUIRE (edited.notes.size() == 8);

    // The same analysis again: every note keeps its id, nothing to report.
    const auto same = phrase::edit::mergeReanalysisReported (edited, phrase::analysePhrase (fx.audio, source).atlas);
    CHECK (same.keptIds == 8);
    CHECK (same.newIds == 0);
    CHECK (same.conflicts.empty());
    for (std::size_t i = 0; i < 8; ++i)
        CHECK (same.atlas.notes[i].id == edited.notes[i].id);

    // The musician corrects three notes.
    const auto pitchId = edited.notes[1].id, boundaryId = edited.notes[3].id, outId = edited.notes[6].id;
    REQUIRE (phrase::edit::setRoot (edited, pitchId, edited.notes[1].rootMidi + 12));
    REQUIRE (phrase::edit::moveMarker (edited, boundaryId, phrase::edit::Marker::end, edited.notes[3].sourceEnd - 2000));
    REQUIRE (phrase::edit::setIncluded (edited, outId, false));
    const auto correctedBoundaryEnd = edited.findNote (boundaryId)->sourceEnd;

    // A new analysis that disagrees: it splits note 3 in two, loses note 1 and moves note 4
    // (by 45 % of its length: still overlapping, but no longer the same place).
    auto fresh = phrase::analysePhrase (fx.audio, source).atlas;
    REQUIRE (fresh.notes.size() == 8);
    auto split = fresh.notes[3];
    const auto middle = (split.attackStart + split.sourceEnd) / 2;
    fresh.notes[3].sourceEnd = middle;
    split.attackStart = split.sourceStart = middle;
    split.id = "n0100";
    auto& shifted = fresh.notes[4];
    const auto delta = (shifted.sourceEnd - shifted.attackStart) * 45 / 100;
    for (auto* frame : { &shifted.sourceStart, &shifted.attackStart, &shifted.sustain.start, &shifted.sustain.end,
                         &shifted.releaseStart, &shifted.sourceEnd })
        *frame += delta;
    const auto movedId = edited.notes[4].id;
    fresh.notes.erase (fresh.notes.begin() + 1);
    fresh.notes.push_back (split);
    fresh.sortByTime();

    const auto merged = phrase::edit::mergeReanalysisReported (edited, fresh);
    CHECK (merged.keptCorrected == 3);
    // The corrections are exactly as the musician left them.
    REQUIRE (merged.atlas.findNote (pitchId) != nullptr);
    CHECK (merged.atlas.findNote (pitchId)->rootMidi == edited.findNote (pitchId)->rootMidi);
    REQUIRE (merged.atlas.findNote (boundaryId) != nullptr);
    CHECK (merged.atlas.findNote (boundaryId)->sourceEnd == correctedBoundaryEnd);
    REQUIRE (merged.atlas.findNote (outId) != nullptr);
    CHECK_FALSE (merged.atlas.findNote (outId)->included);
    // And every disagreement is reported, not applied.
    auto conflictOf = [&] (const std::string& id) -> const phrase::edit::MergeConflict* {
        for (const auto& c : merged.conflicts)
            if (c.noteId == id)
                return &c;
        return nullptr;
    };
    const auto* gone = conflictOf (pitchId);
    REQUIRE (gone != nullptr);
    CHECK (gone->kind == phrase::edit::MergeConflict::Kind::noteGone);
    const auto* disagree = conflictOf (boundaryId);
    REQUIRE (disagree != nullptr);
    CHECK (disagree->kind == phrase::edit::MergeConflict::Kind::analysisDisagrees);
    CHECK (disagree->found.size() == 2);
    CHECK_FALSE (disagree->message.empty());
    // The moved note was untouched: too different to keep its id, so a new one, reported.
    const auto* moved = conflictOf (movedId);
    REQUIRE (moved != nullptr);
    CHECK (moved->kind == phrase::edit::MergeConflict::Kind::uncertainMatch);
    CHECK (merged.atlas.findNote (movedId) == nullptr);
    // Ids are unique and an id is never given to a different note.
    std::set<std::string> ids;
    for (const auto& n : merged.atlas.notes)
        CHECK (ids.insert (n.id).second);
    const auto validation = atlas::validate (merged.atlas);
    INFO ((validation.errors.empty() ? std::string() : validation.errors.front()));
    CHECK (validation.ok());
    // The untouched, unmoved notes kept their ids.
    for (int i : { 0, 2, 5, 7 })
        CHECK (merged.atlas.findNote (edited.notes[static_cast<std::size_t> (i)].id) != nullptr);
}
