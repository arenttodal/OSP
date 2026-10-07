#pragma once

#include "core/Prng.h"
#include "engine/Shaping.h"

#include <array>
#include <vector>

namespace osp
{

/**
    ECHO: a delay in parallel with SPACE (the ECHO macro is its send level), as one of two
    machines.

      TAPE  a tape echo (the RE-201 / Echoplex family): every pass goes through the record
            and playback heads, so each repeat is a little darker and thinner (a gentle
            low cut with the head's bass bump, a high cut that TONE moves), saturates
            softly as FEEDBACK rises (it runs away into warm compression, never into
            harsh clipping), and wobbles: AGE adds wow (a slow, seeded wander around
            ~0.8 Hz) and flutter (~7 Hz). Moving TIME changes the tape speed, so the
            repeats glide in pitch, as on the machine.
      BBD   a bucket-brigade delay (the DM-2 / Memory Man family): the clock that sets
            the time also sets the bandwidth - long times are darker - through steep
            anti-alias and reconstruction filters around the line; the repeats are
            soft-clipped, darker and grainier than tape, and AGE adds the slow chorus
            those pedals put on their repeats.

    TIME is synced to the host tempo (1/16 .. 1 bar, dotted and triplet) or free (ms).
    STEREO: MONO in the centre, PING-PONG alternating sides, WIDE two heads a little apart.
    prepare() allocates (2.6 s per side); everything else is real-time safe and
    deterministic (the wow is a seeded random walk).
*/
class EchoDelay
{
public:
    struct Settings
    {
        EchoType type = EchoType::tape;
        bool sync = true;
        int division = 5;
        double timeMs = 375.0;
        double feedback = 0.45;
        double tone = 0.5;
        double age = 0.35;
        EchoStereo stereo = EchoStereo::pingPong;

        static Settings from (const Shaping& s) noexcept;
    };

    static constexpr double maxSeconds = 2.5;

    void prepare (double sampleRate);
    void reset() noexcept;
    void setSettings (const Settings& settings) noexcept;
    void setTempo (double bpm) noexcept;
    /** The delay time the settings ask for at a tempo, in seconds (capped at maxSeconds). */
    static double timeSeconds (const Settings& settings, double bpm) noexcept;

    /** Wet output only (the repeats). */
    void process (float inL, float inR, float& outL, float& outR) noexcept;

    /** True when nothing has come in for longer than the line holds and the repeats have
        died away (below -120 dBFS): the owner may stop calling process() until sound returns. */
    bool silent() const noexcept { return quietRun > sleepAfter; }
    /** One sample the owner does not need processed (silent): the clocks move on. */
    void skip() noexcept;

private:
    struct Line
    {
        std::vector<float> buffer;
        int write = 0, mask = 0;
        void allocate (int samples);
        void clear() noexcept;
        void push (float x) noexcept { buffer[static_cast<std::size_t> (write)] = x; write = (write + 1) & mask; }
        float read (double delay) const noexcept;
    };
    struct Svf
    {
        float ic1 = 0.0f, ic2 = 0.0f;
        void process (float x, float g, float k, float& low, float& high) noexcept;
    };
    struct Channel
    {
        Line line;
        Svf lowCut, highCut, highCut2, antiAlias, antiAlias2;
        float bump = 0.0f;
        float last = 0.0f;
    };

    float shape (Channel& c, float x) noexcept;
    double advanceModulation() noexcept;
    void updateCoefficients() noexcept;

    double sampleRate = 48000.0;
    Settings settings;
    double bpm = 120.0;
    std::array<Channel, 2> channels;
    double delaySamples = 18000.0, targetSamples = 18000.0, glide = 0.0001;
    double feedback = 0.45;
    // Modulation
    Prng rng { 0x4543484full };
    double wowPhase = 0.0, flutterPhase = 0.0, chorusPhase = 0.0;
    float wander = 0.0f, wanderFrom = 0.0f, wanderTo = 0.0f;
    int wanderCountdown = 0;
    static constexpr int wanderBlock = 256;
    // Filters (coefficients at control rate)
    int countdown = 0;
    float gLowCut = 0.01f, gHighCut = 0.3f, gAlias = 0.3f, bumpCoef = 0.01f, drive = 1.0f;
    int quietRun = 0, sleepAfter = 1;
};

} // namespace osp
