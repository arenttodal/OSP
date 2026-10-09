#pragma once

#include "core/Prng.h"
#include "engine/ReimaginedEngine.h"

#include <array>

namespace osp
{

/**
    TOYBOX: the recording inside a small, clever, slightly strange digital keyboard.

    The sound is held in a primitive memory: sampled at a low internal rate through a
    crude averaging anti-alias filter and stored at a few bits less than CD (DIGITAL: from
    almost transparent to about 7 kHz and 8 bits). A fixed-rate DAC plays it with simple
    (towards drop-sample) interpolation, so transposition aliases the way small samplers
    do; a gentle filter after the DAC leaves some of its sparkle.

    The memory is read by a head that can turn: FWD plays through (looping a sustaining
    body while LOOP is on), TURN swings back and forth over a region that walks slowly through the sound
    (click-free U-turns), CHAOS picks irregular legs, skips and jumps, still on the
    recording's own pitch. The walk drifts the way the note plays (REVERSE: backwards); with
    LOOP on it stays in the body while held, with LOOP off it walks on to the end and stops.
    MOTION sets how often it turns and how much the sound's own
    envelope modulates its playback rate and level (self-modulation instead of an LFO).
    Two memory taps a few tens of ms behind the head recirculate fragments of what was
    just played (bounded: no feedback loop).

    Amount: 0-25 % digital character; 25-50 % memory and loop behaviour; 50-75 % turns and
    self-modulation; 75-100 % a strong alternate identity, still pitched and chord-friendly.
*/
class ToyboxEngine final : public ReimaginedVoiceEngine
{
public:
    void prepare (double outputRate) noexcept override;
    bool start (const ReimaginedNote& note, const ReimaginedControl& control) noexcept override;
    void control (const ReimaginedControl& control) noexcept override;
    double stepFactor() const noexcept override { return factor; }
    void render (const float* dryL, const float* dryR, float* outL, float* outR, int n) noexcept override;
    double sourcePosition() const noexcept override { return note.granular || done ? -1.0 : heads[0].pos; }
    double grainPositionOffset() const noexcept override { return grainOffset; }
    bool finished() const noexcept override { return done; }

private:
    struct Head
    {
        double pos = 0.0, dir = 1.0;
    };
    float storedAt (int channel, double pos) const noexcept;
    float boxAt (int channel, std::int64_t k) const noexcept;
    /** A jump elsewhere: an equal-power crossfade from the old head (uncorrelated audio). */
    void turn (double newPos, double newDir, double seconds) noexcept;
    /** A U-turn waits for the next extremum of the waveform under the head and reverses there:
        the value is continuous and its slope is ~0, so it needs no crossfade (a crossfade
        between a forward and a backward read of the same point would cancel). */
    void requestTurn (double legScale) noexcept;
    void reverseNow (bool atExtremum) noexcept;
    void planLeg() noexcept;

    ReimaginedNote note;
    double rate = 48000.0;
    double amount = 0.0;
    Prng rng;
    bool done = false;

    // Memory: stored-sample period (source frames), resolution, interpolation.
    double period = 1.0;
    float levels = 32768.0f, nearest = 0.0f;
    bool quantise = false;

    // Heads: the playing one and the one it crossfades from (turns, jumps).
    std::array<Head, 2> heads;
    double fade = 1.0, fadeStep = 0.0;
    bool turnPending = false;
    int turnWait = 0;
    double pendingLeg = 1.0;
    float lastValue = 0.0f, lastSlope = 0.0f;
    double regionStart = 0.0, regionEnd = 0.0, soundEnd = 0.0, legEnd = 0.0, legLength = 0.0;
    bool sustains = false;   ///< LOOP on and a stable body: the head stays in it while held
    double noteDir = 1.0;    ///< the way the note plays (REVERSE: backwards); its legs are longer
    /** Where legs may reach: the body while it sustains; otherwise on to the sound's end
        (forwards) or back through the attack to the start (REVERSE), where the note ends. */
    double lowBound() const noexcept { return noteDir < 0.0 && ! sustains ? 0.0 : regionStart; }
    double highBound() const noexcept { return sustains ? regionEnd : soundEnd; }
    ToyboxPlay play = ToyboxPlay::forward;
    double motion = 0.0, step = 1.0, factor = 1.0;

    // Granular notes: a head (seconds around POS) the grains follow - the memory looping a
    // leg (FORWARD), turning at its ends (TURN) or jumping within it (CHAOS).
    double grainHead = 0.0, grainDir = 1.0, grainDuration = 1.0, grainOffset = 0.0;
    void moveGrainHead() noexcept;

    // Memory taps: where the head was (one entry per control period).
    static constexpr int historySize = 512;
    std::array<double, historySize> history {};
    int historyWrite = 0, historyCount = 0;
    std::array<int, 2> tapPeriods {};
    std::array<float, 2> tapLevel {};

    // DAC
    double dacPhase = 0.0, dacIncrement = 1.0;
    float heldL = 0.0f, heldR = 0.0f;
    std::array<reimagined::OnePole, 4> post;
    // Granular: the grains through the DAC (averaged between ticks).
    double sumL = 0.0, sumR = 0.0;
    int sumCount = 0;

    // Self-modulation: the sound's own envelope against its slower average.
    float fast = 0.0f, slow = 0.0f, fastCoef = 0.0f, slowCoef = 0.0f;
    double selfMod = 0.0;
    float gainMod = 1.0f;
};

} // namespace osp
