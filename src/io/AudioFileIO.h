#pragma once

#include "core/AudioData.h"

#include <filesystem>
#include <string>
#include <vector>

namespace osp::io
{

struct AudioFileInfo
{
    std::string formatName;   ///< "WAV" or "AIFF"
    int bitDepth = 0;
    bool isFloatingPoint = false;
    double sampleRate = 0.0;
    int channels = 0;         ///< channels in the file (the decoded audio keeps at most 2)
    std::int64_t frames = 0;  ///< frames in the file
};

struct LoadOptions
{
    double maxSeconds = 600.0;  ///< longer files are truncated (with a warning), never rejected
    int maxChannels = 2;        ///< extra channels are dropped (with a warning)
};

struct LoadResult
{
    bool ok = false;
    std::string error;        ///< set when !ok
    AudioData audio;
    AudioFileInfo info;
    bool truncated = false;
    std::vector<std::string> warnings;
};

/** True for .wav/.wave/.aif/.aiff/.aifc (case-insensitive). */
bool isSupportedAudioExtension (const std::filesystem::path& path);

/** The file's format, rate, channels and length from its header only (nothing decoded): the
    Library reads it for every import and scan. False with `error` when it is not audio OSP
    can read. */
bool readAudioFileInfo (const std::filesystem::path& path, AudioFileInfo& info, std::string& error);

/**
    Decodes a WAV or AIFF file (PCM 8/16/24/32-bit, 32-bit float; AIFF-C sowt/fl32)
    into planar float. Never modifies the file. Never throws: failures (missing,
    unsupported encoding, malformed, empty) are returned as !ok with a message.
*/
LoadResult loadAudioFile (const std::filesystem::path& path, const LoadOptions& options = {});

enum class SampleFormat
{
    float32,
    pcm24,
    pcm16
};

/** Writes WAV (".wav") or AIFF (".aif"/".aiff") depending on the extension. */
bool writeAudioFile (const std::filesystem::path& path, const AudioData& audio, SampleFormat format, std::string& error);

} // namespace osp::io
