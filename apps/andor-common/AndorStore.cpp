#include "AndorStore.h"

#include "io/AudioFileIO.h"
#include "io/ContentHash.h"

#include <algorithm>
#include <cmath>

namespace osp::andorlab
{

AndorStore::AndorStore (juce::File directory) : dir (std::move (directory)) {}

juce::File AndorStore::defaultDirectory()
{
    const auto env = juce::SystemStats::getEnvironmentVariable ("ANDOR_SAMPLE_STORE", {});
    if (env.isNotEmpty())
        return juce::File (env);
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("ANDOR Lab").getChildFile ("Samples");
}

std::optional<juce::File> AndorStore::find (const std::string& sha256Hex) const
{
    if (sha256Hex.empty() || ! dir.isDirectory())
        return std::nullopt;
    for (const auto& f : dir.findChildFiles (juce::File::findFiles, false, juce::String (sha256Hex) + ".*"))
        if (! f.getFileName().endsWith (".andoratlas") && ! f.getFileName().endsWith (".tmp"))
            return f;
    return std::nullopt;
}

std::optional<juce::File> AndorStore::import (const juce::File& file, const std::string& sha256Hex, std::string& error)
{
    if (auto existing = find (sha256Hex))
        return existing;
    if (! dir.createDirectory())
    {
        error = "cannot create the sample store at " + dir.getFullPathName().toStdString();
        return std::nullopt;
    }
    const auto target = dir.getChildFile (juce::String (sha256Hex) + file.getFileExtension().toLowerCase());
    const auto temp = target.withFileExtension (target.getFileExtension() + ".tmp");
    if (! file.copyFileTo (temp) || ! temp.moveFileTo (target))
    {
        temp.deleteFile();
        error = "cannot copy " + file.getFileName().toStdString() + " into the sample store";
        return std::nullopt;
    }
    return target;
}

std::optional<juce::File> AndorStore::importAudio (const AudioData& audio, const std::string& name, std::string& sha256HexOut, std::string& error)
{
    if (! dir.createDirectory())
    {
        error = "cannot create the sample store";
        return std::nullopt;
    }
    const auto temp = dir.getChildFile (juce::String (name) + "-" + juce::String (juce::Time::currentTimeMillis()) + ".wav.tmp");
    // Written as WAV to a temporary name, hashed, then renamed to its hash.
    const auto tempWav = temp.withFileExtension ("wav");
    if (! io::writeAudioFile (tempWav.getFullPathName().toStdString(), audio, io::SampleFormat::pcm24, error))
        return std::nullopt;
    const auto hash = io::sha256OfFile (tempWav.getFullPathName().toStdString());
    if (! hash)
    {
        tempWav.deleteFile();
        error = "cannot hash the generated audio";
        return std::nullopt;
    }
    sha256HexOut = *hash;
    if (auto existing = find (*hash))
    {
        tempWav.deleteFile();
        return existing;
    }
    const auto target = dir.getChildFile (juce::String (*hash) + ".wav");
    if (! tempWav.moveFileTo (target))
    {
        tempWav.deleteFile();
        error = "cannot store the generated audio";
        return std::nullopt;
    }
    return target;
}

juce::File AndorStore::analysisCache (const std::string& sha256Hex, int analysisVersion, const std::string& settingsDigest) const
{
    const auto digest = juce::String::toHexString (juce::String (settingsDigest).hashCode64());
    return dir.getChildFile (juce::String (sha256Hex) + ".phrase-v" + juce::String (analysisVersion) + "-" + digest + ".andoratlas");
}

void computeOverview (const AudioData& audio, int buckets, std::vector<float>& mins, std::vector<float>& maxs)
{
    mins.assign (static_cast<std::size_t> (std::max (1, buckets)), 0.0f);
    maxs.assign (mins.size(), 0.0f);
    const auto frames = audio.numFrames();
    if (frames <= 0)
        return;
    for (std::size_t b = 0; b < mins.size(); ++b)
    {
        const auto from = static_cast<std::int64_t> (b) * frames / static_cast<std::int64_t> (mins.size());
        const auto to = std::max (from + 1, static_cast<std::int64_t> (b + 1) * frames / static_cast<std::int64_t> (mins.size()));
        float lo = 0.0f, hi = 0.0f;
        for (auto i = from; i < std::min (to, frames); ++i)
        {
            float s = 0.0f;
            for (const auto& ch : audio.channels)
                s += ch[static_cast<std::size_t> (i)];
            s /= static_cast<float> (audio.numChannels());
            lo = std::min (lo, s);
            hi = std::max (hi, s);
        }
        mins[b] = lo;
        maxs[b] = hi;
    }
}

std::optional<LoadedSource> loadSource (AndorStore& store, const juce::File& fileIn, const std::string& expectedHash, const std::string& originalPath,
                                        double maxSeconds, std::string& error)
{
    LoadedSource result;
    juce::File file = fileIn;
    if (! expectedHash.empty())
    {
        if (auto stored = store.find (expectedHash))
            file = *stored;
        else if (originalPath.size() > 0 && juce::File (originalPath).existsAsFile())
            file = juce::File (originalPath);
    }
    if (! file.existsAsFile())
    {
        error = "the sample is missing" + (originalPath.empty() ? std::string() : " (" + juce::File (originalPath).getFileName().toStdString() + ")");
        return std::nullopt;
    }
    const auto hash = io::sha256OfFile (file.getFullPathName().toStdString());
    if (! hash)
    {
        error = "cannot read " + file.getFileName().toStdString();
        return std::nullopt;
    }
    if (! expectedHash.empty() && *hash != expectedHash)
        result.warnings.push_back ("the file found differs from the one the session was made with");
    io::LoadOptions options;
    options.maxSeconds = maxSeconds;
    auto decoded = io::loadAudioFile (file.getFullPathName().toStdString(), options);
    if (! decoded.ok)
    {
        error = decoded.error;
        return std::nullopt;
    }
    for (auto& w : decoded.warnings)
        result.warnings.push_back (w);
    auto stored = store.import (file, *hash, error);
    if (! stored)
    {
        result.warnings.push_back (error);
        error.clear();
    }
    result.sha256Hex = *hash;
    result.fileName = ! originalPath.empty() ? juce::File (originalPath).getFileName().toStdString()
                    : fileIn.getFullPathName().isNotEmpty() ? fileIn.getFileName().toStdString() : file.getFileName().toStdString();
    result.originalPath = originalPath.empty() ? fileIn.getFullPathName().toStdString() : originalPath;
    result.storedPath = stored ? stored->getFullPathName().toStdString() : file.getFullPathName().toStdString();
    auto audio = std::make_shared<AudioData> (std::move (decoded.audio));
    computeOverview (*audio, 1200, result.peakMin, result.peakMax);
    result.audio = std::move (audio);
    return result;
}

} // namespace osp::andorlab
