#pragma once

#include "core/AudioData.h"
#include "model/AnalysisData.h"
#include "model/InstrumentModel.h"
#include "engine/SampleSetInference.h"
#include "model/InstrumentSet.h"
#include "model/NoteAtlas.h"

#include <memory>

#include <cstdint>
#include <string>
#include <vector>

namespace osp::plugin
{

/**
    How a layer's Note Atlas instrument came to be (unified engine Phase 4), for the musician:
    a weak analysis or a partial build is shown as such, never as a perfect instrument.
*/
struct AtlasStatus
{
    bool playableOnly = false;          ///< Stage A: sustain loops and releases are still being prepared
    bool partial = false;               ///< notes left out (capacity) or cut (region limit): listed
    std::vector<std::string> skippedNoteIds, cutNoteIds;
    double regionSeconds = 0.0;         ///< region audio the instrument holds (capacity accounting)
    std::size_t memberBytes = 0;
    double buildMilliseconds = 0.0;

    // From the phrase analysis (a hand-made atlas has none):
    bool analysed = false;
    bool fromCache = false;             ///< the stored analysis of this recording was reused
    bool olderAnalysis = false;         ///< made by an older analysis version: kept (re-analysis is the musician's choice)
    std::string suitability;            ///< "monophonic", "uncertain", "unsuitable", "silent"
    int notes = 0;                      ///< included notes
    int doubtfulNotes = 0;              ///< included notes with any diagnostic
    int leftOut = 0;                    ///< notes kept in the atlas but not on the keyboard
    double analysisMilliseconds = 0.0;
    std::vector<std::string> warnings;
};

/**
    Everything the plugin needs for one dropped sample. Built on the loader thread,
    then immutable. The audio thread only touches `model` (through the engine) and
    `generation` (through ModelExchange). One load publishes up to three of these, one
    per model stage (spec §62); they share audio buffers.
*/
struct LoadedInstrument
{
    std::uint64_t generation = 0;
    std::uint64_t loadId = 0;        ///< the load request this belongs to (stages share it)

    std::string contentHash;     ///< "sha256:<hex>"
    std::string filename;        ///< original file name (metadata only)
    std::string originalPath;    ///< where it was dropped from (metadata, fallback for recall)
    std::string storedPath;      ///< copy in the managed sample store

    AnalysisData analysis;
    double analysisRootMidi = 60.0;  ///< root the PlaybackSource was built with
    std::string rootOrigin;          ///< "analysis", "analysis-low-confidence", "fallback"

    std::shared_ptr<const InstrumentModel> model;   ///< the played model (one file) or the set's primary member

    /** Multi-sample instrument (null for a single file). */
    std::shared_ptr<const InstrumentSet> set;
    struct MemberFile
    {
        std::string contentHash;
        std::string filename;
        std::string originalPath;
    };
    std::vector<MemberFile> memberFiles;           ///< identities of set->members, same order
    std::vector<SetAssignment> assignments;        ///< user corrections the set was built with
    /** A set built from a Note Atlas (Phase 3): the atlas, as loaded (null otherwise; a source
        relinked while loading keeps its new location). The session saves it in place of
        member files. */
    std::shared_ptr<const atlas::NoteAtlas> atlas;
    /** The atlas's source recordings as the sample store holds them (store hash, name, path):
        what presets, the Library and portable instruments reference. */
    std::vector<MemberFile> atlasSources;
    std::shared_ptr<const AtlasStatus> atlasStatus;   ///< Phase 4: stage, capacity, analysis verdict (atlas sets only)
    double startSeconds = 0.0;       ///< where notes start reading (analysed onset minus pre-roll)
    double playbackGainDb = 0.0;     ///< non-destructive level match

    /** Waveform overview: min/max per bucket of the mono mix, its RMS, and a brightness
        descriptor (0 dark .. 1 bright: how fast the signal moves against its level), for the
        display's body and colour. Computed on the loader thread, never while painting. */
    std::vector<float> peakMin;
    std::vector<float> peakMax;
    std::vector<float> peakRms;
    std::vector<float> peakBright;
    std::vector<float> peakFlux;     ///< onset strength 0..1: the rise of the level against the few ms before
    double durationSeconds = 0.0;
    /** Average spectrum, dB re its peak, on log-spaced bins from 20 Hz to 20 kHz (display). */
    static constexpr int spectrumBins = 96;
    std::vector<float> spectrumDb;

    /** Short descriptor line for the UI, e.g. "SUSTAINED · TONAL". */
    std::string character;
};

} // namespace osp::plugin
