#pragma once

#include "audio/envelopes/Adsr.h"
#include "audio/pitch/SincInterpolator.h"
#include "engine/InstrumentVoice.h"
#include "engine/PerformanceEngine.h"
#include "engine/PostProcessor.h"
#include "engine/ReimaginedEngine.h"
#include "engine/ReimaginedModes.h"
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

/**
    A source layer's own controls (adaptive 1-3 layer redesign): START, TUNE, PAN, LEVEL
    and the source modifiers REVERSE, LOOP and FOLLOW. LINK lives in the plugin (it moves
    the other linked layers' controls). Real-time safe to change.
*/
struct LayerSettings
{
    static constexpr double minLevelDb = -48.0;   ///< LEVEL at (or below) this is silence

    double start = 0.0;           ///< START 0..1: One Shot starts this far into the sound; Granular adds it to POS
    double tuneSemitones = 0.0;   ///< TUNE, on top of the root correction (-24..24)
    double pan = 0.0;             ///< PAN -1 (left) .. 1 (right), a balance (centre leaves the layer untouched)
    double levelDb = 0.0;         ///< LEVEL trim before the mix (-48 = silent .. +6)
    bool reverse = false;         ///< REVERSE: One Shot reads backwards, grains read backwards
    bool loop = true;             ///< LOOP (One Shot): sustain by the recording's own loops; off: play it once
    bool follow = true;           ///< FOLLOW: keep the recording's own loudness contour (off: flatten it)
    /** This layer's Original <-> Reimagined (0..1); below 0 it follows the instrument's
        (Macros::reimagined). Per voice it shapes the layer's notes; the shared resonance
        stage gets the layers' amounts weighted by how loud each is in the mix. */
    double reimagined = -1.0;
    /** REIMAGINED mode and every mode's settings. A new mode applies to notes started
        afterwards (sounding notes keep theirs); the settings apply live. */
    ReimaginedSettings reimaginedSettings;
};

/**
    Where Reimagined's bus stage (ReimaginedStage: resonators and wandering formants)
    sits. The per-voice part (continuation, saturation, doubling, grains, drift) follows
    each layer's own amount in both.
*/
enum class ReimaginedRouting
{
    /** One shared stage after the mix, at the layers' power-weighted amount: every session
        made before per-layer routing (they must keep sounding exactly as they did). */
    legacyGlobal,
    /** Each layer its own stage on its own signal, before LEVEL, PAN and the mix: one
        source can stay itself while another is reimagined. New patches. */
    perLayer
};

/** How loud each layer is in the mix (before its LEVEL and PAN). */
struct LayerMixWeights
{
    std::array<double, 3> gain {};
};

struct EngineSettings
{
    static constexpr int maxPolyphony = 64;   ///< per layer
    static constexpr int layers = 3;          ///< source layers A, B, C (adaptive: 1-3 are used)

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
    double blend = 0.0;                  ///< two layers: 0 = only the first, 1 = only the second (equal-power)
    double mixX = 0.5, mixY = 1.0 / 3.0; ///< three layers: position in the A (left) / B (top) / C (right) triangle
    int mixSlots = 0;                    ///< layers that count for the mix law even without a sound (setMixSlots)
    std::array<SourceMode, layers> sourceMode { SourceMode::oneShot, SourceMode::oneShot, SourceMode::oneShot };
    std::array<GranularParams, layers> granular {};
    std::array<LayerSettings, layers> layer {};
    /** Mono: one note at a time, newest key wins; a key played while another is held
        changes the sounding note's pitch (legato, no restart), sliding over glideSeconds. */
    bool mono = false;
    double glideSeconds = 0.0;
    ReimaginedRouting reimaginedRouting = ReimaginedRouting::legacyGlobal;
};

