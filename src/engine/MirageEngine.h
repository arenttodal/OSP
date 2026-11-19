#pragma once

#include "core/Prng.h"
#include "engine/CharacterFilter.h"
#include "engine/PedalFold.h"
#include "engine/ReimaginedEngine.h"

#include <array>

namespace osp
{

/**
    MIRAGE: an early digital sampler voice, heavy rather than playful.

    The recording is held at a reduced rate and resolution, and played by changing the
    sample clock: every stored sample is held until the read moves on (zero-order hold),
    so the clock - and with it the bandwidth, the stair-step grain and the images - follows
    the note. Low notes get slow, dark, gritty texture; high notes skip stored samples and
    fold bright and sharp. Quantisation is a little coarser on soft passages (they expose
    more texture). A sustaining body loops between fixed loop points, as those machines did.

    It all goes into a 4-pole resonant low-pass (the CHARACTER ladder: nonlinear feedback,
    input drive) that follows the key, opens with touch and a short filter envelope, and
    wanders very slightly. CLOCK sets the old-digital intensity, FILTER the filter's
    character (lower and more resonant, more drive), TONE dark or open.

    Amount: 0-25 % a light clock colour; 25-60 % old-digital texture; 60-85 % strong
    pitch-dependent resampling and the filter's identity; 85-100 % a deep grainy voice.
*/
class MirageEngine final : public ReimaginedVoiceEngine
{
public:
    void prepare (double outputRate) noexcept override;
    bool start (const ReimaginedNote& note, const ReimaginedControl& control) noexcept override;
    void control (const ReimaginedControl& control) noexcept override;
    void render (const float* dryL, const float* dryR, float* outL, float* outR, int n) noexcept override;
    double sourcePosition() const noexcept override { return note.granular || done ? -1.0 : fold.position (heads[0]); }
    bool finished() const noexcept override { return done; }
    PedalLoopMode pedalLoopMode() const noexcept override { return fold.configuredLoop(); }
    void pedal (bool down) noexcept override { fold.want (down); }
    bool pedalArmed() const noexcept override { return fold.holding(); }
    bool pedalLooping() const noexcept override { return fold.looping (heads[0]); }

private:
    float boxAt (int channel, std::int64_t k) const noexcept;
    /** The held read at the pedal fold's taps for linear position `pos` (the plain read when
        no pedal loop is engaged). */
    float foldedAt (int channel, double pos) const noexcept;
    float heldAt (int channel, double pos) const noexcept;

    ReimaginedNote note;
    double rate = 48000.0;
    double amount = 0.0;
    Prng rng;
    bool done = false;
    double step = 1.0;

    // Memory and clock
    double period = 1.0;            ///< source frames per stored sample
    float hold = 0.0f;              ///< 0: interpolated, 1: zero-order hold
    float quantStep = 0.0f;
    std::array<double, 2> heads {}; ///< the read and the one it crossfades from (loop points)
    double fade = 1.0, fadeStep = 0.0;
    double loopStart = 0.0, loopEnd = 0.0, soundEnd = 0.0;
    bool loops = false;
    float loopCorrelation = 0.0f;
    double direction = 1.0;
    // Granular: the grains through a clock that follows the note.
    double clockPhase = 1.0;
    double sumL = 0.0, sumR = 0.0;
    int sumCount = 0;
    float heldL = 0.0f, heldR = 0.0f;
    float envelope = 0.0f, envCoef = 0.0f;

    /** PEDAL LOOP (docs/research/pedal-reimagined): the heads' reads loop the recording's pedal
        region while the pedal holds the note (LOOP off; with LOOP on, Ping-Pong). */
    PedalFold fold;

    // Filter
    CharacterFilter filter;
    double filterEnv = 1.0, filterEnvCoef = 0.99, wander = 0.0, wanderPhase = 0.0;
};

} // namespace osp
