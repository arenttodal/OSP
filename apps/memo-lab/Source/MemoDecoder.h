#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>

namespace osp::memolab
{

/**
    Reading recordings for Memo Lab, never writing them. JUCE's readers first (WAV, AIFF, FLAC
    and, on macOS, CoreAudio: M4A / AAC, CAF); on macOS an AVFoundation reader for containers
    CoreAudio will not open (Voice Memos' newer .qta). The original is only ever opened for
    reading, and a file that changes while it is read (still syncing) is refused.
*/
struct AudioInfo
{
    double sampleRate = 0.0;
    int channels = 0;
    std::int64_t frames = 0;
    juce::String format;   ///< "WAV", "AIFF", "FLAC", "CoreAudio", "AVFoundation"
    double seconds() const noexcept { return sampleRate > 0.0 ? static_cast<double> (frames) / sampleRate : 0.0; }
};

struct DecodedAudio
{
    juce::AudioBuffer<float> buffer;   ///< at most 2 channels
    AudioInfo info;
    bool truncated = false;            ///< longer than the limit: its first part
};

/** The extensions Memo Lab lists (lower case, with the dot). */
const juce::StringArray& supportedExtensions();
bool isSupported (const juce::File& file);

/** The header only (nothing decoded): rate, channels, length. False with `error` otherwise. */
bool probe (const juce::File& file, AudioInfo& info, juce::String& error);

/** Decodes up to `maxSeconds` (the rest is left out, `truncated`). Refuses a file that changed
    while it was read, and audio that is not plausible (no frames, absurd rate). */
bool decode (const juce::File& file, DecodedAudio& out, juce::String& error, double maxSeconds = 900.0);

#if JUCE_MAC
/** AVFoundation (MemoDecoderMac.mm): any audio track AVAsset can read, as float PCM. */
bool probeWithAVFoundation (const juce::File& file, AudioInfo& info, juce::String& error);
bool decodeWithAVFoundation (const juce::File& file, DecodedAudio& out, juce::String& error, double maxSeconds);
#endif

} // namespace osp::memolab
