#pragma once

#include "model/NoteAtlas.h"
#include "analysis/phrase/PhraseAnalysis.h"
#include "core/AudioData.h"

#include <string>

namespace osp::phrase
{

/**
    Corrections a user makes to an analysed phrase (docs/andor-lab/phrase.md "Editing"). Each
    takes the atlas by reference, changes it, marks what the user changed (atlas::Corrections)
    and keeps it valid (atlas::repair). They return false and leave the atlas unchanged when the
    request makes no sense (unknown note, a split point outside the note, nothing to merge).
    Undo is the caller's: the plugin snapshots the atlas around each call.
*/
namespace edit
{
    enum class Marker { attackStart, sustainStart, sustainEnd, end };

    /** The note *is* this key: an octave or note-name correction. Its cents are kept. */
    bool setRoot (atlas::NoteAtlas&, const std::string& noteId, int rootKey);
    /** Moves one marker (source frames); neighbours keep their order. */
    bool moveMarker (atlas::NoteAtlas&, const std::string& noteId, Marker marker, std::int64_t frame);
    /** Plays the note on another key (-1: back on its own). */
    bool assignKey (atlas::NoteAtlas&, const std::string& noteId, int key);
    bool setIncluded (atlas::NoteAtlas&, const std::string& noteId, bool included);
    bool setGroup (atlas::NoteAtlas&, const std::string& noteId, const std::string& group);

    /** Splits at `frame`: two notes, each re-described from the audio; returns the new id. */
    std::string split (atlas::NoteAtlas&, const std::string& noteId, std::int64_t frame, const AudioData& audio,
                       const AnalysisSettings& settings = {});
    /** Merges a note with the next one in time (same source): one note over both. */
    bool mergeWithNext (atlas::NoteAtlas&, const std::string& noteId, const AudioData& audio, const AnalysisSettings& settings = {});
    /** Re-describes a note from the audio over its current range (pitch, sustain, confidence);
        keeps its id, range, key assignment and inclusion; clears its pitch correction. */
    bool reanalyse (atlas::NoteAtlas&, const std::string& noteId, const AudioData& audio, const AnalysisSettings& settings = {});

    /**
        Combines a fresh analysis with the user's corrections: notes the user touched survive
        unchanged, untouched notes come from `fresh`, and fresh notes overlapping a kept one are
        dropped. Old analysis results never silently replace edits.
    */
    atlas::NoteAtlas mergeReanalysis (const atlas::NoteAtlas& edited, const atlas::NoteAtlas& fresh);

    /** OSP Phase 4: where a re-analysis and the musician's corrections disagree. Nothing in a
        conflict is applied: the corrected note stays as the musician left it, and the conflict
        says what the new analysis found instead, so it can be shown and decided. */
    struct MergeConflict
    {
        enum class Kind
        {
            analysisDisagrees,   ///< a corrected note: the new analysis finds other notes, or another pitch
            noteGone,            ///< a corrected note: the new analysis finds no note there
            uncertainMatch       ///< an untouched note re-detected too differently to keep its id
        };
        Kind kind = Kind::analysisDisagrees;
        std::string noteId;                    ///< the musician's note (kept), or the old id given up
        std::vector<atlas::NoteEvent> found;   ///< what the new analysis found there (not applied)
        std::string message;
    };

    struct MergeResult
    {
        atlas::NoteAtlas atlas;
        std::vector<MergeConflict> conflicts;
        int keptCorrected = 0;   ///< notes the musician touched, kept unchanged
        int keptIds = 0;         ///< re-detected notes that kept their id (same place and key)
        int newIds = 0;          ///< notes new to this analysis (or re-detected too differently)
    };

    /**
        mergeReanalysis with stable ids and a report: an untouched note re-detected at nearly
        the same place (overlap >= 50 % of their union) and key keeps its id; a corrected note
        (any Corrections flag) is kept exactly, and any new note overlapping it is not applied
        but listed in a conflict. Notes left out by the analysis itself (quiet alternates) are
        re-decided; notes the musician left out are corrections.
    */
    MergeResult mergeReanalysisReported (const atlas::NoteAtlas& edited, const atlas::NoteAtlas& fresh);
}

} // namespace osp::phrase
