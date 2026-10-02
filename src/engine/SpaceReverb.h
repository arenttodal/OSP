#pragma once

#include "engine/Shaping.h"

#include <array>
#include <vector>

namespace osp
{

/**
    SPACE (shaping system v1.0 §40-46): four curated ambiences, only TYPE and DECAY
    exposed.

      ROOM     small, dark, present early reflections, moderate width.
      CHAMBER  warmer and larger, more diffusion, a softer onset.
      PLATE    dense and bright from the start, wide, gently modulated tail.
      SPRING   a dispersive allpass chain in a short feedback loop: the chirp and
               "drip" of a tank, band-limited and narrow.

    One engine (pre-delay, early reflections, input diffusion, an 8-line feedback delay
    network with damping and modulation, output tone) configured per type, plus a
    separate spring tank. The decay retunes the feedback gains smoothly. prepare()
    allocates; everything else is real-time safe.
*/
class SpaceReverb
{
public:
    void prepare (double sampleRate);
    void reset() noexcept;
    void configure (SpaceType type, double decaySeconds) noexcept;
    /** Wet output only. */
    void process (float inL, float inR, float& outL, float& outR) noexcept;

private:
    static constexpr int lines = 8;
    static constexpr int diffusers = 4;
    static constexpr int erTaps = 8;
    static constexpr int springStages = 16;

    struct Delay
    {
        std::vector<float> buffer;
        int write = 0, mask = 0;
        void allocate (int samples);
        void clear() noexcept;
        void push (float x) noexcept { buffer[static_cast<std::size_t> (write)] = x; write = (write + 1) & mask; }
        float tap (int delay) const noexcept { return buffer[static_cast<std::size_t> ((write - delay) & mask)]; }
        float tapFrac (double delay) const noexcept;
    };

    double sampleRate = 48000.0;
    SpaceType type = SpaceType::plate;
    double decay = 1.8;

    Delay pre, er;
    std::array<Delay, diffusers> diff;
    std::array<int, diffusers> diffLength {};
    float diffGain = 0.7f;
    std::array<Delay, lines> fdn;
    std::array<int, lines> fdnLength {};
    std::array<float, lines> fdnGain {};
    std::array<float, lines> damp {};
    float dampCoef = 0.3f;
    std::array<int, erTaps> erDelay {};
    std::array<float, erTaps> erGainL {}, erGainR {};
    float erLevel = 0.0f;
    int preDelay = 0;
    float modDepth = 0.0f;
    double modPhase = 0.0, modRate = 0.5;
    float width = 1.0f;
    float lowL = 0.0f, lowR = 0.0f, highStateL = 0.0f, highStateR = 0.0f;
    float toneLow = 0.5f, toneHigh = 0.02f;
    float outputGain = 1.0f;

    // Spring
    std::array<float, springStages> springStateA {}, springStateB {};
    Delay springA, springB;
    int springLengthA = 1, springLengthB = 1;
    float springGain = 0.0f, springCoef = 0.6f;
    float springLow = 0.0f, springHigh = 0.0f;
};

} // namespace osp
