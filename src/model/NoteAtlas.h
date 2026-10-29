#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace osp::atlas
{

/**
    The Note Atlas (docs/research/unified-andor/phase-3-report.md, schema below): a versioned
    description of recorded audio and its musical interpretation - where each note is, which
    pitch it is, how it should be played, what the user changed. Ported from ANDOR Lab's
    Note Atlas (andor-lab eeb5f39, src/andor/atlas), extended for OSP:

    - a source is an immutable audio asset identified by its content (sha256 of the decoded
      or stored bytes), not by a file path. Where it came from (a file import, a Library
      asset, a live capture, a retrospective buffer snapshot) is recorded, and paths are only
      hints for finding a file again. A captured recording and an imported file reach the
      same model once their audio has been finalised into an asset;
    - every note can carry a velocity layer, a variation group and index (round robin), and
      whether it came from analysis or was made by hand.

    Plain data: no JUCE, no GUI, no voice. Positions are frames in the source's own sample rate,
    exact (int64), never rounded UI time. JSON: io/NoteAtlasJson. OSP builds an InstrumentSet
    from it (engine/AtlasSet); playback is OSP's own.
*/
constexpr int currentSchemaVersion = 2;   ///< 1: ANDOR Lab's (read as is); 2: + origin, velocity layers, note origin, mapping

/** Where a source's audio came from (it is an immutable asset either way). */
enum class AssetOrigin
{
    fileImport,      ///< a WAV/AIFF/... file the user imported
    library,         ///< an asset in the Library (managed by content hash)
    liveCapture,     ///< recorded live (future): finalised into an asset before use
    bufferSnapshot,  ///< a retrospective capture buffer snapshot (future)
};

/** One audio asset an atlas refers to. The audio is never embedded in the atlas. */
struct SourceAsset
{
    std::string assetId;            ///< stable id within the atlas ("src0")
    std::string contentHash;        ///< "sha256:<hex>": the asset's identity (required to play)
    AssetOrigin origin = AssetOrigin::fileImport;
    std::string fileName;           ///< display name (a file's name, or a capture's title)
    // Hints for finding the audio again (none is required; the content hash decides):
    std::string relativePath;       ///< relative to the atlas file (portable bundles)
    std::string lastKnownPath;      ///< absolute path when the atlas was written
    std::string managedCopy;        ///< the managed sample store's copy (by hash)
    std::string captureId;          ///< live captures / snapshots: the capture subsystem's id
    double sampleRate = 0.0;
    int numChannels = 0;            ///< channels used (at most 2)
    std::int64_t numFrames = 0;

    double durationSeconds() const noexcept { return sampleRate > 0.0 ? static_cast<double> (numFrames) / sampleRate : 0.0; }
};

/** Half-open frame range [start, end) in source coordinates. */
struct FrameRange
{
    std::int64_t start = 0;
    std::int64_t end = 0;

    std::int64_t length() const noexcept { return end > start ? end - start : 0; }
    bool isEmpty() const noexcept { return end <= start; }
    bool contains (std::int64_t frame) const noexcept { return frame >= start && frame < end; }
    bool operator== (const FrameRange&) const = default;
};

enum class Articulation
{
    sustained,  ///< long, stable: loops well
    short_,     ///< staccato / plucked: plays as recorded
    legato,     ///< joined to a neighbour without a gap
    noisy,      ///< weakly voiced (breath, consonant): pitch unreliable
};

/** Whether a note came from analysis or was made by hand. */
enum class NoteOrigin
{
    analysis,   ///< found by an analysis (`analysisMethod` says which)
    user,       ///< made or placed by the user (or a prepared fixture)
};

/** What an analysis was unsure about in one note (Phase 4). Hints for the musician and a
    later NOTE MAP; playback ignores them. None set: a reliable note. User-made notes have none. */
struct Diagnostics
{
    bool uncertainPitch = false;       ///< weakly periodic, unstable or quiet: the root may be wrong
    bool possibleOctaveError = false;  ///< many of its frames sat about an octave away from its pitch
    bool uncertainBoundary = false;    ///< a join to a neighbour within a semitone (a slide or a dip decided it), or very short
    bool unvoiced = false;             ///< mostly breath, noise or consonant
    bool possibleSplit = false;        ///< joined to a neighbour on the same key: maybe one note cut in two
    bool quietAlternate = false;       ///< far quieter than another take of its key (a decaying tail): left off the keyboard
    bool any() const noexcept { return uncertainPitch || possibleOctaveError || uncertainBoundary || unvoiced || possibleSplit || quietAlternate; }
    bool operator== (const Diagnostics&) const = default;
};

/** What the user changed by hand. A re-analysis must never silently undo these. */
struct Corrections
{
    bool pitch = false;       ///< the root / pitch was set by the user
    bool boundaries = false;  ///< start / end / sustain markers moved by the user
    bool key = false;         ///< reassigned to another key
    bool group = false;       ///< moved to another variation group
    bool split = false;       ///< made by splitting a region
    bool merged = false;      ///< made by merging regions
    bool velocity = false;    ///< velocity layer set by the user
    bool included = false;    ///< included or left out by the user (Phase 4)

    bool any() const noexcept { return pitch || boundaries || key || group || split || merged || velocity || included; }
    bool operator== (const Corrections&) const = default;
};

/**
    One note region. All positions are source frames:
    sourceStart <= attackStart <= sustain.start <= sustain.end <= releaseStart <= sourceEnd.
    attackStart is where playback begins (keeps consonants, bow noise, picks); sustain is the
    pitch-stable body; release is the tail.
*/
struct NoteEvent
{
    std::string id;                    ///< stable within the atlas ("n0007"); survives edits, never reused
    std::string sourceId;              ///< SourceAsset::assetId
    std::int64_t sourceStart = 0;
    std::int64_t sourceEnd = 0;
    std::int64_t attackStart = 0;
    FrameRange sustain;
    std::int64_t releaseStart = 0;
    std::int64_t fadeInFrames = 0;     ///< non-destructive fade at attackStart
    std::int64_t fadeOutFrames = 0;    ///< non-destructive fade before sourceEnd

    double pitchHz = 0.0;              ///< robust central pitch of the voiced body
    double midiPitch = 0.0;            ///< the same as a fractional MIDI note
    int rootMidi = 60;                 ///< the key the note is "at"
    double centsOffset = 0.0;          ///< fine tuning: midiPitch - rootMidi, in cents (signed)
    double pitchStabilityCents = 0.0;
    double vibratoDepthCents = 0.0;
    double vibratoRateHz = 0.0;
    double voicing = 0.0;              ///< 0..1
    double confidence = 0.0;           ///< overall trust in pitch and boundaries, 0..1
    double peakDb = -120.0;
    double rmsDb = -120.0;
    Articulation articulation = Articulation::sustained;

    NoteOrigin origin = NoteOrigin::user;
    std::string analysisMethod;        ///< e.g. "yin-viterbi" (origin analysis)
    int analysisVersion = 0;

    Corrections corrections;
    Diagnostics diagnostics;           ///< analysis notes only (Phase 4)
    bool included = true;              ///< false: kept in the atlas, not playable
    int assignedKey = -1;              ///< -1: plays at rootMidi; else the key it answers to
    int velocityLayer = 0;             ///< 0 = softest; layers of one key are played by velocity
    std::string variationGroup;        ///< round-robin group id; empty = by key
    int variationIndex = 0;            ///< order within its group (time order by default)

    /** Optional region a granular consumer should take grains from (default: the play range). */
    std::optional<FrameRange> grainRegion;

    int playKey() const noexcept { return assignedKey >= 0 ? assignedKey : rootMidi; }
    FrameRange playRange() const noexcept { return { attackStart, sourceEnd }; }
};

/** How the analysis was run (enough to know when a cached atlas is stale). */
struct AnalysisInfo
{
    std::string method;
    int version = 0;
    double a4Hz = 440.0;
    double tuningOffsetCents = 0.0;
    std::string settingsDigest;

    // What the analysis found about the recording as a whole (Phase 4; empty / 0 for
    // hand-made atlases). Hints only: playback never reads them.
    /** "monophonic" (one clear pitch at a time), "uncertain", "unsuitable" (mostly unpitched,
        polyphonic or noise: the notes are unreliable) or "silent". Empty: not analysed. */
    std::string suitability;
    double analysedSeconds = 0.0;
    double pitchedSeconds = 0.0;        ///< sound with a confident pitch
    /** Stretches of confidently pitched sound outside every note (>= 100 ms each, source
        frames): possible missed notes. */
    std::vector<FrameRange> uncovered;
};

/**
    How the atlas plays on the keyboard (FILL KEYBOARD). Each recorded key plays its own notes.
    With `fill`, a key without notes borrows the nearest recorded key within
    `maxTransposeSemitones` (ties: the lower key, deterministically). A key out of reach stays
    silent: no voice is started and nothing is stolen.
*/
struct Mapping
{
    bool fill = true;
    double maxTransposeSemitones = 12.0;
    /** Fine tuning: play each note in tune (its cents corrected) or as recorded. */
    bool correctTuning = true;
    bool operator== (const Mapping&) const = default;
};

struct NoteAtlas
{
    int schemaVersion = currentSchemaVersion;
    std::string atlasId;
    std::string createdBy;
    std::vector<SourceAsset> sources;
    AnalysisInfo analysis;
    Mapping mapping;
    std::vector<NoteEvent> notes;

    const SourceAsset* findSource (const std::string& assetId) const noexcept;
    const NoteEvent* findNote (const std::string& noteId) const noexcept;
    NoteEvent* findNote (const std::string& noteId) noexcept;
    int indexOf (const std::string& noteId) const noexcept;
    /** A new note id not used yet ("n0000", "n0001", ...): one past the highest in use. */
    std::string nextNoteId() const;
    /** Notes sorted by source and start (stable). */
    void sortByTime();
};

//==============================================================================
struct ValidationReport
{
    std::vector<std::string> errors;    ///< the atlas cannot be played safely as is
    std::vector<std::string> warnings;  ///< it can, but something is odd
    bool ok() const noexcept { return errors.empty(); }
};

/** Ids, references, marker order, ranges against each source's length, pitch sanity. Never throws. */
ValidationReport validate (const NoteAtlas& atlas);
/** Clamps every note's markers into a valid order inside its source. Returns how many changed. */
int repair (NoteAtlas& atlas);

struct RoundedPitch
{
    int key = 60;
    double cents = 0.0;
};
/** Rounds a fractional MIDI pitch to a key: exactly +50 cents stays on the lower key. */
RoundedPitch roundPitch (double midiPitch, double tuningOffsetCents = 0.0) noexcept;
/** Recomputes rootMidi / cents for notes whose pitch the user did not correct, and renumbers
    variation indices per group in time order. */
void remapNotes (NoteAtlas& atlas);
/** The round-robin group a note belongs to: its explicit group, else its play key and layer. */
std::string groupOf (const NoteEvent& note);
/** Included note indices per key (0..127), each in (velocity layer, variation) order. */
std::array<std::vector<int>, 128> notesByKey (const NoteAtlas& atlas);

enum class RelinkResult
{
    exact,         ///< same content hash
    compatible,    ///< different bytes, same rate / channels / length
    incompatible,  ///< positions would be wrong
};
RelinkResult checkRelink (const SourceAsset& source, const std::string& candidateHash, double sampleRate, int numChannels,
                          std::int64_t numFrames) noexcept;

const char* toString (Articulation) noexcept;
std::optional<Articulation> articulationFromString (const std::string&) noexcept;
const char* toString (AssetOrigin) noexcept;
std::optional<AssetOrigin> assetOriginFromString (const std::string&) noexcept;

} // namespace osp::atlas
