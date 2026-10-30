#pragma once

#include "core/AudioData.h"
#include "library/Tagging.h"
#include "model/AnalysisData.h"

#include <optional>
#include <string>
#include <vector>

namespace osp::library
{

/**
    What kind of recording a sound is (Stage 6.5/6.6). Several signals decide, never one
    duration threshold: events separated by silence, onset regularity, the energy envelope,
    pitch stability, sustained energy, spectral flux. Long sustained single sounds are
    one-shots. The user can override every answer (the catalog keeps the override apart), and
    nothing is ever deleted because of a classification.

    Pure and deterministic. Expensive (it runs the analysis): background threads only.
*/
enum class SoundClass
{
    likelyOneShot,
    possibleOneShot,
    multipleEvents,
    likelyLoop,
    longRecording
};

const char* toString (SoundClass) noexcept;          ///< "likely-one-shot", ...
const char* displayName (SoundClass) noexcept;       ///< "Likely One-Shot", ...
std::optional<SoundClass> soundClassFrom (const std::string&) noexcept;

struct SoundFeatures
{
    double durationSeconds = 0.0;
    double peakDbfs = -200.0;
    int onsets = 0;                  ///< energy-rise attacks (the slicer's)
    int events = 0;                  ///< slices: a sound that rises and dies away
    int separatedEvents = 0;         ///< events with silence after them before the next
    double mainEventShare = 0.0;     ///< the loudest event's share of the energy
    double silenceFraction = 0.0;    ///< time spent at the noise floor
    double onsetRegularity = 0.0;    ///< share of onset intervals on one grid (loops), 0..1
    double leadingSilenceSeconds = 0.0;
    double attackSeconds = 0.0;
    double decaySeconds = 0.0;
    double sustainLevelDb = -200.0;
    bool endsWhileSounding = false;
    bool pitched = false;
    double fundamentalHz = 0.0;
    double pitchStabilityCents = 0.0;
    int notes = 0;                   ///< held pitches in a row (a legato phrase has several)
    double flatness = 0.0;
    double centroidHz = 0.0;
    double flux = 0.0;
    bool stereo = false;
};

struct Classification
{
    SoundClass soundClass = SoundClass::possibleOneShot;
    double confidence = 0.0;
    std::string reason;              ///< in words, for the review list
    SoundFeatures features;
    std::string suggestedType;       ///< from the audio (Bass, Keys, Pluck, Pad, Percussion, Texture) or empty
    std::vector<InferredTag> audioTags;   ///< percussive, sustained, tonal, noisy, bright, dark, stereo
    std::optional<double> rootMidi;  ///< a confident pitch
};

/** Classifies a sound; `hints` (its name's evidence) can tip close cases ("loop" in a name). */
Classification classifySound (const AudioData& audio, const NameEvidence* hints = nullptr);

/** Held pitches in a row in an analysis's pitch track (`SoundFeatures::notes`): stretches of at
    least `minHoldSeconds` of confident frames within 40 cents of where they began; a new note
    when the pitch moves more than 90 cents and stays. Vibrato, glides and jumps by an octave or
    a fifth (a tracker on a chord or a rich pad) are not new notes unless `leapsAreNotes` (a
    melody's octaves and fifths). Cheap: one pass over the track an ordinary load already has
    (OSP's "Map notes?" prompt asks with it). */
int heldNotes (const TimeSeries& trackHz, const TimeSeries& trackConfidence, double minHoldSeconds = 0.2, bool leapsAreNotes = false);

/** The features and answer as compact JSON (the catalog's audio_analysis.features). */
std::string toJson (const Classification&);

/** The analysis version written with each answer: a newer one re-analyses. */
constexpr int classifierVersion = 1;

} // namespace osp::library
