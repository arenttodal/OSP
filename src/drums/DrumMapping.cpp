#include "drums/DrumMapping.h"

#include <algorithm>
#include <cstdlib>

namespace osp::drums
{

namespace
{
    bool validPad (int pad) noexcept { return pad >= 0 && pad < maxPads; }
    bool validNote (int note) noexcept { return note >= 0 && note <= 127; }
    std::size_t at (int pad) noexcept { return static_cast<std::size_t> (pad); }

    EditResult refused (std::string message, std::vector<int> pads = {})
    {
        EditResult r;
        r.message = std::move (message);
        r.collidingPads = std::move (pads);
        return r;
    }

    EditResult done()
    {
        EditResult r;
        r.applied = true;
        return r;
    }

    /** Applies `candidate` to `pad` under `policy`; `map` is untouched when refused. */
    EditResult place (KeyMap& map, int pad, Zone candidate, Collision policy)
    {
        if (! candidate.enabled())
            return refused ("A zone must lie within MIDI notes 0-127.");
        if (candidate.root >= 0 && ! candidate.contains (candidate.root))
            candidate.root = noNote;
        auto others = collisions (map, pad, candidate);
        if (others.empty() || (policy == Collision::overlap && map.allowOverlap))
        {
            map.zone[at (pad)] = candidate;
            return done();
        }
        if (policy == Collision::swap && others.size() == 1)
        {
            // The other zone takes this pad's old place (its own width and root offset kept);
            // only if that place is free of everyone else.
            const int other = others.front();
            const Zone old = map.zone[at (pad)];
            Zone moved = map.zone[at (other)];
            if (old.enabled())
            {
                const int shift = old.low - moved.low;
                moved.low += shift;
                moved.high += shift;
                if (moved.root >= 0)
                    moved.root += shift;
            }
            if (! moved.enabled())
                return refused ("There is no room to swap these zones.", std::move (others));
            KeyMap trial = map;
            trial.zone[at (pad)] = candidate;
            trial.zone[at (other)] = moved;
            for (int p = 0; p < maxPads; ++p)
                for (int q = p + 1; q < maxPads; ++q)
                    if (trial.zone[at (p)].overlaps (trial.zone[at (q)]) && ! (map.zone[at (p)].overlaps (map.zone[at (q)])))
                        return refused ("Swapping would overlap another zone.", std::move (others));
            map = trial;
            return done();
        }
        return refused ("That range is already used by another pad.", std::move (others));
    }
} // namespace

const char* toString (KeyboardMode mode) noexcept
{
    return mode == KeyboardMode::expanded ? "expanded" : "pad";
}

KeyboardMode keyboardModeFromString (const std::string& text, KeyboardMode fallback) noexcept
{
    if (text == "expanded")
        return KeyboardMode::expanded;
    if (text == "pad")
        return KeyboardMode::pad;
    return fallback;
}

std::array<int, maxPads> defaultPadNotes()
{
    return { 36, 38, 42, 46, 39, 41, 37, 43, 49 };
}

std::array<Zone, maxPads> defaultZones()
{
    std::array<Zone, maxPads> zones {};
    for (int p = 0; p < maxPads; ++p)
        zones[at (p)] = Zone { 12 + 12 * p, 23 + 12 * p, noNote };
    return zones;
}

KeyMap KeyMap::defaults()
{
    KeyMap map;
    map.padNote = defaultPadNotes();
    map.zone = defaultZones();
    return map;
}

int resolve (const KeyMap& map, KeyboardMode mode, int note, std::array<Hit, maxPads>& hits) noexcept
{
    int count = 0;
    if (! validNote (note))
        return 0;
    for (int p = 0; p < maxPads; ++p)
    {
        if (mode == KeyboardMode::pad)
        {
            if (map.padNote[at (p)] == note)
                hits[static_cast<std::size_t> (count++)] = Hit { p, 0 };
        }
        else
        {
            const auto& z = map.zone[at (p)];
            if (z.contains (note))
                hits[static_cast<std::size_t> (count++)] = Hit { p, note - z.rootNote() };
        }
    }
    return count;
}

std::vector<int> collisions (const KeyMap& map, int pad, const Zone& candidate)
{
    std::vector<int> pads;
    for (int p = 0; p < maxPads; ++p)
        if (p != pad && map.zone[at (p)].overlaps (candidate))
            pads.push_back (p);
    return pads;
}

EditResult moveZone (KeyMap& map, int pad, int newLow, Collision policy)
{
    if (! validPad (pad))
        return refused ("No such pad.");
    const Zone z = map.zone[at (pad)];
    if (! z.enabled())
        return refused ("This pad has no zone to move.");
    const int shift = newLow - z.low;
    Zone moved { z.low + shift, z.high + shift, z.root >= 0 ? z.root + shift : noNote };
    if (! validNote (moved.low) || ! validNote (moved.high))
        return refused ("The zone would leave MIDI notes 0-127.");
    return place (map, pad, moved, policy);
}

EditResult resizeZone (KeyMap& map, int pad, int newLow, int newHigh, Collision policy)
{
    if (! validPad (pad))
        return refused ("No such pad.");
    if (! validNote (newLow) || ! validNote (newHigh) || newHigh < newLow)
        return refused ("A zone must lie within MIDI notes 0-127 and hold at least one key.");
    return place (map, pad, Zone { newLow, newHigh, map.zone[at (pad)].root }, policy);
}

EditResult setZoneRoot (KeyMap& map, int pad, int root)
{
    if (! validPad (pad))
        return refused ("No such pad.");
    auto& z = map.zone[at (pad)];
    if (root != noNote && ! z.contains (root))
        return refused ("The root must lie inside the pad's zone.");
    z.root = root;
    return done();
}

EditResult swapZones (KeyMap& map, int padA, int padB)
{
    if (! validPad (padA) || ! validPad (padB) || padA == padB)
        return refused ("Choose two different pads.");
    std::swap (map.zone[at (padA)], map.zone[at (padB)]);
    return done();
}

EditResult swapPadNotes (KeyMap& map, int padA, int padB)
{
    if (! validPad (padA) || ! validPad (padB) || padA == padB)
        return refused ("Choose two different pads.");
    std::swap (map.padNote[at (padA)], map.padNote[at (padB)]);
    return done();
}

EditResult setPadNote (KeyMap& map, int pad, int note, bool layer)
{
    if (! validPad (pad))
        return refused ("No such pad.");
    if (note != noNote && ! validNote (note))
        return refused ("Notes are MIDI 0-127.");
    if (note != noNote && ! layer)
    {
        std::vector<int> others;
        for (int p = 0; p < maxPads; ++p)
            if (p != pad && map.padNote[at (p)] == note)
                others.push_back (p);
        if (! others.empty())
            return refused ("Another pad already answers that note.", others);
    }
    map.padNote[at (pad)] = note;
    return done();
}

EditResult clearZone (KeyMap& map, int pad)
{
    if (! validPad (pad))
        return refused ("No such pad.");
    map.zone[at (pad)] = Zone {};
    return done();
}

int nearestFreeStart (const KeyMap& map, int width, int preferredLow, int ignorePad)
{
    if (width < 1 || width > 128)
        return -1;
    int best = -1;
    for (int low = 0; low + width - 1 <= 127; ++low)
    {
        const Zone candidate { low, low + width - 1, noNote };
        bool free = true;
        for (int p = 0; p < maxPads && free; ++p)
            if (p != ignorePad && map.zone[at (p)].overlaps (candidate))
                free = false;
        if (free && (best < 0 || std::abs (low - preferredLow) < std::abs (best - preferredLow)))
            best = low;
    }
    return best;
}

EditResult moveZoneToFreeSpace (KeyMap& map, int pad)
{
    if (! validPad (pad) || ! map.zone[at (pad)].enabled())
        return refused ("This pad has no zone to move.");
    const auto z = map.zone[at (pad)];
    const int low = nearestFreeStart (map, z.width(), z.low, pad);
    if (low < 0)
        return refused ("There is no free range of " + std::to_string (z.width()) + " keys for this zone.");
    return moveZone (map, pad, low, Collision::refuse);
}

int widestFittingWidth (int pads) noexcept
{
    return pads > 0 ? std::min (128, 128 / pads) : 128;
}

EditResult applyStandardPadNotes (KeyMap& map)
{
    map.padNote = defaultPadNotes();
    return done();
}

EditResult applyChromaticPadNotes (KeyMap& map, const std::vector<int>& order, int startNote)
{
    std::vector<int> pads;
    for (int p : order)
        if (validPad (p) && std::find (pads.begin(), pads.end(), p) == pads.end())
            pads.push_back (p);
    if (pads.empty())
        return refused ("No pads to lay out.");
    const int start = std::clamp (startNote, 0, 128 - static_cast<int> (pads.size()));
    map.padNote.fill (noNote);
    for (std::size_t i = 0; i < pads.size(); ++i)
        map.padNote[at (pads[i])] = start + static_cast<int> (i);
    return done();
}

EditResult applyStandardZones (KeyMap& map)
{
    const auto notes = defaultPadNotes();
    for (int p = 0; p < maxPads; ++p)
        map.zone[at (p)] = Zone { notes[at (p)], notes[at (p)], noNote };
    return done();
}

std::vector<int> changedPads (const KeyMap& before, const KeyMap& after, KeyboardMode mode)
{
    std::vector<int> pads;
    for (int p = 0; p < maxPads; ++p)
    {
        const bool changed = mode == KeyboardMode::pad ? before.padNote[at (p)] != after.padNote[at (p)]
                                                       : ! (before.zone[at (p)] == after.zone[at (p)]);
        if (changed)
            pads.push_back (p);
    }
    return pads;
}

int widthOf (ZonePreset preset) noexcept
{
    switch (preset)
    {
        case ZonePreset::compact: return 1;
        case ZonePreset::oneOctave: return 12;
        case ZonePreset::twoOctaves: return 24;
    }
    return 12;
}

EditResult applyZonePreset (KeyMap& map, ZonePreset preset, const std::vector<int>& order, int startNote)
{
    return applyZoneLayout (map, widthOf (preset), order, startNote);
}

EditResult applyZoneLayout (KeyMap& map, int width, const std::vector<int>& order, int startNote)
{
    std::vector<int> pads;
    for (int p : order)
        if (validPad (p) && std::find (pads.begin(), pads.end(), p) == pads.end())
            pads.push_back (p);
    if (pads.empty())
        return refused ("No pads to lay out.");
    if (width < 1)
        return refused ("A zone holds at least one key.");
    const int needed = width * static_cast<int> (pads.size());
    if (needed > 128)
    {
        const int fits = widestFittingWidth (static_cast<int> (pads.size()));
        return refused (std::to_string (pads.size()) + " zones of " + std::to_string (width)
                        + " keys need " + std::to_string (needed) + " keys; MIDI has 128. Try "
                        + std::to_string (fits) + " keys per pad or fewer pads.");
    }
    const int start = std::clamp (startNote, 0, 128 - needed);
    for (auto& z : map.zone)
        z = Zone {};
    for (std::size_t i = 0; i < pads.size(); ++i)
    {
        const int low = start + width * static_cast<int> (i);
        map.zone[at (pads[i])] = Zone { low, low + width - 1, noNote };
    }
    return done();
}

} // namespace osp::drums
