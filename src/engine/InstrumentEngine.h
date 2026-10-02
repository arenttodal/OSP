#pragma once

#include "audio/envelopes/Adsr.h"
#include "audio/pitch/SincInterpolator.h"
#include "engine/InstrumentVoice.h"
#include "engine/PerformanceEngine.h"
#include "engine/PostProcessor.h"
#include "model/InstrumentModel.h"
#include "model/InstrumentSet.h"

#include <array>
#include <cstdint>
#include <memory>

namespace osp
{

/** The five musician-facing macros plus the Original <-> Reimagined control (spec §11, §12). 0..1. */
struct Macros
{
    double life = 0.35;
    double dynamics = 0.5;
    double character = 0.5;  ///< 0.5 = neutral
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
    static constexpr int maxPolyphony = 64;

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
    DynamicsMode dynamicsMode = DynamicsMode::full;
    Macros macros;
};

/**
    The OSP instrument engine ("C — current engine" in every listening test). Plays an
    immutable InstrumentModel with continuation, release grafting and per-note
    performance shapes. Baselines A and B stay in BaselineSampler.

    Threading: prepare() allocates (call off the audio thread). setModel(), the note
    functions, runtime setters and render() are real-time safe.
*/
class InstrumentEngine
{
public:
    InstrumentEngine();

    void prepare (double outputSampleRate, int maximumBlockSize, const EngineSettings& settings);

    /** The model must stay alive while any voice may use it (see isModelInUse). */
    void setModel (const InstrumentModel* model) noexcept
    {
        currentModel = model;
        currentSet = nullptr;
    }

    /**
        A multi-sample instrument (Phase 7): notes choose the nearest pitch anchor, the
        velocity layer and a round-robin take. The set (and every member model) must stay
        alive while voices may use it (see isSetInUse).
    */
    void setInstrumentSet (const InstrumentSet* set) noexcept
    {
        currentSet = set;
        currentModel = set != nullptr && set->isValid() ? set->members[static_cast<std::size_t> (set->primary)].model.get() : nullptr;
    }
    bool isSetInUse (const InstrumentSet* set) const noexcept;
    void killVoicesUsing (const InstrumentSet* set) noexcept;
    const InstrumentModel* model() const noexcept { return currentModel; }

    static int requiredSourcePaddingFor (int interpolationZeroCrossings) noexcept
    {
        return 2 * SincInterpolator::maxReachFor (interpolationZeroCrossings) + 4;
    }

    void setEnvelope (const AdsrSettings& adsr) noexcept;
    void setOutputGainDb (double db) noexcept;
    void setPitchOffsetSemitones (double semitones) noexcept;
    void setMacros (const Macros& macros) noexcept
    {
        config.macros = macros;
        post.setMacros (macros);
    }
    void setPitchCharacter (PitchCharacter character) noexcept { config.pitchCharacter = character; }
    void setContinuation (ContinuationStrategy strategy) noexcept { config.continuation = strategy; }
    void setSeed (std::uint64_t seed) noexcept { config.seed = seed; }
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

    /** For tests: the voice slots (read-only). */
    const InstrumentVoice& voiceAt (int index) const noexcept { return voices[static_cast<std::size_t> (index)]; }
    static constexpr int voiceSlots() noexcept { return totalSlots; }

    /** The shape a note would get (pure apart from the performance memory it advances). */
    NoteShape shapeFor (int note, int velocity, std::uint64_t eventIndex) noexcept
    {
        return shapeFor (currentModel, note, velocity, eventIndex, 100.0, 0.0);
    }
    NoteShape shapeFor (const InstrumentModel* model, int note, int velocity, std::uint64_t eventIndex, double referenceVelocity,
                        double registerBrightnessDb) noexcept;

    /** Which member of the current set a note would use (no state change). -1 without a set. */
    int memberFor (int note, int velocity, std::uint64_t eventIndex) const noexcept;

private:
    static constexpr int tailSlots = 16;
    static constexpr int totalSlots = EngineSettings::maxPolyphony + tailSlots;

    InstrumentVoice* findFreeSlot() noexcept;
    InstrumentVoice* chooseVictim() noexcept;
    int countSoundingVoices() const noexcept;

    EngineSettings config;
    double sampleRate = 48000.0;
    std::unique_ptr<SincInterpolator> interpolator;
    std::array<InstrumentVoice, totalSlots> voices;
    const InstrumentModel* currentModel = nullptr;
    const InstrumentSet* currentSet = nullptr;
    std::array<std::int8_t, 512> lastTake {};  ///< last round-robin take per (group, layer)
    bool pedalDown = false;
    std::uint64_t noteCounter = 0;
    float outputGain = 1.0f;
    double pitchRatio = 1.0;
    PerformanceEngine performance;
    PostProcessor post;
    bool mpe = false;
    std::array<double, 17> channelBendRatio {};   ///< index 1..16
    std::array<float, 17> channelPressure {};
    std::array<float, 17> channelTimbre {};
    std::int64_t sampleClock = 0;   ///< samples rendered since prepare/reset (note times for performance memory)
};

} // namespace osp
