#pragma once

#include "midi/MidiEvent.h"

#include <filesystem>
#include <string>
#include <vector>

namespace osp::research::arplab
{

/**
    Custom ARP Patterns, CP1 (docs/research/custom-arp): a headless prototype of how a captured
    phrase becomes an adaptive pattern, and how that pattern is played over whatever notes are
    held. Nothing here runs in the plugin. The arpeggiator itself is untouched; the lab steps a
    plain grid (Stage A: one captured note-on is one step, the ARP's own RATE and GATE).

    Times are in quarter notes; pitches are MIDI note numbers.
*/
struct CapturedNote
{
    double onset = 0.0, duration = 0.25;
    int pitch = 60, velocity = 100;
};

/** One of the lab's reference gestures (original phrases written for the lab). */
struct Gesture
{
    std::string name, description;
    std::vector<CapturedNote> notes;
    double lengthQuarters = 4.0;   ///< the phrase's loop length (trailing silence kept)
};

/** The reference corpus: ascending motifs, pedal basses, thumb leaps, two-pitch rhythms, arches,
    wide jumps, rests, short and long lines, a single note, legato overlaps and a chord attack. */
std::vector<Gesture> referenceGestures();

/** Several notes starting together in a capture: which one is the step. */
enum class ChordPolicy
{
    highest,
    lowest
};

/** A captured phrase as an adaptive pattern (Stage A: the order of its notes). */
struct Contour
{
    std::vector<int> ranks;          ///< one per step: 0 = the lowest captured pitch
    std::vector<int> pitches;        ///< the distinct captured pitches, ascending (rank -> pitch)
    std::vector<double> onsets;      ///< each step's onset (quarters), for Stage B
    std::vector<int> velocities;     ///< each step's captured velocity, for Stage C
    std::vector<double> durations;   ///< each step's captured duration (quarters), for Stage B rests
    int maxRank = 0;                 ///< R
    int chordAttacks = 0;            ///< onsets where several notes started together
    int notesDropped = 0;            ///< notes of those chords not chosen by the policy
};

/** Notes starting within `together` quarters of each other are one attack. */
Contour extractContour (const std::vector<CapturedNote>& notes, ChordPolicy policy = ChordPolicy::highest, double together = 1.0 / 32.0);

/** How a rank picks a note from the available pool (N notes, ascending). */
enum class Mapping
{
    adaptiveContour,   ///< index = round (r / R * (N - 1)): the shape stretched over the pool
    degreeCycle,       ///< index = r mod N
    contourFit,        ///< the lab's candidate: r itself while the pool is large enough (N > R), else compressed like adaptive
    fitOctaveCycles,   ///< contour-fit over the held notes only; OCTAVES repeats the whole pattern an octave higher per cycle
    hybrid             ///< contour-fit over the fewest octaves the gesture needs, then the whole gesture at each higher octave position that fits
};
const char* mappingName (Mapping m) noexcept;

/** The pool index for rank r (R = highest rank, N = pool size); -1 for an empty pool. Integer
    arithmetic only (round half up), so it is exact and identical everywhere. */
int mapRank (Mapping m, int r, int R, int N) noexcept;

/** The arpeggiator's pool: held notes ascending, repeated an octave up per extra octave, notes
    above 127 left out (as Arpeggiator::ensureSequence builds it). */
std::vector<int> notePool (std::vector<int> held, int octaves);

/** The notes a contour plays over a pool, one per step (an empty pool: none). */
std::vector<int> interpret (const Contour& contour, const std::vector<int>& pool, Mapping mapping);

/** The hybrid's plan for one held chord: the base pool (the fewest octaves, within OCTAVES,
    whose distinct notes hold every rank), the octave positions the whole gesture can climb to
    (each a shift whose every used note is in the full pool, so wide voicings, duplicates and
    the MIDI top are handled by the pool itself), and whether it had to compress. */
struct HybridPlan
{
    std::vector<int> base;        ///< distinct, ascending
    std::vector<int> positions;   ///< semitone shifts, from 0 upwards
    int baseOctaves = 1;
    bool compressed = false;
};
HybridPlan planHybrid (const std::vector<int>& held, int octaves, int maxRank);

/** The note a rank plays in a given pattern cycle (-1: none, the pool is empty). */
int noteFor (Mapping mapping, int rank, int maxRank, const std::vector<int>& held, int octaves, long long cycle);

/** One full cycle as played: one pattern length, OCTAVES lengths for fitOctaveCycles (each an
    octave higher; a note above 127 drops by octaves until it fits), one length per octave
    position for the hybrid. */
std::vector<int> playLine (const Contour& contour, const std::vector<int>& held, int octaves, Mapping mapping);

/** How well a played line keeps the captured gesture. */
struct Fidelity
{
    double directionAgreement = 0.0;   ///< share of consecutive steps moving the same way (up, down, same)
    int distinctCaptured = 0, distinctPlayed = 0;
    int collapsedRanks = 0;            ///< captured ranks sharing a note with another rank
    int maxLeapCaptured = 0, maxLeapPlayed = 0;   ///< semitones
    int spanPlayed = 0;
    bool repeatsKept = true;           ///< every repeated captured pitch is a repeated played note
};
Fidelity measure (const Contour& contour, const std::vector<int>& played);

/** A Standard MIDI File of the gesture as captured (120 BPM). */
MidiSequence capturedSequence (const Gesture& g, double bpm = 120.0);

/** One chord of the listening progression: the held notes, OCTAVES, bars. */
struct HeldChord
{
    std::string name;
    std::vector<int> notes;
    int octaves = 1;
    int bars = 1;
};
/** One note, an interval, a major and a minor triad, a seventh, an extended chord, and the
    seventh again over two octaves. */
std::vector<HeldChord> listeningProgression();
/** For OCTAVES: a triad over 4 octaves, one note over 4, a seventh over 2, a six-note voicing
    over 3; two bars each (enough cycles to hear the octave positions move). */
std::vector<HeldChord> octavesProgression();

/** The contour played over the progression on a 1/16 grid at `bpm`, GATE 75 %, the pattern's
    phase running on across chord changes (as the arpeggiator keeps its step count). */
MidiSequence progressionSequence (const Contour& contour, const std::vector<HeldChord>& chords, Mapping mapping, double bpm = 120.0);

// ---------------------------------------------------------------------------------------------
// Rhythmic patterns (ARP STRUM & CUSTOM PATTERN RHYTHMIC EDITING, Part II): events of variable
// duration in exact integer ticks. One nominal step (one ARP RATE) is `ticksPerStep`; onsets are
// the running sum of durations, so they never drift and the pattern's length is independent of
// how many events it has.

constexpr int ticksPerStep = 960;            ///< halves to 30, thirds to 40: 1/2, 1/3, 1/4, 1/6, 1/8, ... exact
constexpr int minEventTicks = 30;            ///< a thirty-second of a step
constexpr int maxPatternSteps = 128;
constexpr int maxPatternEvents = 256;

/** One event: a relative pitch rank, or a rest, for its rhythmic duration. Version 1 is
    monophonic; a later chord event would hold several ranks here (the boundary is this struct,
    which nothing outside the pattern code reads directly). */
struct PatternEvent
{
    int rank = 0;
    bool rest = false;
    int duration = ticksPerStep;   ///< ticks
    int velocity = 0;              ///< 0: the held key's velocity (Stage C may store a captured one)
    bool operator== (const PatternEvent&) const = default;
};

struct RhythmPattern
{
    std::vector<PatternEvent> events;
    int maxRank = 0;   ///< R: the highest rank the pattern was made with (the mapping's scale)

