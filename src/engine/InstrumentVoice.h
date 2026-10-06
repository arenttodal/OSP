#pragma once

#include "audio/envelopes/Adsr.h"
#include "audio/pitch/SincInterpolator.h"
#include "core/Prng.h"
#include "engine/CharacterFilter.h"
#include "engine/GranularSource.h"
#include "engine/NoteShape.h"
#include "engine/Shaping.h"
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
    int layerIndex = 0;                         ///< A/B layer (0 = A, 1 = B)
    SourceMode sourceMode = SourceMode::oneShot;
    const GranularParams* granular = nullptr;   ///< live settings of the layer (granular mode)
    // Source modifiers of the layer (One Shot; Granular takes them from `granular`).
    double startFraction = 0.0;   ///< START: 0..1 of the recording after its onset (from the end when reversed)
    bool reverse = false;         ///< REVERSE: read backwards (LOOP then loops the best loop, mirrored)
    bool follow = true;           ///< FOLLOW off: flatten the recording's loudness contour
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
    /** `shaping`: the engine's live shaping settings and macro positions (read at control rate). */
    void prepare (double outputSampleRate, const AdsrSettings& adsr, const SincInterpolator* interpolator,
                  const ShapingState* shaping = nullptr) noexcept;
    void setEnvelopeSettings (const AdsrSettings& adsr) noexcept;

    void start (const InstrumentVoiceStart& params) noexcept;
    void release() noexcept;
    void beginFastFade (int fadeSamples) noexcept;
    void kill() noexcept;

    /** Adds into left/right (right may equal left). pitchRatio: global bend/fine tune. */
    /** `clockAtStart`: the engine's sample clock at the first sample (shared movement). */
    void render (float* left, float* right, int numSamples, double pitchRatio, std::int64_t clockAtStart = 0) noexcept;

    bool isActive() const noexcept { return active; }
    bool isReleased() const noexcept { return released; }
    bool isFading() const noexcept { return fadeRemaining > 0; }
    bool isHeldByPedal() const noexcept { return heldByPedal; }
    void setHeldByPedal (bool held) noexcept { heldByPedal = held; }
    int note() const noexcept { return currentNote; }
    int layerIndex() const noexcept { return voiceLayer; }
    bool isGranular() const noexcept { return granularMode; }
    int grainCount() const noexcept { return granularMode ? granularSource.grainCount() : 0; }
    /** One Shot voices: where the read head is (0..1 of the recording) and how loud the
        note is now (envelope, 0..1), for the display. False for granular or idle voices. */
    bool playhead (float& where, float& level) const noexcept
    {
        if (! active || granularMode || layer == nullptr || layer->source == nullptr)
            return false;
        const auto frames = static_cast<double> (std::max<std::int64_t> (1, layer->source->numFrames()));
        where = static_cast<float> (std::clamp (position / frames, 0.0, 1.0));
        level = std::clamp (envelope.level() * fadeGain, 0.0f, 1.0f);
        return true;
    }
    /** Granular voices: the playing grains (display), their level scaled by the envelope. */
    int collectGrains (GranularSource::GrainView* out, int max) const noexcept
    {
        if (! active || ! granularMode)
            return 0;
        const int n = granularSource.collect (out, max);
        const float env = std::clamp (envelope.level() * fadeGain, 0.0f, 1.0f);
        for (int i = 0; i < n; ++i)
            out[i].level *= env;
        return n;
    }
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
    double direction = 1.0;          ///< -1: REVERSE
    bool followContour = true;       ///< FOLLOW (false: followGain lifts quiet parts)
    float followGain = 1.0f, followGainStep = 0.0f;
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
    Prng driftRng;
    std::array<double, 5> driftFrom {};   // level, cents, brightness, pan, tone at the start of the glide
    int driftCountdown = 0, driftSegment = 1;
    float controlGain = 1.0f, controlGainStep = 0.0f;
    double pitchMod = 1.0;
    int controlCountdown = 0;

    // Transient preservation: the attack's transient read at its own speed (tPosition);
    // its transposed copy is read wherever the main read is (also after a jump).
    double tPosition = 0.0, tStep = 1.0;
    int tRemaining = 0;
    float tAmount = 0.0f, tMix = 0.0f;
    double tailEndPosition = 0.0; ///< after a graft: where the recording's ending has died away (> 0)

    // Reimagined doubling head
    float dAmount = 0.0f, dRamp = 0.0f, dRampStep = 0.0f, dSide = 1.0f;
    double dBase = 0.0, dDepth = 0.0, dPhase = 0.0, dOmega = 0.0, dEnd = 0.0;
    int dDelaySamples = 0;

    // Reimagined granular continuation: a small pool of windowed grains read from what the
    // note has already played (so they never run ahead of the recording or the attack).
    struct Grain
    {
        double position = 0.0, step = 1.0, phase = 0.0, phaseStep = 0.0;
        float left = 0.0f, right = 0.0f;
        bool active = false;
    };
    static constexpr int maxGrains = 8;
    void spawnGrain() noexcept;
    float readHermite (int channel, double pos) const noexcept;
    std::array<Grain, maxGrains> grains {};
    Prng grainRng;
    float gAmount = 0.0f, gFade = 0.0f, gFadeStep = 0.0f, gNorm = 1.0f, gLowCoef = 1.0f, gLowL = 0.0f, gLowR = 0.0f;
    double gFloor = 0.0, gDensity = 10.0;
    int gCountdown = 0, gDelay = 0;

    // Granular source mode (A/B layers): grains around POS instead of reading through.
    bool granularMode = false;
    GranularSource granularSource;
    const GranularParams* granularLive = nullptr;
    int voiceLayer = 0;

    // CHARACTER: per-voice filter and its AD envelope (control rate)
    const ShapingState* shapingState = nullptr;
    std::int64_t clock = 0;
    CharacterFilter charFilter;
    double charOctaves = 0.0;      ///< smoothed CHARACTER position (octaves re 1 Hz)
    double charSmoothing = 1.0;
    double filterEnv = 0.0, filterEnvStep = 1.0, filterEnvDecay = 0.0;
    bool filterEnvAttacking = true;
    double velocityOctaves = 0.0, envelopeScale = 1.0;
    double driftTone = 0.0, driftToneTarget = 0.0;
    void updateCharacter (bool immediate) noexcept;
    /** The layer's live grain settings with this note's LIFE variation applied. */
    GranularParams notesGranular() const noexcept;

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
