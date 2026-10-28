#pragma once

#include "andor/atlas/NoteAtlas.h"
#include "andor/phrase/PhraseAnalysis.h"
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
}

} // namespace osp::phrase
