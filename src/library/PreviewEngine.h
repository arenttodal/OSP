#pragma once

#include "core/AudioData.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace osp::library
{

/**
    A sound prepared for previewing: decoded audio (at most two channels, at the file's own
    rate), a waveform overview and the root the keyboard plays it against. Immutable once
    made; made on a background worker (decoding and the root estimate are expensive), never on
    the audio thread.
*/
struct PreviewSound
{
    AudioData audio;
    double rootMidi = 60.0;
    bool rootKnown = false;           ///< false: no root in the catalog and none detected (plays as C4)
    std::vector<float> peakMin, peakMax;   ///< waveform overview, `overviewBins` bins (-1..1)
    std::string assetId;              ///< the catalog record it came from (display only)
    std::string name;

    static constexpr int overviewBins = 512;
    double durationSeconds() const noexcept { return audio.durationSeconds(); }
};

/** Builds a PreviewSound from decoded audio: the overview and, when `knownRoot` is empty,
    a root from a pitch estimate of its first seconds (no reliable pitch: C4, rootKnown false).
    Background thread only. */
std::shared_ptr<const PreviewSound> makePreviewSound (AudioData audio, std::optional<double> knownRoot,
                                                      std::string assetId = {}, std::string name = {});

/**
    The Library's audition path: hearing sounds without touching the instrument
    (docs/library/preview.md). Four slots - the audition tray's A, B, C and the browser's own
    preview - each holding one PreviewSound. Dry: no instrument effects.

    Threads:
      - message: setSound(), play(), stop(), setGainDb(), collectGarbage(), the display reads.
      - audio:   render() adds the preview into the output; noteOn() / noteOff() /
                 allNotesOff() from the block's MIDI. No allocation, no locks, no I/O.

    Sounds are handed over like the instrument's ModelExchange: a slot points at an entry
    stamped with a publish generation; the audio thread reports the oldest generation it can
    still touch and entries are freed on the message thread only after that.

    Gain: one preview gain (default -6 dB); slots started together share an equal-power sum
    (1/sqrt(n)), and a peak limiter keeps the preview from ever exceeding -1 dBFS on its own.
    At most `maxVoices` voices sound; a new one steals the oldest with a 5 ms fade.
*/
class PreviewEngine
{
public:
    static constexpr int numSlots = 4;
    static constexpr int browserSlot = 3;    ///< 0, 1, 2: the tray's A, B, C
    static constexpr int maxVoices = 8;
    static constexpr unsigned trayMask = 0b0111u;
    static constexpr float defaultGainDb = -6.0f;

    PreviewEngine();
    ~PreviewEngine();

    /** Not the audio thread. Stops everything. */
    void prepare (double sampleRate);

    // Message thread ---------------------------------------------------------
    /** Puts `sound` into a slot (nullptr clears it). Voices already playing the old one finish. */
    void setSound (int slot, std::shared_ptr<const PreviewSound> sound);
    std::shared_ptr<const PreviewSound> sound (int slot) const;
    /** Plays the slots in `slotMask` once from the start, together (replacing what the
        previous play() started). */
    void play (unsigned slotMask);
    /** Fades out everything the preview is playing. */
    void stop();
    void setGainDb (float db) noexcept;
    float gainDb() const noexcept { return gainDecibels.load (std::memory_order_relaxed); }
    /** Frees sounds the audio thread can no longer reach. */
    void collectGarbage();
    /** Entries alive (tests). */
    std::size_t ownedCount() const noexcept { return owned.size(); }

    /** Where the newest one-shot of a slot is (seconds), or -1 when the slot is not playing. */
    double playheadSeconds (int slot) const noexcept;
    bool isSounding() const noexcept { return sounding.load (std::memory_order_relaxed); }

    // Audio thread -----------------------------------------------------------
    /** Adds the preview into `out` (1 or 2 channels). */
    void render (float* const* out, int numChannels, int numFrames) noexcept;
    /** The keyboard plays the slots in `slotMask`, each pitched against its own root. */
    void noteOn (int note, float velocity, unsigned slotMask) noexcept;
    void noteOff (int note) noexcept;
    void allNotesOff() noexcept;

private:
    struct Entry
    {
        std::shared_ptr<const PreviewSound> sound;
        std::uint64_t generation = 0;
    };
    struct Voice
    {
        const Entry* entry = nullptr;
        int slot = -1;
        int note = -1;            ///< -1: a one-shot from play()
        double position = 0.0;    ///< frames into the sound
        double increment = 1.0;
        float gain = 0.0f;
        float fade = 0.0f, fadeStep = 0.0f;   ///< fade-in / fade-out ramp
        bool releasing = false;
        std::uint64_t age = 0;
    };
    struct Command
    {
        enum class Kind : std::uint8_t { play, stop } kind = Kind::play;
        unsigned mask = 0;
    };

    void startVoice (int slot, const Entry* entry, int note, float gain) noexcept;
    void release (Voice& voice, float seconds) noexcept;
    void handleCommands() noexcept;

    double rate = 48000.0;
    std::array<std::atomic<const Entry*>, numSlots> slots {};
    std::array<std::shared_ptr<const PreviewSound>, numSlots> latest {};   ///< message thread
    std::array<const Entry*, numSlots> latestEntry {};                     ///< message thread
    std::vector<std::unique_ptr<Entry>> owned;                             ///< message thread
    std::atomic<std::uint64_t> publishedGeneration { 0 };
    std::atomic<std::uint64_t> oldestInUse { 0 };

    static constexpr int commandCapacity = 64;
    std::array<Command, commandCapacity> commands {};
    std::atomic<int> commandWrite { 0 }, commandRead { 0 };

    static constexpr int voiceCapacity = maxVoices + 4;   ///< room for voices still fading out
    std::array<Voice, voiceCapacity> voices {};
    std::uint64_t voiceCounter = 0;
    std::array<const Entry*, numSlots> blockSlots {};
    std::atomic<float> gainDecibels { defaultGainDb };
    float limiterGain = 1.0f, limiterRelease = 0.0f;
    std::array<std::atomic<float>, numSlots> playheads {};
    std::atomic<bool> sounding { false };
};

} // namespace osp::library
