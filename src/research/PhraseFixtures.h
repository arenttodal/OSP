#pragma once

#include "model/NoteAtlas.h"
#include "core/AudioData.h"

#include <string>
#include <vector>

namespace osp::phrase
{

/** A note in a generated fixture: what the analysis should find. */
struct TruthNote
{
    double startSeconds = 0.0;
    double endSeconds = 0.0;
    double midi = 60.0;   ///< the intended (centre) pitch, fractional for out-of-tune notes
};

struct PhraseFixture
{
    std::string name;
    std::string description;
    AudioData audio;
    std::vector<TruthNote> notes;
};

/**
    Generated monophonic phrases with exact ground truth (docs/andor-lab/phrase-analysis.md).
    Deterministic (seeded osp::Prng). The tone is additive with a vowel-like formant filter so
    it behaves like a voice or bowed string rather than a bare sine; each fixture stresses one
    difficulty: vibrato, legato, noise, staccato, repeats, octave ambiguity, breath, quiet notes.
    Frozen once used by tests: add new fixtures instead of changing existing ones.
*/
std::vector<std::string> phraseFixtureNames();
/** A held-out stress set: harder cases the analysis was *not* tuned on, reported as measured
    (docs/andor-lab/phrase-analysis.md). makePhraseFixture() builds these too. */
std::vector<std::string> phraseStressFixtureNames();
PhraseFixture makePhraseFixture (const std::string& name, double sampleRate = 48000.0);

//==============================================================================
/** Note-level evaluation against ground truth (mir_eval-like onset + pitch matching). */
struct EvaluationSettings
{
    double onsetToleranceSeconds = 0.05;
    double pitchToleranceCents = 50.0;
};

struct EvaluationResult
{
    int truthNotes = 0;
    int detectedNotes = 0;
    int matched = 0;            ///< onset within tolerance and pitch within tolerance
    int octaveErrors = 0;       ///< onset matched, pitch off by an octave (+-tolerance)
    int pitchErrors = 0;        ///< onset matched, pitch wrong otherwise
    int falseSplits = 0;        ///< extra detected notes starting inside a truth note
    int missed = 0;             ///< truth notes without any detected onset near them
    int spurious = 0;           ///< detected notes outside every truth note
    double meanOnsetErrorMs = 0.0;
    double meanOffsetErrorMs = 0.0;
    double meanPitchErrorCents = 0.0;
    // OSP Phase 4: is the analysis's own judgement worth trusting? (calibration)
    double medianOnsetErrorMs = 0.0;
    double meanSignedPitchErrorCents = 0.0;   ///< matched notes: + = detected sharp (a tuning bias)
    double correctConfidence = 0.0;           ///< mean confidence of matched notes
    double wrongConfidence = 0.0;             ///< mean confidence of the other detected notes (wrong pitch, splits, spurious)
    int wrongDetected = 0;                    ///< detected notes that are not matches
    int wrongFlagged = 0;                     ///< ... of them carrying a diagnostic (the analysis knew)
    int correctFlagged = 0;                   ///< matched notes carrying a diagnostic (false alarms)
    double precision() const noexcept { return detectedNotes > 0 ? static_cast<double> (matched) / detectedNotes : 0.0; }
    double recall() const noexcept { return truthNotes > 0 ? static_cast<double> (matched) / truthNotes : 0.0; }
    double f1() const noexcept
    {
        const double p = precision(), r = recall();
        return p + r > 0.0 ? 2.0 * p * r / (p + r) : 0.0;
    }
};

/** Evaluates the included notes of `atlas` (attack start = onset, source end = offset). */
EvaluationResult evaluate (const atlas::NoteAtlas& atlas, const std::vector<TruthNote>& truth, const EvaluationSettings& settings = {});

} // namespace osp::phrase
