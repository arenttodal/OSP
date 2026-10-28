#pragma once

#include "core/AudioData.h"
#include "engine/InstrumentBuilder.h"
#include "model/InstrumentSet.h"
#include "model/NoteAtlas.h"

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
        /** Memory limit: at most this much region audio in total (frames at the source rate,
            summed over members); notes beyond it are skipped with a warning. Measured (Phase 3
            report, section 8): 0.57-0.78 MB of member memory per second of stereo 48 kHz region
            audio, and about 0.28 s of building per second on the Linux VM. Default: 5 minutes,
            about 170-235 MB per layer and a minute or two to build. */
        double maxTotalSeconds = 300.0;
        double maxRegionSeconds = 60.0;      ///< a longer region is cut to this (warning)
        std::int64_t edgeFadeFrames = 64;    ///< anti-click fade at each cut when the note's own fade is shorter
    };

    struct BuildReport
    {
        bool ok = false;                     ///< a set was built
        std::vector<std::string> errors;     ///< why not (invalid atlas, missing or mismatched audio)
        std::vector<std::string> warnings;   ///< notes skipped or cut, odd data
        std::vector<std::string> missingSources;   ///< asset ids whose audio was not supplied
        int notesUsed = 0, notesSkipped = 0;
        std::int64_t regionFrames = 0;       ///< summed member frames (source rate)
        std::size_t memberBytes = 0;         ///< approximateBytes() of the built set
        double buildMilliseconds = 0.0;
    };

    struct BuildResult
    {
        std::shared_ptr<const InstrumentSet> set;   ///< null when nothing could be built
        BuildReport report;
    };

    /**
        Builds the set. Fails (no set, `errors` filled) when the atlas is invalid, when any
        source an included note uses has no audio or audio that does not match its record
        (rate, channels, length): a set is never built from part of its sources, so a session
        whose audio is missing cannot look restored.
    */
    BuildResult buildInstrumentSet (const atlas::NoteAtlas& atlas, const std::vector<SourceAudio>& sources, const BuildOptions& options);

    /** Approximate memory the set's audio holds (every member's playback buffers, transients,
        anchors, REIMAGINED data), in bytes. */
    std::size_t approximateBytes (const InstrumentSet& set);

    /** The region a note's member is copied from (its play range, cut to the options' limit). */
    atlas::FrameRange memberRange (const atlas::NoteEvent& note, double sampleRate, const BuildOptions& options);
}

} // namespace osp
