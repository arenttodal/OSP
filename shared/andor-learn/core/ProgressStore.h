#pragma once

#include <juce_core/juce_core.h>

#include <functional>
#include <map>

namespace andor::learn
{

/** One lesson's progress. `status`: "complete", "in-progress" (with the step to resume). */
struct LessonProgress
{
    int revision = 0;
    juce::String status;
    juce::String completedAt;   ///< ISO 8601, UTC
    juce::String lastStep;      ///< the step to resume from (in progress)
};

/** A product's Learn preferences: the musician's, never a session's or a preset's. */
struct ProgressData
{
    static constexpr int schemaVersion = 1;
    juce::String product;
    bool firstInviteSeen = false;
    bool learnModeEnabled = false;
    std::map<juce::String, LessonProgress> lessons;
};

/**
    The per-user Learn file of one product: <user application data>/ANDOR/Learn/<product>.json
    (macOS: ~/Library/Application Support/ANDOR/Learn), or $ANDOR_LEARN_DIR/<product>.json.
    Kept apart from the processor, its state, presets and Interface.xml: it never reaches a DAW
    project. No samples, MIDI, file or project names, nothing sent anywhere.

    Message thread only (never the audio thread). Every change re-reads the file, applies the
    change and writes a temporary file that replaces it, under one process-wide lock, so two
    editors (or a second plugin instance) never lose each other's updates; another process
    writing at the same instant loses at most its own last change. A missing or corrupt file
    reads as empty (a corrupt one is kept beside it as .corrupt); unknown fields written by a
    later version are kept. If writing fails, lessons still run.
*/
class ProgressStore
{
public:
    explicit ProgressStore (juce::String productId);

    /** The folder: $ANDOR_LEARN_DIR, else the platform's user application data/ANDOR/Learn. */
    static juce::File directory();
    juce::File file() const;

    ProgressData read() const;
    /** Applies `change` to the latest data and writes it. False if the write failed. */
    bool update (const std::function<void (ProgressData&)>& change);
    /** Forgets every lesson's progress and the invitation (Reset learning progress). */
    bool reset();

    /** For tests: the JSON written for some data. */
    static juce::String toJson (const ProgressData&, const juce::var& keep = {});
    static ProgressData fromJson (const juce::var&, const juce::String& product);

    /** Whether some editor of this product shows the first-run invitation (process-wide). */
    static bool claimInvitation (const juce::String& productId);
    static void releaseInvitation (const juce::String& productId);

private:
    juce::var readVar (bool& corrupt) const;
    juce::String product;
};

} // namespace andor::learn
