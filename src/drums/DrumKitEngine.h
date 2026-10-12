#pragma once

#include "drums/DrumLayers.h"
#include "drums/DrumMapping.h"
#include "engine/InstrumentEngine.h"
#include "engine/PostProcessor.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

/**
    ANDOR Drums' instrument engine (pure C++, no JUCE).

    Every pad is a complete OSP InstrumentEngine (layer A; the voices, LIFE, DYNAMICS,
    CHARACTER, REIMAGINED, EQ and the round robins of a set are OSP's own, unchanged) with
    its post stage switched off. The pads are mixed (pad gain, pan, mute/solo) and then share
    ONE PostProcessor (DRIVE, MOVEMENT, ECHO, SPACE) and the kit volume, so the effects cost
    the same as in OSP however many pads play. A note reaches pads only through the key map
    (DrumMapping.h); a pad never hears another pad's notes.

    Threading as InstrumentEngine: prepare() allocates (message thread); everything else is
    real-time safe and called from one thread (the audio thread in the plugin).
*/
namespace osp::drums
{

enum class DrumCategory
{
    kick, snare, clap, closedHat, openHat, tom, rim, cymbal, percussion, unknown
};
const char* toString (DrumCategory category) noexcept;
DrumCategory categoryFromString (const std::string& text) noexcept;

/** How a pad answers repeated notes. */
enum class VoiceMode
{
    poly,           ///< hits overlap (each rings out)
    monoRetrigger   ///< a new hit cuts the previous one with a short fade
};

/** What note-off does. */
enum class TriggerMode
{
    oneShot,   ///< note-off is ignored: the sample plays to its end
    gate       ///< note-off releases (the envelope's release)
};

const char* toString (VoiceMode mode) noexcept;
const char* toString (TriggerMode mode) noexcept;

/** A pad's own settings (real-time safe to change; applied to new hits where noted). */
struct PadSettings
{
    DrumCategory category = DrumCategory::unknown;
    double gainDb = 0.0;            ///< -60 (silent) .. +12
    double pan = 0.0;               ///< -1 .. 1, a balance
    double tuneSemitones = 0.0;     ///< on top of the key's transposition (-24 .. 24)
    bool mute = false;
    bool solo = false;
    VoiceMode voiceMode = VoiceMode::poly;
    TriggerMode trigger = TriggerMode::oneShot;
    int chokeGroup = 0;             ///< 0 = none, 1..8: a hit fades the group's other pads
    int polyphony = 12;             ///< voices for this pad (1..24: half its voice slots)
    AdsrSettings adsr { 0.0005, 0.0, 1.0, 0.12 };
    /** The pad's own LIFE, DYNAMICS, CHARACTER and REIMAGINED; below 0 = the kit's. */
    double life = -1.0, dynamics = -1.0, character = -1.0, reimagined = -1.0;
    double start = 0.0;             ///< START 0..1 (new hits)
    bool reverse = false;           ///< new hits

    static PadSettings defaultsFor (DrumCategory category);
};

/** Kit-wide settings: the shared effects, the kit's default macro values and the volume. */
struct KitSettings
{
    Macros macros;                  ///< SPACE, ECHO, DRIVE, MOTION are kit-wide; the rest are the pads' default
    Shaping shaping;
    double volumeDb = -9.0;         ///< after the shared effects (OSP's output level)
    std::uint64_t seed = 1;
    KeyboardMode keyboardMode = KeyboardMode::pad;
    KeyMap keyMap = KeyMap::defaults();
    /** MACHINE (experimental, docs/drums/m9-machine.md): 0 = the samples as they are,
        about 0.35 = a subtle older-drum-machine character, 1 = a strong transformation.
        Built only from existing OSP processing (spec 9.4). */
    double machine = 0.0;

    KitSettings()
    {
        // Drums start dry and restrained: little variation, no reimagining, no wander.
        macros.life = 0.25;
        macros.reimagined = 0.0;
        macros.motion = 0.0;
        macros.space = 0.0;
    }
};

class DrumKitEngine
{
public:
    DrumKitEngine();

    void prepare (double sampleRate, int maximumBlockSize, const KitSettings& kit, const std::array<PadSettings, maxPads>& pads);

    // Sounds (the model or set must stay alive while isModelInUse / isSetInUse say so)
    void setPadModel (int pad, const InstrumentModel* model) noexcept;
    void setPadSet (int pad, const InstrumentSet* set) noexcept;
    /**
        Several sample layers on one pad (DrumLayers.h): models[0 .. plan.count-1] are the
        layers' sounds (each must stay alive while isModelInUse says so). Every layer plays
        at its own natural pitch on the pad's keys. A plan with one layer at its defaults
        plays exactly as setPadModel. The picker's memory is kept while the count stays.
    */
    void setPadLayers (int pad, const std::array<const InstrumentModel*, maxLayers>& models, const LayerPlan& plan) noexcept;
    const LayerPlan& padLayerPlan (int pad) const noexcept { return plans[index (pad)]; }
    /** The layer the pad's last hit played (round robin / velocity; -1 before any). */
    int lastLayerPlayed (int pad) const noexcept { return pickers[index (pad)].lastPicked(); }
    bool hasSound (int pad) const noexcept;
    bool isModelInUse (const InstrumentModel* model) const noexcept;
    bool isSetInUse (const InstrumentSet* set) const noexcept;
    void killVoicesUsing (const InstrumentModel* model) noexcept;
    void killVoicesUsing (const InstrumentSet* set) noexcept;

