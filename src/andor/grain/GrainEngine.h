#pragma once

#include "andor/atlas/NoteAtlas.h"
#include "andor/dsp/DspUtil.h"
#include "andor/dsp/SharedFx.h"
#include "andor/dsp/Wavetable.h"
#include "andor/grain/KeyDna.h"
#include "audio/envelopes/Adsr.h"
#include "core/Prng.h"
#include "model/PlaybackSource.h"

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

namespace osp::grain
{

//==============================================================================
/** A region grains may be taken from, in source frames. */
struct GrainRegion
{
    double start = 0.0, end = 0.0;
    double rootMidi = 60.0;      ///< the pitch the region sounds at untransposed
    double confidence = 1.0;
    int key = 60;                ///< the key it was mapped to (atlas notes)
    int noteIndex = -1;          ///< atlas note, -1 for the whole source
};

/**
    What the engine plays, built off the audio thread (allocates), immutable afterwards and
    published through osp::ModelExchange. One shared read-only source: no voice or key copies
    the audio (spec 15.1).
*/
struct GrainModel
{
    std::uint64_t generation = 0;
    std::shared_ptr<const PlaybackSource> source;   ///< null: oscillator only
    GrainRegion whole;                              ///< the whole source at its root
    std::vector<GrainRegion> regions;               ///< from a Note Atlas (may be empty)
    std::array<std::vector<int>, 128> keyRegions;   ///< atlas notes per key (variation order)
    std::shared_ptr<const atlas::NoteAtlas> atlas;  ///< for the editor and the session

    bool hasSource() const noexcept { return source != nullptr && source->isValid(); }
    bool hasAtlas() const noexcept { return ! regions.empty(); }
};

int grainPadding() noexcept;

/** A plain sample: one region over the whole file at `rootMidi`. */
std::shared_ptr<GrainModel> buildGrainModel (std::shared_ptr<const PlaybackSource> source, double rootMidi, std::uint64_t generation);
/** A Note Atlas: every included note's grain range (sustain unless set) at its own pitch. */
std::shared_ptr<GrainModel> buildGrainModel (std::shared_ptr<const PlaybackSource> source, std::shared_ptr<const atlas::NoteAtlas> atlas,
                                             double fallbackRootMidi, std::uint64_t generation);

//==============================================================================
/** Which atlas region a played key takes its grains from (spec 21.2). */
enum class SourcePolicy
{
    mapped,       ///< the note on that key (variations in turn), else the nearest recorded key
    closest,      ///< the note whose pitch is nearest to the key
    fixedNote,    ///< one chosen note for every key
    wholeSource,  ///< the whole file as one source across all keys
};

enum class GrainWindow { hann, soft, percussive };
enum class Quality { eco, standard, high };

struct QualityLimits
{
    int voices = 12;
    int grainBudget = 192;   ///< all voices together
};
QualityLimits limitsFor (Quality) noexcept;

//==============================================================================
/** Modulation sources and destinations (spec 18). */
enum class ModSource { none, velocity, note, modWheel, ampEnv, auxEnv, lfo1, lfo2, macro1, macro2 };
enum class ModDest { none, size, density, position, positionScatter, pitchScatter, grainLevel, oscMix, cutoff, spread };
constexpr int numModSources = 10;
constexpr int numModDests = 10;
const char* modSourceName (ModSource) noexcept;
const char* modDestName (ModDest) noexcept;

struct ModSlot
{
    ModSource source = ModSource::none;
    ModDest dest = ModDest::none;
    double amount = 0.0;   ///< -1..1 of the destination's normalised travel
};
constexpr int numModSlots = 8;

//==============================================================================
/**
    Live settings. Granular values are given in their own units and converted to the normalised
    0..1 domain for the precedence chain (spec 18):
        base -> + KEY DNA offset -> + per-trigger variation -> + modulation -> clamp/convert
        -> smoothing -> DSP
*/
struct GrainParams
{
    // Granular
    double sizeSeconds = 0.12;        ///< 5 ms .. 1 s
    double density = 18.0;            ///< grains per second, 1 .. 200
    double position = 0.35;           ///< 0..1 across the region
    double positionScatter = 0.15;    ///< 0..1
    double pitchSemitones = 0.0;      ///< grain transposition, -24..24
    double pitchScatter = 0.0;        ///< semitones, 0..12
    double spread = 0.35;             ///< stereo spread 0..1
    double reverseProbability = 0.0;  ///< 0..1
    double timingJitter = 0.25;       ///< temporal randomness 0..1
    GrainWindow window = GrainWindow::hann;
    double grainLevel = 1.0;          ///< linear, 0..1
    double triggerVariation = 0.1;    ///< per-trigger random offset on the DNA parameters, 0..1

