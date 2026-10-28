#include "model/NoteAtlas.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <set>
#include <tuple>

namespace osp::atlas
{

const SourceAsset* NoteAtlas::findSource (const std::string& assetId) const noexcept
{
    for (const auto& s : sources)
        if (s.assetId == assetId)
            return &s;
    return nullptr;
}

const NoteEvent* NoteAtlas::findNote (const std::string& noteId) const noexcept
{
    for (const auto& n : notes)
        if (n.id == noteId)
            return &n;
    return nullptr;
}

NoteEvent* NoteAtlas::findNote (const std::string& noteId) noexcept
{
    for (auto& n : notes)
        if (n.id == noteId)
            return &n;
    return nullptr;
}

int NoteAtlas::indexOf (const std::string& noteId) const noexcept
{
    for (std::size_t i = 0; i < notes.size(); ++i)
        if (notes[i].id == noteId)
            return static_cast<int> (i);
    return -1;
}

std::string NoteAtlas::nextNoteId() const
{
    // Ids are never reused within an atlas (an undone split must not collide with a later one
    // only by luck), so take one past the highest number in use.
    int highest = -1;
    for (const auto& n : notes)
        if (n.id.size() > 1 && n.id[0] == 'n')
            highest = std::max (highest, std::atoi (n.id.c_str() + 1));
    char buffer[16];
    std::snprintf (buffer, sizeof (buffer), "n%04d", highest + 1);
    return buffer;
}

void NoteAtlas::sortByTime()
{
    std::stable_sort (notes.begin(), notes.end(), [] (const NoteEvent& a, const NoteEvent& b)
                      { return a.sourceId != b.sourceId ? a.sourceId < b.sourceId : a.sourceStart < b.sourceStart; });
}

//==============================================================================
ValidationReport validate (const NoteAtlas& atlas)
{
    ValidationReport report;
    if (atlas.schemaVersion != currentSchemaVersion)
        report.errors.push_back ("schemaVersion " + std::to_string (atlas.schemaVersion) + " is not the current version "
                                 + std::to_string (currentSchemaVersion) + " (migrate before use)");
    if (atlas.sources.empty())
        report.errors.push_back ("the atlas has no source");
    if (! std::isfinite (atlas.mapping.maxTransposeSemitones) || atlas.mapping.maxTransposeSemitones < 0.0)
        report.errors.push_back ("mapping: the maximum transposition must be 0 or more semitones");

    std::set<std::string> sourceIds;
    for (const auto& s : atlas.sources)
    {
        if (s.assetId.empty())
            report.errors.push_back ("a source has no id");
        else if (! sourceIds.insert (s.assetId).second)
            report.errors.push_back ("duplicate source id " + s.assetId);
        if (s.sampleRate <= 0.0 || s.numFrames <= 0)
            report.errors.push_back ("source " + s.assetId + " has no length or sample rate");
        if (s.contentHash.empty())
            report.errors.push_back ("source " + s.assetId + " has no content hash: its audio cannot be identified");
        if (s.numChannels < 1 || s.numChannels > 2)
            report.errors.push_back ("source " + s.assetId + " has " + std::to_string (s.numChannels) + " channels (1 or 2)");
    }

    std::set<std::string> noteIds;
    for (const auto& n : atlas.notes)
    {
        const auto tag = "note " + (n.id.empty() ? std::string ("(no id)") : n.id) + ": ";
        if (n.id.empty())
            report.errors.push_back (tag + "no id");
        else if (! noteIds.insert (n.id).second)
            report.errors.push_back (tag + "duplicate id");

        const auto* src = atlas.findSource (n.sourceId);
        if (src == nullptr)
        {
            report.errors.push_back (tag + "refers to unknown source '" + n.sourceId + "'");
            continue;
        }
        const bool ordered = n.sourceStart <= n.attackStart && n.attackStart <= n.sustain.start && n.sustain.start <= n.sustain.end
                             && n.sustain.end <= n.releaseStart && n.releaseStart <= n.sourceEnd;
        if (! ordered)
            report.errors.push_back (tag + "markers out of order");
        if (n.sourceStart < 0 || n.sourceEnd > src->numFrames)
            report.errors.push_back (tag + "outside its source (0.." + std::to_string (src->numFrames) + ")");
        if (n.sourceEnd - n.attackStart < 2)
            report.errors.push_back (tag + "empty play range");
        if (n.grainRegion && (n.grainRegion->start < 0 || n.grainRegion->end > src->numFrames || n.grainRegion->isEmpty()))
            report.errors.push_back (tag + "grain region outside its source or empty");
        if (! std::isfinite (n.midiPitch) || n.midiPitch < -24.0 || n.midiPitch > 151.0)
            report.errors.push_back (tag + "pitch out of range");
        if (n.rootMidi < 0 || n.rootMidi > 127)
            report.errors.push_back (tag + "root key out of range");
        if (n.assignedKey > 127)
            report.errors.push_back (tag + "assigned key out of range");
        if (n.fadeInFrames < 0 || n.fadeOutFrames < 0)
            report.errors.push_back (tag + "negative fade");
        if (n.confidence < 0.0 || n.confidence > 1.0)
            report.warnings.push_back (tag + "confidence outside 0..1");
        if (n.velocityLayer < 0 || n.velocityLayer > 15)
            report.errors.push_back (tag + "velocity layer outside 0..15");
    }
    return report;
}

int repair (NoteAtlas& atlas)
{
    int changed = 0;
    for (auto& n : atlas.notes)
    {
        const auto* src = atlas.findSource (n.sourceId);
        if (src == nullptr)
            continue;
        const auto before = std::make_tuple (n.sourceStart, n.attackStart, n.sustain.start, n.sustain.end, n.releaseStart, n.sourceEnd);
        const auto len = std::max<std::int64_t> (2, src->numFrames);
        n.sourceStart = std::clamp<std::int64_t> (n.sourceStart, 0, len - 2);
        n.sourceEnd = std::clamp<std::int64_t> (n.sourceEnd, n.sourceStart + 2, len);
        n.attackStart = std::clamp (n.attackStart, n.sourceStart, n.sourceEnd - 2);
        n.sustain.start = std::clamp (n.sustain.start, n.attackStart, n.sourceEnd);
        n.sustain.end = std::clamp (n.sustain.end, n.sustain.start, n.sourceEnd);
        n.releaseStart = std::clamp (n.releaseStart, n.sustain.end, n.sourceEnd);
        if (n.grainRegion)
        {
            n.grainRegion->start = std::clamp<std::int64_t> (n.grainRegion->start, 0, len - 1);
            n.grainRegion->end = std::clamp<std::int64_t> (n.grainRegion->end, n.grainRegion->start + 1, len);
        }
        if (before != std::make_tuple (n.sourceStart, n.attackStart, n.sustain.start, n.sustain.end, n.releaseStart, n.sourceEnd))
            ++changed;
    }
    return changed;
}

RoundedPitch roundPitch (double midiPitch, double tuningOffsetCents) noexcept
{
    // The tuning rule: remove the recording's global tuning offset, then round half away from
    // the lower key only past +50 cents (a note at exactly +50 belongs to the lower key: the
    // rounding is deterministic and does not depend on floating-point tie behaviour).
    const double corrected = midiPitch - tuningOffsetCents / 100.0;
    double key = std::floor (corrected);
    if (corrected - key > 0.5)
        key += 1.0;
    RoundedPitch result;
    result.key = static_cast<int> (std::clamp (key, 0.0, 127.0));
    result.cents = (midiPitch - static_cast<double> (result.key)) * 100.0;
    return result;
}

void remapNotes (NoteAtlas& atlas)
{
    for (auto& n : atlas.notes)
    {
        if (n.corrections.pitch)
        {
            n.centsOffset = (n.midiPitch - n.rootMidi) * 100.0;
            continue;
        }
        const auto r = roundPitch (n.midiPitch, atlas.analysis.tuningOffsetCents);
        n.rootMidi = r.key;
        n.centsOffset = r.cents;
    }

    // Variation order: time order within each group.
    std::map<std::string, std::vector<NoteEvent*>> groups;
    for (auto& n : atlas.notes)
        groups[groupOf (n)].push_back (&n);
    for (auto& [name, members] : groups)
    {
        std::stable_sort (members.begin(), members.end(), [] (const NoteEvent* a, const NoteEvent* b) { return a->sourceStart < b->sourceStart; });
        for (std::size_t i = 0; i < members.size(); ++i)
            members[i]->variationIndex = static_cast<int> (i);
    }
}

std::string groupOf (const NoteEvent& note)
{
    // Round robins are per key and velocity layer unless the note names its own group.
    return note.variationGroup.empty() ? "key" + std::to_string (note.playKey()) + ".v" + std::to_string (note.velocityLayer) : note.variationGroup;
}

std::array<std::vector<int>, 128> notesByKey (const NoteAtlas& atlas)
{
    std::array<std::vector<int>, 128> keys;
    for (std::size_t i = 0; i < atlas.notes.size(); ++i)
    {
        const auto& n = atlas.notes[i];
        if (! n.included)
            continue;
        const int key = n.playKey();
        if (key >= 0 && key < 128)
            keys[static_cast<std::size_t> (key)].push_back (static_cast<int> (i));
    }
    for (auto& list : keys)
        std::stable_sort (list.begin(), list.end(), [&] (int a, int b) {
            const auto& na = atlas.notes[static_cast<std::size_t> (a)];
            const auto& nb = atlas.notes[static_cast<std::size_t> (b)];
            return na.velocityLayer != nb.velocityLayer ? na.velocityLayer < nb.velocityLayer : na.variationIndex < nb.variationIndex;
        });
    return keys;
}

RelinkResult checkRelink (const SourceAsset& source, const std::string& candidateHash, double sampleRate, int numChannels,
                          std::int64_t numFrames) noexcept
{
    if (! source.contentHash.empty() && candidateHash == source.contentHash)
        return RelinkResult::exact;
    if (std::abs (sampleRate - source.sampleRate) < 1.0e-6 && numChannels == source.numChannels && numFrames == source.numFrames)
        return RelinkResult::compatible;
    return RelinkResult::incompatible;
}

const char* toString (Articulation a) noexcept
{
    switch (a)
    {
        case Articulation::sustained: return "sustained";
        case Articulation::short_: return "short";
        case Articulation::legato: return "legato";
        case Articulation::noisy: return "noisy";
    }
    return "sustained";
}

const char* toString (AssetOrigin o) noexcept
{
    switch (o)
    {
        case AssetOrigin::fileImport: return "fileImport";
        case AssetOrigin::library: return "library";
        case AssetOrigin::liveCapture: return "liveCapture";
        case AssetOrigin::bufferSnapshot: return "bufferSnapshot";
    }
    return "fileImport";
}

std::optional<AssetOrigin> assetOriginFromString (const std::string& s) noexcept
{
    if (s == "fileImport") return AssetOrigin::fileImport;
    if (s == "library") return AssetOrigin::library;
    if (s == "liveCapture") return AssetOrigin::liveCapture;
    if (s == "bufferSnapshot") return AssetOrigin::bufferSnapshot;
    return std::nullopt;
}

std::optional<Articulation> articulationFromString (const std::string& s) noexcept
{
    if (s == "sustained") return Articulation::sustained;
    if (s == "short") return Articulation::short_;
    if (s == "legato") return Articulation::legato;
    if (s == "noisy") return Articulation::noisy;
    return std::nullopt;
}

} // namespace osp::atlas
