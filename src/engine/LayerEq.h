#pragma once

#include <array>
#include <cstdint>

namespace osp::eq
{

/**
    Each source layer's own equaliser: five bands on the layer's signal after its voices
    (and its own REIMAGINED stage) and before its LEVEL, PAN and the mix. Minimum phase, no
    latency. Every band is a trapezoidal state-variable filter (Simper / Cytomic): the same
    bilinear-transform responses as the RBJ cookbook designs (bell, shelves in their Q form,
    HP / LP at 12 dB/oct, or 24 dB/oct as two Butterworth sections), but a structure whose
    state stays meaningful when its coefficients move, so gliding and modulated settings do not
    click (a direct-form biquad redesigned under a moving gain does).

    Frequency, gain and Q glide (about 20 ms) and the sections are recomputed from the glided
    settings every 16 samples; switching a band or the whole EQ crossfades its output with its
    input over 10 ms. Off (and fully faded) the layer's samples are not touched at all:
    sessions without the EQ are bit-identical.

    The display draws responseDb(), the exact response of the sections the processor runs, so
    the curve is the sound. Pure C++, real-time safe (no allocation).
*/
enum class Band : int
{
    highPass,
    lowShelf,
    bell,
    highShelf,
    lowPass
};
constexpr int bandCount = 5;
const char* bandName (Band band) noexcept;   ///< "HP", "LOW SHELF", "BELL", "HIGH SHELF", "LP"

struct BandSettings
{
    bool enabled = false;
    double frequencyHz = 1000.0;
    double gainDb = 0.0;      ///< shelves and bell
    double q = 0.707;         ///< bell (0.2..12), shelves (0.3..2): the RBJ Q form
    bool steep = false;       ///< HP / LP: 24 dB/oct instead of 12
    bool operator== (const BandSettings&) const = default;
};

struct Settings
{
    bool enabled = false;     ///< the EQ's own switch (off: bypassed, the layer untouched)
    std::array<BandSettings, bandCount> bands {};
    Settings();
    bool operator== (const Settings&) const = default;
    /** Something is heard: switched on and at least one band on. */
    bool audible() const noexcept;
};

struct Range
{
    double minHz, maxHz, defaultHz;
};
Range frequencyRange (Band band) noexcept;
constexpr double maxGainDb = 18.0;
constexpr double minBellQ = 0.2, maxBellQ = 12.0;
constexpr double minShelfQ = 0.3, maxShelfQ = 2.0;
bool hasGain (Band band) noexcept;   ///< shelves and bell
bool hasQ (Band band) noexcept;      ///< shelves and bell
bool hasSlope (Band band) noexcept;  ///< HP and LP

/** One state-variable section: g = tan (pi fc / fs), damping k, and the output mix of the
    input (m0), band (m1) and low (m2) outputs. */
struct Section
{
    double g = 0.1, k = 1.414, m0 = 1.0, m1 = 0.0, m2 = 0.0;
};

/** The band's sections at a sample rate (1 or 2; frequency kept below Nyquist): exactly what
    the processor runs. */
int design (Band band, const BandSettings& settings, double sampleRate, std::array<Section, 2>& sections) noexcept;
/** Magnitude (dB) of one section at a frequency (its exact digital response). */
double sectionDb (const Section& section, double hz, double sampleRate) noexcept;
/** The whole EQ's magnitude (dB) at a frequency: the switched-on bands' sections multiplied
    (0 dB when the EQ is off). */
double responseDb (const Settings& settings, double hz, double sampleRate) noexcept;
/** One band's own magnitude (dB) at a frequency, whether or not it is on (the display). */
double bandResponseDb (Band band, const BandSettings& settings, double hz, double sampleRate) noexcept;

class Processor
{
public:
    void prepare (double sampleRate) noexcept;
    void reset() noexcept;
    /** The settings to move to (glided; switches crossfade). Audio thread, any block. */
    void setTarget (const Settings& settings) noexcept;
    /** Engaged: switched on with a band on, or still fading (the layer's signal goes through it). */
    bool active() const noexcept;
    /** Its filters still hold a tail (a layer that stopped sounding is run until it rings out). */
    bool ringing() const noexcept;
    /** Filters the layer's signal in place (`right` ignored when mono). */
    void process (float* left, float* right, int numSamples, bool mono) noexcept;

private:
    struct BandState
    {
        double logHz = 0.0, gainDb = 0.0, q = 0.707;   ///< glided settings
        double mix = 0.0;                              ///< 0 off .. 1 on (crossfade)
        bool steep = false;
        int sectionCount = 0;
        std::array<Section, 2> sections {};
        std::array<std::array<double, 2>, 4> state {};   ///< [section * 2 + channel][ic1eq, ic2eq]
        bool designed = false;
    };
    void glide (int samples) noexcept;
    void processBand (BandState& b, int band, float* left, float* right, int n, bool mono) noexcept;

    double sampleRate = 48000.0;
    Settings target;
    std::array<BandState, bandCount> bands {};
    double masterMix = 0.0;
    bool started = false;
};

} // namespace osp::eq
