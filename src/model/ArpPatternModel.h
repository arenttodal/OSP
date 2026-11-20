#pragma once

#include "engine/ArpCustomPattern.h"

#include <string>
#include <vector>

namespace osp::arppattern
{

/**
    Custom ARP Patterns as the musician makes and edits them (docs/research/custom-arp): pure
    C++, message thread only (it allocates). A Document keeps the original capture apart from the
    editable adaptive pattern, so a phrase can be reinterpreted later without recording it again;
    compile() turns the pattern into the arpeggiator's fixed-size playback form.

    Time in the pattern is exact integer ticks (arp::ticksPerStep per nominal step, one ARP
    RATE); the capture keeps quarter notes. Pitches in the pattern are relative ranks (0 = the
    lowest pitch the gesture used), never MIDI notes.

    The capture interpretation and the editing operations follow the CP1 lab
    (research/ArpPatternLab: extractContour, stageA, stageB, edit::*), which stays frozen as the
    research record; tests check that both agree.
*/

/** One note of the original capture (quarter notes from the capture's first note). */
struct CapturedNote
{
    double onset = 0.0, duration = 0.25;
    int pitch = 60, velocity = 100;
    bool operator== (const CapturedNote&) const = default;
};

/** Several notes starting together: which one becomes the event (the order is stored). */
enum class ChordPolicy
{
    highest = 0,
    lowest
};

/** How the capture becomes a pattern. The order of Timing is stored: append only. */
struct Interpretation
{
    enum class Timing
    {
        rhythm = 0,   ///< the captured onsets, quantised; silences longer than half a step become rests
        steps         ///< one event per captured note, each one step (the order only)
    };
    Timing timing = Timing::rhythm;
    int quantumTicks = arp::ticksPerStep / 4;   ///< the rhythm's grid (a quarter step by default)
    double stepQuarters = 0.25;                 ///< one step of the capture, in quarters (the ARP RATE it was played against)
    double lengthQuarters = 0.0;                ///< the loop; 0: the capture's own length, rounded up to whole steps
    ChordPolicy chord = ChordPolicy::highest;
    arp::CustomMapping mapping = arp::CustomMapping::contour;
    bool operator== (const Interpretation&) const = default;
};

/** One event of the editable pattern. */
struct Event
{
    int rank = 0;
    bool rest = false;
    int duration = arp::ticksPerStep;   ///< ticks
    int velocity = 0;                   ///< 0: the held key's velocity
    /** A rest's rank means nothing (it plays no note, and the file stores a rest as rank -1), so
        two rests of the same length and velocity are the same event whatever rank they carry. */
    bool operator== (const Event& o) const noexcept
    {
        return rest == o.rest && duration == o.duration && velocity == o.velocity && (rest || rank == o.rank);
    }
};

struct Pattern
{
    std::vector<Event> events;
    int maxRank = 0;   ///< R: the scale CONTOUR maps the ranks on (at least the highest rank used)
    arp::CustomMapping mapping = arp::CustomMapping::contour;

