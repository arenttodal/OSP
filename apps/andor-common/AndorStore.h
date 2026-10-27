#pragma once

#include "core/AudioData.h"

#include <juce_core/juce_core.h>

#include <array>
#include <atomic>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace osp::andorlab
{

/**
    The ANDOR Lab instruments' managed sample store: every imported file is copied once, named
    by its content hash, so a session survives the original moving. Analysis caches (Note
    Atlases) sit next to the copies, named by hash + analysis version + settings digest, so a
    cache is never used for a different analysis. Shared by Phrase and Grain (one copy of a
    sample on disk); each product keeps its own presets elsewhere.

    Default: <user app data>/ANDOR Lab/Samples; ANDOR_SAMPLE_STORE overrides (tests).
*/
class AndorStore
{
public:
    explicit AndorStore (juce::File directory = defaultDirectory());
    static juce::File defaultDirectory();

    const juce::File& directory() const noexcept { return dir; }

    /** Copies `file` in when missing (atomic: temp file + rename). Returns the stored copy. */
    std::optional<juce::File> import (const juce::File& file, const std::string& sha256Hex, std::string& error);
    /** Writes generated audio (Load example) into the store as WAV, named by its hash. */
    std::optional<juce::File> importAudio (const AudioData& audio, const std::string& name, std::string& sha256HexOut, std::string& error);
    std::optional<juce::File> find (const std::string& sha256Hex) const;

    juce::File analysisCache (const std::string& sha256Hex, int analysisVersion, const std::string& settingsDigest) const;

private:
    juce::File dir;
};

/** A decoded, hashed source ready for the instruments (built on a worker thread). */
struct LoadedSource
{
    std::string sha256Hex;
    std::string fileName;
    std::string originalPath;
    std::string storedPath;
    std::shared_ptr<const AudioData> audio;
    std::vector<std::string> warnings;
    /** Waveform overview: min / max per bucket of the mono mix (display only). */
    std::vector<float> peakMin, peakMax;
};

/**
    Imports, hashes and decodes one file (store first, then the original path). Never throws.
    `maxSeconds` bounds memory (longer files are truncated with a warning).
*/
std::optional<LoadedSource> loadSource (AndorStore& store, const juce::File& file, const std::string& expectedHash, const std::string& originalPath,
                                        double maxSeconds, std::string& error);

/** Min/max overview of a recording in `buckets` columns. */
void computeOverview (const AudioData& audio, int buckets, std::vector<float>& mins, std::vector<float>& maxs);

/**
    A small single-producer (audio thread) / single-consumer (editor) display mailbox: plain
    values in atomics, written every block, read by a timer. Tearing between entries is
    harmless for a display; nothing here locks or allocates.
*/
template <std::size_t N>
struct DisplayMailbox
{
    std::array<std::atomic<float>, N> a {}, b {};
    std::array<std::atomic<int>, N> c {};
    std::atomic<int> count { 0 };
    std::atomic<int> voices { 0 };
    std::atomic<int> grains { 0 };
    std::atomic<std::uint32_t> heldKeys[4] {};   ///< 128-bit mask of sounding keys
};

} // namespace osp::andorlab
