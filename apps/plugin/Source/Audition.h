#pragma once

#include "LibraryService.h"
#include "library/PreviewEngine.h"

#include <juce_events/juce_events.h>

#include <array>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <mutex>
#include <string>
#include <vector>

namespace osp::plugin
{

/** The Library's per-user settings (`<Library>/settings.json`, schemaVersion 1): never part of
    a patch or a project. Message thread. */
struct LibrarySettings
{
    static constexpr int schemaVersion = 1;
    float previewGainDb = library::PreviewEngine::defaultGainDb;

    static juce::File file();
    static LibrarySettings load();
    bool save() const;
};

/**
    Hearing Library sounds before they change the instrument (Stage 4, docs/library/preview.md):
    the browser's own preview, the audition tray's A, B and C, and loading sounds into the
    instrument (one, or the tray's combination).

    A sound is found through the catalog (store first, then the places it was seen, by its
    bytes), decoded and prepared on a background thread of its own, then handed to the
    PreviewEngine on the message thread. Previewing never touches the instrument: no
    parameter, no layer, no undo step. Loading goes through the processor's own loader as one
    recoverable step (the patch before it is kept).

    Message thread only; completion callbacks arrive on the message thread.
*/
class Audition
{
public:
    /** One Library sound for a layer: where its bytes are now, the name the instrument shows
        and where it came from (so the catalog record and the card stay the sound's own, even
        when the bytes are the managed copy). */
    struct LayerLoad
    {
        int layer = 0;
        juce::File file;
        juce::String filename, originalPath;
        std::optional<double> rootMidi;   ///< the root the user gave it in the Library (else the analysis decides)
    };
    /** Loads sounds into the instrument as one recoverable step named `label`. */
    using LoadHook = std::function<bool (const std::vector<LayerLoad>&, const juce::String& label)>;
    using Done = std::function<void (bool ok, const juce::String& message)>;

    Audition (library::PreviewEngine& engine, LibraryService& library, juce::File sampleStore, LoadHook load);
    ~Audition();

    static constexpr int numTraySlots = 3;

    struct Slot
    {
        std::string assetId;
        juce::String name;
        bool loading = false;
        juce::String error;                                ///< why it cannot be heard (missing, unreadable)
        std::shared_ptr<const library::PreviewSound> sound;
        juce::File file;                                   ///< where its bytes are (resolved)
    };

    /** The browser's preview: the sound plays as soon as it is ready (a newer request wins). */
    void preview (const std::string& assetId);
    /** Prepares the browser's preview without playing it (a selected row): play() then starts
        it at once. */
    void cue (const std::string& assetId);
    /** A file outside the catalog (a scan result, the Inbox): previewed the same way. */
    void previewFile (const juce::File& file);
    /** Tray slot 0..2 (A, B, C): holds the sound for auditioning and committing. */
    void setSlot (int slot, const std::string& assetId);
    void clearSlot (int slot);
    /** Plays tray slots (bit 0 = A) or, with mask 1 << browserSlot, the browser's preview. */
    void play (unsigned slotMask);
    void stop();
    const Slot& slot (int index) const;   ///< 0..2 the tray, 3 the browser preview
    bool trayEmpty() const;
    double playheadSeconds (int slot) const { return engine.playheadSeconds (slot); }
    bool isSounding() const { return engine.isSounding(); }

    void setGainDb (float db);
    float gainDb() const { return engine.gainDb(); }

    /** One Library sound into a layer (0..2), recoverable. */
    void loadIntoLayer (const std::string& assetId, int layer, Done done);
    /** "Load combination": every tray slot that holds a sound goes to its own layer (A to A,
        B to B, C to C; empty tray slots leave their layer as it is). Every sound is resolved
        first: if one cannot be found, nothing changes. Recoverable as one step. */
    void commit (Done done);

    /** Something shown changed (a sound became ready, failed, was cleared, an overview came). */
    std::function<void()> onChange;

    /** A small waveform overview (`bins` max-abs values, 0..1) for a Library sound's row: from
        memory, or made in the background (onChange tells when it is there). */
    static constexpr int overviewBins = 48;
    const std::vector<float>* overview (const std::string& assetId);

    /** Runs what the background threads finished (a sound ready, a load resolved): the
        processor's timer calls it on the message thread. */
    void deliver();
    /** Waits for background work (tests); deliver() then applies it. */
    bool waitUntilIdle (int milliseconds);

private:
    struct Found
    {
        bool ok = false;
        juce::File file;
        juce::String name, error, filename, originalPath;
        std::optional<double> root;
        std::string contentHash;
    };
    /** Library thread: where a sound is now, its name and root. */
    static Found find (library::Catalog& catalog, const std::filesystem::path& store, const std::string& assetId);
    void prepare (int slot, const std::string& assetId, std::optional<juce::File> file, bool playWhenReady);
    void changed();
    /** From a background thread: run on the message thread at the next deliver(). */
    void post (std::function<void()> onMessageThread);
    std::map<std::string, std::vector<float>> overviews;   ///< by asset id (message thread)
    std::set<std::string> overviewsPending;
    std::mutex doneMutex;
    std::vector<std::function<void()>> finishedWork;

    library::PreviewEngine& engine;
    LibraryService& library;
    juce::File store;
    LoadHook loadHook;
    std::array<Slot, library::PreviewEngine::numSlots> slots;
    std::array<std::uint64_t, library::PreviewEngine::numSlots> requests {};
    LibrarySettings settings;
    juce::ThreadPool decoder { juce::ThreadPoolOptions().withThreadName ("OSP Preview").withNumberOfThreads (1).withDesiredThreadPriority (juce::Thread::Priority::low) };
    std::shared_ptr<bool> alive = std::make_shared<bool> (true);   ///< callbacks after destruction do nothing
};

} // namespace osp::plugin
