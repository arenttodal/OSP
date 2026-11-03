#pragma once

#include "core/Prng.h"
#include "engine/Shaping.h"

#include <array>
#include <cstdint>
#include <vector>

namespace osp
{

/**
    ABYSS: SPACE's vast, modulated space (docs/spec/SPACE_ENGINES_PLAN.md, section 5.3). An
    original design on the textbook structure (Jot's feedback delay networks, Householder
    mixing): none of its lengths or corners come from any product.

      input     LOW / HIGH CUT (as the rooms), PRE-DELAY, GRAVITY's window, four allpass
                diffusers per side;
      stage 1   a 16-line feedback delay network, Householder mixed (lossless, O(N)), every
                line read through a moving cubic tap (a sine and a seeded wander) and given
                a two-band decay (LOW / MID / HIGH reverberation times);
      stage 2   two 4-line networks (left, right), longer, fed by stage 1: the space's
                second, larger bloom;
      safety    a soft ceiling on every line written (inaudible below full scale), a
                non-finite guard that clears the network (counted), a 0.9995 loop-gain
                ceiling except under FREEZE.

    The SPACE controls keep their meaning: DECAY the mid reverberation time, SIZE the
    lengths (0.35x .. 2.5x: up to a hole), DAMP the highs' share, MOD the motion, WIDTH,
    PRE-DELAY, LOW / HIGH CUT. ABYSS's own:
      GRAVITY    -1..+1. 0 a natural decay; up to +1 longer (x3) and darker; below 0 the
                 envelope turns over: the input reaches the network through a window of
                 taps whose level RISES towards its end and the loop dies faster, so the
                 sound swells into the space instead of starting loud.
      FREEZE     the input fades out of the loop and the loop becomes lossless (its decay
                 gains glide to 1 in ~60 ms): the space holds what it has, still moving.
                 Released, the set decay returns.
      RESONANCE  0..1: the band between the two crossovers rings longer (x1..x3) and the
                 input diffusion thins, so the space's modes sing.

    The same contract as SpaceReverb: prepare() allocates; configure() on a reset or idle
    instance (SIZE retunes the lengths: the owner crossfades); tune() moves the rest in
    place; process() and skip() are real-time safe and deterministic (seeded wander).
*/
class AbyssReverb
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
        double gravity = 0.0;     ///< -1..1
        bool freeze = false;
        double resonance = 0.0;   ///< 0..1

        static Settings from (const Shaping& s) noexcept;
        /** SIZE differs: the network's lengths change (crossfade to a new instance). */
        bool structurallyDifferent (const Settings& other) const noexcept;
    };

    /** SIZE's length factor (0.35 at 0 %, 1 at 50 %, 2.5 at 100 %). */
    static double sizeFactor (double size) noexcept;
    /** The mid reverberation time GRAVITY makes of DECAY (seconds). */
    static double effectiveDecay (double decaySeconds, double gravity) noexcept;

    void prepare (double sampleRate);
    void reset() noexcept;
    void configure (const Settings& settings) noexcept;
    void tune (const Settings& settings) noexcept;
    /** Wet output only. */
    void process (float inL, float inR, float& outL, float& outR) noexcept;
    /** The reverb is asleep: only the modulation's clocks move on. */
    void skip() noexcept;

    const Settings& settings() const noexcept { return current; }
    /** Non-finite samples met (the network was cleared each time): 0 in a healthy run. */
    std::uint32_t guardEvents() const noexcept { return guards; }

    static constexpr int stage1Lines = 16;
    static constexpr int stage2Lines = 4;   ///< per side

private:
    static constexpr int diffusers = 4;
    static constexpr int gravityTaps = 16;
    static constexpr int modulationBlock = 64;

    struct Delay
    {
        std::vector<float> buffer;
        int write = 0, mask = 0;
        void allocate (int samples);
        void clear() noexcept;
        void push (float x) noexcept { buffer[static_cast<std::size_t> (write)] = x; write = (write + 1) & mask; }
        float tap (int delay) const noexcept { return buffer[static_cast<std::size_t> ((write - delay) & mask)]; }
        float tapLinear (double delay) const noexcept;
        float tapCubic (double delay) const noexcept;
    };

    struct Svf
    {
        float ic1 = 0.0f, ic2 = 0.0f;
        float lowPass (float x, float g, float k) noexcept;
        float highPass (float x, float g, float k) noexcept;
    };

    struct Line
    {
        Delay delay;
        double length = 1.0;   ///< samples (before modulation)
        float gainLow = 0.0f, gainMid = 0.0f, gainHigh = 0.0f;   ///< targets with FREEZE applied
        float lowState = 0.0f, highState = 0.0f;
        double phase = 0.0, phaseStep = 0.0, sinValue = 0.0, cosValue = 1.0, stepSin = 0.0, stepCos = 1.0;
        float wander = 0.0f, wanderFrom = 0.0f, wanderTo = 0.0f;
    };

    void computeDecay() noexcept;
    void updateFilters() noexcept;
    void advanceModulation() noexcept;
    void clearNetwork() noexcept;
    static float ceiling (float x) noexcept;

    double sampleRate = 48000.0;
    Settings current;
    double lengthFactor = 1.0;

    // Input EQ and pre-delay (as the rooms').
    std::array<Svf, 2> lowCut {}, highCut {};
    double lowCutOct = 0.0, highCutOct = 0.0, lowCutTarget = 0.0, highCutTarget = 0.0;
    float gLow = 0.0f, gHigh = 1.0f;
    int filterCountdown = 0;
    Delay preL, preR;
    double preSamples = 0.0, preTarget = 0.0;

    // GRAVITY's window (below 0): taps rising towards its end.
    Delay windowL, windowR;
    std::array<int, gravityTaps> windowDelay {};
    std::array<float, gravityTaps> windowGain {};
    float windowMix = 0.0f, windowMixTarget = 0.0f;

    // Diffusion.
    std::array<std::array<Delay, diffusers>, 2> diff;
    std::array<int, diffusers> diffLength {}, diffLengthR {};
    float diffGain = 0.7f, diffGainTarget = 0.7f;

    // The networks.
    std::array<Line, stage1Lines> net1;
    std::array<std::array<Line, stage2Lines>, 2> net2;
    float lowCoef = 0.03f, highCoef = 0.3f;
    double modDepth = 0.0, modDepthTarget = 0.0;
    int modCountdown = 0, resyncCountdown = 0;
    Prng wanderRng { 0x41425953ull };

    // FREEZE: 0 open .. 1 frozen, glided.
    float freezeNow = 0.0f, freezeTarget = 0.0f, freezeStep = 0.0f;

    float width = 1.0f, widthTarget = 1.0f;
    float outputGain = 1.46f;   ///< (as loud as HALL at the same send: measured)
    float dcL = 0.0f, dcR = 0.0f, dcCoef = 0.001f;
    std::uint32_t guards = 0;
};

} // namespace osp
