#pragma once

#include "audio/envelopes/Adsr.h"
#include "audio/pitch/SincInterpolator.h"
#include "engine/InstrumentVoice.h"
#include "model/InstrumentModel.h"

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
    void setModel (const InstrumentModel* model) noexcept { currentModel = model; }
    const InstrumentModel* model() const noexcept { return currentModel; }

    static int requiredSourcePaddingFor (int interpolationZeroCrossings) noexcept
    {
        return 2 * SincInterpolator::maxReachFor (interpolationZeroCrossings) + 4;
    }

    void setEnvelope (const AdsrSettings& adsr) noexcept;
    void setOutputGainDb (double db) noexcept;
    void setPitchOffsetSemitones (double semitones) noexcept;
    void setMacros (const Macros& macros) noexcept { config.macros = macros; }
    void setPitchCharacter (PitchCharacter character) noexcept { config.pitchCharacter = character; }
    void setContinuation (ContinuationStrategy strategy) noexcept { config.continuation = strategy; }
    void setSeed (std::uint64_t seed) noexcept { config.seed = seed; }
    void setVelocityRangeDb (double db) noexcept { config.velocityRangeDb = db; }

    bool isModelInUse (const InstrumentModel* model) const noexcept;
    void killVoicesUsing (const InstrumentModel* model) noexcept;

    void noteOn (int note, int velocity) noexcept;
    void noteOff (int note) noexcept;
    void setSustainPedal (bool down) noexcept;
    void allNotesOff() noexcept;
    void reset() noexcept;

    void render (float* const* output, int numChannels, int numSamples) noexcept;

    int activeVoiceCount() const noexcept;
    bool isNoteActive (int note) const noexcept;
    const EngineSettings& settings() const noexcept { return config; }
    std::uint64_t noteOnCount() const noexcept { return noteCounter; }

    /** For tests: the voice slots (read-only). */
    const InstrumentVoice& voiceAt (int index) const noexcept { return voices[static_cast<std::size_t> (index)]; }
    static constexpr int voiceSlots() noexcept { return totalSlots; }

    /** The shape a note would get (pure apart from the performance memory it advances). */
    NoteShape shapeFor (int note, int velocity, std::uint64_t eventIndex) noexcept;

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
    bool pedalDown = false;
    std::uint64_t noteCounter = 0;
    float outputGain = 1.0f;
    double pitchRatio = 1.0;
};

} // namespace osp
