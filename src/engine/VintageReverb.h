#pragma once

#include "core/Prng.h"
#include "engine/Shaping.h"

#include <array>
#include <cstdint>
#include <vector>

namespace osp
{

/**
    VINTAGE: SPACE's early-1980s digital hall (docs/spec/SPACE_ENGINES_PLAN.md, section 5.1).
    An original design after what is published about how those machines sounded. No code,
    microcode or coefficient tables of any machine are used. The character comes from:

      a low internal rate   the network runs at 20.48 kHz (EARLY) or 34.13 kHz (LATER),
                            reached through steep anti-alias filters (6th order at 0.42 x
                            the rate) and resampled back the same way: no air above ~8.6 or
                            ~14.3 kHz, as on the hardware;
      short words           the delay memory keeps 16 bits (TPDF-dithered from a seeded
                            generator), the input passes a 12-bit gain-ranging converter
                            (four ranges 6 dB apart): the quiet tail grows a gentle grain;
      coarse coefficients   diffusion gains on a 1/32 grid, decay gains on a 1/256 grid,
                            the damping on a 1/32 grid;
      the tank              two mirrored halves (an allpass, a long delay, a damping pole,
                            the decay, a second allpass, a second delay) cross-feeding each
                            other in a figure eight, read at several taps for each side;
      the chorus            MOD moves the long delays' read point between adjacent taps (a
                            crossfade, no LFO pitch wobble), towards seeded random targets.

    Variants (lengths and voicing): HALL, PLATE (dense, bright), CHAMBER, ROOM. The SPACE
    controls keep their meaning (DECAY the reverberation time, SIZE the lengths, DAMP the
    highs, MOD the chorus, WIDTH, PRE-DELAY, LOW / HIGH CUT); DIFFUSION is its own.

    The SpaceReverb contract: prepare() allocates; configure() (variant, era, SIZE: the
    owner crossfades) on a reset or idle instance; tune() in place; process() / skip()
    real-time safe and deterministic.
*/
class VintageReverb
{
public:
    struct Settings
    {
        double decaySeconds = 3.0;
        double preDelayMs = 8.0;
        double size = 0.5;
        double damping = 0.4;
        double modulation = 0.4;
        double width = 1.0;
        double lowCutHz = 100.0;
        double highCutHz = 5000.0;
        VintageVariant variant = VintageVariant::hall;
        VintageEra era = VintageEra::early;
        double diffusion = 0.7;   ///< 0..1

        static Settings from (const Shaping& s) noexcept;
        /** Variant, era or SIZE differ: the network is rebuilt (crossfade to a new instance). */
        bool structurallyDifferent (const Settings& other) const noexcept;
    };

    /** The network's rate for an era (Hz). */
    static double internalRate (VintageEra era) noexcept { return era == VintageEra::early ? 20480.0 : 34130.0; }

    void prepare (double sampleRate);
    void reset() noexcept;
    void configure (const Settings& settings) noexcept;
    void tune (const Settings& settings) noexcept;
    void process (float inL, float inR, float& outL, float& outR) noexcept;
    void skip() noexcept {}

    const Settings& settings() const noexcept { return current; }

private:
    struct Delay
    {
        std::vector<float> buffer;
        int write = 0, mask = 0;
        void allocate (int samples);
        void clear() noexcept;
        void push (float x) noexcept { buffer[static_cast<std::size_t> (write)] = x; write = (write + 1) & mask; }
        float tap (int delay) const noexcept { return buffer[static_cast<std::size_t> ((write - delay) & mask)]; }
        float tapLinear (double delay) const noexcept;
    };
    struct Svf
    {
        float ic1 = 0.0f, ic2 = 0.0f;
        float lowPass (float x, float g, float k) noexcept;
        float highPass (float x, float g, float k) noexcept;
    };
    /** A 6th-order Butterworth low-pass (three biquads, transposed direct form II). */
    struct Steep
    {
        std::array<std::array<double, 5>, 3> c {};   // b0 b1 b2 a1 a2
        std::array<std::array<double, 2>, 3> z {};
        void design (double cutoffHz, double sampleRate) noexcept;
        void clear() noexcept { z = {}; }
        float process (float x) noexcept;
    };
    struct Half
    {
        Delay ap1, line1, ap2, line2;
        int ap1Length = 1, ap2Length = 1, line2Length = 1;
        double line1Length = 1.0;
        float lp = 0.0f, out = 0.0f;   ///< the damping pole; what it last sent across
        // The chorus: line 1's read point, gliding towards seeded targets.
        double offset = 0.0, offsetTarget = 0.0, offsetStep = 0.0;
        int countdown = 0;
    };

    float networkTick (float x, float& left, float& right) noexcept;
    float quantise16 (float x) noexcept;
    static float convert12 (float x) noexcept;
    void updateFilters() noexcept;

    double hostRate = 48000.0, rate = 20480.0;
    Settings current;

    // Host side: EQ, pre-delay, anti-alias / reconstruction, the resampler.
    std::array<Svf, 2> lowCut {}, highCut {};
    double lowCutOct = 0.0, highCutOct = 0.0, lowCutTarget = 0.0, highCutTarget = 0.0;
    float gLow = 0.0f, gHigh = 1.0f;
    int filterCountdown = 0;
    Delay pre;
    double preSamples = 0.0, preTarget = 0.0;
    Steep antiAlias, reconstructL, reconstructR;
    std::array<float, 4> history {};   ///< the filtered input, newest last
    double nextTime = 0.0;            ///< the next network tick, in host samples from now
    double step = 1.0;                ///< host samples per network tick
    float lastL = 0.0f, lastR = 0.0f, prevL = 0.0f, prevR = 0.0f;   ///< the network's last two outputs
    double lastTickAge = 0.0;         ///< host samples since the last tick

    // The network (internal rate).
    std::array<Delay, 4> diffusers;
    std::array<int, 4> diffLength {};
    float diffGain1 = 0.0f, diffGain2 = 0.0f, tankGain = 0.5f, decayGain = 0.5f, dampCoef = 0.3f;
    std::array<Half, 2> halves;
    float apGain2 = 0.5f;
    double chorusDepth = 0.0;
    Prng rng { 0x56494e54ull };
    struct Tap
    {
        int half = 0, line = 0, delay = 1;
        float sign = 1.0f;
    };
    std::array<std::array<Tap, 5>, 2> taps {};   ///< each side's output taps

    float width = 1.0f, widthTarget = 1.0f;
    float outputGain = 0.505f;   ///< (as loud as HALL at the same send: measured)
    float dcL = 0.0f, dcR = 0.0f, dcCoef = 0.001f;
};

} // namespace osp
