#pragma once

#include "core/AudioData.h"
#include "engine/AtlasSet.h"
#include "engine/KeyDna.h"
#include "midi/MidiEvent.h"
#include "research/RenderConfig.h"
#include "research/RenderSession.h"

#include <juce_core/juce_core.h>

#include <array>
#include <string>
#include <vector>

namespace osp::research::unified
{

/**
    The unified-engine baseline (docs/research/unified-andor, Phase 0): fixed scenes that
    exercise OSP's existing granular and One Shot playback headless, so every later phase
    can prove it changed nothing it was not meant to. Each scene fixes the sources (deterministic
    test signals, no licensed audio), the MIDI, the sample rate, the block size, the seeds and
    every engine setting. The digests of their renders are the baseline
    (tests/regression/golden/unified/), captured from the build before KEY DNA existed.

    Scenes are append-only: a scene's definition never changes once its digest is stored
    (add a new scene instead), like the MIDI fixtures.
*/

/** The scenes' sources: synthetic, reproducible from their name. */
enum class SourceId
{
    vowelA3,   ///< a sung "ah" at A3 (220 Hz), 2.5 s
    pluckE2,   ///< a plucked string at E2, 2.5 s
    vowelE4,   ///< a sung vowel at E4, 2.5 s
    vowelA3Mono,   ///< vowelA3's left channel alone (a mono recording: where PAN is plainly heard)
};
const char* sourceName (SourceId id) noexcept;
AudioData sourceAudio (SourceId id);

struct LayerSpec
{
    SourceId source = SourceId::vowelA3;
    /** Phase 3: a Note Atlas fixture (research/AtlasFixtures: "sung-four", ..., or "regions-<n>"
        for n one-second notes) whose set the layer plays instead of `source`. */
    std::string atlas = {};
};

struct Scene
{
    std::string name;
    std::string description;
    std::vector<LayerSpec> layers;   ///< 1..3: A, B, C (the engine's source mode, granular and layer settings are in config)
    RenderConfig config;
    MidiSequence midi;
    /** Phase 3, publication while rendering: every `swapEverySeconds`, layer A's set is replaced
        (on the render thread, between blocks, as the plugin's exchange hands it over) by this
        atlas's, then back. Sounding notes keep the set they started on. */
    std::string swapAtlas = {};
    double swapEverySeconds = 0.0;
};

/** Every baseline scene, in a fixed order (append only). */
std::vector<Scene> baselineScenes();
/**
    The KEY DNA demonstrations (Phase 1): three patches on one sample, each played as single
    notes, repeated notes, a scale, a four-note and an eight-note chord.
      A static     the existing Granulator, KEY DNA off (must be the original audio);
      B organic    small, smooth per-key differences: one sound across the keyboard;
      C expressive pitch-dependent POS, SIZE and DENS that are clearly heard in a chord.
    Their settings are public so tests and reports read the same profile.
*/
std::vector<Scene> keyDnaScenes();
keydna::Settings organicKeyDna();
keydna::Settings expressiveKeyDna();

/**
    The Phase 2 demonstrations (research-renderer --unified <dir> --set phase2), on the same
    instrument as the KEY DNA ones:
      the grain options   SCATTER, PAN and the SOFT / PERCUSSIVE windows, alone;
      KEY DNA's new ones  SCATTER and PAN by key (low keys steady and centred, high keys
                          scattered and wide);
      live AMOUNT         a held chord whose KEY DNA AMOUNT is automated 0 -> 1 -> 0, and one
                          an LFO sweeps (the keyDnaAmount destination);
      the grain budget    the stress scene unlimited and at 128 grains.
    "p2-options-neutral-chord4" sets every new option to neutral explicitly: it must render
    the Phase 1 "keydna-a-static-chord4" exactly.
*/
std::vector<Scene> phase2Scenes();
/** KEY DNA with SCATTER and PAN curves (the phase-2 scale demonstration). */
keydna::Settings scatterPanKeyDna();
/** Grain-budget stress (--set budget): three dense Granular layers (DENS 40, SIZE 400 ms, KALEIDOSCOPE
    doubling on C) playing an eight-note chord, once per budget: unlimited, 512, 384, 256, 192, 128, 96, 64. */
std::vector<Scene> budgetScenes();

/**
    The Note Atlas scenes (Phase 3, --set atlas): prepared atlases (research/AtlasFixtures, no
    analysis) played through OSP's own voices - One Shot, Granular, KEY DNA, three atlases on
    A/B/C, heavy polyphony, rapid repeats, atlases of 10 and 50 regions, and an atlas replaced
    while its notes sound.
*/
std::vector<Scene> atlasScenes();
/** The sets of the scene's atlas layers (null for a layer playing a source); built once per
    atlas and kept for the process. */
std::array<const InstrumentSet*, 3> setsFor (const Scene& scene);
/** How an atlas fixture's set was built (memory, time, notes), from the cache. */
atlasset::BuildReport atlasReport (const std::string& atlas);

/** What one key does on its own: its profile, and what its grains measurably did. */
struct KeyMeasure
{
    int key = 60;
    double posOffset = 0.0, sizeOctaves = 0.0, densityOctaves = 0.0, spreadOffset = 0.0;   ///< the profile
    double position = 0.5, sizeMs = 0.0, density = 0.0, spread = 0.0;   ///< the layer's settings after the profile
    double meanGrainPosition = -1.0;   ///< where its grains read (0..1 of the recording), from the grain display
    double meanGrains = 0.0;           ///< grains sounding at once on average (expected about DENS x SIZE)
    double rmsDb = -200.0, brightness = 0.0;
};
/** Plays each key alone (0.8 s, LIFE off so only KEY DNA differs) through the scene's first layer and measures it. */
std::vector<KeyMeasure> measureKeys (const Scene& scene, const std::vector<int>& keys);

/** A scene by name (nullptr: none). */
const Scene* findScene (const std::vector<Scene>& scenes, const std::string& name);

/** The engine models for the scene's layers (built once per source and kept for the process). */
std::array<const InstrumentModel*, 3> modelsFor (const Scene& scene);

/** Renders a scene (its own block size unless `blockSizeOverride` > 0). */
RenderOutput render (const Scene& scene, int blockSizeOverride = 0);

/** What is stored per scene: an exact sample hash and robust level/brightness figures. */
struct Digest
{
    std::string hash;           ///< FNV-1a over every float's bits, both channels (exact)
    double seconds = 0.0;
    double rmsDb = -200.0;      ///< over the whole render, both channels
    double peakDb = -200.0;
    double brightness = 0.0;    ///< sqrt (first-difference energy / energy): a cheap spectral tilt
    double stereoWidth = 0.0;   ///< side / mid energy ratio
};
Digest digest (const AudioData& audio);
juce::var digestToJson (const Scene& scene, const Digest& d, const std::string& platform);

/** "linux-x86_64-gcc", "macos-arm64-clang": where a digest was made (exact hashes are only
    compared on the platform that stored them; elsewhere the figures are). */
std::string platformTag();

/** Real-time cost of one scene, measured as a host would drive it (one call per block). */
struct Cost
{
    double renderedSeconds = 0.0;
    double meanBlockMicros = 0.0, p99BlockMicros = 0.0, worstBlockMicros = 0.0;
    double meanBudgetPercent = 0.0;   ///< mean block time / block duration
    double worstBudgetPercent = 0.0;
    int peakVoices = 0;
    int peakGrains = 0;               ///< grains displayed at once, all layers (GrainSnapshot)
    int peakLiveGrains = 0;           ///< grains playing at once, all layers (the engine's own count)
    double worstObservedMicros = 0.0; ///< the slowest block of any run (the figures above keep each block's fastest run)
};
/** Renders the scene `repeats` times and keeps the fastest run per block (less noise). */
Cost measure (const Scene& scene, int repeats = 3);

/** Peak resident memory of this process so far, in MB (getrusage). */
double peakResidentMegabytes();

} // namespace osp::research::unified
