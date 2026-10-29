#pragma once

#include "core/AudioData.h"
#include "engine/InstrumentBuilder.h"
#include "model/InstrumentSet.h"
#include "model/NoteAtlas.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace osp
{

/**
    Note Atlas -> InstrumentSet (unified engine Phase 3; docs/research/unified-andor/phase-3-report.md).

    A recorded phrase becomes an ordinary OSP multi-sample instrument: every included note
    region is copied out of its source asset and becomes one set member, played by OSP's own
    voices (One Shot or Granular, KEY DNA, modulation, A/B/C), exactly like a dropped sample set.
    There is no second sampler.

    - Places come from the atlas, not from inference: one pitch group per play key (the note's
      assigned key, else its root), the note's velocity layer, and its take order within its
      round-robin group.
    - Pitch: a member's root is its play key plus its fine tuning (cents) when the atlas plays in
      tune (`mapping.correctTuning`), else the play key itself (the note sounds as recorded on
      its key).
    - Region copies (the recommended first step, 06-note-atlas-integration.md section 2): each
      member owns the frames attackStart..sourceEnd of its source, with its fades (and a short
      anti-click fade at each cut). Grains, loops and the release stay inside the note by
      construction: a member's buffer holds nothing of its neighbours.
    - One playback gain for the whole atlas (from its loudest region), so loudness differences
      between notes and velocity layers are kept, as for sample sets.
    - Each member still knows its region (SetMember::regionId, sourceAssetId, regionStart/End),
      so a later shared-buffer implementation can read in place.

    Two stages (Phase 4; docs/research/unified-andor/phase-4-report.md). Stage A
    (buildPlayableSet) is playable at once: regions, roots, boundaries, fades, envelopes,
    levels and places, every member a provisional model (no sustain loop, release graft or
    transient layer; a note plays its region). Stage B (enhanceSet) adds that continuation to
    every member and reassembles the same set by the same code, so the finished set is exactly
    the one a single-pass build makes (buildInstrumentSet is A then B). Publish A, then B: a
    note already sounding keeps the model it started with.

    Offline only: allocates, analyses, may take a while. Call it from a worker thread, never the
    audio thread. Never throws.
*/
namespace atlasset
{
    /** A source asset's decoded audio (immutable). Its frames, rate and channels must match the atlas's record. */
    struct SourceAudio
    {
        std::string assetId;
        std::shared_ptr<const AudioData> audio;
    };

    struct BuildOptions
    {
        InstrumentBuildOptions instrument;   ///< as for sample sets (playback level, interpolation, continuation)
        bool withContinuation = true;        ///< sustain loops and release graft per member (as sets)
        /** Phase 4B: the shortest stable region an atlas note sustains from (a sung phrase's
            notes are often short). Sample sets keep `instrument.continuation.minRegionSeconds`
            (0.5 s); a region between the two loops with its minimums scaled to it. */
        double minSustainSeconds = 0.3;
        /** Memory limit: at most this much region audio in total (frames at the source rate,
            summed over members); notes beyond it are skipped with a warning. Measured (Phase 3
            report, section 8): 0.57-0.78 MB of member memory per second of stereo 48 kHz region
            audio, and about 0.28 s of building per second on the Linux VM. Default: 5 minutes,
            about 170-235 MB per layer and a minute or two to build. */
        double maxTotalSeconds = 300.0;
        double maxRegionSeconds = 60.0;      ///< a longer region is cut to this (warning)
        std::int64_t edgeFadeFrames = 64;    ///< anti-click fade at each cut when the note's own fade is shorter
        /** Worker threads for the per-member work (region analysis, Stage B), at most 8. The
            result does not depend on it (members are independent and deterministic). */
        int threads = 1;
        const std::atomic<bool>* cancel = nullptr;   ///< set: stop at the next member (no set, "cancelled")
        std::atomic<float>* progress = nullptr;      ///< 0..1 over the stage (relaxed stores)
    };

    struct BuildReport
    {
        bool ok = false;                     ///< a set was built
        std::vector<std::string> errors;     ///< why not (invalid atlas, missing or mismatched audio)
        std::vector<std::string> warnings;   ///< notes skipped or cut, odd data
        std::vector<std::string> missingSources;   ///< asset ids whose audio was not supplied
        int notesUsed = 0, notesSkipped = 0;
        /** Phase 4: the build is not the whole atlas - notes were skipped (capacity) or cut
            (region limit). Never silent: the note ids are listed and the atlas itself keeps
            every note, so the instrument can be rebuilt once there is room. */
        bool partial = false;
        std::vector<std::string> skippedNoteIds, cutNoteIds;
        bool cancelled = false;
        bool playableOnly = false;           ///< Stage A: members have no continuation yet
        std::int64_t regionFrames = 0;       ///< summed member frames (source rate)
        std::size_t memberBytes = 0;         ///< approximateBytes() of the built set
        double buildMilliseconds = 0.0;
        /** Where the time went (Phase 4 profiling), milliseconds: region copies, per-region
            analysis (Analyzer), the playable model (sources, playback), continuation (loops,
            release, transients, resonances, REIMAGINED data), and finishing the set. */
        double copyMilliseconds = 0.0, analysisMilliseconds = 0.0, provisionalMilliseconds = 0.0,
               continuationMilliseconds = 0.0, finaliseMilliseconds = 0.0;
    };

    struct BuildResult
    {
        std::shared_ptr<const InstrumentSet> set;   ///< null when nothing could be built
        BuildReport report;
    };

    /** What Stage B needs from Stage A: each member's region copy, its analysis and playable
        model, and the places. Immutable; released once Stage B is built. */
    struct PreparedAtlas;

    struct StagedBuild
    {
        BuildResult playable;                          ///< Stage A (report.playableOnly)
        std::shared_ptr<const PreparedAtlas> pending;  ///< null when Stage A failed
    };

    /**
        Builds the set. Fails (no set, `errors` filled) when the atlas is invalid, when any
        source an included note uses has no audio or audio that does not match its record
        (rate, channels, length): a set is never built from part of its sources, so a session
        whose audio is missing cannot look restored.
    */
    BuildResult buildInstrumentSet (const atlas::NoteAtlas& atlas, const std::vector<SourceAudio>& sources, const BuildOptions& options);

    /** Stage A: the playable set and what Stage B needs (same checks and limits as above). */
    StagedBuild buildPlayableSet (const atlas::NoteAtlas& atlas, const std::vector<SourceAudio>& sources, const BuildOptions& options);

    /** Stage B: every member with continuation, the same set otherwise. `options` gives the
        threads, cancel flag and progress (its other fields come from Stage A). */
    BuildResult enhanceSet (const PreparedAtlas& prepared, const BuildOptions& options);

    /** Region audio (seconds at the source rate) an atlas's included notes would load, with the
        options' per-region cut: for a capacity check before building. */
    double regionSeconds (const atlas::NoteAtlas& atlas, const BuildOptions& options);

    /** Approximate memory the set's audio holds (every member's playback buffers, transients,
        anchors, REIMAGINED data), in bytes. */
    std::size_t approximateBytes (const InstrumentSet& set);

    /** The region a note's member is copied from (its play range, cut to the options' limit). */
    atlas::FrameRange memberRange (const atlas::NoteEvent& note, double sampleRate, const BuildOptions& options);
}

} // namespace osp
