#include "MemoDecoder.h"

#include <cmath>

namespace osp::memolab
{

namespace
{
    juce::AudioFormatManager& formats()
    {
        // Message and decode threads read through their own readers; the manager itself is
        // only created once and never changed afterwards.
        static juce::AudioFormatManager manager;
        static const bool registered = [] {
            manager.registerBasicFormats();   // WAV, AIFF, FLAC, CoreAudio on macOS
            return true;
        }();
        juce::ignoreUnused (registered);
        return manager;
    }

    std::unique_ptr<juce::AudioFormatReader> openReader (const juce::File& file)
    {
        // Content first (a mislabelled file), then by extension (CoreAudio wants the file).
        if (auto reader = std::unique_ptr<juce::AudioFormatReader> (formats().createReaderFor (file)))
            return reader;
        return {};
    }

    bool plausible (const AudioInfo& info, juce::String& error)
    {
        if (! (info.sampleRate >= 4000.0 && info.sampleRate <= 768000.0) || info.channels < 1 || info.frames <= 0)
        {
            error = "holds no playable audio";
            return false;
        }
        return true;
    }

    struct Fingerprint
    {
        juce::int64 size = -1;
        juce::Time modified;
        bool operator== (const Fingerprint& o) const { return size == o.size && modified == o.modified; }
    };
    Fingerprint fingerprint (const juce::File& f) { return { f.getSize(), f.getLastModificationTime() }; }
}

const juce::StringArray& supportedExtensions()
{
    static const juce::StringArray extensions { ".m4a", ".qta", ".caf", ".aac", ".wav", ".wave", ".aif", ".aiff", ".aifc", ".flac" };
    return extensions;
}

bool isSupported (const juce::File& file)
{
    return supportedExtensions().contains (file.getFileExtension().toLowerCase());
}

bool probe (const juce::File& file, AudioInfo& info, juce::String& error)
{
    if (! file.existsAsFile())
    {
        error = "not found";
        return false;
    }
    if (auto reader = openReader (file))
    {
        info.sampleRate = reader->sampleRate;
        info.channels = static_cast<int> (reader->numChannels);
        info.frames = reader->lengthInSamples;
        const auto name = reader->getFormatName();
        info.format = name.containsIgnoreCase ("wav") ? "WAV" : name.containsIgnoreCase ("aiff") ? "AIFF" : name.containsIgnoreCase ("flac") ? "FLAC" : "CoreAudio";
        return plausible (info, error);
    }
#if JUCE_MAC
    if (probeWithAVFoundation (file, info, error))
        return plausible (info, error);
#endif
    error = "this recording's format cannot be read";
    return false;
}

bool decode (const juce::File& file, DecodedAudio& out, juce::String& error, double maxSeconds)
{
    if (! file.existsAsFile())
    {
        error = "The recording is not on this Mac (not downloaded, or it was moved).";
        return false;
    }
    const auto before = fingerprint (file);
    bool ok = false;
    if (auto reader = openReader (file))
    {
        out.info.sampleRate = reader->sampleRate;
        out.info.channels = static_cast<int> (reader->numChannels);
        out.info.frames = reader->lengthInSamples;
        out.info.format = "JUCE";
        if (! plausible (out.info, error))
            return false;
        const auto limit = static_cast<std::int64_t> (std::floor (maxSeconds * reader->sampleRate));
        const auto frames = std::min (out.info.frames, limit);
        out.truncated = frames < out.info.frames;
        const int channels = std::min (2, out.info.channels);
        out.buffer.setSize (channels, static_cast<int> (frames));
        ok = reader->read (&out.buffer, 0, static_cast<int> (frames), 0, true, channels > 1);
        out.info.frames = frames;
        out.info.channels = channels;
        if (! ok)
            error = "The recording could not be decoded (damaged, or still syncing).";
    }
#if JUCE_MAC
    else
        ok = decodeWithAVFoundation (file, out, error, maxSeconds);
#else
    else
        error = "This recording's format cannot be read on this computer.";
#endif
    if (! ok)
        return false;
    if (! (fingerprint (file) == before))
    {
        error = "The recording changed while it was read (still syncing). Try again in a moment.";
        return false;
    }
    if (out.buffer.getNumSamples() <= 0)
    {
        error = "The recording holds no audio.";
        return false;
    }
    return true;
}

std::int64_t firstSoundFrame (const DecodedAudio& audio)
{
    const auto& b = audio.buffer;
    const double rate = audio.info.sampleRate;
    const int frames = b.getNumSamples(), channels = b.getNumChannels();
    if (frames <= 0 || channels <= 0 || ! (rate > 0.0))
        return 0;
    const int window = std::max (1, static_cast<int> (0.02 * rate));
    auto rmsDb = [&] (int start) {
        double sum = 0.0;
        const int end = std::min (frames, start + window);
        for (int c = 0; c < channels; ++c)
        {
            const float* d = b.getReadPointer (c);
            for (int i = start; i < end; ++i)
                sum += static_cast<double> (d[i]) * d[i];
        }
        return 10.0 * std::log10 (sum / std::max (1, (end - start) * channels) + 1.0e-20);
    };
    const int lookAhead = std::min (frames, static_cast<int> (60.0 * rate));
    double loudest = -200.0;
    for (int s = 0; s < lookAhead; s += window)
        loudest = std::max (loudest, rmsDb (s));
    const double threshold = std::max (-55.0, loudest - 35.0);
    for (int s = 0; s < frames; s += window)
        if (rmsDb (s) >= threshold)
            return std::max<std::int64_t> (0, s - static_cast<std::int64_t> (0.08 * rate));
    return 0;
}

} // namespace osp::memolab
