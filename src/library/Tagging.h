#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace osp::library
{

/**
    Tags inferred from a sound's file name and folders (Stage 6.1). Inferred tags are
    evidence, not facts: each carries a confidence and its source, and the catalog keeps them
    apart from the user's own tags (a user's removal survives every rescan).

    Pure and deterministic: the same name always gives the same tags.
*/
struct InferredTag
{
    std::string tag;
    double confidence = 0.5;
};

struct NameEvidence
{
    std::vector<InferredTag> tags;
    std::string type;                 ///< a sound Type (Bass, Keys, Pluck, Pad, Percussion, Vocal, Texture), or empty
    double typeConfidence = 0.0;
    std::optional<double> rootMidi;   ///< a note name with octave in the name ("Pad C3", "bass_F#1")
    bool saysLoop = false;            ///< "loop", "bpm", "120bpm": a hint for the classifier
    bool saysOneShot = false;         ///< "one shot", "oneshot", "hit", "shot"
};

/** The words of a name: split on separators, digits and case changes, lower case. */
std::vector<std::string> nameWords (const std::string& name);

/** Evidence from a file name (without its folder). */
NameEvidence evidenceFromName (const std::string& fileName);

/** Evidence from the folders between `root` and the file (each folder name, nearer folders
    weigh more); `root` itself does not count. Lower confidence than the name's own. */
NameEvidence evidenceFromFolders (const std::filesystem::path& file, const std::filesystem::path& root);

/** "C3" -> 48 (C4 = 60), "F#1", "Bb4", "a-1"; nothing for anything else. */
std::optional<int> noteFromWord (const std::string& word);

} // namespace osp::library