    // Oscillator
    double oscMix = 0.0;              ///< OSC <-> GRAIN: 0 grains only .. 1 oscillator only
    double oscShape = 0.0;            ///< 0 sine .. 3 square (blends)
    double oscTuneSemitones = 0.0;

    // Voice
    double toneHz = 12000.0;          ///< low-pass cutoff 200 .. 20000
    double resonance = 0.2;           ///< 0..1
    double attackSeconds = 0.02, decaySeconds = 0.3, sustainLevel = 0.85, releaseSeconds = 0.6;
    double auxAttack = 0.5, auxDecay = 1.0, auxSustain = 0.0, auxRelease = 1.0;
    double lfo1Hz = 0.3, lfo2Hz = 4.0;
    andor::Lfo::Shape lfo1Shape = andor::Lfo::Shape::sine, lfo2Shape = andor::Lfo::Shape::triangle;
    double velocitySensitivity = 0.5;
    double bendRangeSemitones = 2.0;
    double gainDb = 0.0;
    double macro1 = 0.0, macro2 = 0.0;

    // KEY DNA (amounts; curves and seed live in KeyDnaSettings)
    KeyDnaSettings dna;

    // Sources
    SourcePolicy policy = SourcePolicy::mapped;
    int fixedNoteIndex = 0;
    double wholeRootMidi = -1.0;      ///< >= 0: the whole source's root (the ROOT control); < 0: the model's

    std::array<ModSlot, numModSlots> mod {};

    Quality quality = Quality::standard;
    bool deterministic = true;
    std::uint64_t seed = 0x4752414eull;
    double space = 0.0, echo = 0.0;
};

/** Normalised (0..1) <-> engine units for the DNA parameters. */
double toNormalised (DnaParam p, double value) noexcept;
double fromNormalised (DnaParam p, double normalised) noexcept;

//==============================================================================
/**
    The ANDOR Grain voice engine. Real-time safe after prepare(): fixed voices, a fixed global
    grain pool with a deterministic fair-share budget, precomputed windows, a cached KEY DNA
    table; no allocation, no locks. Reads never leave the chosen region or the padded source.
*/
class GrainEngine
{
public:
    static constexpr int maxVoices = 16;
    static constexpr int poolSize = 512;          ///< hard ceiling; the quality budget is lower
    static constexpr int maxGrainsPerVoice = 160;
    static constexpr int windowSize = 1024;
    static constexpr int controlInterval = 32;    ///< samples between parameter evaluations

    void prepare (double sampleRate, int maxBlockSize);
    void setModel (const GrainModel* model) noexcept { current = model; }
    const GrainModel* model() const noexcept { return current; }
    void setParams (const GrainParams& p) noexcept;
    const GrainParams& getParams() const noexcept { return params; }
    const KeyDnaTable& dnaTable() const noexcept { return dna; }

    void noteOn (int key, float velocity) noexcept;
    void noteOff (int key) noexcept;
    void setSustainPedal (bool down) noexcept;
    void setPitchBend (double normalised) noexcept { bend = normalised; }
    void setModWheel (double value01) noexcept { modWheel = value01; }
    void allNotesOff() noexcept;
    void panic() noexcept;
    void resetEvents() noexcept;   ///< deterministic mode: restart seeded sequences (transport start)

    void render (float* left, float* right, int numSamples) noexcept;

