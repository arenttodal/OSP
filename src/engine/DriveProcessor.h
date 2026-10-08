#pragma once

#include "engine/Shaping.h"

#include <array>

namespace osp
{

/**
    DRIVE: a shared saturation stage on the mixed instrument (after the layers, CHARACTER's
    per-voice filter and the dynamics, before MOVEMENT, ECHO and SPACE, so the room hears
    the driven sound). Three circuits:

      TUBE    two asymmetric soft stages: the first enriches (a little even harmonic
              warmth), the second joins as DRIVE rises and gives rounded breakup; a coupling
              high-pass between them, bass and treble emphasis around the stages.
      TAPE    a magnetising stage with memory (a soft saturator inside a lagging, mildly
              regenerative loop): smooth, dense, compressed; transients are pushed a little
              harder than the sustain (a linked envelope), and the highs saturate first.
      CRUNCH  a console/preamp overload: an asymmetric preamp, a mid-forward hard-knee
              stage and a soft output limit; tighter bass, rawer and more articulate.

    Common to all: the nonlinear core runs 4x oversampled (2x at 88.2 kHz and above) through
    minimum-phase polyphase IIR half-band filters (no latency to report; ~1 sample of group
    delay), between an exactly inverse pre-/de-emphasis pair (bass less driven to keep chords
    and low notes clean; TONE sets how much the treble is driven, which darkens or opens the
    harmonics without filtering the clean sound), a DC blocker, a gentle amount-scaled
    top-end smoothing and a static output compensation (calibrated on program material, so
    turning DRIVE up adds density, not +10 dB).

    TONE   0 darker, softer harmonics .. 1 more open and present.
    BODY   0 lean: clearer attack, a little of the clean signal kept, less compression ..
           1 dense: more bass into the stages, more second stage / transient rounding.

    At DRIVE 0 the stage is not in the signal path at all (old sessions are bit-identical);
    engaging and releasing it fades over 10 ms; switching circuits crossfades over 50 ms.
    prepare() allocates nothing large and designs the filters; process() is real-time safe
    and deterministic.
*/
class DriveProcessor
{
public:
    struct Settings
    {
        double amount = 0.0;   ///< 0..1 (the DRIVE macro)
        DriveMode mode = DriveMode::tube;
        double tone = 0.5;
        double body = 0.5;

        static Settings from (const Shaping& s, double amount) noexcept;
    };

    /** The level the drive staging is voiced for: program peaks of about -12 dBFS. */
    static constexpr double referenceLevel = 0.25;

    void prepare (double sampleRate);
    void reset() noexcept;
    void setSettings (const Settings& settings) noexcept;

    /** One stereo sample in place. Untouched (exactly) while bypassed. */
    void process (float& left, float& right) noexcept;

    /** True while the stage is in the signal path (DRIVE above 0, or fading out). */
    bool active() const noexcept { return engaged; }
    int oversampling() const noexcept { return factor; }

    /** The steady-state transfer curve of a circuit at these settings (what the popover
        draws): output for an input held at x times referenceLevel, in the same units.
        `push` > 0 shows TAPE's response to a transient (its envelope's extra drive). */
    static double transfer (const Settings& settings, double x, double push = 0.0) noexcept;

    /** The internal half-band design (exposed for tests). Returns the coefficient count. */
    static int designHalfband (double attenuationDb, double transition, float* coefficients, int maxCount) noexcept;

private:
    static constexpr int maxSections = 8;   ///< per polyphase path (16 coefficients)

