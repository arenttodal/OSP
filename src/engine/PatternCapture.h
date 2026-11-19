#pragma once

#include "model/ArpPatternModel.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <vector>

namespace osp
{

/**
    Records the notes the musician plays for a Custom ARP pattern (CP3): the processor pushes the
    incoming note events of each block (before the arpeggiator, the on-screen keyboard merged, so
    the arpeggiator's own output can never be captured) while armed; the message thread collects
    them into notes in quarters from the take's first note.

    The audio side is wait-free and allocation-free: a fixed single-producer single-consumer ring.
    When the ring or the take is full, the take is marked full and stops growing (the editor says
    so); nothing is truncated silently.
*/
class PatternCapture
{
public:
    struct Event
    {
        std::int64_t sample = 0;   ///< on the capture's own clock (samples since arming)
        float bpm = 120.0f;
        std::uint8_t note = 0, velocity = 0;   ///< velocity 0: a note-off
    };
    static constexpr int ringCapacity = 4096;

    // -- message thread ---------------------------------------------------------------------
    /** Arms a new take (forgets the last one). `sampleRate` converts samples to time. */
    void start (double sampleRate) noexcept;
    /** Disarms; notes still held end at the last event's time. */
    void stop();
    void cancel() noexcept;
    bool armed() const noexcept { return isArmed.load (std::memory_order_acquire); }
    /** Collects what arrived since the last call; returns true when anything changed. */
    bool poll();
    /** The take so far, in quarters from its first note (open notes as long as they are now). */
    const std::vector<arppattern::CapturedNote>& notes() const noexcept { return taken; }
    /** The tempo of the take's first note. */
    double bpm() const noexcept { return firstBpm; }
    /** The take reached arppattern::maxCaptureNotes (or the ring overflowed): it stopped growing. */
    bool full() const noexcept { return isFull; }

    // -- audio thread -----------------------------------------------------------------------
    /** One block's note events, in order (call only while armed() is true, or not at all). */
    void push (int offset, int note, int velocity, double bpm) noexcept;
    /** After the block: the capture's clock moves on. */
    void advance (int numSamples) noexcept;

private:
    std::array<Event, ringCapacity> ring {};
    std::atomic<int> head { 0 }, tail { 0 };
    std::atomic<bool> isArmed { false }, overflow { false };
    std::int64_t clock = 0;   ///< audio thread

    // message thread
    double rate = 48000.0;
    std::vector<arppattern::CapturedNote> taken;
    std::array<int, 128> open {};   ///< index into `taken` + 1 of the note sounding (0: none)
    std::int64_t origin = -1, lastSample = 0;
    double firstBpm = 120.0;
    bool isFull = false;
    double quartersAt (std::int64_t sample, double bpm) const noexcept;
};

} // namespace osp
