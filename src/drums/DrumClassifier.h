#pragma once

#include "core/AudioData.h"
#include "drums/DrumKitEngine.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

/**
    ANDOR Drums' drum classification (spec 6.2, 7.5, 7.6, Milestone 7). Pure C++, deterministic,
    background threads. No model weights: a transparent feature-based classifier (spec 7.7,
    CLAUDE.md rule 5), behind an interface a licensed model could implement later.

      1. the file name, when it says what it is (kick, sd, hh, OH, crash ...)
      2. lightweight audio features of the hit: band energies, spectral centroid and
         flatness, decay, attack bursts, a low tonal peak, a bright tail
      3. scores per category; the best one with a confidence; Unknown when unsure
      4. a hit with strong low and strong high content at once (kick + hat) is flagged
         layered, and its confidence is lowered: no pretending a clean kick was extracted

    Similarity grouping proposes round robins (Kick_01, Kick_02 ... that also sound alike);
    auto-mapping proposes pads, with an overflow tray. Everything is a suggestion.
*/
namespace osp::drums
{

/** What a sound's features are (all from the first hit in the region). */
struct DrumFeatures
{
    double durationSeconds = 0.0;   ///< from the onset until 40 dB below its peak
    double attackSeconds = 0.0;     ///< onset to the energy peak
    double centroidHz = 0.0;        ///< of the first 80 ms
    double flatness = 0.0;          ///< 0 tonal .. 1 noise (100 Hz - 16 kHz, first 80 ms)
    double tailCentroidHz = 0.0;    ///< of 100 - 300 ms (open hats, cymbals keep it high)
    std::array<double, 6> bands {}; ///< energy share: <80, 80-200, 200-800, 0.8-3k, 3-8k, >8k Hz
    double lowPeakHz = 0.0;         ///< strongest peak under 400 Hz
    double lowPeakProminence = 0.0; ///< how much it stands out (dB over its neighbourhood)
    int bursts = 1;                 ///< energy peaks in the first 40 ms (claps have 2-4)
    double peakDbfs = -200.0;
    bool valid = false;

    double low() const noexcept { return bands[0] + bands[1]; }      ///< below 200 Hz
    double high() const noexcept { return bands[4] + bands[5]; }     ///< above 3 kHz
};

/** The features of `audio`'s region [start, end) (frames; end <= 0: to the end). */
DrumFeatures drumFeatures (const AudioData& audio, std::int64_t start = 0, std::int64_t end = 0);

struct DrumGuess
{
    DrumCategory category = DrumCategory::unknown;
    double confidence = 0.0;        ///< 0..1
    bool layered = false;           ///< several instruments at once (uncertain)
    bool fromName = false;          ///< the file name decided
    std::string reason;             ///< in words, for the import preview
};

/** The name's evidence alone (unknown with 0 confidence if it says nothing). */
DrumGuess classifyName (const std::string& filename);

/** The sound's evidence alone. */
DrumGuess classifyFeatures (const DrumFeatures& features);

/** Name first when it is reliable, the sound supplementing it; below 0.45: Unknown. */
DrumGuess classify (const std::string& filename, const DrumFeatures& features);

/** The classifier interface (a licensed model could implement it later). */
class DrumClassifier
{
public:
    virtual ~DrumClassifier() = default;
    virtual DrumGuess classify (const std::string& filename, const DrumFeatures& features) const = 0;
};

/** The shipped one: names and features, as above. */
class FeatureClassifier final : public DrumClassifier
{
public:
    DrumGuess classify (const std::string& filename, const DrumFeatures& features) const override { return drums::classify (filename, features); }
};

/** 0 (unrelated) .. 1 (the same sound): the features' distance. */
double similarity (const DrumFeatures& a, const DrumFeatures& b) noexcept;

/** A name without its take / velocity / round-robin numbering ("Kick_03.wav" -> "kick"). */
std::string nameStem (const std::string& filename);

/** One item to group (a file or a slice). */
struct GroupItem
{
    std::string name;
    DrumFeatures features;
    DrumGuess guess;
};

/** Groups of item indices proposed as one pad each (round robins when several). Items
    group when they share a category and either a name stem and a similar sound, or a very
    similar sound. Order: by category (kit order), then first appearance. */
std::vector<std::vector<int>> groupItems (const std::vector<GroupItem>& items);

/** Pads for the groups: a pad of the same category first, then the next free pad in
    `freePads` order; groups beyond the free pads go to the tray (-1). `padCategories`:
    each pad's current category; `freePads`: pads that may receive a group, in order. */
std::vector<int> mapGroupsToPads (const std::vector<std::vector<int>>& groups, const std::vector<GroupItem>& items,
                                  const std::array<DrumCategory, maxPads>& padCategories, const std::vector<int>& freePads);

} // namespace osp::drums
