#pragma once

#include "LoadedInstrument.h"
#include "library/Catalog.h"

#include <juce_core/juce_core.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace osp::plugin
{

/**
    The plugin's door to the Library catalog (docs/library/architecture.md). Every call returns
    at once: the work runs on one low-priority background thread that owns the only catalog
    connection of this instance (opened there on first use, so a slow disk never stalls the
    editor or the audio). Nothing here is ever called from the audio thread.

    The instrument never depends on it: if the catalog cannot be opened, sounds still load and
    projects still recall (the sample store is the store of record).
*/
class LibraryService
{
public:
    explicit LibraryService (juce::File sampleStoreDirectory);
    ~LibraryService();

    /** A sound reached a layer: its record (the existing one for this content and place) and,
        when the user loaded it (not a project recall), a "loaded" entry in its history. */
    void soundLoaded (const LoadedInstrument& instrument, int layer, bool byUser);
    /** A preset or template file was written: indexed with the sounds it needs. */
    void presetSaved (const juce::File& file, library::AssetType type, std::vector<std::string> soundHashes, int layers, int stateVersion);

    /** Runs `task` on the Library thread with the catalog (skipped when it cannot be opened). */
    void post (std::function<void (library::Catalog&)> task);
    /** Runs `task` on the Library thread and waits for it (a user action that needs the
        answer: an export's rights check). False when the catalog is unavailable or it took
        longer than `milliseconds` (the task may then still run later: it must own what it
        touches, e.g. through a shared_ptr). Never from the audio thread. */
    bool runAndWait (std::function<void (library::Catalog&)> task, int milliseconds = 5000);
    /** Waits until the queued work is done (tests, shutdown). */
    bool waitUntilIdle (int milliseconds);
    /** Why the catalog is not available ("" when it is or has not been tried). */
    juce::String unavailableReason() const;

private:
    library::Catalog* catalog();   ///< the Library thread only

    juce::File store;
    juce::ThreadPool pool { juce::ThreadPoolOptions().withThreadName ("OSP Library").withNumberOfThreads (1).withDesiredThreadPriority (juce::Thread::Priority::low) };
    std::unique_ptr<library::Catalog> db;
    bool opened = false;
    mutable juce::CriticalSection reasonLock;
    juce::String reason;
};

} // namespace osp::plugin
