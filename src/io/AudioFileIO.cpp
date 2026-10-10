#include "io/AudioFileIO.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <algorithm>
#include <cctype>

namespace osp::io
{

namespace
{
    std::string lowerExtension (const std::filesystem::path& path)
    {
        auto ext = path.extension().string();
        std::transform (ext.begin(), ext.end(), ext.begin(), [] (unsigned char c) { return static_cast<char> (std::tolower (c)); });
        return ext;
    }

    juce::File toJuceFile (const std::filesystem::path& path)
    {
        std::error_code ec;
        auto absolute = std::filesystem::absolute (path, ec);
        return juce::File (juce::String::fromUTF8 ((ec ? path : absolute).string().c_str()));
    }
}

bool isSupportedAudioExtension (const std::filesystem::path& path)
{
    const auto ext = lowerExtension (path);
    return ext == ".wav" || ext == ".wave" || ext == ".aif" || ext == ".aiff" || ext == ".aifc" || ext == ".flac";
}

bool readAudioFileInfo (const std::filesystem::path& path, AudioFileInfo& info, std::string& error)
{
    std::error_code ec;
    if (! std::filesystem::is_regular_file (path, ec))
    {
        error = "file not found: " + path.string();
        return false;
    }
    if (! isSupportedAudioExtension (path))
    {
        error = "unsupported file type '" + path.extension().string() + "' (supported: WAV, AIFF, FLAC)";
        return false;
    }
    juce::AudioFormatManager formats;
    formats.registerFormat (new juce::WavAudioFormat(), true);
    formats.registerFormat (new juce::AiffAudioFormat(), false);
    formats.registerFormat (new juce::FlacAudioFormat(), false);
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (toJuceFile (path).createInputStream()));
    if (reader == nullptr)
    {
        error = "could not read " + path.filename().string() + " (damaged, or an encoding OSP does not read)";
        return false;
    }
    info.formatName = reader->getFormatName().contains ("AIFF") ? "AIFF" : (reader->getFormatName().contains ("FLAC") ? "FLAC" : "WAV");
    info.bitDepth = static_cast<int> (reader->bitsPerSample);
    info.isFloatingPoint = reader->usesFloatingPointData;
    info.sampleRate = reader->sampleRate;
    info.channels = static_cast<int> (reader->numChannels);
    info.frames = reader->lengthInSamples;
    if (! (info.sampleRate >= 1000.0 && info.sampleRate <= 768000.0) || info.channels < 1 || info.frames <= 0)
    {
        error = path.filename().string() + " holds no playable audio";
        return false;
    }
    return true;
}

LoadResult loadAudioFile (const std::filesystem::path& path, const LoadOptions& options)
{
    LoadResult result;

    std::error_code ec;
    if (! std::filesystem::is_regular_file (path, ec))
    {
        result.error = "file not found: " + path.string();
        return result;
    }

    if (! isSupportedAudioExtension (path))
    {
        result.error = "unsupported file type '" + path.extension().string() + "' (supported: WAV, AIFF, FLAC)";
        return result;
    }

    juce::AudioFormatManager formats;
    formats.registerFormat (new juce::WavAudioFormat(), true);
    formats.registerFormat (new juce::AiffAudioFormat(), false);
    formats.registerFormat (new juce::FlacAudioFormat(), false);

    // Detect the format from content, not the extension (.aifc files, mislabelled files).
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (toJuceFile (path).createInputStream()));
    if (reader == nullptr)
    {
        result.error = "could not decode " + path.filename().string()
                       + " (malformed file or unsupported encoding; supported: PCM 8/16/24/32-bit, 32-bit float)";
        return result;
    }

    result.info.formatName = reader->getFormatName().contains ("AIFF") ? "AIFF" : "WAV";
    result.info.bitDepth = static_cast<int> (reader->bitsPerSample);
    result.info.isFloatingPoint = reader->usesFloatingPointData;
    result.info.sampleRate = reader->sampleRate;
    result.info.channels = static_cast<int> (reader->numChannels);
    result.info.frames = reader->lengthInSamples;

    if (! (reader->sampleRate >= 1000.0 && reader->sampleRate <= 768000.0))
    {
        result.error = "invalid sample rate " + std::to_string (reader->sampleRate);
        return result;
    }
    if (reader->numChannels < 1)
    {
        result.error = "file has no audio channels";
        return result;
    }
    if (reader->lengthInSamples <= 0)
    {
        result.error = "file contains no audio frames";
        return result;
    }

    const int channels = std::min (static_cast<int> (reader->numChannels), std::max (1, options.maxChannels));
    if (channels < static_cast<int> (reader->numChannels))
        result.warnings.push_back ("file has " + std::to_string (reader->numChannels) + " channels; using the first "
                                   + std::to_string (channels));

    auto frames = reader->lengthInSamples;
    const auto maxFrames = static_cast<juce::int64> (options.maxSeconds * reader->sampleRate);
    if (frames > maxFrames)
    {
        frames = maxFrames;
        result.truncated = true;
        result.warnings.push_back ("file longer than " + std::to_string (static_cast<int> (options.maxSeconds))
                                   + " s; truncated");
    }

    result.audio = AudioData::allocate (channels, frames, reader->sampleRate);

    // Read in chunks straight into the planar destination.
    constexpr int chunk = 1 << 16;
    std::vector<float*> pointers (static_cast<std::size_t> (channels));
    for (juce::int64 position = 0; position < frames; position += chunk)
    {
        const int count = static_cast<int> (std::min<juce::int64> (chunk, frames - position));
        for (int ch = 0; ch < channels; ++ch)
            pointers[static_cast<std::size_t> (ch)] = result.audio.channels[static_cast<std::size_t> (ch)].data() + position;

        if (! reader->read (pointers.data(), channels, position, count))
        {
            result.error = "read error in " + path.filename().string() + " at frame " + std::to_string (position);
            result.audio = {};
            return result;
        }
    }

    // Guard against non-finite values from malformed float files.
    std::int64_t nonFinite = 0;
    for (auto& ch : result.audio.channels)
        for (auto& s : ch)
            if (! std::isfinite (s))
            {
                s = 0.0f;
                ++nonFinite;
            }
    if (nonFinite > 0)
        result.warnings.push_back (std::to_string (nonFinite) + " non-finite samples replaced with 0");

    result.ok = true;
    return result;
}

