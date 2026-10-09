#pragma once

#include "engine/HostTiming.h"

#include <array>
#include <cstdint>

namespace osp
{

enum class ArpPattern
{
    up = 0,
    down,
    upDown,
    played,
    random,
    chord
};

/** The order is the parameter's choice order (saved in sessions): never reorder. */
enum class ArpRate
{
    quarter = 0,
    eighth,
    sixteenth,
    thirtySecond,
    quarterDotted,
    eighthDotted,
    sixteenthDotted,
    quarterTriplet,
    eighthTriplet,
    sixteenthTriplet
};

namespace arp
{
    constexpr int patternCount = 6;
    constexpr int rateCount = 10;
    constexpr double minGate = 0.10, maxGate = 1.50;
    constexpr double maxSwing = 1.0;   ///< 100 %: every second step half a step late (a dotted feel)
    constexpr int minOctaves = 1, maxOctaves = 4;

    /** One step, in quarter notes. */
    double rateQuarters (ArpRate rate) noexcept;
    const char* patternName (ArpPattern pattern) noexcept;   ///< "UP", "UP/DOWN", ...
    const char* rateName (ArpRate rate) noexcept;            ///< "1/8", "1/8D", "1/8T", ...
}

/**
    The arpeggiator: turns the notes held on the keyboard into a stepped note stream, ahead of
    the instrument's own voice allocation. It sees only note events (and the sustain pedal and
    All Notes Off); everything else in the MIDI stream passes it by. It produces plain note
    events, so every layer, mode and effect behind it plays exactly as if those notes had been
    played by hand: one arpeggiator for the instrument, no per-layer copies.

    Time: while the host plays, steps sit on the host's grid (PPQ multiples of the step), placed
    to the sample; loops and jumps resynchronise without catching up missed steps. When the host
    is stopped (or there is no host), the arpeggiator runs freely at the last tempo the host
    reported (120 BPM without one), and a chord's first step sounds at once.

    Every generated note gets its own note-off (the gate) from a scheduler; note-offs come before
    note-ons at the same sample. The voice engine knows notes by pitch, so a pitch that is still
    sounding (gate above 100 %) is ended right before it is played again; nothing is ever left
    hanging. RANDOM is reproducible: its choices come from osp::Prng, seeded from the stored
    variation seed and a phrase counter that restarts with the host's transport.

    The held-note tracker always runs, also while the arpeggiator is off, so switching it on
    or off mid-performance hands the notes over cleanly (§ transitions in beginBlock). While it
    is off it emits nothing at all: the MIDI stream is not touched.

    Real-time safe: fixed-capacity storage, no allocation, no locks. prepare() sets the rate.
*/
class Arpeggiator
{
public:
    struct Settings
    {
        bool enabled = false;
        ArpPattern pattern = ArpPattern::up;
        ArpRate rate = ArpRate::eighth;
        double gate = 0.75;   ///< of a step, 0.10 .. 1.50
        double swing = 0.0;   ///< 0 .. 1: every second step later by up to half a step
        int octaves = 1;      ///< 1 .. 4
    };

    struct Event
    {
        enum class Kind : std::uint8_t
        {
            noteOff,
            noteOn,
            sustainOff,
            sustainOn
        };
        int offset = 0;   ///< sample in the current block
        Kind kind = Kind::noteOn;
        std::uint8_t note = 0, velocity = 0, channel = 1;
    };

    static constexpr int maxHeld = 64;
    static constexpr int maxSequence = 2 * maxHeld * arp::maxOctaves;   // UP/DOWN goes both ways
    static constexpr int maxEvents = 2048;
    static constexpr int displaySteps = 16;

    void prepare (double sampleRate) noexcept;
    /** Forgets everything (held notes, sounding notes, pedal) without emitting events. */
    void reset() noexcept;
    void setSeed (std::uint64_t seed) noexcept { baseSeed = seed; }
    /** The host's transport started: the pattern and RANDOM's choices start over, so a bounce
        is the same every time. Held notes are kept. */
    void restartPattern() noexcept;

    /** Starts a block: applies settings (turning on or off emits the hand-over events at
        offset 0) and reads the host's time. Clears the previous block's events. */
    void beginBlock (const Settings& settings, const HostTiming& timing, int numSamples) noexcept;
    /** Generates everything due before `offset` (steps, note-offs). Call before each incoming
        event at that offset, and with the block size at the end. */
    void advanceTo (int offset) noexcept;
    void noteOn (int offset, int note, int velocity, int channel) noexcept;
    void noteOff (int offset, int note, int channel) noexcept;
    void sustainPedal (int offset, bool down) noexcept;
    void allNotesOff (int offset) noexcept;
    /** Finishes the block (advances to its end); the clock moves on. */
    void endBlock() noexcept;

