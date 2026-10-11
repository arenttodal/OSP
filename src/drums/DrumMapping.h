#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

/**
    ANDOR Drums: how incoming MIDI notes reach the pads (pure C++, no JUCE, real-time safe
    to read). Two independent maps live side by side and switching between them never
    edits either one:

      PAD       every pad answers one note (GM-style defaults, editable)
      EXPANDED  every pad owns a contiguous key range; a key plays the pad transposed by
                (key - root) semitones, the root being the zone's lowest key unless the
                pad has its own root override

    Pads are identified by a stable index 0..8 (their identity, not their position on
    screen). Editing functions run on the message thread and return what they did; the
    audio thread only calls resolve().
*/
namespace osp::drums
{

constexpr int maxPads = 9;
constexpr int noNote = -1;

enum class KeyboardMode
{
    pad,
    expanded
};

const char* toString (KeyboardMode mode) noexcept;
KeyboardMode keyboardModeFromString (const std::string& text, KeyboardMode fallback = KeyboardMode::pad) noexcept;

/** One pad's key range in EXPANDED mode (inclusive MIDI notes). */
struct Zone
{
    int low = noNote;    ///< noNote: the pad has no zone (EXPANDED leaves it silent)
    int high = noNote;
    int root = noNote;   ///< noNote: the lowest key is the natural pitch

    bool enabled() const noexcept { return low >= 0 && high >= low && high <= 127; }
    int width() const noexcept { return enabled() ? high - low + 1 : 0; }
    int rootNote() const noexcept { return root >= 0 ? root : low; }
    bool contains (int note) const noexcept { return enabled() && note >= low && note <= high; }
    bool overlaps (const Zone& other) const noexcept
    {
        return enabled() && other.enabled() && low <= other.high && other.low <= high;
    }
    bool operator== (const Zone&) const = default;
};

struct KeyMap
{
    std::array<int, maxPads> padNote {};    ///< PAD mode: the note each pad answers (noNote = none)
    std::array<Zone, maxPads> zone {};      ///< EXPANDED mode
    /** EXPANDED: zones may overlap (an advanced, deliberate choice: one key then plays every
        pad whose zone holds it). Off: every edit keeps the zones exclusive. */
    bool allowOverlap = false;

    /** PAD notes 36 38 42 46 39 41 37 43 49 (kick, snare, closed hat, open hat, clap, tom,
        rim, percussion, extra); EXPANDED: defaultZones(). */
    static KeyMap defaults();
    bool operator== (const KeyMap&) const = default;
};

/** A note's effect on one pad. */
struct Hit
{
    int pad = 0;
    int semitones = 0;   ///< transposition from the pad's natural pitch
};

/**
    Every pad a note plays in `mode` (none, one, or - only where the musician deliberately
    configured it - several). Real-time safe. Returns the number of hits written.
*/
int resolve (const KeyMap& map, KeyboardMode mode, int note, std::array<Hit, maxPads>& hits) noexcept;

// ---------------------------------------------------------------------------------------
// Editing (message thread). Nothing is ever changed silently: a refused edit leaves the
// map untouched and says why; collisions name the pads in the way.
// ---------------------------------------------------------------------------------------

enum class Collision
{
    refuse,   ///< an edit that would overlap another zone is refused (the default)
    swap,     ///< moving onto exactly one other zone exchanges the two zones
    overlap   ///< allowed only when KeyMap::allowOverlap is on
};

struct EditResult
{
    bool applied = false;
    std::vector<int> collidingPads;   ///< pads whose zones were in the way
    std::string message;              ///< empty on success
};

/** Pads (other than `pad`) whose zones overlap `candidate`. */
std::vector<int> collisions (const KeyMap& map, int pad, const Zone& candidate);

/** Moves a zone so it starts at `newLow`, keeping its width and its root's place in it. */
EditResult moveZone (KeyMap& map, int pad, int newLow, Collision policy = Collision::refuse);
/** New bounds for a zone (resizing either edge); the root override is kept if it still
    lies inside, otherwise cleared (back to the lowest key). */
EditResult resizeZone (KeyMap& map, int pad, int newLow, int newHigh, Collision policy = Collision::refuse);
/** Root override (noNote = the lowest key). Must lie inside the zone. */
EditResult setZoneRoot (KeyMap& map, int pad, int root);
/** Exchanges two pads' zones (bounds and roots). */
EditResult swapZones (KeyMap& map, int padA, int padB);
/** Removes a pad's zone (EXPANDED leaves it silent until it gets a new one). */
EditResult clearZone (KeyMap& map, int pad);
/**
    Moves `pad`'s zone (same width and root offset) to the free place nearest its current
    one: how a collision is resolved by moving the zone that was in the way. Refused when
    no free place of that width exists.
*/
EditResult moveZoneToFreeSpace (KeyMap& map, int pad);
/** The lowest note a zone of `width` keys could start at, nearest to `preferredLow`, without
    overlapping any zone except `ignorePad`'s; -1 when none fits. */
int nearestFreeStart (const KeyMap& map, int width, int preferredLow, int ignorePad = -1);
/** Exchanges two pads' PAD-mode notes. */
EditResult swapPadNotes (KeyMap& map, int padA, int padB);
/** PAD mode: assigns a note. Refused if another pad already answers it, unless `layer`. */
EditResult setPadNote (KeyMap& map, int pad, int note, bool layer = false);

/** One-click zone layouts for the pads in `order` (display order; others lose their zones). */
enum class ZonePreset
{
    compact,       ///< one key per pad, side by side
    oneOctave,     ///< 12 keys per pad
    twoOctaves     ///< 24 keys per pad
};
int widthOf (ZonePreset preset) noexcept;
/**
    Lays out contiguous zones of the preset's width starting at `startNote` (moved down if
    needed so the last zone ends at or below 127). Refused - with the widest width that
    would fit suggested in the message - when they cannot all fit in 0..127.
*/
EditResult applyZonePreset (KeyMap& map, ZonePreset preset, const std::vector<int>& order, int startNote = 36);
/** applyZonePreset with any width (1..128 keys per pad). */
EditResult applyZoneLayout (KeyMap& map, int width, const std::vector<int>& order, int startNote = 36);
/** The widest zone width that lets `pads` zones fit in MIDI 0..127. */
int widestFittingWidth (int pads) noexcept;

// PAD-mode layouts (Auto Map).
/** The factory GM-style notes, by pad identity (Standard Drum Kit). */
EditResult applyStandardPadNotes (KeyMap& map);
/** One adjacent key per pad from `startNote`, in display order; pads not in `order` lose
    their notes (Compact Chromatic). */
EditResult applyChromaticPadNotes (KeyMap& map, const std::vector<int>& order, int startNote = 36);
/** Standard Drum Kit for EXPANDED: one-key zones on the factory GM notes. */
EditResult applyStandardZones (KeyMap& map);

/** Pads that would answer differently after `after` replaces `before` in `mode`. */
std::vector<int> changedPads (const KeyMap& before, const KeyMap& after, KeyboardMode mode);

/** The factory EXPANDED layout: one octave per pad from C0 (MIDI 12; MIDI 12..119). */
std::array<Zone, maxPads> defaultZones();
std::array<int, maxPads> defaultPadNotes();

} // namespace osp::drums
