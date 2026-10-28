#include "andor/phrase/PhraseEdits.h"

#include <algorithm>

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

atlas::NoteAtlas mergeReanalysis (const atlas::NoteAtlas& edited, const atlas::NoteAtlas& fresh)
{
    atlas::NoteAtlas out = fresh;
    out.atlasId = edited.atlasId.empty() ? fresh.atlasId : edited.atlasId;
    out.notes.clear();
    std::vector<atlas::NoteEvent> kept;
    for (const auto& n : edited.notes)
        if (n.corrections.any() || ! n.included)
            kept.push_back (n);
    for (const auto& n : fresh.notes)
    {
        bool overlaps = false;
        for (const auto& k : kept)
            overlaps = overlaps || (n.sourceId == k.sourceId && n.attackStart < k.sourceEnd && k.attackStart < n.sourceEnd);
        if (! overlaps)
            out.notes.push_back (n);
    }
    // Fresh notes get ids that do not collide with the kept ones.
    for (auto& n : out.notes)
        n.id.clear();
    for (const auto& k : kept)
        out.notes.push_back (k);
    for (auto& n : out.notes)
        if (n.id.empty())
            n.id = out.nextNoteId();
    out.sortByTime();
    atlas::remapNotes (out);
    return out;
}

} // namespace osp::phrase::edit
