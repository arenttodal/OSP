#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace osp::atlas
{

/**
    The Note Atlas (docs/andor-lab/note-atlas.md): a versioned description of source audio
    and its musical interpretation, shared by ANDOR Phrase (which produces it) and ANDOR Grain
    (which can consume it). It is plain data: no JUCE, no editor, no host, no singleton, no
    voice. Positions are absolute frames in the source file's own sample rate; nothing is
    stored as rounded UI time.

    Serialisation lives in andor/io/NoteAtlasJson (JUCE-backed JSON), like the analysis report.
*/
constexpr int currentSchemaVersion = 1;

/** One audio file an atlas refers to. The file itself is never embedded in the atlas. */
struct SourceAsset
{
    std::string assetId;            ///< stable id within the atlas ("src0")
    std::string contentHash;        ///< "sha256:<hex>" of the file's bytes: the fingerprint
    std::string fileName;           ///< display name
    std::string relativePath;       ///< relative to the atlas file (portable bundles); may be empty
    std::string lastKnownPath;      ///< absolute path when the atlas was written (fallback)
    std::string managedCopy;        ///< optional: the managed sample store's copy (by hash)
    double sampleRate = 0.0;
    int numChannels = 0;
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
};

enum class Articulation
{
    sustained,  ///< long, stable: loops well
    short_,     ///< staccato / plucked: plays as recorded
    legato,     ///< joined to a neighbour without a gap
    noisy,      ///< weakly voiced (breath, consonant): pitch unreliable
};

/** What the user changed by hand. An analysis re-run must never silently undo these. */
struct Corrections
{
    bool pitch = false;       ///< the root / pitch was set by the user
    bool boundaries = false;  ///< start / end / sustain markers moved by the user
    bool key = false;         ///< reassigned to another key
    bool group = false;       ///< moved to another variation group
    bool split = false;       ///< made by splitting a region
    bool merged = false;      ///< made by merging regions

    bool any() const noexcept { return pitch || boundaries || key || group || split || merged; }
};

/**
    One detected (or user-made) note region. All positions are source frames.

    sourceStart <= attackStart <= sustain.start <= sustain.end <= releaseStart <= sourceEnd.
    attackStart is where playback should begin to keep the attack (consonant, bow, pick);
    sustain is the pitch-stable body (where loops and grains belong); release is the tail.
*/
struct NoteEvent
{
    std::string id;                    ///< stable within the atlas ("n0007"); survives edits
    std::string sourceId;              ///< SourceAsset::assetId
    std::int64_t sourceStart = 0;
    std::int64_t sourceEnd = 0;
    std::int64_t attackStart = 0;
    FrameRange sustain;
    std::int64_t releaseStart = 0;
    std::int64_t fadeInFrames = 0;     ///< non-destructive fade at attackStart
    std::int64_t fadeOutFrames = 0;    ///< non-destructive fade before sourceEnd

    double pitchHz = 0.0;              ///< robust central pitch of the voiced body
    double midiPitch = 0.0;            ///< the same as a fractional MIDI note (A4 reference below)
    int rootMidi = 60;                 ///< the key the note is "at" (rounded under the tuning rule)
    double centsOffset = 0.0;          ///< midiPitch - rootMidi, in cents (signed)
    double pitchStabilityCents = 0.0;  ///< robust spread of the body's pitch around the centre
    double vibratoDepthCents = 0.0;    ///< expressive deviation, kept apart from the note identity
    double vibratoRateHz = 0.0;
    double voicing = 0.0;              ///< fraction of the body's frames that are voiced, 0..1
    double confidence = 0.0;           ///< overall trust in pitch and boundaries, 0..1
    double peakDb = -120.0;            ///< dBFS
    double rmsDb = -120.0;             ///< dBFS over the body
    Articulation articulation = Articulation::sustained;

    std::string analysisMethod;        ///< e.g. "yin-viterbi"
    int analysisVersion = 0;

    Corrections corrections;
    bool included = true;              ///< false: kept in the atlas, not playable
    int assignedKey = -1;              ///< -1: plays at rootMidi; else the user's key
    std::string variationGroup;        ///< group id; empty = by key
    int variationIndex = 0;            ///< order within its group (time order by default)

    /** Optional region a granular consumer should take grains from (default: sustain). */
    std::optional<FrameRange> grainRegion;

    /** The key this note answers to. */
    int playKey() const noexcept { return assignedKey >= 0 ? assignedKey : rootMidi; }
    /** The pitch it sounds at when played untransposed (fractional MIDI). */
    double soundingMidi() const noexcept { return midiPitch; }
    FrameRange playRange() const noexcept { return { attackStart, sourceEnd }; }
    FrameRange grainRange() const noexcept { return grainRegion.value_or (sustain.isEmpty() ? playRange() : sustain); }
};

/** How the analysis was run (enough to know when a cached atlas is stale). */
struct AnalysisInfo
{
    std::string method;              ///< pitch backend
    int version = 0;                 ///< analysis algorithm version (bump invalidates caches)
    double a4Hz = 440.0;             ///< tuning reference used for rounding
    double tuningOffsetCents = 0.0;  ///< global offset applied before rounding (auto tuning)
    std::string settingsDigest;      ///< digest of the analysis settings
};

struct NoteAtlas
{
    int schemaVersion = currentSchemaVersion;
    std::string atlasId;             ///< stable id, e.g. derived from the source hash + creation
    std::string createdBy;           ///< "ANDOR Phrase 0.1.0"
    std::vector<SourceAsset> sources;
    AnalysisInfo analysis;
    std::vector<NoteEvent> notes;

    const SourceAsset* findSource (const std::string& assetId) const noexcept;
    const NoteEvent* findNote (const std::string& noteId) const noexcept;
    NoteEvent* findNote (const std::string& noteId) noexcept;
    int indexOf (const std::string& noteId) const noexcept;

    /** A new note id not used yet ("n0000", "n0001", ...). */
    std::string nextNoteId() const;

    /** Notes sorted by source start (stable). */
    void sortByTime();
};

//==============================================================================
/** Integrity problems found by validate(). Each is a human-readable sentence. */
struct ValidationReport
{
    std::vector<std::string> errors;    ///< the atlas cannot be played safely as is
    std::vector<std::string> warnings;  ///< it can, but something is odd
    bool ok() const noexcept { return errors.empty(); }
};

/**
    Checks ids, source references, frame ordering and ranges against each source's length,
    pitch sanity and duplicate ids. Never throws.
*/
ValidationReport validate (const NoteAtlas& atlas);

/** Clamps every note's markers into a valid order inside its source (after edits or a
    relink to a shorter file). Returns how many notes changed. */
int repair (NoteAtlas& atlas);

/** Rounds a fractional MIDI pitch to a key under the tuning rule; returns {key, cents}. */
struct RoundedPitch
{
    int key = 60;
    double cents = 0.0;
};
RoundedPitch roundPitch (double midiPitch, double tuningOffsetCents = 0.0) noexcept;

/** Recomputes rootMidi / centsOffset from midiPitch for notes whose pitch was not corrected
    by the user, and renumbers variation indices per group in time order. */
void remapNotes (NoteAtlas& atlas);

/** The group a note belongs to: its explicit group or its play key. */
std::string groupOf (const NoteEvent& note);

/** Note indices per key (0..127) for included notes, each list in variation order. */
std::array<std::vector<int>, 128> notesByKey (const NoteAtlas& atlas);

//==============================================================================
/** Relinking a source to a file found elsewhere. */
enum class RelinkResult
{
    exact,         ///< same content hash
    compatible,    ///< different bytes, same rate / channels / length (re-encode, metadata edit)
    incompatible,  ///< length or rate differ: positions would be wrong
};

/** Compares a candidate file's identity with a source. */
RelinkResult checkRelink (const SourceAsset& source, const std::string& candidateHash, double sampleRate,
                          int numChannels, std::int64_t numFrames) noexcept;

const char* toString (Articulation) noexcept;
std::optional<Articulation> articulationFromString (const std::string&) noexcept;

} // namespace osp::atlas