    int lengthTicks() const noexcept;
    int onsetOf (int index) const noexcept;
    int notes() const noexcept;
    int highestRank() const noexcept;
    /** Within arp::CustomPattern's limits, every duration at least arp::minEventTicks, at least
        one note. */
    bool valid() const noexcept;
    bool operator== (const Pattern&) const = default;
};

constexpr int maxCaptureNotes = 1024;   ///< a take's note-ons; beyond it the take stops and says so
constexpr int maxRankLimit = arp::maxPatternRank;

/** What interpreting a capture reports (nothing is ever dropped silently). */
struct InterpretReport
{
    int capturedNotes = 0;     ///< note-ons in the capture
    int placedNotes = 0;       ///< events that are notes
    int chordAttacks = 0;      ///< onsets where several notes started together
    int chordNotesDropped = 0; ///< notes of those chords the policy did not choose
    int beyondLength = 0;      ///< notes starting after the loop's end (not placed)
    int beyondCapacity = 0;    ///< notes past the pattern's capacity (not placed)
};

/** The capture as an editable pattern. An empty capture gives an empty (invalid) pattern. */
Pattern interpret (const std::vector<CapturedNote>& capture, const Interpretation& how, InterpretReport* report = nullptr);

/** The capture's own length in quarters (first onset to last end), before rounding. */
double captureLengthQuarters (const std::vector<CapturedNote>& capture);

/** The arpeggiator's playback form (an invalid pattern compiles to an invalid one). */
arp::CustomPattern compile (const Pattern& pattern);
/** Back from the playback form (a pattern saved by an older build, or a test). */
Pattern decompile (const arp::CustomPattern& compiled);

/**
    The editing operations. Each one is a whole gesture (the editor keeps one undo step per
    call), returns false and changes nothing when it cannot apply, and keeps the pattern's length
    unless its name says otherwise (insertStep, deleteEvent and setLength change it, explicitly).
*/
namespace edit
{
    bool setRank (Pattern&, int index, int rank);          ///< a note's rank (a rest becomes that note); R grows to fit
    bool split (Pattern&, int index);                      ///< two equal halves, same rank (a rest: two rests)
    bool merge (Pattern&, int index);                      ///< index and the next: same rank (or both rests)
    bool extend (Pattern&, int index, int ticks);          ///< into the rest after it, never over a note
    bool toRest (Pattern&, int index);                     ///< keeps its time
    bool remove (Pattern&, int index);                     ///< a note becomes a rest (its time kept)
    bool insertStep (Pattern&, int index);                 ///< LENGTH + one step: a rest step before `index` (at the end for size())
    bool deleteEvent (Pattern&, int index);                ///< LENGTH - the event's time: the event and its time go
    void reverse (Pattern&);
    void mirror (Pattern&);                                ///< rank -> R - rank
    void rotate (Pattern&, int events);
    bool setLength (Pattern&, int ticks);                  ///< longer: a rest at the end; shorter: only trailing rest is cut
    void setMapping (Pattern&, arp::CustomMapping);
    /** R2, an edge dragged on the grid (the pattern's length kept). The end of event `index` moves
        so that it lasts `duration` ticks: longer takes the time from the rest after it (never
        from a note), shorter gives it to that rest (or a new rest). Every event, and any rest
        left, keeps at least arp::minEventTicks. */
    bool resizeEnd (Pattern&, int index, int duration);
    /** The start of event `index` moves (its end stays) so that it lasts `duration` ticks: the
        time comes from, or goes to, the rest before it (or a new rest). Not for the first event. */
    bool resizeStart (Pattern&, int index, int duration);
    /** A note of `duration` ticks, rank `rank`, `offset` ticks into the rest `index`: what is left
        of the rest on either side stays rests (each at least arp::minEventTicks, or none). */
    bool placeNote (Pattern&, int index, int offset, int duration, int rank);
}

/** A bounded undo history of whole patterns (one entry per completed gesture). */
class History
{
public:
    static constexpr int capacity = 200;
    void reset (const Pattern& now);
    /** After a successful edit: `now` is the new state. */
    void push (const Pattern& now);
    bool canUndo() const noexcept { return position > 0; }
    bool canRedo() const noexcept { return position + 1 < static_cast<int> (states.size()); }
    const Pattern& undo();
    const Pattern& redo();
    const Pattern& current() const
    {
        static const Pattern none;
        return states.empty() ? none : states[static_cast<std::size_t> (position)];
    }

private:
    std::vector<Pattern> states;
    int position = 0;
};

/**
    A bounded undo history of whole states (R2: the editor keeps whole documents, so undoing a
    rebuild from the take restores TIMING and MAPPING with the pattern). Header-only.
*/
template <typename State>
class SnapshotHistory
{
public:
    static constexpr int capacity = 200;
    void reset (const State& now)
    {
        states.assign (1, now);
        position = 0;
    }
    /** After a successful edit: `now` is the new state (a new edit drops the redo branch). */
    void push (const State& now)
    {
        if (states.empty())
        {
            reset (now);
            return;
        }
        states.resize (static_cast<std::size_t> (position + 1));
        states.push_back (now);
        if (static_cast<int> (states.size()) > capacity)
            states.erase (states.begin());
        position = static_cast<int> (states.size()) - 1;
    }
    bool canUndo() const noexcept { return position > 0; }
    bool canRedo() const noexcept { return position + 1 < static_cast<int> (states.size()); }
    const State& undo()
    {
        if (canUndo())
            --position;
        return current();
    }
    const State& redo()
    {
        if (canRedo())
            ++position;
        return current();
    }
    const State& current() const
    {
        static const State none {};
        return states.empty() ? none : states[static_cast<std::size_t> (position)];
    }
    int size() const noexcept { return static_cast<int> (states.size()); }
    /** Changes every state at once (what is not an undo step, such as a pattern's name). */
    template <typename Fn>
    void updateAll (Fn fn)
    {
        for (auto& s : states)
            fn (s);
    }

private:
    std::vector<State> states;
    int position = 0;
};

/**
    A saved pattern (CP4: the pattern library, sessions and presets): product-neutral and
    versioned (io/ArpPatternJson). The id is stable across renames and copies of the file; a
    duplicate gets a new one. Factory patterns are read-only.
*/
struct Document
{
    static constexpr int schemaVersion = 1;
    std::string id;
    std::string name;
    std::string description;
    std::vector<CapturedNote> capture;   ///< the original take (may be empty: a pattern made by editing)
    double captureBpm = 120.0;
    Interpretation interpretation;
    Pattern pattern;                      ///< what plays (the edited adaptive pattern)
    bool factory = false;
    bool operator== (const Document&) const = default;
};

} // namespace osp::arppattern