    // Diagnostics (benchmarks, tests, display)
    int activeVoices() const noexcept;
    /** Voices not fading out after a steal: never above the quality's voice limit. */
    int playingVoices() const noexcept;
    int activeGrains() const noexcept { return liveGrains; }
    int peakGrains() const noexcept { return peakLive; }
    std::uint64_t deniedGrains() const noexcept { return denied; }
    int grainBudget() const noexcept { return limits.grainBudget; }
    std::uint64_t oldestGenerationInUse() const noexcept;

    /** A playing grain for the display: position 0..1 of the source, window level, voice. */
    struct GrainView
    {
        float position = 0.0f, level = 0.0f;
        int voice = 0;
    };
    int collectGrains (GrainView* out, int max) const noexcept;
    /** The keys sounding now with their effective (DNA + variation + modulation) values, normalised. */
    struct VoiceView
    {
        int key = -1;
        float level = 0.0f;
        std::array<float, numDnaParams> effective {};
        float regionStart = 0.0f, regionEnd = 1.0f;   ///< 0..1 of the source
    };
    int collectVoices (VoiceView* out, int max) const noexcept;

    /** The per-key profile in engine units (base + DNA, no trigger variation or modulation). */
    double profileValue (int key, DnaParam p) const noexcept;

private:
    struct Grain
    {
        double position = 0.0, increment = 1.0;
        int length = 0, age = 0;
        float gainL = 0.0f, gainR = 0.0f;
        float windowStep = 0.0f, windowPhase = 0.0f;
        bool active = false;
    };

    struct Voice
    {
        bool active = false, keyDown = false, pedalHold = false, releasing = false, stealing = false;
        float stealGain = 1.0f;
        int key = 60;
        float velocity = 1.0f;
        std::uint64_t order = 0;
        const GrainModel* model = nullptr;
        std::uint64_t generation = 0;
        GrainRegion region;
        double baseRatio = 1.0;               ///< source frames per output sample at the key, no bend
        std::array<float, numDnaParams> dnaOffset {};    ///< snapshot at note-on (spec 16)
        std::array<float, numDnaParams> triggerOffset {};
        std::array<double, numDnaParams> effective {};   ///< normalised, after modulation (display)
        andor::Smoother sizeS, densityS, positionS, scatterS, pitchScatterS, spreadS, cutoffS, levelS, oscMixS;
        Adsr amp, aux;
        andor::Lfo lfo1, lfo2;
        andor::Svf filter;
        andor::WavetableOscillator osc;
        Prng rng { 0 };
        double countdown = 0.0;               ///< samples to the next grain
        int controlCountdown = 0;
        std::array<std::int16_t, maxGrainsPerVoice> grains {};
        int grainCount = 0;
        float lastLevel = 0.0f;
    };

    int chooseRegion (int key, GrainRegion& out) noexcept;
    Voice* allocateVoice() noexcept;
    void release (Voice& v) noexcept;
    void control (Voice& v, int samplesSinceLast) noexcept;
    void spawn (Voice& v, int voiceIndex) noexcept;
    void renderVoice (Voice& v, int voiceIndex, float* left, float* right, int numSamples) noexcept;
    void freeGrains (Voice& v) noexcept;
    double modulation (const Voice& v, ModDest dest) const noexcept;

    double sampleRate = 48000.0;
    const GrainModel* current = nullptr;
    GrainParams params;
    KeyDnaSettings dnaSettings;
    KeyDnaTable dna;
    QualityLimits limits;
    std::array<Voice, maxVoices + 4> voices {};   ///< spare slots for steal fades
    std::array<Grain, poolSize> pool {};
    std::array<std::int16_t, poolSize> freeList {};
    int freeCount = 0;
    int liveGrains = 0, peakLive = 0;
    std::uint64_t denied = 0;
    std::uint64_t noteCounter = 0, eventCounter = 0;
    std::array<std::uint32_t, 128> rrCounter {};
    bool pedal = false;
    double bend = 0.0, modWheel = 0.0;
    double bendRatio = 1.0;
    std::array<std::array<float, windowSize + 1>, 3> windows {};
    andor::WavetableBank wavetable;
    andor::SharedFx fx;
};

} // namespace osp::grain
