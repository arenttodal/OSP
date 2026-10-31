#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace osp::memolab
{

/**
    The library scan's sorting of recordings (docs/research/memo-lab/README.md, "Library scan"):
    what kind of sound a memo mostly holds, from measured features, no learned model (CLAUDE.md
    rule 5). Each second is one of these; a memo's label is its most common sounding class.

    The features (8 kHz mono, 40 ms frames): level, a YIN pitch and its periodicity, and a
    brightness proxy (the first difference's energy share). Per second:
    - quiet:   almost nothing sounds (below -50 dBFS or 40 dB under the memo's loudest);
    - talk:    voiced in short runs (syllables) with gaps between, the pitch gliding rather
               than settling;
    - hum:     voiced, held, steady pitches, dark (few upper harmonics);
    - phrase:  voiced or periodic, held steady pitches with clear notes (sung or played
               solo);
    - jam:     sounding nearly all the time, tonal but rarely one clear pitch (several
               voices or instruments at once);
    - noise:   sounding, but neither pitched nor speech-like (wind, handling, rooms).
    Rules, not certainty: the confidence says how clearly the memo leans one way.
*/
enum class SoundClass : std::uint8_t { quiet, talk, hum, phrase, jam, noise };
constexpr int soundClassCount = 6;
const char* soundClassName (SoundClass c) noexcept;   ///< "Quiet", "Talk", "Hum", "Phrase", "Jam", "Noise"
const char* soundClassId (SoundClass c) noexcept;     ///< "quiet", "talk", "hum", "phrase", "jam", "noise"

struct MemoProfile
{
    static constexpr int version = 1;   ///< bump when the rules change (the cache is redone)
    SoundClass label = SoundClass::quiet;
    float confidence = 0.0f;                    ///< 0..1: the label's share of the sounding seconds
    std::array<float, soundClassCount> share {}; ///< each class's share of all seconds
    std::vector<std::uint8_t> seconds;          ///< the class of each second (SoundClass)
    double firstMusicSeconds = -1.0;            ///< the first hum / phrase / jam second (-1: none)
    double durationSeconds = 0.0;
    /** Classes above `minShare` of the sounding time, the label first ("Jam", "Talk"). */
    std::vector<SoundClass> tags (float minShare = 0.25f) const;
};

/**
    Profiles one recording. `mono` is the recording mixed to one channel at `sampleRate`
    (any rate; it is reduced to 8 kHz internally). Allocates; for a background thread.
    `cancelled`, when given, stops early (the profile is then incomplete).
*/
MemoProfile profileRecording (const float* mono, std::int64_t frames, double sampleRate, const volatile bool* cancelled = nullptr);

} // namespace osp::memolab
