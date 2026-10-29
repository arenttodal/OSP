#include "analysis/phrase/PhraseEdits.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>

namespace osp::phrase::edit
{

namespace
{
    /** Keeps the user-facing fields of `from` on a re-described note. */
    void keepIdentity (atlas::NoteEvent& note, const atlas::NoteEvent& from)
    {
        note.id = from.id;
        note.sourceId = from.sourceId;
        note.assignedKey = from.assignedKey;
        note.variationGroup = from.variationGroup;
        note.included = from.included;
        note.corrections = from.corrections;
        note.grainRegion = std::nullopt;
    }
}

bool setRoot (atlas::NoteAtlas& a, const std::string& id, int rootKey)
{
    auto* n = a.findNote (id);
    if (n == nullptr || rootKey < 0 || rootKey > 127)
        return false;
    const double fraction = n->midiPitch - n->rootMidi;
    n->rootMidi = rootKey;
    n->midiPitch = rootKey + fraction;
    n->corrections.pitch = true;
    atlas::remapNotes (a);
    return true;
}

bool moveMarker (atlas::NoteAtlas& a, const std::string& id, Marker marker, std::int64_t frame)
{
    auto* n = a.findNote (id);
    if (n == nullptr)
        return false;
    switch (marker)
    {
        case Marker::attackStart:
            n->attackStart = std::min (frame, n->sourceEnd - 2);
            n->sourceStart = std::min (n->sourceStart, n->attackStart);
            n->sustain.start = std::max (n->sustain.start, n->attackStart);
            break;
        case Marker::sustainStart:
            n->sustain.start = std::clamp (frame, n->attackStart, n->sourceEnd);
            n->sustain.end = std::max (n->sustain.end, n->sustain.start);
            break;
        case Marker::sustainEnd:
            n->sustain.end = std::clamp (frame, n->attackStart, n->sourceEnd);
            n->sustain.start = std::min (n->sustain.start, n->sustain.end);
            n->releaseStart = n->sustain.end;
            break;
        case Marker::end:
            n->sourceEnd = std::max (frame, n->attackStart + 2);
            n->releaseStart = std::min (n->releaseStart, n->sourceEnd);
            n->sustain.end = std::min (n->sustain.end, n->sourceEnd);
            n->sustain.start = std::min (n->sustain.start, n->sustain.end);
            break;
    }
    n->corrections.boundaries = true;
    // Fades never longer than the region allows.
    const auto length = n->sourceEnd - n->attackStart;
    n->fadeInFrames = std::clamp<std::int64_t> (n->fadeInFrames, 1, std::max<std::int64_t> (1, length / 4));
    n->fadeOutFrames = std::clamp<std::int64_t> (n->fadeOutFrames, 1, std::max<std::int64_t> (1, length / 2));
    atlas::repair (a);
    return true;
}

bool assignKey (atlas::NoteAtlas& a, const std::string& id, int key)
{
    auto* n = a.findNote (id);
    if (n == nullptr || key > 127)
        return false;
    n->assignedKey = key < 0 ? -1 : key;
    n->corrections.key = key >= 0;
    atlas::remapNotes (a);
    return true;
}

bool setIncluded (atlas::NoteAtlas& a, const std::string& id, bool included)
{
    auto* n = a.findNote (id);
    if (n == nullptr)
        return false;
    n->included = included;
    n->corrections.included = true;   // OSP Phase 4: a re-analysis keeps the musician's choice
    return true;
}

bool setGroup (atlas::NoteAtlas& a, const std::string& id, const std::string& group)
{
    auto* n = a.findNote (id);
    if (n == nullptr)
        return false;
    n->variationGroup = group;
    n->corrections.group = ! group.empty();
    atlas::remapNotes (a);
    return true;
}

std::string split (atlas::NoteAtlas& a, const std::string& id, std::int64_t frame, const AudioData& audio, const AnalysisSettings& settings)
{
    const int index = a.indexOf (id);
    if (index < 0)
        return {};
    const auto original = a.notes[static_cast<std::size_t> (index)];
    const auto minLength = static_cast<std::int64_t> (0.02 * audio.sampleRate);
    if (frame <= original.attackStart + minLength || frame >= original.sourceEnd - minLength)
        return {};

    auto left = analyseRange (audio, original.attackStart, frame, settings);
    auto right = analyseRange (audio, frame, original.sourceEnd, settings);
    keepIdentity (left, original);
    keepIdentity (right, original);
    right.id = a.nextNoteId();
    left.fadeInFrames = original.fadeInFrames;
    right.fadeOutFrames = original.fadeOutFrames;
    left.corrections.split = right.corrections.split = true;
    // A user pitch correction applies to the note as it was, not to its re-described halves.
    left.corrections.pitch = right.corrections.pitch = false;
    a.notes[static_cast<std::size_t> (index)] = left;
    a.notes.insert (a.notes.begin() + index + 1, right);
    atlas::remapNotes (a);
    atlas::repair (a);
    return right.id;
}

bool mergeWithNext (atlas::NoteAtlas& a, const std::string& id, const AudioData& audio, const AnalysisSettings& settings)
{
    a.sortByTime();
    const int index = a.indexOf (id);
    if (index < 0 || index + 1 >= static_cast<int> (a.notes.size()))
        return false;
    const auto first = a.notes[static_cast<std::size_t> (index)];
    const auto second = a.notes[static_cast<std::size_t> (index + 1)];
    if (first.sourceId != second.sourceId)
        return false;
    auto merged = analyseRange (audio, first.attackStart, second.sourceEnd, settings);
    keepIdentity (merged, first);
    merged.fadeInFrames = first.fadeInFrames;
    merged.fadeOutFrames = second.fadeOutFrames;
    merged.corrections.merged = true;
    merged.corrections.pitch = false;
    a.notes[static_cast<std::size_t> (index)] = merged;
    a.notes.erase (a.notes.begin() + index + 1);
    atlas::remapNotes (a);
    atlas::repair (a);
    return true;
}

bool reanalyse (atlas::NoteAtlas& a, const std::string& id, const AudioData& audio, const AnalysisSettings& settings)
{
    auto* n = a.findNote (id);
    if (n == nullptr)
        return false;
    auto fresh = analyseRange (audio, n->attackStart, n->sourceEnd, settings);
    keepIdentity (fresh, *n);
    fresh.fadeInFrames = n->fadeInFrames;
    fresh.fadeOutFrames = n->fadeOutFrames;
    fresh.corrections.pitch = false;
    *n = fresh;
    atlas::remapNotes (a);
    atlas::repair (a);
    return true;
}

namespace
{
    std::int64_t overlap (const atlas::NoteEvent& a, const atlas::NoteEvent& b)
    {
        if (a.sourceId != b.sourceId)
            return 0;
        return std::max<std::int64_t> (0, std::min (a.sourceEnd, b.sourceEnd) - std::max (a.attackStart, b.attackStart));
    }