    int numEvents() const noexcept { return eventCount; }
    const Event& event (int index) const noexcept { return events[static_cast<std::size_t> (index)]; }

    /** True while the arpeggiator owns the notes (it is on). */
    bool isEnabled() const noexcept { return enabled; }
    /** True while notes are held and the pattern runs. */
    bool isRunning() const noexcept { return running; }
    bool isSynced() const noexcept { return synced; }
    int heldCount() const noexcept { return held; }
    int soundingCount() const noexcept { return sounding; }
    bool pedalIsDown() const noexcept { return pedal; }

    /** What the 16-step display shows: the page of steps around the one sounding. Played
        steps show what played, the rest what will play (RANDOM's real upcoming choices). */
    struct Display
    {
        std::array<std::int8_t, displaySteps> low {}, high {};   ///< -1: no note in that step
        int current = -1;    ///< the column sounding now, -1 before the first step
        bool active = false; ///< notes held, the pattern runs
    };
    void display (Display& out) const noexcept;

private:
    struct Held
    {
        std::uint8_t note = 0, velocity = 0, channel = 1;
        bool down = false;   ///< physically held (false: kept by the sustain pedal)
    };
    struct Step
    {
        std::uint8_t note = 0, velocity = 0, channel = 1;
    };

    // tracking
    void addHeld (int note, int velocity, int channel) noexcept;
    void releaseHeld (int note, int channel) noexcept;
    void dropSustained() noexcept;
    void heldChanged (int offset) noexcept;

    // pattern
    void ensureSequence() const noexcept;
    int stepNotes (std::int64_t step, int previousIndex, std::array<Step, maxHeld>& out, int& index) const noexcept;
    int pickRandom (std::int64_t step, int previousIndex, int length) const noexcept;

    // time
    double stepSamples() const noexcept;
    /** How late a step with this index plays (odd steps, by SWING), in samples. */
    double swingDelay (std::int64_t index) const noexcept;
    double samplesPerQuarter() const noexcept { return sampleRate * 60.0 / bpm; }
    std::int64_t nextStepTime() const noexcept;
    void syncGridTo (int offset) noexcept;
    void startPhrase (int offset) noexcept;
    void fireStep (std::int64_t time) noexcept;

    // output
    bool emit (std::int64_t time, Event::Kind kind, int note, int velocity, int channel) noexcept;
    void endSounding (std::int64_t time) noexcept;
    void handOverToArp() noexcept;
    void handBackToKeys() noexcept;

    double sampleRate = 48000.0;
    std::uint64_t baseSeed = 1;
    Settings settings;
    bool enabled = false, running = false, synced = false;

    std::array<Held, maxHeld> heldNotes {};   ///< in the order they were pressed
    int held = 0;
    bool pedal = false;

    mutable std::array<Step, maxSequence> sequence {};
    mutable int sequenceLength = 0;
    mutable bool sequenceDirty = true;
    mutable std::array<int, maxHeld> sortedIndex {};
    mutable int sortedCount = 0;

    // the clock
    std::int64_t blockStart = 0;   ///< absolute sample of the block's first sample
    int blockSize = 0, cursor = 0;
    double bpm = 120.0, lastKnownBpm = 120.0;
    double ppqStart = 0.0, expectedPpq = 0.0;
    bool hadSync = false;
    std::int64_t gridIndex = 0;    ///< the last fired (or skipped) boundary, in steps of gridQuarters
    double gridQuarters = 0.5;
    double lastStepExact = 0.0;    ///< absolute sample time of the last step on the straight grid (before SWING)
    double phraseStartExact = 0.0;

    // the phrase
    std::int64_t stepCount = 0;    ///< steps played in this phrase
    int previousIndex = -1;
    std::uint64_t phraseSeed = 0, phraseCounter = 0;
    std::array<std::array<std::int8_t, 2>, displaySteps> history {};

    // what sounds: by pitch (the engine releases a pitch's voices together)
    std::array<std::int64_t, 128> offTime {};
    std::array<std::uint8_t, 128> offChannel {};
    int sounding = 0;

    std::array<Event, maxEvents> events {};
    int eventCount = 0;
};

} // namespace osp
