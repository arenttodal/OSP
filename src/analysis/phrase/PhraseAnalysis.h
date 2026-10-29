#pragma once

#include "model/NoteAtlas.h"
#include "core/AudioData.h"

#include <atomic>
#include <memory>
#include <string>
#include <vector>

namespace osp::phrase
{

/**
    ANDOR Phrase analysis (docs/andor-lab/phrase-analysis.md): a monophonic pitched recording
    becomes a Note Atlas in explicit stages. Pure C++, headless, never on the audio thread
    (it allocates and takes time). Deterministic: no randomness at all.

      1 preprocess   mono analysis copy (high-passed), level envelope, silence
      2 pitch        time-varying contour from a pluggable tracker
      3 segment      voicing + pitch change (vibrato-tolerant) + re-articulation dips
      4 describe     robust centre pitch, stability, vibrato, voicing, confidence
      5 refine       attack start, sustain, release, fades
      6 map          root key under the tuning rule, cents kept apart, variation groups
      7 atlas        the shared Note Atlas
*/

/** Pitch backends compared in docs/andor-lab/phrase-analysis.md. */
enum class PitchBackend
{
    yin,          ///< YIN per frame (absolute threshold), octave folding against the local median
    yinViterbi,   ///< pYIN-inspired: several YIN dips per frame weighted over a threshold range,
                  ///< then the most plausible path through them (Viterbi) with a voicing state
};

const char* toString (PitchBackend) noexcept;

/**
    The musician's one detection choice (FEWER / NORMAL / MORE NOTES): how readily a held sound is
    cut into separate notes. It scales the pitch-change and re-articulation rules together.
*/
enum class Detection
{
    fewer,    ///< only clear pitch steps and near-silent re-attacks split a note
    normal,
    more,     ///< shallow re-articulations and smaller pitch moves split too
};

const char* toString (Detection) noexcept;

struct AnalysisSettings
{
    PitchBackend backend = PitchBackend::yinViterbi;
    double minHz = 40.0;               ///< E1 is 41 Hz
    double maxHz = 2200.0;             ///< C7 is 2093 Hz
    double hopSeconds = 0.005;
    double highPassHz = 50.0;           ///< analysis copy only: rumble never moves a boundary
    double silenceDb = -45.0;           ///< below the recording's peak level: silence
    double absoluteFloorDb = -72.0;     ///< dBFS: never louder than this counts as sound
    double minNoteSeconds = 0.05;       ///< detection floor: nothing shorter is ever a note
    double bridgeGapSeconds = 0.03;     ///< voicing dropouts shorter than this do not end a note
    double pitchChangeSemitones = 0.6;  ///< a sustained move this far starts a new note
    double pitchHoldSeconds = 0.04;     ///< ... when it lasts at least this long
    double rearticulationDb = 9.0;      ///< a level dip this far under the sound either side may split a note
    /** A dip that a sharp rise leaves (dB per 10 ms: a re-bow, a re-pluck, a tongued note) splits
        at half `rearticulationDb`. */
    double attackRiseDb = 6.5;
    /** A dip the voicing drops out of (a breath, a consonant, a new bow stroke) splits this deep.
        Shallower voicing gaps are the tone's own noise: tremolo, harmonics, a breathy tone. */
    double unvoicedDipDb = 9.0;
    /** A dip without either (a bow change, a swell, beating between strings) splits only this
        deep; from `rearticulationDb` down to here the note stays whole and is flagged
        `possibleRepeat` at the dip. */
    double softDipDb = 20.0;
    Detection detection = Detection::normal;
    /** MIN NOTE: a piece shorter than this, joined to a neighbour and without a pitch of its own
        (a fragment of the same note, a slide, an octave slip, a weak scrap), becomes part of that
        neighbour. Short notes with their own pitch (fast runs, grace notes) are kept. */
    double minPieceSeconds = 0.25;
    double maxAnalysisSeconds = 180.0;  ///< longer recordings: the first part is analysed (warning)
    double a4Hz = 440.0;
    bool autoTuning = false;            ///< estimate the recording's global offset before rounding

    std::string digest() const;

    /** These settings with `detection` applied to the pitch-change and re-articulation rules. */
    AnalysisSettings effective() const;
};

/** One analysis frame of the contour. */
struct PitchFrame
{
    double hz = 0.0;           ///< 0 when unvoiced
    double midi = 0.0;         ///< fractional MIDI note (0 when unvoiced)
    double confidence = 0.0;   ///< 0..1 (periodicity / path probability)
    float levelDb = -120.0f;   ///< RMS of the analysis copy around the frame, dBFS
    bool voiced = false;
};

struct PitchContour
{
    double hopSeconds = 0.005;
    std::vector<PitchFrame> frames;

