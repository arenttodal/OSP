#pragma once

#include "audio/envelopes/Adsr.h"
#include "audio/pitch/SincInterpolator.h"
#include "engine/InstrumentVoice.h"
#include "engine/PerformanceEngine.h"
#include "engine/PostProcessor.h"
#include "engine/Shaping.h"
#include "model/InstrumentModel.h"
#include "model/InstrumentSet.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

namespace osp
{

/** The five musician-facing macros plus the Original <-> Reimagined control (spec §11, §12). 0..1. */
struct Macros
{
    double life = 0.5;     // lab, performance-1: realistic (50 %) beat identical retriggers on every instrument
    double dynamics = 0.65;
    double character = 0.9;  ///< CHARACTER: position in the filter range (shaping system v1.0)
    double motion = 0.35;
    double space = 0.2;
    double reimagined = 0.2;
};

/** How velocity acts (Phase 5 experiment; the instrument uses full). */
enum class DynamicsMode
{
    gainOnly,     ///< A: velocity = level
    gainFilter,   ///< B: level + a static brightness tilt (a classic sampler's velocity filter)
    full          ///< C: dynamic performance model (transient, tilt, attack bite, body, pitch, damping)
};

struct EngineSettings
{
    static constexpr int maxPolyphony = 64;   ///< per layer
    static constexpr int layers = 2;          ///< A/B source layers

    int polyphony = 24;
    AdsrSettings adsr { 0.002, 0.0, 1.0, 0.35 };
    double velocityRangeDb = 30.0;      ///< level range across velocity (DYNAMICS = 0 means volume only)
    double outputGainDb = -9.0;
    int interpolationZeroCrossings = 16;
    double stealFadeSeconds = 0.005;
    std::uint64_t seed = 1;

    PitchCharacter pitchCharacter = PitchCharacter::tape;
    ContinuationStrategy continuation = ContinuationStrategy::multiLoopMovement;
    bool releaseGraft = true;
    bool transientPreservation = false;  ///< spec §19; lost the listening test (lab, transients-1), kept as an option
    bool transientMixing = false;        ///< velocity/LIFE move the separated transient, not the whole attack (experiment 8)
    DynamicsMode dynamicsMode = DynamicsMode::full;
    Macros macros;
    Shaping shaping;                     ///< what each macro does (popups); see engine/Shaping.h
    double blend = 0.0;                  ///< A/B: 0 = only A, 1 = only B (equal-power)
    std::array<SourceMode, layers> sourceMode { SourceMode::oneShot, SourceMode::oneShot };
    std::array<GranularParams, layers> granular {};
};

/**
    The OSP instrument engine ("C — current engine" in every listening test). Plays an
    immutable InstrumentModel with continuation, release grafting and per-note
    performance shapes. Baselines A and B stay in BaselineSampler.

    Two source layers (A/B): each holds its own model or set, root shift and source mode
    (One Shot reads the recording through; Granular plays grains around POS). Every note
    starts a voice on each loaded layer; the layers are rendered apart, blended with an
    equal-power crossfade and then share one post stage (Reimagined resonance, MOVEMENT,
    SPACE). The per-voice macro stages (LIFE, DYNAMICS, CHARACTER) use the same settings
    on both layers. With only layer A loaded and the blend at A, output is exactly the
    single-layer engine's.

    Threading: prepare() allocates (call off the audio thread). setModel(), the note
    functions, runtime setters and render() are real-time safe.
*/
class InstrumentEngine
{
public:
    InstrumentEngine();

    void prepare (double outputSampleRate, int maximumBlockSize, const EngineSettings& settings);

    /** The model must stay alive while any voice may use it (see isModelInUse). */
    void setModel (const InstrumentModel* model, int layer = 0) noexcept
    {
        const auto l = layerIndex (layer);
        layerModel[l] = model;
        layerSet[l] = nullptr;
    }

    /**
        A multi-sample instrument (Phase 7): notes choose the nearest pitch anchor, the
        velocity layer and a round-robin take. The set (and every member model) must stay
        alive while voices may use it (see isSetInUse).
    */
    void setInstrumentSet (const InstrumentSet* set, int layer = 0) noexcept
    {
        const auto l = layerIndex (layer);
        layerSet[l] = set;
        layerModel[l] = set != nullptr && set->isValid() ? set->members[static_cast<std::size_t> (set->primary)].model.get() : nullptr;
    }
    bool isSetInUse (const InstrumentSet* set) const noexcept;
    void killVoicesUsing (const InstrumentSet* set) noexcept;
    const InstrumentModel* model (int layer = 0) const noexcept { return layerModel[layerIndex (layer)]; }

