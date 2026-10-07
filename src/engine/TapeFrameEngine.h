#pragma once

#include "core/Prng.h"
#include "engine/ReimaginedEngine.h"

#include <array>

namespace osp
{

/**
    TAPE FRAME: the recording as a finite piece of tape, played by tape speed.

    The tape is prepared once per recording (ReimaginedAnalysis::TapeFrame): the onset,
    then the stable body lengthened by its own matched loops (a fixed splice list: every
    note plays the same tape), or a coherent window of a long recording. A note runs the
    tape at the speed its pitch asks for: low notes play slower, darker and longer, high
    notes quicker and brighter. The tape is FRAME long (SHORT / CLASSIC / LONG, in tape
    seconds at the root); towards the far end it runs out like a tape-replay keyboard
    (lower amounts rewind it to the body instead, quieter each pass).

    Each note is a slightly different pass (seeded by the note's own seed: speed, start,
    wow and flutter phase, tone, gain). AGE narrows the bandwidth (following tape speed),
    saturates, compresses gently and wears the top end; STABILITY sets wow, flutter and
    pass-to-pass variation. High amounts add two faint ghost passes just behind, each a
    little slower or faster and older.

    Amount: 0-25 % a light tape identity; 25-60 % the frame takes over (run-out begins);
    60-85 % mechanics and ghosts; 85-100 % the whole tape instrument.
*/
class TapeFrameEngine final : public ReimaginedVoiceEngine
{
public:
    void prepare (double outputRate) noexcept override;
    bool start (const ReimaginedNote& note, const ReimaginedControl& control) noexcept override;
    void control (const ReimaginedControl& control) noexcept override;
    double stepFactor() const noexcept override { return factor; }
    void render (const float* dryL, const float* dryR, float* outL, float* outR, int n) noexcept override;
    double sourcePosition() const noexcept override;
    bool finished() const noexcept override { return done; }

private:
    struct Head
    {
        double t = 0.0;        ///< tape position (frames)
        std::size_t splice = 0;
        float gain = 0.0f, gainStep = 0.0f;
    };
    void seek (Head& head, double t) const noexcept;
    void readTape (Head& head, double step, bool sinc, float& l, float& r) noexcept;
    double tapeEnd() const noexcept;

    ReimaginedNote note;
    const ReimaginedAnalysis::TapeFrame* tape = nullptr;
    double rate = 48000.0;
    double amount = 0.0;
    double direction = 1.0;
    Prng rng;

    Head main, rewind;
    bool rewinding = false;
    double rewindProgress = 0.0, rewindLength = 1.0;
    double passGain = 1.0;          ///< each rewound pass is quieter (run-out)
    double runout = 0.0;            ///< how far the tape runs out at its end (0: rewinds at full level)
    double frameLength = 0.0, fadeLength = 1.0;
    float level = 1.0f, levelStep = 0.0f;
    bool done = false;

    std::array<Head, 2> ghosts;
    std::array<double, 2> ghostDelay {}, ghostSpeed {};   ///< seconds behind; speed ratio
    std::array<float, 2> ghostLevel {};
    float mainNorm = 1.0f;

    // Mechanics (control rate)
    double factor = 1.0, speedVariation = 1.0;
    double wowPhase = 0.0, wowRate = 0.6, flutterPhase = 0.0, flutterRate = 8.0;
    double wowCents = 0.0, flutterCents = 0.0;
    double wearPhase = 0.0, toneVariation = 0.0;
    float gainVariation = 1.0f;
    double step = 1.0;

    // AGE
    std::array<reimagined::OnePole, 4> lowpass;   ///< two poles per channel
    std::array<reimagined::OnePole, 2> ghostTone;
    float drive = 1.0f, compression = 0.0f, envelope = 0.0f, envAttack = 0.0f, envRelease = 0.0f;
};

} // namespace osp
