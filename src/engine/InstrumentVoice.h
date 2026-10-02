#pragma once

#include "audio/envelopes/Adsr.h"
#include "audio/pitch/SincInterpolator.h"
#include "core/Prng.h"
#include "engine/NoteShape.h"
#include "engine/ShelfFilter.h"
#include "model/ContinuationModel.h"
#include "model/InstrumentModel.h"

#include <array>
#include <cstdint>

namespace osp
{

struct InstrumentVoiceStart
{
    const InstrumentModel* model = nullptr;
    const PitchLayer* layer = nullptr;
    int note = 60;
    int velocity = 100;
    int channel = 1;          ///< MIDI channel (MPE: one note per channel)
    double increment = 1.0;   ///< layer frames per output sample at the note's pitch
    std::uint64_t startOrder = 0;
    NoteShape shape;
    ContinuationStrategy strategy = ContinuationStrategy::multiLoop;
    bool releaseGraft = true;
};

/**
    One voice of the instrument engine: bandlimited playback of a pitch layer that
    continues past the end of the recording by walking its continuation jumps
    (spec §39), grafts the original ending on release (§43), and applies the note's
    performance shape (micro-pitch, brightness, body, transient, damping, drift).

    Real-time safe: all state is inline; render() never allocates or locks.
*/
class InstrumentVoice
{
public:
    void prepare (double outputSampleRate, const AdsrSettings& adsr, const SincInterpolator* interpolator) noexcept;
    void setEnvelopeSettings (const AdsrSettings& adsr) noexcept;

    void start (const InstrumentVoiceStart& params) noexcept;
    void release() noexcept;
    void beginFastFade (int fadeSamples) noexcept;
    void kill() noexcept;

    /** Adds into left/right (right may equal left). pitchRatio: global bend/fine tune. */
    void render (float* left, float* right, int numSamples, double pitchRatio) noexcept;

    bool isActive() const noexcept { return active; }
    bool isReleased() const noexcept { return released; }
    bool isFading() const noexcept { return fadeRemaining > 0; }
    bool isHeldByPedal() const noexcept { return heldByPedal; }
    void setHeldByPedal (bool held) noexcept { heldByPedal = held; }
    int note() const noexcept { return currentNote; }
    int channel() const noexcept { return midiChannel; }

    /** Continuous expression (pressure / MPE timbre): extra level (dB) and brightness (dB). Control rate, smoothed. */
    void setExpression (float gainDb, float brightnessDb) noexcept
    {
        expressionGainDb = gainDb;
        expressionBrightDb = brightnessDb;
    }
    std::uint64_t startOrder() const noexcept { return order; }
    const InstrumentModel* model() const noexcept { return currentModel; }
    float currentLevel() const noexcept { return envelope.level() * baseGain * fadeGain; }

    /** Diagnostics for tests: how many continuation jumps / grafts this note made. */
    int jumpCount() const noexcept { return jumpsTaken; }
    bool hasGrafted() const noexcept { return grafted; }

private:
    static constexpr int controlInterval = 32;
    static constexpr int recentSize = 4;

    void scheduleNextJump() noexcept;
    void scheduleGraft() noexcept;
    void beginCrossfade() noexcept;
    void updateControl() noexcept;
    void readFrame (double pos, double step, float& l, float& r) noexcept;
    float readTransient (const PlaybackSource& src, double pos, double step, int channel) noexcept;

    const SincInterpolator* sinc = nullptr;
    const InstrumentModel* currentModel = nullptr;
    const PitchLayer* layer = nullptr;
    const ContinuationModel* cont = nullptr;
    Adsr envelope;
    AdsrSettings adsrSettings;
    double sampleRate = 48000.0;

    bool active = false;
    bool released = false;
    bool heldByPedal = false;
    int currentNote = -1;
    int midiChannel = 1;
    std::uint64_t order = 0;
    float expressionGainDb = 0.0f, expressionBrightDb = 0.0f;
    double expressionGain = 1.0, expressionBright = 0.0;

    // Reading
    double position = 0.0;
    double baseIncrement = 1.0;
    double currentStep = 1.0;
    double endPosition = 0.0;

    // Continuation
    ContinuationStrategy strategy = ContinuationStrategy::off;
    bool releaseGraftEnabled = true;
    bool hasPending = false;
    ContinuationJump pending;
    int pendingIndex = -1;
    bool pendingIsGraft = false;
    bool crossfading = false;
    double xPosition = 0.0;
    double xProgress = 0.0;
    double xLength = 1.0;
    float xCorrelation = 1.0f;
    bool xIsGraft = false;
    std::array<int, recentSize> recent {};
    int recentWrite = 0;
    bool graftPending = false;
    bool grafted = false;
    bool holdForTail = false;
    int jumpsTaken = 0;
    Prng rng;

    // Shape
    NoteShape shape;
    float baseGain = 1.0f;
    float transientExtra = 0.0f;   // decays to 0
    double attackBright = 0.0;     // dB, decays to 0 (control rate)
    double attackBrightCoef = 1.0;
    float transientCoef = 1.0f;
    float dampingGain = 1.0f;
    float dampingCoef = 1.0f;
    float attackRamp = 1.0f;       // soft-attack fade-in
    float attackRampStep = 0.0f;
    double settleCents = 0.0;
    double settleCoef = 1.0;
    float panLeft = 1.0f, panRight = 1.0f;

    // Drift (control rate)
    double driftLevel = 0.0, driftLevelTarget = 0.0;
    double driftCentsValue = 0.0, driftCentsTarget = 0.0;
    double driftBright = 0.0, driftBrightTarget = 0.0;
    double driftPanValue = 0.0, driftPanTarget = 0.0;
    float saturationDrive = 0.0f, saturationNorm = 1.0f;
    double driftCoef = 0.0;
    int driftCountdown = 0;
    float controlGain = 1.0f, controlGainStep = 0.0f;
    double pitchMod = 1.0;
    int controlCountdown = 0;

    // Transient preservation: the attack's transient read at its own speed (tPosition)
    // and at the body's speed (tBodyPosition, follows the main read until a jump).
    double tPosition = 0.0, tBodyPosition = 0.0, tStep = 1.0;
    int tRemaining = 0;
    float tAmount = 0.0f;

    ShelfFilter highL, highR, lowL, lowR;
    bool filtersActive = false;
    float appliedBright = 0.0f, appliedBody = 0.0f;

    // Fast fade (stealing)
    int fadeRemaining = 0;
    float fadeGain = 1.0f;
    float fadeStep = 0.0f;

    SincInterpolator::Kernel kernel;
};

} // namespace osp