    // A/B layers
    /** 0 = only A, 1 = only B; equal-power, smoothed over about 20 ms. */
    void setBlend (double blend) noexcept { config.blend = std::clamp (blend, 0.0, 1.0); }
    /** A layer's own pitch shift (its root correction), on top of setPitchOffsetSemitones. */
    void setLayerPitchOffsetSemitones (int layer, double semitones) noexcept;
    /** The source mode applies to notes started afterwards; granular settings apply live. */
    void setSourceMode (int layer, SourceMode mode) noexcept { config.sourceMode[layerIndex (layer)] = mode; }
    void setGranular (int layer, const GranularParams& params) noexcept
    {
        config.granular[layerIndex (layer)] = params;
        liveGranular[layerIndex (layer)] = params;
    }
    int activeVoiceCount (int layer) const noexcept;

    /**
        What the voices of a layer are doing, for the display (grains, One Shot read heads): written by the audio
        thread after every block (relaxed atomics, no locks), read by the UI whenever it
        likes. A torn read only mixes two consecutive blocks' grains.
    */
    struct GrainSnapshot
    {
        static constexpr int capacity = 128;
        std::array<std::atomic<float>, capacity> position {}, level {}, lane {};
        std::atomic<int> count { 0 };
        // One Shot notes: one read head per playing voice.
        static constexpr int playheadCapacity = 64;
        std::array<std::atomic<float>, playheadCapacity> playheadPosition {}, playheadLevel {};
        std::atomic<int> playheads { 0 };
    };
    const GrainSnapshot& grainSnapshot (int layer) const noexcept { return grainSnapshots[layerIndex (layer)]; }

    static int requiredSourcePaddingFor (int interpolationZeroCrossings) noexcept
    {
        return 2 * SincInterpolator::maxReachFor (interpolationZeroCrossings) + 4;
    }

    void setEnvelope (const AdsrSettings& adsr) noexcept;
    void setOutputGainDb (double db) noexcept;
    void setPitchOffsetSemitones (double semitones) noexcept;
    void setShaping (const Shaping& shaping) noexcept
    {
        config.shaping = shaping;
        liveShaping.shaping = shaping;
        post.setShaping (shaping);
    }
    void setMacros (const Macros& macros) noexcept
    {
        config.macros = macros;
        liveShaping.character = macros.character;
        liveShaping.dynamics = macros.dynamics;
        liveShaping.movement = macros.motion;
        post.setMacros (macros);
    }
    void setPitchCharacter (PitchCharacter character) noexcept { config.pitchCharacter = character; }
    void setContinuation (ContinuationStrategy strategy) noexcept { config.continuation = strategy; }
    void setSeed (std::uint64_t seed) noexcept
    {
        config.seed = seed;
        liveShaping.seed = seed;
    }
    void setVelocityRangeDb (double db) noexcept { config.velocityRangeDb = db; }
    void setDynamicsMode (DynamicsMode mode) noexcept { config.dynamicsMode = mode; }

    /** Velocity -> performance intensity (spec §34). Adds onto `shape`. Pure. */
    static void applyDynamics (NoteShape& shape, int velocity, const DynamicsProfile& profile, const SourceCharacter& character,
                               double dynamicsMacro, DynamicsMode mode, double referenceVelocity = 100.0) noexcept;

    bool isModelInUse (const InstrumentModel* model) const noexcept;
    void killVoicesUsing (const InstrumentModel* model) noexcept;

    void noteOn (int note, int velocity, int channel = 1) noexcept;
    void noteOff (int note, int channel = 0) noexcept; ///< channel 0 = any channel

    /**
        Expression (spec §50, §51). Without MPE, pitch bend stays global (setPitchOffsetSemitones)
        and pressure / timbre act on every voice. With MPE on, each member channel bends,
        presses and colours only its own notes.
    */
    void setMpe (bool enabled) noexcept { mpe = enabled; }
    bool isMpe() const noexcept { return mpe; }
    void setChannelPitchBend (int channel, double semitones) noexcept;
    void setChannelPressure (int channel, double pressure01) noexcept;
    void setChannelTimbre (int channel, double timbre01) noexcept;
    void setSustainPedal (bool down) noexcept;
    void allNotesOff() noexcept;
    void reset() noexcept;

    /** Restarts performance memory and the note counter (host transport start, bounce). */
    void resetPerformance() noexcept;