    double timeOf (std::size_t frame) const noexcept { return static_cast<double> (frame) * hopSeconds; }
};

/** Stage 1 output. */
struct Preprocessed
{
    std::vector<float> mono;          ///< high-passed analysis copy (playback audio untouched)
    double sampleRate = 0.0;
    std::int64_t analysedFrames = 0;  ///< may be shorter than the file (maxAnalysisSeconds)
    float peakDb = -120.0f;
    std::vector<float> levelDb;       ///< per hop
    float silenceThresholdDb = -120.0f;
    float noiseFloorDb = -120.0f;     ///< quietest tenth of the audible frames
};

Preprocessed preprocess (const AudioData& audio, const AnalysisSettings& settings);

/** Stage 2: a pitch tracker. One instance per thread. */
class PitchTracker
{
public:
    virtual ~PitchTracker() = default;
    virtual const char* name() const noexcept = 0;
    virtual int version() const noexcept = 0;
    /** Returns false when cancelled. `progress` (0..1) may be null. */
    virtual bool track (const Preprocessed& input, const AnalysisSettings& settings, PitchContour& out,
                        const std::atomic<bool>* cancel, std::atomic<float>* progress) = 0;
};

std::unique_ptr<PitchTracker> makePitchTracker (PitchBackend backend);

/** Stage 3 output: a candidate note as a range of contour frames [begin, end). */
struct Segment
{
    std::size_t begin = 0;
    std::size_t end = 0;
    bool legatoIn = false;    ///< joined to the previous segment without a gap
    bool legatoOut = false;
    bool pitchCut = false;    ///< begins where the pitch moved (not at a dip or a gap) (OSP Phase 4B)
    /** Frames of dips inside the segment that may be a re-attack at the same pitch (OSP Phase 4B). */
    std::vector<std::size_t> possibleRepeats;
};

std::vector<Segment> segment (const PitchContour& contour, const Preprocessed& pre, const AnalysisSettings& settings);

/** Stages 4-6 for one segment (neighbours bound the attack and release searches). */
atlas::NoteEvent describe (const Segment& seg, const Segment* previous, const Segment* next, const PitchContour& contour,
                           const Preprocessed& pre, const AnalysisSettings& settings);

struct AnalysisResult
{
    bool ok = false;
    bool cancelled = false;
    std::string error;
    std::vector<std::string> warnings;
    PitchContour contour;
    std::vector<float> levelDb;
    atlas::NoteAtlas atlas;
    double analysisSeconds = 0.0;   ///< wall-clock time of the run (diagnostics only, never stored in the atlas)
};

/**
    The whole pipeline. `source` describes the file (hash, name, paths); its frame count and
    rate are filled from `audio`. Never throws; odd input (silence, noise, tiny files) gives an
    atlas with no notes and a warning, not a failure.
*/
AnalysisResult analysePhrase (const AudioData& audio, const atlas::SourceAsset& source, const AnalysisSettings& settings = {},
                              const std::atomic<bool>* cancel = nullptr, std::atomic<float>* progress = nullptr);

/**
    Re-describes one source range as a single note (split halves, merges, "reanalyse selection"):
    the range is analysed on its own (with a little context either side) and its frames are
    described without segmentation. Positions in the result are source frames. Off the audio
    thread; deterministic.
*/
atlas::NoteEvent analyseRange (const AudioData& audio, std::int64_t startFrame, std::int64_t endFrame, const AnalysisSettings& settings = {});

/** The analysis algorithm's version: bump when results change (it invalidates caches). */
constexpr int analysisVersion = 4;   // 2: scoops/falls/octave slips absorbed, level-relative re-articulation, MIN NOTE
                                     // 3 (OSP Phase 4): per-note doubts, the recording's verdict, quiet alternates left out
                                     // 4 (OSP Phase 4B): a soft dip (bow change, swell, beating) no longer splits; possible repeats flagged
/** A take of a key this much quieter (peak, dB) than the loudest take of the same key and velocity
    layer is left off the keyboard: as a round robin it would sound like a dropout. */
constexpr double quietAlternateDb = 24.0;

} // namespace osp::phrase
