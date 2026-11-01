#pragma once

#include "MemoDecoder.h"

#include <juce_core/juce_core.h>

#include <atomic>
#include <map>
#include <optional>
#include <vector>

namespace osp::memolab
{

/** Where Memo Lab keeps its own things (never next to the recordings). */
juce::File appDataFolder();
/** Where macOS keeps the local copies of iCloud Voice Memos (an Apple detail, not an API). */
juce::File voiceMemosFolder();

/**
    One recording in an approved folder. The file is only ever read. A title comes from Voice
    Memos' own list when that can be read (read-only), else from the file name.
*/
struct Recording
{
    enum class State { ready, notDownloaded, changing, unreadable };

    juce::File file;
    juce::String title;
    juce::Time date;
    double seconds = 0.0;
    double sampleRate = 0.0;
    int channels = 0;
    State state = State::ready;
    juce::String problem;     ///< why it is not ready
    juce::File folder;        ///< the approved folder it is in
};

/** What happened in one approved folder (it is reported, never assumed). */
struct FolderReport
{
    juce::File folder;
    bool exists = false, readable = false;
    int recordings = 0;
    juce::String message;    ///< a plain sentence for the user
};

struct ScanResult
{
    std::vector<Recording> recordings;   ///< newest first
    std::vector<FolderReport> folders;
};

/**
    The folders the user approved. Voice Memos' own folder is offered once (on first launch)
    and kept like any other; any folder can be removed. Stored in
    appDataFolder()/settings.json.
*/
class SourceSettings
{
public:
    explicit SourceSettings (juce::File settingsFile = appDataFolder().getChildFile ("settings.json"));

    const juce::Array<juce::File>& folders() const noexcept { return approved; }
    void addFolder (const juce::File& folder);
    void removeFolder (const juce::File& folder);
    bool offeredVoiceMemos() const noexcept { return voiceMemosOffered; }
    void setOfferedVoiceMemos() { voiceMemosOffered = true; save(); }

private:
    void load();
    void save() const;
    juce::File file;
    juce::Array<juce::File> approved;
    bool voiceMemosOffered = false;
};

/** Reads a folder: does it exist, can it be listed (macOS privacy), how many recordings. */
FolderReport checkFolder (const juce::File& folder);

/**
    Lists the recordings in the approved folders:
    - newest first;
    - at most a few levels deep;
    - hidden entries and packages skipped;
    - never following a link out of its folder;
    - nothing outside the approved roots.

    `now` decides what counts as still being written (modified in the last seconds). Headers
    are read; nothing is decoded. Stops early when `cancelled` becomes true.
*/
ScanResult scanFolders (const juce::Array<juce::File>& roots, const std::atomic<bool>& cancelled, juce::Time now = juce::Time::getCurrentTime());

/** A title for a Voice Memos file name ("20241030 143012-AB12CD34" -> "30 Oct 2024, 14:30"),
    or the name itself. */
juce::String titleFromFileName (const juce::String& name);
/** The moment in a Voice Memos file name (local time), if it has one. */
std::optional<juce::Time> dateFromFileName (const juce::String& name);

/** Voice Memos' own titles, file name -> title, read from its database without writing,
    locking or migrating it. Empty when it cannot be read (it is optional). */
std::map<juce::String, juce::String> voiceMemosTitles (const juce::File& recordingsFolder);

} // namespace osp::memolab
