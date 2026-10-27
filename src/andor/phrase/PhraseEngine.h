#pragma once

#include "andor/atlas/NoteAtlas.h"
#include "andor/dsp/DspUtil.h"
#include "andor/dsp/SharedFx.h"
#include "andor/dsp/Wavetable.h"
#include "audio/envelopes/Adsr.h"
#include "audio/pitch/SincInterpolator.h"
#include "core/Prng.h"
#include "model/PlaybackSource.h"

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

namespace osp::phrase
{

/** How a held note uses its region (docs/andor-lab/phrase.md "Playback modes"). */
enum class PlayMode { oneShot, gate, sustain };
enum class RoundRobin { sequential, random, shuffle };
/** Fill Keyboard: how far a recorded note may be transposed to play a key without one. */
enum class FillMode { off, natural, balanced, wide, extended };
/** Corrected: every note plays in tune on its key. Original: keeps the performance's cents. */
enum class Tuning { corrected, original };

/** Semitone limit of each Fill mode (0 = exact keys only). */
int fillLimitSemitones (FillMode) noexcept;

/** One playable region, prepared from a note (source frames, at the source's rate). */
struct PlayRegion
{
    int noteIndex = 0;
    int key = 60;                    ///< the key it answers to (NoteEvent::playKey)
    double midiPitch = 60.0;         ///< sounding pitch (fractional)
    double centsOffset = 0.0;
    double confidence = 0.0;
    double attackStart = 0.0, sustainStart = 0.0, sustainEnd = 0.0, releaseStart = 0.0, end = 0.0;
    double fadeIn = 32.0, fadeOut = 32.0;
    bool loopable = false;           ///< SUSTAIN can loop it (a long enough, stable sustain)
    double loopStart = 0.0, loopEnd = 0.0, loopCrossfade = 0.0;
    bool equalPowerLoop = false;     ///< the loop's ends correlate poorly: cross-fade at equal power
    double levelDb = -20.0;          ///< body level, for Level matching
};

/**
    Everything the engine plays, built off the audio thread from an atlas and the decoded audio
    (allocates; loop search takes milliseconds per note). Immutable afterwards; published to the
    audio thread through osp::ModelExchange.
*/
struct PhraseModel
{
    std::uint64_t generation = 0;
    std::shared_ptr<const PlaybackSource> source;
    atlas::NoteAtlas atlas;
    std::vector<PlayRegion> regions;
    std::array<std::vector<int>, 128> keyRegions;   ///< region indices per key, in variation order
    double meanLevelDb = -20.0;

    bool hasNotes() const noexcept { return ! regions.empty(); }
};

/** Padding a PlaybackSource needs for the engine's interpolator. */
int phrasePadding() noexcept;

/**
    Builds the model. `source` must hold the atlas' source audio with phrasePadding(). With
    `previous` (the same source), notes whose range and pitch did not change keep their loop
    points instead of searching again: an edit rebuilds in well under a millisecond per note.
*/
std::shared_ptr<PhraseModel> buildPhraseModel (std::shared_ptr<const PlaybackSource> source, const atlas::NoteAtlas& atlas,
                                               std::uint64_t generation, const PhraseModel* previous = nullptr);

/** The playable state of one key, for the keyboard map. */
enum class KeyState { missing, exact, variations, filled };
struct KeyInfo
{
    KeyState state = KeyState::missing;
    int sourceKey = -1;      ///< the recorded key that plays it
    int variations = 0;
};
KeyInfo describeKey (const PhraseModel& model, int key, FillMode fill) noexcept;

/** Live settings (APVTS values on the audio thread; plain fields in offline renders). */
struct PhraseParams
{
    PlayMode playMode = PlayMode::sustain;
    RoundRobin roundRobin = RoundRobin::shuffle;
    FillMode fill = FillMode::balanced;
    Tuning tuning = Tuning::corrected;
    double pitchSemitones = 0.0;
    double fineCents = 0.0;
    double bendRangeSemitones = 2.0;
    double gainDb = 0.0;
    double velocitySensitivity = 0.6;    ///< 0 = velocity ignored, 1 = full range (40 dB)
    double attackSeconds = 0.002;
    double decaySeconds = 0.3;
    double sustainLevel = 1.0;
    double releaseSeconds = 0.25;
    double attackPreservation = 1.0;     ///< 1 keeps the recorded attack, 0 starts at the sustain
    double startOffsetSeconds = 0.0;
    double tone = 1.0;                   ///< low-pass: 0 dark (300 Hz) .. 1 open (20 kHz)
    double synthetic = 0.0;              ///< Natural <-> Synthetic
    double sustainCharacter = 0.0;       ///< SUSTAIN: 0 a clean loop .. 1 a granular bloom of the sustain
    double levelMatch = 0.5;             ///< 0 the recording's own dynamics .. 1 every note at one level
    double space = 0.0;                  ///< shared reverb send (0 = off)
    double echo = 0.0;                   ///< shared delay send (0 = off)
    int polyphony = 16;
    std::uint64_t seed = 0x414e444f52ull; ///< round robin "random" / "shuffle" and grain seeds
};

/**
    The ANDOR Phrase voice engine. Real-time safe after prepare(): a fixed pool of voices, no
    allocation, no locks; the model is read through a const pointer that the owner keeps alive
    (ModelExchange) until no voice uses it. Deterministic: random choices come from
    Prng::deriveSeed (seed, event counter, key); resetEvents() restarts the sequence (transport
    start, offline renders).
*/
class PhraseEngine
{
public:
    static constexpr int maxVoices = 32;

