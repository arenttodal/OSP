#pragma once

#include "engine/Arpeggiator.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <array>
#include <cstdint>
#include <memory>

namespace osp::plugin
{

/**
    The per-card arpeggiator's lanes (one implementation for ANDOR OSP's processor and every
    product built from the ANDOR Instrument Shell that opts in).

    LINK with every card in it, or the arpeggiator off: the one arpeggiator, exactly as before
    (the processor's own path). Otherwise up to three lanes, each an arpeggiator playing some
    cards: LINK, one lane for the cards in it; unlinked, one lane per card in it (card A's
    lane is the processor's own arpeggiator with its settings; B's and C's have their own
    PATTERN / RATE / OCTAVES). The cards left out hold the keys as played, with their own
    sustain pedal. Each note the engine is given names its cards by its MIDI channel:
    channel = 1 + mask (bit 0 A, bit 1 B, bit 2 C), and the host is told (setCardRouting) so
    its engine starts a note only on those cards. A change of cards or LINK ends what sounds
    and plays the keys still down into the new lanes: no note is left hanging.

    prepare() allocates (prepareToPlay, the message thread); process() is real-time safe.
*/
class CardArpLanes
{
public:
    /** What the processor that runs the lanes provides (all audio thread, real-time safe). */
    struct Host
    {
        virtual ~Host() = default;
        /** The processor's arpeggiator: lane 0 (card A, or the linked cards). */
        virtual Arpeggiator& mainArpeggiator() noexcept = 0;
        /** The arpeggiator's settings as the processor reads them (ARP destinations included),
            with forceArpOff() applied to `enabled`. */
        virtual Arpeggiator::Settings arpSettings() noexcept = 0;
        /** The one arpeggiator, as the processor runs it without lanes. */
        virtual const juce::MidiBuffer& runArpeggiator (const juce::MidiBuffer& midi, int numSamples, const HostTiming& timing) noexcept = 0;
        /** The lanes were rearranged: the processor forgets per-run state (OCTAVES by
            modulation) before the settings are read again. */
        virtual void lanesRearranged() noexcept {}
        /** The engine reads each note's channel as its cards (true) or plays every card (false). */
        virtual void setCardRouting (bool on) noexcept = 0;
        virtual void publishArpDisplay() noexcept = 0;
    };

    /** The per-card parameters' values this block. */
    struct Controls
    {
        bool enabled = false;          ///< arp.enabled
        bool link = true;
        std::uint8_t cards = 7;        ///< the cards the arpeggio plays (bit 0 A .. bit 2 C)
        std::array<int, 2> pattern {}, rate {}, octaves { 1, 1 };   ///< B's and C's own (unlinked)
        std::uint64_t seed = 1;
    };

    CardArpLanes();
    ~CardArpLanes();

    /** Allocates (prepareToPlay): the lanes' arpeggiators and buffers; everything starts over. */
    void prepare (double sampleRate);
    /** The transport started: every lane's pattern starts over (as the arpeggiator's). */
    void restartPatterns() noexcept;

    /** The keys of this block to the notes the engine plays (the incoming buffer may be
        returned as it is, as the processor's runArpeggiator does). */
    const juce::MidiBuffer& process (Host& host, const juce::MidiBuffer& midi, int numSamples, const HostTiming& timing, const Controls& controls) noexcept;

    /** No card plays the arpeggio while it is on: the keys as played (Host::arpSettings). */
    bool forceArpOff() const noexcept { return forceOff; }
    /** Lanes are in use (anything but the one arpeggiator). */
    bool active() const noexcept { return laneConfig != 0; }
    /** The arpeggiator whose steps the ARP display shows for the card the panel edits
        (`shownCard` 0 A .. 2 C; A, and every linked arrangement: the processor's own). */
    Arpeggiator& displayed (Host& host, int shownCard) noexcept;

private:
    void trackOutput (const juce::MidiBuffer& out) noexcept;
    void releaseSounding (juce::MidiBuffer& out) noexcept;

    std::array<std::uint8_t, 3> laneMask {};   ///< the cards each lane plays (0: idle)
    std::uint8_t passMask = 0;                 ///< the cards that hold the keys
    int laneConfig = 0;                        ///< 0: the one arpeggiator; else LINK and the cards
    bool forceOff = false;
    bool pedalDown = false;
    /** The larger state, on the heap (allocated by prepare's owner, never on the audio
        thread): a processor on a stack (a host's, a test's) stays small. */
    struct State;
    std::unique_ptr<State> state;
};

} // namespace osp::plugin