    /** A cascade of first-order allpass sections at the low rate: one polyphase path. */
    struct AllpassPath
    {
        std::array<float, maxSections> c {}, x1 {}, y1 {};
        int count = 0;
        float process (float x) noexcept;
        void reset() noexcept;
    };
    /** A polyphase IIR half-band: up (one in, two out) or down (two in, one out). */
    struct Halfband
    {
        AllpassPath even, odd;
        void set (const float* coefficients, int count) noexcept;
        void up (float x, float& out0, float& out1) noexcept;
        float down (float in0, float in1) noexcept;
        void reset() noexcept;
    };
    /** A first-order section (shelves, their exact inverses). */
    struct FirstOrder
    {
        float b0 = 1.0f, b1 = 0.0f, a1 = 0.0f, x1 = 0.0f, y1 = 0.0f;
        float process (float x) noexcept
        {
            const float y = b0 * x + b1 * x1 - a1 * y1;
            x1 = x;
            y1 = y;
            return y;
        }
        void reset() noexcept { x1 = y1 = 0.0f; }
    };
    struct Coefficients
    {
        float b0 = 1.0f, b1 = 0.0f, a1 = 0.0f;
    };
    static Coefficients shelf (bool high, double hz, double gainDb, double rate) noexcept;
    static Coefficients inverse (const Coefficients& c) noexcept;

    /** Everything a circuit needs at control rate (from the smoothed settings). */
    struct Voicing
    {
        DriveMode mode = DriveMode::tube;
        float g1 = 1.0f;                 ///< preamp gain into the first stage
        float w2 = 0.0f, g2 = 1.0f;      ///< second stage: its weight and gain
        float bias = 0.0f, biasOffset = 0.0f, biasNorm = 1.0f;
        float biasDrift = 0.0f;          ///< the bias follows the driven level (grid current)
        float levelAttack = 0.0f, levelRelease = 0.0f;
        float feedback = 0.0f, lag = 1.0f, push = 0.0f;   ///< TAPE
        float mid = 0.0f;                ///< CRUNCH's mid emphasis
        float dry = 0.0f;                ///< clean signal kept (BODY low)
        float comp = 1.0f;               ///< output compensation
        float couple = 0.0f;             ///< TUBE's interstage coupling high-pass (coefficient)
        Coefficients preLow, preHigh, postLow, postHigh;
        float smoothMix = 0.0f, smoothCoef = 1.0f;   ///< top-end smoothing: how much, and its one-pole
        float midG = 0.1f, midK = 1.4f;  ///< CRUNCH's band filter (TPT)
        float splitG = 0.01f;            ///< TUBE's bass split (2nd-order TPT high-pass)
        float lowDrive = 0.5f;           ///< TUBE: the bass band's own (gentle, symmetric) drive
    };
    static Voicing voice (DriveMode mode, double amount, double tone, double body, double baseRate, double innerRate) noexcept;

    struct Channel
    {
        FirstOrder preLow, preHigh, postLow, postHigh;
        Halfband up1, up2, down2, down1;
        float couple = 0.0f, magnet = 0.0f, smooth = 0.0f, level = 0.0f;
        float bandIc1 = 0.0f, bandIc2 = 0.0f;
        float splitIc1 = 0.0f, splitIc2 = 0.0f;
        float dcX = 0.0f, dcY = 0.0f;
        void reset() noexcept;
    };
    struct Core
    {
        Voicing voicing;
        std::array<Channel, 2> channels;
        void apply (const Voicing& v) noexcept;
        void reset() noexcept;
    };

    float runCore (Core& core, int channel, float x, float push) noexcept;
    static float shapeInner (const Voicing& v, Channel& c, float u, float push) noexcept;
    void updateControl() noexcept;

    double sampleRate = 48000.0, innerRate = 192000.0;
    int factor = 4;
    std::array<float, 2 * maxSections> stage1 {}, stage2 {};
    int stage1Count = 0, stage2Count = 0;

    Settings target, current;
    std::array<Core, 3> cores;
    int activeCore = 0, fadingCore = -1;
    float modeFade = 1.0f, modeFadeStep = 0.0f;
    bool engaged = false;
    float engage = 0.0f, engageStep = 0.0f;
    int countdown = 0;
    static constexpr int controlInterval = 16;
    double smoothCoef = 0.05;
    float dcCoef = 0.999f;
    // TAPE's linked envelope (stereo-coherent: one detector for both channels)
    float envFast = 0.0f, envSlow = 0.0f, fastAttack = 0.0f, fastRelease = 0.0f, slowAttack = 0.0f, slowRelease = 0.0f;
};

} // namespace osp
