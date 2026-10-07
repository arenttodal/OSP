#pragma once

#include "core/Prng.h"
#include "engine/Shaping.h"

#include <array>
#include <numbers>
#include <vector>

namespace osp
{

/**
    SPACE v2: four rooms with their own architecture, plus the controls a reverb is
    played with (PRE-DELAY, SIZE, DECAY, DAMP, MOD, WIDTH and an input EQ, LOW / HIGH CUT).

      ROOM     a real small room: early reflections from a shoebox's image sources
               (first and second order, walls absorbing), a short, dense, slightly
               dark tail with a little more bass than mid.
      HALL     a concert hall after the Berlin halls for orchestra: a ~20 ms gap, then
               strong lateral early reflections (the vineyard terraces of the Philharmonie,
               the shoebox walls of the Konzerthaus) over the first 110 ms, a soft build
               into a long, warm tail (bass ratio 1.2, air absorbing the top) that moves
               slowly, the way a full hall never stands still.
      PLATE    a steel plate: no early reflections, instant density, bright, lean in the
               bass, wide, the most modulated.
      SPRING   a dispersive allpass chain in a short feedback loop: the chirp and "drip"
               of a tank, band-limited and narrow.

    Room, hall and plate share one "lush" late network in the manner of the classic
    studio halls: stereo input diffusion (four allpasses per side), an 8-line feedback
    delay network whose lines each hold an allpass (density builds inside the loop),
    read through cubic interpolation at slowly moving positions (a sine per line plus a
    seeded random wander, so the tail chorus is rich and never metallic), and a two-band
    decay per line (low / mid / high reverberation times from DECAY, the type's bass
    ratio and DAMP). The input EQ is two 12 dB/oct filters before everything, as on a
    hardware return.

    configure() sets everything (type and size retune the network's lengths, so the
    owner crossfades to a configured instance for those); tune() changes the rest in
    place without a click. prepare() allocates; everything else is real-time safe and
    deterministic (the wander's random walk is seeded).
*/
class SpaceReverb
{
public:
    struct Settings
    {
        SpaceType type = SpaceType::plate;
        double decaySeconds = 1.8;
        double preDelayMs = 8.0;
        double size = 0.5;
        double damping = 0.4;
        double modulation = 0.4;
        double width = 1.0;
        double lowCutHz = 100.0;
        double highCutHz = 12000.0;

        static Settings from (const Shaping& s) noexcept;
        /** Type or size differ: the network's lengths change (crossfade to a new instance). */
        bool structurallyDifferent (const Settings& other) const noexcept;
    };

    /** What a room is made of, for the SPACE display (pure, no state). */
    struct Portrait
    {
        static constexpr int maxReflections = 16;
        double preMs = 0.0;                                ///< PRE-DELAY plus the room's own gap
        std::array<double, maxReflections> erMs {};        ///< early reflections, ms after the pre-delay
        std::array<float, maxReflections> erGain {};       ///< their level (0..1)
        int erCount = 0;
        float erLevel = 0.0f;
        float damping = 0.0f;       ///< 0 bright .. 1 dark tail (how fast the highs die)
        float width = 1.0f;
        double diffusionMs = 0.0;   ///< how fast the tail fills in
        double bassRatio = 1.0;
        bool spring = false;        ///< spring: echoes every ~33 / 41 ms, dispersed into chirps
    };
    static Portrait portrait (const Settings& settings) noexcept;
    static Portrait portrait (SpaceType type) noexcept;

    void prepare (double sampleRate);
    void reset() noexcept;
    /** Everything, including the network's lengths (type, size): on a reset or idle instance. */
    void configure (const Settings& settings) noexcept;
    /** Decay, damping, modulation, width, pre-delay and EQ, in place and smoothly. */
    void tune (const Settings& settings) noexcept;
    /** Wet output only. */
    void process (float inL, float inR, float& outL, float& outR) noexcept;
    /** One sample the caller does not need processed (the reverb is asleep): only the
        modulation's clocks move on, so the tail resumes exactly in phase. */
    void skip() noexcept;

    const Settings& settings() const noexcept { return current; }

private:
    static constexpr int lines = 8;
    static constexpr int diffusers = 4;
    static constexpr int maxTaps = Portrait::maxReflections;
    static constexpr int springStages = 16;
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

    /** Zavalishin's state-variable filter (12 dB/oct), low- or high-pass output. */
    struct Svf
    {
        float ic1 = 0.0f, ic2 = 0.0f;
        float lowPass (float x, float g, float k) noexcept;
        float highPass (float x, float g, float k) noexcept;
    };

    struct Line
    {
        Delay delay, allpass;
        double length = 1.0;      ///< samples (before modulation)
        int allpassLength = 1;
        float gainLow = 0.0f, gainMid = 0.0f, gainHigh = 0.0f;
        float lowState = 0.0f, highState = 0.0f;
        // Modulation: a sine (a rotating phasor) plus a seeded random wander.
        double phase = 0.0, phaseStep = 0.0;
        double sinValue = 0.0, cosValue = 1.0, stepSin = 0.0, stepCos = 1.0;
        float wander = 0.0f, wanderFrom = 0.0f, wanderTo = 0.0f;
    };

    void advanceModulation() noexcept;
    void updateFilters() noexcept;
    void computeDecay() noexcept;
    void buildReflections() noexcept;
    float processSpring (float x) noexcept;

    double sampleRate = 48000.0;
    Settings current;
    double sizeFactor = 1.0;

    // Input EQ (smoothed in octaves) and pre-delay (glides).
    std::array<Svf, 2> lowCut {}, highCut {};
    double lowCutOct = 0.0, highCutOct = 0.0, lowCutTarget = 0.0, highCutTarget = 0.0;
    float gLow = 0.0f, gHigh = 1.0f;
    int filterCountdown = 0;
    Delay preL, preR;
    double preSamples = 0.0, preTarget = 0.0;

    // Early reflections
    Delay er;
    std::array<int, maxTaps> erDelay {};
    std::array<float, maxTaps> erGainL {}, erGainR {};
    int erCount = 0;
    float erLevel = 0.0f, erFeed = 0.0f, erToneCoef = 0.5f, erToneL = 0.0f, erToneR = 0.0f;

    // Diffusion and the late network
    std::array<std::array<Delay, diffusers>, 2> diff;
    std::array<int, diffusers> diffLength {}, diffLengthR {};
    float diffGain = 0.7f, loopAllpassGain = 0.6f;
    std::array<Line, lines> net;
    float lowCoef = 0.03f, highCoef = 0.3f;
    double modDepth = 0.0, modDepthTarget = 0.0;   ///< samples
    int modCountdown = 0, resyncCountdown = 0;
    Prng wanderRng { 0x5350414345ull };
    float width = 1.0f, widthTarget = 1.0f, typeWidth = 1.0f;
    float outputGain = 1.0f, direct = 0.0f;
    float dcL = 0.0f, dcR = 0.0f, dcCoef = 0.001f;

    // Spring
    std::array<float, springStages> springStateA {}, springStateB {};
    Delay springA, springB;
    int springLengthA = 1, springLengthB = 1;
    float springGain = 0.0f, springCoef = 0.6f;
    float springLow = 0.0f, springHigh = 0.0f, springToneLow = 0.4f, springToneHigh = 0.02f;
};

} // namespace osp
