#pragma once

#include "MemoDecoder.h"
#include "MemoSources.h"
#include "PluginProcessor.h"

#include <juce_audio_devices/juce_audio_devices.h>

#include <memory>

namespace osp::memolab
{

using plugin::InstrumentHost;
using plugin::OspAudioProcessor;

/**
    One recording made into an instrument by ANDOR OSP's own processor (Memo Lab adds no DSP):
    - the chosen passage is written as a WAV under appDataFolder()/Passages;
    - the processor copies it into OSP's sample store by content hash;
    - ONE SHOT loads it as an ordinary sound (loadFile), MAP NOTES analyses it into a Note
      Atlas (analyseRecording: Stage A playable, then Stage B);
    - Save writes the processor's portable instrument (exportInstrument), which the plugin
      imports.
    The recording itself is only ever read. Message thread.
*/
class MemoSession
{
public:
    enum class Mode { oneShot, mapNotes };

    struct Status
    {
        enum class Stage { idle, building, playable, ready, unsuitable, failed };
        Stage stage = Stage::idle;
        juce::String text;            ///< a plain sentence
        Mode mode = Mode::oneShot;
        bool atlas = false;           ///< the instrument is a Note Atlas
        int notes = 0;                ///< atlas notes (included)
        juce::String pitchRange;      ///< "A3 - F5", the one-shot's root otherwise
        bool canPlay() const noexcept { return stage == Stage::playable || stage == Stage::ready || stage == Stage::unsuitable; }
    };

    explicit MemoSession (OspAudioProcessor& processor);
    ~MemoSession();

    /** Writes `frames` of `audio` (empty: all of it) as a 32-bit float WAV named after
        `title` in `folder`; removes any earlier passage there (one conversion at a time). */
    static juce::File writePassage (const DecodedAudio& audio, juce::Range<std::int64_t> frames, const juce::String& title, const juce::File& folder,
                                    juce::String& error);
    static juce::File passageFolder() { return appDataFolder().getChildFile ("Passages"); }

    /** Starts building an instrument from the passage (layer A). False with `error` when it
        cannot start; progress and the outcome arrive through status(). */
    bool convert (const Recording& recording, const DecodedAudio& audio, juce::Range<std::int64_t> frames, Mode mode, juce::String& error);

    /** Follows the processor (call on a timer, after OSP's own load timer has run). */
    Status update();
    const Status& status() const noexcept { return current; }
    const juce::String& instrumentName() const noexcept { return name; }
    const juce::String& sourceName() const noexcept { return source; }
    bool busy() const;

    /** The processor's portable instrument (.ospinstrument), named after the recording. */
    bool save (const juce::File& file, juce::String& error);
    juce::String suggestedFileName() const;

    OspAudioProcessor& processor() noexcept { return osp; }

private:
    OspAudioProcessor& osp;
    Status current;
    Mode mode = Mode::oneShot;
    juce::String name, source;
    std::uint64_t previousLoad = 0;
    bool started = false, sawWork = false;
    int quietPolls = 0;
};

/**
    Hearing a recording (or a passage of it) before making anything: a transport over the
    decoded audio, mixed on the same device as the instrument (an AudioDeviceManager sums its
    callbacks). Message thread, except the audio callback.
*/
class PreviewPlayer
{
public:
    PreviewPlayer();
    ~PreviewPlayer();
    juce::AudioIODeviceCallback& callback() noexcept { return player; }

    /** The recording to hear (kept, not copied); nullptr stops and forgets it. */
    void setAudio (std::shared_ptr<const DecodedAudio> audio);
    /** Plays `frames` (empty: from `from` to the end). */
    void play (juce::Range<std::int64_t> frames, std::int64_t from = 0);
    void stop();
    bool isPlaying() const;
    /** Where it is in the recording (frames), -1 when stopped. */
    std::int64_t position() const;

private:
    std::shared_ptr<const DecodedAudio> audio;
    std::unique_ptr<juce::AudioBuffer<float>> view;
    std::unique_ptr<juce::MemoryAudioSource> memory;
    juce::AudioTransportSource transport;
    juce::AudioSourcePlayer player;
    std::int64_t offset = 0;
};

} // namespace osp::memolab
