#pragma once

#include <array>
#include <atomic>
#include <cstdint>

namespace osp::arp
{

/**
    Custom ARP Patterns (docs/research/custom-arp): a recorded gesture the arpeggiator plays over
    whatever notes are held. Pure C++, fixed size, no allocation: the arpeggiator holds one by
    value, and copying it (as the AUX follower copies an Arpeggiator) copies the pattern.

    Time is exact integer ticks: one nominal step (one ARP RATE) is ticksPerStep. Onsets are the
    running sum of the events' durations, so a repeating pattern never drifts and its length is
    independent of how many events it has. ARP RATE scales the whole pattern; SWING stays on the
    nominal grid (a subdivision keeps its place inside its swung step).
*/
constexpr int ticksPerStep = 960;          ///< halves to 30, thirds to 40: 1/2, 1/3, 1/4, 1/6, 1/8 ... exact
constexpr int minEventTicks = 30;          ///< a thirty-second of a step
constexpr int maxPatternSteps = 128;
constexpr int maxPatternEvents = 256;
constexpr int maxPatternRank = 127;

/** How a rank picks a note from what is held. The order is the stored value: append only. */
enum class CustomMapping : std::uint8_t
{
    contour = 0,   ///< CONTOUR (the approved hybrid): the gesture's shape over the fewest octaves it needs, then the whole gesture an octave higher per cycle while it fits
    degrees        ///< DEGREES: rank r plays the r-th note of the pool, wrapping
};
constexpr int customMappingCount = 2;
const char* customMappingName (CustomMapping mapping) noexcept;   ///< "CONTOUR", "DEGREES"

/** One event: a relative pitch rank, or a rest, for its rhythmic duration. Version 1 is
    monophonic. */
struct CustomEvent
{
    std::int16_t rank = 0;
    bool rest = false;
    std::uint8_t velocity = 0;         ///< 0: the held key's velocity
    std::int32_t duration = ticksPerStep;
    bool operator== (const CustomEvent&) const = default;
};

struct CustomPattern
{
    std::array<CustomEvent, maxPatternEvents> events {};
    int count = 0;
    int maxRank = 0;                   ///< R: the highest rank the gesture was made with (the mapping's scale)
    CustomMapping mapping = CustomMapping::contour;

    int lengthTicks() const noexcept;
    /** 1..maxPatternEvents events, each at least minEventTicks, at most maxPatternSteps long,
        ranks within 0..maxRank, maxRank within 0..maxPatternRank, at least one note. */
    bool valid() const noexcept;
    bool operator== (const CustomPattern& other) const noexcept;
};

/** One held note in the pool (the arpeggiator's step note). */
struct PoolNote
{
    std::uint8_t note = 0, velocity = 0, channel = 1;
};

/**
    The notes a pattern can reach over the held chord, prepared once per change of the held
    notes, OCTAVES or the pattern's R (never per event):
    - the degree pool: the held notes ascending, repeated an octave up per extra octave, notes
      above 127 left out (as the arpeggiator's UP walks them);
    - CONTOUR's plan: the base pool (distinct notes of the fewest octaves within OCTAVES that
      hold every rank) and the octave positions (every shift, contiguous from 0, at which every
      note the gesture uses is a real pool note: wide voicings, duplicates and the MIDI top
      follow from the pool, nothing is clamped). Too few notes: contour-fit's compression and a
      single position.
*/
struct CustomPool
{
    static constexpr int capacity = 64 * 4;
    std::array<PoolNote, capacity> degree {};
    int degreeCount = 0;
    std::array<PoolNote, capacity> base {};
    int baseCount = 0;
    std::array<std::int8_t, 4> positions {};   ///< semitone shifts
    int positionCount = 0;
    int baseOctaves = 1;
    bool compressed = false;
};

/** `held` ascending by pitch (equal pitches in any order), `count` <= 64. */
void planCustomPool (const PoolNote* held, int count, int octaves, int maxRank, CustomPool& out) noexcept;

/** The note rank `rank` plays in pattern cycle `cycle` (note 255: none, nothing is held). */
PoolNote customNote (const CustomPool& pool, CustomMapping mapping, int rank, int maxRank, std::int64_t cycle) noexcept;

/** The pool index for rank r over N notes (R = the highest rank): r itself while the pool is
    large enough, else the shape compressed (round (r / R * (N - 1)), half up, in integers). */
int contourIndex (int r, int R, int N) noexcept;

/**
    Hands each ARP card's custom pattern from the message thread to the audio thread: one
    triple buffer per card, so the writer never waits and the reader never sees a half-written
    pattern; no allocation or lock on either side (it is allocated with its owner). One writer
    thread (the message thread), one reader thread (the audio thread).
*/
class CustomPatternExchange
{
public:
    static constexpr int cards = 3;

    /** Message thread: card 0 A .. 2 C; nullptr (or an invalid pattern) clears it. */
    void publish (int card, const CustomPattern* pattern) noexcept;
    /** Message thread: what was last published for the card (nullptr: none). */
    const CustomPattern* current (int card) const noexcept;

    /** Audio thread: for every card with a new pattern since the last call, fn (card, pattern
        or nullptr). */
    template <typename Fn>
    void collect (Fn&& fn) noexcept
    {
        for (int c = 0; c < cards; ++c)
        {
            auto& slot = slots[static_cast<std::size_t> (c)];
            if ((slot.middle.load (std::memory_order_acquire) & dirty) == 0)
                continue;
            slot.readIndex = slot.middle.exchange (slot.readIndex, std::memory_order_acq_rel) & indexMask;
            const auto& b = slot.buffers[static_cast<std::size_t> (slot.readIndex)];
            fn (c, b.present ? &b.pattern : nullptr);
        }
    }

private:
    static constexpr int dirty = 4, indexMask = 3;
    struct Buffer
    {
        CustomPattern pattern {};
        bool present = false;
    };
    struct Slot
    {
        std::array<Buffer, 3> buffers {};
        std::atomic<int> middle { 1 };
        int writeIndex = 0, readIndex = 2;   ///< each touched by its own thread only
        Buffer published {};                 ///< the writer's copy (current)
    };
    std::array<Slot, cards> slots {};
};

} // namespace osp::arp