/**
    The OSP instrument engine ("C — current engine" in every listening test). Plays an
    immutable InstrumentModel with continuation, release grafting and per-note
    performance shapes. Baselines A and B stay in BaselineSampler.

    Up to three source layers (A, B, C; adaptive: the instrument grows with the sounds it
    is given). Each holds its own model or set, root shift, source mode (One Shot reads the
    recording through; Granular plays grains around POS) and layer controls (START, TUNE,
    PAN, LEVEL, REVERSE, LOOP, FOLLOW). Every note starts a voice on each loaded layer; the
    layers are rendered apart, weighted by the mix (mixWeights: one layer plays alone, two
    crossfade with the equal-power blend, three by their place in a triangle, constant
    power), trimmed and panned, and then share ONE post stage (Reimagined resonance,
    MOVEMENT, SPACE). The per-voice macro stages (LIFE, DYNAMICS, CHARACTER) use the same
    settings on every layer. With one layer and neutral layer controls the output is
    exactly the single-layer engine's.

    A layer whose mix gain is zero is not rendered: its held notes wait (and continue when
    the layer is faded back in), released ones end, and a layer that was emptied ends all
    its notes once it has faded out.

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
        auto& slot = slots[layerIndex (layer)];
        slot.model = model;
        slot.set = nullptr;
    }

    /**
        A multi-sample instrument (Phase 7): notes choose the nearest pitch anchor, the
        velocity layer and a round-robin take. The set (and every member model) must stay
        alive while voices may use it (see isSetInUse).
    */
    void setInstrumentSet (const InstrumentSet* set, int layer = 0) noexcept
    {
        auto& slot = slots[layerIndex (layer)];
        slot.set = set;
        slot.model = set != nullptr && set->isValid() ? set->members[static_cast<std::size_t> (set->primary)].model.get() : nullptr;
    }
    bool isSetInUse (const InstrumentSet* set) const noexcept;
    void killVoicesUsing (const InstrumentSet* set) noexcept;
    const InstrumentModel* model (int layer = 0) const noexcept { return slots[layerIndex (layer)].model; }
    /** Layers holding a playable sound. */
    bool isLayerOccupied (int layer) const noexcept
    {
        const auto* m = slots[layerIndex (layer)].model;
        return m != nullptr && m->isValid();
    }
    int occupiedLayerCount() const noexcept;

    // Source layers and their mix
    /** Two layers: 0 = only the first, 1 = only the second; equal-power, smoothed over about 20 ms. */
    void setBlend (double blend) noexcept { config.blend = std::clamp (blend, 0.0, 1.0); }
    /** Slots kept for the mix while their sound is missing (the plugin's "clear all samples"):
        the first `count` layers count as present when the mix law is chosen, so a refilled
        layer plays at the share it had (an empty one is silent). 0 = only loaded layers. */
    void setMixSlots (int count) noexcept { config.mixSlots = std::clamp (count, 0, EngineSettings::layers); }
    /** Three layers: the position in the mix triangle (x 0..1 left to right, y 0..1 bottom to top). */
    void setMixPosition (double x, double y) noexcept
    {
        config.mixX = std::clamp (x, 0.0, 1.0);
        config.mixY = std::clamp (y, 0.0, 1.0);
    }
    /**
        The mix gain of every layer (pure): one occupied layer plays at unity; two crossfade
        with the equal-power `blend` (cos/sin); three take their barycentric share of the
        triangle position (A bottom left, B top, C bottom right) as power (sqrt), so the
        total power stays constant wherever the position is. Unoccupied layers get 0.
    */
    static LayerMixWeights mixWeights (const std::array<bool, 3>& occupied, double blend, double x, double y) noexcept;
    /** Barycentric (A, B, C) shares of a triangle position (pure; clamped into the triangle). */
    static std::array<double, 3> triangleShares (double x, double y) noexcept;

    /** A layer's own pitch shift (its root correction), on top of setPitchOffsetSemitones and TUNE. */
    void setLayerPitchOffsetSemitones (int layer, double semitones) noexcept;
    /** START, TUNE, PAN, LEVEL and the modifiers. START, REVERSE and LOOP apply to notes
        started afterwards (Granular: live); TUNE, PAN, LEVEL and FOLLOW apply live. */
    void setLayerSettings (int layer, const LayerSettings& settings) noexcept;
    /** The source mode applies to notes started afterwards; granular settings apply live. */
    void setSourceMode (int layer, SourceMode mode) noexcept { config.sourceMode[layerIndex (layer)] = mode; }
    void setGranular (int layer, const GranularParams& params) noexcept
    {
        config.granular[layerIndex (layer)] = params;
        refreshLiveGranular (layerIndex (layer));
    }
    int activeVoiceCount (int layer) const noexcept;
    /** Played notes (a note sounding on three layers counts once): what a musician calls voices. */
    int musicalVoiceCount() const noexcept;

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
    const GrainSnapshot& grainSnapshot (int layer) const noexcept { return slots[layerIndex (layer)].grains; }

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
        if (config.reimaginedRouting == ReimaginedRouting::perLayer)
            post.setReimagined (0.0);   // the layers' own stages do it
    }
    /** Switching glides: the shared stage fades out (or in) as the layers' stages fade in
        (or out), each over its own smoothing. */
    void setReimaginedRouting (ReimaginedRouting routing) noexcept
    {
        config.reimaginedRouting = routing;
        if (routing == ReimaginedRouting::perLayer)
            post.setReimagined (0.0);
        else
            post.setReimagined (config.macros.reimagined);
    }
    ReimaginedRouting reimaginedRouting() const noexcept { return config.reimaginedRouting; }
    /** A layer's Original <-> Reimagined: its own, or the instrument's. */
    double layerReimagined (int layer) const noexcept
    {
        const double own = config.layer[static_cast<std::size_t> (layerIndex (layer))].reimagined;
        return own < 0.0 ? config.macros.reimagined : own;
    }
    void setPitchCharacter (PitchCharacter character) noexcept { config.pitchCharacter = character; }
    /** The host's musical time at the start of the next block (MOVEMENT's SHAPER syncs to it). */
    void setHostTiming (const HostTiming& timing) noexcept { post.setTiming (timing); }
    /** SHAPER's pattern position (0..1) for the display; -1 when it is not running. */
    float shaperPhase() const noexcept { return post.shaperPhase(); }
    void setContinuation (ContinuationStrategy strategy) noexcept { config.continuation = strategy; }
    void setSeed (std::uint64_t seed) noexcept
    {
        config.seed = seed;
        liveShaping.seed = seed;
    }
    void setVelocityRangeDb (double db) noexcept { config.velocityRangeDb = db; }
    void setDynamicsMode (DynamicsMode mode) noexcept { config.dynamicsMode = mode; }
    /** Poly / mono (legato, last note priority). Switching forgets the held keys; notes
        already sounding finish normally. */
    void setMono (bool mono) noexcept
    {
        if (mono != config.mono)
            clearHeldKeys();
        config.mono = mono;
    }
    bool isMono() const noexcept { return config.mono; }
    void setGlideSeconds (double seconds) noexcept { config.glideSeconds = std::clamp (seconds, 0.0, 10.0); }

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
        return shapeFor (slots[0].model, note, velocity, eventIndex, 100.0, 0.0);
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

    /** Everything one source layer owns at run time (the engine-side EngineSlot). */
    struct Slot
    {
        const InstrumentModel* model = nullptr;
        const InstrumentSet* set = nullptr;
        std::array<std::int8_t, 512> take {};   ///< last round-robin take per (group, velocity layer)
        double rootRatio = 1.0, tuneRatio = 1.0;
        double pitchRatio = 1.0;                ///< root correction x TUNE
        GranularParams liveGranular;            ///< read by granular voices (START, REVERSE, FOLLOW applied)
        std::array<std::vector<float>, 2> buffer;   ///< render buffer, allocated in prepare()
        PerformanceEngine performance;          ///< same seed on every layer: they perform together
        GrainSnapshot grains;
        float gainLeft = 0.0f, gainRight = 0.0f;   ///< mix x LEVEL x PAN applied at the end of the last block
        bool primed = false;                    ///< gains valid (false after prepare/reset: no ramp from 0)
        ReimaginedStage reimagined;             ///< per-layer routing: this layer's own bus stage (KALEIDOSCOPE)
        int reimaginedCountdown = 0;
        ReimaginedLive reimaginedLive;          ///< what this layer's mode-engine voices read (refreshed every block)
    };

    static std::size_t layerIndex (int layer) noexcept { return static_cast<std::size_t> (std::clamp (layer, 0, EngineSettings::layers - 1)); }
    void noteOnLayer (int layer, int note, int velocity, int channel, std::uint64_t eventIndex) noexcept;
    void granularLife (NoteShape& shape, int note, std::uint64_t eventIndex) const noexcept;
    void refreshLiveGranular (std::size_t layer) noexcept;
    InstrumentVoice* findFreeSlot() noexcept;
    void startNote (int note, int velocity, int channel) noexcept;   // every layer's voice of one note
    void monoNoteOn (int note, int velocity, int channel) noexcept;
    void monoNoteOff (int note, int channel) noexcept;
    void releaseNote (int note, int channel) noexcept;
    void clearHeldKeys() noexcept
    {
        heldCount = 0;
        monoNote = -1;
    }
    InstrumentVoice* chooseVictim (int layer) noexcept;
    void resetLayerStages() noexcept;
    /** The layer's live REIMAGINED amount and settings for its voices, and its stage's share. */
    void refreshReimagined (std::size_t layer) noexcept;
    /** The layer's amount as KALEIDOSCOPE uses it (0 while the layer plays another mode). */
    double kaleidoscopeAmount (std::size_t layer) const noexcept
    {
        return config.layer[layer].reimaginedSettings.mode == ReimaginedMode::kaleidoscope
                   ? std::clamp (layerReimagined (static_cast<int> (layer)), 0.0, 1.0) : 0.0;
    }
    void runLayerStage (Slot& slot, float* left, float* right, int numSamples, bool mono) noexcept;
    int countSoundingVoices (int layer) const noexcept;
    std::array<bool, 3> occupiedLayers() const noexcept;

    EngineSettings config;
    double sampleRate = 48000.0;
    ShapingState liveShaping;            ///< read by every voice at control rate
    std::unique_ptr<SincInterpolator> interpolator;
    std::vector<InstrumentVoice> voices;   ///< totalSlots, allocated once in the constructor (too big for a stack: ~1.5 MB)
    std::array<Slot, EngineSettings::layers> slots;
    std::size_t context = 0;   ///< the layer a note-on is being prepared for
    int bufferSize = 0;
    double blendNow = 0.0, mixXNow = 0.5, mixYNow = 1.0 / 3.0;   ///< smoothed mix controls
    bool pedalDown = false;
    // Mono: the keys held, oldest first (the newest sounds), and the sounding note.
    static constexpr int maxHeldKeys = 32;
    std::array<int, maxHeldKeys> heldKeys {};
    int heldCount = 0;
    int monoNote = -1;       ///< the note the mono voices play (-1: none held)
    int lastMonoNote = -1;   ///< the last note played in mono (a fresh note glides from it)
    std::uint64_t noteCounter = 0;
    float outputGain = 1.0f;
    double pitchRatio = 1.0;
    void publishGrains() noexcept;
    PostProcessor post;
    bool mpe = false;
    std::array<double, 17> channelBendRatio {};   ///< index 1..16
    std::array<float, 17> channelPressure {};
    std::array<float, 17> channelTimbre {};
    std::int64_t sampleClock = 0;   ///< samples rendered since prepare/reset (note times for performance memory)
};

} // namespace osp