    /** Overlap as a share of the two notes' union (1: the same range). */
    double sameness (const atlas::NoteEvent& a, const atlas::NoteEvent& b)
    {
        const auto shared = overlap (a, b);
        const auto both = std::max (a.sourceEnd, b.sourceEnd) - std::min (a.attackStart, b.attackStart);
        return both > 0 ? static_cast<double> (shared) / static_cast<double> (both) : 0.0;
    }

    /** The musician decided this note: a correction, or (an older atlas without the flag) left
        out by hand rather than by the analysis. */
    bool decidedByUser (const atlas::NoteEvent& n)
    {
        return n.corrections.any() || n.origin == atlas::NoteOrigin::user || (! n.included && ! n.diagnostics.quietAlternate);
    }

    std::string keyName (const atlas::NoteEvent& n)
    {
        static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        return std::string (names[((n.rootMidi % 12) + 12) % 12]) + std::to_string (n.rootMidi / 12 - 1);
    }

    int idNumber (const std::string& id)
    {
        return id.size() > 1 && id[0] == 'n' ? std::atoi (id.c_str() + 1) : -1;
    }
}

MergeResult mergeReanalysisReported (const atlas::NoteAtlas& edited, const atlas::NoteAtlas& fresh)
{
    MergeResult result;
    auto& out = result.atlas;
    out = fresh;
    out.atlasId = edited.atlasId.empty() ? fresh.atlasId : edited.atlasId;
    out.notes.clear();

    std::vector<const atlas::NoteEvent*> kept, untouched;
    for (const auto& n : edited.notes)
        (decidedByUser (n) ? kept : untouched).push_back (&n);

    // 1. The musician's notes, exactly; what the new analysis says about each is reported.
    std::vector<bool> blocked (fresh.notes.size(), false);
    for (const auto* k : kept)
    {
        MergeConflict c;
        c.noteId = k->id;
        for (std::size_t i = 0; i < fresh.notes.size(); ++i)
            if (overlap (fresh.notes[i], *k) > 0)
            {
                blocked[i] = true;
                c.found.push_back (fresh.notes[i]);
            }
        const bool agrees = c.found.size() == 1 && sameness (c.found.front(), *k) >= 0.5
                            && (k->corrections.pitch || k->corrections.key || c.found.front().rootMidi == k->rootMidi);
        if (c.found.empty() && k->included)
        {
            c.kind = MergeConflict::Kind::noteGone;
            c.message = "note " + k->id + " (" + keyName (*k) + "): the new analysis finds no note here; your note is kept";
            result.conflicts.push_back (std::move (c));
        }
        else if (! c.found.empty() && ! agrees)
        {
            std::string what;
            for (const auto& f : c.found)
                what += (what.empty() ? "" : ", ") + keyName (f);
            c.kind = MergeConflict::Kind::analysisDisagrees;
            c.message = "note " + k->id + " (" + keyName (*k) + "): the new analysis finds " + std::to_string (c.found.size())
                        + " note(s) here (" + what + "); your note is kept";
            result.conflicts.push_back (std::move (c));
        }
        out.notes.push_back (*k);
        ++result.keptCorrected;
    }

    // 2. New notes where nothing was corrected: an untouched note re-detected at nearly the
    //    same place and key keeps its id; otherwise a new id (and, when it was close, a note).
    std::vector<bool> claimed (untouched.size(), false);
    std::vector<std::size_t> needsId;
    for (std::size_t i = 0; i < fresh.notes.size(); ++i)
    {
        if (blocked[i])
            continue;
        auto n = fresh.notes[i];
        int best = -1;
        double bestSameness = 0.0;
        int near = -1;
        for (std::size_t u = 0; u < untouched.size(); ++u)
        {
            const double same = sameness (n, *untouched[u]);
            if (same >= 0.5 && untouched[u]->rootMidi == n.rootMidi && ! claimed[u] && same > bestSameness)
            {
                best = static_cast<int> (u);
                bestSameness = same;
            }
            else if (same >= 0.2 && near < 0)
                near = static_cast<int> (u);
        }
        if (best >= 0)
        {
            claimed[static_cast<std::size_t> (best)] = true;
            n.id = untouched[static_cast<std::size_t> (best)]->id;
            ++result.keptIds;
        }
        else
        {
            n.id.clear();
            needsId.push_back (out.notes.size());
            if (near >= 0)
            {
                MergeConflict c;
                c.kind = MergeConflict::Kind::uncertainMatch;
                c.noteId = untouched[static_cast<std::size_t> (near)]->id;
                c.found.push_back (n);
                c.message = "note " + c.noteId + " was re-detected too differently (place or key) to keep its id";
                result.conflicts.push_back (std::move (c));
            }
        }
        out.notes.push_back (std::move (n));
    }
    // New ids: past every id the edited atlas ever used, so none is reused for another note.
    int highest = -1;
    for (const auto& n : edited.notes)
        highest = std::max (highest, idNumber (n.id));
    for (const auto& n : out.notes)
        highest = std::max (highest, idNumber (n.id));
    for (auto index : needsId)
    {
        char buffer[16];
        std::snprintf (buffer, sizeof (buffer), "n%04d", ++highest);
        out.notes[index].id = buffer;
        // A conflict names the new note too.
        for (auto& c : result.conflicts)
            if (c.kind == MergeConflict::Kind::uncertainMatch && ! c.found.empty() && c.found.front().attackStart == out.notes[index].attackStart)
                c.found.front().id = buffer;
        ++result.newIds;
    }
    out.sortByTime();
    atlas::remapNotes (out);
    return result;
}

atlas::NoteAtlas mergeReanalysis (const atlas::NoteAtlas& edited, const atlas::NoteAtlas& fresh)
{
    return mergeReanalysisReported (edited, fresh).atlas;
}

} // namespace osp::phrase::edit
