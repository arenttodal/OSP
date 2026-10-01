#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace osp
{

/**
    Immutable result of analysing one source recording.

    Persisted as JSON (see docs/analysis-schema.md). Bump analysisSchemaVersion whenever
    a field changes meaning or is removed; adding optional fields keeps the version but
    must be documented. Units: seconds, Hz, dB (dBFS for absolute levels, dB re max RMS
    for relative ones), cents. Ratios in [0, 1] are documented per field.
*/
inline constexpr int analysisSchemaVersion = 1;
inline constexpr const char* analyzerVersion = "0.1.0";

/** A uniformly sampled descriptor trajectory. Frame i is centred at i * hopSeconds. */
struct TimeSeries
{
    double hopSeconds = 0.0;
    std::vector<float> values;
};

/** A slow periodic modulation (vibrato, tremolo). */
struct Modulation
{
    double rateHz = 0.0;
    double depth = 0.0;     ///< vibrato: peak deviation in cents; tremolo: peak-to-peak in dB
    double strength = 0.0;  ///< normalised autocorrelation at the modulation period, 0..1
};

struct SourceInfo
{
    std::string filename;
    std::string path;
    std::string contentHash;   ///< "sha256:<hex>" of the file bytes (empty if not from a file)
    std::string format;        ///< "WAV", "AIFF", "synthetic", ...
    int bitDepth = 0;
    bool isFloatingPoint = false;
    double sampleRate = 0.0;
    int channels = 0;
    std::int64_t frames = 0;
    double durationSeconds = 0.0;
    bool truncated = false;    ///< loader stopped at its maximum length
};

struct PitchAnalysis
{
    bool detected = false;            ///< a usable, non-fabricated estimate exists
    double fundamentalHz = 0.0;       ///< stable (weighted median) F0 of voiced frames
    int midiNote = -1;                ///< nearest MIDI note, -1 when not detected
    double centsOffset = 0.0;         ///< fundamentalHz relative to midiNote
    std::string noteName;             ///< e.g. "A3"
    double confidence = 0.0;          ///< 0..1, see docs/analysis-schema.md
    std::string confidenceLevel = "none"; ///< "high" (>= 0.7), "moderate" (>= 0.4), "low", "none"
    double voicedFraction = 0.0;      ///< energy-weighted fraction of active frames that are voiced
    double stabilityCents = 0.0;      ///< energy-weighted std dev of voiced F0 around the median
    double rangeCents = 0.0;          ///< 5th..95th percentile spread of voiced F0
    std::optional<Modulation> vibrato;
    TimeSeries trackHz;               ///< 0 = unvoiced / silent frame
    TimeSeries trackConfidence;       ///< per-frame periodicity (1 - YIN aperiodicity)
};

struct Onset
{
    double timeSeconds = 0.0;
    double strength = 0.0;  ///< normalised spectral flux peak, 0..1
};

struct EnvelopeAnalysis
{
    double peakDbfs = -200.0;            ///< absolute sample peak
    double maxRmsDbfs = -200.0;          ///< loudest 20 ms RMS frame
    double leadingSilenceSeconds = 0.0;  ///< before the first sample above -60 dB re peak
    double trailingSilenceSeconds = 0.0; ///< after the last sample above -60 dB re peak
    double onsetSeconds = 0.0;           ///< first RMS frame within 20 dB of max RMS
    double attackSeconds = 0.0;          ///< onset -> first RMS frame within 3 dB of max RMS
    double peakSeconds = 0.0;            ///< time of max RMS frame
    double estimatedDecaySeconds = 0.0;  ///< peak -> last RMS frame within 20 dB of max
    double decaySlopeDbPerSecond = 0.0;  ///< least-squares slope of RMS dB over the body (end of attack -> sound end)
    double sustainLevelDb = -200.0;      ///< median RMS (dB re max) over the body
    double sustainFluctuationDb = 0.0;   ///< std dev of linearly detrended RMS dB over the body
    bool endsWhileSounding = false;      ///< recording ends within 12 dB of max RMS (no natural release)
    int secondaryPeakCount = 0;          ///< later envelope peaks with >= 6 dB prominence
    std::optional<Modulation> tremolo;
    std::vector<Onset> onsets;           ///< spectral-flux onsets (capped)
    TimeSeries rmsDb;                    ///< 20 ms RMS in dBFS, floored at -120
};

struct SpectralAnalysis
{
    int fftSize = 0;
    double meanCentroidHz = 0.0;
    double centroidStdHz = 0.0;
    double meanRolloffHz = 0.0;          ///< 85 % energy rolloff
    double meanFlatness = 0.0;           ///< 0 (tonal) .. 1 (white noise)
    double meanFlux = 0.0;               ///< mean normalised positive log-spectral flux
    double periodicity = 0.0;            ///< energy-weighted mean (1 - YIN aperiodicity), 0..1
    double harmonicEnergyRatio = 0.0;    ///< energy within +/-3 % of harmonics of the frame F0, 0..1
    double highFrequencyEnergyRatio = 0.0; ///< energy above 4 kHz / total
    double lowFrequencyEnergyRatio = 0.0;  ///< energy below 250 Hz / total ("body")
    TimeSeries centroidHz;
    TimeSeries flux;
    TimeSeries flatness;
};

struct StereoAnalysis
{
    bool isMono = true;           ///< single-channel source
    bool isDualMono = false;      ///< two identical channels
    double correlation = 1.0;     ///< L/R Pearson correlation over active audio, -1..1
    double width = 0.0;           ///< side energy / (mid + side energy): 0 mono, 0.5 decorrelated, 1 out of phase
    double sideToMidDb = -120.0;
    double balanceDb = 0.0;       ///< L energy re R energy (positive = louder left)
    double widthLow = 0.0;        ///< width below 300 Hz
    double widthMid = 0.0;        ///< width 300 Hz .. 3 kHz
    double widthHigh = 0.0;       ///< width above 3 kHz
};

struct AnalysisData
{
    int schemaVersion = analysisSchemaVersion;
    std::string analyzer = analyzerVersion;

    SourceInfo source;
    PitchAnalysis pitch;
    EnvelopeAnalysis envelope;
    SpectralAnalysis spectral;
    StereoAnalysis stereo;

    /** Human-readable notes about anything unusual (silence, truncation, no pitch...). */
    std::vector<std::string> warnings;
};

} // namespace osp