    int lengthTicks() const noexcept;
    int onsetOf (int index) const noexcept;
    int notes() const noexcept;   ///< events that are not rests
    bool valid() const noexcept;  ///< within the capacity, every duration >= minEventTicks
    bool operator== (const RhythmPattern&) const = default;
};

/** Stage A: one step per captured note (the order only). */
RhythmPattern stageA (const Contour& contour);
/** Stage B: the captured onsets quantised to `quantum` ticks of a `stepQuarters` grid. A note
    lasts to the next one unless a silence longer than half a step follows it: then it ends at its
    captured end (rounded up to the quantum) and a rest fills the gap. The loop is
    `lengthQuarters` long (its trailing silence kept as a rest). Notes beyond the loop's length
    are not placed (the caller compares notes() with the capture and says so). */
RhythmPattern stageB (const std::vector<CapturedNote>& notes, double stepQuarters, int quantum, double lengthQuarters,
                      ChordPolicy policy = ChordPolicy::highest);

/** The editing operations. Each keeps the pattern's length (only setLength changes it), returns
    false and changes nothing when it cannot apply. */
namespace edit
{
    bool split (RhythmPattern&, int index);                     ///< two equal halves, same rank (a rest: two rests)
    bool merge (RhythmPattern&, int index);                     ///< index and the next: same rank (or both rests), contiguous
    bool extend (RhythmPattern&, int index, int ticks);         ///< into the rest after it (never over a note)
    bool toRest (RhythmPattern&, int index);
    bool toNote (RhythmPattern&, int index, int rank);
    bool remove (RhythmPattern&, int index);                    ///< a note becomes a rest (its time kept)
    void reverse (RhythmPattern&);
    void mirror (RhythmPattern&);                               ///< rank -> R - rank
    void rotate (RhythmPattern&, int events);
    bool setLength (RhythmPattern&, int ticks);                 ///< longer: a rest at the end; shorter: only trailing rest is cut
}

/** Where a tick of the nominal grid lands with SWING (0..1): odd steps start up to half a step
    late, and a subdivision keeps its relative place inside its (swung) step, so splitting an
    event never swings it on its own. In ticks (fractional). */
double swungTick (long long tick, double swing) noexcept;

/** A rhythmic pattern over the listening progression: ARP RATE 1/16 at `bpm` is one step, GATE
    of each event's own duration, SWING on the nominal grid; the pool changes at each bar. */
MidiSequence rhythmSequence (const RhythmPattern& pattern, const std::vector<HeldChord>& chords, Mapping mapping, double bpm = 120.0,
                             double gate = 0.75, double swing = 0.0);

struct LabResult
{
    std::string markdown;
    int midiFiles = 0, audioFiles = 0;
};

/** Writes under `outDir`: fixtures/<gesture>.mid, midi/<gesture>-<mapping>.mid, audio (when
    `writeAudio`) through baseline A with a synthetic pluck, and report.md: every gesture over
    1 to 6 held notes and 1 to 4 octaves for each mapping, measured. */
LabResult runArpPatternLab (const std::filesystem::path& outDir, bool writeAudio);

} // namespace osp::research::arplab