    void prepare (double sampleRate, int maxBlockSize);
    void setModel (const PhraseModel* model) noexcept;
    const PhraseModel* model() const noexcept { return current; }
    void setParams (const PhraseParams& p) noexcept { params = p; }
    const PhraseParams& getParams() const noexcept { return params; }

    void noteOn (int key, float velocity) noexcept;   ///< velocity 0..1
    void noteOff (int key) noexcept;
    void setSustainPedal (bool down) noexcept;
    void setPitchBend (double normalised) noexcept { bend = normalised; }   ///< -1..1
    void allNotesOff() noexcept;                       ///< release everything (CC 123)
    void panic() noexcept;                             ///< silence at once (CC 120, transport jumps)
    void resetEvents() noexcept;                       ///< restart round robins and random draws

    /** Adds `numSamples` of output to left/right (they are cleared first). */
    void render (float* left, float* right, int numSamples) noexcept;

    int activeVoices() const noexcept;
    /** The oldest model generation a voice still reads (for ModelExchange). */
    std::uint64_t oldestGenerationInUse() const noexcept;

    /** What voices are playing, for the display: region index and read position (source frames). */
    struct VoiceView
    {
        int region = -1;
        float position = 0.0f;
        float level = 0.0f;
        int key = -1;
    };
    int collect (VoiceView* out, int max) const noexcept;

private:
    struct Grain
    {
        double position = 0.0;
        int age = 0, length = 0;
        float pan = 0.0f;
        bool active = false;
    };
    static constexpr int grainsPerVoice = 8;

    struct Voice
    {
        bool active = false;
        bool keyDown = false;
        bool pedalHold = false;      ///< key released while the sustain pedal was down
        bool releasing = false;
        bool stealing = false;       ///< fast fade before reuse
        float stealGain = 1.0f;
        int key = -1;
        int region = -1;
        const PhraseModel* model = nullptr;
        std::uint64_t generation = 0;
        std::uint64_t order = 0;     ///< note-on order (steal the oldest)
        double position = 0.0;       ///< source frames
        double start = 0.0;          ///< where it started (fade-in reference)
        double fadeInLength = 32.0;
        bool fadedIn = false;
        double baseIncrement = 1.0;  ///< source frames per output sample without bend
        double transpose = 0.0;      ///< semitones from the region's pitch to the key
        float velocityGain = 1.0f;
        float levelGain = 1.0f;
        bool looping = false;
        bool inTail = false;         ///< SUSTAIN after note-off: playing the recorded release
        double tailCrossfade = 0.0;  ///< 1 -> 0: the loop fading into the release
        double tailFrom = 0.0;       ///< loop position the release cross-fade started from
        Adsr amp;
        andor::Svf tone;
        andor::WavetableOscillator osc;
        float follower = 0.0f;       ///< envelope of the recorded signal (drives the synthetic tone)
        Prng rng { 0 };
        std::array<Grain, grainsPerVoice> grains {};
        int grainCountdown = 0;
        SincInterpolator::Kernel kernel;
        SincInterpolator::LevelCache levelCache;
        float lastLevel = 0.0f;
    };

    int chooseRegion (int key, int& transposeFrom) noexcept;
    Voice* allocateVoice() noexcept;
    void startVoice (Voice& v, int key, float velocity) noexcept;
    void release (Voice& v) noexcept;
    void renderVoice (Voice& v, float* left, float* right, int numSamples) noexcept;
    void readSource (Voice& v, double position, double increment, float& l, float& r) noexcept;
    float grainSustain (Voice& v, const PlayRegion& region, double increment, float& gr) noexcept;

    double sampleRate = 48000.0;
    const PhraseModel* current = nullptr;
    PhraseParams params;
    std::array<Voice, maxVoices> voices {};
    std::uint64_t noteCounter = 0;
    std::uint64_t eventCounter = 0;
    std::array<std::uint32_t, 128> rrCounter {};
    std::array<int, 128> rrLast {};
    bool pedal = false;
    double bend = 0.0;
    SincInterpolator interpolator { 16, 16.0 };
    andor::WavetableBank wavetable;
    andor::SharedFx fx;
};

} // namespace osp::phrase