    void render (float* const* output, int numChannels, int numSamples) noexcept;

    int activeVoiceCount() const noexcept;
    bool isNoteActive (int note) const noexcept;
    const EngineSettings& settings() const noexcept { return config; }
    std::uint64_t noteOnCount() const noexcept { return noteCounter; }

    /** Level range across velocity: the Advanced range scaled by DYNAMICS (0 % -> 15 % of
        it, the 65 % default -> all of it, 100 % -> about 1.5x). */
    double levelRangeDb() const noexcept { return config.velocityRangeDb * (0.15 + 1.31 * std::clamp (config.macros.dynamics, 0.0, 1.0)); }

    /** For tests: the voice slots (read-only). */
    const InstrumentVoice& voiceAt (int index) const noexcept { return voices[static_cast<std::size_t> (index)]; }
    static constexpr int voiceSlots() noexcept { return totalSlots; }

    /** The shape a note would get (pure apart from the performance memory it advances). */
    NoteShape shapeFor (int note, int velocity, std::uint64_t eventIndex) noexcept
    {
        context = 0;
        return shapeFor (layerModel[0], note, velocity, eventIndex, 100.0, 0.0);
    }
    /** `setMember`: the model is one recording of a set and `referenceVelocity` is the
        velocity at which its own recorded loudness belongs (loudness-anchored), so every
        member plays at the same level for a given velocity. `layered`: its pitch group has
        velocity layers and the set's learned layer differences shape the dynamics (§35). */
    NoteShape shapeFor (const InstrumentModel* model, int note, int velocity, std::uint64_t eventIndex, double referenceVelocity,
                        double registerBrightnessDb, bool setMember = false, bool layered = false) noexcept;

    /** Which member of a layer's set a note would use (no state change). -1 without a set. */
    int memberFor (int note, int velocity, std::uint64_t eventIndex, int layer = 0) const noexcept;

private:
    static constexpr int tailSlots = 16;
    static constexpr int totalSlots = EngineSettings::layers * EngineSettings::maxPolyphony + tailSlots;

    static std::size_t layerIndex (int layer) noexcept { return static_cast<std::size_t> (std::clamp (layer, 0, EngineSettings::layers - 1)); }
    void noteOnLayer (int layer, int note, int velocity, int channel, std::uint64_t eventIndex) noexcept;
    void granularLife (NoteShape& shape, int note, std::uint64_t eventIndex) const noexcept;
    InstrumentVoice* findFreeSlot() noexcept;
    InstrumentVoice* chooseVictim (int layer) noexcept;
    int countSoundingVoices (int layer) const noexcept;

    EngineSettings config;
    double sampleRate = 48000.0;
    ShapingState liveShaping;            ///< read by every voice at control rate
    std::unique_ptr<SincInterpolator> interpolator;
    std::vector<InstrumentVoice> voices;   ///< totalSlots, allocated once in the constructor (too big for a stack: ~1 MB)
    std::array<const InstrumentModel*, EngineSettings::layers> layerModel {};
    std::array<const InstrumentSet*, EngineSettings::layers> layerSet {};
    std::array<std::array<std::int8_t, 512>, EngineSettings::layers> layerTake {};  ///< last round-robin take per (group, velocity layer)
    std::array<double, EngineSettings::layers> layerPitchRatio { 1.0, 1.0 };
    std::array<GranularParams, EngineSettings::layers> liveGranular {};   ///< read by granular voices
    std::size_t context = 0;   ///< the layer a note-on is being prepared for
    // Per-layer render buffers (A/B blend), allocated in prepare().
    std::array<std::array<std::vector<float>, 2>, EngineSettings::layers> layerBuffer;
    int bufferSize = 0;
    double blendNow = 0.0;
    bool pedalDown = false;
    std::uint64_t noteCounter = 0;
    float outputGain = 1.0f;
    double pitchRatio = 1.0;
    std::array<GrainSnapshot, EngineSettings::layers> grainSnapshots;
    void publishGrains() noexcept;
    std::array<PerformanceEngine, EngineSettings::layers> layerPerformance;   ///< same seed: the layers perform together
    PostProcessor post;
    bool mpe = false;
    std::array<double, 17> channelBendRatio {};   ///< index 1..16
    std::array<float, 17> channelPressure {};
    std::array<float, 17> channelTimbre {};
    std::int64_t sampleClock = 0;   ///< samples rendered since prepare/reset (note times for performance memory)
};

} // namespace osp
