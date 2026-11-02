#pragma once

#include "core/Prng.h"
#include "engine/Shaping.h"

#include <array>
#include <vector>

namespace osp
{

/**
    BLOOM: SPACE's diffusion delay (docs/spec/SPACE_ENGINES_PLAN.md, section 5.2). A stereo
    ping-pong delay whose feedback path carries a chain of allpass diffusers: at DIFFUSION 0
    the repeats are clean echoes, raised they smear until the delay is a reverb that pulses
    at the tempo. An original design (no product's code or constants).

      TIME       synced to the host tempo (ECHO's divisions) or free (20..1500 ms); a
                 change glides (the repeats bend, as a delay's do), never clicks;
      DECAY      the feedback, as the time the wash takes to fall 60 dB;
      DIFFUSION  the six diffusers' gain in the loop (0 .. 0.75);
      SIZE       the diffusers' lengths;
      DAMP       the loop's tone (a one-pole from ~12 kHz down to ~1.5 kHz);
      MOD        the diffusers' and the delay's slow motion;
      variants   the loop's character: CLEAN (nothing added), VINTAGE (a bucket-brigade's
                 band limit, companded hiss, soft limit), TAPE (wow, flutter, saturation,
                 head loss), WASH (diffusion pinned high and a second diffusion stage after
                 the loop: hardly a discrete repeat left).

    Safety: the loop gain never reaches 1 (0.97 at most) and every write passes a soft
    limit. Real-time safe and deterministic (the hiss and the wander are seeded).
*/
class BloomReverb
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
        BloomVariant variant = BloomVariant::clean;
        bool sync = true;
        int division = 6;
        double timeMs = 350.0;
        double diffusion = 0.6;

        static Settings from (const Shaping& s) noexcept;
        /** Nothing in BLOOM needs a new instance: every change is played in place. */
        bool structurallyDifferent (const Settings&) const noexcept { return false; }
    };

    static constexpr double maxSeconds = 2.5;
    /** The delay time the settings ask for at a tempo (seconds, at most maxSeconds). */
    static double timeSeconds (const Settings& s, double bpm) noexcept;

    void prepare (double sampleRate);
    void reset() noexcept;
    void configure (const Settings& settings) noexcept { tune (settings); }
    void tune (const Settings& settings) noexcept;
    void setTempo (double bpm) noexcept;
    void process (float inL, float inR, float& outL, float& outR) noexcept;
    void skip() noexcept {}

    const Settings& settings() const noexcept { return current; }

    static constexpr int diffusers = 6, postDiffusers = 4;

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
        float tapCubic (double delay) const noexcept;
    };
    struct Svf
    {
        float ic1 = 0.0f, ic2 = 0.0f;
        float lowPass (float x, float g, float k) noexcept;
        float highPass (float x, float g, float k) noexcept;
    };
    struct Side
    {
        Delay line;
        std::array<Delay, diffusers> diff;
        std::array<Delay, postDiffusers> post;
        float tone = 0.0f, bbd1 = 0.0f, bbd2 = 0.0f, head = 0.0f;
        float out = 0.0f;   ///< what the loop sent across last
    };

    void computeLoop() noexcept;
    void updateFilters() noexcept;
    static float limit (float x) noexcept;

    double sampleRate = 48000.0, bpm = 120.0;
    Settings current;

    std::array<Svf, 2> lowCut {}, highCut {};
    double lowCutOct = 0.0, highCutOct = 0.0, lowCutTarget = 0.0, highCutTarget = 0.0;
    float gLow = 0.0f, gHigh = 1.0f;
    int filterCountdown = 0;
    Delay pre;
    double preSamples = 0.0, preTarget = 0.0;

    std::array<Side, 2> sides;
    std::array<int, diffusers> diffLength {};
    std::array<int, postDiffusers> postLength {};
    double delaySamples = 1.0, delayTarget = 1.0;   ///< the loop's time, gliding
    float feedback = 0.5f, diffGain = 0.4f, postGain = 0.0f, toneCoef = 0.5f;
    // Slow motion: the delay's read (MOD; TAPE's wow and flutter on top).
    double wowPhase = 0.0, flutterPhase = 0.0, wanderPhase = 0.0, modDepth = 0.0;
    Prng rng { 0x424c4f4dull };
    float bbdCoef = 0.3f, headCoef = 0.6f;

    float width = 1.0f, widthTarget = 1.0f;
    float outputGain = 1.0f;
    float dcL = 0.0f, dcR = 0.0f, dcCoef = 0.001f;
};

} // namespace osp