bool writeAudioFile (const std::filesystem::path& path, const AudioData& audio, SampleFormat format, std::string& error)
{
    if (audio.isEmpty())
    {
        error = "nothing to write (empty audio)";
        return false;
    }

    std::error_code ec;
    if (path.has_parent_path())
        std::filesystem::create_directories (path.parent_path(), ec);

    const auto ext = lowerExtension (path);
    std::unique_ptr<juce::AudioFormat> audioFormat;
    if (ext == ".aif" || ext == ".aiff" || ext == ".aifc")
        audioFormat = std::make_unique<juce::AiffAudioFormat>();
    else if (ext == ".flac")
        audioFormat = std::make_unique<juce::FlacAudioFormat>();
    else
        audioFormat = std::make_unique<juce::WavAudioFormat>();

    auto file = toJuceFile (path);
    file.deleteFile();
    std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream> (file);
    if (! static_cast<juce::FileOutputStream*> (stream.get())->openedOk())
    {
        error = "cannot open for writing: " + path.string();
        return false;
    }

    const int bits = format == SampleFormat::float32 ? 32 : (format == SampleFormat::pcm24 ? 24 : 16);
    const auto sampleFormat = format == SampleFormat::float32 ? juce::AudioFormatWriterOptions::SampleFormat::floatingPoint
                                                              : juce::AudioFormatWriterOptions::SampleFormat::integral;
    const auto options = juce::AudioFormatWriterOptions {}
                             .withSampleRate (audio.sampleRate)
                             .withNumChannels (audio.numChannels())
                             .withBitsPerSample (bits)
                             .withSampleFormat (sampleFormat);

    auto writer = audioFormat->createWriterFor (stream, options);
    if (writer == nullptr)
    {
        error = "format does not support " + std::to_string (bits) + "-bit " + (format == SampleFormat::float32 ? "float" : "PCM")
                + " with " + std::to_string (audio.numChannels()) + " channels at " + std::to_string (audio.sampleRate) + " Hz";
        return false;
    }

    std::vector<const float*> pointers;
    for (const auto& ch : audio.channels)
        pointers.push_back (ch.data());

    constexpr int chunk = 1 << 16;
    for (std::int64_t position = 0; position < audio.numFrames(); position += chunk)
    {
        const int count = static_cast<int> (std::min<std::int64_t> (chunk, audio.numFrames() - position));
        std::vector<const float*> offset (pointers.size());
        for (std::size_t c = 0; c < pointers.size(); ++c)
            offset[c] = pointers[c] + position;
        if (! writer->writeFromFloatArrays (offset.data(), static_cast<int> (offset.size()), count))
        {
            error = "write failed: " + path.string();
            return false;
        }
    }
    return true;
}

} // namespace osp::io