    // Settings
    void setPadSettings (int pad, const PadSettings& settings) noexcept;
    const PadSettings& padSettings (int pad) const noexcept { return pads[index (pad)]; }
    void setKitMacros (const Macros& macros) noexcept;
    void setShaping (const Shaping& shaping) noexcept;
    void setVolumeDb (double db) noexcept { kit.volumeDb = db; }
    void setMachine (double amount) noexcept;
    double machine() const noexcept { return kit.machine; }
    /** What the kit's macros and shaping become with MACHINE applied (identical at 0). */
    Macros effectiveMacros() const noexcept;
    Shaping effectiveShaping() const noexcept;
    void setKeyboardMode (KeyboardMode mode) noexcept { kit.keyboardMode = mode; }
    KeyboardMode keyboardMode() const noexcept { return kit.keyboardMode; }
    void setKeyMap (const KeyMap& map) noexcept { kit.keyMap = map; }
    const KeyMap& keyMap() const noexcept { return kit.keyMap; }
    void setSeed (std::uint64_t seed) noexcept;
    void setHostTiming (const HostTiming& timing) noexcept;

    // Notes. A note-off ends exactly what its note-on started, even if the mode or the map
    // changed in between.
    void noteOn (int note, int velocity, int channel = 1) noexcept;
    void noteOff (int note, int channel = 0) noexcept;
    /** Plays a pad at its natural pitch (mouse audition), as if its own key were played. */
    void triggerPad (int pad, int velocity) noexcept;
    /** Plays one layer of a pad alone (layer audition; the round robin is not advanced). */
    void triggerPadLayer (int pad, int layer, int velocity) noexcept;
    void releasePad (int pad) noexcept;
    /** Sustain pedal (CC 64): gate pads keep sounding after note-off until it is released
        (one-shot pads ignore note-offs anyway). */
    void setSustainPedal (bool down) noexcept;
    void allNotesOff() noexcept;
    void reset() noexcept;
    void resetPerformance() noexcept;

    void render (float* const* output, int numChannels, int numSamples) noexcept;

    int activeVoiceCount (int pad) const noexcept;
    int activeVoiceCount() const noexcept;
    /** Display: the pad's voices after the last block, and hits since prepare (lock-free). */
    int padVoicesForDisplay (int pad) const noexcept { return padVoices[index (pad)].load (std::memory_order_relaxed); }
    std::uint32_t padHitsForDisplay (int pad) const noexcept { return padHits[index (pad)].load (std::memory_order_relaxed); }
    bool padAudible (int pad) const noexcept;

    /** For tests and diagnostics: the pad's OSP engine. */
    const InstrumentEngine& padEngine (int pad) const noexcept { return *engines[index (pad)]; }
    static constexpr int engineNoteOffset = 60;
    static constexpr int voiceSlotsPerPad = 48;

private:
    static std::size_t index (int pad) noexcept { return static_cast<std::size_t> (std::clamp (pad, 0, maxPads - 1)); }
    void applyPad (int pad) noexcept;
    void startHit (int pad, int semitones, int velocity, int channel, int sourceNote, int onlyLayer = -1) noexcept;
    Macros padMacros (int pad) const noexcept;

    KitSettings kit;
    std::array<PadSettings, maxPads> pads {};
    std::array<std::unique_ptr<InstrumentEngine>, maxPads> engines;
    std::array<double, maxPads> rootShift {};        ///< model root - its rounded root (exact natural pitch)
    std::array<int, maxPads> baseNote {};            ///< the engine note that plays the sample untransposed
    std::array<std::array<const InstrumentModel*, maxLayers>, maxPads> layerModels {};
    std::array<LayerPlan, maxPads> plans {};
    std::array<LayerPicker, maxPads> pickers {};
    std::array<std::uint64_t, maxPads> layerHits {};  ///< hits since prepare/reset (the picker's randomness)
    PostProcessor post;
    double sampleRate = 48000.0;
    int blockSize = 0;
    std::array<std::vector<float>, 2> scratch, mix;
    std::array<float, maxPads> gainL {}, gainR {};
    bool primed = false;

    /** For every (source note, pad): the engine note its last hit started (-1: none). A
        source note of 128 + pad is a mouse audition. */
    static constexpr int sourceNotes = 128 + maxPads;
    std::array<std::array<std::int16_t, maxPads>, sourceNotes> started {};

    std::array<std::atomic<int>, maxPads> padVoices {};
    std::array<std::atomic<std::uint32_t>, maxPads> padHits {};
};

} // namespace osp::drums
